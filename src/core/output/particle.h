#pragma once
// Particle classes: which form of an alternating particle (이/가, 으로/로, ...) fits
// the text before it.
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "core/output/pieces.h"
#include "core/text/utf8.h"

namespace hdTrans::output {

// The text a particle follows, as one byte string without copying it: the pieces
// before, then head, then tail. In the pieces a word span reads as the syllable of
// its class (ClassSyllable of SpanFinal) and any other span as "*", a symbol that
// ParticleClass passes over.
class TextBefore {
public:
    explicit TextBefore(std::string_view tail);
    TextBefore(std::string_view head, std::string_view tail);
    TextBefore(std::span<const Piece> pieces, std::string_view tail);

    size_t Size() const { return piecesSize_ + head_.size() + tail_.size(); }
    unsigned char At(size_t i) const;

private:
    std::span<const Piece> pieces_;
    size_t piecesSize_ = 0;
    std::string_view head_;
    std::string_view tail_;
};

// The rune that ends the first end bytes of t: U+FFFD of size 1 for an invalid
// sequence, size 0 when end is 0.
text::Rune DecodeLastRune(const TextBefore& t, size_t end);

// The particle class of the text a particle follows: 'v' when the character that
// decides it is read without a final consonant, 'l' with the final ㄹ, 'c' with
// another final, and 0 when no character decides it. The deciding character is the
// last one of these:
//   - a Hangul syllable, read by its final; a consonant jamo is 'c' (ㄹ 'l'), a vowel
//     jamo 'v';
//   - a digit: the number it ends, read in Korean;
//   - a Latin letter: the word it ends, read in English;
//   - % (퍼센트), kana, and a kanji, read by its hanja reading ('v' without one).
// A line break, a comma, a full stop, an opening bracket, and ! or ? before a space end
// the search. Other characters (spaces, closing brackets and quotes, …, ! and ? right
// before the particle, symbols) are passed over: 「그」 is read as 그, 3… as 3, 「빵!」 as 빵.
char ParticleClass(const TextBefore& text);
char ParticleClass(std::string_view text);

// Rows of the alternating particles of dictionary targets: the pattern ("(이)가"),
// the form after a final consonant ("이") and the form after a vowel ("가").
struct JosaForms {
    std::string_view pattern;
    std::string_view consonant;
    std::string_view vowel;
};

constexpr int kJosaRows = 27;

// Two rows are equal; the first one matches.
std::span<const JosaForms, kJosaRows> JosaTable();

// The form of JosaTable row after a text of particle class final: the consonant form
// after 'c' and 'l' (but 로 after 'l'), the vowel form after 'v' and when nothing
// decides.
std::string_view ParticleForm(int row, char final);

namespace internal {

// Each alternating particle of text takes the form for the ParticleClass of context
// followed by the text before it. A particle that nothing decides, or that follows a
// context of only letters and digits (whose reading depends on the text before it:
// 2 m 미터), is left as it is.
std::string AlternateParticles(std::string_view text, std::string_view context);

// Whether a word ending in the ASCII characters a3 a2 a1 (a1 last) is read with a
// final consonant: digits by their Korean reading (0 1 3 6 7 8 yes), Latin letters
// by English spelling rules.
bool EndingConsonant(char a1, char a2, char a3);

}  // namespace internal

}  // namespace hdTrans::output
