#pragma once
// The restore stage of the text path: the cleaned-up pieces of a line compared with the
// pieces as typed, written in Korean forms and flattened to the output line, with the
// in-band marks of the earlier stages resolved.
#include <string>
#include <string_view>

#include "core/output/pieces.h"

namespace hdTrans::pipeline {

// The pieces after each step of Restore.
struct RestoreSteps {
    output::Pieces spaced;     // DropAddedSpaces
    output::Pieces bracketed;  // RestoreBrackets; equal to spaced without keepBrackets
    output::Pieces korean;     // KoreanForms and optional spelling replacements on text pieces
    std::string line;          // DropParticleMarks(WriteDrawls(WriteStutters(WriteDropped(String(korean)))))
};

// cleaned is the output of the cleanup, typed the pieces of the line as the protected
// span cut and the katakana rewrites leave them (before ｢｣ become 「」). With
// keepBrackets the brackets are written as typed (RestoreBrackets).
RestoreSteps RestoreAll(const output::Pieces& cleaned, const output::Pieces& typed, bool keepBrackets,
                        bool literalCleanup = false);

// The output line: RestoreAll(...).line.
std::string Restore(const output::Pieces& cleaned, const output::Pieces& typed, bool keepBrackets);

// out without the ASCII spaces the engine writes at the start of a line or after an
// opening bracket (「 나는 -> 「나는) where the matching line of typed has none there;
// out as it is when the line counts differ. Spans are left as they are.
output::Pieces DropAddedSpaces(const output::Pieces& typed, const output::Pieces& out);

// The brackets of typed written over the forms the engine writes for them, line by line
// (the whole text when the line counts differ): 『』 ｢｣ （） ’ 〝〟 come out as 「」 「」 () ＇
// ˝〃, and the n-th occurrence of such a form in out becomes the n-th bracket of typed
// written in it, with 〝〟 as “”. A form that occurs a different number of times in
// typed and out is left as it is. Spans are left as they are.
output::Pieces RestoreBrackets(const output::Pieces& typed, const output::Pieces& out);

// The ideographic space and the full-width ( ) [ ] { } < > in ASCII; a space before an
// opening “ that follows anything but whitespace or an opening bracket
// (제일의“문제아” -> 제일의 “문제아”).
std::string KoreanForms(std::string_view s);

// Literal spelling replacements; the first matching row wins and is not scanned again. A row
// marked final applies only after a Hangul syllable with a final consonant (있읍니다 -> 있습니다).
std::string KoreanCleanup(std::string_view s);

// Each run of kana that the composer shifted by DropShift (kana the translation does
// not render) written in Hangul as heard (internal::ReadKana).
std::string WriteDropped(std::string_view s);

// Each stutter mark U+E000 and the separator after it (、 ， or a run of … ‥ ― ─ — or of
// two or more ・) written as the next syllable of the text without its final and the
// separator, 、 and ， as ", " (U+E000、어이 -> 어, 어이). A run of k marks and r
// kStutterRest marks writes the share k/(k+r) of the next word's syllables, at least one
// and short of the whole word, with their finals (ごめ、 미안, of 미안해요). Spaces before a mark at the
// start of the line or after an opening bracket are dropped; a mark with no syllable
// after it (past spaces, marks and separators) is dropped with its separator.
std::string WriteStutters(std::string_view s);

// Each drawl span (a drawl mark, pause marks, the same mark) after a Hangul word written as
// its marks, in the Korean form of the tokenizer (・ ·), inside the word. For kDrawlMark
// they go before the polite ending: before 습니다 습니까 읍시다, else 니다 니까 시다 (먹겠…습니다,
// 입…니다), before 지요 까요 네요 군요, else before the last syllable (고마워…요), at the
// end of a one-syllable word. For kLeadDrawlMark they go before 입 or 합 of 입니다 합니다
// (좋아…합니다), for kPastLeadDrawlMark also before 이었 였 했 of 습니다 (조용…했습니다).
// Spaces between the word and the span go unless a word follows. A span with no Hangul
// word before it is dropped.
std::string WriteDrawls(std::string_view s);

// s without the particle marks of word spans (output::kVowelMark ...).
std::string DropParticleMarks(std::string_view s);

// The offset of the kana the composer writes for kana it drops.
constexpr char32_t kDropShift = 0xC000;
// The stutter mark of the sound stage.
constexpr char32_t kStutterMark = 0xE000;
// After the marks of a longer stutter piece, one mark for each kana of the word the piece
// does not reach (ごめ、 of ごめんなさい: 2 kStutterMark, 4 kStutterRest).
constexpr char32_t kStutterRest = 0xE004;
// Enclose the pause marks normalize cut out of a polite ending (いただきま…す), from
// before です or でしょう (好き…です) and from before でした, written after the ending
// (WriteDrawls).
constexpr char32_t kDrawlMark = 0xE005;
constexpr char32_t kLeadDrawlMark = 0xE006;
constexpr char32_t kPastLeadDrawlMark = 0xE007;

namespace internal {

// Kana in Hangul as heard, by text::FromKana under Fan (きょどきょど 쿄도쿄도, ぬちゃっ 누챳):
//   - ー, or a small vowel that repeats the vowel before it, adds a syllable of that
//     vowel (なぁ 나아, ふー 후우; at most four);
//   - a small kana at the start, after ッ, ン or ー, or a small vowel after a vowel
//     other than ウ reads as the plain one (ぅぅ 우우, えぃ 에이);
//   - a leading ン is 으 with the final ㅇ, or ㅁ before ㅁ ㅂ ㅍ, ㄴ before other
//     consonants and ㅅ before a final ッ (んむっ 음뭇, ん 응, んっ 읏).
// Kana that FromKana does not read are written by TransliterateKatakana, or kept when
// that writes nothing or fails.
std::string ReadKana(std::string_view kana);

// Every katakana run of s (ァ..ヺ and ー) in Hangul by the loanword spellings and the
// katakana matrix, the katakana no rule writes dropped. False when a context rule does
// not find the text it replaces; out is then unspecified.
bool TransliterateKatakana(std::string_view s, std::string& out);

}  // namespace internal

}  // namespace hdTrans::pipeline
