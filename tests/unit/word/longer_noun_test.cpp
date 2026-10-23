// A dictionary noun that starts with an adnominal in の written from a kanji (大の字
// after 大の) is read whole when a particle, punctuation or the end follows it.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_longer_noun_after_adnominal_no) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "大の\t대단한\tE0H1\t\t\tGN\t0\t1\t\n"
         "大の字\t큰대자\tI110\t\t\tGN\t0\t2\t\n"
         "字\t글자\tN110\t\t\tGN\t0\t3\t\n"
         "字幕\t자막\tN110\t\t\tGN\t0\t4\t\n"
         "に\t에\tP200\t\t\tGN\t0\t5\t\n"
         "あの\t그\tE0H1\t\t\tGN\t0\t6\t\n"
         "あの人\t저이\tN110\t\t\tGN\t0\t7\t\n"
         "人\t사람\tN110\t\t\tGN\t0\t8\t\n"
         "、\t,\tN600\t\tMk\tGN\t0\t9\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"大の字に", "큰대자에"},
        {"大の字", "큰대자"},
        {"大の字幕", "대단한 자막"},
        {"あの人に", "그 사람에"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
