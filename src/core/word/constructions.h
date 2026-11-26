#pragma once
// The construction table of the rewrite layer, constructions.tsv (docs/rule-language.md, 구문 표):
// rows that pick the Korean of a word from the words around it, written in the body syntax of the
// rules with fields of the resolved chain, and grouped in families that run between the sentence
// passes of Flush. The builtin table is parsed at build time (src/cxgen) and read once, on first use,
// into an immutable structure shared by every thread.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/rules/body.h"

namespace hdTrans::dict::internal {
struct WordClass;
}

namespace hdTrans::word {

struct Flusher;
class Item;

// Runs the families declared after the text pass at index pass of TextPassNames, by stage. skip,
// when given, leaves out the families and rows it names.
void RunConstructions(Flusher& f, std::size_t pass, bool (*skip)(std::string_view name) = nullptr);
// The id of row index of the builtin table ("cx:return-to:10"); empty for none.
std::string_view ConstructionId(int index);

namespace internal {

enum class CxField : uint8_t {
    Surface, Lemma, Target, Part, Sem, Class, EndingSurface, EndingTarget, Word, Text, Head, Feature, Candidate, Gram,
    FirstWord, Tail,
};

// How an atom compares: with a literal (rule surface markers, code patterns), with the members of a
// word class (equal to one, starting or ending with one), or by a registered feature.
enum class CxMatch : uint8_t { Literal, Member, MemberStart, MemberEnd, Feature };

using CxFeature = bool (*)(Flusher& f, const Item* it);

// No bound on the reach of an atom or a scan (CxAtom::reach, CxNode::reach).
inline constexpr uint8_t kWideReach = UINT8_MAX;

struct CxAtom {
    CxField field = CxField::Surface;
    CxMatch match = CxMatch::Literal;
    int8_t delta = 0;
    bool negated = false;
    std::string value;
    const dict::internal::WordClass* set = nullptr;
    CxFeature feature = nullptr;
    uint32_t slot = 0;  // atoms that compare alike share a slot, the place of their value on a word
    // How many words away from its own the atom reads what an action may change (Apply changes the
    // candidates, targets and endings of the word it acts on); kWideReach for no bound.
    uint8_t reach = 0;
};

struct CxNode {
    rules::BodyKind kind = rules::BodyKind::Or;
    bool atom = false;
    uint32_t count = 0;  // lists: operands; scans: the bound, 0 for none; atoms: index into atoms
    uint32_t end = 0;    // one past the subtree
    // Scans: how many words beyond those it walks its target and barrier read what an action may
    // change; kWideReach for no bound (an atom without one, or a scan inside).
    uint8_t reach = 0;
};

// The actions of a row. A new one may change candidates, targets and endings but no surface and no link
// of the chain (Apply).
enum class CxVerb : uint8_t {
    SelectLemma, SelectTarget, TakeTarget, SetTarget, AppendTarget, PrependTarget, SetEnding, ReplaceHead, ReplaceEndingHead, ReplaceEndingTail,
    MapEndingTail, MapTargetTail, MapLemma, MapTarget, SelectMap, SetVerb, SetVerbFrom, CopyEnding, CopyWord, SetSound, SetActive, ClipEnding, SetBaseVerb, DrawEnding,
    TakePrevTarget,
};

struct CxAction {
    CxVerb verb = CxVerb::SelectLemma;
    int8_t delta = 0;
    bool scanned = false;     // @scan±n: counted from the word where the row's first scan outside !1 finds its target
    std::string value, with;  // with: the new head of replace-head, the new tail of replace-ending-tail and set-verb-from
    int map = -1;             // map-lemma, map-target, select-map, map-ending-tail: into maps
};

// A word map declared by "//~ NAME <TAB> FROM=TO ... <TAB> what it is for".
struct CxMap {
    std::string name;
    std::vector<std::pair<std::string, std::string>> pairs;
};

struct CxRow {
    std::string id;  // cx:<family>:<prio>
    std::string spec;
    int prio = 0;
    uint32_t root = 0;  // into nodes
    std::vector<CxAction> actions;
    bool scans = false;  // an action is placed by @scan
    int line = 0;  // in the table
};

struct CxSurfaceHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

inline constexpr uint32_t kNoGuard = UINT32_MAX;

// What must hold at a word for a family to fire there, so that the words where it does not are not
// tried: one of atoms, into ConstructionTable::atoms (one of each slot), holds wherever the guard or a
// row does.
struct CxAnchors {
    std::vector<uint32_t> atoms;
    std::vector<uint32_t> changing;  // those whose words a row that fires may change (AnchorFixed)
    bool found = false;              // none: every word is tried
};

struct CxFamily {
    std::string name;
    std::string after;      // the text pass it runs after
    int stage = 0;          // its order among the families after that pass
    std::vector<int> rows;  // into rows, by PRIO
    int line = 0;           // of its //= line
    // The root of its guard in nodes, kNoGuard for none: a word on which the guard is false tries
    // none of the family's rows.
    uint32_t guard = kNoGuard;
    // The surfaces the guard needs the word to have, when it names them (S+0== "もら", S+0== @set),
    // sorted, into ConstructionTable::surfaces: a word with another surface skips the family without
    // trying the guard.
    std::vector<uint32_t> guardSurfaces;
    bool guardKeyed = false;
    // The rows worth trying on a word, by PRIO, sorted by surface (into ConstructionTable::surfaces).
    // A row whose condition needs the word to be one of a few surfaces (S+0== "で", S+0== @set) is
    // listed under each of them; the other rows (open) are listed under every surface. A word whose
    // surface is no key tries the open rows only.
    std::vector<std::pair<uint32_t, std::vector<int>>> bySurface;
    std::vector<int> open;
    bool indexed = false;  // some row has surfaces
    CxAnchors anchors;
    // The atoms every row needs to hold (itself, or an operand of its && or &&n at any depth), into
    // atoms: a word on which one is false fires none of the rows.
    std::vector<uint32_t> shared;
};

struct CxError {
    int line = 0;
    std::string message;
};

// How a value index finds an anchor: the words whose value equals, starts with, ends with, starts or
// holds one of its strings.
enum class CxProbe : uint8_t { Exact, Prefix, Suffix, Within, Contains };

// The keys of a value index as a byte trie, walked over a value from its start (keys it equals, starts
// with or starts), from its end (keys it ends with, stored reversed) or from each byte (keys it holds).
// A node lists the families of the keys that end at it, whole (the value equals the key) and part (the
// value starts, ends with or holds it), and of those whose path passes it (within: the value starts
// the key).
struct CxTrie {
    struct Node {
        uint32_t edges = 0, edgeCount = 0;  // into edges, sorted by byte
        uint32_t whole = 0, wholeCount = 0, part = 0, partCount = 0, within = 0, withinCount = 0;  // into families
    };
    std::vector<Node> nodes;  // the root first
    std::vector<std::pair<uint8_t, uint32_t>> edges;  // byte, node
    std::vector<uint32_t> families;
};

// The anchors of the families after a pass on one field of the word delta words away.
struct CxValueIndex {
    CxField field = CxField::Surface;
    int delta = 0;
    bool fixed = true;  // the field reads only what no action changes
    CxTrie forward, backward, inner;  // exact, prefix and within keys; suffix keys; contained keys
};

// The anchors of the families after a pass (a family is its place in ConstructionTable::afterPass):
// those a value index finds; the others by anchor (numbered in ConstructionTable::anchorAtoms), of
// atoms that read only what no action changes (slots) and of others (changing), sorted; the families
// with no anchors, which try every word, and with anchors a row may change.
struct CxPassIndex {
    std::vector<CxValueIndex> values;
    std::vector<std::pair<uint32_t, uint32_t>> slots, changing;  // anchor, family
    bool changes = false;  // some anchor is changing or in a value index of a field that is not fixed
    std::vector<uint32_t> open, changers;  // the families with no anchors, and with changing ones
};

struct ConstructionTable {
    std::vector<CxRow> rows;  // in table order; the index is the provenance id
    std::vector<CxFamily> families;
    std::vector<CxNode> nodes;
    std::vector<CxAtom> atoms;
    std::vector<CxMap> maps;
    // The surfaces the families are indexed by, numbered.
    std::unordered_map<std::string, uint32_t, CxSurfaceHash, std::equal_to<>> surfaces;
    uint32_t slots = 0;  // of the atoms
    // The anchors no value index finds (CxPassIndex slots and changing), numbered: an atom of each.
    std::vector<uint32_t> anchorAtoms;
    std::vector<CxPassIndex> passIndex;  // by pass
    std::vector<std::vector<int>> afterPass;  // the families after each pass, by stage
    std::vector<CxError> errors;
};

// Parses the lines of a construction table against the names of the text passes, in order.
// Errors are collected; the rows that parse still load.
ConstructionTable ParseConstructions(std::span<const std::string_view> lines, std::span<const std::string_view> passes);
// Puts the operands of each && and || cheapest first, in the guards and in the rows that place no
// action by @scan: the atoms read nothing that changes while the rows of a family are tried on a
// word, so the order changes no value.
void OrderOperands(ConstructionTable& t);
// The builtin constructions.tsv parsed, its operands ordered.
ConstructionTable ParseBuiltinConstructions();
// The table as bytes and back; ReadConstructions fails on bytes WriteConstructions did not write.
// Word classes are kept by name, so the bytes are read in a process whose builtin classes have them.
std::vector<uint8_t> WriteConstructions(const ConstructionTable& t);
bool ReadConstructions(std::span<const uint8_t> data, ConstructionTable& t);
// WriteConstructions(ParseBuiltinConstructions()) of the build, compiled in by src/cxgen; empty in a
// build that could not run that generator (a cross build).
std::span<const uint8_t> CompiledConstructions();
// ParseBuiltinConstructions as compiled in, or parsed on first use where nothing is compiled in.
const ConstructionTable& BuiltinConstructions();
// RunConstructions over table t instead of the builtin one.
void RunConstructions(Flusher& f, const ConstructionTable& t, std::size_t pass, bool (*skip)(std::string_view name) = nullptr);
// The word after it that repeats it: the same text in the same token, with nothing or a
// pause.copy mark between (いや|いや, 待って、待って); null for none.
const Item* CopyAfter(const Item* it);

}  // namespace internal

}  // namespace hdTrans::word
