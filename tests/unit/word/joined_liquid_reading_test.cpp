#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "率\t율\tN110\t\tZ7Ct\tGN\t0\t1\n"
    "率\t률\tN110\t\tOn\tGN\t0\t2\n"
    "列\t렬\tN110\t\tOn\tGN\t0\t3\n"
    "命中\t명중\tN110\t\t\tGN\t0\t4\n"
    "回収\t회수\tN110\t\t\tGN\t0\t5\n"
    "還元\t환원\tN110\t\t\tGN\t0\t6\n"
    "艦\t함\tN110\t\tOn\tGN\t0\t7\n"
    "車\t차\tN110\t\tOn\tGN\t0\t8\n"
    "が\t(이)가\tP200\t\t\tGN\t0\t9\n";

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

// The reading 렬 or 률 joined to a word is 열, 율 after a vowel or ㄴ and 렬, 률 after another final.
HD_TEST(word_joined_liquid_reading) {
    HD_CHECK(Translate("命中率が").starts_with("명중률"));
    HD_CHECK(Translate("回収率が").starts_with("회수율"));
    HD_CHECK(Translate("還元率が").starts_with("환원율"));
    HD_CHECK(Translate("艦列が").starts_with("함렬"));
    HD_CHECK(Translate("車列が").starts_with("차열"));
}
