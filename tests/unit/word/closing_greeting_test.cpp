// word.closing-greeting: a greeting that ends in a closing particle (よろしくな) stays one word
// where the sentence or a clause ends after it, also after a particle, and gives way to the
// words it starts with elsewhere (よろしくなら).
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_closing_greeting) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "よろしく\t아무쪼록\tA9D0\t\t\tGN\t0\t1\t\n"
         "よろしくな\t잘 부탁해\tA9D0\t\t\tGN\t0\t2\t\n"
         "な\t\tP200\t\t\tGN\t0\t3\t\n"
         "なら\t(이)라면\tP200\t\t\tGN\t0\t4\t\n"
         "猫\t고양이\tN110\t\t\tGN\t0\t5\t\n"
         "は\t(은)는\tP200\t\t\tGN\t0\t6\t\n"
         "を\t(을)를\tP200\t\t\tGN\t0\t7\t\n"
         "、\t,\tN600\t\tMk\tGN\t0\t8\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"よろしくな", "잘 부탁해"},
        {"よろしくな！", "잘 부탁해!"},
        {"よろしくな、猫", "잘 부탁해, 고양이"},
        {"猫をよろしくな。", "고양이를 잘 부탁해."},
        {"猫はよろしくな。", "고양이는 잘 부탁해."},
        {"よろしくなら", "아무쪼록이라면"},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
