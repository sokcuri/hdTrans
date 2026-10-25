#include <cstdlib>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

// Engine parts of the readings after a finished predicate and of the obligation alone: an ending in
// か gives its か to the から after it (CutsWord), map-target-tail on a dictionary row read with a
// final ending, a word in で gives it to the でしょう after it (SplitsDeshou), and set-verb on a
// negative stem of the adjective class (しな, 買わな) writes the verb with the $ families. The rows
// are the shipped ones, so the test needs HDTRANS_DICTS.
HD_TEST(word_finished_predicate) {
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
    HD_CHECK_EQ(translate("名刺まで渡してるんですからね。"), "명함까지 건네주고 있으니까요.");
    HD_CHECK_EQ(translate("埋めるときはお手伝いしますから"), "묻을 때는 돕겠으니까요");
    HD_CHECK_EQ(translate("学校から帰った。"), "학교에서 돌아갔다.");
    HD_CHECK_EQ(translate("初めてでしょう。"), "처음이죠.");
    HD_CHECK_EQ(translate("それもそれでしょうがないね"), "그것도 그래서 어쩔 수 없네");
    HD_CHECK_EQ(translate("大掃除しなきゃ。"), "대청소해야 한다.");
    HD_CHECK_EQ(translate("クリーニングしなきゃ。"), "클리닝해야 한다.");
    HD_CHECK_EQ(translate("水着、買わなくちゃ。"), "수영복, 사야 한다.");
    HD_CHECK_EQ(translate("行かなきゃ損だ"), "가지 않으면 손해다");
}
