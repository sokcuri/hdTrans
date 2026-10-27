// A local rule is the ctx column of the dictionary rows that name it: editing that column
// changes the selection the word stage makes.
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/analyze.h"
#include "core/word/flush.h"
#include "core/word/worker.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

// Two rows of 人気; the second names local rule 2 and carries body as its ctx.
dict::LoadResult Load(std::string_view body, std::string_view more = {}) {
    std::vector<dict::File> files{{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
        "人気\t인적\tN110\tXXX\t\tGN\t0\t1\t\n"
        "人気\t인기\tN110\t\t\tGN\t2\t2\t" + std::string(body) + "\n" + std::string(more)}};
    return dict::Load(std::move(files));
}

// The target selected for 人気 in 人気。 with rule 2's body written as body.
std::string Selected(std::string_view body, word::Selection& choice) {
    const dict::LoadResult loaded = Load(body);
    if (!loaded.errors.empty() || loaded.snapshot == nullptr) return "(no dictionary)";
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    if (!generated->hasRules) return "(no rules)";
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
    word::Flusher f{w, generated->rules, loaded.snapshot->OutputFamilies()};
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
        const char* body;  // the ctx of the row that names rule 2
        const char* target;
        word::Provenance layer;
    };
    const Case cases[] = {
        {"||1 G+0== \"Ow\"", "인기", word::Provenance::LocalRule},
        {"||0", "인적", word::Provenance::FirstXxx},
        {"||1 S+0== \"人\"", "인적", word::Provenance::FirstXxx},
        {"||1 S+0== \"L人\"", "인기", word::Provenance::LocalRule},
        {"||1 &&2 S+0== \"L人\" S-1== \"の\"", "인적", word::Provenance::FirstXxx},
        {"||1 D+0= \"인기도\"", "인기도", word::Provenance::LocalRule},
    };
    for (const Case& c : cases) {
        word::Selection choice;
        HD_CHECK_EQ(Selected(c.body, choice), std::string(c.target));
        HD_CHECK_EQ(static_cast<int>(choice.layer), static_cast<int>(c.layer));
    }
}

// A body the rule parser rejects, and a second body for a rule number, fail the dictionary at
// the row's line.
HD_TEST(rules_edit_bad_body) {
    struct Case {
        const char* body;
        const char* more;  // a further row
        int line;
        const char* message;
    };
    const Case cases[] = {
        {"||1 S+1==  \"が\"", "", 3, "the rule body does not parse: empty token"},
        {"||1 &&2 S+1== \"が\" D+0= \"먹&ZZ@\"", "", 3,
         "the rule body does not parse: the assigned marker family is not in the marker tables"},
        {"||1 &&2 S+1== \"が\" D+0= \"먹&A1@다\"", "", 3,
         "the rule body does not parse: a marker family does not end the assigned text"},
        {"||1 &&2 S+1== \"が\" Y+0= \"6주었으면 &AV@\"", "", 3,
         "the rule body does not parse: an ending text holds a marker family"},
        {"||1 G+0== \"Ow\"", "人気\t인기도\tN110\t\t\tGN\t2\t3\t||0\n", 4, "a second body for the local rule"},
    };
    for (const Case& c : cases) {
        const dict::LoadResult loaded = Load(c.body, c.more);
        HD_CHECK(loaded.snapshot == nullptr);
        HD_REQUIRE(!loaded.errors.empty());
        HD_CHECK_EQ(loaded.errors[0].line, c.line);
        HD_CHECK_EQ(loaded.errors[0].message, std::string(c.message));
    }
}
