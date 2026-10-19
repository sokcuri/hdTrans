#pragma once
// Program data locations. Paths are UTF-8; names are matched without ASCII case.
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::dict {

// Existing file (or directory) ignoring case, otherwise folder/name.
std::string FindChild(std::string_view folder, std::string_view name, bool directory = false);
// programDir/hdTrans when it is a folder, otherwise programDir.
std::string DataFolder(std::string_view programDir);
// The user directory of the data root.
std::string UserFolder(std::string_view programDir);
// Data root's dicts.hdsnap when present, otherwise its dicts folder.
std::string ShippedFolder(std::string_view programDir);
// ShippedFolder(programDir) when it exists, else dicts in the current directory.
std::string DefaultFolder(std::string_view programDir);
// Default user sources; the legacy file precedes the user folder in a flat layout.
std::vector<std::string> UserPaths(std::string_view programDir);
// Expands folders to userdict*.txt/tsv files in lowercase filename byte order.
// Explicit files keep their positions; empty paths are ignored.
std::vector<std::string> UserFiles(const std::vector<std::string>& paths);
std::string ConfigFile(std::string_view programDir);
// programDir/Ehnd when present, outside the hdTrans data folder.
std::string EhndFolder(std::string_view programDir);

}  // namespace hdTrans::dict
