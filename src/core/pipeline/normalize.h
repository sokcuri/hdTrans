#pragma once
// The normalising rewrites of a line before the sound lexicon. They need no dictionary,
// except JoinBraceRuby, which asks the caller.
#include "core/pipeline/line_context.h"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace hdTrans::pipeline {

// Whether a dictionary word holds the run line[at, end) (じっっくり), so that it stays as written.
using WordHoldsRun = bool (*)(const void* context, std::string_view line, size_t at, size_t end);

// The dictionary lookups of CollapseTsuRuns, each optional: holds says whether a dictionary
// word holds the run line[at, end), so that it stays as written; start gives the
// start of the word that holds the っ at line[at] of the collapsed line, before which the stress
// mark goes, or npos.
struct RunLookup {
    WordHoldsRun holds = nullptr;
    size_t (*start)(const void* context, std::string_view line, size_t at) = nullptr;
    const void* context = nullptr;
};

// The line (without "\n" or a final "\r") after the normalising rewrites; the order is
// in normalize.cpp. A line that is written whole from the line table is not passed here.
// runs gives the lookups of CollapseTsuRuns.
std::string Normalize(std::string_view line, const LineContext& ctx, const RunLookup& runs = {});

// How the dictionary reads a ruby base with the kana after it.
struct BaseReading {
    // A verb or adjective starts with the base (背負って, 見惚れる, 些細な), not a noun
    // alone (安堵して, 杞憂って).
    bool continues = false;
    // The word analysis of base and kana starts with a verb or adjective of the base that
    // inflects in the kana (剥がそう, 潰さなきゃ).
    bool inflected = false;
    // The bytes of the kana that end the base's own word when another word follows it
    // in the kana (庇って|くれた, 掬った|まぐろ); 0 when the word takes the whole run.
    std::size_t wordEnd = 0;
};
using ReadBase = BaseReading (*)(const void* context, std::string_view base, std::string_view kana);

// line with the kana after each {rb}base{/rb}{rt}reading{/rt} moved into both tags, so
// that the base and the reading are each read as a whole word ({rb}悔{/rb}{rt}く{/rt}やんでも
// {rb}悔やんでも{/rb}{rt}くやんでも{/rt}): after a base of one character unless they start
// a particle, the copula or a name suffix ({rb}夢{/rb}{rt}ここ{/rt}で, {rb}蒼{/rb}{rt}あおい{/rt}くん)
// and the base is not inflected in them (剥がそう), after a longer base when the base
// continues. Only the kana up to the end of the base's word move when another
// word follows in them ({rb}庇って{/rb}{rt}かばって{/rt}くれた). Run after Normalize,
// with the dictionary.
std::string JoinBraceRuby(std::string_view line, ReadBase read, const void* context);

// Whether the variant at line[at, at + variant.size()) is part of another dictionary word and so
// stays as written (身の程, ときめく).
using WordKeepsVariant = bool (*)(const void* context, std::string_view line, size_t at, std::string_view variant,
                                  std::string_view spelling);

// line with each spelling of a grammatical word in classes.tsv spelling.variant (VARIANT=SPELLING)
// written in the spelling it names, so that both are read alike (痛い程に 痛いほどに, 見たとき
// 見た時): a kanji variant after hiragana, or after a kanji or katakana when
// spelling.variant-after-word lists it; a kana variant written in kanji only after a kana of
// spelling.variant-kana-after; another kana variant anywhere. A variant in a context of spelling.variant-kept (神の位) stays, and so does one where
// keeps says a dictionary word cuts across it or holds it, or it is in a Chinese run
// (internal::ChineseRuns). Run after Normalize, with the dictionary.
std::string RespellVariants(std::string_view line, WordKeepsVariant keeps, const void* context);

// line with each ending of classes.tsv spelling.katakana-ending written in katakana right after a
// hiragana, kanji, Latin letter or digit and before anything but a kana, kanji, letter or digit
// written in hiragana, its ー kept (私デース 私でーす, 楽しいネー 楽しいねー, 受信シマシタ 受信しました);
// spelling.katakana-copula as well, except after spelling.katakana-copula-kept-after or a kanji and
// spelling.katakana-copula-kept-after-kanji (言ったデース, 来たデース stay; あなたデス does not), spelling.katakana-ending-after-kana only after hiragana (行きマース 行きまーす, not 一マス) and
// spelling.katakana-particle only after hiragana or kanji (not Decemberネー).
std::string RespellKatakanaEndings(std::string_view line);

// line with each kanji of classes.tsv spelling.cry-kanji (KANJI=KANA) written in its kana where it
// starts an utterance, after anything but a letter or digit, and a run of kana.drawn-out or
// mark.clip follows it before anything but a letter or digit (嫌ぁぁぁ いやぁぁぁ, 嫌っ！ いやっ！;
// not 嫌っている, すごく嫌ー); a run of one drawn kana only before mark.exclaim or mark.comma
// (嫌ぁ！, not 嫌ぁ……).
std::string RespellKanjiCries(std::string_view line);

