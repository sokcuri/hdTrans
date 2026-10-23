#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "度\t번\tN110\t\tCt\tGN\t0\t1\n"
    "度\t도\tN110\tUXt\tCt\tGN\t0\t2\n"
    "目\t째\tN110\t\t\tGN\t0\t3\n"
    "回\t회\tN110\t\tCt\tGN\t0\t4\n";

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

// 度 after a number of 10 or more or with a decimal point is the degree; a smaller number,
// or 度 before も, ほど or 目, counts times.
HD_TEST(word_degree_after_number) {
    HD_CHECK(Translate("180度").starts_with("180도"));
    HD_CHECK(Translate("0.5度").starts_with("0.5도"));
    HD_CHECK(Translate("5度").starts_with("5번") || Translate("5度").starts_with("다섯 번"));
    // も, ほど and 目 after 度 count times.
    HD_CHECK(!Translate("10度も").starts_with("10도"));
    HD_CHECK(!Translate("10度ほど").starts_with("10도"));
}
