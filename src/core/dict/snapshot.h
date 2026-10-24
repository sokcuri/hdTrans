#pragma once
// The dictionaries loaded from an ordered list of dictionary folders, and the
// lookups over them. A Snapshot is immutable once loaded and may be used from any
// number of threads; everything a lookup depends on besides the dictionaries comes
// in its LookupContext.
//
// Loading (Load) reads the builtin folder (builtin.h) and the top-level *.tsv files
// of the folders, those of each folder in name order. A file may start with a
// header line "#! layer=NAME" (after a byte order mark) that gives its layer; a
// file without one is a base file, and another "#!" first line fails the load.
//   - Column files (their first line that is neither blank nor a "#" comment is a
//     header starting with the key column) of the layer base make the base
//     dictionary. The senses of a file shadow the senses of earlier files with the
//     same key and class digit.
//   - Column files of the layer words are lexicon layers: their senses are added to
//     the senses of earlier files; only a sense with the flag replace shadows the
//     earlier senses of its key and class.
//   - The base senses of a key are ordered by prio, higher first, then by file and
//     line order.
//   - Column files of the layer user make the user dictionary: its records match
//     ahead of the base records and never shadow them.
//   - Column files of the layer names make the name layer (see names.cpp): they
//     shadow earlier name files like base files, but never base senses. Senses with
//     a key that holds 、 or 。 or is an inflected word, or with a long Korean, are
//     dropped (Warnings counts them). A sense with the flag replace respells its key:
//     the base senses of the key that name the same place take its Korean, and so do
//     its senses in the layer; the senses of a respelled key in later name files are
//     dropped unless they respell it too or name a person.
//   - The other files are kept with their text, in order (PlainFiles). Files of the
//     layer readings are names readings tables, of the layer kana kana spellings of
//     base words (userdict::KanaSpellings), of the layers prefilter and postfilter
//     filter rows (filter.h), and the base files plain dictionaries (userdict::Parse),
//     numbered in order from 0; a plain line that does not compile fails the load. The
//     layers kana, readings, prefilter and postfilter are for plain files only, the
//     others but base for column files only.
//   - The user dictionary file, when given and present, loads after the folders.
//     Its header, if any, must be "#! layer=user": as a column file it joins the
//     user dictionary, as a plain file it is the last plain dictionary. A column
//     user dictionary file is read without the cache.
//   - The builtin endings.tsv is a plain dictionary numbered -1, loaded before the
//     others (their entries of an equal key and part code replace its entries); so
//     are the builtin phrases.tsv (but its interjections in kana, which are
//     utterances of the sound lexicon) and patterns.tsv, for the text path only.
//     senses.tsv is compiled into the sense table (userdict::ParseSenses).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/dict/codes.h"
#include "core/dict/dict.h"
#include "core/dict/tsv.h"
#include "core/dict/user_text.h"
#include "core/output/families.h"
#include "core/text/lifetime.h"
#include "core/userdict/conjugating.h"
#include "core/userdict/sense.h"

namespace hdTrans::pipeline {
struct DocumentData;
}

