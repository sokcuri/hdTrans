#include <string_view>

#include "core/dict/classes.h"
#include "core/text/utf8.h"
#include "core/userdict/compile.h"
#include "core/word/conjugation.h"

namespace hdTrans::userdict::internal {

namespace {

constexpr int kKindDirect = 14;  // pattern kind matched without an ending
constexpr int kKindIchidan = 7;
constexpr int kKindSu = 3;
constexpr int kKindSuru = 9;
constexpr int kKindGodanRu = 8;

// Width units: 1 for ASCII and half-width katakana, 2 for any other character.
int Width(char32_t r) { return r < 0x80 || (r >= 0xFF61 && r <= 0xFF9F) ? 1 : 2; }

int WidthOf(std::string_view s) {
    int n = 0;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        n += Width(r.cp);
        i += r.size;
    }
    return n;
}

// The byte offset of width unit n of s (s.size() past the end) and whether a
// character boundary lies there; inside a character, the offset after it.
std::size_t UnitOffset(std::string_view s, int n, bool& boundary) {
    int w = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (w >= n) {
            boundary = w == n;
            return i;
        }
        const text::Rune r = text::DecodeRune(s.substr(i));
        w += Width(r.cp);
        i += r.size;
    }
    boundary = w <= n;
    return s.size();
}

// The text of s from width unit first, count units long; a window whose ends fall
// inside a character ends in a byte that no text holds.
std::string Window(std::string_view s, int first, int count) {
    if (count <= 0) return {};
    first = first < 0 ? 0 : first;
    bool ok1 = false, ok2 = false;
    const std::size_t i = UnitOffset(s, first, ok1);
    const std::size_t j = UnitOffset(s, first + count, ok2);
    std::string out(s.substr(i, j - i));
    if (!ok1 || !ok2) out += '\xff';
    return out;
}

bool AsciiPunct(char c) {
    return c > ' ' && c < 0x7F && !(c >= '0' && c <= '9') && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

std::string_view Conj(int kind, int branch) { return word::Conjugation(kind, branch); }

}  // namespace

bool AnalyseVerbTail(std::string_view key, char kindByte, std::string_view tail, int& branchOut, std::size_t& length) {
    const auto starts = [](std::string_view s, std::string_view p) { return s.starts_with(p); };
    int patternKind = kindByte - '0';
    const int kind = patternKind;
    int d = -1;  // the branch found
    const std::string_view recordText = key;
    const auto done = [&] {
        branchOut = d;
        length = Conj(patternKind, d).size();
        return true;
    };
    if (kind == kKindDirect) return done();
    for (int branch = 0; branch < word::kConjugationBranches; branch++) {
        if (branch == 13) continue;
        const std::string_view entry = Conj(kind, branch);
        if ((kind != kKindIchidan && entry.empty()) || !starts(tail, entry)) continue;
        if (branch == 4 || branch == 11) {
            const std::size_t n = entry.size();
            const std::string_view after = tail.substr(n);
            if (starts(after, "，") || starts(after, "、")) continue;
            if (n < tail.size()) {
                const char next = tail[n];
                if ((branch == 4 && static_cast<unsigned char>(next) < 0x80 && AsciiPunct(next)) ||
                    (branch == 11 && next == ',')) {
                    continue;
                }
            } else if (n == tail.size() && branch == 4) {
                continue;
            }
        }
        d = branch;
        if (d == 4 && starts(tail, Conj(kind, 16))) d = 16;
        if (kind == kKindIchidan && d == 1) d = 4;
        if (kind == kKindIchidan && d == 6) {
            if (starts(tail, "たい") || starts(tail, "たかった") || starts(tail, "たく") || starts(tail, "たければ") ||
                starts(tail, "たき")) {
                d = 10;
            }
        } else if (kind == kKindSu && d == 6) {
            if (starts(tail, "したい") || starts(tail, "したかった") || starts(tail, "したく") ||
                starts(tail, "したければ") || starts(tail, "したき")) {
                d = 10;
            }
        } else if (kind == kKindSuru && d == 6) {
            if (Window(tail, 2, 4) == "たい" || Window(tail, 2, 8) == "たかった" || Window(tail, 2, 4) == "たく" ||
                Window(tail, 2, 8) == "たければ" || Window(tail, 2, 4) == "たき") {
                d = 10;
            }
        } else if (kind == kKindSuru && (d == 8 || d == 10)) {
            if (WidthOf(recordText) >= 4 && Window(tail, 0, 4) == Conj(kind, 11)) d = 11;
        }
        if (d == 0) {
            if (starts(tail, Conj(kind, 15))) {
                d = 15;
            } else if (starts(tail, Conj(kind, 14))) {
                d = 14;
            }
        }
        if (kind == kKindIchidan && (d == 8 || d == 10)) {
            const int old = d;
            for (int next = old + 1; next <= 17; next++) {
                if (next == 13) continue;
                const std::string_view e = Conj(kind, next);
                if (!e.empty() && starts(tail, e)) {
                    d = next;
                    break;
                }
            }
            if (old == d && (starts(tail, "下さ") || starts(tail, "くださ"))) d = 10;
        }
        if (d == 10) {
            if (starts(tail, Conj(kind, 15))) {
                d = 15;
            } else if (starts(tail, Conj(kind, 17))) {
                d = 17;
            }
        }
        if (branch != 8) return done();
        if (const std::size_t n = Conj(kind, 8).size(); n > 0) {
            const std::string_view rest = tail.substr(n);
            if (starts(rest, "な") || starts(rest, "ん") || starts(rest, "ざる") || starts(rest, "んな") ||
                starts(rest, "ねば") || starts(rest, "ぬ")) {
                return done();
            }
        } else {
            if (!starts(tail, "、") && !starts(tail, ",")) return done();
            if (WidthOf(key) != 2) {
                d = 7;
                return done();
            }
        }
    }
    if (kind == kKindIchidan && starts(tail, "れ") && (starts(recordText, "見") || starts(recordText, "食べ"))) {
        patternKind = kKindGodanRu;
        d = 4;
        return done();
    }
    const auto politeRequest = [&] {
        std::string middle;
        if (WidthOf(tail) >= 6) middle = Window(tail, 2, 4);
        return starts(tail, "い") && (middle == "ます" || middle == "ませ" || middle == "まし");
    };
    for (const std::string_view stem : dict::ClassMembers("stem.request")) {
        const std::size_t i = key.find(stem);
        if (i != std::string_view::npos && i == key.size() - stem.size() && politeRequest()) {
            patternKind = kKindGodanRu;
            d = 10;
            return done();
        }
    }
    return false;
}

}  // namespace hdTrans::userdict::internal
