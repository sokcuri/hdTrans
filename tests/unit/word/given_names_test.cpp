// A run of one kanji and a 郎 core, or a kana spelling of one, is one given name: made from its
// reading where the dictionaries lack it, the given-name row where they read it as a full
// name too, and a dictionary word of the same key or a longer one wins.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_ro_given_names) {
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n";
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         header +
             "凶\t흉\tN110\t\tOn\tGN\t0\t1\t\n"
             "三太郎\t멍청이\tN110\tVHn\t\tGN\t0\t2\t\n"
             "が\t(이)가\tP200\t\t\tGN\t0\t3\t\n"
             "響\t히비키\tN130\tQID\t\tGN\t0\t4\t\n"},
        {0, "names.tsv",
         "#! layer=names\n" + header +
             "一郎\t이치로\tN130\tQPNVHn\tWb\tGN\t0\t5\t\n"
             "三郎\t사부로\tN130\tQPNVHn\tWb\tGN\t0\t6\t\n"
             "太郎\t타로\tN130\tQPNVHn\tWb\tGN\t0\t7\t\n"
             "響一郎\t히비키 이치로\tN130\tQPNVHn\t\tGN\t0\t8\t\n"
             "響一郎\t쿄이치로\tN130\tQPNVHn\tWb\tGN\t0\t9\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"凶一郎が", "쿄이치로가"},
        {"響一郎が", "쿄이치로가"},
        {"三太郎が", "멍청이가"},
        {"キョウイチロウが", "쿄우이치로우가"},
        {"きょういちろうが", "쿄이치로가"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
