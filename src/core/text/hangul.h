#pragma once
// Hangul syllable arithmetic: a syllable is U+AC00 + (L*21 + V)*28 + T for the
// initial L (0..18), vowel V (0..20) and final T (0..27, 0 = none).
#include <optional>
#include <string_view>

namespace hdTrans::text {

constexpr char32_t kBase = 0xAC00;  // 가
constexpr char32_t kLast = 0xD7A3;  // 힣

// Final consonant indices (T).
constexpr int kFinalNone = 0;
constexpr int kFinalGiyeok = 1;
constexpr int kFinalNieun = 4;
constexpr int kFinalRieul = 8;
constexpr int kFinalMieum = 16;
constexpr int kFinalBieup = 17;
constexpr int kFinalSiot = 19;
constexpr int kFinalSsangSiot = 20;
constexpr int kFinalIeung = 21;
constexpr int kFinalsCount = 28;

// Initial consonant indices (L).
constexpr int kInitialGiyeok = 0;
constexpr int kInitialRieul = 5;
constexpr int kInitialMieum = 6;
constexpr int kInitialBieup = 7;
constexpr int kInitialKieuk = 15;
constexpr int kInitialPieup = 17;
constexpr int kInitialHieuh = 18;

// Vowel indices (V).
constexpr int kVowelEu = 18;  // ㅡ

struct Syllable {
    int initial;
    int vowel;
    int final;
};

// The initial, vowel and final indices of r; nullopt when r is not a syllable.
constexpr std::optional<Syllable> Split(char32_t r) {
    if (r < kBase || r > kLast) return std::nullopt;
    const int n = static_cast<int>(r - kBase);
    return Syllable{n / (21 * kFinalsCount), n / kFinalsCount % 21, n % kFinalsCount};
}

constexpr char32_t Join(int initial, int vowel, int final) {
    return static_cast<char32_t>(static_cast<int>(kBase) + (initial * 21 + vowel) * kFinalsCount + final);
}

// The final index of r, -1 when r is not a syllable.
constexpr int Final(char32_t r) {
    if (r < kBase || r > kLast) return -1;
    return static_cast<int>(r - kBase) % kFinalsCount;
}

// r with its final replaced; characters other than syllables are returned unchanged.
constexpr char32_t SetFinal(char32_t r, int final) {
    const int f = Final(r);
    if (f < 0) return r;
    return static_cast<char32_t>(static_cast<int>(r) - f + final);
}

// The class of r that selects the particle after it: 0 no final, 1 final ㄹ,
// 2 another final or a character other than a syllable.
constexpr int FinalClass(char32_t r) {
    switch (Final(r)) {
        case kFinalNone: return 0;
        case kFinalRieul: return 1;
    }
    return 2;
}

// Whether a and b, two Hangul spellings of one kana word, differ only in
// conventions: plain, aspirated or tense initials (가이토, 카이토), 츠 쓰 쯔 즈,
// 자 쟈 and 조 죠, ㅐ and ㅔ, and long vowels written or not (유우키, 유키; 케이코, 케코).
bool SameSound(std::string_view a, std::string_view b);

// Whether s is Hangul syllables with the vowels of kana and no final but ㄴ and ㅅ, the
// spelling of a Japanese reading (무라사메, 신지, 핫토리), not of a Chinese or Korean one (펑, 러, 김).
bool JapaneseSpelling(std::string_view s);

}  // namespace hdTrans::text
