#include "core/hdtrans.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "core/config/cache.h"
#include "core/config/config.h"
#include "core/dict/folders.h"
#include "core/engine.h"
#include "core/engine/user_watch.h"
#include "core/names/list.h"
#include "frontend/module_dir.h"

namespace {

std::mutex g_mutex;
std::condition_variable g_adminsDone;
std::atomic<unsigned> g_waitingAdmins{0};
std::uint64_t g_generation = 0;
std::vector<std::string> g_dirs;
std::optional<std::string> g_ehnd;
std::string g_configErrors;
std::string g_configPath;
std::optional<bool> g_strictDictOverride;
bool g_failedLoad = false;
std::string g_failedUserError;
std::string g_failedConfigErrors;
std::vector<std::string> g_failedFilterErrors;
std::weak_ptr<const hdTrans::dict::Snapshot> g_failedSnapshot;
std::weak_ptr<const hdTrans::filter::Filters> g_failedFilters;
std::string g_activeUserError;
std::vector<std::string> g_activeFilterErrors;
hdTrans::internal::ConfigCache g_cache;
std::unique_ptr<hdTrans::Engine> g_engine;
std::unique_ptr<hdTrans::Session> g_session;
std::string g_names;  // the name list of hdTrans_SetNames

// Queued state changes run before new translations.
class AdminLock {
public:
    AdminLock() : lock_(g_mutex, std::defer_lock) {
        ++g_waitingAdmins;
        lock_.lock();
    }
    ~AdminLock() {
        --g_waitingAdmins;
        lock_.unlock();
        g_adminsDone.notify_all();
    }

private:
    std::unique_lock<std::mutex> lock_;
};

std::vector<std::string> SplitNul(const char* dirs) {
    std::vector<std::string> out;
    for (const char* p = dirs; p && *p; p += std::strlen(p) + 1) out.emplace_back(p);
    return out;
}

// WebAssembly has no threads to reload user dictionaries in the background.
#if defined(__EMSCRIPTEN__)
constexpr hdTrans::UserReload kUserReload = hdTrans::UserReload::Off;
#else
constexpr hdTrans::UserReload kUserReload = hdTrans::UserReload::Auto;
#endif

int Copy(const std::string& s, char** out, size_t* outn) {
    auto* buf = static_cast<char*>(std::malloc(s.size() + 1));
    if (!buf) return -1;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = 0;
    *out = buf;
    *outn = s.size();
    return 0;
}

bool HasFatalLoadError(const hdTrans::Engine& engine) {
    for (const auto& error : hdTrans::internal::UserFileErrors(engine))
        if (hdTrans::dict::FailsLoad(error.kind)) return true;
    return hdTrans::internal::FilterFileFailed(engine);
}

void ClearFailedLoad() {
    g_failedLoad = false;
    g_failedUserError.clear();
    g_failedConfigErrors.clear();
    g_failedFilterErrors.clear();
    g_failedSnapshot.reset();
    g_failedFilters.reset();
    g_activeUserError.clear();
    g_activeFilterErrors.clear();
}

void RefreshFailedLoad() {
    if (g_failedLoad && g_engine &&
        (g_failedSnapshot.lock() != g_engine->Dictionaries() || g_failedFilters.lock() != g_engine->Filters() ||
         g_activeUserError != g_engine->UserFileError() || g_activeFilterErrors != g_engine->FilterErrors()))
        ClearFailedLoad();
}

int FailedLoad(std::uint64_t generation, const hdTrans::Engine& engine, const std::string& configErrors) {
    AdminLock lock;
    if (generation == g_generation) {
        g_failedConfigErrors = configErrors;
        g_failedLoad = true;
        g_failedUserError = engine.UserFileError();
        g_failedFilterErrors = engine.FilterErrors();
        if (g_engine) {
            g_failedSnapshot = g_engine->Dictionaries();
            g_failedFilters = g_engine->Filters();
            g_activeUserError = g_engine->UserFileError();
            g_activeFilterErrors = g_engine->FilterErrors();
        }
        if (g_failedUserError.empty() && g_failedFilterErrors.empty()) g_failedUserError = "dictionary load failed";
    }
    return -1;
}

std::string ConfigDiagnostics(const hdTrans::internal::ConfigResult& config, const std::string& path,
                              const std::string& programDir) {
    std::string errors = hdTrans::internal::ConfigErrors(config, path);
    for (const auto& warning : hdTrans::dict::FolderWarnings(programDir)) errors += (errors.empty() ? "" : "\n") + warning;
    return errors;
}

}  // namespace

#if defined(_WIN32) && defined(HDTRANS_BUILD_DLL)
// Nothing waits under the loader lock: at process exit (reserved set) the engine is left to
// the OS; on FreeLibrary no reload thread runs here any more (each holds a module reference)
// and the engine is released without waiting for one.
BOOL WINAPI DllMain(HINSTANCE /*module*/, DWORD reason, LPVOID reserved) {
    if (reason != DLL_PROCESS_DETACH) return TRUE;
    if (reserved) {
        static_cast<void>(g_session.release());
        static_cast<void>(g_engine.release());
        return TRUE;
    }
    g_session.reset();
    if (g_engine) g_engine->DetachReload();
    g_engine.reset();
    return TRUE;
}
#endif

