// J2KEngine-compatible exports: Shift-JIS or UTF-16 in, CP949 or UTF-16 out.
// Program data are in hdTrans/ next to the DLL, or the DLL folder itself.
// The host's path is ignored. The user dictionary is reloaded when it changes.
// Calls are thread-safe, not reentrant; finish all calls before unloading the DLL.
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/config/cache.h"
#include "core/config/config.h"
#include "core/dict/folders.h"
#include "core/engine.h"
#include "frontend/module_dir.h"

namespace {

std::string Narrow(UINT codepage, const wchar_t* s, int n) {
    if (n == 0) return {};
    const int len = WideCharToMultiByte(codepage, 0, s, n, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(codepage, 0, s, n, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Widen(UINT codepage, const char* s, int n) {
    if (n == 0) return {};
    const int len = MultiByteToWideChar(codepage, 0, s, n, nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(codepage, 0, s, n, out.data(), len);
    return out;
}

std::string Utf8FromWide(const std::wstring& w) { return Narrow(CP_UTF8, w.data(), static_cast<int>(w.size())); }
std::wstring WideFromUtf8(const std::string& s) { return Widen(CP_UTF8, s.data(), static_cast<int>(s.size())); }

constexpr UINT kShiftJis = 932;
constexpr UINT kCp949 = 949;

std::mutex g_mutex;
std::condition_variable g_adminsDone;
std::atomic<unsigned> g_waitingAdmins{0};
std::uint64_t g_generation = 0;
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

std::string TranslateUtf8(const std::string& text) { return g_session ? g_session->Translate(text) : std::string(); }

// Copies s into a NUL-terminated block the host releases with J2K_FreeMem.
template <class Char>
Char* Block(const std::basic_string<Char>& s) {
    auto* p = static_cast<Char*>(HeapAlloc(GetProcessHeap(), 0, (s.size() + 1) * sizeof(Char)));
    if (!p) return nullptr;
    std::memcpy(p, s.data(), s.size() * sizeof(Char));
    p[s.size()] = 0;
    return p;
}

hdTrans::internal::ConfigResult ReadConfig(const std::string& dir) {
    const std::string path = hdTrans::dict::ConfigFile(dir);
    auto config = hdTrans::internal::LoadConfig(path);
    const std::string errors = hdTrans::internal::ConfigErrors(config, path);
    if (!errors.empty()) OutputDebugStringW(WideFromUtf8("J2KEngine: " + errors + "\n").c_str());
    return config;
}

// Writes user dictionary errors and skipped Ehnd lines to the
// debugger output.
void LogLoadErrors(const hdTrans::Engine& e) {
    const std::string error = e.UserFileError();
    if (!error.empty()) OutputDebugStringW(WideFromUtf8("J2KEngine: user dictionary not loaded:\n" + error + "\n").c_str());
    std::string skipped;
    for (const std::string& line : e.FilterErrors()) skipped += line + "\n";
    if (!skipped.empty()) OutputDebugStringW(WideFromUtf8("J2KEngine: Ehnd lines skipped:\n" + skipped).c_str());
}

}  // namespace

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

extern "C" {

// Terminate, Initialize or Reload can cancel a pending load (returns 0).
int __stdcall J2K_Initialize(char* /*dicPath*/) {
    std::uint64_t generation;
    {
        AdminLock lock;
        if (g_engine) return 1;
        generation = g_generation;
    }
    const std::string dir = hdTrans::frontend::ModuleDirectory();
    const auto config = ReadConfig(dir);
    const auto& settings = config.settings;
    const auto dirs = settings.dicts.value_or(std::vector<std::string>{hdTrans::dict::ShippedFolder(dir)});
    const auto userPaths = settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
    {
        AdminLock lock;
        if (generation != g_generation) return 0;
        if (!g_cache.Apply(settings.cache)) {
            OutputDebugStringW(L"J2KEngine: cannot set cache directory\n");
            return 0;
        }
    }
    auto e = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto);
    e->OpenEhnd(settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "");
    if (!e->OpenUserDicts(dirs, false, userPaths)) return 0;
    LogLoadErrors(*e);
    AdminLock lock;
    if (generation != g_generation) return 0;
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine, false, settings.cg, settings.misread);
    g_session->SetSoften(settings.soften);
    ++g_generation;
    return 1;
}

int __stdcall J2K_InitializeEx(char* dicPath, char* /*mainDir*/) { return J2K_Initialize(dicPath); }

int __stdcall J2K_Terminate() {
    AdminLock lock;
    ++g_generation;
    g_session.reset();
    g_engine.reset();
    static_cast<void>(g_cache.Apply(std::nullopt));
    return 1;
}

char* __stdcall J2K_TranslateMMNT(HWND /*hWnd*/, char* text) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_engine || !text) return nullptr;
    const std::string utf8 = Utf8FromWide(Widen(kShiftJis, text, static_cast<int>(std::strlen(text))));
    const std::wstring w = WideFromUtf8(TranslateUtf8(utf8));
    return Block(Narrow(kCp949, w.data(), static_cast<int>(w.size())));
}

wchar_t* __stdcall J2K_TranslateMMNTW(HWND /*hWnd*/, const wchar_t* text) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_engine || !text) return nullptr;
    const std::string utf8 = Narrow(CP_UTF8, text, static_cast<int>(std::wcslen(text)));
    return Block(WideFromUtf8(TranslateUtf8(utf8)));
}

