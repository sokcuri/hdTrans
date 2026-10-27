#pragma once
// Character classes of the tokenizer.
#include <string>
#include <string_view>

namespace hdTrans::tokenizer {

// ClassFlags(c, 15).
int Class(char32_t c);

// The class of c under flags: 24 for kanji (flag 1) or ・ 々 ー (flag 8), 13 for
// hiragana ぁ-ゖ (flag 2), 14 for katakana ァ-ヺ and ー (flag 4), else 0. Kanji are the
// Han characters, including 〇, except the IBM and NEC extension kanji of CP932 and
// 仝 々 (IsNonKanjiHan).
int ClassFlags(char32_t c, int flags);

// A character the text front end keeps as written: outside CP932 and neither a Han
// character nor katakana. Hangul, emoji, the letters and symbols of other scripts and
// the hiragana ゔゕゖ are foreign; ASCII, the half-width forms and ヷヸヹヺ are not.
bool Foreign(char32_t c);

// Whether the ASCII text of a token is a number written with signs (50%, 100$, +1, 1~6, 3-3-6):
// an optional + or -, then digits, . , - ~ and + with a digit first and a digit or a unit sign
// (% $) last. Such a token is a word. Times and dates (10:30, 3/20) are not.
bool SignedNumber(std::string_view s);

// Appends the canonical form of c (CanonicalForm), or c itself.
void AppendCanonical(std::string& out, char32_t c);

}  // namespace hdTrans::tokenizer
