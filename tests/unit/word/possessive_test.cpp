#include <memory>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "私\t나\tN120\tVHn\t\tGN\t0\t1\n"
    "わたくし\t저\tN120\tVHn\t\tGN\t0\t2\n"
    "君\t너\tN120\tVHn\t\tGN\t0\t3\n"
    "彼\t그\tN120\tVHn\t\tGN\t0\t4\n"
    "の\t의\tP211\t\t\tGN\t0\t5\n"
    "本\t책\tN110\t\t\tGN\t0\t6\n"
    "本当に\t정말로\tE311\t\t\tGN\t0\t8\n"
    "方\t(분)편\tN110\t\t\tGN\t0\t7\n";

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

HD_TEST(word_short_possessive_before_a_word) {
    HD_CHECK_EQ(Translate("私の本"), "내 책");
    HD_CHECK_EQ(Translate("わたくしの本"), "제 책");
    HD_CHECK_EQ(Translate("君の本"), "네 책");
    // A target with a gloss after the pronoun keeps the space.
    HD_CHECK_EQ(Translate("君の方"), "네 편");
}

HD_TEST(word_short_possessive_keeps_the_genitive) {
    HD_CHECK_EQ(Translate("彼の本"), "그의 책");
    HD_CHECK_EQ(Translate("私の……本"), "나의……책");
    HD_CHECK_EQ(Translate("私の。"), "나의.");
    HD_CHECK_EQ(Translate("本当に私の?"), "정말로 나의?");
}
