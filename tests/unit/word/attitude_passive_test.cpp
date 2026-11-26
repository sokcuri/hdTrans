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
                                            "が\t(이)가\tP200\t\t\tGN\t0\t2\n"
                                            "を\t(을)를\tP200\t\t\tGN\t0\t3\n"
                                            "に\t에게\tP200\t\t\tGN\t0\t4\n"
                                            "後輩\t후배\tN110\tVHn\t\tGN\t0\t5\n"
                                            "犬\t개\tN110\tVAm\t\tGN\t0\t6\n"
                                            "指\t손가락\tN110\tVBp\t\tGN\t0\t7\n"
                                            "舐め\t&FX@\tM427\t\t\tGN\t0\t8\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_attitude_passive_without_a_body) {
    // 舐められる with a person before に or with no agent is being looked down on.
    HD_CHECK(Translate("後輩に舐められた。").find("얕보") != std::string::npos);
    HD_CHECK(Translate("舐められた。").find("얕보") != std::string::npos);
    // After an animal or with an object it is the act of licking.
    HD_CHECK(Translate("犬に舐められた。").find("얕보") == std::string::npos);
    HD_CHECK(Translate("指を舐められた。").find("얕보") == std::string::npos);
}
