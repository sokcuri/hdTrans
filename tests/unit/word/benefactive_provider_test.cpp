#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv",
                                            "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
                                            "V\t5다\tS560\t\t\tGN\t0\t1\n"
                                            "V\t6\tS571\t\t\tGN\t0\t2\n"
                                            "に\t에게\tP200\t\t\tGN\t0\t3\n"
                                            "を\t(을)를\tP200\t\t\tGN\t0\t4\n"
                                            "母\t어머니\tN110\tVHn\t\tGN\t0\t5\n"
                                            "庭\t뜰\tN110\t\t\tGN\t0\t6\n"
                                            "の\t의\tP210\t\t\tGN\t0\t7\n"
                                            "花\t꽃\tN110\t\t\tGN\t0\t8\n"
                                            "見せ\t보여&AJ@\tM437\t\t\tGN\t0\t9\n"
                                            "もら\t&CJ@\tM410\t\tDs\tGN\t0\t10\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_benefactive_provider_is_the_subject) {
    // The person に before a て verb and もらう read as 주다 gives, however far before the verb.
    HD_CHECK(Translate("母に庭の花を見せてもらった。").starts_with("어머니가 뜰의 꽃을"));
    // A thing before に is no giver.
    HD_CHECK(Translate("庭に花を見せてもらった。").starts_with("뜰에게"));
}
