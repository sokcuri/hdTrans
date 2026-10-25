// A word span of Hangul (rule code Kh) has no known part of speech, so the suffix reading of
// the noun after it (G30) needs a real noun before it: 두근두근……頭 is 머리, 白髪頭 백발두. A span
// with a mask (자○) is a noun.
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "頭\t머리\tN110\t\t\tGN\t0\t1\n"
    "頭\t두\tN110\t\tZ7\tGN\t0\t2\n"
    "白髪\t백발\tN110\t\t\tGN\t0\t3\n"
    "が\t(이)가\tP200\t\t\tGN\t0\t4\n";

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv", std::string(kRows)}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_korean_span_is_no_noun_before_a_suffix) {
    HD_CHECK_EQ(Translate("두근두근頭が"), "두근두근머리가");
    HD_CHECK_EQ(Translate("두근두근……頭が"), "두근두근……머리가");
}

HD_TEST(word_noun_before_a_suffix_keeps_it) {
    HD_CHECK_EQ(Translate("白髪頭が"), "백발두가");
}