void* __stdcall J2K_TranslateMM(HWND hWnd, char* text) { return J2K_TranslateMMNT(hWnd, text); }
void* __stdcall J2K_TranslateMMEx(HWND hWnd, char* text) { return J2K_TranslateMMNT(hWnd, text); }
void* __stdcall J2K_TranslateChat(char* text) { return J2K_TranslateMMNT(nullptr, text); }
void* __stdcall J2K_TranslateFM(HWND /*hWnd*/, char* /*fileName*/) { return nullptr; }

void __stdcall J2K_FreeMem(void* block) {
    if (block) HeapFree(GetProcessHeap(), 0, block);
}

int __stdcall J2K_StopTranslation(void* /*target*/) { return 1; }
void __stdcall J2K_ReloadUserDict() {
    std::uint64_t generation;
    {
        AdminLock lock;
        if (!g_engine) return;
        generation = g_generation;
    }
    const std::string dir = hdTrans::frontend::ModuleDirectory();
    const auto config = ReadConfig(dir);
    const auto& settings = config.settings;
    const auto dirs = settings.dicts.value_or(std::vector<std::string>{hdTrans::dict::ShippedFolder(dir)});
    const auto userPaths = settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
    {
        AdminLock lock;
        if (generation != g_generation) return;
        if (!g_cache.Apply(settings.cache)) {
            OutputDebugStringW(L"J2KEngine: cannot set cache directory\n");
            return;
        }
    }
    auto e = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto);
    e->OpenEhnd(settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "");
    if (!e->OpenUserDicts(dirs, false, userPaths)) return;
    LogLoadErrors(*e);
    if (!e->UserFileError().empty() && hdTrans::dict::UserFiles(userPaths).size() == 1) return;
    AdminLock lock;
    if (generation != g_generation) return;
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine, false, settings.cg, settings.misread);
    g_session->SetSoften(settings.soften);
    ++g_generation;
}
void __stdcall J2K_SetField(int) {}
void __stdcall J2K_SetHnj2han(int) {}
void __stdcall J2K_SetDelJPN(int) {}
void __stdcall J2K_SetJWin(int) {}
void __stdcall J2K_SetPriorDict(char* /*path*/) {}
char* __stdcall J2K_GetPriorDict() { return nullptr; }
int __stdcall J2K_SetProperty(int /*id*/, void* /*value*/, int /*size*/) { return 0; }
int __stdcall J2K_GetProperty(int /*id*/, void* /*value*/, int /*size*/) { return 0; }
void __stdcall J2K_GetJ2KMainDir() {}

}  // extern "C"
