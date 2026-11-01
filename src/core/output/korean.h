#pragma once
// Korean conjugation of the words of plain dictionaries: a lemma (먹다) becomes a
// target that ends in a marker family (먹&..@).
#include <span>
#include <string>
#include <string_view>

#include "core/output/families.h"

namespace hdTrans::output {

// The conjugation classes of Conjugating.
std::span<const std::string_view> Classes();

struct Conjugated {
    std::string target;
    std::string error;  // empty when target is set
};

// The target of a conjugating word, the inverse of Lemma: lemma (ending in 다) with
// its final syllables replaced by a marker family ('&' verbs, '$' i-adjectives, '%'
// adjectival nouns): a static family that conjugates the stem in its class, else a
// family made in generated. cls is the conjugation class (Classes), or "" to detect it
// from the spelling of the stem.
Conjugated Conjugating(std::string_view lemma, char marker, std::string_view cls, Families& generated);

// Conjugating with the static families only: an error where a family would have to be made.
Conjugated StaticConjugating(std::string_view lemma, char marker, std::string_view cls);

}  // namespace hdTrans::output
