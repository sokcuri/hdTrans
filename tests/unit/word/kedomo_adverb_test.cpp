// An ending in けども gives its も to an adverb that starts with it and reaches as far as the words
// after the ending do (悪いけどもう一度: けど + もう一度); before such a word (うん) it stays.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_kedomo_gives_mo_to_adverb) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "悪\t나$AG@\tH7U0\t\t\tGN\t0\t1\t\n"
         "いけど\t1지만\tT500\t\t\tGN\t0\t2\t\n"
         "いけども\t1지만\tT500\t\t\tGN\t0\t3\t\n"
         "もう一度\t한번 더\tE300\t\t\tGN\t0\t4\t\n"
         "うん\t응\tA110\t\t\tGN\t0\t5\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"悪いけどもう一度", "나쁘지만 한번 더"},
        {"悪いけどもうん", "나쁘지만 응"},
        {"悪いけども", "나쁘지만"},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
