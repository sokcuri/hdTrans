#pragma once
// The analyze stage: the per-token word stage that turns the tokens of a chunk into
// the item chain of each sentence, up to the end-of-sentence passes.
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/tokenizer/token.h"
#include "core/word/item.h"
#include "core/word/select.h"

namespace hdTrans::word {

class Worker;
struct MisreadModel;
struct SessionSenses;

// Each switch and its default; reference mode disables every switch.
#define HDTRANS_IMPROVEMENTS(X) \
    X(extraBuiltin, true)             /* Additional builtin dictionary rows. */ \
    X(literalCleanup, true)           /* Literal output replacements. */ \
    X(kanjiNumbers, true)             /* Kanji numbers with 百, 千 and of two or more characters as digits. */ \
    X(imperativeEu, true)             /* Imperative form of Korean stems ending in 으. */ \
    X(causativePassive, true)         /* Short causative-passive verb endings. */ \
    X(exclamationNa, true)            /* Exclamatory な after predicates. */ \
    X(dialectForms, true)             /* Colloquial negative and contracted verb forms. */ \
    X(documentSenses, false)          /* Reuse trusted lexical senses within a session. */ \
    X(desireWindow, false)            /* Eight-character lookahead for desire endings. */ \
    X(expandedGroups, true)           /* Allow more than 100 entries in a group. */ \
    X(semanticAlternatives, true)     /* Check every alternative of a semantic attribute. */ \
    X(preserveNeighbourSlots, false)  /* Preserve direct slots during neighbour traversal. */ \
    X(perLevelRetries, false)         /* Give each candidate search level its own retry count. */ \
    X(nonterminalBoundary, true)      /* End an unterminated ending at the query boundary. */ \
    X(tailLengthCheck, false)         /* Measure the matched tail rather than the whole record. */ \
    X(preparedNextToken, false)       /* Canonicalize lookahead token text before lookup. */ \
    X(forwardTerminal, false)         /* Visit terminal-form candidates from left to right. */ \
    X(misreadFilter, false)           /* Replace a chosen candidate the misread model scores as a misreading. */ \
    X(cgRules, true)                  /* Run the hand CG-lite rules over the misread decisions. */ \
    X(cgLearned, false)               /* Run the learned CG-lite rules over the misread decisions. */

struct Improvements {
#define HDTRANS_IMPROVEMENT_FIELD(name, defaultValue) bool name = defaultValue;
    HDTRANS_IMPROVEMENTS(HDTRANS_IMPROVEMENT_FIELD)
#undef HDTRANS_IMPROVEMENT_FIELD

    static Improvements Reference() {
        Improvements improvements;
#define HDTRANS_REFERENCE_IMPROVEMENT(name, defaultValue) improvements.name = false;
        HDTRANS_IMPROVEMENTS(HDTRANS_REFERENCE_IMPROVEMENT)
#undef HDTRANS_REFERENCE_IMPROVEMENT
        return improvements;
    }
};

// The switches the word stage reads.
struct Settings {
    Improvements improvements;
    SessionSenses* sessionSenses = nullptr;

    static Settings Reference() {
        Settings settings;
        settings.improvements = Improvements::Reference();
        return settings;
    }

    // Text input: the word choices and lookup filters of the text path apply.
    bool text = false;
    // The preferTerminal pass of the sentence passes is on.
    bool terminal = false;
    // The line of verse goes on in the next line of its document: a て form that ends it is
    // the connective 고 rather than a request.
    bool verse = false;
    // Text path: the last character of the line's text before the chunk, white space
    // aside; empty for the first chunk.
    std::string_view before;
    // Text path: the depth of 「」 and 『』 open at the start of the chunk.
    int quoteDepth = 0;
    // Text path: the line uses the tic ぽい (PoiTicLine).
    bool poiTic = false;
    // Text path: the document writes its speech in 「」, so its text outside them is narration.
    bool narrated = false;
    // Empty by default; the list and its names outlive the analysis.
    std::span<const Scorer> scorers;
    std::span<const Scorer> shadowScorers;  // recorded only when selectShadow is set
    bool selectShadow = false;
    bool selectRules = false;  // scorers may override context or rule choices, never sense rows
    const MisreadModel* misreadModel = nullptr;  // the misread filter's model; null for the built-in one
    rules::Tally* ruleTally = nullptr;  // counts the runs of the selection rules when set
    // The speech level (an output register, 0 for automatic) of sentence-final plain
    // and polite predicates; it selects the plain-dictionary end rows with a register.
    uint8_t registers[2] = {0, 0};
    int hanjaToHangul = 1;    // synthetic targets: kanji as their Hangul reading
    int deleteJapan = 1;      // synthetic targets: kana dropped
    int tokenizerOption = 1;  // a J2 ending matches at the end of the token text
    // The marks the loaded pre-filter writes (filter::Filters::Marks), sorted: such a character
    // standing alone is no word of its own, so it stays joined to its neighbours for the post-filter.
    std::span<const char32_t> filterMarks;
    // The characters the loaded pre-filter writes before kana (filter::Filters::Written), sorted:
    // kana right after one is text the pre-filter wrote, not an unknown stem to read by sound.
    std::span<const char32_t> filterWritten;
};

// Called after every dictionary lookup with the token being processed (-1 before the
// first), the query, the mode and the candidates after the filters.
struct LookupObserver {
    void (*fn)(void* context, int token, std::string_view query, int mode, const Record* records) = nullptr;
    void* context = nullptr;
};

// One sentence of a chunk.
struct Sentence {
    int first = 0;  // its tokens
    int last = 0;
    Chain chain;
    // Its control markers: tokens.markers[markerBegin, markerEnd).
    int markerBegin = 0;
    int markerEnd = 0;
    // The lookup context at its end, which the lookups of the sentence passes keep:
    // the token being processed and the last Japanese token text segmented.
    int current = -1;
    std::string_view transformSurface;
};

// The sentences of a chunk.
struct Analysis {
    std::vector<Sentence> sentences;
    // Per token: a full-width space after a word with a "Wa" grammar code, which
    // reads as " " from then on, is never visited, and does not end the sentence.
    std::vector<uint8_t> spaced;
    // A katakana transliteration fault ended the chunk in the sentence after the
    // last one; the output of the chunk is that of the sentences before.
    bool fault = false;
    // The state the items refer to (tokens, settings, lookups), which the sentence
    // passes continue with.
    std::unique_ptr<Worker> worker;

    Analysis();
    ~Analysis();
    Analysis(const Analysis&) = delete;
    Analysis& operator=(const Analysis&) = delete;
};

// Runs the token loop over tokens (prepared by the tokenizer) and returns the item
// chain of every sentence before its sentence passes. Lookups take the plain
// entries, strategy, prior dictionary and name keys of lookup (its skip is replaced
// by the word stage's own filter). The items live in arena, which is reset first.
void Analyze(const tokenizer::Tokens& tokens, const dict::Snapshot& snapshot, const dict::LookupContext& lookup,
             const Settings& settings, Arena& arena, Analysis& out, LookupObserver observer = {});

// Whether line uses the tic ぽい: ぽい (ぽーい, ぽいぽい) stands alone between marks, or two
// sentences end in っぽい or ぽい (提督さん、情報見るっぽい？了解っぽい。) (constructions.cpp).
bool PoiTicLine(std::string_view line);

}  // namespace hdTrans::word
