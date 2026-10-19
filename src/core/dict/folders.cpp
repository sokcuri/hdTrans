#include "core/dict/folders.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "core/dict/runes.h"

namespace hdTrans::dict {
namespace {

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string Lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

}  // namespace

std::string FindChild(std::string_view folder, std::string_view name, bool directory) {
    const auto parent = PathOf(folder);
    const auto wanted = Lower(std::string(name));
    std::string matched;
    std::error_code error;
    std::filesystem::directory_iterator entries(folder.empty() ? std::filesystem::path(".") : parent, error), end;
    while (!error && entries != end) {
        const auto child = entries->path();
        const auto filename = Utf8(child.filename());
        std::error_code kindError;
        const bool kind = directory ? entries->is_directory(kindError) : entries->is_regular_file(kindError);
        if (!kindError && kind) {
            if (filename == name) return Utf8(child);
            const auto candidate = Utf8(child);
            if (Lower(filename) == wanted && (matched.empty() || candidate < matched)) matched = candidate;
        }
        entries.increment(error);
    }
    return matched.empty() ? Utf8(parent / PathOf(name)) : matched;
}

std::string DataFolder(std::string_view programDir) {
    if (programDir.empty()) return {};
    const auto folder = FindChild(programDir, "hdTrans", true);
    std::error_code error;
    return std::filesystem::is_directory(PathOf(folder), error) ? folder : std::string(programDir);
}

std::string UserFolder(std::string_view programDir) {
    return FindChild(DataFolder(programDir), "user", true);
}

std::string ShippedFolder(std::string_view programDir) {
    const auto data = DataFolder(programDir);
    const auto snapshot = FindChild(data, "dicts.hdsnap");
    std::error_code error;
    if (std::filesystem::is_regular_file(PathOf(snapshot), error)) return snapshot;
    return FindChild(data, "dicts", true);
}

std::string DefaultFolder(std::string_view programDir) {
    if (programDir.empty()) return "dicts";
    const auto folder = ShippedFolder(programDir);
    std::error_code error;
    if (std::filesystem::is_regular_file(PathOf(folder), error)) return folder;
    return std::filesystem::is_directory(PathOf(folder), error) ? folder : "dicts";
}

namespace {

std::string UserPath(std::string_view programDir, std::string_view name) {
    const auto path = FindChild(UserFolder(programDir), name);
    std::error_code error;
    if (std::filesystem::is_regular_file(PathOf(path), error)) return path;
    if (DataFolder(programDir) == programDir) {
        const auto legacy = FindChild(programDir, name);
        if (std::filesystem::is_regular_file(PathOf(legacy), error)) return legacy;
    }
    return path;
}

}  // namespace

std::vector<std::string> UserFiles(const std::vector<std::string>& paths) {
    std::vector<std::string> files;
    for (const std::string& path : paths) {
        if (path.empty()) continue;
        if (path.find('\0') != std::string::npos || !internal::ValidUtf8(path)) {
            files.push_back(path);
            continue;
        }
        std::error_code error;
        if (!std::filesystem::is_directory(PathOf(path), error)) {
            error.clear();
            if (std::filesystem::exists(PathOf(path), error) || error) files.push_back(path);
            continue;
        }
        std::vector<std::string> names;
        std::filesystem::directory_iterator entries(PathOf(path), error), end;
        while (!error && entries != end) {
            const auto filename = Utf8(entries->path().filename());
            const auto lower = Lower(filename);
            std::error_code kindError;
            if (lower.starts_with("userdict") && (lower.ends_with(".txt") || lower.ends_with(".tsv")) &&
                entries->is_regular_file(kindError) && !kindError) names.push_back(filename);
            entries.increment(error);
        }
        std::sort(names.begin(), names.end(), [](const std::string& left, const std::string& right) {
            const auto lowerLeft = Lower(left), lowerRight = Lower(right);
            return lowerLeft == lowerRight ? left < right : lowerLeft < lowerRight;
        });
        for (const std::string& name : names) files.push_back(Utf8(PathOf(path) / PathOf(name)));
    }
    return files;
}

std::vector<std::string> UserPaths(std::string_view programDir) {
    if (programDir.empty()) return {};
    std::vector<std::string> paths{UserFolder(programDir)};
    if (DataFolder(programDir) == programDir) {
        const auto legacy = FindChild(programDir, "userdict.tsv");
        std::error_code error;
        if (std::filesystem::is_regular_file(PathOf(legacy), error)) paths.insert(paths.begin(), legacy);
    }
    return paths;
}

std::string ConfigFile(std::string_view programDir) { return UserPath(programDir, "hdtrans.config.yaml"); }

std::string EhndFolder(std::string_view programDir) {
    if (programDir.empty()) return {};
    const auto folder = FindChild(programDir, "Ehnd", true);
    std::error_code error;
    return std::filesystem::is_directory(PathOf(folder), error) ? folder : std::string();
}

}  // namespace hdTrans::dict
