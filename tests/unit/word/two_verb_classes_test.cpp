#include <cstdlib>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

// A stem that the dictionary has as two verbs reads the form that only one of them takes: the
// で, だ and じゃ after い of a ぐ verb whose stem is also a わ-row verb (担いで of 担ぐ, not 担う + で),
// but not where the stem in い is a noun (戦いだ 싸움이다); and the polite い of a godan verb in る
// written in kanji (仰いました of 仰る, not 仰ぐ). The rows are the shipped ones, so the test needs
// HDTRANS_DICTS.
HD_TEST(word_two_verb_classes) {
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
    HD_CHECK_EQ(translate("男を担いで移動する。"), "남자를 메어 이동한다.");
    HD_CHECK_EQ(translate("荷物を担いだ。"), "짐을 메었다.");
    HD_CHECK_EQ(translate("責任を担っている"), "책임을 짊어지고 있다");
    HD_CHECK_EQ(translate("次は大きな戦いだ"), "다음은 큰 싸움이다");
    HD_CHECK_EQ(translate("先生が仰いました。"), "선생님이 말씀하셨습니다.");
    HD_CHECK_EQ(translate("空を仰いだ。"), "하늘을 우러러보았다.");
    HD_CHECK_EQ(translate("おっしゃいました"), "말씀하셨습니다");
}
