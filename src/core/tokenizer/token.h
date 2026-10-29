#pragma once
// Tokens of one chunk of text, with every view of a token the later stages read.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/text/lifetime.h"

namespace hdTrans::tokenizer {

// Token types. The scanner emits 1-9, 17, 18, 22, 24, 26, 77 and 79. 13 and 14 are
// ClassFlags results. 23 is accepted by the per-token stage but never emitted; 15, 16,
// 20 and 78 are accepted (as 1, 2, 18/22 and 26) but nothing produces them.
constexpr int kTypeNumber = 1;       // ASCII digits, full-width digits folded to ASCII, "1,000"
constexpr int kTypeAlpha = 2;        // ASCII/full-width letters (folded) and what follows them: "a1", "C:\x", "1.5"
constexpr int kTypeAsciiPunct = 3;   // . , ? !
constexpr int kTypeAsciiClose = 4;   // " ] } >
constexpr int kTypeAsciiOpen = 5;    // [ { <
constexpr int kTypeNewline = 6;      // CR, LF
constexpr int kTypeAsciiSpace = 7;   // space, TAB, VT; other controls and Unicode spaces become a space
constexpr int kTypeAsciiSymbol = 8;  // other printable ASCII
constexpr int kTypeEnd = 9;          // final token, empty text
constexpr int kClassHiragana = 13;   // ぁ-ゖ
constexpr int kClassKatakana = 14;   // ァ-ヺ, ー
constexpr int kTypeSentenceEnd = 17; // 。．？！, sentence-final ・, ー ～ - closing a hiragana run
constexpr int kTypeBracket = 18;     // full-width brackets and quotes not in 24, ＜ ＞
constexpr int kTypeSymbol = 22;      // any other character; half-width ｡｢､･ﾞﾟ widened
constexpr int kTypeTruncated = 23;
constexpr int kTypeJapanese = 24;    // kanji/kana runs incl. 、「」『』（）＝“”～○＊ and ASCII ( ) '
constexpr int kTypeWideSpace = 26;   // U+3000
constexpr int kTypeMarkup = 77;      // <tag>, "&#CS", emptied tokens, opaque spans
constexpr int kTypeProtected = 79;   // word span

// Span kinds. A span is a protected run of text that the stages do not read as
// Japanese and the output writes as it is.
constexpr int kSpanWord = 1;   // Hangul or foreign text, read as a noun (type 79)
constexpr int kSpanOpen = 2;   // tag, entity, host marker, stutter marks (type 77)
constexpr int kSpanClose = 3;  // closing tag (type 77)

// A run of text, or with span != 0 a protected span of that kind.
struct Piece {
    std::string text;
    int span = 0;
};

// Where a control marker was removed: the token that absorbed the following text, the
// length in bytes of its merged text at the marker, and the kind (1 = "&#CS",
// 2 = "</a>" or "</A>").
struct ControlMarker {
    int tokenIndex;
    int textLength;
    int markerKind;
    bool inWord = false;  // a break of joined lines that falls inside a word
};

// A text of Tokens::text.
struct TextRef {
    uint32_t offset = 0;
    uint32_t size = 0;
};

// One token. The per-token stage visits the tokens in order; visiting token i first
// merges control markers that follow it (which may append later tokens' text to it and
// empty those tokens), then replaces the text of punctuation-like types by its
// canonical form. The views hold the text at each of those points:
//   raw        as scanned
//   merged     raw plus the text merged in when the token is visited; also the token
//              as typed, which later stages read for the characters before a word.
//              Empty for a token emptied by an earlier token's merge.
//   canonical  merged, canonicalised for types 3 6 9 17 18 20 22 23 26 78
// A span token has the span text in all three views.
struct Token {
    int type = 0;        // as scanned
    int span = 0;        // span kind, 0 for scanned text
    int clearedBy = -1;  // the token whose merge emptied this one (type 77, text ""), or -1
    TextRef raw;
    TextRef merged;
    TextRef canonical;
    // Bytes of the input (the pieces' texts joined) the token was scanned from; empty
    // at the position of an empty token.
    uint32_t sourceBegin = 0;
    uint32_t sourceEnd = 0;
    // A type-24 token that goes on from the type-24 token before it across a space the
    // text leaves out, a word boundary (TokenizeSpans).
    bool afterSpace = false;
};

// A token as an observer sees it: type and text.
struct TokenView {
    int type;
    std::string_view text;
};

// The tokens of one chunk and the storage of their texts. Reusing one Tokens for
// many chunks reuses its buffers.
struct Tokens {
    std::vector<Token> tokens;
    std::vector<ControlMarker> markers;  // in token order
    std::string text;

    size_t size() const { return tokens.size(); }
    const Token& operator[](size_t i) const { return tokens[i]; }
    std::string_view Text(TextRef r) const HDTRANS_LIFETIMEBOUND { return std::string_view(text).substr(r.offset, r.size); }
    std::string_view Raw(size_t i) const HDTRANS_LIFETIMEBOUND { return Text(tokens[i].raw); }
    std::string_view Merged(size_t i) const HDTRANS_LIFETIMEBOUND { return Text(tokens[i].merged); }
    std::string_view Canonical(size_t i) const HDTRANS_LIFETIMEBOUND { return Text(tokens[i].canonical); }

    // The type after the token has been visited: 77 when it was emptied.
    int TypeAfter(size_t i) const { return tokens[i].clearedBy >= 0 ? kTypeMarkup : tokens[i].type; }

    // Token j as seen while token `at` is visited, after its merge: tokens up to `at`
    // are canonical, later tokens raw unless a merge up to `at` emptied them.
    TokenView View(size_t j, size_t at) const {
        const Token& t = tokens[j];
        if (j <= at) return {TypeAfter(j), Canonical(j)};
        if (t.clearedBy >= 0 && static_cast<size_t>(t.clearedBy) <= at) return {kTypeMarkup, {}};
        return {t.type, Raw(j)};
    }

    // Scanner buffers.
    struct SpanAt {
        uint32_t position;
        uint32_t piece;
    };
    std::vector<char32_t> chars;
    std::vector<uint32_t> offsets;
    std::vector<SpanAt> spans;
    std::string buffer;
};

}  // namespace hdTrans::tokenizer
