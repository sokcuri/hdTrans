#pragma once
// Word classes: named sets of surfaces that the passes test words against, read from the
// builtin table classes.tsv (docs/dictionary-files.md). The table is parsed once, on first
// use, into an immutable structure shared by every thread.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "classes_gen.h"

namespace hdTrans::dict {

namespace internal {

// FNV-1a of a class name.
constexpr std::uint64_t ClassHash(std::string_view s) {
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : s) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    return h;
}

constexpr bool KnownClass(std::string_view s) {
    for (const std::string_view n : kClassNames) {
        if (n == s) return true;
    }
    return false;
}

// Not constexpr: a name that classes.tsv lacks reaches it while the program is compiled, which
// fails the compilation.
void UnknownClassName();

struct WordClass;

}  // namespace internal

// The name of a word class: a string literal of a set of classes.tsv, checked when the program is
// compiled.
struct ClassName {
    std::string_view name;
    consteval ClassName(const char* s) : name(s) {
        if (!internal::KnownClass(name)) internal::UnknownClassName();
    }
};

// A word class as the code names it: an id the process gives a name on its first use, in the order
// of first use and never reused, with the class of the builtin table of that name (null for none,
// which has no members).
struct ClassId {
    std::uint32_t id = 0;
    const internal::WordClass* data = nullptr;
    // Thread-safe.
    static ClassId Of(std::string_view name);
};

// The ClassId of the set of classes.tsv that the string literal names, found once for the call
// site.
#define HDTRANS_CLASS(literal)                                                                        \
    ([]() -> ::hdTrans::dict::ClassId {                                                               \
        static const ::hdTrans::dict::ClassId id =                                                    \
            ::hdTrans::dict::ClassId::Of(::hdTrans::dict::ClassName(literal).name);                   \
        return id;                                                                                    \
    }())

// Whether surface is a member of class set.
bool InClass(ClassId set, std::string_view surface);
// InClass for a class named at run time; HDTRANS_CLASS finds a literal name once.
bool InClass(std::string_view set, std::string_view surface);
// The members of class set in table order.
std::span<const std::string_view> ClassMembers(ClassId set);

namespace internal {

struct WordClass {
    std::string_view name;
    std::string_view doc;
    std::vector<std::string_view> members;  // in table order
    std::unordered_set<std::string_view> index;
};

struct ClassError {
    int line = 0;
    std::string message;
};

// The classes of a table, with views into its lines, which must outlive it.
struct ClassTable {
    std::vector<WordClass> classes;  // in the order of their doc lines
    std::vector<ClassError> errors;
    // Open addressing over the ClassHash of the names: {hash, index into classes}, -1 for an
    // empty slot; the size is a power of two.
    std::vector<std::pair<std::uint64_t, int>> slots;
};

// Parses the lines of a classes table. Rows are SET <TAB> MEMBER [<TAB> NOTE]; "//= SET <TAB> DOC"
// names a set and says what it is for; blank lines and other "//" lines are comments. A row of a
// set without a doc line, a doc line without rows, a repeated member or doc line, an empty
// field and a member that ends in a space are errors; the rest of the table still loads.
ClassTable ParseClasses(std::span<const std::string_view> lines);
// The class of t named name, whose ClassHash is hash; null for none.
const WordClass* FindClass(const ClassTable& t, std::string_view name, std::uint64_t hash);
// The parsed builtin classes.tsv.
const ClassTable& BuiltinClasses();

}  // namespace internal

}  // namespace hdTrans::dict
