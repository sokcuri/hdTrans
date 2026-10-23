// The ている of a result verb is a state (壁に掛かっている 걸려 있다); a compound or phrase takes
// the state of a listed verb it ends in, except 掛か after anything but the place に
// (取り掛かっている 착수하고 있다).
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "壁に掛か\t벽에 걸&AA@\tM428\tXXX\t\tGN\t0\t1\n"
    "取り掛か\t착수&AV@\tM428\tXXX\t\tGN\t0\t2\n"
    "時間が掛か\t시간이 걸&AA@\tM428\tXXX\t\tGN\t0\t3\n"
    "引っ掛か\t걸&AA@\tM428\tXXX\t\tGN\t0\t4\n"
    "い\t&HN@\tM427\tXXX\t\tGN\t0\t5\n"
    "V\t1고\tS570\tXXX\t\tGN\t0\t6\n"
    "るから\tA니까\tS500\tXXX\t\tGN\t0\t7\n";

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv", std::string(kRows)}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_result_state_of_kakaru_after_ni) {
    HD_CHECK_EQ(Translate("壁に掛かっているから"), "벽에 걸려 있으니까");
    HD_CHECK_EQ(Translate("引っ掛かっているから"), "걸려 있으니까");
}

HD_TEST(word_result_state_not_for_kakaru_of_a_verb_or_ga) {
    HD_CHECK_EQ(Translate("取り掛かっているから"), "착수하고 있으니까");
    HD_CHECK_EQ(Translate("時間が掛かっているから"), "시간이 걸리고 있으니까");
}
