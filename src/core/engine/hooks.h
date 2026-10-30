#pragma once
// Hook points where the 0.8 proof of concept (poc08/bridge) replaces stages of the text path:
// normalization, segmentation and the composition of a sentence. The call sites exist only
// when the library is built with HDTRANS_POC08_HOOKS (the hdtrans_hooked target); a null
// member, or no hooks in TranslationOptions, keeps every stage as it is.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/output/item.h"
#include "core/tokenizer/token.h"

namespace hdTrans::word {
struct Chain;
}

namespace hdTrans::hooks {

// One sentence after resolve and compose, as the generate hook reads it.
struct Sentence {
    int line = 0;                       // index in the call's lines, context lines included
    int chunk = 0;                      // chunk of the line
    int index = 0;                      // sentence of the chunk
    std::string_view chunkText;         // the chunk's pieces joined; Token::sourceBegin/End count in it
    const tokenizer::Tokens* tokens = nullptr;
    int firstToken = 0, lastToken = 0;  // the sentence's tokens
    std::span<const output::ResolvedItem> items;  // the resolved items, in chain order
    const word::Chain* chain = nullptr;
    std::string_view composed;          // what the compose stage wrote for the sentence
    // The speech of the line as the compose stage read it.
    bool casual = false, asked = false, narrated = false, spoken = false;
    int quoteDepth = 0;                 // 「」 open at the start of the chunk
};

struct Hooks {
    void* context = nullptr;
    // Called once per TranslateDocument call with its lines (context lines included); the result is
    // the doc argument of the other hooks, released by endDocument.
    void* (*beginDocument)(void* context, std::span<const std::string> lines) = nullptr;
    void (*endDocument)(void* context, void* doc) = nullptr;
    // N: the line's text after the pre-filter → the normalised text the sound stage reads; false
    // keeps the 0.7.x normalization.
    bool (*normalize)(void* context, void* doc, int line, std::string_view text, std::string& out) = nullptr;
    // A: the segmentation of one chunk; the result is the chunk argument of fits, generate and
    // endChunk (null for none).
    void* (*beginChunk)(void* context, void* doc, int line, int chunk, std::string_view text) = nullptr;
    void (*endChunk)(void* context, void* chunk) = nullptr;
    // A: whether a word over [begin, end) of the chunk text, of the part-code class digit part
    // ('1' noun, '4' verb, …), keeps to the segmentation. A word that does not gives way to the next
    // shorter candidate at the same place that does; when none does, the first candidate stays.
    bool (*fits)(const void* chunk, uint32_t begin, uint32_t end, char part) = nullptr;
    // D inside 0.7.x: may change which candidate an item of the chain selects (for example the ending
    // 0.7.x already had for the word); true when it did, and the sentence is composed again.
    bool (*reselect)(void* context, void* doc, void* chunk, const Sentence& sentence, word::Chain& chain) = nullptr;
    // G: the output of a sentence; false keeps what the compose stage wrote.
    bool (*generate)(void* context, void* doc, void* chunk, const Sentence& sentence, std::string& out) = nullptr;
};

// The hooks of one TranslateDocument call and where it is.
struct Run {
    const Hooks* hooks = nullptr;
    void* doc = nullptr;
    int line = -1;
    // While a call translates the pieces of lines longer than kMaxLineBytes: the line of each piece.
    std::vector<int> pieceLine;
    int LineOf(std::size_t piece) const {
        if (pieceLine.empty()) return static_cast<int>(piece);
        return piece < pieceLine.size() ? pieceLine[piece] : -1;
    }
};

}  // namespace hdTrans::hooks
