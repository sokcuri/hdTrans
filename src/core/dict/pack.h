#pragma once
// Packs: rows that override one decision of a word after the word stage has chosen
// (docs/packs.md). A pack file is a plain file of a dictionary folder with the header
// "#! layer=pack"; its rows are tab-separated under a header line naming the columns
//
//   id  lemma  pos  cond  target  [slot]  [kind]  [agent]  [pri]  [spec]  [prov]  [note]
//
// slot is sense (the default), name, voice, polarity or tense. lemma (written forms joined
// by |; a lexicon id ja/yomi/pos reads its ja) and pos name the word: lemma is compiled like
// a plain-dictionary key, so a row applies to the words whose dictionary key is that key
// and whose class digit is that of pos. cond is a cue (space-separated atoms
// slot=value|value, the lexicon's cue DSL: subj obj dat loc with from to arg head mod pred
// next after form register, and on voice rows voice, the reading the chain decided; value - :
// the slot is empty, * : it is filled; a cond of * or empty: always). target is the Korean of a sense or name
// row in dictionary form, passive, potential, respectful, spontaneous or active for voice (with
// the Korean verb that says it after a colon: passive:혼나다),
// and the ending text for polarity and tense (a form code and Korean, as endings rows write
// it: 1잖아, 5었다). kind is frame (the default) or colloc: the cond of a colloc row is a
// Japanese pattern, as written, whose last word may be conjugated and which may hold one
// slot {X} or {X:@class} (one noun), and its target the Korean phrase of the whole pattern,
// {X} where the slot's Korean stands (首を縦に振る 고개를 끄덕이다, {X}を元に戻す {X}을 원래대로 되돌리다).
// A pattern without a slot may give its spellings joined by | (別に良い|別にいい).
// A file with the header "#! layer=pack-classes" holds the noun classes the cues name: under
// the header line "class parent" (more columns are ignored) the classes with their parents,
// as the lexicon's classes.tsv, or under "lemma classes" the classes of words joined by |.
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/dict/user_text.h"
#include "core/userdict/conjugating.h"

namespace hdTrans::dict {

class Dict;

enum class PackSlot : uint8_t { Sense, Name, Voice, Polarity, Tense };

// One cue atom: slot=value|value ("-" alone: the slot is empty).
struct PackAtom {
    std::string slot;
    std::vector<std::string> values;
};

struct PackRow {
    std::string id, file, spec, prov;
    PackSlot slot = PackSlot::Sense;
    std::string key;        // the dictionary key of the words it applies to ("" for lemma * and @class)
    char classDigit = 0;    // their class digit (part code byte 1)
    std::string lemmaClass; // lemma @class: the words of that pack class or classes.tsv set; "" with key "" is lemma *
    // lemma @class: the dictionary keys a member can have (the member, without its last kana or する),
    // each with its members (CheckPackClasses)
    std::unordered_map<std::string, std::vector<std::string>, StringHash, std::equal_to<>> lemmaKeys;
    std::string target;     // compiled target (sense, name), voice name, or ending text; "-" keeps the slot
    std::string written;    // the target column as written
    std::string lexeme;     // a voice row's compiled Korean verb (passive:혼나다), "" for none
    std::string agent;      // a voice row's Korean particle for the agent phrase (に, から: 에게서), "" for none
    bool adjective = false; // a noun target in 하다 (the root of a 하다 adjective), as in senses
    std::vector<PackAtom> atoms;
    int pri = 0;
    int line = 0;           // 1-based line in its file
    int global = 0;         // its id in the process-wide row registry (PackRowLabel)
    bool colloc = false;    // a colloc row (Colloc)
};

// A colloc row compiled: the pattern around its slot and the Korean of the span. The last
// word of the span takes head (by its class: a verb, an adjective, a な adjective, else
// raw), the word right after the slot takes particle, the first word takes lead, and the
// other words of the span, but the slot, are left empty.
struct Colloc {
    int row = 0;                     // index in PackTable::rows
    std::vector<std::string> befores;  // the spellings of the pattern before the slot (all of it without one)
    std::string after;                 // the pattern after the slot
    bool slot = false;
    bool spaced = false;             // the target has a space right after {X}
    std::string slotClass;           // @class of the slot, "" for any noun
    std::string lead, particle;      // Korean before the slot's, and the particle after it (0.7.x notation)
    std::string headRaw, headVerb, headAdjective, headNa;  // the rest, raw and compiled per class
};

struct PackTable {
    std::vector<PackRow> rows;  // in load order
    std::unordered_map<std::string, std::vector<int>, StringHash, std::equal_to<>> byKey;
    // the rows of lemma * and lemma @class, which no key names, by OpenIndex(slot, class digit)
    std::unordered_map<int, std::vector<int>> open;
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> parent;              // @class → parent
    std::unordered_map<std::string, std::vector<std::string>, StringHash, std::equal_to<>> classes; // lemma → @classes
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> ids;                  // row id → file:line
    std::vector<Colloc> collocs;  // in load order

    bool Empty() const { return rows.empty(); }
    // The depth of a class (a root is 1); 0 for an unknown class.
    int Depth(std::string_view cls) const;
    // Whether lemma belongs to cls or to a class below it.
    bool Member(std::string_view lemma, std::string_view cls) const;
};

struct PackError {
    std::string file;
    int line = 0;
    std::string message;
};

// Adds the rows of one pack file (text without its layer header line) to table; the
// targets are compiled with families (and base for る-verbs, may be null).
void ParsePackRows(std::string_view file, std::string_view text, const Dict* base, userdict::Families& families,
                   PackTable& table, std::vector<PackError>& errors);
// Adds the classes and memberships of one pack-classes file.
void ParsePackClasses(std::string_view file, std::string_view text, PackTable& table, std::vector<PackError>& errors);
// After every pack file: each parent, each class of a word and each class a cue names is declared.
void CheckPackClasses(PackTable& table, std::vector<PackError>& errors);

const char* PackSlotName(PackSlot slot);

// Whether a row of table reads attachment annotations (core/word/attach.h): an atom how, X.how, cl.* or
// main.*. Only then does the engine load the attachment provider.
bool ReadsAttachment(const PackTable& table);
// The key of PackTable::open for the rows of a slot and a class digit.
inline int OpenIndex(PackSlot slot, char classDigit) { return static_cast<int>(slot) * 256 + static_cast<unsigned char>(classDigit); }

// The cast-blind contract of evaluation: when the environment variable HD08_CAST_OFF names a TSV
// file, the surfaces of its first column, whose name rows are not loaded; empty otherwise.
std::set<std::string, std::less<>> CastOffNames();

// What -why prints for a row: its id, slot and target, by the process-wide id that
// ParsePackRows gives each row (rows of every snapshot loaded in the process).
struct PackRowLabel {
    std::string id, slot, target;
};
PackRowLabel PackRowById(int global);

}  // namespace hdTrans::dict
