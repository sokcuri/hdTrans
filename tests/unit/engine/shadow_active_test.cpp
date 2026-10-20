#include <string>
#include <vector>

#include "core/engine/driver.h"
#include "hdtest.h"
#include "rule_fixture.h"

namespace {

struct Observed {
    std::vector<hdTrans::word::Selection> choices;
    std::size_t shadows = 0;
};

void Observe(void* context, int, int, const hdTrans::word::Chain& chain) {
    auto& out = *static_cast<Observed*>(context);
    for (const auto* item = chain.head; item != nullptr; item = item->next) {
        if (item->group->surface == "東京") out.choices.push_back(item->choice);
        out.shadows += item->shadowChoices.size();
    }
}

}  // namespace

HD_TEST(engine_shadow_preserves_active_document_senses) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", fixture::WithRuleBodies(
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "東京\t동경\tN130\tQPNXXX\t\tGN\t0\t1\n"
        "東京\t도쿄\tN130\tQCT\t\tGN\t9\t2\n")}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const std::string input[] = {"東京、", "東京。"};
    for (const bool shadow : {false, true}) {
        internal::TranslationOptions options;
        options.wordImprovements.documentSenses = true;
        options.selectShadow = shadow;
        internal::Memory memory;
        Observed observed;
        internal::StageObserver observer;
        observer.context = &observed;
        observer.resolved = Observe;
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, input, memory, observer, options);
        HD_REQUIRE(out.size() == 2);
        HD_CHECK_EQ(out[0], std::string("도쿄,"));
        HD_CHECK_EQ(out[1], std::string("도쿄."));
        HD_REQUIRE(observed.choices.size() == 2);
        HD_CHECK(observed.choices[0].layer == word::Provenance::LocalRule);
        HD_CHECK(observed.choices[1].layer == word::Provenance::Scorer);
        HD_CHECK_EQ(observed.choices[1].id, 0);
        HD_CHECK_EQ(observed.shadows > 0, shadow);
    }
}

HD_TEST(engine_shadow_preserves_active_scorer_ids) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", fixture::WithRuleBodies(
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "東京\t동경\tN130\tQPNXXX\t\tGN\t0\t1\n"
        "東京\t도쿄\tN130\tQCT\t\tGN\t9\t2\n")}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const word::Scorer active[] = {{"first", [](const word::Item&, int candidate, const word::SelectContext&) {
        return candidate == 0 ? 1 : 0;
    }}};
    const std::string input[] = {"東京、", "東京。"};
    for (const bool shadow : {false, true}) {
        internal::TranslationOptions options;
        options.scorers = active;
        options.selectRules = true;
        options.selectShadow = shadow;
        internal::Memory memory;
        Observed observed;
        internal::StageObserver observer;
        observer.context = &observed;
        observer.resolved = Observe;
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, input, memory, observer, options);
        HD_REQUIRE(out.size() == 2);
        HD_CHECK_EQ(out[0], std::string("동경,"));
        HD_CHECK_EQ(out[1], std::string("동경."));
        HD_REQUIRE(observed.choices.size() == 2);
        HD_CHECK(observed.choices[0].layer == word::Provenance::Scorer);
        HD_CHECK_EQ(observed.choices[0].id, 0);
        HD_CHECK_EQ(observed.shadows > 0, shadow);
    }
}