namespace hdTrans::dict {

namespace internal {
class SnapshotFile;
}

enum class FileLayer : uint8_t { Base, Words, Names, User, Kana, Readings, PreFilter, PostFilter };

// A *.tsv file of a dictionary folder.
struct File {
    int folder = 0;     // index of the folder in load order
    std::string name;   // the folder joined with the file name
    std::string text;
    FileLayer layer = FileLayer::Base;  // set by the load from the header
    bool user = false;                  // the user dictionary file of the load
    // text holds only the head of a column file, through the line that makes it one;
    // the load reads the rest when it parses the file.
    bool head = false;
};

namespace internal {

// The layer of the header of text; Base without a "#!" first line, false for a "#!"
// first line that is not a header.
bool ReadLayer(std::string_view text, FileLayer& layer);
// text with the content of its "#!" first line removed, so that line numbers stay.
std::string_view WithoutHeader(std::string_view text HDTRANS_LIFETIMEBOUND);

}  // namespace internal

enum class LoadErrorKind : uint8_t {
    Folder,      // the folder cannot be listed
    Read,        // the file cannot be read
    Tsv,         // a malformed line or header of a column file (tsv says which)
    UnknownPos,  // a line skipped for a pos that is neither a part code nor a known tag
    MarkerClass, // a line loaded whose marker family does not end the target or fit its part code (MarkerClassError)
    Plain,       // a line of a plain dictionary or sense table that does not compile
    Header,      // a "#!" first line that is not a header or a layer the file cannot have
    MissingRuleBodies,  // rows name local rules and no row carries a body (an older dictionary)
    NegationEnding,     // a line loaded whose 지 않 negation fits no class of its key (NegationEndingError)
};

// Whether an error of kind fails the load of its file; the other kinds list lines of a
// load that takes place.
constexpr bool FailsLoad(LoadErrorKind kind) {
    return kind != LoadErrorKind::UnknownPos && kind != LoadErrorKind::MarkerClass &&
           kind != LoadErrorKind::MissingRuleBodies && kind != LoadErrorKind::NegationEnding;
}

struct LoadError {
    std::string file;
    LoadErrorKind kind = LoadErrorKind::Read;
    int line = 0;  // 1-based; 0 for the whole file
    TsvErrorKind tsv = TsvErrorKind::Read;
    std::string message;  // why a Plain or Header line fails
};

using KeySet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

// Decides per entry whether a lookup leaves out a plain or name-layer entry.
struct SkipFunc {
    bool (*fn)(const void* context, const UserEntry& e) = nullptr;
    const void* context = nullptr;
    bool operator()(const UserEntry& e) const { return fn && fn(context, e); }
};

// What a lookup depends on besides the snapshot.
struct LookupContext {
    const UserText* plain = nullptr;  // plain-dictionary entries; null for none
    // More user-dictionary records, matched after the snapshot's own; null for none.
    const Dict* user = nullptr;
    Strategy strategy = Strategy::LongestMatch;
    // Base and user records with this dict code are preferred; empty for none.
    std::string_view priorDict;
    // Name-layer keys confirmed for the text: their name senses take part.
    const KeySet* confirmed = nullptr;
    // Respelled place keys that the text shows to be persons: their senses of the
    // person spelling's sound take it.
    const KeySet* persons = nullptr;
    SkipFunc skip;
};

// One result of a lookup. The views point into the looked-up text, the snapshot or
// the plain entries.
struct Candidate {
    std::string_view source;  // the matched key; empty for the D/I/V sentinel queries
    std::string_view target;
    const Codes* codes = nullptr;
    RuleNumber rule = 0;
    int32_t id = 0;  // the record's id; 0 for user-dictionary records and entries
    // The plain or name-layer entry the candidate was built from; null for base and
    // user-dictionary records.
    const UserEntry* user = nullptr;
};

struct Candidates {
    Candidates() = default;
    Candidates(Candidates&&) = default;
    Candidates& operator=(Candidates&&) = default;
    Candidates(const Candidates&) = delete;
    Candidates& operator=(const Candidates&) = delete;

