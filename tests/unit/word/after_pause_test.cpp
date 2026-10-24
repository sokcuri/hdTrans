// The Hp piece of the rules: a sentence end, an ellipsis or a dash right before the word,
// spaces between them aside.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_after_pause_fragment) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "犬\t가\tN110\tXXX\t\tGN\t0\t1\t\n"
         "犬\t나\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Hp\"\n"
         "鳥\t새\tN110\tXXX\t\tGN\t0\t4\t\n"
         "鳥\t참새\tN110\t\t\tGN\t3\t5\t||1 G+0== \"Hl\"\n"
         "猫\t고양이\tN110\t\t\tGN\t0\t3\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"猫……犬", "고양이……나"},
        {"猫――犬", "고양이――나"},
        {"猫。犬", "고양이. 나"},
        {"猫。 犬", "고양이. 나"},
        {"猫犬", "고양이가"},
        {"犬", "가"},
        // Hl: an ellipsis or a dash only.
        {"猫……鳥", "고양이……참새"},
        {"猫。鳥", "고양이. 새"},
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