// line with each old form of a kanji in classes.tsv spelling.old-form (OLD=NEW) written in its
// new form, so that both read alike (格好惡い 格好悪い), unless holds says a dictionary word holds
// the old form (赤澤, 澤 on its own) or it is in a Chinese run (internal::ChineseRuns). Run after
// Normalize, with the dictionary.
std::string RespellOldForms(std::string_view line, WordHoldsRun holds, const void* context);

namespace internal {

// Composes only adjacent voiced/semivoiced kana letter pairs, without other form rewrites.
std::string ComposeKanaPairs(std::string_view text);

// Adjacent kana letter pairs with U+3099/U+309A composed, excluding iteration marks.
// Half-width katakana and punctuation other than ｢｣ in full width, a following voiced
// or semi-voiced mark composed (ｶﾞ ガ, ﾊﾟ パ, ｳﾞ ヴ; another mark becomes ゛ or ゜); a voiced
// mark after a vowel, ん, っ, ー or a small kana dropped (あ゛っ あっ, ｱﾞ ア), on ウ before a
// small vowel or a katakana letter composed (ウ゛ァ ヴァ); the CP932 mapping differences (〜 ～,
// − －, ‖ ∥, ¢ £ ¬ in full width) and kanji forms outside JIS X 0208 (頰 頬, 𠮟 叱) mapped;
// ‼ ⁉ ⁈ as two marks (！！ ！？ ？！) without a variation selector after them; U+0000..U+0003 as
// spaces. Invalid UTF-8 bytes are kept.
std::string NormalizeForms(std::string_view text);

// A run of two or more っ or ッ after a letter and before a letter other than a small kana, ー
// or ～ as its first character (褒められちゃっっっった 褒められちゃった, とっっても とっても),
// unless runs.holds says a dictionary word holds the run, and output::kStressMark before the
// word runs.start finds for it (すっっっごい \uE00Dすっごい). A run that ends a word (あっっ！) stays.
std::string CollapseTsuRuns(std::string_view text, const RunLookup& runs = {});

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

// A を repeated after a word written once (素振りをを見せる 素振りを見せる); a run of を
// that does not follow a word stays.
std::string CollapseDoubledWo(std::string_view text);

// The spoken and old-fashioned spellings the dictionary does not read in their plain
// form: an adjective or verb stem in katakana (ウザい うざい), じゃね？ and 良くね？,
// ねえ after は or も, the contracted ている and いる, じゃない of an appeal (じゃん) or a
// prohibition (なよ), a clipped adjective (怖っ！ 怖い！), the negative ん (知らん 知らない),
// できぬ; the ね or ん after a sentence-final かしら is dropped (どうしてかしらね).
std::string SpokenForms(std::string_view text, bool exclamationNa = false, bool dialectForms = false);

// あれ (also drawn out, あれ～？) and え at the start of a sentence before 、，！？…‥ or a
// dash with a small っ (あれ、 あれっ、, え―― えっ――, えぇ？ えっ？), but not あれ when a
// particle follows its …‥ or dash run
// (あれ――は何だ), え after an ellipsis or a dash before a question mark as えっ
// (へ……え？ へ……えっ？), じゃあ before a word with a comma (じゃあどっち じゃあ、どっち).
std::string RewriteInterjections(std::string_view text);

// Words said with an emphatic っ or ん inside written plain (さっすが さすが, しっかたない
// しかたない, すんごい すごい, あったりまえ あたりまえ, くっさい くさい, バッカ バカ, ばっかじゃ
// バカじゃ where a clause starts).
std::string RewriteEmphaticSpellings(std::string_view text);

// Casual contractions written in full (見てねぇな 見てないな, わかんねぇ わからない,
// やらねー やらない, 渡しちまった 渡しちゃった, なめんなよ なめるなよ, 貰っとく 貰っておく,
// 置いてく 置いていく, 行くんすか 行くんですか, わかるっス わかるっす, そだね そうだね). With runs.holds,
// a contracted ておく that a dictionary word holds stays as written (言っとくけど).
std::string RewriteCasual(std::string_view text, const RunLookup& runs = {});

// Four words of the military time in digits (マルフタマルマル 0200, ヒトヨン、マルマル
// 1400, ヒトハチ～マルマル 1800, マルサン…マルマル 0300) when no kana follows, or when
// hiragana follows a time of day (マルロクマルマルです).
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
    bool quoted = false;  // the line holds an utterance in 「」
};
SpeechCues ReadSpeechCues(std::string_view line);
bool FemaleSpeech(std::span<const std::string> lines);

// line without ruby markup, keeping the base text: |漢字《かな》, 漢字《かな》 with a kana reading and
// HTML <ruby>.
std::string StripRuby(std::string_view line);

}  // namespace internal

}  // namespace hdTrans::pipeline
