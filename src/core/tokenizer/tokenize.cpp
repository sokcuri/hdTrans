#include "core/tokenizer/tokenize.h"

#include <cstdint>
#include <algorithm>
#include <cstring>

#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/tokenizer/canonical.h"
#include "core/tokenizer/classify.h"

namespace hdTrans::tokenizer {

namespace {

// Holds the place of a span in the scanned characters; the look-arounds read it as a
// control character.
constexpr char32_t kSpanMark = 0x01;

bool IsDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool IsAlpha(char32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool IsWideDigit(char32_t c) { return c >= U'０' && c <= U'９'; }
bool IsWideAlpha(char32_t c) { return (c >= U'Ａ' && c <= U'Ｚ') || (c >= U'ａ' && c <= U'ｚ'); }
// An ASCII or full-width digit or letter, as the scanner folds them to ASCII.
bool IsDigitRune(char32_t c) { return IsDigit(c) || IsWideDigit(c); }
bool IsAlphaRune(char32_t c) { return IsAlpha(c) || IsWideAlpha(c); }
bool IsHalfWidth(char32_t c) { return c >= 0xFF61 && c <= 0xFF9F; }

// A non-ASCII punctuation, symbol or space character.
bool IsWideSymbol(char32_t c) {
    return c >= 0x80 && (text::IsPunct(c) || text::IsSymbol(c) || text::IsSpace(c));
}

// White space outside ASCII other than U+3000; the scanner reads it as a space.
bool IsUnicodeSpace(char32_t c) { return c >= 0x80 && c != U'　' && text::IsSpace(c); }

// A full-width digit or letter folded to ASCII.
char32_t Fold(char32_t c) { return c >= 0xFF01 && c <= 0xFF5E ? c - 0xFEE0 : c; }

bool OneOf(char32_t c, std::u32string_view set) { return c != 0 && set.find(c) != std::u32string_view::npos; }

// Scanning rewrites some characters in place (control characters to ' ', ， to 、,
// ― － to ー), and later look-behinds see the rewritten characters.
class Scanner {
public:
    Scanner(Tokens& out, std::span<const Piece> pieces, bool dashes)
        : out_(out), s_(out.chars), pieces_(pieces), dashes_(dashes) {}

    void Run() {
        const size_t n = s_.size();
        bufStart_ = out_.text.size();
        while (p_ < n) {
            if (s_[p_] == '<' && (IsAlpha(At(p_ + 1)) || At(p_ + 1) == '!' || At(p_ + 1) == '/')) inTag_ = true;
            if (HasPrefix(p_, U"&#CS")) inCS_ = true;
            if (span_ < out_.spans.size() && out_.spans[span_].position == p_) {
                Span(pieces_[out_.spans[span_++].piece]);
            } else if (s_[p_] < 0x80) {
                Ascii();
            } else {
                Wide();
            }
        }
        if (BufSize() > 0) Emit(cur_);
        const uint32_t end = out_.offsets[n];
        Token t;
        t.type = kTypeEnd;
        t.raw = {static_cast<uint32_t>(out_.text.size()), 0};
        t.sourceBegin = t.sourceEnd = end;
        out_.tokens.push_back(t);
    }

private:
    char32_t At(size_t i) const { return i < s_.size() ? s_[i] : 0; }
    char32_t Before(size_t i) const { return i > 0 ? s_[i - 1] : 0; }

    bool HasPrefix(size_t i, std::u32string_view prefix) const {
        return s_.size() - i >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s_.begin() + i);
    }

    // Whether, after skipping spaces (ASCII, full-width, TAB, VT) and "&#CS" markers
    // from i, the next character is Japanese text. The end of text counts as no.
    bool IsSkippableTranslateRun(size_t i) const {
        while (i < s_.size()) {
            const char32_t c = s_[i];
            if (Class(c) != 0) return true;
            if (c == ' ' || c == '\t' || c == '\v' || c == U'　') {
                i++;
            } else if (HasPrefix(i, U"&#CS")) {
                i += 4;
            } else {
                return false;
            }
        }
        return false;
    }

    size_t BufSize() const { return out_.text.size() - bufStart_; }

    void Emit(int type) {
        Token t;
        t.type = type;
        t.raw = {static_cast<uint32_t>(bufStart_), static_cast<uint32_t>(BufSize())};
        if (BufSize() > 0) {
            t.sourceBegin = srcBegin_;
            t.sourceEnd = srcEnd_;
        } else {
            t.sourceBegin = t.sourceEnd = out_.offsets[p_];
        }
        out_.tokens.push_back(t);
        bufStart_ = out_.text.size();
    }

    void Start(int type) {
        cur_ = type;
        out_.text.resize(bufStart_);
    }

    void Add(char32_t c) {
        if (BufSize() == 0) srcBegin_ = out_.offsets[p_];
        srcEnd_ = out_.offsets[p_ + 1];
        text::AppendRune(out_.text, c);
    }

    // Emits the pending token and the token of the span. The span's type stays current,
    // so an ASCII space after a span is never dropped as a space between Japanese text.
    void Span(const Piece& piece) {
        if (BufSize() > 0) Emit(cur_);
        cur_ = piece.span == kSpanWord ? kTypeProtected : kTypeMarkup;
        out_.text.append(piece.text);
        srcBegin_ = out_.offsets[p_];
        srcEnd_ = out_.offsets[p_ + 1];
        Emit(cur_);
        out_.tokens.back().span = piece.span;
        prev_ = 0;
        p_++;
    }

    int AsciiType() {
        const char32_t c = s_[p_];
        if (c == '\n' || c == '\r') return kTypeNewline;
        if (inTag_ || inCS_) return kTypeMarkup;
        if (IsDigit(c)) return kTypeNumber;
        if (IsAlpha(c)) return kTypeAlpha;
        if (c == '(' || c == ')' || c == '\'') return kTypeJapanese;
        if (c == '.' || c == ',' || c == '?' || c == '!') return kTypeAsciiPunct;
        if (c == '"' || c == ']' || c == '}' || c == '>') return kTypeAsciiClose;
        if (c == '[' || c == '{' || c == '<') return kTypeAsciiOpen;
        if (c == ' ' || c == '\t' || c == '\v') return kTypeAsciiSpace;
        if (c >= 0x20) return kTypeAsciiSymbol;
        s_[p_] = ' ';
        return kTypeAsciiSpace;
    }

    void Ascii() {
        const int type = AsciiType();
        const char32_t c = s_[p_];
        if (type == cur_) {
            Add(c);
        } else if ((cur_ == kTypeNumber && (type == kTypeAlpha || type == kTypeAsciiSymbol)) ||
                   (cur_ == kTypeAlpha && (type == kTypeNumber || type == kTypeAsciiSymbol)) ||
                   (cur_ == kTypeAsciiSymbol && type == kTypeAlpha)) {
            cur_ = cur_ == kTypeNumber ? type : kTypeAlpha;
            Add(c);
        } else if (cur_ == kTypeAsciiSymbol && type == kTypeNumber) {
            Add(c);
        } else if (cur_ == kTypeJapanese && type == kTypeAsciiSpace && IsSkippableTranslateRun(p_ + 1)) {
            // A space between Japanese text is dropped.
        } else {
            AsciiBoundary(type);
        }
        if (inTag_ && c == '>') {
            inTag_ = false;
            Emit(cur_);
        }
        if (inCS_ && c == 'S') {
            inCS_ = false;
            Emit(cur_);
        }
        prev_ = 0;
        p_++;
    }

    // An ASCII character that does not extend the current token.
    void AsciiBoundary(int type) {
        const char32_t c = s_[p_];
        if (c == ',') {
            // A comma between digits is a thousands separator and stays in the run,
            // even when a letter or symbol precedes the digits (HP4,867, 4,867/4,867).
            if (IsDigitRune(Before(p_)) && IsDigitRune(At(p_ + 1))) {
                Add(c);
                return;
            }
            if (cur_ != kTypeJapanese) {
                if (BufSize() > 0) Emit(cur_);
            }
            cur_ = kTypeJapanese;
            Add(U'、');
        } else if (c == '-' && Class(Before(p_)) != 0) {
            // After Japanese text '-' becomes ー; after hiragana and not before kana or
            // kanji it is a sentence end of its own.
            if (prev_ == kClassHiragana && ClassFlags(At(p_ + 1), 7) == 0) {
                Emit(cur_);
                cur_ = kTypeSentenceEnd;
            } else {
                cur_ = kTypeJapanese;
            }
            Add(U'ー');
        } else if (c == '.' && IsDigitRune(At(p_ + 1)) &&
                   (IsDigitRune(Before(p_)) || IsAlphaRune(Before(p_)))) {
            // A decimal point or version dot stays in the run (1.5, 4.5/4.5, Lv.2).
            if (cur_ == kTypeNumber) cur_ = kTypeAlpha;
            Add(c);
        } else if (c == '.' && cur_ == kTypeAlpha && IsAlpha(At(p_ + 1))) {
            Add(c);
        } else {
            if (BufSize() > 0) Emit(cur_);
            Start(type);
            Add(c);
        }
    }

    // Whether the ー at i is in a run of two or more that stands for a dash: after other
    // than kana and before a kanji or a kana that is not small (その時ーー扉, but
    // 兄上ーーっ).
    bool DashRun(size_t i) const {
        size_t from = i, to = i;
        while (Before(from) == U'ー') from--;
        while (At(to) == U'ー') to++;
        const char32_t next = At(to);
        return to - from >= 2 && ClassFlags(Before(from), 6) == 0 && ClassFlags(next, 7) != 0 &&
               !OneOf(next, U"ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮ");
    }

    // The type of the non-ASCII character at p; halfWidth reports a half-width
    // katakana or punctuation character. May rewrite the character.
    int WideType(bool& halfWidth) {
        halfWidth = false;
        const size_t n = s_.size();
        const char32_t c = s_[p_];
        if (dashes_ && (c == U'・' || c == U'。') && (Before(p_) == c || At(p_ + 1) == c)) return kTypeSymbol;
        if (dashes_ && c == U'ー' && DashRun(p_)) return kTypeSymbol;
        if (c == U'・') {
            if (p_ + 1 == n || IsWideSymbol(s_[p_ + 1]) || Before(p_) == U'・') return kTypeSentenceEnd;
            return kTypeJapanese;
        }
        if (prev_ == kClassHiragana && (c == U'～' || c == U'ー') && ClassFlags(At(p_ + 1), 7) == 0) {
            return kTypeSentenceEnd;
        }
        if (Class(c) != 0) return kTypeJapanese;
        // With dashes, a 」 that closes the line's utterance (before the end, 「『, a
        // full-width space or a line break) is a token of its own, so the predicate before
        // it ends a sentence (「黙れ」 is the imperative); after a te-form it stays in the
        // text (「疲れてしまって」).
        if (dashes_ && c == U'」' && p_ > 0 && s_[p_ - 1] != U'て' && s_[p_ - 1] != U'で' &&
            (p_ + 1 == n || OneOf(s_[p_ + 1], U"「『　\n"))) {
            return kTypeBracket;
        }
        switch (c) {
            case U'，':
                s_[p_] = U'、';
                return kTypeJapanese;
            case U'、': case U'（': case U'）': case U'「': case U'」': case U'『': case U'』':
            case U'＝': case U'“': case U'”': case U'～': case U'○': case U'＊':
                return kTypeJapanese;
            case U'。': case U'．': case U'？': case U'！':
                return kTypeSentenceEnd;
            case U'＜': case U'＞': case U'‘': case U'’': case U'〔': case U'〕': case U'［': case U'］':
            case U'｛': case U'｝': case U'〈': case U'〉': case U'《': case U'》': case U'【': case U'】':
                return kTypeBracket;
            case U'―': case U'－': {
                const char32_t next = At(p_ + 1);
                const bool keepDash = dashes_ && ((c == U'－' && Class(next) == 0) || OneOf(next, U"―－"));
                if (Class(Before(p_)) != 0 && !keepDash) {
                    s_[p_] = U'ー';
                    return kTypeJapanese;
                }
                return kTypeSymbol;
            }
            case U'　':
                return kTypeWideSpace;
            default:
                break;
        }
        if (c >= U'０' && c <= U'９') return kTypeNumber;
        if ((c >= U'Ａ' && c <= U'Ｚ') || (c >= U'ａ' && c <= U'ｚ')) return kTypeAlpha;
        if ((c >= U'｡' && c <= U'･') || c == U'ﾞ' || c == U'ﾟ') {
            halfWidth = true;
            return kTypeSymbol;
        }
        if (IsHalfWidth(c)) {
            halfWidth = true;
            return kTypeJapanese;
        }
        return kTypeSymbol;
    }

    void Wide() {
        // A full-width comma or period inside a number is a thousands separator or a
        // decimal point and folds to ASCII in the run (４，８６７, Ｌｖ．２).
        const char32_t wide = s_[p_];
        if (wide == U'，' && IsDigitRune(Before(p_)) && IsDigitRune(At(p_ + 1))) {
            Add(U',');
            prev_ = 0;
            p_++;
            return;
        }
        if (wide == U'．' && IsDigitRune(At(p_ + 1)) && (IsDigitRune(Before(p_)) || IsAlphaRune(Before(p_)))) {
            if (cur_ == kTypeNumber) cur_ = kTypeAlpha;
            Add(U'.');
            prev_ = 0;
            p_++;
            return;
        }
        bool halfWidth = false;
        const int type = WideType(halfWidth);
        const char32_t c = s_[p_];
        const bool letterOrDigit = type == kTypeNumber || type == kTypeAlpha;
        if (type == cur_) {
            if (letterOrDigit) {
                Add(Fold(c));
            } else if (halfWidth) {
                Add(WidenHalfWidth(c));
            } else {
                // 。 starts a new token, and so does ！ or ？ after a lengthening ～ (行くよ～！！),
                // which then reads like a sentence end anywhere else.
                const bool afterLength = (c == U'！' || c == U'？') && p_ > 0 && s_[p_ - 1] == U'～';
                if ((c == U'。' || afterLength) && BufSize() > 0) Emit(cur_);
                Add(c);
            }
        } else if ((cur_ == kTypeNumber && type == kTypeAlpha) || (cur_ == kTypeAlpha && type == kTypeNumber)) {
            cur_ = kTypeAlpha;
            Add(Fold(c));
        } else if ((cur_ == kTypeNumber || cur_ == kTypeAlpha) && type == kTypeJapanese) {
            // Copied as it is, even a half-width character.
            cur_ = kTypeJapanese;
            Add(c);
        } else if (cur_ == kTypeJapanese && type == kTypeNumber) {
            Add(Fold(c));
        } else if (cur_ == kTypeJapanese && type == kTypeWideSpace && IsSkippableTranslateRun(p_ + 1)) {
            // A full-width space between Japanese text is dropped.
        } else {
            if (BufSize() > 0) Emit(cur_);
            Start(type);
            if (letterOrDigit) {
                Add(Fold(c));
            } else if (halfWidth && c == U'｣') {
                Start(kTypeJapanese);
                Add(U'」');
            } else if (halfWidth && c == U'､') {
                Start(kTypeJapanese);
                Add(U'、');
            } else if (halfWidth) {
                Add(WidenHalfWidth(c));
            } else {
                Add(c);
            }
        }
        prev_ = Class(c);
        p_++;
    }

    Tokens& out_;
    std::vector<char32_t>& s_;
    std::span<const Piece> pieces_;
    bool dashes_;
    size_t p_ = 0;
    size_t span_ = 0;      // next entry of out_.spans
    size_t bufStart_ = 0;  // the text of the token being built is out_.text from here
    uint32_t srcBegin_ = 0, srcEnd_ = 0;
    int cur_ = -1;         // its type, -1 before the first token
    bool inTag_ = false;   // after '<' + letter, '!' or '/', until '>'
    bool inCS_ = false;    // after "&#CS" starts, until its 'S'
    int prev_ = 0;         // Class of the previous character (0 after ASCII)
};

void Reset(Tokens& out) {
    out.tokens.clear();
    out.markers.clear();
    out.text.clear();
    out.chars.clear();
    out.offsets.clear();
    out.spans.clear();
}

// Appends the characters of a text piece up to a NUL; false after a NUL.
bool AppendChars(Tokens& out, std::string_view s, uint32_t& offset) {
    const size_t nul = s.find('\0');
    const std::string_view text = s.substr(0, nul);
    for (size_t i = 0; i < text.size();) {
        const text::Rune r = text::DecodeRune(text.substr(i));
        out.chars.push_back(r.cp);
        out.offsets.push_back(offset + static_cast<uint32_t>(i));
        i += r.size;
    }
    offset += static_cast<uint32_t>(text.size());
    return nul == std::string_view::npos;
}

void Scan(Tokens& out, std::span<const Piece> pieces, bool dashes, uint32_t offset) {
    out.offsets.push_back(offset);
    if (out.chars.empty()) {
        out.offsets.clear();
        return;
    }
    for (char32_t& c : out.chars) {
        if (IsUnicodeSpace(c)) c = ' ';
    }
    while (!out.chars.empty() && out.chars.back() == ' ') {
        out.chars.pop_back();
        out.offsets.pop_back();
    }
    Scanner(out, pieces, dashes).Run();
    Prepare(out);
}

// The per-token stage's view of the tokens: the current text of a token is its merged
// view and its current type TypeAfter.
class Preparer {
public:
    explicit Preparer(Tokens& t) : t_(t), tokens_(t.tokens) {}

