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
// so that the words
// on its sides are read apart (光 影, 副総理 兼 外務大臣), unless joins says that a word of
// the dictionary spans it (未 来, text spaced out character by character), and a letter after
// kana goes on with the Japanese text when a word spans both (うｐ, おｋ). joins gets the
// Japanese text before the space or letter and the text after it, the space left out and the
// letters folded to ASCII.
using SpaceJoins = std::function<bool(std::string_view before, std::string_view after)>;
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
