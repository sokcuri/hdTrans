#include <string>

#include "core/engine/driver.h"
#include "hdtest.h"
#include "rule_fixture.h"

// A column separator is written as typed on the text path and on the raw path, and the
// words on its two sides are translated apart.
HD_TEST(engine_column_gaps) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", fixture::WithRuleBodies(
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "名前\t이름\tN110\t\t\tGN\t0\t1\n"
        "職業\t직업\tN110\t\t\tGN\t0\t2\n")}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string input[] = {"名前　　 職業", "名前\t職業", "名前　職業"};
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, input, memory, {}, {});
    HD_REQUIRE(out.size() == 3);
    HD_CHECK_EQ(out[0], std::string("이름　　 직업"));
    HD_CHECK_EQ(out[1], std::string("이름\t직업"));
    HD_CHECK_EQ(out[2], std::string("이름 직업"));
    HD_CHECK_EQ(internal::TranslateRawLine(*loaded.snapshot, *generated, input[0], {}, {}), std::string("이름　　 직업"));
    HD_CHECK_EQ(internal::TranslateRawLine(*loaded.snapshot, *generated, input[1], {}, {}), std::string("이름\t직업"));
}
