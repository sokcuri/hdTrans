#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/dict.h"

namespace hdTrans::gui {

// A user dictionary file of the user sources (files, or folders of userdict*.txt/tsv). A file
// turned off carries the suffix .off, which the engine's file pattern does not match.
struct UserDict {
    std::filesystem::path path;  // the name the engine reads, without .off
    bool on = true;
};

// The dictionaries of sources in the engine's order; off files are listed in their place.
std::vector<UserDict> ListUserDicts(const std::vector<std::string>& sources);
std::filesystem::path OffPath(const std::filesystem::path& path);
// Renames the file to or from its off name.
bool SetUserDictOn(UserDict& dict, bool on);
// The first source that is a folder or, missing, not named like a file; empty when there is none.
std::filesystem::path UserDictFolder(const std::vector<std::string>& sources);
// userdict-<name>.txt in folder for a UTF-8 name (kept when it already reads userdict*.txt/tsv);
// empty for a blank name.
std::filesystem::path UserDictPath(const std::filesystem::path& folder, std::string_view name);

// The lines of a dictionary file, without newlines. Entry lines are those that are not
// blank or comments; a column file (a header row starting with key) is not edited here.
struct DictText {
    std::vector<std::string> lines;
    bool bom = false;
    bool crlf = false;
    bool column = false;
};

// A row of a plain dictionary file; note is the text of a trailing // field, which the loader skips.
struct DictRow {
    std::string key{}, ko{}, pos{}, options{}, note{};
};

bool ReadDictText(const std::filesystem::path& path, DictText& text);
// Replaces the file through a temporary file, keeping the BOM and newline style.
bool WriteDictText(const std::filesystem::path& path, const DictText& text);
bool EntryLine(std::string_view line);
DictRow ParseRow(std::string_view line);
// The line of row, its note as a trailing "// note" field. Tabs and newlines in fields become spaces.
std::string RowLine(const DictRow& row);
// The field a row problem is in, and the problem in Korean; an empty message when the row loads.
enum class RowField { Key, Ko, Pos, Options };
struct RowProblem {
    RowField field = RowField::Key;
    std::string message;
};
// Checks row as the user dictionary loader does; base resolves る-verbs as at load, null for none.
RowProblem CheckRow(const DictRow& row, const dict::Dict* base = nullptr);

// Copies source (any encoding ReadTextFile reads) into folder as a UTF-8 user dictionary under a
// free name; returns the new path, empty on failure.
std::filesystem::path ImportUserDict(const std::filesystem::path& source, const std::filesystem::path& folder);

}  // namespace hdTrans::gui
