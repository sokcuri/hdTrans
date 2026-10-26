#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::gui {

enum class TextEncoding { Utf8, Utf8Bom, Utf16Le, ShiftJis };

// BOMs take precedence over UTF-8 and CP932. Decoded text has LF newlines and no NULs.
// lineBreak receives the source's first line break ("\r\n", "\r" or "\n"), or "\n" when it has none.
bool DecodeText(std::string_view bytes, std::string& utf8, TextEncoding& encoding,
                std::string_view* lineBreak = nullptr);
bool ReadTextFile(const std::filesystem::path& path, std::string& utf8, TextEncoding& encoding,
                  std::string_view* lineBreak = nullptr);
// Writes validated UTF-8 with a BOM, with each LF written as lineBreak and other bytes unchanged.
bool WriteTextFile(const std::filesystem::path& path, std::string_view utf8, std::string_view lineBreak = "\n");
// <stem>_번역.txt in folder, or next to the input when folder is empty.
std::filesystem::path TranslatedPath(const std::filesystem::path& input, const std::filesystem::path& folder);
// Appends the paths not yet in list; on Windows paths compare without case.
void AppendFiles(std::vector<std::filesystem::path>& list, const std::vector<std::filesystem::path>& added);

}  // namespace hdTrans::gui
