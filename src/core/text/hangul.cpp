#include "core/text/hangul.h"

#include <string>

#include "core/text/utf8.h"

namespace hdTrans::text {
namespace {

// Each initial mapped to its plain consonant.
constexpr int kPlainInitial[19] = {0, 0, 2, 3, 3, 5, 6, 7, 7, 9, 9, 11, 12, 12, 12, 0, 3, 7, 18};

// ㅑ ㅕ ㅛ ㅠ to ㅏ ㅓ ㅗ ㅜ.
constexpr int PlainVowel(int v) {
    switch (v) {
        case 2: return 0;
        case 6: return 4;
        case 12: return 8;
        case 17: return 13;
    }
    return 0;
}

std::string SoundKey(std::string_view s) {
    std::string out;
    int prev = -1;  // vowel of the last syllable when it has no final
    while (!s.empty()) {
        const Rune r = DecodeRune(s);
        s.remove_prefix(r.size);
        const auto syl = Split(r.cp);
        if (!syl) {
            AppendRune(out, r.cp);
            prev = -1;
            continue;
        }
        int i = kPlainInitial[syl->initial];
        int v = syl->vowel;
        const int f = syl->final;
        if (i == 9 && v == 18) {
            i = 12;
        } else if (i == 12 && (v == 2 || v == 6 || v == 12 || v == 17)) {
            v = PlainVowel(v);
        } else if (v == 1) {
            v = 5;
        }
        if (i == 11 && f == 0 && prev >= 0 &&
            (v == prev || (v == 13 && (prev == 8 || prev == 12 || prev == 17)) || (v == 20 && prev == 5))) {
            continue;
        }
        AppendRune(out, Join(i, v, f));
        prev = v;
        if (f != 0) prev = -1;
    }
    return out;
}

}  // namespace

bool SameSound(std::string_view a, std::string_view b) { return SoundKey(a) == SoundKey(b); }

bool JapaneseSpelling(std::string_view s) {
    if (s.empty()) return false;
    while (!s.empty()) {
        const Rune r = DecodeRune(s);
        const int f = Final(r.cp);
        if (f != kFinalNone && f != kFinalNieun && f != kFinalSiot) return false;
        // The vowels of Japanese kana: 아 야 에 오 와 요 우 유 으 이.
        const int v = (static_cast<int>(r.cp - kBase) / kFinalsCount) % 21;
        if (v != 0 && v != 2 && v != 5 && v != 8 && v != 9 && v != 12 && v != 13 && v != 17 && v != 18 && v != 20) return false;
        s.remove_prefix(r.size == 0 ? 1 : r.size);
    }
    return true;
}

bool AllSyllables(std::string_view s) {
    if (s.empty()) return false;
    for (std::size_t i = 0; i < s.size();) {
        const Rune r = DecodeRune(s.substr(i));
        if (r.cp < kBase || r.cp > kLast) return false;
        i += r.size;
    }
    return true;
}

}  // namespace hdTrans::text
