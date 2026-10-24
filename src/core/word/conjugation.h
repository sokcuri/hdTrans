#pragma once
// Japanese verb conjugation endings.
#include <span>
#include <string_view>

namespace hdTrans::word {

constexpr int kConjugationKinds = 14;     // rows: the kind digit of a verb's part code
constexpr int kConjugationBranches = 18;  // columns: the conjugation branch
constexpr int kKindWa = 0;                // わ-row godan verbs (しまう, and its ちゃう, ちまう)
constexpr int kKindIku = '?' - '0';       // 行く-type verbs
constexpr int kPrefixForms = 200;
constexpr int kPrefixGates = 34;

// The ending of kind and branch; kind kKindIku is kind 1 (く) with った, って and
// っちゃ in branches 6, 7 and 17. "" outside the table. Some cells are the literal
// pseudo-forms "(-X8|)" and "XX".
std::string_view Conjugation(int kind, int branch);

// Forms of する, くる and 来る used as prefixes: entry e of group g (1..3) is index
// 50 * g + e, e < kConjugationBranches; the group's first entry is the dictionary form.
// Group 0 holds forms of 死ぬ. "" outside 0..199.
std::string_view PrefixForm(int index);

// The texts that gate a conjugated する, くる or 来る found by its prefix form, by
// number ("い", "かった", ...; 14 is "に"). "" outside 0..33.
std::string_view PrefixGate(int index);

namespace internal {

std::span<const char* const, kConjugationBranches> ConjugationRow(int kind);  // kind 0..13
std::span<const char* const, kPrefixForms> PrefixFormTable();
std::span<const char* const, kPrefixGates> PrefixGateTable();

}  // namespace internal

}  // namespace hdTrans::word
