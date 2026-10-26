#pragma once
// Lines broken in the middle of a sentence (Session::SetWrapped): which line goes on in the
// next one, and the split of the translation of joined lines back into its lines.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/output/item.h"

namespace hdTrans::internal {

// A word of a line as the translation of the line alone resolved it.
struct WrapWord {
    std::string surface;  // the word, a verb or adjective without its ending
    std::string code;     // part code without its major tag ("211")
    output::DetailClass detail = output::DetailClass::None;
    int kind = -1;         // the conjugation branch of a verb (10 the 連用 form), -1 without one
    std::string ending;    // the Japanese ending ("なければ")
    std::string koEnding;  // the Korean of the ending, with its form code ("1고")
};

// The first and last words of a line, symbols aside, how many words it has and whether a word
// before the last one is a verb or an adjective.
struct WrapEdges {
    std::optional<WrapWord> first;
    std::optional<WrapWord> last;
    int words = 0;
    bool predicate = false;
};

// The arrows a caption ends with when its sentence goes on in the next caption: ➡, ￫, → and ⇒.
inline constexpr std::string_view kArrows[] = {"➡", "￫", "→", "⇒"};

// The arrow line ends with, white space aside; empty when it ends in none.
std::string_view ArrowOf(std::string_view line);

// line without the arrow it ends with and the white space before the arrow.
std::string WithoutArrow(std::string_view line);

// The symbol after which the stages read the last word of a line as going on (与える➡ 주는); →, ⇒
// and the other marks that the tokenizer takes as Shift-JIS symbols end it (与える→ 준다→).
inline constexpr std::string_view kGoesOn = "➡";

// The text that line is translated as on its own: without the arrow it ends with and the white
// space before the arrow, and with kGoesOn in their place when a letter stands before them, where the
// sentence goes on (与える➡), not a mark that closes it (らしい。➡).
std::string AloneText(std::string_view line);

// translated with arrow written after its last character other than white space, after one space.
std::string WithArrow(std::string_view translated, std::string_view arrow);

// The edges of a word chain resolved for line, added to edges (the chains of a line come in order).
void AddWrapEdges(const std::vector<output::ResolvedItem>& items, WrapEdges& edges);

// Whether the sentence that line ends with cannot end there and goes on in next: line ends in an
// arrow (ArrowOf), a comma, a case particle, an attributive, a conditional or connective form, an
// adverb with no predicate before it (もしよかったら一緒に), or じゃ or では before the ない of next, a
// word before a quotative particle at the start of next, a て form before an auxiliary, a predicate
// before a word that needs one (こと, はず), or a bare verb or adjective before a noun at the start of
// next (与える / 赤紫と青紫色のオッドアイ). edges are those of line
// without its arrow. A blank line, a line that ends in another mark or a bracket, a line that holds
// kBreakMark, and a next line that starts an item of its own (a bracket, a list or heading mark, a
// speaker, turn or voice mark, a speaker label) never go on.
bool GoesOn(std::string_view line, const WrapEdges& edges, std::string_view next, const WrapEdges& nextEdges);

// The span piece (U+E010) put between joined lines before the chunks are cut, and the control
// marker the chunks write for it into the translation, at the word the break stands before.
inline constexpr std::string_view kBreakSpan = "\xEE\x80\x90";
inline constexpr std::string_view kBreakMark = "&#CS";

// The translation of count joined lines split at its break marks, the spaces around each mark
// dropped; empty unless there are count - 1 marks and no piece is empty.
std::vector<std::string> SplitAtBreaks(std::string_view translated, std::size_t count);

}  // namespace hdTrans::internal
