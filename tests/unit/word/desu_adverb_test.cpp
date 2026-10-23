// An ending in です gives its す to an adverb or interjection that starts with it
// (寝込んだのですっかり: ので + すっかり); です before a particle stays.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_desu_gives_su_to_adverb) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "寝込\t드러&J4@\tM425\t\t\tGN\t0\t1\t\n"
         "ので\t5으므로\tS560\t\t\tGN\t0\t2\t\n"
         "のです\t5습니다\tS560\tXXX\t\tGN\t0\t3\t\n"
         "すっかり\t완전히\tE311\t\t\tGN\t0\t4\t\n"
         "すみません\t죄송합니다\tA110\t\t\tGN\t0\t5\t\n"
         "よ\t요\tP200\t\t\tGN\t0\t6\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"寝込んだのですっかり", "드러누웠으므로 완전히"},
        {"寝込んだのですみません", "드러누웠으므로 죄송합니다"},
        {"寝込んだのです", "드러누웠습니다"},
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
