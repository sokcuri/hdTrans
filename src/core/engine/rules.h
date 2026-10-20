#pragma once
// The rules program of the word stage, compiled in from the *.txt files of src/core/rules.
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "core/rules/rules.h"

namespace hdTrans::internal {

// The rules program (generated at build time): a header line, then the lines of the *.txt
// files of src/core/rules in name order, each ending in "\n".
std::span<const std::string_view> RuleLines();

// A rule file: its name and its lines in RuleLines.
struct RuleFile {
    const char* name;
    std::size_t first, count;
};
std::span<const RuleFile> RuleFiles();

// "02-part.txt:L925" or "01-surface.txt:G30" for a local rule index or a global rule PRIO;
// empty when there is no such rule.
std::string RuleSource(char kind, int id);

// Gives program the compiled code (compiled_gen.cpp) of each rule whose record line in
// text matches the one the code was generated from. text holds L and G record lines.
void AttachCompiled(rules::Program& program, std::string_view text);

// The loaded program, with the compiled code (compiled_gen.cpp) of the rules whose record
// lines it was generated from; loaded on first use and immutable afterwards.
const rules::Program& RulesProgram();
// Whether RulesProgram loaded.
bool RulesLoaded();

}  // namespace hdTrans::internal