    std::vector<Candidate> list;
    // The candidate count: user-dictionary matches plus accepted records, plus the
    // plain and name entries taken, minus the candidates they dropped.
    int count = 0;
    // The name-layer entries of the lookup, which list points to.
    std::vector<UserEntry> names;
};

class Snapshot;

struct LoadResult {
    // Null when a file could not be read or holds malformed lines.
    std::shared_ptr<const Snapshot> snapshot;
    // Every error, in file order. With a snapshot, only lines skipped for an unknown
    // pos.
    std::vector<LoadError> errors;
};

// Lists the *.tsv files of the folders in order, those of each folder in name order,
// and reads them, then the user dictionary file userFile (none when empty or missing).
// A first entry ending in .hdsnap loads a compiled snapshot without a cache. With no
// other entries, its embedded plain files are used. Otherwise, the entries supply the
// current plain files instead of the embedded ones. A column userFile fails such a load.
LoadResult LoadFolders(const std::vector<std::string>& folders, std::string_view cacheDir = {}, bool verify = false,
                       bool cacheWrite = true, std::string_view userFile = {});
// Loads explicit user files in order.
LoadResult LoadFolders(const std::vector<std::string>& folders, std::string_view cacheDir, bool verify,
                       bool cacheWrite, const std::vector<std::string>& userFiles);
// Loads the editable files without reading or writing a cache.
LoadResult LoadFoldersTsv(const std::vector<std::string>& folders, std::string_view userFile = {});
LoadResult LoadFoldersTsv(const std::vector<std::string>& folders, const std::vector<std::string>& userFiles);
// Finishes a new column-only snapshot with the folders' current noncolumn files and a
// plain userFile.
LoadResult LoadPlainFolders(std::shared_ptr<Snapshot> snapshot, const std::vector<std::string>& folders,
                           std::vector<LoadError> errors, const std::vector<std::string>& userFiles);
// Finishes a new column-only snapshot with ordered plain files and external user files.
LoadResult LoadPlainFiles(std::shared_ptr<Snapshot> snapshot, std::vector<File> files, std::size_t folders,
                         std::vector<LoadError> errors, const std::vector<std::string>& userFiles);
// Compiles all dictionary files to a standalone resource; publishes only a fully
// validated file. Any build of the same engine data reads it (LoadFolders).
bool CompileSnapshot(const std::vector<std::string>& folders, std::string_view outPath);
// Loads files in load order.
LoadResult Load(std::vector<File> files);

class Snapshot {
public:
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    // Whether a column file was loaded.
    bool HasDictionary() const { return hasDictionary_; }
    const Dict& Base() const { return base_; }
    const Dict* User() const { return user_.Len() > 0 ? &user_ : nullptr; }
    const Dict* Names() const { return names_.Len() > 0 ? &names_ : nullptr; }
    // The files that are not column files, in load order.
    const std::vector<File>& PlainFiles() const { return plain_; }
    // The body of each local rule a dictionary row carries, by rule number; an entry is
    // empty for a number no row names. The engine assembles the rules program from these
    // and the global rules of the rule files.
    const std::vector<std::string>& RuleBodies() const { return ruleBodies_; }
    // The plain entries of the word pipeline: the builtin endings, then the entries
    // of the plain dictionaries.
    const UserText& Plain(bool extra = false) const { return *(extra ? rawExtraPlain_ : rawPlain_); }
    // The plain entries of the text path: the kana spellings, the builtin endings,
    // phrases and patterns, then the entries of the plain dictionaries.
    const UserText& TextPlain(bool extra = false) const { return *(extra ? textExtraPlain_ : textPlain_); }
    // The shared dictionary data used to build document contexts.
    const pipeline::DocumentData& DocumentData() const;
    // The builtin sense table.
    const userdict::SenseTable& Senses() const { return senses_; }
    // The marker families that the conjugating plain entries and senses use beyond
    // the static ones.
    const userdict::Families& GeneratedFamilies() const { return families_; }
    // The marker families of the output lookups and the conjugating targets of the text
    // path. Appended to under the families' own lock, so a const snapshot hands them out.
    output::Families& OutputFamilies() const { return outputFamilies_; }
    // The name keys Load dropped, per file.
    const std::vector<std::string>& Warnings() const { return warnings_; }

    // Every candidate whose key is a prefix of text and the candidate count, from the
    // base and user dictionaries and the plain and name entries of ctx.
    //
    // Mode General returns all candidates with tags other than S/T/U/V in one list:
    // user-dictionary matches, then the prior-dictionary buckets in tag order, merged
    // with the ordinary buckets by ascending tag and, within a tag, longer source
    // first. The other modes return only the U, T, S or V bucket; when it is empty,
    // the prior-dictionary bucket of that tag, else the user-dictionary matches. With
    // empty text modes 1..3 query "D", "I", "V".
    //
    // Plain entries whose key prefixes text are placed among those candidates by
    // ctx.strategy, and base candidates with the key and class digit of such an entry
    // are dropped. Name-layer senses that take part are placed like plain entries.
    // Place senses of a key the name layer respells take its Korean, or the person
    // spelling for the keys of ctx.persons.
    Candidates GetWordInfo(std::string_view text HDTRANS_LIFETIMEBOUND, LookupMode mode, const LookupContext& ctx) const;

