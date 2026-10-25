#include <memory>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "ボク\t나\tN110\tVHn\t\tGN\t0\t1\n"
    "わたくし\t저\tN120\tVHn\t\tGN\t0\t2\n"
    "キミ\t너\tN120\tVHn\t\tGN\t0\t3\n"
    "彼\t그\tN120\tVHn\t\tGN\t0\t4\n"
    "子供\t아이\tN110\tVHn\t\tGN\t0\t5\n"
    "たち\t들\tN110\tVHn\tTu\tGN\t0\t6\n"
    "ら\t등\tN110\t\tZ7\tGN\t0\t7\n"
    "は\t(은)는\tP211\t\t\tGN\t0\t8\n"
    "の\t\tP211\t\t\tGN\t0\t9\n"
    "仲間\t동료\tN110\t\t\tGN\t0\t10\n"
    "全員\t전원\tN110\t\t\tGN\t0\t11\n";

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

HD_TEST(word_plural_pronoun) {
    HD_CHECK_EQ(Translate("ボクたちは"), "우리는");
    HD_CHECK_EQ(Translate("わたくしらは"), "저희는");
    HD_CHECK_EQ(Translate("キミたちは"), "너희는");
    // The space the suffix had before a word stays, past an empty の.
    HD_CHECK_EQ(Translate("ボクら全員"), "우리 전원");
    HD_CHECK_EQ(Translate("ボクたちの仲間"), "우리 동료");
}

HD_TEST(word_plural_pronoun_keeps_other_nouns) {
    HD_CHECK_EQ(Translate("子供たちは"), "아이들은");
}
