#pragma once
// External word boundaries (the 0.8 analyzer). When an oracle is set, the word search keeps only
// items that end where the analyzer ends a word; a word the dictionary cannot end there keeps the
// first reading.
#include <cstdint>
#include <string_view>
#include <vector>

namespace hdTrans::word {

// What the external analyzer says about a token text: ends[i] for each byte offset 0..size is 0
// where no word ends, else the class letter of the analyzer word starting there ('p' particle, 'x'
// auxiliary, 'v' verb, ...), 'e' for an end only, 'i' for a cut inside a compound verb. rejected
// holds byte spans and class letters of single dictionary words the analyzer considered and split.
struct BoundaryHints {
    std::vector<uint8_t> ends;
    struct Rejected {
        uint32_t begin = 0, end = 0;
        uint8_t cls = 0;  // class letter of the rejected word
    };
    std::vector<Rejected> rejected;
    bool blockParticleReading = true;  // no content item reads one analyzer particle (のみ is no 飲み)
};
// Returns false when it has no opinion on this text.
using BoundaryFn = bool (*)(void* ctx, std::string_view text, BoundaryHints* out);
void SetBoundaryOracle(BoundaryFn fn, void* ctx);
bool QueryBoundaries(std::string_view text, BoundaryHints* out);

}  // namespace hdTrans::word
