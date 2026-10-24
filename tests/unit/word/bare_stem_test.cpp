// An adjectival-noun stem that ends a clause with its copula left out reads as the
// word in its plain form wherever it stands, also before a note or a heart; a noun of the
// same length still wins.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_bare_adjectival_noun_stem) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "綺麗\t예%C9@\tG8U0\t\t\tGN\t0\t1\t\n"
         "無理\t무리%D4@\tG8U0\t\t\tGN\t0\t2\t\n"
         "無理\t무리\tN110\t\t\tGN\t0\t3\t\n"
         "花\t꽃\tN110\t\t\tGN\t0\t4\t\n"
         "が\t이\tP200\t\t\tGN\t0\t5\t\n"
         "綺\t기\tN110\t\tOj\tGN\t0\t6\t\n"
         "麗\t려\tN110\t\tOn\tGN\t0\t7\t\n"
         "、\t,\tN600\t\tMk\tGN\t0\t8\t\n"
         "っ\t\tQ110\t\t\tGN\t0\t9\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"綺麗。", "예쁘다."},
        {"花が綺麗！", "꽃이 예쁘다!"},
        {"花が綺麗…", "꽃이 예쁘다…"},
        {"花が綺麗♪", "꽃이 예쁘다♪"},
        {"花が綺麗っ！", "꽃이 예쁘다!"},
        {"「花が綺麗」", "「꽃이 예쁘다」"},
        {"花が綺麗？", "꽃이 예뻐?"},
        {"綺麗、花", "예쁘다, 꽃"},
        {"綺麗、綺麗、花", "예쁘다, 예쁘다, 꽃"},
        {"花が無理！", "꽃이 무리!"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
