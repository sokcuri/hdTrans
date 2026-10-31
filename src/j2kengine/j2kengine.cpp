// J2KEngine-compatible exports: Shift-JIS or UTF-16 in, CP949 or UTF-16 out.
// Program data are in hdtrans-data/ next to the DLL.
// The host's path is ignored. The user dictionary is reloaded when it changes. Inside Anemone,
// its AneDic.txt is a user dictionary too.
// Calls are thread-safe, not reentrant; finish all calls before unloading the DLL.
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/config/cache.h"
#include "core/config/config.h"
#include "core/dict/folders.h"
#include "core/dict/runes.h"
#include "core/engine.h"
#include "core/engine/user_watch.h"
#include "core/engine/load_info.h"
#include "core/filter/ehnd.h"
#include "frontend/load_info.h"
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
std::weak_ptr<const hdTrans::dict::Snapshot> g_loggedSnapshot;
std::weak_ptr<const hdTrans::filter::Filters> g_loggedFilters;
std::optional<std::uint64_t> g_loggedDiagnosticsVersion;
std::atomic<bool> g_anemone{false};  // Anemone's window was found in this process
bool g_anemoneProbed = false;        // the engine was checked for it after it opened

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

void LogLoadErrors(const hdTrans::Engine& engine, bool force = true, bool loaded = true);

void Log(const std::string& text) { OutputDebugStringW(WideFromUtf8("J2KEngine: " + text + "\n").c_str()); }

// Whether this process shows Anemone's parent window: the first window of the first of its two
// class names that exists belongs to this process.
bool InAnemone() {
    HWND w = FindWindowW(L"AneParentClass", nullptr);
    if (!w) w = FindWindowW(L"AnemoneParentWndClass", nullptr);
    DWORD pid = 0;
    return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
}

// Anemone's AneDic.txt next to the executable in the Ehnd UserDict format (docs/ehnd-compat.md):
// UTF-16LE after its byte order mark, otherwise UTF-8. None when it is missing or not UTF-8.
std::vector<hdTrans::filter::Source> AneDic() {
    std::wstring exe(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (n == 0) return {};
        if (n < exe.size()) {
            exe.resize(n);
            break;
        }
        exe.resize(exe.size() * 2);
    }
    const std::string path = hdTrans::dict::FindChild(Utf8FromWide(exe.substr(0, exe.find_last_of(L"\\/"))), "AneDic.txt");
    std::ifstream in(std::filesystem::path(WideFromUtf8(path)), std::ios::binary);
    if (!in) return {};
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad() || (!bytes.starts_with("\xFF\xFE") && !hdTrans::dict::internal::ValidUtf8(bytes))) {
        Log(path + ": cannot be read as UTF-8; not loaded");
        return {};
    }
    Log("Anemone: reading " + path);
    return {{hdTrans::filter::Source::Kind::UserDict, path, hdTrans::filter::EhndText(bytes)}};
}

// Runs at the first translation of an engine opened without the window found, as a host may
// create it later; Anemone calls J2K_ReloadUserDict only when AneDic.txt changes.
void ProbeAnemone() {
    g_anemoneProbed = true;
    if (!g_anemone && !InAnemone()) return;
    g_anemone = true;
    auto dict = AneDic();
    if (dict.empty()) return;
    hdTrans::internal::SetHostUserDicts(*g_engine, std::move(dict));
    if (!g_engine->Reload()) Log("AneDic.txt: the dictionaries did not reload");
}

std::string TranslateUtf8(const std::string& text) {
    if (!g_session) return {};
    if (!g_anemoneProbed) ProbeAnemone();
    const std::string output = g_session->Translate(text);
    LogLoadErrors(*g_engine, false);
    return output;
}

// Copies s into a NUL-terminated block the host releases with J2K_FreeMem.
template <class Char>
Char* Block(const std::basic_string<Char>& s) {
    auto* p = static_cast<Char*>(HeapAlloc(GetProcessHeap(), 0, (s.size() + 1) * sizeof(Char)));
    if (!p) return nullptr;
    std::memcpy(p, s.data(), s.size() * sizeof(Char));
    p[s.size()] = 0;
    return p;
}

bool HasFatalLoadError(const hdTrans::Engine& engine) {
    for (const auto& error : hdTrans::internal::UserFileErrors(engine))
        if (hdTrans::dict::FailsLoad(error.kind)) return true;
    return hdTrans::internal::FilterFileFailed(engine);
}

hdTrans::internal::ConfigResult ReadConfig(const std::string& path) {
    auto config = hdTrans::internal::LoadConfig(path);
    std::string errors = hdTrans::internal::ConfigErrors(config, path);
    for (const auto& warning : hdTrans::dict::FolderWarnings(hdTrans::frontend::ModuleDirectory()))
        errors += (errors.empty() ? "" : "\n") + warning;
    if (!errors.empty()) OutputDebugStringW(WideFromUtf8("J2KEngine: " + errors + "\n").c_str());
    return config;
}

