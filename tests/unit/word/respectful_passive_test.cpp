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
                                            "は\t(은)는\tP200\t\t\tGN\t0\t3\n"
                                            "を\t(을)를\tP200\t\t\tGN\t0\t4\n"
                                            "に\t에게\tP200\t\t\tGN\t0\t5\n"
                                            "陛下\t폐하\tN110\tVHn\t\tGN\t0\t6\n"
                                            "彼\t그\tN120\tVHn\t\tGN\t0\t7\n"
                                            "手紙\t편지\tN110\t\t\tGN\t0\t8\n"
                                            "茶\t차\tN110\t\t\tGN\t0\t9\n"
                                            "書\t&AF@\tM431\t\t\tGN\t0\t10\n"
                                            "帰\t돌아&A1@\tM418\t\t\tGN\t0\t11\n"
                                            "飲\t마&AE@\tM435\t\t\tGN\t0\t12\n"
                                            "命じ\t명&AV@\tM447\t\t\tGN\t0\t13\n"
                                            "役目\t역할\tN110\t\t\tGN\t0\t14\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_respectful_passive_of_an_honoured_subject) {
    HD_CHECK_EQ(Translate("陛下が手紙を書かれた。"), "폐하가 편지를 쓰셨다.");
    HD_CHECK_EQ(Translate("陛下が帰られた。"), "폐하가 돌아가셨다.");
    HD_CHECK_EQ(Translate("陛下は茶を飲まれた。"), "폐하는 차를 드셨다.");
}

HD_TEST(word_respectful_passive_keeps_the_passive) {
    // An agent, a subject that is no title, or a transitive verb without its object.
    HD_CHECK_EQ(Translate("陛下が彼に手紙を書かれた。"), "폐하가 그에게 편지를 써졌다.");
    HD_CHECK_EQ(Translate("彼が手紙を書かれた。"), "그가 편지를 써졌다.");
    HD_CHECK_EQ(Translate("陛下が書かれた。"), "폐하가 써졌다.");
}

HD_TEST(word_passive_conditional_after_an_object) {
    // A verb of acts done to someone is passive in the conditional after its object, and
    // potential with a recipient に.
    HD_CHECK(Translate("役目を命じられれば").starts_with("역할을 명령받"));
    HD_CHECK(Translate("彼に役目を命じられれば").starts_with("그에게 역할을 명할 수 "));
}
