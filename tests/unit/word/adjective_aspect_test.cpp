#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = hdTrans::dict::Load({{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "効\t강&Mf@\tM411\t\tVa\tGN\t0\t1\n"
        "い\t&HN@\tM428\tXXX\tDaVa\tGN\t0\t2\n"
        "V\t1고\tS571\t\tJ1\tGN\t0\t3\n"
        "V\t5다\tS560\t\t\tGN\t0\t4\n"
        "V\t4다\tS500\t\t\tGN\t0\t5\n"
        "V\t0\tS500\t\t\tGN\t0\t6\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    hdTrans::internal::Memory memory;
    const std::string line(text);
    const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_adjective_target_reads_teiru_as_its_state) {
    // A verb whose Korean is an adjective reads ている as the adjective itself; the verb
    // form 4 of いる (있다) has no adjective form to move onto it.
    HD_CHECK_EQ(Translate("効いている。"), "강하다.");
}
