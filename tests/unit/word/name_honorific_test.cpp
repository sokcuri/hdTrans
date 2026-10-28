// The honorific after a name: ちゃん and と after a name that the dictionary also reads as a
// proper noun (ふたばちゃんと).
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_name_honorific_after_proper_noun) {
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n";
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         header +
             "ふたば\t후타바\tI130\t\t\tGN\t0\t1\t\n"
             "ちゃんと\t제대로\tE300\t\t\tGN\t0\t3\t\n"
             "ちゃん\t짱\tN110\tVHn\tTu\tGN\t0\t4\t\n"
             "と\t(와)과\tP200\t\t\tGN\t0\t5\t\n"
             "ここ\t여기\tI120\t\t\tGN\t0\t8\t\n"},
        {0, "names.tsv",
         "#! layer=names\n" + header + "ふたば\t후타바\tN130\tQPNVHn\tWb\tGN\t0\t9\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"ふたばちゃんと", "후타바짱과"},
        // No person of the name layer before the adverb.
        {"ここちゃんと", "여기 제대로"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
