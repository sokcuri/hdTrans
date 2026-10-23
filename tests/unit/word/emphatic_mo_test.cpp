#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

// A verb continuative before も and する is the verb: V-지도 않다 with a negative する,
// V-기도 하다 otherwise, the ending kept before a 하다 verb's する; a noun list before する
// (返事もせず) and one kana before でもする stay as they were. The analysis takes the
// dictionary's verb stems and endings, so the test needs HDTRANS_DICTS.
HD_TEST(word_emphatic_mo) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    auto translate = [&](std::string_view text) {
        internal::Memory memory;
        const std::string line(text);
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
        return out.empty() ? std::string() : out[0];
    };
    HD_CHECK_EQ(translate("彼は立ち上がりもせず、"), "그는 일어서지도 않고,");
    HD_CHECK_EQ(translate("読みもしません。"), "읽지도 않습니다.");
    HD_CHECK_EQ(translate("読みもせずに捨てた。"), "읽지도 않고 버렸다.");
    HD_CHECK_EQ(translate("見もしない。"), "보지도 않는다.");
    HD_CHECK_EQ(translate("読みもした。"), "읽기도 했다.");
    HD_CHECK_EQ(translate("疑いもした。"), "의심하기도 했다.");
    HD_CHECK_EQ(translate("返事もせず、"), "대답도 하지 않고,");
    HD_CHECK(translate("練習でもしようか。").find("나오") == std::string::npos);
}
