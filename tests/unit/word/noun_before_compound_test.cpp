// A country or province in 国 before a noun in 国 gives its 国 to that noun (美濃国境 미노 + 국경),
// unless a longer word starts after the name (美濃国旗頭 미노국 + 우두머리) or the name without 国
// is one kanji (王国中 왕국 + 중).
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "美濃\t미노\tN130\t\t\tGN\t0\t1\n"
    "美濃国\t미노국\tN110\t\t\tGN\t0\t2\n"
    "国境\t국경\tN110\t\t\tGN\t0\t3\n"
    "境\t경계\tN110\t\t\tGN\t0\t4\n"
    "国旗\t국기\tN110\t\t\tGN\t0\t5\n"
    "旗頭\t우두머리\tN110\t\t\tGN\t0\t6\n"
    "頭\t머리\tN110\t\t\tGN\t0\t7\n"
    "王\t왕\tN110\t\t\tGN\t0\t8\n"
    "王国\t왕국\tN110\t\t\tGN\t0\t9\n"
    "国中\t온 나라\tN110\t\t\tGN\t0\t10\n"
    "中\t중\tN110\t\t\tGN\t0\t11\n";

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

HD_TEST(word_province_gives_its_kuni_to_the_noun_after_it) {
    HD_CHECK(Translate("美濃国境").find("국경") != std::string::npos);
    HD_CHECK(Translate("美濃国境").find("경계") == std::string::npos);
}

HD_TEST(word_province_keeps_its_kuni_before_a_longer_word) {
    HD_CHECK(Translate("美濃国旗頭").find("우두머리") != std::string::npos);
    HD_CHECK(Translate("王国中").find("온 나라") == std::string::npos);
}
