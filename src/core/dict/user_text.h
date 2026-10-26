#pragma once
// Plain-dictionary entries and their index.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/dict/codes.h"
#include "core/dict/dict.h"

namespace hdTrans::dict {

// One record compiled from a plain-text dictionary line; a line may compile to
// several records, numbered by order. Name-layer senses take part in lookups as
// entries of this kind too (nameLayer).
struct UserEntry {
    std::string key;
    std::string target;  // conjugating entries end in a stem marker such as "&AV@"
    // PartCode "N110", "M429", ...; semantic codes "QPNVHn"; grammar codes "Wb". The
    // dict code is GN, the code of plain entries in lookups.
    const Codes* codes = nullptr;
    int prio = 0;
    int file = 0;  // index of the source text in load order
    int line = 0;
    int order = 0;
    bool name = false;       // name or surname
    bool nameLayer = false;  // a sense of the name layer, made per lookup
    // A kana spelling of a word of the base dictionary, read only where the kana run
    // is bounded like a word.
    bool kana = false;
    bool force = false;   // ahead of every base candidate, even a longer one
    bool hidden = false;  // parsed but not loaded
    char finalClass = 0;  // josa class of target: 0 auto, 'v' vowel, 'c' consonant, 'l' ㄹ
    // Kept out of the lookup index; see UserText::Honorific.
    bool honorific = false;

    // An end row applies only to sentence-final predicates. Endings (S, T and U tags)
    // may have an empty key: they follow a verb conjugation with nothing after it.
    // register limits an end row to predicates of that register (0 for any); polite
    // marks an end row whose model is polite Japanese; stative limits it to stative
    // predicates.
    bool end = false;
    uint8_t registerLevel = 0;
    bool polite = false;
    bool stative = false;
    // An intj row read only where the utterance ends right after it (option final).
    bool utteranceEnd = false;
    bool euImperative = false;  // a verb model form starting with 쓰라; form code 2 rather than 1

    // The target (with stem marker) of the Korean verb that negates a conjugating
    // entry; empty for none.
    std::string negative;

    PartCode Part() const { return codes ? codes->part : PartCode(); }
};

// How plain entries rank against the base candidates (base and user-dictionary
// records) of a lookup.
enum class Strategy : uint8_t {
    // A plain entry goes behind the base candidates with a longer key and ahead of all
    // others (ties favour the plain entry); an entry with force goes ahead of all base
    // candidates. Base candidates keep their order within each group.
    LongestMatch,
    // Every plain entry goes ahead of every base candidate.
    UserFirst,
};

// The modes of a lookup: which tags it returns.
enum class LookupMode : uint8_t {
    General = 0,               // every tag but S, T, U and V
    AdjectivalNounEnding = 1,  // U
    AdjectiveEnding = 2,       // T
    VerbEnding = 3,            // S
    PrefixVerb = 4,            // V
};

// Hashes strings and string views alike, for maps keyed by std::string.
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>()(s); }
};

// Whether a lookup of mode returns records of tag.
bool AcceptsTag(LookupMode mode, char tag);

// An immutable index of plain-dictionary entries. The entries' codes must outlive
// it.
class UserText {
public:
    // Indexes entries, which come from the sources in load order. Hidden entries,
    // entries without part code and entries other than endings without key are
    // dropped, and so is an entry when a later source has one with the same key,
    // part code, register, polite and stative.
    explicit UserText(std::vector<UserEntry> entries);
    // Indexes only the changed keys; base and its entry codes must outlive this index.
    UserText(const UserText& base, std::vector<UserEntry> entries);
    UserText(const UserText&) = delete;
    UserText& operator=(const UserText&) = delete;

    // The number of indexed lookup entries.
    std::size_t Len() const;
    // The entries supplied to this index, without the base entries of an overlay.
    const std::vector<UserEntry>& Entries() const { return entries_; }
    // The honorific row with the longest key that prefixes text, the one loaded last
    // among equal keys, or null.
    const UserEntry* Honorific(std::string_view text) const;
    // The entries whose key prefixes text (ending on a character boundary) and whose
    // tag mode accepts: longer keys first, then descending prio, later source, then
    // line and order. Entries with an empty key match only empty text.
    std::vector<const UserEntry*> Search(std::string_view text, LookupMode mode) const;
    // Whether an entry of any mode whose key prefixes text (Search) is longer than past bytes.
    bool KeyPast(std::string_view text, std::size_t past) const;

private:
    const UserText* base_ = nullptr;
    std::vector<UserEntry> entries_;
    std::unordered_map<std::string, std::vector<const UserEntry*>, StringHash, std::equal_to<>> byKey_;
    std::size_t maxKey_ = 0;
    std::vector<const UserEntry*> honorifics_;
};

}  // namespace hdTrans::dict
