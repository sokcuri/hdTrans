#include "frontend/dictionary_version.h"

#include <filesystem>
#include <fstream>

namespace hdTrans::frontend {
namespace {

std::string ReadVersion(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::string line;
    if (!std::getline(file, line)) return {};
    if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
    if (line.ends_with('\r')) line.pop_back();
    return line;
}

}  // namespace

std::string DictionaryVersion(const std::vector<std::string>& directories) {
    constexpr const char* unversioned = "development (unversioned)";
    if (directories.empty()) return unversioned;
    const auto& utf8 = directories.front();
    std::filesystem::path base(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
    if (base.filename().empty()) base = base.parent_path();
    auto extension = base.extension().u8string();
    for (char8_t& c : extension) {
        if (c >= u8'A' && c <= u8'Z') c = static_cast<char8_t>(c - u8'A' + u8'a');
    }
    if (extension != u8".hdsnap") {
        const std::string version = ReadVersion(base / "dicts.txt");
        if (!version.empty()) return version;
    }
    const std::string version = ReadVersion(base.parent_path() / "dicts.txt");
    return version.empty() ? unversioned : version;
}

}  // namespace hdTrans::frontend
