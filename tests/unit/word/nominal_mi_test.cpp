// An i-adjective stem reads a following み as the nominal of speech (つらみ 괴로움) before a particle,
// punctuation or the end, unless a dictionary noun spells the stem and that ending (高み 높은 곳) or
// the stem is also a verb's (楽しみ of 楽しむ).
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "高\t$D3@\tH7M0\t\t\tGN\t0\t1\n"
    "甘\t$I1@\tH7M0\t\t\tGN\t0\t2\n"
    "高み\t높은 곳\tN110\t\t\tGN\t0\t3\n"
    "楽し\t즐$GH@\tH7M0\t\t\tGN\t0\t6\n"
    "楽し\t즐&A2@\tM445\t\t\tGN\t0\t7\n"
    "ます\t7니다\tS5:0\t\t\tGN\t0\t8\n"
    "み\t8\tT500\t\tPn\tGN\t0\t4\n"
    "い\t1다\tT500\t\t\tGN\t0\t5\n";

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

HD_TEST(word_dictionary_noun_over_the_nominal_of_an_adjective) {
    HD_CHECK(Translate("高み").find("높은 곳") != std::string::npos);
    HD_CHECK(Translate("甘み").find("곳") == std::string::npos);
    HD_CHECK(!Translate("甘み").empty());
}

HD_TEST(word_nominal_mi_not_on_a_verb_stem_or_before_a_verb_ending) {
    HD_CHECK(Translate("楽しみます").find("즐깁") != std::string::npos);
    HD_CHECK(Translate("甘みます").find("음") == std::string::npos);
}
