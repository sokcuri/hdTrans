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
};

// The senses of one call's document. Only choices from completed lines are visible to the
// scorer; the first trusted target for a key lasts to the end of the call.
struct SessionSenses {
    std::map<std::string, SessionSense, std::less<>> choices;
    std::map<std::string, SessionSense, std::less<>> pending;

    void FinishLine();
    void Remember(const Item& it);
};

// A view into target without its form code, marker suffix, auxiliary prefixes or
// common particles. This is a spelling comparison, not a Korean lemmatizer.
std::string_view DocumentSenseStem(std::string_view target HDTRANS_LIFETIMEBOUND);

int DocumentSenseScore(const Item& it, int candidate, const SelectContext& context);

}  // namespace hdTrans::word
