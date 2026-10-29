#include <cstdlib>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

// A verb whose longest ending leaves text that no word reads (読むのです + ぞ, なってるでしょ + うか,
// みるとこ + れ) is read with a shorter ending when the token has no other reading of it; a token the
// search reads whole keeps its reading. The analysis takes the dictionary's words, so the test
// needs HDTRANS_DICTS.
HD_TEST(word_shorter_ending) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    auto translate = [&](std::string_view text) {
        const std::string line(text);
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
        return out.empty() ? std::string() : out[0];
    };
    auto has = [](const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; };
    HD_CHECK(has(translate("読むのですぞ"), "읽는"));
    HD_CHECK(has(translate("金がかかるのですぞ"), "드는"));
    HD_CHECK(has(translate("考えてみるとこれ"), "보면 이것"));
    HD_CHECK(has(translate("自分となるとこれは落ち着かない"), "되면 이것은"));
    HD_CHECK(has(translate("首を傾げるわたしを見て"), "갸웃하는"));
    HD_CHECK(has(translate("知ってるとこだわ"), "곳"));
    // The token is read whole without a shorter ending.
    HD_CHECK_EQ(translate("それをあげるのです"), "그것을 줍니다");
    HD_CHECK(has(translate("わかんないんだけど"), "모르는데"));
}
