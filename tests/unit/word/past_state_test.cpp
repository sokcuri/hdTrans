// The ている of 亡くなる is a state that Korean reads in the past (亡くなっているから
// 죽었으니까); another verb keeps its progressive (待っているから 기다리고 있으니까).
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "亡くな\t&HT@\tM428\tXXX\t\tGN\t0\t1\n"
    "い\t&HN@\tM427\tXXX\t\tGN\t0\t2\n"
    "待\t기다&AA@\tM434\tXXX\t\tGN\t0\t3\n"
    "V\t1고\tS570\tXXX\t\tGN\t0\t4\n"
    "るから\tA니까\tS500\tXXX\t\tGN\t0\t5\n";

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

HD_TEST(word_past_result_state) {
    HD_CHECK_EQ(Translate("亡くなっているから"), "죽었으니까");
    HD_CHECK_EQ(Translate("待っているから"), "기다리고 있으니까");
}
