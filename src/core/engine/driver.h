#pragma once
// The driver: the stages of the pipeline in order, over the lines of a document (the text
// path) or over one line as a single piece (the raw path).
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine.h"
#include "core/output/families.h"
#include "core/names/document.h"
#include "core/output/pieces.h"
#include "core/pipeline/restore.h"
#include "core/tokenizer/token.h"
#include "core/word/analyze.h"
#include "core/word/document_senses.h"

namespace hdTrans {
class Session;
}

namespace hdTrans::filter {
class Filters;
}

namespace hdTrans::internal {

// Receives the outputs of the stages as they are made. A null member is not called. The
// line callback starts each line before its stages; chunk indices restart per line.
struct StageObserver {
    void* context = nullptr;
    void (*line)(void* context, int line) = nullptr;
    // N (normalised) and SN (sounded) of a line.
    void (*text)(void* context, std::string_view tag, std::string_view text) = nullptr;
    // P (the protected pieces of a line, chunk -1) and CH (a chunk).
    void (*pieces)(void* context, std::string_view tag, int chunk, const output::Pieces& pieces) = nullptr;
    // The tokens of a chunk, after tokenizing.
    void (*tokens)(void* context, int chunk, const tokenizer::Tokens& tokens) = nullptr;
    // The sentences of a chunk after the word analysis, before resolve.
    void (*analysis)(void* context, int chunk, const tokenizer::Tokens& tokens, const word::Analysis& analysis) =
        nullptr;
    // The chain of a sentence after the resolve stage.
    void (*resolved)(void* context, int chunk, int sentence, const word::Chain& chain) = nullptr;
    // The output pieces of a sentence after the compose stage (K).
    void (*composed)(void* context, int chunk, int sentence, const output::Pieces& pieces) = nullptr;
    // The cleaned-up pieces of a line (C), then the restore steps of the line (R1-R3, OUT).
    void (*cleaned)(void* context, const output::Pieces& pieces) = nullptr;
    void (*restored)(void* context, const pipeline::RestoreSteps& steps) = nullptr;
    // Ehnd-style stage snapshots and fired filter rules, including trailing newlines.
    void (*filterLog)(void* context, std::string_view message) = nullptr;
};

// Uses the session's dictionaries and options without retaining the observer; context as
// for Session::Translate.
std::string TranslateObserved(Session& session, std::string_view text, const StageObserver& observer,
                              std::string_view context = {});

// The marker families of one loaded dictionary, made by the translations of all sessions.
// The families synchronise themselves and the rules are not changed after the load, so
// sentences resolve and compose concurrently; only a translation that counts rule runs
// into a TranslationOptions::ruleTally resolves and composes under mu.
struct Generated {
    std::mutex mu;
    output::Families families;
    // The rules program assembled from the rule bodies the dictionary rows carry and the
    // global rules of the rule files; empty when no row carries a body, in which case the
    // built-in program runs.
    rules::Program rules;
    bool hasRules = false;
};

// The rules program text a loaded dictionary assembles: the header, one L line per rule
// number the rows carry (an empty rule for a number no row names) and the G lines of the
// rule files. Empty when no row carries a body.
std::string RuleProgramText(const dict::Snapshot& snap);

// The marker families of a loaded dictionary, with the families the dictionaries made at load.
std::shared_ptr<Generated> NewGenerated(const dict::Snapshot& snap);

// What a session keeps between its calls.
// The quotes a line leaves open for the lines after it (「明日行く。 then 明後日も行く」), and
// how many lines in a row have ended inside them.
struct QuoteCarry {
    int depth = 0;
    int lines = 0;
};

struct TranslationOptions {
    bool reference = false;
    word::Improvements wordImprovements;
    std::span<const word::Scorer> scorers;
    std::span<const word::Scorer> shadowScorers;
    bool selectShadow = false;
    bool selectRules = false;
    const word::MisreadModel* misreadModel = nullptr;  // null: the built-in model
    const word::Segmenter* segmenter = nullptr;        // the external analyzer; null: none
    rules::Tally* ruleTally = nullptr;  // counts the runs of the selection rules when set
    const rules::Program* rules = nullptr;  // the selection rules; null for the built-in program
    // The pre-filter rewrites each line before translation and the post-filter its
    // translation; their user dictionary joins the lookups. Null for none.
    const filter::Filters* filters = nullptr;
    // The speech level of the text inside 「」; Strict with reference.
    Speech speech = Speech::Strict;
    // Lines may break in the middle of a sentence (Session::SetWrapped).
    bool wrapped = false;
    // The text is wrapped lines joined with kBreakMark (core/engine/wrapped.h).
    bool joinedLines = false;
    // For each break of the joined lines, whether it falls inside a word (kCutBreakSpan).
    std::span<const char> cutBreaks;
    // The characters the pre-filter writes before kana (filter::Filters::Written) that none of the
    // call's lines held before it; TranslateDocument sets them.
    std::span<const char32_t> filterWritten;
};

// Translates the lines of one call as one document. Nothing is kept from one call to the
// next: the names, speech and senses of a call come from its own lines. The first context
// lines are read for their cues only. Returns the lines after them translated (each keeps
// its "\r").
std::vector<std::string> TranslateDocument(const dict::Snapshot& snap, Generated& generated,
                                           std::span<const std::string> lines,
                                           const StageObserver& observer, const TranslationOptions& options = {},
                                           std::size_t context = 0);

// Translates line (without "\n") as one piece with the word pipeline only.
std::string TranslateRawLine(const dict::Snapshot& snap, Generated& generated, std::string_view line,
                             const StageObserver& observer, const TranslationOptions& options = {});

}  // namespace hdTrans::internal
