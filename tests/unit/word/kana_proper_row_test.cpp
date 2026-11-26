// A proper noun row of the dictionary whose key starts in hiragana is not read right after hiragana
// inside a kana run (ばかりか: not ばか + りか), but at the start of a run and in the slot of a noun after
// a particle (spec LEX-8160).
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_kana_proper_row_stays_out_of_kana_runs) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
         "りか\t리카\tN130\tQPN\tWb\tGN\t0\t1\n"
         "ばか\t바보\tN110\t\t\tGN\t0\t2\n"
         "ばかりか\t뿐만 아니라\tP210\t\t\tGN\t0\t3\n"
         "が\t(이)가\tP211\t\t\tGN\t0\t4\n"
         "と\t와\tP200\t\t\tGN\t0\t5\n"
         "彼\t그\tN120\tVHn\t\tGN\t0\t6\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        bool name;  // whether the output reads 리카
    };
    const Case cases[] = {
        {"ばかりか", false},
        {"りかが", true},
        {"彼とりかが", true},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0].find("리카") != std::string::npos, c.name);
    }
}
