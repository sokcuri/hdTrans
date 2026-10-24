#pragma once
// The construction table of the rewrite layer, constructions.tsv (docs/rule-language.md, 구문 표):
// rows that pick the Korean of a word from the words around it, written in the body syntax of the
// rules with fields of the resolved chain, and grouped in families that run between the sentence
// passes of Flush. The builtin table is parsed once, on first use, into an immutable structure
// shared by every thread.
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

// Runs the family anchored after the text pass at index pass of TextPassNames, then the family
// anchored after it, and so on. skip, when given, leaves out the families and rows it names.
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

struct CxAtom {
    CxField field = CxField::Surface;
    CxMatch match = CxMatch::Literal;
    int8_t delta = 0;
    bool negated = false;
    std::string value;
    const dict::internal::WordClass* set = nullptr;
    CxFeature feature = nullptr;
};

struct CxNode {
    rules::BodyKind kind = rules::BodyKind::Or;
    bool atom = false;
    uint32_t count = 0;  // lists: operands; scans: the bound, 0 for none; atoms: index into atoms
    uint32_t end = 0;    // one past the subtree
};

enum class CxVerb : uint8_t {
    SelectLemma, SelectTarget, TakeTarget, SetTarget, AppendTarget, SetEnding, ReplaceHead, ReplaceEndingTail, MapLemma, MapTarget, SelectMap,
    SetVerb, CopyEnding, CopyWord, SetSound, SetActive,
};

struct CxAction {
    CxVerb verb = CxVerb::SelectLemma;
    int8_t delta = 0;
    std::string value, with;  // with: the new head of replace-head, the new tail of replace-ending-tail
    int map = -1;             // map-lemma, map-target, select-map: into maps
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
    int line = 0;  // in the table
};

struct CxSurfaceHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

struct CxFamily {
    std::string name;
    std::string after;
    std::vector<int> rows;  // into rows, by PRIO
    int next = -1;          // the family anchored after this one
    int line = 0;           // of its //= line
    // The rows worth trying on a word, by PRIO. A row whose condition needs the word to be one of a
    // few surfaces (S+0== "で", S+0== @set) is listed under each of them; the other rows (open) are
    // listed under every surface. A word whose surface is no key tries the open rows only.
    std::unordered_map<std::string, std::vector<int>, CxSurfaceHash, std::equal_to<>> bySurface;
    std::vector<int> open;
    bool indexed = false;  // some row has surfaces
};

struct CxError {
    int line = 0;
    std::string message;
};

struct ConstructionTable {
    std::vector<CxRow> rows;  // in table order; the index is the provenance id
    std::vector<CxFamily> families;
    std::vector<CxNode> nodes;
    std::vector<CxAtom> atoms;
    std::vector<CxMap> maps;
    std::vector<int> afterPass;  // the family anchored after each pass, -1 for none
    std::vector<CxError> errors;
};

// Parses the lines of a construction table against the names of the text passes, in order.
// Errors are collected; the rows that parse still load.
ConstructionTable ParseConstructions(std::span<const std::string_view> lines, std::span<const std::string_view> passes);
// The parsed builtin constructions.tsv.
const ConstructionTable& BuiltinConstructions();
// The word after it that repeats it: the same text in the same token, with nothing or a
// pause.copy mark between (いや|いや, 待って、待って); null for none.
const Item* CopyAfter(const Item* it);

}  // namespace internal

}  // namespace hdTrans::word
