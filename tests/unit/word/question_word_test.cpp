// The Qw piece of the rules: a question word before the word in its sentence, alone or with a
// case particle (どこから), not an indefinite (どこか) or a question closed by か before it.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_question_word_fragment) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "犬\t가\tN110\tXXX\t\tGN\t0\t1\t\n"
         "犬\t나\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Qw\"\n"
         "どこ\t어디\tN120\t\t\tGN\t0\t3\t\n"
         "どこから\t어디에서\tN120\t\t\tGN\t0\t4\t\n"
         "か\t인가\tP200\t\t\tGN\t0\t5\t\n"
         "から\t부터\tP200\t\t\tGN\t0\t6\t\n"
         "かな\t일까\tP200\t\t\tGN\t0\t7\t\n"
         "猫\t고양이\tN110\t\t\tGN\t0\t8\t\n"
         "行\t&A1@\tM451\t\t\tGN\t0\t9\t\n"
         "か\t0지\tS500\t\t\tGN\t0\t10\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"どこから犬", "어디에서나"},
        {"どこかな犬", "어디일까 나"},
        {"どこか犬", "어디인가 가"},
        {"どこ猫か犬", "어디고양이인가 가"},
        {"どこ行くか犬", "어디 가는지 가"},  // the か after the verb's own kana closes a question
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
