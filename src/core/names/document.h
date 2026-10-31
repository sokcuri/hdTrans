#pragma once
// The context of a document, built from all its lines before any of them is translated,
// and the context of each line: what the stages of the text path read besides the
// dictionaries (the name registry, the kin terms, the person places and the plain
// entries of the names).
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/codes.h"
#include "core/dict/snapshot.h"
#include "core/dict/user_text.h"
#include "core/names/list.h"
#include "core/names/name_cues.h"
#include "core/names/registry.h"
#include "core/pipeline/line_context.h"
#include "core/userdict/conjugating.h"

namespace hdTrans::pipeline {

// What the lines of a document show together. Build it with BuildDocument.
struct DocumentContext {
    DocumentContext() = default;
    DocumentContext(const DocumentContext&) = delete;
    DocumentContext& operator=(const DocumentContext&) = delete;

    const dict::Snapshot* snap = nullptr;
    const struct DocumentData* data = nullptr;
    // Identifies the registry entries and speaker bindings.
    std::string registryKey;
    // The key the document is identified by: the registry key with the female, persons,
    // verse and historical parts appended.
    std::string key;
    bool female = false;
    // The document writes its speech in 「」: its text outside them is narration.
    bool narrated = false;
    bool extraBuiltin = false;
    std::vector<std::string> persons;  // sorted person surfaces
    std::vector<std::string> verse;    // sorted verse lines
    std::string historical;            // JP KR POS rows
    dict::KeySet persons_;             // the persons of the document, for the person places
    dict::KeySet cued;                 // the keys a strong cue of the document names (names::NameCues)
    dict::KeySet accepted;             // the accepted names of the registry
    dict::KeySet named;                // the accepted names and the surfaces shown to be names
    std::map<std::string, int, std::less<>> prose;  // Registry::ProseKatakana
    std::set<std::string> verseLines;  // the lines that run on into the next (verse)
    std::unique_ptr<names::Registry> registry;
    std::unique_ptr<dict::CodeTable> codes;  // the codes of the entries compiled here
    userdict::Families families;
    // The plain entries of the registry, the listed names and the historical names, before the
    // skip filter. The entries of listed names have file kListedFile.
    std::vector<dict::UserEntry> extra;
    // The Korean of the listed names that stand alone (not hon).
    std::map<std::string, std::string, std::less<>> listed;
    // The Korean of the listed persons, hon ones too.
    std::map<std::string, std::string, std::less<>> listedPersons;
    struct CachedLookup {
        std::unique_ptr<dict::UserText> plain;
        dict::KeySet confirmed;  // filtered accepted names, only for nonempty skip keys
    };
    // The lookups' plain lists and confirmed names by their skip key, built on first use.
    std::map<std::string, CachedLookup> plainByKey;
    // names::NameCues by text, read once for the lines and the line facts.
    std::map<std::string, std::vector<names::NameCue>, std::less<>> nameCues;
};

// The facts of one line of a document.
struct LineFacts {
    // The line as the stages read it: kin terms written, ruby markup removed.
    std::string text;
    std::vector<std::string> skip;         // registered names left out for the line, sorted
    std::vector<std::string> persons;      // the place keys the line shows to be persons, sorted
    std::optional<std::string> wholeLine;  // the line's KR row of lines.tsv, when it is one
    bool verse = false;                    // the line runs on into the next line
    std::vector<std::string> prose;        // names::ProseRuns of the line
    std::vector<std::string> cued;         // the keys a cue of the line names, sorted
};

// The lookups of a line: its plain entries, the names it confirms and its person places.
// The plain entries and confirmed names are borrowed from the document.
struct LineLookup {
    const dict::UserText* plain = nullptr;
    const dict::KeySet* confirmed = nullptr;
    dict::KeySet persons;
    dict::KeySet cued;               // the keys the line or a strong cue of the document names
    std::vector<std::string> prose;  // LineFacts::prose
};

// The source index of the plain entries of listed names: later than every dictionary file, so
// that a listed name replaces a plain row of its key.
inline constexpr int kListedFile = 1 << 30;

// Builds the context of the lines of a document (each may end in "\r"), with the names of
// list (null for none).
std::unique_ptr<DocumentContext> BuildDocument(const dict::Snapshot& snap, std::span<const std::string> lines,
                                               bool extraBuiltin = false, const names::List* list = nullptr);

// The names list would add for the document of lines: the registry's accepted names that list
// lacks, in the list format, one per line.
std::string ProposeNames(const dict::Snapshot& snap, std::span<const std::string> lines,
                         const names::List* list = nullptr);

// The facts of one line of the document; line is the line without its "\r".
LineFacts ReadLine(DocumentContext& doc, std::string_view line);

// The lookups of a line with its facts. The plain list is built once per skip list.
LineLookup LookupOf(DocumentContext& doc, const LineFacts& facts);

// The line context of a line with its lookups; markers are left empty.
LineContext ContextOf(const DocumentContext& doc, const LineLookup& lookup);

// The names a text confirms as persons, from its speaker cues, for text without a
// document.
std::vector<std::string> CuedNames(const dict::Snapshot& snap, std::string_view text);

namespace internal {

// Place keys shown as persons. With a registry, a key is named when the registry
// names it or persons contains it; without one, a title or speaker cue is required.
std::vector<std::string> PersonPlaces(const dict::Snapshot& snap, std::string_view text, names::Registry* registry,
                                      const dict::KeySet* persons);

// The lines that run on into the next line: a line that ends in て or で, holds no sentence end
// before it and is followed by a line with hiragana.
std::set<std::string> VerseLines(std::span<const std::string> lines);

}  // namespace internal

}  // namespace hdTrans::pipeline
