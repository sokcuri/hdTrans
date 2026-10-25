// A word after an opening bracket that starts the line starts its sentence for the Hz
// test of the rules.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_sentence_start_after_opening_bracket) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "人気\t인적\tN110\tXXX\t\tGN\t0\t1\t\n"
         "人気\t인기\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Hz\"\n"
         "雨\t비\tN110\t\t\tGN\t0\t3\t\n"
         "の\t의\tP200\t\t\tGN\t0\t4\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"人気。", "인기."},
        {"「人気」", "「인기」"},
        {"『人気』", "『인기』"},
        {"雨の人気", "비의 인적"},
        {"「雨の人気」", "「비의 인적」"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