    void Run() {
        for (Token& tok : tokens_) tok.merged = tok.raw;
        for (size_t i = 0; i < tokens_.size(); i++) {
            MergeControlMarkers(i);
            Token& tok = tokens_[i];
            tok.canonical = tok.merged;
            if (CanonicalType(t_.TypeAfter(i)) && tok.merged.size > 0) {
                t_.buffer.clear();
                AppendCanonicalText(t_.buffer, t_.Text(tok.merged));
                tok.canonical = {static_cast<uint32_t>(t_.text.size()), static_cast<uint32_t>(t_.buffer.size())};
                t_.text.append(t_.buffer);
            }
        }
    }

private:
    int Type(size_t i) const { return t_.TypeAfter(i); }
    std::string_view Left(size_t i) const { return t_.Text(tokens_[i].merged); }

    // Folds control markers that follow the type-24 token i into it:
    //   - 24, "</a>" or "</A>" (not a span), 24: the second 24's text is appended to
    //     token i and both following tokens are emptied (marker kind 2);
    //   - otherwise each run of "&#CS" markers, spaces and other markup that ends in a
    //     24 token is merged by MergeControlMarkedTokenRun.
    void MergeControlMarkers(size_t i) {
        if (Type(i) != kTypeJapanese || i + 2 >= tokens_.size()) return;
        const std::string_view next = Left(i + 1);
        if (Type(i + 2) == kTypeJapanese && tokens_[i + 1].span == 0 && (next == "</a>" || next == "</A>")) {
            AddMarker(i, 2);
            Append(i, i + 2);
            Clear(i + 1, i);
            Clear(i + 2, i);
            return;
        }
        while (HasControlMarkerBeforePunctuation(i + 1)) MergeControlMarkedTokenRun(i + 1);
    }

