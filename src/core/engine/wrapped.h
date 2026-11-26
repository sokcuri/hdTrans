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
    int lastLength = 0;   // bytes of the source text the last word covers
    int firstLength = 0;  // bytes of the source text the first word covers
    bool predicate = false;
    // The kana the line read alone left unread before its first word read (the る of るとか, the
    // げる of げるしかない); empty when the line starts with a word read.
    std::string unreadStart;
    bool wordRead = false;  // a word of the line was read, which ends unreadStart
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

// Where the break between two lines may fall inside a word (CutRun): the start of the next line
// that the word read across the break must cover, and whether the break may fall in its stem.
struct WrapCut {
    std::string run;
    bool stem = true;     // false: only in its ending (CutEnding)
    bool common = false;  // a kanji after a kanji: a proper noun needs a kanji, katakana or の after the run
                          // (大阪 / 府の, 横 / 浜市の; not 山 / 川が as 山川 야마카와)
};

// The cut between line and next; an empty run when the break may not fall inside a word. line ends in
// kana or kanji, with neither a blank line nor an item of its own (StartsItem) between them, and
//  - next starts with kana that line left unread (WrapEdges::unreadStart: 始め / るとか, 逃 / げるしかない);
//  - or line ends in a kanji and next starts with a word of one kanji (技 / 術の), or line ends in a
//    word of one kanji (楽 / しめる): the run is the first word of next, after a kanji that no digit
//    stands before (not 29日 / 次の), and no word of wrap.kanji-word-start (先生 / 方がいい, 中 / 今、);
//  - or both sides of the break are kana and line has more words than one: the run is the first kana
//    of next, cut in an ending only (逮捕されま / した, not うん / で).
// edges are those of line, nextEdges those of next. The joined lines decide whether one word is read
// across the break (CutStem, CutEnding, ReadWord).
WrapCut CutRun(std::string_view line, const WrapEdges& edges, std::string_view next, const WrapEdges& nextEdges);

// Whether head, the part of a word read across a break that stands before it, and run, the unread
// kana after the break, cut one word: head holds a kanji (始め / る, 逃 / げる), run starts with a
// small kana or ン that starts no word (こ / りゃ), or both are katakana and a kana or kanji, after
// stands after run (ラ / ノベって).
bool CutStem(std::string_view head, std::string_view run, char32_t after);

// Whether a break inside the ending of a word read across it cuts the word: run starts with a small
// kana or ン (変わるでし / ょう), or line, of edges, read alone does not end in a conjugated word with an
// ending (見られていま / す, not 思います / よう).
bool CutEnding(std::string_view run, const WrapEdges& edges);

// Whether item, read from the joined lines, is a word that the dictionary or a conjugation read.
bool ReadWord(const output::ResolvedItem& item);

// The span piece (U+E010) put between joined lines before the chunks are cut, and the control
// marker the chunks write for it into the translation, at the word the break stands before.
inline constexpr std::string_view kBreakSpan = "\xEE\x80\x90";
// The span piece (U+E011) put in place of kBreakSpan where the break falls inside a word (MayCutWord).
inline constexpr std::string_view kCutBreakSpan = "\xEE\x80\x91";
inline constexpr std::string_view kBreakMark = "&#CS";

// The translation of count joined lines split at its break marks, the spaces around each mark
// dropped; empty unless there are count - 1 marks and no piece is empty.
std::vector<std::string> SplitAtBreaks(std::string_view translated, std::size_t count);

}  // namespace hdTrans::internal
