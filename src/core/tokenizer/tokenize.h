#pragma once
// Splitting a chunk of text into typed tokens.
#include <functional>
#include <span>
#include <string_view>

#include "core/tokenizer/token.h"

namespace hdTrans::tokenizer {

// Splits the pieces into tokens and prepares them (Prepare). Each protected span
// becomes a token of its own, type 79 for a word span and 77 for the others, with the
// span text as its text. Text after a NUL is ignored and trailing spaces are dropped.
// No input gives no tokens; otherwise the last token is {kTypeEnd, ""}.
// With dashes (text input), a － before other than Japanese text and a run of two or
// more ― stay dashes after Japanese text instead of becoming ー (あんたが―― 네가――,
// ロンド－( 론도－(, but シャ－カ－ン 샤칸), and a 」 that closes the line's utterance
// is a token of its own.
// With joins, a space or an ideographic space between two kanji stays a space token of its own,
// so that the words on its sides are read apart (光 影, 副総理 兼 外務大臣), unless joins says
// that a word of the dictionary spans it (未 来, text spaced out character by character), and a
// Latin run after kana goes on with the Japanese text when a word spans the whole run (うｐ, おｋ)
// and no digit follows it. joins gets the text before the space or letter and the text after it,
// with the space left out and letters folded to ASCII. word says which words count (SpaceWord).
// With joins, any other single space or ideographic space between two Japanese characters that
// the text leaves out still parts two words: the Japanese token ends there and the next one is
// afterSpace, so that no word of the dictionary spans it (あ ごめんね, いや そうじゃない). Not
// parted are text spaced out character by character, two or more pieces of one character in a
// row up to the space and on after it when kanji stand on both sides of the space (未 来, 通 知
// 表が; before a last piece of more than one character only where joins finds a word across the
// space as SpaceWord::Spelled, not 私 先 行く) and three or more otherwise (こ の 展 開); the
// two-piece names and the kanji after
// katakana above (林 涛, ジモ得 週); a piece said again (やめて やめて) or starting with a small
// kana or a long mark (消す っていう); and the words joins finds across the space as
// SpaceWord::Binding (山田 太郎, 荒川アンダー ザ ブリッジ) or as SpaceWord::Phrase with the text
// before the space from its last space on (何だ あれ, ようこそ いらっしゃいました), and a last
// piece that joins finds as SpaceWord::Ending (そうだ ね……).
enum class SpaceWord {
    Any,     // a word that starts in the text before and goes on past it
    Binding, // a name other than a place's, or a predicate marked as a compound particle (gram Pj);
             // two characters or more before the space, or a name starting with one kanji (泊 明日菜)
    Phrase,  // a word that starts at the start of the text before, two characters or more on each side
    Ending,  // after is the text after the space up to a mark or the end, which ends what is before
    Filler,  // the whole piece before is an interjection, which must stay a separate word
    Spelled, // a word that starts in the text before and goes on past it to no hiragana but a particle or copula
};
using SpaceJoins = std::function<bool(std::string_view before, std::string_view after, SpaceWord word)>;
void TokenizeSpans(std::span<const Piece> pieces, bool dashes, Tokens& out, const SpaceJoins* joins = nullptr);

// TokenizeSpans of input as one text piece, without dashes.
void Tokenize(std::string_view input, Tokens& out);

// Visits every token in order as the per-token stage does: merges the control markers
// that follow a type-24 token into it, then canonicalises the text of punctuation,
// symbol, space and terminator types. Sets the merged and canonical views, clearedBy
// and the markers.
void Prepare(Tokens& tokens);

// Whether visiting a token replaces its text by its canonical form.
bool CanonicalType(int type);

// The canonical form of a non-empty token text: the FirstForm of its character when it is
// one character or starts with an exclamation or question mark (？！ ?), otherwise the
// concatenated canonical forms of its characters. In a run of two or more characters, a first
// character with a FirstForm and a character other than a kanji whose canonical form is only □
// stay as typed, so that a run of symbols keeps every character (％＆＄ ％＆＄, 〒♪ 〒♪, ／ヽ
// ／ヽ); trailing spaces dropped.
void AppendCanonicalText(std::string& out, std::string_view s);

// The leading run of exclamation and question marks of s in ASCII, one for one (！！ !!,
// ！？ !?, ‼ !!, ⁉ !?, ⁈ ?!); "" when s does not start with one. The canonical text keeps
// only the first mark; the output writes the whole run.
std::string MarkRun(std::string_view s);

}  // namespace hdTrans::tokenizer