    // Whether the tokens from index on are only markup (77), spaces (7, 26) or "&#CS"
    // up to a type-24 token, with at least one "&#CS" among them. A span ends the run.
    bool HasControlMarkerBeforePunctuation(size_t index) const {
        bool found = false;
        for (; index < tokens_.size(); index++) {
            const int type = Type(index);
            if (tokens_[index].span != 0) return false;
            if (type == kTypeJapanese) return found;
            if (Left(index) == "&#CS") {
                found = true;
            } else if (type != kTypeMarkup && type != kTypeWideSpace && type != kTypeAsciiSpace) {
                return false;
            }
        }
        return false;
    }

    // Empties the "&#CS" and space tokens from index on, adding a marker at the current
    // length of token index-1 for each "&#CS", and moves the text of the first type-24
    // token into token index-1. Other markup is left as it is. Stops at any other type.
    void MergeControlMarkedTokenRun(size_t index) {
        const size_t prev = index - 1;
        for (; index < tokens_.size(); index++) {
            const int type = Type(index);
            if (type == kTypeJapanese) {
                Append(prev, index);
                Clear(index, prev);
                return;
            }
            if (Left(index) == "&#CS") {
                AddMarker(prev, 1);
                Clear(index, prev);
            } else if (type == kTypeMarkup) {
            } else if (type == kTypeWideSpace || type == kTypeAsciiSpace) {
                Clear(index, prev);
            } else {
                return;
            }
        }
    }

