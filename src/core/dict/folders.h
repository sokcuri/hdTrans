#pragma once
// Program data locations. Paths are UTF-8; names are matched without ASCII case.
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::dict {

// Existing file (or directory) ignoring case, otherwise folder/name.
std::string FindChild(std::string_view folder, std::string_view name, bool directory = false,
                      std::vector<std::string>* warnings = nullptr);
// The selected or expected hdtrans-data directory next to the program.
std::string DataFolder(std::string_view programDir, std::vector<std::string>* warnings = nullptr);
// Ambiguous case variants of the data, user and ehnd directories.
std::vector<std::string> FolderWarnings(std::string_view programDir);
// The user directory of the data root.
std::string UserFolder(std::string_view programDir);
// The selected or expected dicts.hdsnap in the data root.
std::string ShippedFolder(std::string_view programDir);
// The default snapshot path, including when it does not exist.
std::string DefaultFolder(std::string_view programDir);
// The optional user directory in the data root.
std::vector<std::string> UserPaths(std::string_view programDir);
// A requested path's provenance and the availability found by the last observation.
struct UserSource {
    std::string path;
    bool directory = false;
    bool available = false;  // a file exists, or a folder was completely enumerated
    bool explicitPath = true;
    bool operator==(const UserSource&) const = default;
};

// Expands folders to top-level userdict*.txt/tsv and .txt/tsv files whose first line
// is a prefilter/postfilter header, in lowercase filename byte order.
// Explicit files keep their positions; empty paths are ignored. The name lists of the
// folders, their top-level names*.txt/tsv files, go to names in the same order.
std::vector<std::string> UserFiles(const std::vector<std::string>& paths);
std::vector<std::string> UserFiles(const std::vector<std::string>& paths, std::vector<UserSource>& sources);
std::vector<std::string> UserFiles(const std::vector<UserSource>& paths, std::vector<UserSource>& sources,
                                   std::vector<std::string>* readFailures = nullptr,
                                   std::vector<std::string>* names = nullptr);
// Reads a whole file; false when it cannot be read.
bool ReadUserFile(std::string_view utf8, std::string& out);
std::string ConfigFile(std::string_view programDir);
// The selected or expected optional ehnd directory in the data root.
std::string EhndFolder(std::string_view programDir);

}  // namespace hdTrans::dict
