#pragma once
// What the stages of the text path need from the document and the session a line
// belongs to, besides the dictionaries.
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "core/dict/snapshot.h"

namespace hdTrans::pipeline {

struct LineContext {
    // The line is a line of a document (its kin terms and ruby markup are rewritten);
    // false for text translated without a document.
    bool document = true;
    // The document shows a woman's speech.
    bool female = false;
    // The document writes its speech in 「」, so its text outside them is narration.
    bool narrated = false;
    bool extraBuiltin = false;
    bool exclamationNa = false;
    bool dialectForms = false;
    // Host markers: text that is protected wherever it occurs, the longest first.
    std::span<const std::string> markers{};
    // The lookups of the line: its plain entries (the plain dictionaries with the names
    // of the document), the name keys confirmed for it and its person places.
    dict::LookupContext lookup{};
    // The surfaces the document shows to be names: its accepted names and every
    // surface written where a name stands (a speaker label, before an honorific,
    // under ruby, a part of a ・ chain). Null without a document.
    const dict::KeySet* named = nullptr;
    // The katakana runs the document writes in lines that are mostly not katakana, with
    // their counts, and those of this line (names::ProseRuns). Null without a document.
    const std::map<std::string, int, std::less<>>* prose = nullptr;
    const std::vector<std::string>* lineProse = nullptr;
    // The person names of the document's name list (Session::SetNames) and their Korean.
    // Null without one.
    const std::map<std::string, std::string, std::less<>>* listed = nullptr;
};

}  // namespace hdTrans::pipeline
