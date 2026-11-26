#pragma once
// Rules that reject a word followed by a given text.
#include <span>

namespace hdTrans::word {

// A rule applies to a word of class sourceClass whose text equals source and that is
// followed by a word of class followClass; a source starting with "XX" applies to every
// word of sourceClass whatever follows. first lists ';' or ' ' separated tokens:
// "text" rejects when the following text starts with text, "text$" when it is text and the token ends
// there, "text[k]" when the
// dictionary has a kind-k entry for text + the following text. Classes are the
// characters '1'..'8' and 'X'.
struct ValidityRule {
    const char* source;
    char sourceClass;
    char followClass;
    const char* first;
};

// In table order; a word is valid when no applicable rule rejects it.
std::span<const ValidityRule> ValidityRules();

}  // namespace hdTrans::word
