// A word that spells the continuative or the potential stem of a verb of the dictionary (喜び,
// 頑張れ) before the polite ます is that verb, its records ahead of the longer row; before a
// particle the longer row stays.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_verb_before_polite) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "喜\t기뻐&AV@\tM446\t\t\tGN\t0\t1\t\n"
         "喜び\t기쁨\tI110\t\t\tGN\t0\t2\t\n"
         "頑張\t노력&AV@\tM418\t\t\tGN\t0\t3\t\n"
         "頑張れ\t힘내라\tA9H1\t\t\tGN\t0\t4\t\n"
         "ます\t7니다\tS5:0\t\t\tGN\t0\t5\t\n"
         "を\t(을)를\tP200\t\t\tGN\t0\t6\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"喜びます", "기뻐합니다"},
        {"喜びを", "기쁨을"},
        {"頑張れ", "힘내라"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
