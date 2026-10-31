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
    Scanner(Tokens& out, std::span<const Piece> pieces, bool dashes, const SpaceJoins* joins)
        : out_(out), s_(out.chars), pieces_(pieces), dashes_(dashes), joins_(joins) {}

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

    // Whether the space or ideographic space at i between two kanji parts two words (SpaceJoins).
    bool KeptSpace(size_t i) const {
        const auto blank = [](char32_t c) { return c != '\n' && c != '\r' && text::IsSpace(c); };
        if (joins_ == nullptr || !blank(s_[i]) || !text::IsKanji(Before(i)) || !text::IsKanji(At(i + 1))) return false;
        // A name of one kanji and one or two more is often written apart (林 涛, 林 香生): two such
        // pieces alone between other text stay one name.
        size_t start = i, end = i + 1;
        while (start > 0 && Class(s_[start - 1]) != 0) start--;
        while (end < s_.size() && Class(s_[end]) != 0) end++;
        const bool alone = (start == 0 || !blank(s_[start - 1])) && (end == s_.size() || !blank(s_[end]));
        if (alone && i - start == 1 && end - i - 1 <= 2 && text::IsKanji(At(end - 1))) return false;
        // Kanji written on to katakana before the space (ジモ得 週まとめ割) end a name or a title,
        // whose reading the space does not part.
        size_t kanji = i;
        while (kanji > start && text::IsKanji(s_[kanji - 1])) kanji--;
        if (kanji > start && Class(s_[kanji - 1]) == kClassKatakana) return false;
        return !Joins(i, SpaceWord::Any);
    }

    // SpaceJoins on the Japanese text before the space at i and after it. The text on each side
    // reaches over single spaces, so that a word of text spaced out character by character is seen
    // whole (通 知 表), and further for the names and titles (錆びついたマシンガンで 今を撃ち抜こう);
    // for a phrase the text before starts after the last space.
    bool Joins(size_t i, SpaceWord word) const {
        const auto blank = [](char32_t c) { return c != '\n' && c != '\r' && text::IsSpace(c); };
        const size_t reach = word == SpaceWord::Any ? 10 : 24;
        size_t from = i;
        while (from > 0 && i - from < reach &&
               (Class(s_[from - 1]) != 0 ||
                (word != SpaceWord::Phrase && word != SpaceWord::Filler && blank(s_[from - 1]) && from > 1 &&
                 Class(s_[from - 2]) != 0))) {
            from--;
        }
        size_t to = i + 1;
        while (to < s_.size() && to < i + 1 + reach &&
               (Class(s_[to]) != 0 || (blank(s_[to]) && to + 1 < s_.size() && Class(s_[to + 1]) != 0))) {
            to++;
        }
        std::string before, after;
        for (size_t k = from; k < i; k++) {
            if (!blank(s_[k])) text::AppendRune(before, s_[k]);
        }
        for (size_t k = i + 1; k < to; k++) {
            if (!blank(s_[k])) text::AppendRune(after, s_[k]);
        }
        return (*joins_)(before, after, word);
    }

    // SpaceWord::Spelled (or word) on the text from the piece at from to the space at i and the text
    // after it, which reaches over single spaces.
    bool Spelled(size_t from, size_t i, SpaceWord word = SpaceWord::Spelled) const {
        const auto blank = [](char32_t c) { return c != '\n' && c != '\r' && text::IsSpace(c); };
        size_t to = i + 1;
        while (to < s_.size() && to < i + 25 &&
               (Class(s_[to]) != 0 || (blank(s_[to]) && to + 1 < s_.size() && Class(s_[to + 1]) != 0))) {
            to++;
        }
        std::string before, after;
        for (size_t k = from; k < i; k++) {
            if (!blank(s_[k])) text::AppendRune(before, s_[k]);
        }
        for (size_t k = i + 1; k < to; k++) {
            if (!blank(s_[k])) text::AppendRune(after, s_[k]);
        }
        return (*joins_)(before, after, word);
    }

    // Whether the space at i, left out of the Japanese token being built, parts two words
    // (TokenizeSpans): a single space between two Japanese characters, unless the pieces on its
    // sides are spaced out character by character or a name.
    bool Parts(size_t i, bool& copy) const {
        copy = false;
        const auto blank = [](char32_t c) { return c != '\n' && c != '\r' && text::IsSpace(c); };
        if (joins_ == nullptr || BufSize() == 0 || Class(Before(i)) == 0 || Class(At(i + 1)) == 0) return false;
        size_t start = i, end = i + 1;
        while (start > 0 && Class(s_[start - 1]) != 0) start--;
        while (end < s_.size() && Class(s_[end]) != 0) end++;
        const bool kanji = text::IsKanji(Before(i)) && text::IsKanji(At(i + 1));
        if (kanji) {
            // The two exceptions of KeptSpace.
            const bool alone = (start == 0 || !blank(s_[start - 1])) && (end == s_.size() || !blank(s_[end]));
            if (alone && i - start == 1 && end - i - 1 <= 2 && text::IsKanji(At(end - 1))) return false;
            size_t k = i;
            while (k > start && text::IsKanji(s_[k - 1])) k--;
            if (k > start && Class(s_[k - 1]) == kClassKatakana) return false;
        }
        // Pieces of one character in a row up to the space and on after it: a letter of the
        // spelling is not a filler even when it has that reading on its own. Where the run ends in a
        // piece of more than one character, between kanji joins finds a word in the kanji pieces
        // across the space as SpaceWord::Spelled (通 知 表が, こ の 展 開は; not 私 先 行く).
        size_t run = 0, kanjiStart = i;
        bool kanjiRun = true;
        for (size_t from = start, to = i; to - from == 1;) {
            run++;
            kanjiRun = kanjiRun && text::IsKanji(s_[from]);
            if (kanjiRun) kanjiStart = from;
            if (from < 2 || !blank(s_[from - 1]) || Class(s_[from - 2]) == 0) break;
            to = from - 1;
            for (from = to; from > 0 && Class(s_[from - 1]) != 0;) from--;
        }
        bool wordAfter = end - i - 1 > 1;  // the run ends in a piece of more characters
        for (size_t from = i + 1, to = end; run > 0 && to - from == 1;) {
            run++;
            if (to + 1 >= s_.size() || !blank(s_[to]) || Class(s_[to + 1]) == 0) break;
            from = to + 1;
            for (to = from; to < s_.size() && Class(s_[to]) != 0;) to++;
            wordAfter = to - from > 1;
        }
        if (run >= (kanji ? 2u : 3u) && (!kanji || !wordAfter || Spelled(kanjiStart, i))) return false;
        // A kanji standing alone before the space, not after another space, that starts a kanji noun
        // of the dictionary ending in the piece after it (紅 白って 홍백) spells that noun out
        // (SpaceWord::Compound).
        if (kanji && run == 1 && i - start == 1 && wordAfter && (start == 0 || !blank(s_[start - 1])) &&
            Spelled(start, i, SpaceWord::Compound)) {
            return false;
        }
        if (Joins(i, SpaceWord::Filler)) return true;
        // A piece that starts with a small kana or a long mark goes on with the word before it
        // (消す っていう).
        if (OneOf(At(i + 1), U"ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮーヽヾゝゞ")) return false;
        // A piece said again after the space is a copy, which the word stage reads on its own
        // (やめて やめて); the space between copies of two or more characters stays a boundary
        // (CopySpace). Copies of one kana are sounds, which the sound stage writes.
        if (i - start == end - i - 1 && std::equal(s_.begin() + start, s_.begin() + i, s_.begin() + i + 1)) {
            copy = i - start >= 2;
            return false;
        }
        // The last piece before a mark or the end may end the one before (そうだ ね……).
        if (end == s_.size() || !blank(s_[end])) {
            std::string before, after;
            for (size_t k = start; k < i; k++) text::AppendRune(before, s_[k]);
            for (size_t k = i + 1; k < end; k++) text::AppendRune(after, s_[k]);
            if ((*joins_)(before, after, SpaceWord::Ending)) return false;
        }
        return !Joins(i, SpaceWord::Binding) && !Joins(i, SpaceWord::Phrase);
    }

    // Leaves out the space at p_ between Japanese text: it parts two words (Parts), stands between
    // copies (CopySpace) or is dropped.
    void DropSpace() {
        bool copy = false;
        if (Parts(p_, copy)) {
            PartAtSpace();
        } else if (copy) {
            size_t start = p_;
            while (start > 0 && Class(s_[start - 1]) != 0) start--;
            std::string piece;
            for (size_t k = start; k < p_; k++) text::AppendRune(piece, s_[k]);
            out_.copySpaces.push_back({static_cast<int>(out_.tokens.size()), static_cast<uint32_t>(BufSize()),
                                       static_cast<uint32_t>(piece.size())});
        }
    }

    // Ends the Japanese token being built at a space that parts two words; the next one goes on
    // after it.
    void PartAtSpace() {
        Emit(cur_);
        afterSpace_ = true;
    }

    // A Latin run after kana joins the Japanese token only when a dictionary word spans the
    // whole folded run (うp, おk). A following digit makes it an identifier instead.
    bool KeptLetter(size_t i) const {
        if (joins_ == nullptr || (prev_ != kClassHiragana && prev_ != kClassKatakana)) return false;
        constexpr size_t kBefore = 10, kAfter = 10;
        size_t from = i;
        while (from > 0 && i - from < kBefore && Class(s_[from - 1]) != 0) from--;
        std::string before, after;
        for (size_t k = from; k < i; k++) text::AppendRune(before, s_[k]);
        size_t to = i;
        while (to < s_.size() && IsAlphaRune(s_[to])) text::AppendRune(after, Fold(s_[to++]));
        if (IsDigitRune(At(to))) return false;
        for (const size_t end = to + kAfter; to < s_.size() && to < end && Class(s_[to]) != 0; to++) {
            text::AppendRune(after, s_[to]);
        }
        return (*joins_)(before, after, SpaceWord::Any);
    }

    size_t BufSize() const { return out_.text.size() - bufStart_; }

    void Emit(int type) {
        Token t;
        t.type = type;
        t.afterSpace = afterSpace_ && type == kTypeJapanese && !out_.tokens.empty() &&
                       out_.tokens.back().type == kTypeJapanese && BufSize() > 0;
        if (BufSize() > 0) afterSpace_ = false;
        t.raw = {static_cast<uint32_t>(bufStart_), static_cast<uint32_t>(BufSize())};
        if (BufSize() > 0) {
            t.sourceBegin = srcBegin_;
            t.sourceEnd = srcEnd_;
        } else {
            t.sourceBegin = t.sourceEnd = out_.offsets[p_];
        }
        out_.tokens.push_back(t);
        bufStart_ = out_.text.size();
        maskAt_ = kNoMask;
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
        maskAt_ = kNoMask;
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
        } else if (cur_ == kTypeJapanese && type == kTypeAsciiSpace && IsSkippableTranslateRun(p_ + 1) && !KeptSpace(p_)) {
            // A space between Japanese text is dropped.
            DropSpace();
        } else if (cur_ == kTypeJapanese && type == kTypeAlpha && KeptLetter(p_)) {
            Add(c);
            p_++;  // prev_ stays the class of the kana, for the letters after it
            return;
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
    int WideType(bool& halfWidth, bool& mask) {
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
            case U'＝': case U'“': case U'”': case U'～':
                return kTypeJapanese;
            case U'○': case U'＊':
                // A mask character after a symbol goes on with the run of symbols (＠＊＋).
                mask = true;
                return cur_ == kTypeSymbol && BufSize() > 0 ? kTypeSymbol : kTypeJapanese;
            case U'。': case U'．': case U'？': case U'！':
                return kTypeSentenceEnd;
            case U'＜': case U'＞': case U'‘': case U'’': case U'〔': case U'〕': case U'［': case U'］':
            case U'｛': case U'｝': case U'〈': case U'〉': case U'《': case U'》': case U'【': case U'】':
                return kTypeBracket;
            case U'―': case U'－': case U'−': {
                const char32_t next = At(p_ + 1);
                const bool keepDash = dashes_ && ((c != U'―' && Class(next) == 0) || OneOf(next, U"―－−"));
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
        bool halfWidth = false, mask = false;
        const int type = WideType(halfWidth, mask);
        const char32_t c = s_[p_];
        // Mask characters that end Japanese text before a symbol start the run of symbols
        // (「＊＠＋ 「 ＊＠＋).
        if (type == kTypeSymbol && cur_ == kTypeJapanese && maskAt_ != kNoMask) StartRunAtMasks();
        const size_t addedAt = out_.text.size();
        const bool letterOrDigit = type == kTypeNumber || type == kTypeAlpha;
        if (type == cur_) {
            if (letterOrDigit) {
                Add(Fold(c));
            } else if (halfWidth) {
                Add(WidenHalfWidth(c));
            } else {
                // 。 starts a new token, and so does ！ or ？ after a lengthening ～ (行くよ～！！),
                // which then reads like a sentence end anywhere else. A sign with a form of its
                // own (FirstForm) after a number is a token of its own, the unit of the number,
                // not the start of a run of other symbols (３４％…… 34%……, but ３４％％％).
                const bool afterLength = (c == U'！' || c == U'？') && p_ > 0 && s_[p_ - 1] == U'～';
                const bool afterUnit = cur_ == kTypeSymbol && p_ > 1 && IsDigitRune(s_[p_ - 2]) &&
                                       FirstForm(s_[p_ - 1]) && c != s_[p_ - 1];
                if ((c == U'。' || afterLength || afterUnit) && BufSize() > 0) Emit(cur_);
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
        } else if (cur_ == kTypeJapanese && type == kTypeAlpha && KeptLetter(p_)) {
            Add(Fold(c));
            maskAt_ = kNoMask;
            p_++;  // prev_ stays the class of the kana, for the letters after it
            return;
        } else if (cur_ == kTypeJapanese && type == kTypeWideSpace && IsSkippableTranslateRun(p_ + 1) && !KeptSpace(p_)) {
            // A full-width space between Japanese text is dropped.
            DropSpace();
        } else if (cur_ == kTypeJapanese && type == kTypeWideSpace && KeptSpace(p_)) {
            // A kept space between two words (KeptSpace) is a space, not a break of the sentence.
            if (BufSize() > 0) Emit(cur_);
            Start(kTypeAsciiSpace);
            Add(U' ');
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
        if (!mask || cur_ != kTypeJapanese || out_.text.size() == addedAt) {
            maskAt_ = kNoMask;
        } else if (maskAt_ == kNoMask) {
            maskAt_ = addedAt - bufStart_;
            maskSource_ = out_.offsets[p_];
        }
        prev_ = Class(c);
        p_++;
    }

    // Moves the mask characters at the end of the Japanese token being built into a new
    // token of symbols.
    void StartRunAtMasks() {
        const std::string masks = out_.text.substr(bufStart_ + maskAt_);
        out_.text.resize(bufStart_ + maskAt_);
        if (BufSize() > 0) {
            srcEnd_ = maskSource_;
            Emit(cur_);
        }
        Start(kTypeSymbol);
        out_.text.append(masks);
        srcBegin_ = maskSource_;
    }

    Tokens& out_;
    std::vector<char32_t>& s_;
    std::span<const Piece> pieces_;
    bool dashes_;
    const SpaceJoins* joins_;
    size_t p_ = 0;
    size_t span_ = 0;      // next entry of out_.spans
    size_t bufStart_ = 0;  // the text of the token being built is out_.text from here
    uint32_t srcBegin_ = 0, srcEnd_ = 0;
    int cur_ = -1;         // its type, -1 before the first token
    bool inTag_ = false;   // after '<' + letter, '!' or '/', until '>'
    bool inCS_ = false;    // after "&#CS" starts, until its 'S'
    int prev_ = 0;         // Class of the previous character (0 after ASCII)
    bool afterSpace_ = false;  // the token being built starts after a space that parts two words
    // Where the mask characters (○ ＊) that end the Japanese token being built start in its
    // text, and in the input; kNoMask when it does not end in one.
    static constexpr size_t kNoMask = static_cast<size_t>(-1);
    size_t maskAt_ = kNoMask;
    uint32_t maskSource_ = 0;
};

void Reset(Tokens& out) {
    out.tokens.clear();
    out.markers.clear();
    out.copySpaces.clear();
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

void Scan(Tokens& out, std::span<const Piece> pieces, bool dashes, uint32_t offset, const SpaceJoins* joins) {
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
    Scanner(out, pieces, dashes, joins).Run();
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


void TokenizeSpans(std::span<const Piece> pieces, bool dashes, Tokens& out, const SpaceJoins* joins) {
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
    Scan(out, pieces, dashes, offset, joins);
}

void Tokenize(std::string_view input, Tokens& out) {
    Reset(out);
    uint32_t offset = 0;
    AppendChars(out, input, offset);
    Scan(out, {}, false, offset, nullptr);
}

void Prepare(Tokens& tokens) {
    tokens.markers.clear();
    for (Token& t : tokens.tokens) t.clearedBy = -1;
    Preparer(tokens).Run();
}

namespace {

// The ASCII marks of an exclamation or question mark, "" for other characters.
std::string_view MarkForm(char32_t c) {
    switch (c) {
        case U'！': case U'!': return "!";
        case U'？': case U'?': return "?";
        case U'‼': return "!!";
        case U'⁉': return "!?";
        case U'⁈': return "?!";
        default: return "";
    }
}

}  // namespace

void AppendCanonicalText(std::string& out, std::string_view s) {
    const size_t start = out.size();
    const text::Rune first = text::DecodeRune(s);
    const auto form = FirstForm(first.cp);
    if (form && (first.size == s.size() || !MarkForm(first.cp).empty())) {
        out.append(*form);
    } else {
        size_t i = 0;
        if (form) {
            out.append(s.substr(0, first.size));
            i = first.size;
        }
        for (; i < s.size();) {
            const text::Rune r = text::DecodeRune(s.substr(i));
            AppendCanonical(out, r.cp);
            i += r.size;
        }
    }
    while (out.size() > start && out.back() == ' ') out.pop_back();
}

std::string MarkRun(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        const std::string_view form = MarkForm(r.cp);
        if (form.empty()) break;
        out.append(form);
        i += r.size;
    }
    return out;
}

}  // namespace hdTrans::tokenizer
