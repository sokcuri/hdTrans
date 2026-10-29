#include "core/word/conjugation.h"

#include <cstdint>
#include <string>

#include "fnv.h"
#include "hdtest.h"

using namespace hdTrans::word;

// The expected values are the hashes of the tables the data was generated from: the
// cells in table order, and Conjugation over kinds and branches -2..20.
HD_TEST(conjugation_fingerprints) {
    hdtest::Fnv cells;
    cells.U32(kConjugationKinds);
    cells.U32(kConjugationBranches);
    for (int kind = 0; kind < kConjugationKinds; kind++) {
        for (const char* c : internal::ConjugationRow(kind)) cells.Str(c);
    }
    HD_CHECK_EQ(cells.h, uint64_t{0xa0fd56fdb4eae901ull});

    hdtest::Fnv lookups;
    for (int kind = -2; kind <= 20; kind++) {
        for (int branch = -2; branch <= 20; branch++) lookups.Str(Conjugation(kind, branch));
    }
    HD_CHECK_EQ(lookups.h, uint64_t{0x7ed2ea5c6c748107ull});

    hdtest::Fnv prefix;
    prefix.U32(kPrefixForms);
    for (int i = 0; i < kPrefixForms; i++) prefix.Str(PrefixForm(i));
    HD_CHECK_EQ(prefix.h, uint64_t{0xe80fe7b978ef1d4bull});

    hdtest::Fnv gate;
    gate.U32(kPrefixGates);
    for (int i = 0; i < kPrefixGates; i++) gate.Str(PrefixGate(i));
    HD_CHECK_EQ(gate.h, uint64_t{0x355eec07a1a61c76ull});
    HD_CHECK_EQ(std::string(PrefixGate(14)), std::string("に"));
    HD_CHECK(PrefixGate(34).empty());
}

HD_TEST(conjugation_samples) {
    HD_CHECK_EQ(std::string(Conjugation(1, 6)), std::string("いた"));
    HD_CHECK_EQ(std::string(Conjugation(kKindIku, 6)), std::string("った"));
    HD_CHECK_EQ(std::string(Conjugation(kKindIku, 0)), std::string("く"));
    HD_CHECK_EQ(std::string(Conjugation(9, 1)), std::string("(-X8|)"));
    HD_CHECK(Conjugation(14, 0).empty());
    HD_CHECK(Conjugation(0, kBranchTo + 1).empty());
    HD_CHECK(Conjugation(14, kBranchTo).empty());
    HD_CHECK_EQ(std::string(PrefixForm(50)), std::string("する"));
    HD_CHECK_EQ(std::string(PrefixForm(150)), std::string("来る"));
    HD_CHECK(PrefixForm(200).empty());
}

// The と form: the て form with と for て and ど for で.
HD_TEST(conjugation_to_form) {
    for (int kind = 0; kind < kConjugationKinds; kind++) {
        const std::string te(Conjugation(kind, 7)), to(Conjugation(kind, kBranchTo));
        const std::string_view last = te.ends_with("て") ? "と" : "ど";
        HD_CHECK_EQ(to, te.substr(0, te.size() - std::string_view("て").size()) + std::string(last));
    }
    HD_CHECK_EQ(std::string(Conjugation(kKindIku, kBranchTo)), std::string("っと"));
    HD_CHECK_EQ(std::string(kOnbinTo), std::string("うと"));
}
