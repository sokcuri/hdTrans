// A kana spelling of a noun or adjectival noun of three letters or more (kana layer) is read as its
// word before the copula だ, です, でした, でしょ and じゃ as before a particle (LEX-94). だ after a
// spelling ending in い or ん whose rest is the stem of a godan verb of that row is the verb's past,
// a spelling of another class (an adnominal) or of two letters needs a particle, and a key the name
// layer holds as a person is left to the name before the copula.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_kana_spelling_before_copula) {
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n";
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         header +
             "指切り\t약속\tN110\t\t\tGN\t0\t1\t\n"
             "由来\t유래\tN110\t\t\tGN\t0\t2\t\n"
             "勇気\t용기\tN110\t\t\tGN\t0\t3\t\n"
             "罰\t벌\tN110\t\t\tGN\t0\t4\t\n"
             "安心\t안심\tN110\t\t\tGN\t0\t12\t\n"
             "小判\t금화\tN110\t\t\tGN\t0\t13\t\n"
             "こば\t거절&AV@\tM425\t\t\tGN\t0\t14\t\n"
             "ゆら\t흔들&AA@\tM442\t\t\tGN\t0\t15\t\n"
             "例の\t예의\tE0H1\t\t\tGN\t0\t16\t\n"
             "だ\t(이)다\tP200\t\tZ2\tGN\t0\t5\t\n"
             "です\t(이)에요\tP200\t\tZ2\tGN\t0\t10\t\n"
             "じゃ\t(이)지\tP200\t\tZ2\tGN\t0\t11\t\n"
             "でした\t(이)었어요\tP200\t\tZ2\tGN\t0\t17\t\n"
             "が\t(이)가\tP200\t\t\tGN\t0\t6\t\n"
             "昨日\t어제\tN110\t\t\tGN\t0\t8\t\n"
             "、\t,\tN600\t\tMk\tGN\t0\t9\t\n"},
        {0, "kana.tsv", "#! layer=kana\nゆびきり\t指切り\t\nゆらい\t由来\t\nゆうき\t勇気\t\nばち\t罰\t\n"
                     "あんしん\t安心\t\nこばん\t小判\t\nれいの\t例の\t\n"},
        {0, "names.tsv", "#! layer=names\n" + header + "ゆうき\t유우키\tN130\tQPNVHn\tWb\tGN\t0\t7\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* has;
        bool want;
    };
    const Case cases[] = {
        {"昨日、ゆびきりだ", "약속", true},
        {"昨日、ゆびきりです", "약속", true},
        {"昨日、ゆびきりじゃ", "약속", true},
        {"昨日、ゆびきりでした", "약속", true},
        {"昨日、ゆびきりでしょ", "약속", true},
        {"昨日、あんしんだ", "안심", true},
        {"昨日、こばんだ", "금화", false},
        {"昨日、こばんが", "금화", true},
        {"昨日、れいのです", "예의", false},
        {"昨日、ゆらいだ", "유래", false},
        {"昨日、ゆうきだ", "용기", false},
        {"昨日、ゆうきが", "용기", true},
        {"昨日、ばちが", "벌", true},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, {}, {});
        HD_REQUIRE(out.size() == 1);
        const bool has = out[0].find(c.has) != std::string::npos;
        HD_CHECK_EQ(out[0] + (has ? " has " : " lacks ") + c.has, out[0] + (c.want ? " has " : " lacks ") + c.has);
    }
}
