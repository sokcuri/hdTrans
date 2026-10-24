#pragma once
// Candidate-selection rules as data. A Program holds the local rules (indexed by the
// rule column of the dictionary) and the global rules (in dispatch order), loaded
// from the flat rule format, and is evaluated against an Item.
//
// Flat rule format: UTF-8 text, one record per line; empty lines and lines starting
// with '#' are skipped. The first record is the header, then the local and global rules
// in any order:
//
//   rules 1 LOCALCOUNT GLOBALCOUNT
//   L INDEX BODY
//   G PRIO PUMSA NUMBER UGAN BODY
//
// Local rules may come in any order; each INDEX in [0, LOCALCOUNT) occurs once. Global
// rules run in ascending PRIO, equal PRIO in file order. PUMSA is the one-byte pumsa key,
// NUMBER the rule number, UGAN 1 for a stem rule and 0 for an ending rule. BODY is a prefix expression of tokens separated by one
// space:
//
//   ||N E1 .. EN   short-circuit OR; the root of every body is ||N over the rule's
//                  statements in order (||0 for an empty rule)
//   &&N E1 .. EN   short-circuit AND
//   &N E1 .. EN    AND that evaluates every operand
//   FdOP ARG       atom: field F at delta d (+0, -1, ...), OP one of == != = -=;
//                  ARG is a string literal or @d, the same field at another delta
//
// String literals are bytes in double quotes; \\, \" and \xHH are the only escapes.
//
// Fields: S source surface, D target text, P pumsa code, A semantic attributes,
// G grammar attributes, and for the ending (gobi) U source, Y target, M pumsa,
// T attributes, W grammar attributes. Atoms on D and Y with = assign the target
// text as they are evaluated; the word stage takes the writes back when the rule is
// false (word::Journal).
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::rules {

// A word item as the rules see it.
//
// di selects a candidate; -1 means none is fixed, and the Check methods then look
// at the enabled candidates. GetCurrentCompDI and GetCurrentCompUmiDI return the
// candidate under consideration for the base word and for its ending.
//
// GetObject(delta) returns the item delta positions away (delta != 0, negative is
// leftwards), skipping parenthesised groups and, rightwards from a ruby base, its
// reading, and resets that item's
// GetCurrentCompDI and GetCurrentCompUmiDI to -1; nullptr when there is none. The
// wi argument of the LL methods is nullptr in that case too.
class Item {
public:
    // number (a local rule index or a global rule PRIO) is -1 when evaluation ends.
    virtual void SetRuleContext(bool, int) {}
    // Called around the body of each rule, EndRule with whether the rule fired. An
    // assignment takes effect as it is evaluated, so a word stage that records them
    // takes back the writes of a rule that did not fire here.
    virtual void BeginRule() {}
    virtual void EndRule(bool) {}
    virtual Item* GetObject(int delta) = 0;
    virtual int GetCurrentCompDI() = 0;
    virtual int GetCurrentCompUmiDI() = 0;

    virtual bool CheckSrc(std::string_view s) = 0;
    virtual bool CheckLLSrc(Item* wi) = 0;
    virtual bool CheckDst(std::string_view s, int di) = 0;
    virtual bool AssignDst(int di, std::string_view s) = 0;
    virtual bool AssignLLDst(Item* wi, int di) = 0;
    virtual bool CheckPumsa(std::string_view s, int di) = 0;
    virtual bool CheckAttr(std::string_view s, int di) = 0;
    virtual bool MinAttrOperation(int di, std::string_view s) = 0;
    virtual bool CheckLLAttr(int op, Item* wi, int di) = 0;
    virtual bool CheckGAttr(std::string_view s, int di) = 0;
    virtual bool MinGAttrOperation(int di, std::string_view s) = 0;

    virtual bool CheckGobiSrc(std::string_view s) = 0;
    virtual bool CheckGobiDst(std::string_view s, int di) = 0;
    virtual bool AssignGobiDst(int di, std::string_view s) = 0;
    virtual bool AssignLLGobiDst(Item* wi, int di) = 0;
    virtual bool CheckGobiPumsa(std::string_view s, int di) = 0;
    virtual bool CheckGobiAttr(std::string_view s, int di) = 0;
    virtual bool MinGobiAttrOperation(int di, std::string_view s) = 0;
    virtual bool CheckGobiGAttr(std::string_view s, int di) = 0;

protected:
    ~Item() = default;
};

// The CheckLLAttr operation of A -= @d.
constexpr int kOpMinSubAttr = 6;

// The rule operators: field, comparison, and W when the right operand is a word.
// Each is false when its word does not exist, the != forms included.
enum class Op : uint8_t {
    SrcEq, SrcNe, SrcEqW,
    DstEq, DstNe, DstSet, DstSetW,
    PumsaEq, PumsaNe,
    AttrEq, AttrNe, AttrMinusEq, AttrMinusEqW,
    GramEq, GramNe, GramMinusEq,
    GobiSrcEq, GobiSrcNe,
    GobiDstEq, GobiDstSet, GobiDstSetW,
    GobiPumsaEq,
    GobiAttrEq, GobiAttrMinusEq,
    GobiGramEq, GobiGramNe,
};

enum class NodeKind : uint8_t { Atom, Or, And, AndAll };

// The nodes of a rule body in prefix order: a list node is followed by the
// subtrees of its children.
struct Node {
    NodeKind kind;
    Op op;              // atoms
    int8_t delta;       // atoms: the left word
    int8_t rightDelta;  // atoms whose right operand is a word
    uint32_t first;     // atoms: literal offset in Program::literals; lists: child count
    uint32_t second;    // atoms: literal length; lists: index one past the subtree
};

struct GlobalRule {
    unsigned char pumsa;
    int number;
    bool uganRule;
    uint32_t root;
    int prio = 0;
};

struct Program {
    std::vector<Node> nodes;
    std::string literals;
    std::vector<uint32_t> localRules;  // root node of each local rule
    std::vector<GlobalRule> globalRules;  // in PRIO order
    // Native code of rules (src/core/rules/compiled.h), by the index of localRules and
    // globalRules; a rule without code (shorter vector or null) evaluates its nodes. The
    // code of a rule makes the calls its nodes make.
    std::vector<bool (*)(Item*)> localCode, globalCode;
};

// How often each rule ran and how often it was true: local rules by index, global rules by
// their position in Program::globalRules. Not synchronised; one evaluating thread at a time.
struct Tally {
    std::vector<uint64_t> localRuns, localFires, globalRuns, globalFires;
    // The rows of the construction table (core/word/constructions.h) by their index, sized by the
    // first run: runs counts the items a row was tried on.
    std::vector<uint64_t> constructionRuns, constructionFires;

    explicit Tally(const Program& program)
        : localRuns(program.localRules.size()), localFires(program.localRules.size()),
          globalRuns(program.globalRules.size()), globalFires(program.globalRules.size()) {}
};

struct LoadError {
    int line;  // 1-based
    const char* message;
};

// Parses text in the flat rule format. On failure returns false, sets error, and
// program is unspecified.
bool Load(std::string_view text, Program& program, LoadError& error);

// Evaluates the rule whose body starts at node root. Evaluation does not allocate.
bool RunRule(const Program& program, uint32_t root, Item* pwi);

// Runs local rule ruleIndex on pwi; out-of-range indices are false. A tally, when given,
// counts the run.
bool RunLocal(const Program& program, Item* pwi, int ruleIndex, Tally* tally = nullptr);

// Tries, in order, every global rule whose uganRule flag and pumsa key match and
// reports whether one fired. Stem rules are restricted by number: 1000 < number <
// 5000, or 0 < number < 1000 when last is set.
// fired receives the PRIO of the rule that fired, or -1 when none did.
bool RunGlobal(const Program& program, Item* pwi, bool uganRule, unsigned char pumsa, bool last, int* fired = nullptr,
               Tally* tally = nullptr);

}  // namespace hdTrans::rules
