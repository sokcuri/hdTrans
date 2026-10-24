#pragma once
// Output text as pieces: runs of text, and the texts of protected spans, which the
// cleanup leaves as they are.
#include <string>
#include <string_view>
#include <vector>

#include "core/text/lifetime.h"

namespace hdTrans::output {

// Span kinds of a piece.
enum SpanKind : int {
    SpanText = 0,   // translated text
    SpanWord = 1,   // Hangul or foreign text, read as a noun
    SpanOpen = 2,   // tag, entity, host marker, stutter marks
    SpanClose = 3,  // closing tag
    SpanGap = 4,    // a column separator between chunks, written as typed (pipeline::Segment)
};

// A particle mark at the end of a word span: the span takes the particles of a word
// that ends in a vowel (U+E001), a consonant (U+E002) or ㄹ (U+E003) (섹○ of 섹스). The
// restore stage drops it.
constexpr char32_t kVowelMark = 0xE001;
constexpr char32_t kConsonantMark = 0xE002;
constexpr char32_t kRieulMark = 0xE003;

// The marks the sound stage writes around a word spelled out with ・ for emphasis
// (さ・い・て・い), an opening and a closing span that the restore stage writes as · between
// the syllables of the word; the closing mark does not hide the sentence end after it.
constexpr char32_t kEmphasisOpen = 0xE008;
constexpr char32_t kEmphasisClose = 0xE009;

// The mark the sound stage writes after a word whose last syllable is lengthened
// (せんぱーい), a closing span that the restore stage writes as ~; a sentence end after it
// is still the end of the word's sentence.
constexpr char32_t kLengthenMark = 0xE00A;

// The mark the sound stage writes, once for each kana, after a word drawn out by three or
// more vowel kana (やめてぇぇぇ, 待ったああああ), a closing span that the restore stage writes
// as a syllable drawing out the vowel of the Korean syllable before it (그만둬어어어); a
// sentence end after it is still the end of the word's sentence.
constexpr char32_t kDrawnMark = 0xE00B;

// The mark a construction writes after the ending of the last copy of a cry clipped with っ
// (痛い痛いっ), which the restore stage drops, closing the open Hangul syllable before it with ㅅ
// (아파아팟).
constexpr char32_t kClipMark = 0xE00C;

// The mark normalize writes before a word said with a run of っ inside (すっっっごい), an opening
// span that the restore stage writes as the first syllable of the Korean after it drawn out by
// two syllables of its vowel (대애애단해, WriteStress).
constexpr char32_t kStressMark = 0xE00D;

// The mark the sound stage writes after an adjective drawn out at the end of a phrase by one or
// two vowel kana (痛いぃ, 熱いいいっ), whose drawl is not written: a closing span that the restore
// stage drops. Like kDrawnMark, it makes the adjective a cry.
constexpr char32_t kCryMark = 0xE00E;
// The cry mark of such an adjective that took a shout's っ after it (熱いいいっ): dropped too, but
// copies said again take the clip on their last syllable (熱いぃっ、熱いぃっ 뜨거웟, 뜨거웟).
constexpr char32_t kClippedCryMark = 0xE00F;

constexpr bool IsCryMark(char32_t c) { return c == kCryMark || c == kClippedCryMark; }

// Whether c is a mark the sound stage writes at the drawn-out end of a word (kLengthenMark,
// kDrawnMark, kClipMark, kCryMark, kClippedCryMark).
constexpr bool IsDrawnEndMark(char32_t c) {
    return c == kLengthenMark || c == kDrawnMark || c == kClipMark || IsCryMark(c);
}

// Whether c is a closing mark of the sound stage: kEmphasisClose or a drawn end mark. A
// sentence end after the marks is still the end of the word's sentence.
constexpr bool IsClosingMark(char32_t c) { return c == kEmphasisClose || IsDrawnEndMark(c); }

struct Piece {
    std::string text;
    SpanKind span = SpanText;
};

// No text piece is empty and no two text pieces follow each other; Add and Append
// keep that.
using Pieces = std::vector<Piece>;

std::string String(const Pieces& p);

// Appends text, joining it to a text piece at the end.
void Add(Pieces& p, std::string_view text);

// Appends the pieces of q.
void Append(Pieces& p, const Pieces& q);
void Append(Pieces& p, Pieces&& q);

// Splits only text pieces at newlines, preserving empty lines and protected spans.
std::vector<Pieces> Lines(const Pieces& text);
Pieces JoinLines(const std::vector<Pieces>& lines);

// The first piece when it is text, otherwise "".
std::string_view Head(const Pieces& p HDTRANS_LIFETIMEBOUND);

// Drops the ASCII spaces that end the last piece when it is text; the piece goes
// when nothing is left of it.
void TrimRight(Pieces& p);

// Drops the ASCII spaces that start the first piece when it is text; the piece
// goes when nothing is left of it.
void TrimLeft(Pieces& p);

}  // namespace hdTrans::output
