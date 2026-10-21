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
#include "frontend/module_dir.h"

namespace {

std::mutex g_mutex;
std::condition_variable g_adminsDone;
std::atomic<unsigned> g_waitingAdmins{0};
std::uint64_t g_generation = 0;
std::vector<std::string> g_dirs;
std::optional<std::string> g_ehnd;
std::string g_configErrors;
hdTrans::internal::ConfigCache g_cache;
std::unique_ptr<hdTrans::Engine> g_engine;
std::unique_ptr<hdTrans::Session> g_session;

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

int hdTrans_Initialize(const char* dirs) {
    std::uint64_t generation;
    {
        AdminLock lock;
        generation = g_generation;
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
    const std::string ehnd = settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "";
    {
        AdminLock lock;
        if (generation != g_generation || !g_cache.Apply(settings.cache)) return -1;
    }
    auto e = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off, kUserReload);
    e->OpenEhnd(ehnd);
    if (!e->OpenUserDicts(paths, false, userPaths)) return -1;
    AdminLock lock;
    if (generation != g_generation) return -1;
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine, false, settings.cg, settings.misread);
    g_session->SetSoften(settings.soften);
    g_dirs = requestedPaths;
    g_ehnd.reset();
    g_configErrors = hdTrans::internal::ConfigErrors(config, configPath);
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
    static_cast<void>(g_cache.Apply(std::nullopt));
}

int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_session) return -1;
    return Copy(g_session->Translate(std::string_view(text, n)), out, outn);
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
    {
        AdminLock lock;
        if (!g_engine) return -1;
        generation = g_generation;
        dirs = g_dirs;
        ehndOverride = g_ehnd;
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
    const std::string configuredEhnd = settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "";
    const std::string ehnd = ehndOverride.value_or(configuredEhnd);
    {
        AdminLock lock;
        if (generation != g_generation || !g_cache.Apply(settings.cache)) return -1;
    }
    auto e = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off, kUserReload);
    e->OpenEhnd(ehnd);
    if (!e->OpenUserDicts(dirs, false, userPaths)) return -1;
    if (!e->UserFileError().empty() && hdTrans::dict::UserFiles(userPaths).size() == 1) return -1;
    AdminLock lock;
    if (generation != g_generation) return -1;
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine, false, settings.cg, settings.misread);
    g_session->SetSoften(settings.soften);
    g_configErrors = hdTrans::internal::ConfigErrors(config, configPath);
    ++g_generation;
    return 0;
}

int hdTrans_UserDictError(char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_engine) return -1;
    std::string errors = g_configErrors;
    const std::string userError = g_engine->UserFileError();
    if (!userError.empty()) errors += (errors.empty() ? "" : "\n") + userError;
    return Copy(errors, out, outn);
}

int hdTrans_OpenEhnd(const char* dir) {
    AdminLock lock;
    if (!g_engine) return -1;
    const std::string d = dir ? dir : "";
    if (!g_engine->OpenEhnd(d)) return -1;
    g_ehnd = d;
    return 0;
}

int hdTrans_FilterErrors(char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_engine) return -1;
    std::string s;
    for (const std::string& e : g_engine->FilterErrors()) s += (s.empty() ? "" : "\n") + e;
    return Copy(s, out, outn);
}

void hdTrans_FreeMem(void* p) { std::free(p); }

const char* hdTrans_Version(void) { return HDTRANS_VERSION; }

}  // extern "C"
