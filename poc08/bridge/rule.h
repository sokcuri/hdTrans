#pragma once
// 0.8-rule (--poc08 rule): the generator's Korean-side retrofit on 0.7.x's own output, in the binary. The CLI
// captures what 0.7.x resolved and cleaned for every line (RuleCapture, fed by the stage observer); after a
// document is translated, RuleRewrite reads each line into the retrofit's pre-IR (the port of the retrofit
// reader), runs the retrofit (speech level and generation; its rewrite rules are off) and replaces the lines
// the dialogue detector accepts whose speech level changes. Nothing here reads a table trained on model labels.
#include <memory>
#include <string>
#include <vector>

#include "core/output/pieces.h"

namespace hdTrans::word {
struct Chain;
}

namespace hd08::bridge {

struct RuleData;
struct Bridge;

// One analyzer token of a source line, as the keigo check reads it.
struct SourceToken {
    std::string lemma, reading, cform;
    bool verb = false, prefix = false;
};
// Whether the tokens hold an honorific or humble form (reader.tsv keigo rows: いらっしゃる, 申す, おります,
// ございます, お待ちになる …).
bool KeigoSource(const RuleData& data, const std::vector<SourceToken>& toks);

// dataRoot: the poc08 data folder (bridge/data/reader.tsv, generator/data/).
std::shared_ptr<const RuleData> LoadRule(const std::string& dataRoot, std::string& err);

// What 0.7.x resolved and cleaned, per line of one TranslateDocument call (the last pass of a line counts).
struct RuleCapture {
    struct Item {
        int chunk = 0, sentence = 0, token = 0;
        std::string surface, target, ending;
    };
    std::vector<std::vector<Item>> items;  // per line
    std::vector<std::string> cleaned;      // per line: the cleaned text (the stage C pieces joined)
    int line = -1;

    void Clear();
    void Line(int line);
    void Resolved(int chunk, int sentence, const hdTrans::word::Chain& chain);
    void Cleaned(const hdTrans::output::Pieces& pieces);
};

struct RuleCounts {
    uint64_t lines = 0, dialogue = 0, read = 0, replaced = 0, captionDocs = 0, keigo = 0, sameLevel = 0, mixed = 0;
};

// Whether a line is speech by its shape: a quote that starts the line (after white space and an optional
// speaker tag) — not a quote inside running prose.
bool DialogueLine(const RuleData& data, const std::string& ja);
// Whether a document reads as captions: timing marks, speaker tags in parentheses or the half-width marks of
// broadcast captions on a fifth of its lines, or mostly short lines without quotes. The retrofit is off for
// such documents.
bool CaptionDocument(const RuleData& data, const std::vector<std::string>& ja);

// The retrofit's input line (JSON) for line i, from the capture: exposed for tests and the reader check.
std::string PreLine(const RuleData& data, size_t i, const std::string& ja, const std::string& out075,
                    const RuleCapture& capture, bool dialogue, bool keigo = false,
                    bool command = false);

// Rewrites out (the final lines of the call, one per ja line) in place.
// pre, when given, gets the retrofit's input lines (for the reader check).
// 0.7.x's 합쇼체 is never lowered. bridge, when given, analyzes the dialogue lines for keigo: there 0.7.x's
// polite level (해요체 too) is kept.
// Only lines whose speech level the retrofit changes are replaced.
void RuleRewrite(const RuleData& data, const std::vector<std::string>& ja, const RuleCapture& capture,
                 std::vector<std::string>& out, RuleCounts& counts, std::string* pre = nullptr,
                 const Bridge* bridge = nullptr);

}  // namespace hd08::bridge
