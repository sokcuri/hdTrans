#include "core/register/hangul.h"

namespace hdTrans::reg::hangul {

bool IsSyllable(char32_t c) { return c >= kBase && c <= kLast; }
int Initial(char32_t c) { return IsSyllable(c) ? static_cast<int>(c - kBase) / (21 * 28) : -1; }
int Medial(char32_t c) { return IsSyllable(c) ? static_cast<int>(c - kBase) / 28 % 21 : -1; }
int Final(char32_t c) { return IsSyllable(c) ? static_cast<int>(c - kBase) % 28 : -1; }
char32_t Join(int initial, int medial, int final) {
    return kBase + static_cast<char32_t>((initial * 21 + medial) * 28 + final);
}
char32_t WithFinal(char32_t c, int final) { return Join(Initial(c), Medial(c), final); }
char32_t WithMedial(char32_t c, int medial) { return Join(Initial(c), medial, Final(c)); }

namespace {

constexpr std::u32string_view kCompat = U"ㄱㄲㄳㄴㄵㄶㄷㄸㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅃㅄㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
constexpr std::u32string_view kInitials = U"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
constexpr std::u32string_view kFinals = U" ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ";
constexpr std::u32string_view kMedials = U"ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ";

bool IsClosing(char32_t c) {
    switch (c) {
        case U'」': case U'』': case U'）': case U')': case U']': case U'>': case U'〉': case U'》':
        case U'"': case U'\'': case U'’': case U'”': case U'】': case U'〕':
            return true;
        default:
            return false;
    }
}

// Digits without leading zeros: the reading of the last non-zero place decides.
char DigitSound(std::u32string_view digits) {
    while (digits.size() > 1 && digits[0] == U'0') digits.remove_prefix(1);
    const std::size_t n = digits.size();
    if (n == 1 && digits[0] == U'0') return 'C';  // 영
    if (digits[n - 1] == U'0') return 'C';         // 십, 백, 천, 만 …
    switch (digits[n - 1]) {
        case U'1': case U'7': case U'8': return 'L';            // 일 칠 팔
        case U'2': case U'4': case U'5': case U'9': return 'V';  // 이 사 오 구
        default: return 'C';                                     // 삼 육
    }
}

char LatinSound(std::u32string_view w) {
    std::size_t start = w.size();
    while (start > 0 && ((w[start - 1] >= U'A' && w[start - 1] <= U'Z') || (w[start - 1] >= U'a' && w[start - 1] <= U'z')))
        --start;
    const std::u32string_view run = w.substr(start);
    bool upper = true;
    for (const char32_t c : run)
        if (c >= U'a' && c <= U'z') upper = false;
    const char32_t last = run.back();
    if (upper && run.size() <= 4) {  // read letter by letter: 엘 엠 엔 알 end in a consonant
        if (last == U'L' || last == U'R') return 'L';
        if (last == U'M' || last == U'N') return 'C';
        return 'V';
    }
    const char32_t l = last >= U'A' && last <= U'Z' ? last + 32 : last;
    switch (l) {
        case U'b': case U'g': case U'm': case U'n': return 'C';
        case U'l': return 'L';
        default: return 'V';
    }
}

}  // namespace

int FinalOfJamo(char32_t jamo) {
    for (int i = 1; i < 28; ++i)
        if (kFinals[static_cast<std::size_t>(i)] == jamo) return i;
    return -1;
}
int MedialOfJamo(char32_t jamo) {
    for (int i = 0; i < 21; ++i)
        if (kMedials[static_cast<std::size_t>(i)] == jamo) return i;
    return -1;
}
char32_t JamoOfInitial(int initial) {
    return initial >= 0 && initial < 19 ? kInitials[static_cast<std::size_t>(initial)] : 0;
}
bool IsConsonantJamo(char32_t c) { return kCompat.find(c) != std::u32string_view::npos; }

std::u32string ToU32(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        const auto at = [&](std::size_t k) { return static_cast<char32_t>(static_cast<unsigned char>(s[i + k]) & 0x3F); };
        char32_t c = 0xFFFD;
        std::size_t n = 1;
        if (b < 0x80) {
            c = b;
        } else if ((b >> 5) == 6 && i + 1 < s.size()) {
            c = (static_cast<char32_t>(b & 0x1F) << 6) | at(1);
            n = 2;
        } else if ((b >> 4) == 14 && i + 2 < s.size()) {
            c = (static_cast<char32_t>(b & 0x0F) << 12) | (at(1) << 6) | at(2);
            n = 3;
        } else if ((b >> 3) == 30 && i + 3 < s.size()) {
            c = (static_cast<char32_t>(b & 0x07) << 18) | (at(1) << 12) | (at(2) << 6) | at(3);
            n = 4;
        }
        out.push_back(c);
        i += n;
    }
    return out;
}

std::string ToU8(std::u32string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (const char32_t c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

char EndSound(std::u32string_view w) {
    while (!w.empty() && (IsClosing(w.back()) || w.back() == U' ')) w.remove_suffix(1);
    if (w.empty()) return 0;
    const char32_t c = w.back();
    if (IsSyllable(c)) {
        const int f = Final(c);
        return f == 0 ? 'V' : (f == kFinRieul ? 'L' : 'C');
    }
    if (c >= U'0' && c <= U'9') {
        std::size_t start = w.size();
        while (start > 0 && w[start - 1] >= U'0' && w[start - 1] <= U'9') --start;
        return DigitSound(w.substr(start));
    }
    if (c >= U'０' && c <= U'９') {
        std::u32string d;
        std::size_t start = w.size();
        while (start > 0 && w[start - 1] >= U'０' && w[start - 1] <= U'９') --start;
        for (std::size_t i = start; i < w.size(); ++i) d.push_back(w[i] - U'０' + U'0');
        return DigitSound(d);
    }
    if ((c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z')) return LatinSound(w);
    return 0;
}

}  // namespace hdTrans::reg::hangul
