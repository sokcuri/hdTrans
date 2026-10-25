// A verb before a word whose text starts with its own conjugation marker writes only its
// stem, unless a markup tag stands between them (a ruby base and its reading).
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(output_text_gate_stops_at_a_tag) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "割\t&C2@\tM448\t\t\tGN\t0\t1\t\n"
         "わ\t&C2@\tM448\t\t\tGN\t0\t2\t\n"
         "V\t6\tS571\tXXX\t\tGN\t0\t3\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"{rb}割って{/rb}{rt}わって{/rt}", "{rb}깨어{/rb}{rt}깨어{/rt}"},
        {"割ってわって", "깨어"},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
