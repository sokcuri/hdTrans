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
                                            "母\t어머니\tN110\tVHn\t\tGN\t0\t5\n"
                                            "彼\t그\tN120\tVHn\t\tGN\t0\t6\n"
                                            "手紙\t편지\tN110\t\t\tGN\t0\t7\n"
                                            "書\t&AF@\tM431\t\t\tGN\t0\t8\n"
                                            "撫で\t쓰다&H7@\tM447\t\t\tGN\t0\t9\n"
                                            "頭\t머리\tN110\t\t\tGN\t0\t10\n"
                                            "待たされ\t기다리게 &C6@\tM427\t\t\tGN\t0\t11\n"
                                            "庭\t뜰\tN110\t\t\tGN\t0\t12\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_agent_passive_is_active) {
    // A person before に with an object of を in the clause does the act.
    HD_CHECK_EQ(Translate("母に手紙を書かれた。"), "어머니가 편지를 썼다.");
    HD_CHECK_EQ(Translate("手紙を母に書かれた。"), "편지를 어머니가 썼다.");
    // The られ of an ichidan verb after an object is the passive with that agent.
    HD_CHECK_EQ(Translate("母に頭を撫でられた。"), "어머니가 머리를 쓰다듬었다.");
}

HD_TEST(word_agent_passive_keeps_the_passive) {
    // No object, a thing before に, or a subject が before the agent.
    HD_CHECK_EQ(Translate("母に書かれた。"), "어머니에게 써졌다.");
    HD_CHECK_EQ(Translate("庭に手紙を書かれた。").find("뜰에게"), 0u);
    HD_CHECK(Translate("彼が母に手紙を書かれた。").starts_with("그가 어머니에게"));
}

HD_TEST(word_causer_of_a_causative_passive) {
    HD_CHECK_EQ(Translate("母に待たされた。"), "어머니 때문에 기다리게 되었다.");
    HD_CHECK_EQ(Translate("庭に待たされた。"), "뜰에게 기다리게 되었다.");
}
