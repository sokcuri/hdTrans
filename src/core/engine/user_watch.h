#pragma once
// User dictionary sources and their automatic reload (UserReload::Auto).
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <thread>
#endif

#include "core/dict/folders.h"
#include "core/dict/mapped_file.h"
#include "core/dict/snapshot.h"
#include "core/engine.h"

namespace hdTrans::attach {
struct Data;
}

namespace hdTrans::internal {

// What a check finds at a path: whether a regular file is there, and its stamp.
struct FileVersion {
    bool present = false;
    dict::internal::FileStamp stamp;
    bool headerProbeFailed = false;
    bool operator==(const FileVersion&) const = default;
};

FileVersion VersionOf(std::string_view utf8Path);

// When to reload a watched file.
struct FileWatch {
    FileVersion loaded;  // the version the dictionaries hold; absent when they hold none
    FileVersion failed;  // the last version that failed to load, when hasFailed
    bool hasFailed = false;
    std::chrono::steady_clock::time_point next{};  // the earliest next check

    // Checks path unless the last check was less than interval before now; true when it
    // finds a version other than loaded and failed, stored in found.
    bool Due(std::string_view path, std::chrono::steady_clock::time_point now,
             std::chrono::steady_clock::duration interval, FileVersion& found);
};

struct UserVersion {
    std::vector<std::pair<std::string, FileVersion>> files;
    std::vector<dict::UserSource> sources;
    FileVersion config;
    std::uint64_t optionsRevision = 0;
    bool operator==(const UserVersion&) const = default;
};
UserVersion UserVersionOf(const std::vector<std::string>& paths);
UserVersion UserVersionOf(const std::vector<std::string>& paths, std::string_view configPath);
UserVersion UserVersionOf(const std::vector<std::string>& paths, std::string_view configPath,
                          std::uint64_t optionsRevision);
UserVersion UserVersionOf(const std::vector<dict::UserSource>& paths, std::string_view configPath,
                          std::uint64_t optionsRevision = 0);

struct UserFilesWatch {
    UserVersion loaded;
    UserVersion failed;
    bool hasFailed = false;
    std::chrono::steady_clock::time_point next{};

    bool Due(const std::vector<std::string>& paths, std::chrono::steady_clock::time_point now,
             std::chrono::steady_clock::duration interval, UserVersion& found, std::string_view configPath = {},
             std::uint64_t optionsRevision = 0);
    bool Due(const std::vector<dict::UserSource>& paths, std::chrono::steady_clock::time_point now,
             std::chrono::steady_clock::duration interval, UserVersion& found, std::string_view configPath,
             std::uint64_t optionsRevision);
};

// The files of an Ehnd folder that a load reads, with their stamps, in load order.
struct FolderVersion {
    std::vector<std::pair<std::string, dict::internal::FileStamp>> files;
    bool available = true;
    bool operator==(const FolderVersion&) const = default;
};

FolderVersion FolderVersionOf(std::string_view dir);

// When to reload a watched Ehnd folder; as FileWatch.
struct FolderWatch {
    FolderVersion loaded;
    FolderVersion failed;
    bool hasFailed = false;
    std::chrono::steady_clock::time_point next{};

    bool Due(std::string_view dir, std::chrono::steady_clock::time_point now,
             std::chrono::steady_clock::duration interval, FolderVersion& found);
};

// A background thread. On Windows it holds a reference to the module containing the
// library until it ends, so an unload never unmaps code it still runs.
class ReloadThread {
public:
    ReloadThread() = default;
    ReloadThread(const ReloadThread&) = delete;
    ReloadThread& operator=(const ReloadThread&) = delete;
    ~ReloadThread() { Detach(); }

    // Runs body on a new thread; false when none starts. The thread must be joined or
    // detached first.
    bool Start(std::function<void()> body);
    bool Joinable() const;
    void Join();
    // Lets the thread end on its own.
    void Detach();

private:
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    std::thread thread_;
#endif
};

// The engine a reload thread delivers to; null once the engine is gone or detached.
// Lock order: mu, then the engine's mutex.
struct ReloadTarget {
    std::mutex mu;
    Engine* engine = nullptr;
};

// The user dictionary state of an engine, guarded by the engine's mutex.
struct UserWatch {
    EngineOptions options;
    std::uint64_t optionsRevision = 0;
    std::vector<dict::UserSource> requestedSources;
    dict::UserSource requestedEhnd;
    std::vector<dict::UserSource> acceptedSources;
    std::optional<dict::UserSource> acceptedEhnd;
    bool sourceHistorySeeded = false;
    std::optional<dict::File> memoryUser;
    std::optional<std::vector<EhndFile>> memoryEhnd;
    bool memoryEhndStrict = true;
    bool memoryEhndStrictOverride = false;
    std::optional<bool> userStrictOverride;
    std::optional<bool> ehndStrictOverride;
    bool automatic = false;  // UserReload::Auto
    bool detached = false;   // no more reloads: the engine is being abandoned
    UserFilesWatch watch;
    FolderWatch ehnd;  // the Ehnd folder
    std::vector<dict::LoadError> errors;  // why the failed version did not load
    bool filterFailed = false;
    std::vector<dict::LoadFileInfo> dictionaryInfo;
    std::vector<dict::LoadFileInfo> filterInfo;
    std::shared_ptr<ReloadTarget> target = std::make_shared<ReloadTarget>();
    ReloadThread worker;
    bool running = false;
    std::condition_variable done;  // signalled when running turns false
};

// Reload threads running in the process.
int RunningReloads();

// Checks the user dictionary file and the Ehnd folder of e now, whatever the interval,
// and waits for the reload it starts; false when it starts none.
bool CheckUserFile(const Engine& e);
// Like CheckUserFile, without waiting for the reload.
bool StartUserFileCheck(const Engine& e);
// The errors and skipped-row warnings of the last user dictionary load.
std::vector<dict::LoadError> UserFileErrors(const Engine& e);
bool FilterFileFailed(const Engine& e);
// The attachment provider's data of the current dictionaries (Generated::attach), null for none.
std::shared_ptr<const attach::Data> AttachmentData(const Engine& e);
// Wrapper-serialized observation; an automatic reload in progress has no completed ticket.
std::optional<std::uint64_t> LoadDiagnosticsVersion(const Engine& e);

// Carries active source history into a replacement engine before its first Open.
std::vector<dict::UserSource> AcceptedUserSources(const Engine& e);
void SeedUserSources(Engine& e, std::vector<dict::UserSource> sources);
std::vector<dict::UserSource> AcceptedUserSources(const Engine& e, std::optional<dict::UserSource>& ehnd);
void SeedUserSources(Engine& e, std::vector<dict::UserSource> sources, std::optional<dict::UserSource> ehnd);

// Public string-path calls are explicit; frontends preserve generated defaults here.
bool OpenUserSources(Engine& e, const std::vector<std::string>& dirs, bool verify,
                     const std::vector<dict::UserSource>& sources);
bool OpenEhndSource(Engine& e, const dict::UserSource& source, const DictionaryOptions& options = {});

struct ConfigResult;
// Paths and policy use this same parse; the engine does not read the config again.
bool OpenUserSources(Engine& e, const std::vector<std::string>& dirs, bool verify,
                     const std::vector<dict::UserSource>& sources, const ConfigResult& config);
bool OpenEhndSource(Engine& e, const dict::UserSource& source, const ConfigResult& config,
                    const DictionaryOptions& options = {});
bool ReloadWithConfig(Engine& e, const ConfigResult& config, const DictionaryOptions& options = {});

}  // namespace hdTrans::internal
