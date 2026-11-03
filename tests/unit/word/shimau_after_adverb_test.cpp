// The し of a する word before まう, まっ, まわ, まえ or まお starts the verb しまう (ちゃんとしまった is
// ちゃんと and しまった, not ちゃんとする and 待つ); a する word before other kana keeps its し.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_shimau_after_suru_adverb) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
         "ちゃんと\t제대로 &AV@\tE429\tXXX\tPe\tGN\t0\t1\n"
         "ちゃんと\t제대로\tE311\t\t\tGN\t0\t2\n"
         "しま\t&H5@\tM430\t\t\tGN\t0\t3\n"
         "ま\t기다&AA@\tM444\t\t\tGN\t0\t4\n"
         "V\t5다\tS560\t\t\tGN\t0\t5\n"
         "V\t4다\tS500\t\t\tGN\t0\t6\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* out;
    };
    const Case cases[] = {
        {"ちゃんとしまった。", "제대로 넣었다."},
        {"ちゃんとしまう。", "제대로 넣는다."},
        {"ちゃんとした。", "제대로 했다."},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.out));
    }
}
