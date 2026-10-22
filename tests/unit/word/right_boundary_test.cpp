// The Ow piece of the rules after a word closed by っ: a comma or an ellipsis after the っ ends
// the clause; other marks between do not.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_right_boundary_after_tsu) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "犬\t가\tN110\tXXX\t\tGN\t0\t1\t\n"
         "犬\t나\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Ow\"\n"
         "っ\t\tQ110\t\t\tGN\t0\t3\t\n"
         "猫\t이\tN110\t\t\tGN\t0\t4\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"犬っ、猫", "나, 이"},
        {"犬っ……～猫", "나……～이"},
        {"犬っ♪～猫", "가♪～이"},
        {"犬っ★～猫", "가★～이"},
        {"犬っ猫", "가이"},
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
