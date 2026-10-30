#pragma once
// External word boundaries (the 0.8 analyzer). When an oracle is set, the word search keeps only
// items that end where the analyzer ends a word; a word the dictionary cannot end there keeps the
// first reading.
#include <cstdint>
#include <string>
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
    // Kana verb and adjective forms of several words whose word the analyzer is sure of, with that
    // word's kanji spelling (たっ → 経っ): the item's candidates the kanji key also has come first.
    struct Lemma {
        uint32_t begin = 0, end = 0;
        std::string kanji;
    };
    std::vector<Lemma> lemmas;
};
// The external analyzer as the word search sees it (word::Settings::segmenter): hints for a token
// text, and the kanji respelling of a text. Either function may be null; a false return means no
// opinion. The functions are called from any thread at once.
struct Segmenter {
    void* ctx = nullptr;
    bool (*hints)(void* ctx, std::string_view text, BoundaryHints* out) = nullptr;
    bool (*respell)(void* ctx, std::string_view text, std::string* out) = nullptr;
};

}  // namespace hdTrans::word