extern "C" {

int hdTrans_SetOptions(const hdTrans_Options* options) {
    std::optional<bool> strictDict;
    if (options && options->size != 0) {
        if (options->size < sizeof(options->size)) return -1;
        if (options->size >= offsetof(hdTrans_Options, strict_dict) + sizeof(options->strict_dict)) {
            if (options->strict_dict > 2) return -1;
            if (options->strict_dict != 0) strictDict = options->strict_dict == 1;
        }
    }
    AdminLock lock;
    if (strictDict != g_strictDictOverride) {
        g_strictDictOverride = strictDict;
        if (g_engine) g_engine->SetOptions(hdTrans::EngineOptions{strictDict, true, g_configPath});
    }
    return 0;
}

int hdTrans_Initialize(const char* dirs) {
    std::uint64_t generation;
    std::optional<bool> strictDict;
    bool hadActiveEngine;
    std::vector<hdTrans::dict::UserSource> acceptedSources;
    std::optional<hdTrans::dict::UserSource> acceptedEhnd;
    {
        AdminLock lock;
        generation = g_generation;
        strictDict = g_strictDictOverride;
        hadActiveEngine = static_cast<bool>(g_engine);
        if (g_engine) acceptedSources = hdTrans::internal::AcceptedUserSources(*g_engine, acceptedEhnd);
    }
    auto paths = SplitNul(dirs);
    const std::string dir = hdTrans::frontend::ModuleDirectory();
    const std::string configPath = hdTrans::dict::ConfigFile(dir);
    const auto config = hdTrans::internal::LoadConfig(configPath);
    const auto& settings = config.settings;
    const auto requestedPaths = paths;
    if (paths.empty() && settings.dicts) paths = *settings.dicts;
#if !defined(__EMSCRIPTEN__)
    else if (paths.empty()) paths = {hdTrans::dict::ShippedFolder(dir)};
#endif
    const auto userPaths = settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
    std::vector<hdTrans::dict::UserSource> requestedUserSources;
    for (const auto& path : userPaths)
        requestedUserSources.push_back({path, false, false, settings.userdict.has_value()});
    const std::string ehnd = settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "";
    {
        AdminLock lock;
        if (generation != g_generation || !g_cache.Apply(settings.cache)) return -1;
    }
    const hdTrans::EngineOptions dictionaryOptions{strictDict, true, configPath};
    auto e = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off, kUserReload, dictionaryOptions);
    if (hadActiveEngine)
        hdTrans::internal::SeedUserSources(*e, std::move(acceptedSources), std::move(acceptedEhnd));
    const hdTrans::dict::UserSource ehndSource{ehnd, true, false, settings.ehnd.has_value()};
    const bool ehndLoaded = hdTrans::internal::OpenEhndSource(*e, ehndSource, config);
    const bool loaded = hdTrans::internal::OpenUserSources(*e, paths, false, requestedUserSources, config);
    if (!loaded || (hadActiveEngine && (!ehndLoaded || HasFatalLoadError(*e))))
        return FailedLoad(generation, *e, ConfigDiagnostics(config, configPath, dir));
    AdminLock lock;
    if (generation != g_generation) return -1;
    e->SetOptions(hdTrans::EngineOptions{g_strictDictOverride, true, configPath});
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine, false, settings.cg, settings.misread);
    g_session->SetSpeech(settings.speech);
    g_session->SetSpeechLevels(settings.speechLevels);
    g_session->SetWrapped(settings.wrapped);
    g_session->SetPredicateVariants(settings.predicateVariants);
    static_cast<void>(g_session->SetNames(g_names));
    g_dirs = requestedPaths;
    g_ehnd.reset();
    ClearFailedLoad();
    g_configErrors = ConfigDiagnostics(config, configPath, dir);
    g_configPath = configPath;
    ++g_generation;
    return 0;
}

void hdTrans_Terminate(void) {
    AdminLock lock;
    ++g_generation;
    g_session.reset();
    g_engine.reset();
    g_dirs.clear();
    g_ehnd.reset();
    g_configErrors.clear();
    g_configPath.clear();
    g_strictDictOverride.reset();
    g_names.clear();
    ClearFailedLoad();
    static_cast<void>(g_cache.Apply(std::nullopt));
}

int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_session) return -1;
    return Copy(g_session->Translate(std::string_view(text, n)), out, outn);
}

int hdTrans_TranslateContext(const char* context, size_t cn, const char* text, size_t n, char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_session) return -1;
    const std::string_view lines = context ? std::string_view(context, cn) : std::string_view();
    return Copy(g_session->Translate(std::string_view(text, n), lines), out, outn);
}

