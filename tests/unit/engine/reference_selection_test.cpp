#include <string>

#include "core/engine/driver.h"
#include "hdtest.h"
#include "rule_fixture.h"

namespace {

int scoreCalls = 0;

int PreferFirst(const hdTrans::word::Item&, int candidate, const hdTrans::word::SelectContext&) {
    scoreCalls++;
    return candidate == 0 ? 1 : 0;
}

void CountShadows(void* context, int, int, const hdTrans::word::Chain& chain) {
    auto& count = *static_cast<std::size_t*>(context);
    for (const auto* item = chain.head; item != nullptr; item = item->next) count += item->shadowChoices.size();
}

}  // namespace

HD_TEST(engine_reference_disables_selection_options) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", fixture::WithRuleBodies(
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "東京\t동경\tN130\tQPNXXX\t\tGN\t0\t1\n"
        "東京\t도쿄\tN130\tQCT\t\tGN\t9\t2\n")}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const word::Scorer scorers[] = {{"first", PreferFirst}};
    internal::TranslationOptions options;
    options.reference = true;
    options.scorers = scorers;
    options.shadowScorers = scorers;
    options.selectShadow = true;
    options.selectRules = true;
    options.wordImprovements.documentSenses = true;
    std::size_t shadows = 0;
    internal::StageObserver observer;
    observer.context = &shadows;
    observer.resolved = CountShadows;
    const std::string input[] = {"東京、", "東京。"};
    scoreCalls = 0;
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, input, observer, options);
    HD_REQUIRE(out.size() == 2);
    HD_CHECK_EQ(out[0], std::string("도쿄,"));
    HD_CHECK_EQ(out[1], std::string("동경."));
    internal::TranslationOptions plain;
    plain.reference = true;
    const auto expectedRaw = internal::TranslateRawLine(*loaded.snapshot, *generated, input[0], {}, plain);
    HD_CHECK(!expectedRaw.empty());
    HD_CHECK_EQ(internal::TranslateRawLine(*loaded.snapshot, *generated, input[0], observer, options), expectedRaw);
    HD_CHECK_EQ(scoreCalls, 0);
    HD_CHECK_EQ(shadows, std::size_t(0));

    options.reference = false;
    options.selectShadow = false;
    const auto changed = internal::TranslateDocument(*loaded.snapshot, *generated, input, {}, options);
    HD_REQUIRE(changed.size() == 2);
    HD_CHECK_EQ(changed[0], std::string("동경,"));
    HD_CHECK(scoreCalls > 0);
}

HD_TEST(engine_individual_improvement_outputs) {
    using namespace hdTrans;
    const struct {
        bool word::Improvements::* flag;
        const char* input;
        const char* rows;
        const char* plain;
        const char* off;
        const char* on;
    } cases[] = {
        {&word::Improvements::extraBuiltin, "がぼっ！", "", "", "가봇!", "커헉!"},
        {&word::Improvements::literalCleanup, "何度も",
         "何度も\t몇번\tN110\tXXX\t\tGN\t0\t1\n", "", "몇번", "몇 번"},
        {&word::Improvements::exclamationNa, "日が暮れるな。", "",
         "日が暮れるな\t저물지 마\tphrase\n日が暮れるなあ\t저무는구나\tphrase\n",
         "저물지 마.", "저무는구나."},
        {&word::Improvements::dialectForms, "待っとった。", "",
         "待っとった\t기다렸다\tphrase\n待っていた\t기다리고 있었다\tphrase\n",
         "기다렸다.", "기다리고 있었다."},
    };
    for (const auto& c : cases) {
        const auto loaded = dict::Load({
            {0, "base.tsv", fixture::WithRuleBodies(std::string("key\tko\tpos\tsem\tgram\tdict\trule\tid\n") + c.rows)},
            {0, "plain.tsv", c.plain},
        });
        HD_REQUIRE(loaded.errors.empty());
        HD_REQUIRE(loaded.snapshot != nullptr);
        HD_CHECK(std::string(c.off) != c.on);
        for (bool enabled : {false, true}) {
            const auto generated = internal::NewGenerated(*loaded.snapshot);
            internal::TranslationOptions options;
            options.wordImprovements = word::Improvements::Reference();
            options.wordImprovements.*(c.flag) = enabled;
            const std::string input[] = {c.input};
            const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, input, {}, options);
            HD_REQUIRE(out.size() == 1);
            HD_CHECK_EQ(out[0], std::string(enabled ? c.on : c.off));
        }
    }
}