    // The records of the base dictionary stored under exactly key, in order, with key
    // as their source. With an empty key, modes 1..3 query "D", "I", "V".
    std::vector<Candidate> RetrieveRecords(std::string_view key HDTRANS_LIFETIMEBOUND, LookupMode mode) const;

    // The name-layer records of key, whether or not the key takes part in lookups.
    std::vector<Record> NameRecords(std::string_view key) const;
    // In order of first occurrence, the respelled place keys in text that have a
    // person spelling of the same sound (霧島: 기리시마 as a place, 키리시마 as a person).
    std::vector<std::string> PersonPlaceKeys(std::string_view text) const;
    // The fan reading of the katakana key when the name layer has key as a person
    // written with that sound; empty otherwise.
    std::string NameReading(std::string_view key) const;
    // NameReading, when the base dictionary reads key no other way; empty otherwise.
    std::string PersonName(std::string_view key) const;
    // Whether a cue in the text may confirm key as a person's name and the name layer
    // has a person sense of it.
    bool PersonCueKey(std::string_view key) const;

    // Bytes held by the snapshot, the texts of the plain files included.
    std::size_t MemoryBytes() const;
    bool Mapped() const;
    bool Healthy() const;
    // The editable-file generation used when a mapped resource fails validation.
    std::shared_ptr<const Snapshot> Fallback() const;

private:
    friend LoadResult Load(std::vector<File> files);
    friend struct SnapshotLoader;
    friend struct SnapshotStorage;

    // The Korean a name-layer sense with the flag replace gives its key, and which
    // base senses of the key take it: with sameSound, its place-name senses (QCT or
    // QNN) of the same sound; with allPlaces every place-name sense; with allNouns
    // every noun sense. person is the spelling of a person sense of the key that
    // differs from ko in spelling only, empty for none.
    struct Respelling {
        enum class Mode : uint8_t { SameSound, AllPlaces, AllNouns };
        std::string ko;
        std::string person;
        Mode mode = Mode::AllNouns;
    };

    Snapshot() = default;

    Candidates GetWordInfoImpl(std::string_view text HDTRANS_LIFETIMEBOUND, LookupMode mode, const LookupContext& ctx) const;
    std::vector<Candidate> RetrieveRecordsImpl(std::string_view key HDTRANS_LIFETIMEBOUND, LookupMode mode) const;
    const Respelling* Place(std::string_view key) const;
    std::string_view Respelled(const Respelling* sp, const KeySet* persons, std::string_view key, const Record& r) const;
    void NameEntries(std::string_view text, LookupMode mode, const LookupContext& ctx,
                     const std::vector<const UserEntry*>& owned, std::vector<UserEntry>& out) const;
    bool StrongName(std::string_view s, std::size_t n) const;
    bool TitleKey(std::string_view key) const;
    bool AddressedName(std::string_view s, std::size_t n) const;
    bool CuedKey(std::string_view key) const;
    bool PhraseKey(std::string_view key) const;
    bool BaseWord(std::string_view key) const;
    bool CountedKey(std::string_view key) const;

    std::shared_ptr<internal::SnapshotFile> backing_;
    bool hasDictionary_ = false;
    std::shared_ptr<const CodeTable> columnCodes_;
    CodeTable codes_;
    Dict base_;
    Dict user_;
    Dict names_;
    std::unordered_map<std::string, Respelling, StringHash, std::equal_to<>> places_;
    std::vector<std::string> warnings_;
    std::vector<File> plain_;
    std::vector<std::string> ruleBodies_;
    userdict::Families families_;
    mutable output::Families outputFamilies_;
    std::unique_ptr<const UserText> rawPlain_, textPlain_, rawExtraPlain_, textExtraPlain_;
    userdict::SenseTable senses_;
    mutable std::once_flag documentOnce_;
    mutable std::shared_ptr<const pipeline::DocumentData> documentData_;
};

}  // namespace hdTrans::dict
