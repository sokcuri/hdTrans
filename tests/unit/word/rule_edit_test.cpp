// Editing one line of the compiled-in rules program changes the selection the word stage makes.
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/engine/rules.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/analyze.h"
#include "core/word/flush.h"
#include "core/word/worker.h"
#include "hdtest.h"
#include "rule_fixture.h"

using namespace hdTrans;

namespace {

// The rules program of the two rows below with the body of rule 2 written as body. The
// rows load with a valid body and the text is edited after, so a body that does not
// parse reaches the rule loader rather than failing the dictionary first.
std::string EditedRules(std::string_view body) {
    std::vector<dict::File> files{{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
        "人気\t인적\tN110\tXXX\t\tGN\t0\t1\t\n"
        "人気\t인기\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Ow\"\n"}};
    const dict::LoadResult loaded = dict::Load(std::move(files));
    if (!loaded.snapshot) return {};
    std::string text = internal::RuleProgramText(*loaded.snapshot);
    const std::string prefix = "\nL 2 ";
    const std::size_t start = text.find(prefix);
    if (start == std::string::npos) return {};
    const std::size_t end = text.find('\n', start + 1);
    text.replace(start + 1, end - start - 1, "L 2 " + std::string(body));
    return text;
}

// The target selected for 人気 in 人気。 under program, with the two rows below.
std::string Selected(const rules::Program& program, word::Selection& choice) {
    std::vector<dict::File> files{{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
        "人気\t인적\tN110\tXXX\t\tGN\t0\t1\t\n"
        "人気\t인기\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Ow\"\n"}};
    const dict::LoadResult loaded = dict::Load(std::move(files));
    if (!loaded.errors.empty() || loaded.snapshot == nullptr) return "(no dictionary)";
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Analysis analysis;
    tokenizer::Tokenize("人気。", tokens);
    word::Analyze(tokens, *loaded.snapshot, {}, word::Settings::Reference(), arena, analysis);
    if (analysis.fault || analysis.sentences.empty()) return "(no analysis)";
    word::Worker& w = *analysis.worker;
    w.chain = analysis.sentences[0].chain;
    w.current = analysis.sentences[0].current;
    w.transformSurface = analysis.sentences[0].transformSurface;
    word::Flusher f{w, program, loaded.snapshot->OutputFamilies()};
    word::Flush(f);
    for (word::Item* it = w.chain.head; it != nullptr; it = it->next) {
        if (it->group->surface != "人気") continue;
        choice = it->choice;
        return std::string(it->EntryText(it->selected));
    }
    return "(no item)";
}

}  // namespace

HD_TEST(rules_edit_changes_selection) {
    struct Case {
        const char* body;  // the new body of rule 2; null for the shipped one
        const char* target;
        word::Provenance layer;
    };
    const Case cases[] = {
        {nullptr, "인기", word::Provenance::LocalRule},
        {"||0", "인적", word::Provenance::FirstXxx},
        {"||1 S+0== \"人\"", "인적", word::Provenance::FirstXxx},
        {"||1 S+0== \"L人\"", "인기", word::Provenance::LocalRule},
        {"||1 &&2 S+0== \"L人\" S-1== \"の\"", "인적", word::Provenance::FirstXxx},
        {"||1 D+0= \"인기도\"", "인기도", word::Provenance::LocalRule},
    };
    for (const Case& c : cases) {
        rules::Program program;
        {
            rules::LoadError error{};
            HD_REQUIRE(rules::Load(EditedRules(c.body ? c.body : "||1 G+0== \"Ow\""), program, error));
        }
        word::Selection choice;
        HD_CHECK_EQ(Selected(program, choice), std::string(c.target));
        HD_CHECK_EQ(static_cast<int>(choice.layer), static_cast<int>(c.layer));
    }
}

// A broken record makes the whole program fail to load, with its line.
HD_TEST(rules_edit_bad_line) {
    rules::Program program;
    rules::LoadError error{};
    HD_CHECK(!rules::Load(EditedRules("||1 S+1==  \"が\""), program, error));
    HD_CHECK(error.line > 0);
}
