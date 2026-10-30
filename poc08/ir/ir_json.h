#pragma once
// JSON Lines interchange for the IR (IR.md §7). Every stage boundary reads and writes the same
// keys as the struct fields. Writers omit a field at its zero value (and head/parent at -1);
// readers take a missing field as that value and ignore unknown keys.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ir.h"
#include "json.h"

namespace hd08 {

// Parts of a line record, by writing stage.
enum Part : uint32_t {
    kPartSrc = 1,         // doc, line, name, src, context
    kPartNorm = 2,        // norm, map, edits                (N)
    kPartToks = 4,        // toks with nested alts            (A)
    kPartScope = 8,       // utts, mentions                   (S)
    kPartBuns = 16,       // buns                             (C)
    kPartClauses = 32,    // clauses                          (L)
    kPartKir = 64,        // kir                              (T)
    kPartReg = 128,       // reg, lev                         (R)
    kPartKir2 = 256,      // kir2                             (W)
    kPartOut = 512,       // out, spans                       (G)
    kPartFlags = 1024,    // flags
    kPartDecisions = 2048,// decisions
    kPartAll = 4095,
};

// One line record, one JSON object without a newline.
std::string LineJson(const Doc& doc, const Line& line, uint32_t parts = kPartAll);
// The document record: {"doc":…, "scenes":[…], "speakers":[…]}.
std::string DocJson(const Doc& doc);

// Reads the keys of a line record that are present into line (interning into doc).
bool ReadLine(const Json& record, Doc& doc, Line& line, std::string& err);
// Reads a document record.
bool ReadDoc(const Json& record, Doc& doc, std::string& err);
// Reads a JSON Lines file. Records group into documents by "doc" (a record without "doc" joins the
// current one); a record with neither "line" nor "src"/"toks"/"kir2"… keys but with "doc" is a
// document record. Line ids default to the 1-based position in the document.
bool ReadJsonl(std::string_view text, std::vector<Doc>& docs, std::string& err);

// Single structs, for component tests.
std::string NodeJson(const Doc& doc, const KNode& node);
bool ReadNode(const Json& value, Doc& doc, KNode& node, std::string& err);
std::string TokenJson(const Doc& doc, const Line& line, const Token& tok);
std::string ClauseJson(const Doc& doc, const Clause& clause);
std::string DecisionJson(const Doc& doc, const Decision& decision);

}  // namespace hd08
