// The Rw piece of the rules: a word written right after the same word repeats it (畑畑), so
// the global rules read it neither as a kanji compound (On) nor as a suffix (Z7).
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_repeated_kanji_is_no_compound) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "畑\t밭\tN110\tXXX\t\tGN\t0\t1\t\n"
         "畑\t전\tN110\t\tOn\tGN\t0\t2\t\n"
         "池\t연못\tN110\tXXX\t\tGN\t0\t3\t\n"
         "池\t지\tN110\t\tOn\tGN\t0\t4\t\n"
         "水\t물\tN110\tXXX\t\tGN\t0\t5\t\n"
         "水\t수\tN110\t\tZ7\tGN\t0\t6\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"畑池", "전지"},
        {"池畑", "지전"},
        {"畑畑", "밭밭"},
        {"畑畑池", "밭밭 연못"},
        {"畑畑畑畑", "밭밭밭밭"},
        {"畑", "밭"},
        {"池水", "연못수"},
        {"水水", "물물"},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
