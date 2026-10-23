#pragma once
// Word classes: named sets of surfaces that the passes test words against, read from the
// builtin table classes.tsv (docs/dictionary-files.md). The table is parsed once, on first
// use, into an immutable structure shared by every thread.
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hdTrans::dict {

// Whether surface is a member of the class named set; false for a set the table lacks.
bool InClass(std::string_view set, std::string_view surface);
// The members of the class named set in table order; empty for a set the table lacks.
std::span<const std::string_view> ClassMembers(std::string_view set);

namespace internal {

struct WordClass {
    std::vector<std::string_view> members;  // in table order
    std::unordered_set<std::string_view> index;
    std::string_view doc;
};

struct ClassError {
    int line = 0;
    std::string message;
};

// The classes of a table, with views into its lines, which must outlive it.
struct ClassTable {
    std::unordered_map<std::string_view, WordClass> sets;
    std::vector<std::string_view> order;  // set names in the order of their doc lines
    std::vector<ClassError> errors;
};

// Parses the lines of a classes table. Rows are SET <TAB> MEMBER [<TAB> NOTE]; "//= SET <TAB> DOC"
// names a set and says what it is for; blank lines and other "//" lines are comments. A row of a
// set without a doc line, a doc line without rows, a repeated member or doc line, an empty
// field and a member that ends in a space are errors; the rest of the table still loads.
ClassTable ParseClasses(std::span<const std::string_view> lines);
// The parsed builtin classes.tsv.
const ClassTable& BuiltinClasses();

}  // namespace internal

}  // namespace hdTrans::dict
