#pragma once
// The capture of the lines a host sends to a DLL (settings key capture): each distinct line, in
// the order first seen, appended to a UTF-8 file that hdtrans --propose-names reads. Translations
// never read it.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace hdTrans::frontend {

inline constexpr std::size_t kCaptureBytes = std::size_t{8} << 20;  // a file stops growing at this size
inline constexpr std::size_t kCaptureLines = 100000;                // or at this many lines

// The file name of this process's executable without its extension; empty when unknown.
std::string ProgramName();

// user/capture/<ProgramName()>.txt of the data root next to programDir ("host.txt" when the
// name is unknown).
std::string CapturePath(std::string_view programDir);

class Capture {
public:
    // Captures into the file path (UTF-8). The lines it already holds count as written; the
    // folder and file are made at the first line written.
    explicit Capture(std::string path, std::size_t maxBytes = kCaptureBytes, std::size_t maxLines = kCaptureLines);
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    // Appends each line of text (split at "\n", without "\r" and surrounding spaces) that holds a
    // non-ASCII character and is not in the file yet, one write per line. Safe from several
    // threads. Stops for good, silently, once the file cannot be written or would pass a limit.
    void Add(std::string_view text);

    const std::string& Path() const { return path_; }

private:
    bool Open();
    void Stop();

    std::mutex mutex_;
    const std::string path_;
    const std::size_t maxBytes_, maxLines_;
    std::FILE* file_ = nullptr;
    bool stopped_ = false;
    std::size_t bytes_ = 0;
    std::unordered_set<std::uint64_t> seen_;  // hashes of the lines in the file
};

// The capture of the settings key capture for the DLL in programDir; null when off.
std::unique_ptr<Capture> OpenCapture(bool on, std::string_view programDir);

// Puts next in current, but keeps current when next writes the same file: next has not seen the
// lines current wrote since next read the file.
void ReplaceCapture(std::unique_ptr<Capture>& current, std::unique_ptr<Capture> next);

}  // namespace hdTrans::frontend
