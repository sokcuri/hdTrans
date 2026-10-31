#include "core/dict/folders.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <utility>

#include "core/dict/runes.h"
#include "core/dict/mapped_file.h"
#include "core/dict/snapshot.h"

namespace hdTrans::dict {
namespace {

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

bool UserFilterHeader(const std::filesystem::path& path, bool& filter) {
    filter = false;
    std::FILE* file = internal::OpenShared(path);
    if (!file) return false;
    std::string first;
    bool ok = true;
    char buffer[4096];
    for (;;) {
        const auto n = std::fread(buffer, 1, sizeof buffer, file);
        if (std::ferror(file)) { ok = false; break; }
        const std::string_view bytes(buffer, n);
        const auto newline = bytes.find('\n');
        first.append(bytes.substr(0, newline));
        if (newline != std::string_view::npos || n < sizeof buffer) break;
    }
    std::fclose(file);
    FileLayer layer;
    if (ok && internal::ReadLayer(first, layer)) {
        filter = layer == FileLayer::PreFilter || layer == FileLayer::PostFilter;
    }
    return ok;
}

std::string Lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

}  // namespace

std::string FindChild(std::string_view folder, std::string_view name, bool directory,
                      std::vector<std::string>* warnings) {
    const auto parent = PathOf(folder);
    const auto wanted = Lower(std::string(name));
    std::string matched, exact;
    std::vector<std::string> names;
    std::error_code error;
    std::filesystem::directory_iterator entries(folder.empty() ? std::filesystem::path(".") : parent, error), end;
    while (!error && entries != end) {
        const auto child = entries->path();
        const auto filename = Utf8(child.filename());
        std::error_code kindError;
        const bool kind = directory ? entries->is_directory(kindError) : entries->is_regular_file(kindError);
        if (!kindError && kind && Lower(filename) == wanted) {
            const auto candidate = Utf8(child);
            if (filename == name) {
                if (!warnings) return candidate;
                exact = candidate;
            }
            if (matched.empty() || candidate < matched) matched = candidate;
            if (warnings && directory) names.push_back(filename);
        }
        entries.increment(error);
    }
    const auto selected = !exact.empty() ? exact : matched.empty() ? Utf8(parent / PathOf(name)) : matched;
    if (warnings && names.size() > 1) {
        std::sort(names.begin(), names.end());
        std::string listed;
        for (const auto& filename : names) listed += (listed.empty() ? "" : ", ") + filename;
        warnings->push_back(Utf8(parent) + ":0: " + std::string(name) + " 폴더가 여러 개 있습니다 (" +
                            listed + "). " + Utf8(PathOf(selected).filename()) + "를 씁니다");
    }
    return selected;
}

std::string DataFolder(std::string_view programDir, std::vector<std::string>* warnings) {
    return FindChild(programDir, "hdtrans-data", true, warnings);
}

std::vector<std::string> FolderWarnings(std::string_view programDir) {
    std::vector<std::string> warnings;
    if (programDir.empty()) return warnings;
    const auto data = DataFolder(programDir, &warnings);
    FindChild(data, "user", true, &warnings);
    FindChild(data, "ehnd", true, &warnings);
    return warnings;
}

std::string UserFolder(std::string_view programDir) {
    return FindChild(DataFolder(programDir), "user", true);
}

std::string ShippedFolder(std::string_view programDir) {
    return FindChild(DataFolder(programDir), "dicts.hdsnap");
}

std::string DefaultFolder(std::string_view programDir) {
    return ShippedFolder(programDir);
}

namespace {

std::string UserPath(std::string_view programDir, std::string_view name) {
    return FindChild(UserFolder(programDir), name);
}

}  // namespace

std::vector<std::string> UserFiles(const std::vector<std::string>& paths) {
    std::vector<UserSource> sources;
    return UserFiles(paths, sources);
}

std::vector<std::string> UserFiles(const std::vector<std::string>& paths, std::vector<UserSource>& sources) {
    std::vector<UserSource> requested;
    for (const auto& path : paths) requested.push_back({path});
    return UserFiles(requested, sources);
}

std::vector<std::string> UserFiles(const std::vector<UserSource>& paths, std::vector<UserSource>& sources,
                                   std::vector<std::string>* readFailures, std::vector<std::string>* nameFiles) {
    std::vector<std::string> files;
    for (const auto& input : paths) {
        const std::string& path = input.path;
        if (path.empty()) continue;
        UserSource source{path};
        source.explicitPath = input.explicitPath;
        if (path.find('\0') != std::string::npos || !internal::ValidUtf8(path)) {
            sources.push_back(source);
            files.push_back(path);
            continue;
        }
        std::error_code error;
        source.directory = std::filesystem::is_directory(PathOf(path), error);
        if (!source.directory) {
            error.clear();
            source.available = std::filesystem::is_regular_file(PathOf(path), error);
            if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory) error.clear();
            if (source.available || error) files.push_back(path);
            sources.push_back(source);
            continue;
        }
        std::vector<std::string> names, lists;
        std::filesystem::directory_iterator entries(PathOf(path), error), end;
        while (!error && entries != end) {
            const auto filename = Utf8(entries->path().filename());
            const auto lower = Lower(filename);
            if (lower.ends_with(".txt") || lower.ends_with(".tsv")) {
                std::error_code kindError;
                const bool regular = entries->is_regular_file(kindError);
                if (regular && lower.starts_with("names")) {
                    lists.push_back(filename);
                } else if (regular) {
                    bool selected = lower.starts_with("userdict");
                    if (!selected && !UserFilterHeader(entries->path(), selected)) {
                        selected = true;
                        if (readFailures) readFailures->push_back(Utf8(entries->path()));
                    }
                    if (selected) names.push_back(filename);
                }
                if (kindError) error = kindError;
            }
            if (!error) entries.increment(error);
        }
        source.available = !error;
        sources.push_back(source);
        if (!source.available) continue;
        const auto byLower = [](const std::string& left, const std::string& right) {
            const auto lowerLeft = Lower(left), lowerRight = Lower(right);
            return lowerLeft == lowerRight ? left < right : lowerLeft < lowerRight;
        };
        std::sort(names.begin(), names.end(), byLower);
        for (const std::string& name : names) files.push_back(Utf8(PathOf(path) / PathOf(name)));
        if (nameFiles) {
            std::sort(lists.begin(), lists.end(), byLower);
            for (const std::string& name : lists) nameFiles->push_back(Utf8(PathOf(path) / PathOf(name)));
        }
    }
    return files;
}

bool ReadUserFile(std::string_view utf8, std::string& out) {
    std::FILE* file = internal::OpenShared(PathOf(utf8));
    if (!file) return false;
    std::string text;
    char buffer[1 << 14];
    bool ok = true;
    for (;;) {
        const auto n = std::fread(buffer, 1, sizeof buffer, file);
        text.append(buffer, n);
        if (std::ferror(file)) { ok = false; break; }
        if (n < sizeof buffer) break;
    }
    std::fclose(file);
    if (ok) out = std::move(text);
    return ok;
}

std::vector<std::string> UserPaths(std::string_view programDir) {
    if (programDir.empty()) return {};
    return {UserFolder(programDir)};
}

std::string ConfigFile(std::string_view programDir) { return UserPath(programDir, "hdtrans.config.yaml"); }

std::string EhndFolder(std::string_view programDir) {
    if (programDir.empty()) return {};
    return FindChild(DataFolder(programDir), "ehnd", true);
}

}  // namespace hdTrans::dict
