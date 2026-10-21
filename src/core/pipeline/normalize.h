#pragma once
// The normalising rewrites of a line before the sound lexicon. They need no dictionary.
#include "core/pipeline/line_context.h"
#include <span>
#include <string>
#include <string_view>

namespace hdTrans::pipeline {

// The line (without "\n" or a final "\r") after the normalising rewrites; the order is
// in normalize.cpp. A line that is written whole from the line table is not passed here.
std::string Normalize(std::string_view line, const LineContext& ctx);

namespace internal {

// Half-width katakana and punctuation other than ｢｣ in full width, a following voiced
// or semi-voiced mark composed (ｶﾞ ガ, ﾊﾟ パ, ｳﾞ ヴ; a mark that does not compose becomes
// ゛ or ゜); the CP932 mapping differences (〜 ～, − －, ‖ ∥, ¢ £ ¬ in full width) and
// kanji forms outside JIS X 0208 (頰 頬, 𠮟 叱) mapped; ‼ ⁉ ⁈ as two marks (！！ ！？ ？！)
// without a variation selector after them; U+0000..U+0003 as spaces. Invalid UTF-8 bytes
// are kept.
std::string NormalizeForms(std::string_view text);

// ごめん, ゴメン, おやすみ, おかえり, お帰り joined to a なさい cut off by 、，…‥・.
std::string JoinNasai(std::string_view text);

// A verb stem joined to ます, ました, ません, ましょう cut off by 、，…‥ (ござい、ます); ます,
// ました, ません, ましょう, です, でしょう cut inside by 、，…‥・―─ (いただきま…す, 好き……で……す)
// or spelled まっす, まス, でっす joined; both when no kana or ー ～ follows, or after a cut
// by …‥・―─ only, when か ね よ わ かね よね わね わよ and no kana follow. Not for the word
// いま or 今ま, nor before 、 or ， and a restart (ここで…す、すみません). Marks of …‥・―─ only
// are written after the ending as a drawl span (kDrawlMark), others are dropped.
std::string JoinMasu(std::string_view text);

// A word joined to です, でした, でしょう cut off by …‥・―─ when no kana or ー ～ follows,
// or the particles of JoinMasu and no kana (好き…です 好きです, 静か…ですね). The marks are
// written after the ending as a drawl span (kLeadDrawlMark, for でした kPastLeadDrawlMark).
// Only after a word that can be a predicate: not after a particle, a filler or a
// conjunction (それは, えっと, つまり, だから) or a request form (ください, なさい).
std::string JoinPausedCopula(std::string_view text);

// The spoken and old-fashioned spellings the dictionary does not read in their plain
// form: an adjective or verb stem in katakana (ウザい うざい), じゃね？ and 良くね？,
// ねえ after は or も, the contracted ている and いる, じゃない of an appeal (じゃん) or a
// prohibition (なよ), a clipped adjective (怖っ！ 怖い！), the negative ん (知らん 知らない),
// できぬ.
std::string SpokenForms(std::string_view text, bool exclamationNa = false, bool dialectForms = false);

// あれ and え at the start of a sentence before 、，！？…‥ or a dash with a small っ (あれ、 あれっ、,
// え―― えっ――, えぇ？ えっ？), but not あれ when a particle follows its …‥ or dash run
// (あれ――は何だ), え after an ellipsis or a dash before a question mark as えっ
// (へ……え？ へ……えっ？), じゃあ before a word with a comma (じゃあどっち じゃあ、どっち).
std::string RewriteInterjections(std::string_view text);

// Casual contractions written in full (見てねぇな 見てないな, わかんねぇ わからない,
// やらねー やらない, 渡しちまった 渡しちゃった, なめんなよ なめるなよ, 貰っとく 貰っておく,
// 置いてく 置いていく).
std::string RewriteCasual(std::string_view text);

// Four words of the military time in digits (マルフタマルマル 0200, ヒトヨン、マルマル
// 1400) when no kana follows.
std::string RewriteMilitaryTime(std::string_view text);

// The elder-sibling words of line in a woman's Korean (お兄様 오빠, お姉ちゃん 언니) when
// female is set and the line shows no man's speech, or when the line itself shows a
// woman's speech.
std::string WriteKinTerms(std::string_view line, bool female);

// A document whose utterances (「」 and 『』 text) show a woman's speech and whose lines
// with a kin term never show a man's speech in an utterance holding the kin term.
struct SpeechCues {
    bool female = false;
    bool maleKin = false;
};
SpeechCues ReadSpeechCues(std::string_view line);
bool FemaleSpeech(std::span<const std::string> lines);

// line without ruby markup, keeping the base text: |漢字《かな》, 漢字《かな》 and HTML
// <ruby>.
std::string StripRuby(std::string_view line);

}  // namespace internal

}  // namespace hdTrans::pipeline
