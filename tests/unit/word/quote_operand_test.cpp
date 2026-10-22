// The Qt piece of the rules: a word inside 「」 or 『』, also in a quote an earlier line of
// the document left open.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_quote_operand) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "彼女\t그녀\tN120\tXXX\t\tGN\t0\t1\t\n"
         "彼女\t걔\tN120\t\t\tGN\t2\t2\t||1 G+0== \"Qt\"\n"
         "が\t(이)가\tP200\t\t\tGN\t0\t3\t\n"
         "来\t오&AH@\tM421\t\t\tGN\t0\t4\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        std::vector<std::string> lines;
        std::size_t at;
        const char* want;
    };
    const Case cases[] = {
        {{"彼女が"}, 0, "그녀가"},
        {{"「彼女が」"}, 0, "「걔가」"},
        {{"『彼女』"}, 0, "『걔』"},
        {{"「あ」彼女が"}, 0, "「아」그녀가"},
        {{"「あ", "彼女が」"}, 1, "걔가」"},
        {{"「あ」", "彼女が"}, 1, "그녀가"},
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, c.lines, memory, {}, {});
        HD_REQUIRE(out.size() == c.lines.size());
        HD_CHECK_EQ(out[c.at], std::string(c.want));
    }
}
