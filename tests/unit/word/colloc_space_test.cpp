// A colloc span whose first words the packs layer empties writes the space before its head only
// where the first word would have had one: after a word of its token, not after a mark, a digit or a
// Latin letter or at the start of a token.
#include <memory>
#include <string>
#include <string_view>
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

std::shared_ptr<const dict::Snapshot> Collocs() {
    std::vector<dict::File> files{
        {0, "base.tsv",
         fixture::WithRuleBodies("key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
                                 "特製\t특제\tN110\t\t\tGN\t0\t1\n"
                                 "肉\t고기\tN110\t\t\tGN\t0\t2\n"
                                 "じゃ\t는\tP200\t\t\tGN\t0\t3\n"
                                 "が\t(이)가\tP200\t\t\tGN\t0\t4\n"
                                 "・\t·\tN600\t\tMk\tGN\t0\t5\n")},
        {0, "pack.tsv",
         "#! layer=pack\n"
         "id\tlemma\tpos\tkind\tcond\ttarget\n"
         "dish\t肉じゃが\tn\tcolloc\t肉じゃが\t고기감자조림\n"}};
    const dict::LoadResult loaded = dict::Load(std::move(files));
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

const rules::Program& Rules(const dict::Snapshot& snapshot) {
    static std::vector<std::shared_ptr<internal::Generated>> kept;
    kept.push_back(internal::NewGenerated(snapshot));
    return kept.back()->hasRules ? kept.back()->rules : internal::RulesProgram();
}

// The Korean the packs layer leaves on the が of the first sentence of text.
std::string Head(const dict::Snapshot& snapshot, std::string_view text) {
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Analysis analysis;
    word::Settings settings = word::Settings::Reference();
    settings.text = true;
    tokenizer::Tokenize(text, tokens);
    word::Analyze(tokens, snapshot, {}, settings, arena, analysis);
    if (analysis.fault || analysis.sentences.empty()) return "fault";
    word::Worker& w = *analysis.worker;
    w.chain = analysis.sentences.front().chain;
    w.current = analysis.sentences.front().current;
    w.transformSurface = analysis.sentences.front().transformSurface;
    word::Flusher f{w, Rules(snapshot), snapshot.OutputFamilies(), {}};
    word::Flush(f);
    for (const word::Item* it = w.chain.head; it != nullptr; it = it->next)
        if (it->group != nullptr && it->group->surface == "が") return std::string(it->EntryText(it->selected));
    return "none";
}

}  // namespace

HD_TEST(word_colloc_space_before_span) {
    HD_REQUIRE(internal::RulesLoaded());
    const auto snapshot = Collocs();
    HD_REQUIRE(snapshot != nullptr);
    HD_CHECK_EQ(Head(*snapshot, "肉じゃが。"), std::string("고기감자조림"));
    HD_CHECK_EQ(Head(*snapshot, "特製肉じゃが。"), std::string(" 고기감자조림"));
    HD_CHECK_EQ(Head(*snapshot, "・肉じゃが。"), std::string("고기감자조림"));
    HD_CHECK_EQ(Head(*snapshot, "特製……肉じゃが。"), std::string("고기감자조림"));
    HD_CHECK_EQ(Head(*snapshot, "3肉じゃが。"), std::string("고기감자조림"));
    HD_CHECK_EQ(Head(*snapshot, "ＤＸ肉じゃが。"), std::string("고기감자조림"));
}
