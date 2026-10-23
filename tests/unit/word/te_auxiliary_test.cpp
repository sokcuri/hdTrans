// The phrase pass reads a verb and its continuative ending as the noun they spell
// (向かいに 맞은편에), except a kana auxiliary after a て form (増えていき: いく, not 息).
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::string Rows() {
    std::string rows =
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "増え\t늘어&A5@\tM427\t\t\tGN\t0\t1\n"
        "い\t&A1@\tM411\tXXX\t\tGN\t0\t3\n"
        "いき\t숨\tN110\t\t\tGN\t0\t4\n"
        "向か\t향&AV@\tM420\t\t\tGN\t0\t5\n"
        "向かい\t맞은편\tN110\t\t\tGN\t0\t6\n"
        "に\t에\tP200\t\t\tGN\t0\t7\n"
        "V\t6\tS570\tXXX\t\tGN\t0\t8\n";
    // The phrase pass looks at words with twenty or more ending slots.
    for (int i = 0; i < 20; i++) rows += "V\t6\tS5:0\t" + std::string(i == 0 ? "XXX" : "") + "\t\tGN\t0\t" + std::to_string(10 + i) + "\n";
    return rows;
}

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv", Rows()}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_te_auxiliary_stays_a_verb) {
    HD_CHECK_EQ(Translate("増えていき……"), "늘어나 가……");
    HD_CHECK_EQ(Translate("増えて向かいに"), "늘어나 맞은편에");
}
