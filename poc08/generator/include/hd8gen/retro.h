#pragma once
// The retrofit: 0.7.x's output read as pre-IR lines (one JSON object per line: line, ja, out075, speaker,
// scene, work, dialogue, imp, lead, items, keigo, command), rebuilt as kir nodes by analysis by synthesis, then R (speech
// level), W (rewrite rules) and G. Lines whose words could not all be read keep 0.7.x's output.
#include <memory>
#include <string>
#include <vector>

namespace hd8gen {

struct RetroSystem;
// data: the generator data folder (ko/, register/, retro/).
std::shared_ptr<const RetroSystem> LoadRetro(const std::string& data, std::string& err);

struct RetroOptions {
    std::string level = "auto";          // auto | keep | strict
    std::vector<std::string> only, skip;  // rewrite rules: only these ("-none-" for none), skip these
    bool align = true;                    // build lines on 0.7.x's final output; false: from the items alone
    bool keepHapsyo = false;              // a predicate 0.7.x wrote in 합쇼체 keeps it (never lowered)
    bool debug = false;
};

struct RetroLine {
    std::string out, gen, level, key, kir;
    bool ok = false;
    bool levelChanged = false;  // the line's last sentence end changes speech level (inner sentences alone do not count)
    bool mixedLevels = false;   // the sentences of the line would end at different speech levels
    std::string ends;           // the speech level (+ for -시-) of each sentence end after the rewrite rules
    std::vector<std::string> fired;
};

struct RetroStats {
    size_t lines = 0, linesOk = 0, words = 0, fallbacks = 0, literal = 0, recovered = 0, aligned = 0, skipped = 0;
};

// Runs the retrofit over the lines of one input (R reads them as one document per work). False on a
// malformed line (err says which).
// The ids of the rewrite rules the retrofit knows.
std::vector<std::string> RetroRuleIds(const RetroSystem& sys);

bool RunRetro(const RetroSystem& sys, const std::vector<std::string>& pre, const RetroOptions& opt,
              std::vector<RetroLine>& out, RetroStats* stats, std::string& err);

}  // namespace hd8gen
