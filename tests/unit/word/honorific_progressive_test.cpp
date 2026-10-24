// The ている of a verb read as an honorific is 고 계시다 (読まれている 읽고 계신다), with the いる in
// the auxiliary or in the ending of a contracted てる; a result verb is 아 계시다 and 亡くなる the
// past. A verb whose Korean ends in 시다 without being an honorific (飲む 마시다) keeps 고 있다.
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "陛下\t폐하\tN110\tVHn\t\tGN\t0\t1\n"
    "は\t(은)는\tP200\t\t\tGN\t0\t2\n"
    "本\t책\tN110\t\t\tGN\t0\t3\n"
    "を読まれ\t(을)를 읽으&AE@\tM427\t\tTuPj\tGN\t0\t4\n"
    "書\t&AF@\tM431\t\t\tGN\t0\t5\n"
    "を\t(을)를\tP200\t\t\tGN\t0\t6\n"
    "手紙\t편지\tN110\t\t\tGN\t0\t7\n"
    "仰\t말씀하&AE@\tM418\t\t\tGN\t0\t8\n"
    "亡くなられ\t돌아가&AE@\tM417\t\t\tGN\t0\t9\n"
    "飲\t마&AE@\tM435\t\t\tGN\t0\t10\n"
    "酒\t술\tN110\t\t\tGN\t0\t11\n"
    "い\t&HN@\tM427\tXXX\t\tGN\t0\t12\n"
    "V\t1고\tS570\tXXX\t\tGN\t0\t13\n"
    "るから\tA니까\tS500\tXXX\t\tGN\t0\t14\n"
    "るから\t1고 있으니까\tS570\tXXX\t\tGN\t0\t15\n";

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv", std::string(kRows)}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_honorific_progressive_of_an_honorific_row) {
    HD_CHECK_EQ(Translate("陛下は本を読まれているから"), "폐하는 책을 읽고 계시니까");
    HD_CHECK_EQ(Translate("陛下は仰っているから"), "폐하는 말씀하고 계시니까");
}

HD_TEST(word_honorific_progressive_in_a_contracted_ending) {
    HD_CHECK_EQ(Translate("陛下は手紙を書かれてるから"), "폐하는 편지를 쓰고 계시니까");
    HD_CHECK_EQ(Translate("陛下は本を読まれてるから"), "폐하는 책을 읽고 계시니까");
}

HD_TEST(word_honorific_progressive_of_a_result_verb) {
    HD_CHECK_EQ(Translate("陛下は亡くなられているから"), "폐하는 돌아가셨으니까");
}

HD_TEST(word_honorific_progressive_not_for_a_verb_in_sida) {
    HD_CHECK_EQ(Translate("陛下は酒を飲んでいるから"), "폐하는 술을 마시고 있으니까");
}