    void AddMarker(size_t i, int kind) {
        t_.markers.push_back({static_cast<int>(i), static_cast<int>(tokens_[i].merged.size), kind});
    }

    // Appends the current text of token from to token to; the text of to moves to the
    // end of the storage unless it is there already.
    void Append(size_t to, size_t from) {
        std::string& s = t_.text;
        TextRef& dst = tokens_[to].merged;
        const TextRef src = tokens_[from].merged;
        if (dst.offset + dst.size != s.size()) {
            const size_t at = s.size();
            s.resize(at + dst.size);
            std::memcpy(s.data() + at, s.data() + dst.offset, dst.size);
            dst.offset = static_cast<uint32_t>(at);
        }
        const size_t at = s.size();
        s.resize(at + src.size);
        std::memcpy(s.data() + at, s.data() + src.offset, src.size);
        dst.size += src.size;
    }

    void Clear(size_t i, size_t by) {
        if (tokens_[i].clearedBy < 0) tokens_[i].clearedBy = static_cast<int>(by);
        tokens_[i].merged = {};
    }

    Tokens& t_;
    std::vector<Token>& tokens_;
};

}  // namespace

bool CanonicalType(int type) {
    switch (type) {
        case 3: case 6: case 9: case 17: case 18: case 20: case 22: case 23: case 26: case 78:
            return true;
        default:
            return false;
    }
}


void TokenizeSpans(std::span<const Piece> pieces, bool dashes, Tokens& out) {
    Reset(out);
    uint32_t offset = 0;
    for (size_t k = 0; k < pieces.size(); k++) {
        const Piece& p = pieces[k];
        if (p.span != 0) {
            out.spans.push_back({static_cast<uint32_t>(out.chars.size()), static_cast<uint32_t>(k)});
            out.chars.push_back(kSpanMark);
            out.offsets.push_back(offset);
            offset += static_cast<uint32_t>(p.text.size());
            continue;
        }
        if (!AppendChars(out, p.text, offset)) break;
    }
    Scan(out, pieces, dashes, offset);
}

void Tokenize(std::string_view input, Tokens& out) {
    Reset(out);
    uint32_t offset = 0;
    AppendChars(out, input, offset);
    Scan(out, {}, false, offset);
}

void Prepare(Tokens& tokens) {
    tokens.markers.clear();
    for (Token& t : tokens.tokens) t.clearedBy = -1;
    Preparer(tokens).Run();
}

void AppendCanonicalText(std::string& out, std::string_view s) {
    const size_t start = out.size();
    const text::Rune first = text::DecodeRune(s);
    if (const auto form = FirstForm(first.cp)) {
        out.append(*form);
    } else {
        for (size_t i = 0; i < s.size();) {
            const text::Rune r = text::DecodeRune(s.substr(i));
            AppendCanonical(out, r.cp);
            i += r.size;
        }
    }
    while (out.size() > start && out.back() == ' ') out.pop_back();
}

}  // namespace hdTrans::tokenizer
