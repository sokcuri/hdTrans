#include <cstdlib>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

// A regular stem spelled as the plain form of くる (the くる of くるむ) yields to 来る where the
// prefix table reads as far (くるんだ 오는 거야), unless a mark of its object or means comes before
// it (タオルでくるんだ 수건으로 감쌌다), a wrapping thing and に (毛布にくるんだ), or a noun after its
// past (くるんだ布); a て form before it is no such mark (読んでくるんだ). The
// analysis takes the dictionary's words, so the test needs HDTRANS_DICTS.
HD_TEST(word_irregular_lookalike_stem) {
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
    HD_CHECK_EQ(translate("くるんだ"), "오는 거야");
    HD_CHECK(has(translate("入れてくるんだ"), "넣어 오는 거야"));
    HD_CHECK(has(translate("読んでくるんだ"), "오는"));
    HD_CHECK(has(translate("タオルでくるんだ"), "감쌌다"));
    HD_CHECK(has(translate("乳房をくるんだ。"), "감쌌다"));
    HD_CHECK(has(translate("毛布にくるんだ赤ちゃん"), "담요에 감싼 아기"));
    HD_CHECK(has(translate("くるんだ布を広げる"), "감싼 천을"));
    HD_CHECK(has(translate("赤子をおくるみにくるんだ"), "감쌌다"));
    HD_CHECK(has(translate("ここにくるんだ"), "오는 거야"));
}
