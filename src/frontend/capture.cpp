#include "frontend/capture.h"

#include <filesystem>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>

#include <vector>
#endif

#include "core/dict/folders.h"

namespace hdTrans::frontend {

namespace {

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::FILE* OpenFile(const std::filesystem::path& path, bool append) {
#if defined(_WIN32)
    return _wfopen(path.c_str(), append ? L"ab" : L"rb");
#else
    return std::fopen(path.c_str(), append ? "ab" : "rb");
#endif
}

std::uint64_t Hash(std::string_view s) {
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::string_view Trim(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return s;
}

bool HasNonAscii(std::string_view s) {
    for (const char c : s) {
        if (static_cast<unsigned char>(c) >= 0x80) return true;
    }
    return false;
}

}  // namespace

std::string ProgramName() {
    std::filesystem::path image;
#if defined(_WIN32)
    std::wstring path(260, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    image = std::filesystem::path(path);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size + 1, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
    image = std::filesystem::path(path.data());
#elif defined(__linux__)
    std::error_code error;
    image = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return {};
#else
    return {};
#endif
    return Utf8(image.stem());
}

std::string CapturePath(std::string_view programDir) {
    std::string name = ProgramName();
    if (name.empty()) name = "host";
    return Utf8(PathOf(dict::UserFolder(programDir)) / "capture" / PathOf(name + ".txt"));
}

Capture::Capture(std::string path, std::size_t maxBytes, std::size_t maxLines)
    : path_(std::move(path)), maxBytes_(maxBytes), maxLines_(maxLines) {
    std::FILE* in = OpenFile(PathOf(path_), false);
    if (in == nullptr) return;
    std::string text;
    char buffer[1 << 14];
    for (;;) {
        const std::size_t n = std::fread(buffer, 1, sizeof buffer, in);
        text.append(buffer, n);
        if (n < sizeof buffer || text.size() > maxBytes_) break;
    }
    std::fclose(in);
    bytes_ = text.size();
    for (std::string_view rest = text; !rest.empty() && seen_.size() < maxLines_;) {
        const std::size_t nl = rest.find('\n');
        if (const std::string_view line = Trim(rest.substr(0, nl)); !line.empty()) seen_.insert(Hash(line));
        rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
    }
    if (bytes_ >= maxBytes_ || seen_.size() >= maxLines_) stopped_ = true;
}

Capture::~Capture() {
    if (file_ != nullptr) std::fclose(file_);
}

bool Capture::Open() {
    if (file_ != nullptr) return true;
    const std::filesystem::path path = PathOf(path_);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    file_ = OpenFile(path, true);
    return file_ != nullptr;
}

void Capture::Stop() {
    stopped_ = true;
    if (file_ != nullptr) std::fclose(file_);
    file_ = nullptr;
}

void Capture::Add(std::string_view text) {
    const std::lock_guard<std::mutex> lock(mutex_);
    while (!stopped_ && !text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view line = Trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        if (line.empty() || !HasNonAscii(line)) continue;
        const std::uint64_t hash = Hash(line);
        if (seen_.contains(hash)) continue;
        if (bytes_ + line.size() + 1 > maxBytes_ || seen_.size() >= maxLines_ || !Open()) {
            Stop();
            return;
        }
        std::string record(line);
        record += '\n';
        // One write per line, so a host that ends the process loses at most the line in flight.
        if (std::fwrite(record.data(), 1, record.size(), file_) != record.size() || std::fflush(file_) != 0) {
            Stop();
            return;
        }
        bytes_ += record.size();
        seen_.insert(hash);
    }
}

std::unique_ptr<Capture> OpenCapture(bool on, std::string_view programDir) {
    return on ? std::make_unique<Capture>(CapturePath(programDir)) : nullptr;
}

void ReplaceCapture(std::unique_ptr<Capture>& current, std::unique_ptr<Capture> next) {
    if (!next || !current || next->Path() != current->Path()) current = std::move(next);
}

}  // namespace hdTrans::frontend
