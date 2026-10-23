#pragma once
// The rule compiler: one syntax tree for the flat format of src/core/rules/rules.txt and
// the readable syntax of docs/rule-compiler.md, diagnostics with source positions, the
// static checks, and the C++ code of compiled rules.
//
// Both parsers produce a File whose atoms hold the literal bytes the engine sees (the
// surface markers L, R, 11, 22, 00, 99 included), so Lower gives the Program that
// rules::Load builds from the same flat text.
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/rules/rules.h"

namespace hdTrans::rulec {

// The text of one file, or of several files read as one (Join): files holds where each
// starts, and positions are those in the file holding the offset.
struct Source {
    struct File {
        uint32_t start;
        std::string name;
    };
    std::string name;
    std::string text;
    std::vector<uint32_t> lineStarts;
    std::vector<File> files;

    Source(std::string name, std::string text);
    // The file, its 1-based line and column (columns count code points) of an offset.
    void Position(uint32_t offset, int& line, int& column) const;
    const std::string& FileAt(uint32_t offset) const;
    std::string_view Line(uint32_t offset) const;
};

// The rules program of a directory as the build makes it: the header line "rules 1
// LOCALCOUNT GLOBALCOUNT", then the *.txt files in name order. False when a file cannot
// be read.
bool ReadRuleDir(const std::string& dir, std::string& text, std::vector<Source::File>& files, std::string& error);

enum class Severity : uint8_t { Note, Warning, Error };

// A message about bytes [offset, offset + length) of the source. Notes follow the
// diagnostic they belong to. kind, index and statement name the rule ('L' or 'G', the
// record index, the 1-based statement; 0 for none).
struct Diagnostic {
    Severity severity;
    uint32_t offset, length;
    std::string message;
    std::string flag;
    char kind = 0;
    int index = -1;
    int statement = 0;
};

struct Diagnostics {
    std::vector<Diagnostic> list;
    int errors = 0, warnings = 0;

    void Error(uint32_t offset, uint32_t length, std::string message);
    void Warning(std::string flag, uint32_t offset, uint32_t length, std::string message);
    void Note(uint32_t offset, uint32_t length, std::string message);
};

// "file:line:col: warning: message [-Wflag]", the source line and a caret under the span.
std::string Format(const Source& source, const Diagnostic& d);
// One TSV row: kind, index, statement, file, line, column, severity, flag, message.
std::string FormatRow(const Source& source, const Diagnostic& d);

// An expression of a rule body. Atoms have op, delta, rightDelta (word operand) and
// literal; lists have children. offset/length cover the atom or the list token.
struct Expr {
    rules::NodeKind kind = rules::NodeKind::Atom;
    rules::Op op = rules::Op::SrcEq;
    int8_t delta = 0;
    int8_t rightDelta = 0;
    std::string literal;
    uint32_t offset = 0, length = 0;
    std::vector<Expr> children;
};

// A test line of a rule: input is one line translated as a document with the built-in
// dictionaries.
//   Text       the translation is expected
//   Unchanged  the translation is the same with this rule emptied
//   Picks      a word's candidate was chosen by this rule and reads expected
//   Silent     no word's candidate was chosen by this rule
enum class TestKind : uint8_t { Text, Unchanged, Picks, Silent };

struct RuleTest {
    TestKind kind = TestKind::Text;
    std::string input, expected;
    uint32_t offset = 0, length = 0;
};

// Appends s in double quotes: valid UTF-8 as is, '"' and '\\' escaped, control bytes and
// bytes outside UTF-8 sequences as \xHH.
void AppendQuoted(std::string& out, std::string_view s);

// Parses the text after "test" ('"in" -> "out"', '"in" -> unchanged', '"in" picks "ko"',
// '"in" silent') starting at byte offset of the source.
bool ParseTest(std::string_view text, uint32_t offset, Diagnostics& diags, RuleTest& test);
std::string PrintTest(const RuleTest& test);

// A local rule's index is its number; a global rule's is its PRIO. File keeps local rules by
// index and global rules in PRIO order (equal PRIO in file order), as they run.
struct Rule {
    char kind = 'L';
    int index = 0;
    unsigned char pumsa = 0;  // G
    int number = 0;           // G
    bool ugan = false;        // G
    uint32_t offset = 0, length = 0;  // the record (flat) or header line
    Expr body;                        // an Or over the statements
    std::vector<RuleTest> tests;
    std::vector<std::string> comments;  // the comment lines above the record, without "#"
};

struct File {
    std::vector<Rule> local, global;
};

// How an operator reads its operand.
enum class Operand : uint8_t { Surface, Text, Code3, Code2 };

struct OpInfo {
    rules::Op op;
    char field;        // S D P A G U Y M T W
    const char* flat;  // == != = -=
    bool word;         // right operand @d
    Operand operand;
    bool assigns;      // D= and Y=
    const char* name;  // field name in the readable syntax
    const char* keyword;
};

const OpInfo& Info(rules::Op op);
std::span<const OpInfo> Ops();

// A surface pattern (S, D, U, Y tests) split into its test and value, as
// rules::MatchRuleSurface reads it, and the one spelling of each test.
enum class Match : uint8_t { Exact, Prefix, Suffix, Inside };
Match SplitSurface(std::string_view raw, std::string_view& value);
std::string JoinSurface(Match m, std::string_view value);

// The flat format. Errors are those rules::Load reports; parsing resumes at the next line.
File ParseFlat(const Source& source, Diagnostics& diags);
std::string PrintFlat(const File& file);

// The readable syntax.
File ParseReadable(const Source& source, Diagnostics& diags);
std::string PrintReadable(const File& file);

// Drops singleton lists below the root and splices a list into a parent of its kind;
// the result evaluates the same calls in the same order.
void Normalize(Expr& e);
void Normalize(File& file);
// Rewrites surface literals to one spelling per test ("11x" to "Lx", "99x" to "x" when x
// has no marker).
void CanonicalLiterals(File& file);
bool SameTree(const Expr& a, const Expr& b);

// A Program for file: the rules rules::Load builds from its flat text, local rules in index
// order.
rules::Program Lower(const File& file);
// The rules of a program in run order as flat text: local rules by index, then global rules
// by position with their key, number and stem flag (not their PRIO). Two programs with the
// same dump select the same way.
std::string Dump(const rules::Program& program);

// What the dictionaries hold: part codes as the rules see them, semantic and grammar
// codes, and the rows naming each local rule.
struct Inventory {
    bool loaded = false;
    std::set<std::string> partCodes, semCodes, gramCodes;
    std::map<int, int> ruleRows;  // rule number -> rows
    // rule number -> keys of its rows, of word rows and of ending rows (class digit 5)
    std::map<int, std::set<std::string>> ruleKeys, ruleEndingKeys;
};
bool LoadInventory(const std::string& dictDir, Inventory& inventory, std::string& error);

struct CheckOptions {
    int maxDelta = 5;
};
void Check(const File& file, const Inventory& inventory, const CheckOptions& options, Diagnostics& diags);

// C++ source of the local rules in [first, last] and, with globals, of every global rule:
// one function per rule (RuleL3, RuleG0) and the tables of compiled.h. file was parsed from
// the flat source, whose record lines give the hashes.
std::string EmitCpp(const File& file, const Source& source, int first, int last, bool globals);

// C++ source registering each test line of the rules as a unit test of tests/unit.

}  // namespace hdTrans::rulec
