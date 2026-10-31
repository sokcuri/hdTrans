#pragma once
// Hangul syllable arithmetic and UTF-8 conversion for the Korean generator.
#include <string>
#include <string_view>

namespace hd8gen::hangul {

constexpr char32_t kBase = 0xAC00;
constexpr char32_t kLast = 0xD7A3;

// Vowel indices (medial).
enum Vowel : int {
    A = 0, AE = 1, YA = 2, YAE = 3, EO = 4, E = 5, YEO = 6, YE = 7, O = 8, WA = 9, WAE = 10, OE = 11,
    YO = 12, U = 13, WO = 14, WE = 15, WI = 16, YU = 17, EU = 18, UI = 19, I = 20
};
// Initial indices used by the rules.
constexpr int kInitIeung = 11;
// Final indices used by the rules.
constexpr int kFinNone = 0, kFinNieun = 4, kFinRieul = 8, kFinRieulMieum = 10, kFinMieum = 16, kFinBieup = 17,
              kFinSiot = 19, kFinSsangSiot = 20, kFinHieuh = 27;

// Syllables the phonology names (computed, so the sources hold no Hangul literals).
constexpr char32_t Syl(int initial, int medial, int final = 0) {
    return kBase + static_cast<char32_t>((initial * 21 + medial) * 28 + final);
}
constexpr int kInitRieul = 5;
constexpr char32_t kEu = Syl(kInitIeung, EU), kI = Syl(kInitIeung, I), kO = Syl(kInitIeung, O), kE = Syl(kInitIeung, E),
                   kYe = Syl(kInitIeung, YE), kYo = Syl(kInitIeung, YO), kYeo = Syl(kInitIeung, YEO), kReo = Syl(kInitRieul, EO);
// "(으)" and "(이)" of the ending and particle notation
inline std::u32string EuMark() { return std::u32string{U'(', kEu, U')'}; }
inline std::u32string IMark() { return std::u32string{U'(', kI, U')'}; }

bool IsSyllable(char32_t c);
int Initial(char32_t c);  // -1 when not a syllable
int Medial(char32_t c);
int Final(char32_t c);
char32_t Join(int initial, int medial, int final);
char32_t WithFinal(char32_t c, int final);
char32_t WithMedial(char32_t c, int medial);

// Compatibility jamo (ㄱ…ㅎ, ㅏ…ㅣ) ↔ indices; -1 when there is none.
int FinalOfJamo(char32_t jamo);
int InitialOfJamo(char32_t jamo);
int MedialOfJamo(char32_t jamo);
char32_t JamoOfInitial(int initial);
bool IsConsonantJamo(char32_t c);

std::u32string ToU32(std::string_view s);
std::string ToU8(std::u32string_view s);
void AppendU8(std::string& out, char32_t c);

// Sound class of the end of a written word, for particle and copula allomorphy:
// 'V' vowel, 'C' consonant, 'L' ㄹ, 0 unknown. Reads digits (by their Sino-Korean reading) and
// Latin letters, and skips closing quotes and brackets.
char EndSound(std::u32string_view word);

}  // namespace hd8gen::hangul
