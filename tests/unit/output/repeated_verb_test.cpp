#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "あ\t&HN@\tM418\tXXX\t\tGN\t0\t1\n"
    "ますか\t7니까\tS5:0\t\t\tGN\t0\t2\n"
    "ます\t7니다\tS5:0\t\t\tGN\t0\t3\n";

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

// A verb said again after a question that closes its clause is written each time.
HD_TEST(output_repeated_verb_after_question) {
    HD_CHECK_EQ(Translate("ありますかあります"), std::string("있습니까 있습니다"));
}
