#include "core/word/util.h"

#include "core/tokenizer/classify.h"

namespace hdTrans::word {

bool RuneIn(char32_t r, std::string_view set) {
    while (!set.empty()) {
        const text::Rune c = text::DecodeRune(set);
        if (c.cp == r) return true;
        set.remove_prefix(c.size);
    }
    return false;
}

bool AnyRuneIn(std::string_view s, std::string_view set) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (RuneIn(r.cp, set)) return true;
        s.remove_prefix(r.size);
    }
    return false;
}

int QuoteDepthAfter(std::string_view s, int depth) {
    for (std::size_t at = 0; at < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(at));
        if (r.size == 0) break;
        if (r.cp == U'「' || r.cp == U'『') depth++;
        if ((r.cp == U'」' || r.cp == U'』') && depth > 0) depth--;
        at += r.size;
    }
    return depth;
}

std::string_view TrimRunes(std::string_view s, std::string_view cutset) {
    while (!s.empty() && RuneIn(text::DecodeRune(s).cp, cutset)) s.remove_prefix(text::DecodeRune(s).size);
    while (!s.empty() && RuneIn(DecodeLastRune(s).cp, cutset)) s.remove_suffix(DecodeLastRune(s).size);
    return s;
}

std::string_view TrimSpace(std::string_view s) {
    auto space = [](char32_t r) {
        return r == ' ' || (r >= '\t' && r <= '\r') || r == 0x85 || r == 0xA0 || r == 0x1680 ||
               (r >= 0x2000 && r <= 0x200A) || r == 0x2028 || r == 0x2029 || r == 0x202F || r == 0x205F || r == 0x3000;
    };
    while (!s.empty() && space(text::DecodeRune(s).cp)) s.remove_prefix(text::DecodeRune(s).size);
    while (!s.empty() && space(DecodeLastRune(s).cp)) s.remove_suffix(DecodeLastRune(s).size);
    return s;
}

int Width(char32_t r) { return r < 0x80 || (r >= 0xFF61 && r <= 0xFF9F) ? 1 : 2; }

int WidthOf(std::string_view s) {
    int n = 0;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        n += Width(r.cp);
        s.remove_prefix(r.size);
    }
    return n;
}

UnitPos UnitOffset(std::string_view s, int n) {
    int w = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        if (w >= n) return {i, w == n};
        const text::Rune r = text::DecodeRune(s.substr(i));
        w += Width(r.cp);
        i += r.size;
    }
    return {s.size(), w <= n};
}

std::string Window::Str() const {
    std::string s(text);
    if (broken) s += '\xff';
    return s;
}

Window Cut(std::string_view s, int first, int count) {
    if (count <= 0) return {};
    if (first < 0) first = 0;
    const UnitPos i = UnitOffset(s, first);
    const UnitPos j = UnitOffset(s, first + count);
    return {s.substr(i.offset, j.offset - i.offset), !i.boundary || !j.boundary};
}

Window Left(std::string_view s, int n) { return Cut(s, 0, n); }

Window Right(std::string_view s, int n) {
    const int total = WidthOf(s);
    if (n >= total) return {s, false};
    return Cut(s, total - n, n);
}

std::string_view FirstChar(std::string_view s) { return s.substr(0, text::DecodeRune(s).size); }

std::string_view LastChar(std::string_view s) { return s.substr(s.size() - DecodeLastRune(s).size); }

text::Rune DecodeLastRune(std::string_view s) {
    if (s.empty()) return {text::kReplacement, 0};
    const std::size_t end = s.size();
    const auto last = static_cast<unsigned char>(s[end - 1]);
    if (last < 0x80) return {last, 1};
    const long lim = end >= 4 ? static_cast<long>(end - 4) : 0;
    long start = static_cast<long>(end) - 2;
    for (; start >= lim; start--) {
        if ((static_cast<unsigned char>(s[static_cast<std::size_t>(start)]) & 0xC0) != 0x80) break;
    }
    if (start < 0) start = 0;
    const text::Rune r = text::DecodeRune(s.substr(static_cast<std::size_t>(start)));
    if (static_cast<std::size_t>(start) + r.size != end) return {text::kReplacement, 1};
    return r;
}

int Classify(std::string_view text) {
    if (text.empty()) return 0;
    return tokenizer::Class(text::DecodeRune(text).cp);
}

int Classify(const Window& w) {
    if (w.text.empty()) return w.broken ? tokenizer::Class(text::kReplacement) : 0;
    return Classify(w.text);
}

bool HasPrefixFold(std::string_view s, std::string_view prefix) {
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : c; };
    for (std::size_t i = 0; i < prefix.size(); i++) {
        if (lower(At(s, static_cast<long>(i))) != lower(prefix[i])) return false;
    }
    return true;
}

int FindHonorificPrefix(std::string_view c) {
    if (c == "お") return 0;
    if (c == "ご") return 1;
    if (c == "御") return 2;
    return -1;
}

int NameClassOf(std::string_view c) {
    if (c.empty()) return kClassOther;
    const char32_t r = text::DecodeRune(c).cp;
    if (r >= U'ぁ' && r <= U'ん') return kClassHiragana;
    if ((r >= U'ァ' && r <= U'ヶ') || r == U'ー' || r == U'ヽ' || r == U'ヾ') return kClassKatakana;
    if ((tokenizer::ClassFlags(r, 1) == 24 && r != U'〇') || r == U'々' || r == U'〆') return kClassKanji;
    return kClassOther;
}

}  // namespace hdTrans::word
