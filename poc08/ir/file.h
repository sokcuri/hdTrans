#pragma once
// UTF-8 file paths on every platform (Windows needs the wide API for non-ASCII paths).
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hd08 {

inline std::FILE* OpenFile(const std::string& path, bool write) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0) return nullptr;
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), n);
    return _wfopen(wide.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

// Appends the whole file to out; false when it cannot be opened or read.
inline bool ReadWholeFile(const std::string& path, std::string& out) {
    std::FILE* f = OpenFile(path, false);
    if (!f) return false;
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

}  // namespace hd08
