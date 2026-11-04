#include <string>

#include "core/engine/driver.h"
#include "hdtest.h"
#include "rule_fixture.h"

// A space typed between two words that parts them (Token::afterSpace) stays a space before a noun
// read after it, whatever script the word before it ends in; a particle after it still attaches,
// and words typed together keep the spacing of compose (spec LAY-20).
HD_TEST(engine_typed_space_before_noun) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", fixture::WithRuleBodies(
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "あんた\t너\tI110\tVHn\t\tGN\t0\t1\n"
        "これ\t이것\tN120\t\t\tGN\t0\t2\n"
        "ミルク\t우유\tN110\t\t\tGN\t0\t3\n"
        "海\t바다\tN110\t\t\tGN\t0\t4\n"
        "が\t(이)가\tP211\t\t\tGN\t0\t5\n")}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const std::string input[] = {"あんた これ", "ミルク これ", "海 これ", "あんた が", "あんたこれ"};
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, input, {}, {});
    HD_REQUIRE(out.size() == 5);
    HD_CHECK_EQ(out[0], std::string("너 이것"));
    HD_CHECK_EQ(out[1], std::string("우유 이것"));
    HD_CHECK_EQ(out[2], std::string("바다 이것"));
    HD_CHECK(out[3].find(' ') == std::string::npos);  // the particle attaches
    HD_CHECK_EQ(out[4], std::string("너이것"));         // typed together
}
