// The reading of a verb's (ら)れ is decided once before selection (DecideVoice, spec GRM-10): an
// agent that is a person or an animal makes the passive and its に 에게; a word of ability, a
// trouble after its condition and a verb of feeling decide the rest, and a dictionary word keeps the
// candidates of its reading.
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
    "V\t1고\tS5:0\t\t\tGN\t0\t3\n"
    "と\t2면\tS500\t\t\tGN\t0\t4\n"
    "に\t에\tP200\tXXX\t\tGN\t0\t5\n"
    "に\t에게\tP200\t\t\tGN\t0\t6\n"
    "が\t(이)가\tP200\t\t\tGN\t0\t7\n"
    "は\t(은)는\tP200\t\t\tGN\t0\t8\n"
    "を\t(을)를\tP200\t\t\tGN\t0\t9\n"
    "彼\t그\tN120\tVHn\t\tGN\t0\t10\n"
    "猫\t고양이\tN110\tQAA\t\tGN\t0\t11\n"
    "窓\t창\tN110\tXXX\t\tGN\t0\t12\n"
    "誰でも\t누구라도\tN120\tVHn\t\tGN\t0\t13\n"
    "将来\t장래\tN110\t\t\tGN\t0\t14\n"
    "食べ\t&HU@\tM437\t\t\tGN\t0\t15\n"
    "案じ\t염려&AV@\tM437\t\t\tGN\t0\t16\n"
    "見られ\t볼 수 &HN@\tM447\tXXX\tKe\tGN\t0\t17\n"
    "見られ\t보&AJ@\tM427\t\t\tGN\t0\t18\n"
    "困\t곤란&Mf@\tM418\t\t\tGN\t0\t19\n"
    "任せ\t맡&A2@\tM437\t\t\tGN\t0\t20\n"
    "仕事\t일\tN110\t\t\tGN\t0\t21\n"
    "寝\t&AK@\tM427\t\t\tGN\t0\t22\n"
    "なれ\t될 수 &HN@\tM427\t\tKe\tGN\t0\t23\n"
    "なれ\t익숙해 &AL@\tM427\t\t\tGN\t0\t24\n"
    "連れてこられ\t끌려&AH@\tM427\tXXX\t\tGN\t0\t25\n"
    "連れてこられ\t데려올 수 &HN@\tM427\t\tKe\tGN\t0\t26\n"
    "な\t$FA@\tH7U0\t\tXn\tGN\t0\t27\n"
    "い\t1다\tT500\tXXX\t\tGN\t0\t28\n";

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

HD_TEST(word_voice_agent_makes_the_passive) {
    // A person or an animal before に is the agent: the passive, and the に 에게.
    HD_CHECK_EQ(Translate("彼に食べられた。"), "그에게 먹혔다.");
    HD_CHECK_EQ(Translate("猫に食べられた。"), "고양이에게 먹혔다.");
    // A thing before に is none, and the に of a verb of entrusting names who receives: the
    // potential stays and the person before に is the one entrusted (GRM-16), not an agent.
    HD_CHECK_EQ(Translate("窓に食べられた。"), "창에 먹을 수 있었다.");
    HD_CHECK_EQ(Translate("彼に仕事を任せられた。"), "그에게 일을 맡길 수 있었다.");
}

HD_TEST(word_voice_keeps_the_candidates_of_its_reading) {
    // A dictionary word in られ keeps the candidates of the reading a signal decides, and all of
    // them without one.
    HD_CHECK_EQ(Translate("彼に見られた。"), "그에게 보였다.");
    HD_CHECK_EQ(Translate("見られると困る。"), "보이면 곤란하다.");
    HD_CHECK_EQ(Translate("見られた。"), "볼 수 있었다.");
}

HD_TEST(word_voice_kuru_word_before_a_negative) {
    // A dictionary word in the られ of くる before a negative is the potential (GRM-10, 7).
    HD_CHECK_EQ(Translate("連れてこられない。"), "데려올 수 없다.");
    HD_CHECK_EQ(Translate("連れてこられた。"), "끌려왔다.");
}

HD_TEST(word_voice_no_passive_for_a_verb_without_object) {
    // An ichidan verb that takes no object has no passive a trouble could ask for, and なれ (なれる)
    // is no word in れ.
    HD_CHECK_EQ(Translate("寝られると困る。"), "잘 수 있으면 곤란하다.");
    HD_CHECK_EQ(Translate("なれると困る。"), "될 수 있으면 곤란하다.");
}

HD_TEST(word_voice_ability_and_feeling) {
    HD_CHECK_EQ(Translate("誰でも食べられる。"), "누구라도 먹을 수 있다.");
    HD_CHECK_EQ(Translate("将来が案じられる。"), "장래가 염려된다.");
    HD_CHECK_EQ(Translate("食べられる。"), "먹을 수 있다.");
}

HD_TEST(word_synthetic_causative_candidate_classifications) {
    auto translate = [](std::string_view rows, std::string_view input) {
        const auto loaded = dict::Load({{0, "base.tsv", std::string(kRows) + std::string(rows)}});
        HD_CHECK(loaded.errors.empty());
        if (loaded.snapshot == nullptr) return std::string();
        const auto generated = internal::NewGenerated(*loaded.snapshot);
        const std::string line(input);
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
        return out.empty() ? std::string() : out[0];
    };
    const std::string stem = "続け\t계속&AV@\tM437\tXXX\t\tGN\t0\t100\n";
    HD_CHECK_EQ(translate(stem, "仕事を続けさせられる。"), "일을 계속하게 된다.");
    const std::string negative = stem + "ません\t1지 않습니다\tS5:0\tXXX\tYn\tGN\t0\t102\n";
    HD_CHECK(translate(negative, "仕事を続けさせられません。").find("할 수 없") != std::string::npos);
    // A later selection may replace the default: mixed copula or particle-predicate candidates abstain.
    HD_CHECK(translate(negative + "続け\t&Ac@\tM437\t\t\tGN\t0\t101\n", "仕事を続けさせられません。")
                 .find("할 수") == std::string::npos);
    HD_CHECK(translate(negative + "続け\t계속&AV@\tM437\t\tPj\tGN\t0\t101\n", "仕事を続けさせられません。")
                 .find("할 수") == std::string::npos);
    // Dictionary-form classes use the same candidate even before a slot has been selected.
    const std::string experience = "考え\t생각&AV@\tM437\tXXX\t\tGN\t0\t100\n"
                                   "あること\t어떤 것\tN110\t\t\tGN\t0\t101\n";
    HD_CHECK(translate(experience, "あることを考えさせられる").find("생각하게 된다") != std::string::npos);
}
