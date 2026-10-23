#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "core/text/lifetime.h"
#include "core/word/select.h"

namespace hdTrans::word {

struct SessionSense {
    std::string target;
    std::string partCode;
    std::uint64_t insertionAge = 0;  // Untracked entries are oldest.
};

// Only choices from completed lines are visible to the scorer. The first trusted
// target for a key lasts until reset or a between-call memory trim.
struct SessionSenses {
    std::map<std::string, SessionSense, std::less<>> choices;
    std::map<std::string, SessionSense, std::less<>> pending;

    void FinishLine();
    // Evicts oldest insertions to bound cross-call choices; call after the whole document.
    void Trim();
    void Remember(const Item& it);

private:
    std::uint64_t lastInsertionAge_ = 0;
};

// A view into target without its form code, marker suffix, auxiliary prefixes or
// common particles. This is a spelling comparison, not a Korean lemmatizer.
std::string_view DocumentSenseStem(std::string_view target HDTRANS_LIFETIMEBOUND);

int DocumentSenseScore(const Item& it, int candidate, const SelectContext& context);

}  // namespace hdTrans::word
