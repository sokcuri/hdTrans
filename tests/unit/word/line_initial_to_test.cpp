// A と that opens its line, or follows a sentence end on it, quotes what was said before
// it (LineInitialTo).
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_line_initial_to_quotes) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "と\t(와)과\tP200\tXXX\t\tGN\t0\t1\t\n"
         "と\t(이)라고\tP200\t\t\tGN\t0\t2\t\n"
         "私\t나\tN120\t\t\tGN\t0\t3\t\n"
         "雨\t비\tN110\t\t\tGN\t0\t4\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"と私", "라고 나"},
        {"　と私", " 라고 나"},
        {"雨。と私", "비. 라고 나"},
        {"雨と私", "비와 나"},
        // Not a word but other text before it, a bracket, or a hesitation after it.
        {"××と私", "××와 나"},
        {"「と私」", "「와 나」"},
        {"と……", "와……"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
