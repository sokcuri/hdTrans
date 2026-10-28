#include "core/word/conjugation.h"

namespace hdTrans::word {

namespace {

// The て forms of branch 7 with と for て and ど for で, by kind.
constexpr std::string_view kTo[kConjugationKinds] = {"っと", "いと", "いど", "しと", "っと", "んど", "んど",
                                                     "と",   "っと", "しと", "じと", "きと", "んど", "来と"};

}  // namespace

std::string_view Conjugation(int kind, int branch) {
    if (branch == kBranchTo) {
        if (kind == kKindIku) return "っと";
        return kind >= 0 && kind < kConjugationKinds ? kTo[kind] : std::string_view();
    }
    if (branch < 0 || branch >= kConjugationBranches) return {};
    if (kind == kKindIku) {
        switch (branch) {
            case 6:
                return "った";
            case 7:
                return "って";
            case 17:
                return "っちゃ";
        }
        kind = 1;
    }
    if (kind < 0 || kind >= kConjugationKinds) return {};
    return internal::ConjugationRow(kind)[branch];
}

std::string_view PrefixForm(int index) {
    if (index < 0 || index >= kPrefixForms) return {};
    return internal::PrefixFormTable()[index];
}

std::string_view PrefixGate(int index) {
    if (index < 0 || index >= kPrefixGates) return {};
    return internal::PrefixGateTable()[index];
}

}  // namespace hdTrans::word
