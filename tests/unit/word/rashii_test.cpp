// A noun ending in ら before しい leaves ら to the adjective らしい.
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "お前\t너\tN120\t\t\tGN\t0\t1\n"
    "お前ら\t너희들\tN120\t\t\tGN\t0\t2\n"
    "ら\t들\tN110\t\t\tGN\t0\t3\n"
    "らし\t$G4@\tH7M0\tXXX\tTu\tGN\t0\t4\n"
    "しい\t강요&AV@\tM437\t\t\tGN\t0\t5\n"
    "い\t1다\tT500\tXXX\t\tGN\t0\t6\n"
    "V\t1고\tS5:0\tXXX\t\tGN\t0\t7\n";

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

HD_TEST(word_noun_in_ra_before_rashii) {
    HD_CHECK_EQ(Translate("お前らしい"), "너답다");
    HD_CHECK_EQ(Translate("お前ら"), "너희들");
}
