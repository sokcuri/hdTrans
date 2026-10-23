#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv",
                                            "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
                                            "V\t5다\tS560\t\t\tGN\t0\t1\n"
                                            "が\t(이)가\tP200\t\t\tGN\t0\t2\n"
                                            "天才\t천재\tN110\t\t\tGN\t0\t3\n"
                                            "男\t남자\tN110\t\t\tGN\t0\t4\n"
                                            "と言\t(이)라고 &AV@\tM430\t\t\tGN\t0\t5\n"
                                            "嫌味を言\t비꼬는 말을 &AV@\tM430\t\t\tGN\t0\t6\n"
                                            "来\t&AH@\tM427\t\t\tGN\t0\t7\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

// The passive of 言う after a quote is 불리다 before a noun it describes or after a subject
// that is no person, 라는 말을 듣다 without a subject; after an object 하다 becomes 듣다. No
// 말해지다, 해지다 or an active 말하다.
HD_TEST(word_say_passive) {
    const std::string quoted = Translate("天才と言われた。");
    HD_CHECK(quoted.ends_with("라는 말을 들었다."));
    const std::string called = Translate("男が天才と言われた。");
    HD_CHECK(called.find("라고 불") != std::string::npos && called.find("말했") == std::string::npos);
    const std::string named = Translate("天才と言われた男が来た。");
    HD_CHECK(named.find("라고 불") != std::string::npos && named.find("해지") == std::string::npos);
    HD_CHECK_EQ(Translate("嫌味を言われた。"), "비꼬는 말을 들었다.");
}
