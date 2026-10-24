#pragma once
// The segment stage of the text path: a line cut into chunks that are translated one by
// one and joined by a space.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/output/pieces.h"

namespace hdTrans::pipeline {

// The pieces of one chunk of a line; each chunk is an independent translation.
using Chunk = output::Pieces;

// The chunks of the protected pieces of a line. A text piece is cut
//   - after a 、 that ends a clause (internal::Conjunctive) when at least 40 Japanese
//     characters precede it since the last cut or sentence end and at least 10
//     characters other than white space, a span counting as one, follow it before the
//     next sentence end;
//   - at an ideographic space between two words of a line (第一章　完, えっ！？　本当に),
//     and at an ASCII space that ends a phrase (internal::KanaSpace), unless the words
//     on both sides stand alone between spaces as in text spaced out character by
//     character (こ　の　展　開).
//   - at a column separator (internal::ColumnGap) and at an escape \n, \r or \t typed as two
//     characters (internal::EscapeGap); gaps, when given, receives for each chunk the
//     separator or escape before it as typed, "" after the other cuts and for the first.
// The white space after a cut is dropped. Spans are never cut and stay with the text
// before them.
std::vector<Chunk> Segment(const output::Pieces& pieces, std::vector<std::string>* gaps = nullptr);

namespace internal {

// Whether the ASCII space at s[i] ends a phrase: after a final particle before more
// Japanese (そうだね そうじゃない, すきだよ すきだよ), or between hiragana and a kanji
// when the hiragana ends neither a particle nor a dictionary or attributive form
// (ひろい集め 彼は, ほら 上も). Spaces of text written word by word (おなかが すきました,
// かがやく おでこ, 美しい 花) and between kanji or katakana words (大見 健) are not.
bool KanaSpace(std::string_view s, size_t i);

// Whether s ends in a conjunctive form that ends a clause before 、: て ば たら だら ても
// でも けど けれど けれども から ので のに ながら on their own; で after ん or い, し
// after い る だ す う く, が after た い る う す だ ん.
bool Conjunctive(std::string_view s);

// The end of the column separator that starts at s[i], or i where none does: a run of
// spaces, ideographic spaces and tabs that holds a tab or two or more characters, with
// characters of the line on both sides (lineBefore, lineAfter: the line goes on before
// or after s), unless a kanji standing alone is on each side, a two-character name
// padded to the width of three (林  涛). The leading white space of a line is none.
size_t ColumnGap(std::string_view s, size_t i, bool lineBefore, bool lineAfter);

// The end of the escape \n, \r or \t typed as two characters that starts at s[i], with the
// spaces, ideographic spaces and tabs before and after it, or i where none does: with
// characters of the line on both sides (lineBefore, lineAfter as for ColumnGap), the
// backslash after neither an ASCII letter or digit nor a backslash and the escape before no
// lowercase ASCII letter (C:\temp, \notag).
size_t EscapeGap(std::string_view s, size_t i, bool lineBefore, bool lineAfter);

// Whether the white space at s[i] stands between two words of a line: characters other
// than white space on both sides, not both of them alone between spaces.
bool BetweenWords(std::string_view s, size_t i);

}  // namespace internal

}  // namespace hdTrans::pipeline
