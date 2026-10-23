#include "core/output/inflect.h"

#include <cstdint>
#include <algorithm>

#include "core/text/hangul.h"
#include "core/text/utf8.h"

namespace hdTrans::output::internal {
namespace {

using Class = InflectionClass;
using Stem = std::u32string;

constexpr int kA = 0, kAe = 1, kYa = 2, kYae = 3, kEo = 4, kE = 5, kYeo = 6, kYe = 7;
constexpr int kO = 8, kWa = 9, kOe = 11, kU = 13, kWo = 14, kEu = 18, kI = 20;
constexpr int kNieun = 4, kRieul = 8, kRieulMieum = 10, kMieum = 16, kBieup = 17, kSsangSiot = 20;

bool Bright(int vowel) { return vowel == kA || vowel == kO || vowel == kYa || vowel == kWa; }

Stem WithFinal(Stem stem, int final) {
    stem.back() = text::SetFinal(stem.back(), final);
    return stem;
}

Stem WithVowel(Stem stem, int vowel) {
    const auto syllable = *text::Split(stem.back());
    stem.back() = text::Join(syllable.initial, vowel, 0);
    return stem;
}

Stem EuStem(const Stem& stem, Class cls, bool keepRieul) {
    switch (cls) {
        case Class::Bieup: return WithFinal(stem, 0) + U"우";
        case Class::Digeut: return WithFinal(stem, kRieul) + U"으";
        case Class::Siot: return WithFinal(stem, 0) + U"으";
        case Class::Hieuh: return WithFinal(stem, 0);
        default: break;
    }
    const int final = text::Final(stem.back());
    if (final == 0) return stem;
    if (final == kRieul) return keepRieul ? stem : WithFinal(stem, 0);
    return stem + U"으";
}

Stem FinalForm(const Stem& stem, Class cls, int final, std::u32string_view consonantEnding) {
    switch (cls) {
        case Class::Bieup:
        case Class::Digeut:
        case Class::Siot:
        case Class::Hieuh: return WithFinal(EuStem(stem, cls, true), final);
        default: break;
    }
    const int last = text::Final(stem.back());
    if (last == 0 || last == kRieul) return WithFinal(stem, final);
    return stem + Stem(consonantEnding);
}

Stem Infinitive(const Stem& stem, Class cls, uint16_t flags) {
    const auto syllable = *text::Split(stem.back());
    const int vowel = syllable.vowel;
    const auto ending = Bright(vowel) ? U"아" : U"어";
    switch (cls) {
        case Class::Yeo: {
            Stem result = stem;
            result.back() = U'해';
            return result;
        }
        case Class::Bieup: return WithFinal(stem, 0) + ((flags & kBieupWa) ? U"와" : U"워");
        case Class::Digeut: return WithFinal(stem, kRieul) + ending;
        case Class::Siot: return WithFinal(stem, 0) + ending;
        case Class::Hieuh: {
            int contracted = vowel;
            switch (vowel) {
                case kA: contracted = kAe; break;
                case kYa: contracted = kYae; break;
                case kEo: contracted = kE; break;
                case kYeo: contracted = kYe; break;
            }
            return WithVowel(stem, contracted);
        }
        case Class::U: return WithVowel(stem, kEo);
        case Class::Reo: return stem + U"러";
        case Class::Reu: {
            if (stem.size() < 2) return {};
            Stem prefix = stem.substr(0, stem.size() - 1);
            const bool bright = Bright(text::Split(prefix.back())->vowel);
            return WithFinal(prefix, kRieul) + (bright ? U"라" : U"러");
        }
        case Class::Copula: return U"(이어)여";
        default: break;
    }
    if (syllable.final != 0) return stem + ending;
    if (vowel == kEu) {
        const bool bright = (flags & kBrightEu) ||
                            (stem.size() > 1 && Bright(text::Split(stem[stem.size() - 2])->vowel));
        return WithVowel(stem, bright ? kA : kEo);
    }
    switch (vowel) {
        case kA:
        case kEo:
        case kYeo:
        case kYa: return stem;
        case kAe: return (flags & kDropAe) ? stem : stem + U"어";
        case kE: return (flags & kDropE) ? stem : stem + U"어";
        case kO: return (flags & kNoContraction) ? stem + U"아" : WithVowel(stem, kWa);
        case kU: return (flags & kNoContraction) ? stem + U"어" : WithVowel(stem, kWo);
        case kI:
            if ((syllable.initial != 11 && !(flags & kNoContraction)) || (flags & kContractIeung))
                return WithVowel(stem, kYeo);
            return stem + U"어";
        case kOe: return stem + U"어";
    }
    return stem + U"어";
}

Stem Past(const Stem& stem, Class cls, uint16_t flags) {
    if (cls == Class::Copula) return U"(이었)였";
    auto infinitive = Infinitive(stem, cls, flags);
    if (infinitive.empty()) return {};
    return WithFinal(infinitive, kSsangSiot);
}

Stem Form(const Stem& stem, KoreanPartOfSpeech pos, Class cls, uint16_t flags, char marker, char code) {
    if (marker == '$' || marker == '%') {
        if (code == '4' && (pos == KoreanPartOfSpeech::Verb || cls == Class::Existential)) {
            return (text::Final(stem.back()) == kRieul ? WithFinal(stem, 0) : stem) + U"는";
        }
        if (code == '4') code = '8';
        else if (code == '8') code = 'B';
        else if (code < '1' || code > '7') return {};
    } else if (marker != '&') {
        return {};
    }
    const int final = text::Final(stem.back());
    switch (code) {
        case '0':
            if (pos == KoreanPartOfSpeech::Adjective) return FinalForm(stem, cls, kNieun, U"은");
            return (final == kRieul ? WithFinal(stem, 0) : stem) + U"는";
        case '1': return stem;
        case '2': return EuStem(stem, cls, true);
        case '3': return final == kRieul ? stem : FinalForm(stem, cls, kRieul, U"을");
        case '4':
            if (pos == KoreanPartOfSpeech::Adjective || (flags & kBareDeclarative)) return stem;
            return final == 0 || final == kRieul ? WithFinal(stem, kNieun) : stem + U"는";
        case '5': return Past(stem, cls, flags);
        case '6': return Infinitive(stem, cls, flags);
        case '7': return final == 0 || final == kRieul ? WithFinal(stem, kBieup) : stem + U"습";
        case '8': return FinalForm(stem, cls, kNieun, U"은");
        case '9':
            if (flags & kPastHortative) return Past(stem, cls, flags) + U"습";
            if (final == 0 || final == kRieul) return WithFinal(stem, kBieup);
            if (flags & kFormalHortative) return stem + U"습";
            if (cls == Class::Bieup || cls == Class::Digeut || cls == Class::Siot)
                return FinalForm(stem, cls, kBieup, U"읍");
            return stem + U"읍";
        case 'A': return EuStem(stem, cls, false);
        case 'B':
            // A stem in ㄹ keeps it before the nominal ㅁ (만듦, 걺, 앎, 긺).
            if (final == kRieul) return WithFinal(stem, kRieulMieum);
            return FinalForm(stem, cls, kMieum, U"음");
    }
    return {};
}

}  // namespace

const KoreanFamily* FindKoreanFamily(std::string_view key) {
    const auto families = KoreanFamilies();
    const auto found = std::lower_bound(families.begin(), families.end(), key,
                                       [](const KoreanFamily& family, std::string_view k) { return family.key < k; });
    return found != families.end() && found->key == key ? &*found : nullptr;
}

std::string InflectStem(std::string_view stem, KoreanPartOfSpeech partOfSpeech,
                       Class inflectionClass, uint16_t flags, char marker, char code) {
    Stem syllables;
    while (!stem.empty()) {
        const auto rune = text::DecodeRune(stem);
        if (!text::Split(rune.cp)) return {};
        syllables += rune.cp;
        stem.remove_prefix(rune.size);
    }
    if (syllables.empty()) return {};
    const auto form = Form(syllables, partOfSpeech, inflectionClass, flags, marker, code);
    std::string result;
    for (const auto syllable : form) text::AppendRune(result, syllable);
    return result;
}

std::string Inflect(const KoreanFamily& family, char code) {
    for (const auto& exception : family.exceptions) {
        if (exception.code == code) return std::string(exception.surface);
    }
    if (!(family.forms & InflectionFormBit(code))) return {};
    return InflectStem(family.stem, family.partOfSpeech, family.inflectionClass, family.flags, family.key[0], code);
}

}  // namespace hdTrans::output::internal
