#pragma once
// The builtin dictionary folder, compiled into the library from src/core/dict/builtin/ and
// loaded ahead of the dictionary folders (see Load): the end rows, register table and
// sentence-final particles (endings.tsv), and for the text path its phrases (phrases.tsv),
// grammar patterns (patterns.tsv), sense table (senses.tsv), whole-line rows (lines.tsv), sound
// lexicon (sound.tsv) and the word classes of the passes (classes.tsv, core/dict/classes.h).
// All but senses.tsv, lines.tsv and classes.tsv are in the plain dictionary format. The extra/
// tables are loaded only where Load asks for them.
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::dict {

// The names of the builtin files, in name order.
std::vector<std::string_view> BuiltinNames();
// The text of the builtin file name ("endings.tsv"); empty when there is none.
std::string BuiltinText(std::string_view name, bool extra = false);
std::string ExtraBuiltinText(std::string_view name);

namespace internal {

// A builtin file as the lines of its text, each with its "\n" but the last.
struct BuiltinTable {
    std::string_view name;
    std::span<const std::string_view> lines;
};

// Sorted by name.
std::span<const BuiltinTable> BuiltinTables();
std::span<const BuiltinTable> ExtraBuiltinFiles();

}  // namespace internal

}  // namespace hdTrans::dict
