#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace hdTrans::output::internal {

enum class KoreanPartOfSpeech : uint8_t { Verb, Adjective };

// ㄹ, ㅅ, ㅎ, 우/으, ㄷ, ㅂ, 여, 러 and 르 select the stem alternation.
enum class InflectionClass : uint8_t {
    Regular, Rieul, Siot, Hieuh, U, Eu, Digeut, Bieup, Yeo, Reo, Reu, Existential, Copula
};

enum InflectionFlag : uint16_t {
    kNoContraction = 1 << 0,
    kBieupWa = 1 << 1,
    kDropAe = 1 << 2,
    kDropE = 1 << 3,
    kBrightEu = 1 << 4,
    kContractIeung = 1 << 5,
    kFormalHortative = 1 << 6,
    kPastHortative = 1 << 7,
    kBareDeclarative = 1 << 8,
};

struct InflectionException {
    char code;
    std::string_view surface;
};

struct KoreanFamily {
    std::string_view key;
    std::string_view stem;
    KoreanPartOfSpeech partOfSpeech;
    InflectionClass inflectionClass;
    uint16_t flags;
    // Bit n admits form n ('0'..'9', 'A'..'C'); unlisted forms have no row.
    uint16_t forms;
    std::span<const InflectionException> exceptions;
};

constexpr uint16_t InflectionFormBit(char code) {
    if (code >= '0' && code <= '9') return static_cast<uint16_t>(1u << (code - '0'));
    if (code >= 'A' && code <= 'C') return static_cast<uint16_t>(1u << (10 + code - 'A'));
    return 0;
}

// Sorted by the complete four-byte family key.
std::span<const KoreanFamily> KoreanFamilies();
const KoreanFamily* FindKoreanFamily(std::string_view key);

// Marker '&' uses verb form codes; '$' and '%' use adjective form codes.
// Non-Hangul stems and invalid form codes have no computed form.
std::string InflectStem(std::string_view stem, KoreanPartOfSpeech partOfSpeech,
                       InflectionClass inflectionClass, uint16_t flags, char marker, char code);

// Exceptions take precedence; forms absent from the membership mask stay absent.
std::string Inflect(const KoreanFamily& family, char code);

}  // namespace hdTrans::output::internal
