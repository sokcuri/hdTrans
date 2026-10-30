#pragma once
// Read-only view of a compiled lexicon file (tools/lexbin.py writes it).
//
// File layout, little-endian, every table 4-byte aligned:
//   magic "HD8LEX\0\0", u32 version, u32 tableCount, then tableCount × (u32 offset, u32 count) for
//   entries, senses, atoms, alts, phrases, names, classes, byJa, strings.
// Strings are one NUL-terminated UTF-8 blob; a string field is a byte offset into it (0 = empty).
// Entries are sorted by (ja, yomi, pos) as bytes joined with 0x1F; byJa lists entry indexes by ja.
// Phrases are sorted by the first lemma of their pattern, names by ja.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hd8::lex {

enum class Pos : uint8_t { n, pn, num, suf, pref, v1, v5, vs, vk, vz, adj, na, adv, adn, conj, intj, count };
enum class Status : uint8_t { reviewed, machine, unreviewed };
enum class KClass : uint8_t { none, REG, HA, D, B, BWA, S, H, REU, REO, U, EU, L, COP, EXIST };
enum class KPos : uint8_t { none, verb, adjective };
enum class Slot : uint8_t {
    subj, obj, dat, loc, with, from, to, arg, head, mod, pred, after, next, form, domain, reg, mainVol, ctx,
    subjOf, objOf, datOf, verb, count
};
enum class AltKind : uint8_t { lemma, cls, absent };
enum class NameType : uint8_t { family, given, person, place, org, title, other, ship, work };

struct RawEntry { uint32_t ja, yomi; uint8_t pos, st, flags, pad; uint32_t firstSense; uint16_t nSense, pad2; uint32_t feat; };
struct RawSense {
    uint32_t ko; uint8_t kcls, kpos, st, pad; uint32_t firstAtom; uint16_t nAtom, sense; uint32_t frame, prov;
};
struct RawAtom { uint8_t slot, pad; uint16_t nAlt; uint32_t firstAlt; };
struct RawAlt { uint8_t kind, pad; uint16_t pad2; uint32_t value; };
struct RawPhrase { uint32_t id, pattern, lemmas, ko, firstAtom; uint16_t nAtom; uint8_t st, pad; uint32_t head; };
struct RawName { uint32_t ja, yomi, ko, needs; uint8_t type, conf; uint16_t pad; };
struct RawClass { uint32_t name; int32_t parent; };

constexpr uint8_t kEntryLongUnit = 1;

class Lexicon {
public:
    // Loads a file written by lexbin.py. On failure returns false and sets err.
    bool Load(const std::string& path, std::string& err);
    // Takes the file contents (for WASM and tests).
    bool LoadBytes(std::vector<uint8_t> bytes, std::string& err);

    size_t EntryCount() const { return entries_.size(); }
    size_t PhraseCount() const { return phrases_.size(); }
    size_t NameCount() const { return names_.size(); }

    // Entry index of the lemma id, or -1. yomi may be hiragana or katakana. Verb classes (v1 v5 vs vk vz)
    // match each other only when no entry has the exact class.
    int32_t Find(std::string_view ja, std::string_view yomi, Pos pos) const;
    // Entry indexes whose ja is this spelling (any reading and pos), in key order.
    std::vector<uint32_t> FindSpelling(std::string_view ja) const;

    const RawEntry& Entry(uint32_t i) const { return entries_[i]; }
    std::span<const RawSense> Senses(uint32_t entry) const;
    std::span<const RawAtom> Atoms(const RawSense& s) const { return atoms_.subspan(s.firstAtom, s.nAtom); }
    std::span<const RawAtom> Atoms(const RawPhrase& p) const { return atoms_.subspan(p.firstAtom, p.nAtom); }
    std::span<const RawAlt> Alts(const RawAtom& a) const { return alts_.subspan(a.firstAlt, a.nAlt); }
    std::span<const RawPhrase> Phrases() const { return phrases_; }
    // Phrases whose pattern starts with this lemma.
    std::span<const RawPhrase> PhrasesStartingWith(std::string_view lemma) const;
    // Names with this spelling.
    std::span<const RawName> FindNames(std::string_view ja) const;

    int32_t ClassIndex(std::string_view name) const;
    // Is class c the class `ancestor` or below it?
    bool IsA(int32_t c, int32_t ancestor) const;
    std::span<const RawClass> Classes() const { return classes_; }

    std::string_view Str(uint32_t off) const;
    // Row id for decisions: "lex:<ja>/<yomi>/<pos>#<sense>" (IR.md §8).
    std::string RowId(uint32_t entry, uint16_t sense) const;

private:
    std::vector<uint32_t> data_;
    std::span<const RawEntry> entries_;
    std::span<const RawSense> senses_;
    std::span<const RawAtom> atoms_;
    std::span<const RawAlt> alts_;
    std::span<const RawPhrase> phrases_;
    std::span<const RawName> names_;
    std::span<const RawClass> classes_;
    std::span<const uint32_t> byJa_;
    std::string_view strings_;
};

std::string_view PosName(Pos p);
bool PosFromName(std::string_view s, Pos& out);
std::string_view SlotName(Slot s);
std::string_view KClassName(KClass c);
// Hiragana copy of a reading given in katakana or hiragana.
std::string Hiragana(std::string_view s);

}  // namespace hd8::lex
