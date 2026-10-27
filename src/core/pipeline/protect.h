#pragma once
// The protect stage of the text path: the line after the sound lexicon cut into its
// protected spans and the text between them, with the rewrites that read the text
// pieces in the context of the spans.
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/output/pieces.h"
#include "core/pipeline/line_context.h"

namespace hdTrans::pipeline {

// The pieces the line is translated from, in this order:
//   - the protected-span cut (internal::CutSpans);
//   - a line of speech written in katakana read as hiragana (internal::KataSpeech);
//   - a katakana honorific after a katakana name, written in hiragana (internal::KataHonorifics);
//   - a pronoun, demonstrative or adjective written in katakana for emphasis read as
//     hiragana unless it may be a name (internal::KataWordsToHiragana);
//   - a hiragana nickname before ちゃん or くん written in Hangul
//     (internal::NicknamesToHangul);
//   - a conjunction cut from its clause at the start of a sentence written with its
//     pronoun (internal::RewriteConjunctions);
//   - ｢ and ｣ written as 「 and 」.
// sounded is the line after the sound lexicon and the emphasis rewrites.
output::Pieces Protect(std::string_view sounded, const LineContext& ctx, const dict::Snapshot& snap);

// Protect without the last step: the pieces as typed, which the cleanup compares the
// output with.
output::Pieces ProtectTyped(std::string_view sounded, const LineContext& ctx, const dict::Snapshot& snap);

// ｢ and ｣ in the text pieces written as 「 and 」.
output::Pieces MarkCornerBrackets(const output::Pieces& typed);

namespace internal {

// text cut into its protected spans and the text between them. The classes are
// searched one after the other, a later class never matching inside or across an
// earlier match:
//   - markup tags <...> of up to 1024 characters other than <BR> and <LI> and those holding
//     。、！？「」… (SpanOpen, a closing tag SpanClose);
//   - BBCode tags [b], [/b], [color=red] and the same in braces ({rb}, {/rt}) of up to
//     128 characters (SpanOpen, SpanClose);
//   - entities &amp; &#123; &#x1F;  (SpanOpen);
//   - inline style codes (StyleCodeLength, SpanOpen);
//   - the host markers, the longest that matches (SpanOpen);
//   - the stutter marks U+E000 with their separators (SpanOpen);
//   - the drawl spans U+E005, U+E006 or U+E007 to the same mark (SpanOpen);
//   - the emphasis marks U+E008 and U+E00D (SpanOpen) and U+E009 (SpanClose), the lengthening
//     mark U+E00A, a run of the drawn mark U+E00B and the cry marks U+E00E and U+E00F (SpanClose);
//   - Hangul runs with the masks in or after them (자○), joined by ASCII or full-width
//     spaces (SpanWord);
//   - the Han runs written in Chinese (ChineseRuns; SpanWord);
//   - runs of foreign characters other than white space (IsForeign) or of ◯, a ゔ
//     taking the small vowel after it (SpanWord);
//   - runs of marks of tone that name a thing, after の and before a particle
//     (あなたの♡を; SpanWord).
output::Pieces CutSpans(std::string_view text, std::span<const std::string> markers);

// The length of the inline style code at the start of s, or 0: a font %fNAME; (NAME up to
// 64 bytes without ;, % or a line feed), a colour #RRGGBB; or #RRGGBBAA;, or the reset #;.
// A colour or reset does not follow &, where it is a malformed entity (&#12345678;).
size_t StyleCodeLength(std::string_view s, char before);

// A character kept as written: outside CP932 and neither a Han character nor kana.
// Hangul, emoji, the letters and symbols of other scripts and the hiragana ゔゕゖ are
// foreign; ASCII, the half-width forms, ヷヸヹヺ and the marks of tone outside CP932
// (kToneMarks) are not: like ♪ they close an utterance rather than name a thing.
bool IsForeign(char32_t r);

// The hearts and notes that CP932 lacks, written after an utterance as ♪ is (すき♡).
constexpr std::u32string_view kToneMarks = U"♡♥❤❣♫♬";

// On each line whose katakana runs read as Japanese speech (ワタシハ ニホンゴガ
// ワカリマセン), the runs that read as words written in hiragana; katakana dictionary
// words inside a run stay. A line written only in katakana with the punctuation of
// speech, and a line without hiragana that writes its okurigana in katakana (痛イ……
// 沈マナイ), count as speech on weaker evidence. A span with Hangul next to the text
// counts as a sound already written.
output::Pieces KataSpeech(const output::Pieces& text, const LineContext& ctx, const dict::Snapshot& snap);

// A katakana honorific (サマ サン クン チャン ドノ) that ends a katakana run in the text
// pieces written in hiragana when the run before it is a name or no dictionary word
// and the whole run is no dictionary word (ヨウタサマ ヨウタさま); not after オ (オジサン).
output::Pieces KataHonorifics(const output::Pieces& text, const LineContext& ctx, const dict::Snapshot& snap);

// Each word of a short list (アレ, ドコ, アタシ, ダメ, ヤバイ, ...) in the text pieces
// written in hiragana where it reads as the word rather than a name: it fills its
// katakana run, does not follow ・ ＝ の or a span, is followed by a particle or
// predicate it takes (an adjective also by the end of a sentence), does not join the
// text around it into a dictionary word, has no plain or name sense and is not one of
// ctx.named. The stem of 行く in katakana before its hiragana conjugation (イっちゃう,
// イッた, イクの, イきそう) is written 行 the same way.
output::Pieces KataWordsToHiragana(const output::Pieces& text, const LineContext& ctx, const dict::Snapshot& snap);

// A nickname of two to four hiragana before ちゃん or くん in the text pieces written as a
// Hangul word span, its long vowels dropped (ゆうくん 유くん, みーちゃん 미ちゃん). It follows
// a character that is neither kana nor a kanji, or a one-kana particle after a kanji or
// katakana (私はゆうくん). It is not a dictionary word with its honorific and what follows
// (どんちゃん騒ぎ), a kin word (ねえちゃん), a word with お or ご, the adverb ちゃんと or a
// verb before くんだ or くんな (叩くんじゃねえ, 狙ってくんな).
output::Pieces NicknamesToHangul(const output::Pieces& text, const LineContext& ctx, const dict::Snapshot& snap);

// ならば なら ですから であれば にも関わらず にも拘らず にもかかわらず 対して at the
// start of a sentence or utterance written with their pronoun (それなら, だから,
// それでも, 反面) in the text pieces; なら before hiragana other than いい (ならいい)
// stays. A span next to a text piece reads as a character that is neither hiragana nor a
// line break.
output::Pieces RewriteConjunctions(const output::Pieces& text);

// The text of a text piece, "" for a span.
std::string_view TextOf(const output::Piece& p);

// The part-of-speech tags (first part-code bytes) of the base records and the plain
// entries stored under exactly s, kana spellings left out, and end rows too when endRows is
// false; "" when s is no key.
std::string KeyTags(std::string_view s, const LineContext& ctx, const dict::Snapshot& snap, bool endRows = true);

}  // namespace internal

}  // namespace hdTrans::pipeline
