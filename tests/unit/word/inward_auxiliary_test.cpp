#include <string>
#include <string_view>
#include <vector>

#include "core/engine/driver.h"
#include "hdtest.h"

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = hdTrans::dict::Load({
        {0, "plain.tsv", "詰める\t채우다\tv1\n込む\t붐비다\tv5\n道\t길\tn\n"},
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
         "V\t5다\tS560\t\t\tGN\t0\t1\n"
         "が\t(이)가\tP201\t\t\tGN\t0\t2\n"},
    });
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    hdTrans::internal::Memory memory;
    const std::string line(text);
    const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_inward_auxiliary_after_a_verb_stem) {
    // 込む after a verb stem writes nothing; the verb takes its ending.
    HD_CHECK_EQ(Translate("詰め込んだ。"), "채웠다.");
}

HD_TEST(word_inward_auxiliary_keeps_komu_after_a_noun) {
    HD_CHECK_EQ(Translate("道が込んだ。"), "길이 붐볐다.");
}