int hdTrans_SetNames(const char* text, size_t n) {
    AdminLock lock;
    g_names = text ? std::string(text, n) : std::string();
    if (g_session) return g_session->SetNames(g_names) ? 0 : -1;
    hdTrans::names::List list;
    return hdTrans::names::ParseList(g_names, list) ? 0 : -1;
}

int hdTrans_Reset(void) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_session) return -1;
    g_session->Reset();
    return 0;
}

int hdTrans_Reload(void) {
    std::uint64_t generation;
    std::vector<std::string> dirs;
    std::optional<std::string> ehndOverride;
    std::optional<bool> strictDict;
    std::vector<hdTrans::dict::UserSource> userSources;
    std::optional<hdTrans::dict::UserSource> acceptedEhnd;
    {
        AdminLock lock;
        if (!g_engine) return -1;
        generation = g_generation;
        dirs = g_dirs;
        ehndOverride = g_ehnd;
        strictDict = g_strictDictOverride;
        userSources = hdTrans::internal::AcceptedUserSources(*g_engine, acceptedEhnd);
    }
    const std::string dir = hdTrans::frontend::ModuleDirectory();
    const std::string configPath = hdTrans::dict::ConfigFile(dir);
    const auto config = hdTrans::internal::LoadConfig(configPath);
    const auto& settings = config.settings;
    if (dirs.empty() && settings.dicts) dirs = *settings.dicts;
#if !defined(__EMSCRIPTEN__)
    else if (dirs.empty()) dirs = {hdTrans::dict::ShippedFolder(dir)};
#endif
    const auto userPaths = settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
    std::vector<hdTrans::dict::UserSource> requestedUserSources;
    for (const auto& path : userPaths)
        requestedUserSources.push_back({path, false, false, settings.userdict.has_value()});
    const std::string configuredEhnd = settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "";
    const std::string ehnd = ehndOverride.value_or(configuredEhnd);
    {
        AdminLock lock;
        if (generation != g_generation || !g_cache.Apply(settings.cache)) return -1;
    }
    const hdTrans::EngineOptions dictionaryOptions{strictDict, true, configPath};
    auto e = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off, kUserReload, dictionaryOptions);
    hdTrans::internal::SeedUserSources(*e, std::move(userSources), std::move(acceptedEhnd));
    const hdTrans::dict::UserSource ehndSource{ehnd, true, false, ehndOverride.has_value() || settings.ehnd.has_value()};
    if (!hdTrans::internal::OpenEhndSource(*e, ehndSource, config) ||
        !hdTrans::internal::OpenUserSources(*e, dirs, false, requestedUserSources, config) || HasFatalLoadError(*e))
        return FailedLoad(generation, *e, ConfigDiagnostics(config, configPath, dir));
    AdminLock lock;
    if (generation != g_generation) return -1;
    e->SetOptions(hdTrans::EngineOptions{g_strictDictOverride, true, configPath});
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine, false, settings.cg, settings.misread);
    g_session->SetSpeech(settings.speech);
    g_session->SetSpeechLevels(settings.speechLevels);
    g_session->SetWrapped(settings.wrapped);
    g_session->SetPredicateVariants(settings.predicateVariants);
    static_cast<void>(g_session->SetNames(g_names));
    g_configErrors = ConfigDiagnostics(config, configPath, dir);
    g_configPath = configPath;
    ClearFailedLoad();
    ++g_generation;
    return 0;
}

int hdTrans_UserDictError(char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    RefreshFailedLoad();
    if (!g_engine && !g_failedLoad) return -1;
    std::string errors = g_failedLoad ? g_failedConfigErrors : g_configErrors;
    const std::string userError = g_failedLoad ? g_failedUserError : g_engine->UserFileError();
    if (!userError.empty()) errors += (errors.empty() ? "" : "\n") + userError;
    return Copy(errors, out, outn);
}

int hdTrans_OpenEhnd(const char* dir) {
    AdminLock lock;
    if (!g_engine) return -1;
    const std::string d = dir ? dir : "";
    const auto config = hdTrans::internal::LoadConfig(g_configPath);
    g_configErrors = ConfigDiagnostics(config, g_configPath, hdTrans::frontend::ModuleDirectory());
    ClearFailedLoad();
    if (!hdTrans::internal::OpenEhndSource(*g_engine, {d, true, false, true}, config)) return -1;
    g_ehnd = d;
    return 0;
}

int hdTrans_FilterErrors(char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    RefreshFailedLoad();
    if (!g_engine && !g_failedLoad) return -1;
    std::string s;
    const auto errors = g_failedLoad ? g_failedFilterErrors : g_engine->FilterErrors();
    for (const std::string& e : errors) s += (s.empty() ? "" : "\n") + e;
    return Copy(s, out, outn);
}

void hdTrans_FreeMem(void* p) { std::free(p); }

const char* hdTrans_Version(void) { return HDTRANS_VERSION; }

}  // extern "C"