// Writes diagnostics once per completed load, including failed loads.
void LogLoadErrors(const hdTrans::Engine& engine, bool force, bool loaded) {
    const auto ticket = hdTrans::internal::LoadDiagnosticsVersion(engine);
    if (!ticket || (!force && g_loggedDiagnosticsVersion == ticket)) return;
    const auto snapshot = engine.Dictionaries();
    const auto filters = engine.Filters();
    const bool sourcesChanged = force || snapshot != g_loggedSnapshot.lock() || filters != g_loggedFilters.lock();
    if (!sourcesChanged && g_loggedDiagnosticsVersion == ticket) return;
    std::string diagnostics = engine.UserFileError();
    for (const std::string& line : engine.FilterErrors()) diagnostics += (diagnostics.empty() ? "" : "\n") + line;
    const auto info = loaded && sourcesChanged ? hdTrans::internal::LoadedSourceInfo(engine) : hdTrans::internal::LoadInfo{};
    if (hdTrans::internal::LoadDiagnosticsVersion(engine) != ticket) return;
    if (!diagnostics.empty())
        OutputDebugStringW(WideFromUtf8("J2KEngine: dictionary diagnostics:\n" + diagnostics + "\n").c_str());
    if (!loaded) return;
    const auto programDir = hdTrans::frontend::ModuleDirectory();
    for (const auto& file : info.files)
        OutputDebugStringW(WideFromUtf8(hdTrans::frontend::FormatLoadFileInfo(file, programDir) + "\n").c_str());
    g_loggedDiagnosticsVersion = ticket;
    g_loggedSnapshot = snapshot;
    g_loggedFilters = filters;
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
    const std::string configPath = hdTrans::dict::ConfigFile(dir);
    const auto config = ReadConfig(configPath);
    const auto& settings = config.settings;
    const auto dirs = settings.dicts.value_or(std::vector<std::string>{hdTrans::dict::ShippedFolder(dir)});
    const auto userPaths = settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
    std::vector<hdTrans::dict::UserSource> requestedUserSources;
    for (const auto& path : userPaths)
        requestedUserSources.push_back({path, false, false, settings.userdict.has_value()});
    const hdTrans::dict::UserSource ehndSource{
        settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "",
        true, false, settings.ehnd.has_value()};
    if (!g_anemone && InAnemone()) g_anemone = true;
    auto aneDic = g_anemone ? AneDic() : std::vector<hdTrans::filter::Source>{};
    {
        AdminLock lock;
        if (generation != g_generation) return 0;
        if (!g_cache.Apply(settings.cache)) {
            OutputDebugStringW(L"J2KEngine: cannot set cache directory\n");
            return 0;
        }
    }
    const hdTrans::EngineOptions dictionaryOptions{std::nullopt, false, configPath};
    auto e = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off,
                                            hdTrans::UserReload::Auto, dictionaryOptions);
    hdTrans::internal::SetHostUserDicts(*e, std::move(aneDic));
    static_cast<void>(hdTrans::internal::OpenEhndSource(*e, ehndSource, config));
    const bool loaded = hdTrans::internal::OpenUserSources(*e, dirs, false, requestedUserSources, config);
    AdminLock lock;
    if (generation != g_generation) return 0;
    LogLoadErrors(*e, true, loaded);
    if (!loaded) return 0;
    g_anemoneProbed = g_anemone;
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine, false, settings.cg, settings.misread);
    g_session->SetSpeech(settings.speech);
    g_session->SetSpeechLevels(settings.speechLevels);
    g_session->SetWrapped(settings.wrapped);
    g_session->SetPredicateVariants(settings.predicateVariants);
    ++g_generation;
    return 1;
}

int __stdcall J2K_InitializeEx(char* dicPath, char* /*mainDir*/) { return J2K_Initialize(dicPath); }

int __stdcall J2K_Terminate() {
    AdminLock lock;
    ++g_generation;
    g_session.reset();
    g_engine.reset();
    g_loggedSnapshot.reset();
    g_loggedFilters.reset();
    g_loggedDiagnosticsVersion.reset();
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
    std::vector<hdTrans::dict::UserSource> userSources;
    std::optional<hdTrans::dict::UserSource> acceptedEhnd;
    {
        AdminLock lock;
        if (!g_engine) return;
        generation = g_generation;
        userSources = hdTrans::internal::AcceptedUserSources(*g_engine, acceptedEhnd);
    }
    const std::string dir = hdTrans::frontend::ModuleDirectory();
    const std::string configPath = hdTrans::dict::ConfigFile(dir);
    const auto config = ReadConfig(configPath);
    const auto& settings = config.settings;
    const auto dirs = settings.dicts.value_or(std::vector<std::string>{hdTrans::dict::ShippedFolder(dir)});
    const auto userPaths = settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
    std::vector<hdTrans::dict::UserSource> requestedUserSources;
    for (const auto& path : userPaths)
        requestedUserSources.push_back({path, false, false, settings.userdict.has_value()});
    const hdTrans::dict::UserSource ehndSource{
        settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "",
        true, false, settings.ehnd.has_value()};
    if (!g_anemone && InAnemone()) g_anemone = true;
    auto aneDic = g_anemone ? AneDic() : std::vector<hdTrans::filter::Source>{};
    {
        AdminLock lock;
        if (generation != g_generation) return;
        if (!g_cache.Apply(settings.cache)) {
            OutputDebugStringW(L"J2KEngine: cannot set cache directory\n");
            return;
        }
    }
    const hdTrans::EngineOptions dictionaryOptions{std::nullopt, false, configPath};
    auto e = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off,
                                            hdTrans::UserReload::Auto, dictionaryOptions);
    hdTrans::internal::SeedUserSources(*e, std::move(userSources), std::move(acceptedEhnd));
    hdTrans::internal::SetHostUserDicts(*e, std::move(aneDic));
    const bool loaded = hdTrans::internal::OpenEhndSource(*e, ehndSource, config) &&
                        hdTrans::internal::OpenUserSources(*e, dirs, false, requestedUserSources, config) && !HasFatalLoadError(*e);
    AdminLock lock;
    if (generation != g_generation) return;
    LogLoadErrors(*e, true, loaded);
    if (!loaded) return;
    g_session.reset();
    g_anemoneProbed = true;
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine, false, settings.cg, settings.misread);
    g_session->SetSpeech(settings.speech);
    g_session->SetSpeechLevels(settings.speechLevels);
    g_session->SetWrapped(settings.wrapped);
    g_session->SetPredicateVariants(settings.predicateVariants);
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
