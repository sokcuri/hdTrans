#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv",
                                            "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
                                            "金\t돈\tN110\t\t\tGN\t0\t1\n"
                                            "があ\t(이)가 &M9@\tM428\t\tTuPj\tGN\t0\t2\n"
                                            "ませんのに\t1지 않는데요\tS5:0\t\t\tGN\t0\t3\n"
                                            "ませんわ\t1지 않아요\tS5:0\t\t\tGN\t0\t4\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_absent_takes_the_negative_ending_as_its_own_form) {
    // A word read as 없다 does not take the negative 지 않 again.
    HD_CHECK_EQ(Translate("金がありませんのに"), "돈이 없는데요");
    HD_CHECK_EQ(Translate("金がありませんわ"), "돈이 없어요");
}
