// A と or たら before a past main clause is a step of the story, -니, -자 or -었더니, not the
// condition -면 (PastSequence, spec GRM-175..178), unless its sentence makes it a condition.
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "V\t5다\tS560\tXXX\t\tGN\t0\t1\n"
    "V\t4다\tS500\tXXX\t\tGN\t0\t2\n"
    "V\t1고\tS570\t\t\tGN\t0\t3\n"
    "と\t2면(자)\tS500\t\t\tGN\t0\t4\n"
    "ら\t2면(자)\tS560\t\t\tGN\t0\t5\n"
    "が\t(이)가\tP200\tXXX\t\tGN\t0\t6\n"
    "は\t(은)는\tP200\t\t\tGN\t0\t7\n"
    "を\t(을)를\tP200\t\t\tGN\t0\t8\n"
    "ドア\t문\tN110\t\t\tGN\t0\t9\n"
    "男\t남자\tN110\t\t\tGN\t0\t10\n"
    "彼\t그\tN120\tVHn\t\tGN\t0\t11\n"
    "開け\t&LB@\tM437\t\t\tGN\t0\t12\n"
    "待\t기다&AA@\tM434\t\t\tGN\t0\t13\n"
    "い\t&HN@\tM427\t\t\tGN\t0\t14\n"
    "入\t들어&A1@\tM418\tXXX\t\tGN\t0\t15\n"
    "もし\t만약\tP351\t\t\tGN\t0\t16\n"
    "決め\t결정&AV@\tM437\tXXX\t\tGN\t0\t17\n"
    "た\t5다\tS570\t\tJ2Sh\tGN\t0\t18\n";

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

HD_TEST(word_past_sequence_joins_without_a_pause) {
    // と and たら with no comma before a past event (GRM-175, GRM-176), the いる of ている too.
    HD_CHECK_EQ(Translate("ドアを開けると男が入った。"), "문을 열자 남자가 들어갔다.");
    HD_CHECK_EQ(Translate("男が待っていると彼が入った。"), "남자가 기다리고 있으니 그가 들어갔다.");
    HD_CHECK_EQ(Translate("ドアを開けたら男が入った。"), "문을 열었더니 남자가 들어갔다.");
}

HD_TEST(word_past_sequence_keeps_conditions) {
    // A word of supposition keeps the condition (GRM-177), and a verb of deciding right after the
    // と takes it as its quotation.
    HD_CHECK_EQ(Translate("もしドアを開けたら男が入った。"), "만약 문을 열면 남자가 들어갔다.");
    HD_CHECK_EQ(Translate("もしドアを開けると男が入った。"), "만약 문을 열면 남자가 들어갔다.");
    HD_CHECK_EQ(Translate("ドアを開けると決めた。"), "문을 연다고 결정했다.");
}
