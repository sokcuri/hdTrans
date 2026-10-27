#include "core/hdtrans.h"
#include "core/dict/snapshot.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int g_failures = 0;

struct UnicodeSample {
    const char* input;
    const char* expected;
    const char* message;
};

constexpr UnicodeSample kUnicodeSamples[] = {
    {"", "", "Unicode empty string"},
    {"猫", "고양이", "Unicode Japanese translation"},
    {"㐂", "㐂", "Unicode Japanese outside CP932"},
    {"ᄓꥠ", "ᄓꥠ", "Unicode Hangul outside CP949"},
    {"\U00020bb7\U0001f642", "\U00020bb7\U0001f642", "Unicode supplementary CJK and emoji"},
    {"猫\U0001f642", "고양이 \U0001f642", "Unicode translation next to a surrogate pair"},
    {"�A�", "�A�", "Unicode replacement characters preserved"},
};

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); ++g_failures; }
}

void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string Translate(const std::string& text) {
    char* output = nullptr;
    std::size_t size = 0;
    Check(hdTrans_Translate(text.data(), text.size(), &output, &size) == 0, "translate");
    const std::string result = output ? std::string(output, size) : std::string();
    hdTrans_FreeMem(output);
    return result;
}

std::string TranslateContext(const char* context, std::size_t cn, const std::string& text) {
    char* output = nullptr;
    std::size_t size = 0;
    Check(hdTrans_TranslateContext(context, cn, text.data(), text.size(), &output, &size) == 0, "translate with context");
    const std::string result = output ? std::string(output, size) : std::string();
    hdTrans_FreeMem(output);
    return result;
}

std::string Errors() {
    char* output = nullptr;
    std::size_t size = 0;
    Check(hdTrans_UserDictError(&output, &size) == 0, "diagnostics");
    const std::string result = output ? std::string(output, size) : std::string();
    hdTrans_FreeMem(output);
    return result;
}

int SetStrict(uint32_t value) {
    const hdTrans_Options options{static_cast<uint32_t>(sizeof(hdTrans_Options)), value};
    return hdTrans_SetOptions(&options);
}

void CheckDictionaryOptions(const std::filesystem::path& config, const std::filesystem::path& user) {
    const std::string base = "dicts: fixtures/first\nuserdict: fixtures/options-user.txt\ncache: off\n";
    Write(config, base);
    Write(user, "犬\tOPTION_OLD\n");
    hdTrans_Terminate();
    Check(hdTrans_SetOptions(nullptr) == 0, "reset options before initialization");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize with default options");
    Write(user, "犬\tOPTION_NEW\n猫\n");
    Check(hdTrans_Reload() == -1, "C API defaults to strict rows");
    Check(Translate("犬") == "OPTION_OLD", "strict reload keeps old output");
    Check(Errors().find("options-user.txt:2: empty KR") != std::string::npos, "failed candidate diagnostics retained");
    Check(SetStrict(2) == 0, "select skip mode");
    Check(hdTrans_Reload() == 0, "skip mode reloads good rows");
    Check(Translate("犬") == "OPTION_NEW", "good row in mixed file loads");
    Check(Errors().find("options-user.txt:2: empty KR") != std::string::npos, "row warning is queryable");
    for (uint32_t size : {1u, 2u, 3u}) {
        const hdTrans_Options invalid{size, 0};
        Check(hdTrans_SetOptions(&invalid) == -1, "reject truncated size header");
        Check(hdTrans_Reload() == 0, "invalid size preserves skip option");
    }
    Check(SetStrict(3) == -1, "reject unknown strict encoding");
    Check(hdTrans_Reload() == 0, "invalid value preserves skip option");
    struct FutureOptions { hdTrans_Options known; uint32_t tail; };
    const FutureOptions future{{static_cast<uint32_t>(sizeof(FutureOptions)), 2}, 99};
    Check(hdTrans_SetOptions(&future.known) == 0, "ignore future option tail");
    Check(hdTrans_Reload() == 0, "future option tail preserves known value");
    Write(config, base + "strict_dict: true\n");
    Check(hdTrans_Reload() == 0, "explicit skip overrides strict YAML");
    Write(config, base + "strict_dict: false\n");
    Check(SetStrict(1) == 0, "select strict mode");
    Check(hdTrans_Reload() == -1, "explicit strict overrides skip YAML");
    Check(Translate("犬") == "OPTION_NEW", "failed strict candidate retains output");
    for (uint32_t size : {0u, 4u, 5u, 6u, 7u}) {
        const hdTrans_Options missing{size, 99};
        Check(hdTrans_SetOptions(&missing) == 0, "absent strict field uses config/default");
        Check(hdTrans_Reload() == 0, "absent strict field permits YAML skip");
    }
    Check(SetStrict(0) == 0 && hdTrans_Reload() == 0, "encoding zero uses YAML");
    Check(hdTrans_SetOptions(nullptr) == 0, "NULL resets explicit override");
    Write(config, base + "strict_dict: true\n");
    Check(hdTrans_Reload() == -1, "reload rereads strict YAML");
    Write(config, base + "strict_dict: false\n");
    Check(hdTrans_Reload() == 0, "reload rereads skip YAML");
    for (int round = 0; round < 3; ++round) {
        Write(user, "犬\tOPTION_NEW\n");
        Check(SetStrict(1) == 0, "prepare valid concurrent strict candidate");
        std::atomic<bool> started{false};
        int status = -1;
        std::thread candidate([&] { started = true; status = hdTrans_Reload(); });
        while (!started) std::this_thread::yield();
        Check(SetStrict(2) == 0, "set the following load option without canceling this load");
        candidate.join();
        Check(status == 0, "SetOptions does not cancel a valid in-flight load");
        Write(user, "犬\tOPTION_NEW\n猫\n");
        Check(hdTrans_Reload() == 0, "published candidate preserves the newer skip request");
        Check(Translate("犬") == "OPTION_NEW", "newer option keeps good rows");
    }
    Write(user, "犬\tOPTION_NEW\n");
    Check(hdTrans_Reload() == 0, "prepare valid user rows for Ehnd options");
    const auto ehnd = user.parent_path() / "options-ehnd";
    std::error_code error;
    std::filesystem::create_directory(ehnd, error);
    Check(!error, "create Ehnd option fixture");
    Write(ehnd / "PreFilter_options.txt", "(\tbad\t100\t1\n");
    const auto ehndPath = Utf8(ehnd);
    Check(hdTrans_OpenEhnd(ehndPath.c_str()) == 0, "OpenEhnd inherits skip option");
    Check(SetStrict(1) == 0, "select strict before OpenEhnd");
    Check(hdTrans_OpenEhnd(ehndPath.c_str()) == -1, "OpenEhnd inherits changed strict option");
    Check(Translate("犬") == "OPTION_NEW", "failed OpenEhnd preserves old output");
    Check(hdTrans_OpenEhnd("") == 0, "drop malformed Ehnd with stored strict option");
    Write(user, "犬\tOPTION_NEW\n猫\n");
    hdTrans_Terminate();
    Check(hdTrans_Initialize(nullptr) == 0, "Terminate resets strict override to YAML skip");
    Check(Translate("犬") == "OPTION_NEW", "reset override permits the good user row");
    Check(Errors().find("options-user.txt:2: empty KR") != std::string::npos, "initial skipped-row warning remains queryable");
    hdTrans_Terminate();
    Check(SetStrict(1) == 0, "set strict before initialization");
    Check(hdTrans_Initialize(nullptr) == 0, "strict initial file exclusion still permits the base");
    Check(Translate("猫") == "고양이" && Translate("犬") != "OPTION_NEW", "strict cold load excludes the whole bad user file");
    Check(Errors().find("options-user.txt:2: empty KR") != std::string::npos, "excluded initial file diagnostic remains queryable");
    hdTrans_Terminate();
    Check(SetStrict(2) == 0, "set skip while uninitialized");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize uses the pending skip option");
    hdTrans_Terminate();
    Write(config, base + "strict_dict: true\n");
    Check(hdTrans_Initialize(nullptr) == 0, "Terminate also resets skip override to YAML strict");
    Check(Translate("猫") == "고양이" && Translate("犬") != "OPTION_NEW", "reset skip override excludes the malformed file");
    Check(hdTrans_SetOptions(nullptr) == 0, "reset stored options after test");
}

void CheckColdExclusions(const std::filesystem::path& config, const std::filesystem::path& fixtures) {
    const auto user = fixtures / "cold-bad.txt";
    const auto ehnd = fixtures / "cold-ehnd";
    std::error_code error;
    std::filesystem::create_directory(ehnd, error);
    Check(!error, "create cold Ehnd fixture");
    Write(fixtures / "cold-good.txt", "犬\tCOLD_KEEP\n");
    const std::string base = "dicts: fixtures/first\nuserdict: [fixtures/cold-good.txt, fixtures/cold-bad.txt]\n"
                             "ehnd: fixtures/cold-ehnd\ncache: off\nstrict_dict: true\n";
    hdTrans_Terminate();
    Write(config, "dicts: fixtures/first\nuserdict: ''\nehnd: ''\ncache: off\nstrict_dict: true\n");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize a base-only engine with no accepted optional sources");
    Write(user, "鳥\tEXCLUDED\n犬\t개\tzzz\n");
    Write(config, "dicts: fixtures/second\nuserdict: fixtures/cold-bad.txt\nehnd: ''\ncache: off\nstrict_dict: true\n");
    Check(hdTrans_Reload() == -1, "base-only active reload rejects a new bad optional file");
    Check(Errors().find("cold-bad.txt:2: unknown POS") != std::string::npos, "base-only failure reports the new optional file");
    Check(Translate("猫") == "고양이", "empty optional history still retains the old base on reload failure");
    Check(hdTrans_Initialize(nullptr) == -1, "base-only active Initialize rejects a new bad optional file");
    Check(Translate("猫") == "고양이", "empty optional history still retains the old base on Initialize failure");
    const struct Case { std::string user; std::string filter; const char* diagnostic; bool filterError; } cases[] = {
        {"鳥\tEXCLUDED\n犬\t개\tzzz\n", "// empty\n", "cold-bad.txt:2: unknown POS", false},
        {"鳥\tEXCLUDED\n犬\t\xB0\xED\n", "// empty\n", "cold-bad.txt: invalid UTF-8", false},
        {"鳥\tCOLD_USER\n", "title\n猫\t犬\t1\t0\n(\tbad\t1\t1\n", "PreFilter_cold.txt:1:", true},
    };
    for (const auto& test : cases) {
        hdTrans_Terminate();
        Write(config, base);
        Write(user, test.user);
        Write(ehnd / "PreFilter_cold.txt", test.filter);
        Check(hdTrans_Initialize(nullptr) == 0, "cold load excludes one bad optional file");
        Check(Translate("猫") == "고양이" && Translate("犬") == "COLD_KEEP", "cold exclusion keeps base and other user file");
        Check(Translate("鳥") != "EXCLUDED", "no good row from an excluded file is applied");
        char* output = nullptr;
        std::size_t size = 0;
        Check(hdTrans_FilterErrors(&output, &size) == 0, "query cold filter diagnostics");
        const std::string filters = output ? std::string(output, size) : std::string();
        hdTrans_FreeMem(output);
        Check((test.filterError ? filters : Errors()).find(test.diagnostic) != std::string::npos,
              "excluded cold file diagnostic is preserved");
        Write(user, "鳥\tCOLD_USER\n");
        Write(ehnd / "PreFilter_cold.txt", "// empty\n");
        Check(hdTrans_Reload() == 0, "repair cold excluded files");
        Check(Translate("鳥") == "COLD_USER", "repaired file becomes active");
        Write(user, test.user);
        Write(ehnd / "PreFilter_cold.txt", test.filter);
        std::string changed = base;
        changed.replace(changed.find("fixtures/first"), std::strlen("fixtures/first"), "fixtures/second");
        Write(config, changed);
        Check(hdTrans_Reload() == -1, "active reload rejects the same bad optional file");
        Check(Translate("猫") == "고양이" && Translate("犬") == "COLD_KEEP" && Translate("鳥") == "COLD_USER",
              "failed active reload retains the whole old stack");
        Check(hdTrans_Initialize(nullptr) == -1, "active Initialize also retains the whole stack on failure");
        Check(Translate("猫") == "고양이" && Translate("鳥") == "COLD_USER", "failed active Initialize keeps old outputs");
    }
    Write(user, "鳥\tCOLD_USER\n");
    Write(ehnd / "PreFilter_cold.txt", "title\n猫\t犬\t1\t0\n");
    Write(config, base);
    Check(hdTrans_Reload() == 0, "ignored Ehnd title does not reject a healthy active file");
    Check(Translate("猫") == "COLD_KEEP", "valid row following a title remains active");
    Write(ehnd / "PreFilter_cold.txt", "// empty\n");
    Write(config, base + "unknown: value\n");
    Check(hdTrans_Reload() == 0, "publish config warning with healthy dictionaries");
    Check(Errors().find("unknown key: unknown") != std::string::npos, "active config warning is visible");
    Write(config, "dicts: fixtures/missing\ncache: off\n");
    Check(hdTrans_Reload() == -1, "fail a candidate with a clean config parse");
    Check(Errors().find("unknown key: unknown") == std::string::npos, "failed candidate uses fresh config diagnostics");
    Write(config, "dicts: fixtures/missing\nunknown: value\ncache: off\n");
    Check(hdTrans_Reload() == -1, "failed candidate can carry its own config warning");
    Check(Errors().find("unknown key: unknown") != std::string::npos, "failed config warning remains queryable");
    Write(config, base);
    Check(hdTrans_OpenEhnd("") == 0, "clear failed candidate after config repair");
    Check(Errors().empty(), "OpenEhnd refreshes and clears stale config diagnostics");
}

void CheckAcceptedUserSources(const std::filesystem::path& config, const std::filesystem::path& fixtures) {
    const auto user = fixtures / "history-user.txt";
    const auto renamed = fixtures / "history-renamed.txt";
    const auto folder = fixtures / "history-users";
    const std::string first = "dicts: fixtures/first\nehnd: fixtures/Ehnd\ncache: off\n";
    const std::string second = "dicts: fixtures/second\nehnd: fixtures/Ehnd\ncache: off\n";
    Write(user, "犬\tHISTORY_OLD\n");
    Write(config, first + "userdict: fixtures/history-user.txt\n");
    hdTrans_Terminate();
    Check(hdTrans_SetOptions(nullptr) == 0, "reset options for source history");
    Check(hdTrans_Initialize(nullptr) == 0, "accept explicit user source");
    Check(Translate("犬") == "HISTORY_OLD", "accepted user source output");
    std::error_code error;
    Check(std::filesystem::remove(user, error) && !error, "remove accepted explicit user file");
    Write(config, second + "userdict: fixtures/history-user.txt\n");
    Write(fixtures / "Ehnd/UserDict_test.txt", "タマ\tHISTORY_EHND_NEW\t1\n");
    Check(hdTrans_Initialize(nullptr) == -1, "active Initialize rejects a missing accepted same-path source");
    Check(Translate("猫") == "고양이" && Translate("犬") == "HISTORY_OLD", "failed active Initialize retains base and user");
    Check(hdTrans_Reload() == -1, "candidate rejects missing accepted user file");
    Check(Translate("猫") == "고양이", "missing source keeps old base dictionary");
    Check(Translate("犬") == "HISTORY_OLD", "missing source keeps old user dictionary");
    Check(Translate("タマ") == "나비", "missing source keeps old Ehnd dictionary");
    Check(Errors().find("history-user.txt") != std::string::npos, "missing accepted source diagnostic");
    Write(user, "犬\tHISTORY_NEW\n");
    Check(hdTrans_Reload() == 0, "restored accepted path reloads");
    Check(Translate("猫") == "야옹이", "restored source publishes new base dictionary");
    Check(Translate("犬") == "HISTORY_NEW", "restored source publishes new user dictionary");
    Check(Translate("タマ") == "HISTORY_EHND_NEW", "restored source publishes new Ehnd dictionary");
    Check(Errors().empty(), "restored source clears missing diagnostic");
    Check(std::filesystem::remove(user, error) && !error, "remove source intentionally");
    Write(config, second + "userdict: ''\n");
    Check(hdTrans_Reload() == 0, "removing path from requested list is allowed");
    Check(Translate("犬") != "HISTORY_NEW", "removed source no longer applies");
    Write(user, "犬\tHISTORY_RENAMED\n");
    Write(config, second + "userdict: fixtures/history-user.txt\n");
    Check(hdTrans_Reload() == 0, "accept source before rename");
    std::filesystem::remove(renamed, error);
    error.clear();
    std::filesystem::rename(user, renamed, error);
    Check(!error, "rename accepted source");
    Write(config, second + "userdict: fixtures/history-renamed.txt\n");
    Check(hdTrans_Reload() == 0, "new requested path permits intentional rename");
    Check(Translate("犬") == "HISTORY_RENAMED", "renamed source applies");
    std::filesystem::create_directories(folder, error);
    Check(!error, "create configured user folder");
    Write(folder / "userdict-a.txt", "犬\tHISTORY_FOLDER\n");
    Write(config, second + "userdict: fixtures/history-users\n");
    Check(hdTrans_Reload() == 0, "accept configured user folder");
    std::filesystem::rename(folder / "userdict-a.txt", folder / "userdict-b.txt", error);
    Check(!error, "rename member of configured folder");
    Check(hdTrans_Reload() == 0, "folder member rename is allowed");
    Check(Translate("犬") == "HISTORY_FOLDER", "renamed folder member still applies");
    std::filesystem::remove_all(folder, error);
    Check(!error, "remove accepted configured folder");
    Check(hdTrans_Reload() == -1, "candidate rejects missing accepted folder");
    Check(Translate("犬") == "HISTORY_FOLDER", "missing folder keeps old stack");
    Check(Errors().find("history-users") != std::string::npos, "missing accepted folder diagnostic");
    std::filesystem::create_directory(folder, error);
    Check(!error, "restore accepted configured folder");
    Write(folder / "userdict-b.txt", "犬\tHISTORY_FOLDER_NEW\n");
    Check(hdTrans_Reload() == 0, "restored configured folder reloads");
    Check(Translate("犬") == "HISTORY_FOLDER_NEW", "restored folder applies");
    Write(fixtures / "Ehnd/UserDict_test.txt", "タマ\t나비\t1\n");
}

template<class Initialize, class Reload, class Terminate, class Translation>
void CheckDefaultLayouts(const std::filesystem::path& folder, Initialize initialize, Reload reload,
                         Terminate terminate, Translation translation) {
    std::error_code error;
    const auto oldCwd = std::filesystem::current_path(error);
    const auto cwd = folder / "fixtures/unrelated-cwd";
    std::filesystem::create_directories(cwd, error);
    std::filesystem::current_path(cwd, error);
    Check(!error, "use unrelated working directory for default discovery");
    for (const bool nested : {false, true}) {
        const auto data = nested ? folder / "HDTRANS" : folder;
        const auto dicts = data / "DiCtS";
        const auto user = data / "USER";
        const auto snapshot = data / "DiCtS.HdSnAp";
        const auto source = folder / "fixtures/snapshot-source";
        const auto config = user / "HDTRANS.CONFIG.YAML";
        std::filesystem::create_directories(dicts, error);
        std::filesystem::create_directories(user, error);
        Write(dicts / "base.tsv", "key\tko\tpos\tid\n猫\t야옹이\tN110\t1\n");
        Write(user / "USERDICT.TSV", "犬\t새사전\n");
        Check(initialize(), "initialize default TSV fallback");
        translation("猫", "야옹이", "default TSV fallback translation");
        translation("犬", "새사전", "default user discovery with TSV fallback");
        Write(snapshot, "damaged snapshot\n");
        reload(false);
        translation("猫", "야옹이", "bad default snapshot reload retains dictionary");
        terminate();
        Check(!initialize(), "bad default snapshot does not fall back to TSV");
        terminate();
        std::filesystem::remove(snapshot, error);

        std::filesystem::create_directories(source, error);
        Write(source / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n");
        Check(hdTrans::dict::CompileSnapshot({Utf8(source)}, Utf8(snapshot)), "compile standalone default snapshot");
        std::filesystem::remove_all(source, error);
        Check(initialize(), "initialize default snapshot without source TSV");
        translation("猫", "고양이", "default snapshot takes precedence over TSV");
        translation("犬", "새사전", "snapshot keeps independent user discovery");
        reload(true);
        translation("猫", "고양이", "reload default snapshot without source TSV");
        Write(user / "USERDICT.TSV", "犬\t뒤파일\n");
        reload(true);
        translation("犬", "뒤파일", "snapshot reload reads independent user dictionaries");
        terminate();

        Write(config, "dicts: ../DiCtS\ncache: off\n");
        Check(initialize(), "configured TSV overrides adjacent snapshot");
        translation("猫", "야옹이", "configured dictionary precedence over snapshot");
        Write(data / "bad.hdsnap", "damaged snapshot\n");
        Write(config, "dicts: ../bad.hdsnap\ncache: off\n");
        reload(false);
        translation("猫", "야옹이", "bad configured snapshot reload retains dictionary");
        terminate();
        Check(!initialize(), "bad configured snapshot does not fall back");
        terminate();
        std::filesystem::remove(config, error);
        std::filesystem::remove(data / "bad.hdsnap", error);

        std::filesystem::remove_all(dicts, error);
        Check(initialize(), "initialize snapshot-only default layout");
        reload(true);
        translation("猫", "고양이", "snapshot-only default reload");
        translation("犬", "뒤파일", "snapshot-only default user discovery");
        terminate();
        std::filesystem::remove(snapshot, error);
        std::filesystem::remove_all(user, error);
        if (nested) std::filesystem::remove_all(data, error);
    }
    std::filesystem::current_path(oldCwd, error);
    Check(!error, "restore working directory");
}

#ifdef _WIN32
using InitializeFn = int (__stdcall*)(char*);
using TerminateFn = int (__stdcall*)();
using TranslateWFn = wchar_t* (__stdcall*)(HWND, const wchar_t*);
using FreeMemFn = void (__stdcall*)(void*);

std::wstring Wide(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (size) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

void CheckJ2KUnicode(InitializeFn initialize, TerminateFn terminate, TranslateWFn translate, FreeMemFn freeMem) {
    static_assert(sizeof(wchar_t) == 2);
    wchar_t* output = translate(nullptr, L"猫");
    Check(output == nullptr, "J2K W returns NULL before initialization");
    freeMem(output);
    Check(initialize(nullptr) == 1, "J2K Unicode fixture initialize");
    for (const auto& sample : kUnicodeSamples) {
        std::wstring input = Wide(sample.input);
        const std::wstring original = input;
        const std::wstring expected = Wide(sample.expected);
        output = translate(nullptr, input.c_str());
        Check(output && std::wstring(output) == expected, sample.message);
        Check(input == original, "J2K W does not modify the input");
        if (output) {
            Check(output != input.data() && HeapValidate(GetProcessHeap(), 0, output), "J2K W returns a process-heap block");
            freeMem(output);
        }
    }
    output = translate(nullptr, nullptr);
    Check(output == nullptr, "J2K W rejects NULL input");
    freeMem(output);
    constexpr const wchar_t* invalid[] = {L"\xd800", L"\xdc00", L"\xd800" L"A" L"\xdc00"};
    constexpr const wchar_t* replaced[] = {L"\xfffd", L"\xfffd", L"\xfffd" L"A" L"\xfffd"};
    for (std::size_t index = 0; index < 3; ++index) {
        output = translate(nullptr, invalid[index]);
        Check(output && std::wstring(output) == replaced[index], "J2K W replaces unpaired surrogates with U+FFFD");
        freeMem(output);
    }
    constexpr wchar_t terminated[] = {L'猫', 0, L'犬', 0};
    output = translate(nullptr, terminated);
    Check(output && std::wstring(output) == L"고양이", "J2K W input ends at the first NUL");
    freeMem(output);
    wchar_t* first = translate(nullptr, L"猫");
    wchar_t* second = translate(nullptr, L"猫");
    Check(first && second && first != second, "J2K W returns independent allocations");
    Check(terminate() == 1, "J2K Unicode fixture terminate");
    Check(first && std::wstring(first) == L"고양이" && second && std::wstring(second) == L"고양이",
          "J2K W buffers remain host-owned after another call and Terminate");
    freeMem(first);
    if (second != first) freeMem(second);
    output = translate(nullptr, L"猫");
    Check(output == nullptr, "J2K W returns NULL after Terminate");
    freeMem(output);
    freeMem(nullptr);
}

template<class Function>
Function Procedure(HMODULE module, const char* name) {
    const FARPROC address = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

void CheckJ2K(const std::filesystem::path& source, const std::filesystem::path& folder) {
    std::error_code error;
    const auto dll = folder / "J2KEngine.dll";
    std::filesystem::copy_file(source, dll, std::filesystem::copy_options::overwrite_existing, error);
    Check(!error, "copy J2K DLL to isolated config folder");
    if (error) return;
    const HMODULE module = LoadLibraryW(dll.c_str());
    Check(module != nullptr, "load J2K config fixture");
    if (!module) return;
    const auto initialize = Procedure<InitializeFn>(module, "J2K_Initialize");
    const auto reload = Procedure<void (__stdcall*)()>(module, "J2K_ReloadUserDict");
    const auto terminate = Procedure<TerminateFn>(module, "J2K_Terminate");
    const auto translate = Procedure<TranslateWFn>(module, "J2K_TranslateMMNTW");
    const auto freeMem = Procedure<FreeMemFn>(module, "J2K_FreeMem");
    Check(initialize && reload && terminate && translate && freeMem, "J2K config exports");
    if (initialize && reload && terminate && translate && freeMem) {
        const auto config = folder / "hdtrans.config.yaml";
        const std::string base = "dicts: fixtures/first\nuserdict: ''\ncache: off\n";
        auto checkTranslation = [&](const wchar_t* input, const wchar_t* expected, const char* message) {
            wchar_t* output = translate(nullptr, input);
            Check(output && std::wstring(output) == expected, message);
            freeMem(output);
        };
        Write(config, base);
        CheckJ2KUnicode(initialize, terminate, translate, freeMem);
        Check(initialize(nullptr) == 1, "J2K config initialize");
        checkTranslation(L"猫", L"고양이", "J2K ignores sibling Ehnd by default");
        Write(config, base + "ehnd: true\n");
        reload();
        checkTranslation(L"猫", L"검은고양이", "J2K enables sibling Ehnd on reload");
        Write(config, base + "ehnd: false\n");
        reload();
        checkTranslation(L"猫", L"고양이", "J2K disables Ehnd on reload");
        Write(config, base + "ehnd: fixtures/Ehnd\n");
        reload();
        checkTranslation(L"タマ", L"나비", "J2K configured Ehnd path");
        const auto userRows = folder / "fixtures/j2k-user.txt";
        const std::string rowConfig = "dicts: fixtures/first\nuserdict: fixtures/j2k-user.txt\ncache: off\n";
        Write(userRows, "犬\tJ2K_OLD\n");
        Write(config, rowConfig);
        reload();
        checkTranslation(L"犬", L"J2K_OLD", "J2K loads valid user row");
        Write(userRows, "犬\tJ2K_NEW\n猫\n");
        Write(config, rowConfig + "strict_dict: true\n");
        reload();
        checkTranslation(L"犬", L"J2K_OLD", "J2K strict YAML retains old dictionary");
        Write(config, rowConfig + "strict_dict: false\n");
        reload();
        checkTranslation(L"犬", L"J2K_NEW", "J2K rereads skip YAML and keeps good rows");
        terminate();
        Write(config, rowConfig + "strict_dict: true\n");
        Check(initialize(nullptr) == 1, "J2K strict initialization excludes the bad file");
        checkTranslation(L"猫", L"고양이", "J2K strict cold load keeps the base");
        wchar_t* excluded = translate(nullptr, L"犬");
        Check(excluded && std::wstring(excluded) != L"J2K_NEW", "J2K excludes good rows from the malformed file");
        freeMem(excluded);
        terminate();
        Write(config, rowConfig);
        Check(initialize(nullptr) == 1, "J2K default initialization skips bad row");
        checkTranslation(L"犬", L"J2K_NEW", "J2K default keeps good rows from mixed file");
        Check(std::filesystem::remove(userRows, error) && !error, "remove accepted J2K user file");
        reload();
        checkTranslation(L"犬", L"J2K_NEW", "J2K missing source keeps old user dictionary");
        Write(userRows, "犬\tJ2K_RESTORED\n");
        reload();
        checkTranslation(L"犬", L"J2K_RESTORED", "J2K restored source reloads");
        const auto renamedRows = folder / "fixtures/j2k-renamed.txt";
        std::filesystem::remove(renamedRows, error);
        error.clear();
        std::filesystem::rename(userRows, renamedRows, error);
        Check(!error, "rename accepted J2K user file");
        Write(config, "dicts: fixtures/first\nuserdict: fixtures/j2k-renamed.txt\ncache: off\n");
        reload();
        checkTranslation(L"犬", L"J2K_RESTORED", "J2K changed requested path permits rename");
        const auto coldUser = userRows.parent_path() / "cold-bad.txt";
        const auto coldFilter = userRows.parent_path() / "cold-ehnd/PreFilter_cold.txt";
        const std::string coldConfig = "dicts: fixtures/first\nuserdict: [fixtures/cold-good.txt, fixtures/cold-bad.txt]\n"
                                       "ehnd: fixtures/cold-ehnd\ncache: off\n";
        terminate();
        Write(config, "dicts: fixtures/first\nuserdict: ''\nehnd: ''\ncache: off\nstrict_dict: true\n");
        Check(initialize(nullptr) == 1, "J2K initializes a base-only engine with empty optional history");
        Write(coldUser, "鳥\tEXCLUDED\n犬\t개\tzzz\n");
        Write(config, "dicts: fixtures/second\nuserdict: fixtures/cold-bad.txt\nehnd: ''\ncache: off\nstrict_dict: true\n");
        reload();
        checkTranslation(L"猫", L"고양이", "J2K empty optional history still retains the old base on failure");
        const struct ColdCase { const char* user; const char* filter; bool strict; } coldCases[] = {
            {"鳥\tEXCLUDED\n犬\t개\tzzz\n", "// empty\n", true},
            {"鳥\tEXCLUDED\n犬\t\xB0\xED\n", "// empty\n", false},
            {"鳥\tCOLD_USER\n", "title\n猫\t犬\t1\t0\n(\tbad\t1\t1\n", true},
        };
        for (const auto& test : coldCases) {
            terminate();
            const std::string strict = test.strict ? "strict_dict: true\n" : "strict_dict: false\n";
            Write(config, coldConfig + strict);
            Write(coldUser, test.user);
            Write(coldFilter, test.filter);
            Check(initialize(nullptr) == 1, "J2K cold bad POS, decode or Ehnd file is excluded");
            checkTranslation(L"猫", L"고양이", "J2K cold exclusion keeps the base");
            checkTranslation(L"犬", L"COLD_KEEP", "J2K cold exclusion keeps the other user file");
            Write(coldUser, "鳥\tCOLD_USER\n");
            Write(coldFilter, "// empty\n");
            reload();
            checkTranslation(L"鳥", L"COLD_USER", "J2K repaired file becomes active");
            Write(coldUser, test.user);
            Write(coldFilter, test.filter);
            std::string changed = coldConfig;
            changed.replace(changed.find("fixtures/first"), std::strlen("fixtures/first"), "fixtures/second");
            Write(config, changed + strict);
            reload();
            checkTranslation(L"猫", L"고양이", "J2K failed active load retains the old base");
            checkTranslation(L"鳥", L"COLD_USER", "J2K failed active load retains the old user");
            checkTranslation(L"鳥", L"COLD_USER", "J2K unchanged retained generation remains usable");
            Write(config, coldConfig + strict);
            Write(coldUser, "鳥\tCOLD_USER\n");
            Write(coldFilter, "// empty\n");
            reload();
            checkTranslation(L"鳥", L"COLD_USER", "J2K recovery publishes a healthy generation");
        }
        Write(coldFilter, "title\n猫\t犬\t1\t0\n");
        Write(config, coldConfig + "strict_dict: true\n");
        reload();
        checkTranslation(L"猫", L"COLD_KEEP", "J2K ignored title preserves the following valid filter row");
        terminate();
        std::filesystem::remove(config, error);
        std::filesystem::create_directories(folder / "dicts", error);
        std::filesystem::copy_file(folder / "fixtures/first/base.tsv", folder / "dicts/base.tsv",
                                   std::filesystem::copy_options::overwrite_existing, error);
        Check(initialize(nullptr) == 1, "J2K initialize without config");
        checkTranslation(L"猫", L"고양이", "J2K without config ignores sibling Ehnd");
        terminate();
        std::filesystem::remove_all(folder / "dicts", error);
        const auto data = folder / "HDTRANS";
        const auto user = data / "USER";
        std::filesystem::create_directories(data / "DiCtS", error);
        std::filesystem::create_directories(user, error);
        std::filesystem::copy_file(folder / "fixtures/first/base.tsv", data / "DiCtS/base.tsv",
                                   std::filesystem::copy_options::overwrite_existing, error);
        Write(user / "HDTRANS.CONFIG.YAML", "cache: off\n");
        Write(user / "USERDICT.TSV", "犬\t앞파일\n");
        Write(user / "UserDict_z.TxT", "犬\t새사전\n");
        Check(initialize(nullptr) == 1, "J2K nested case-insensitive layout");
        checkTranslation(L"猫", L"고양이", "J2K nested default dictionary");
        checkTranslation(L"犬", L"새사전", "J2K nested user dictionaries in filename order");
        Write(user / "userdict-bad.txt", "猫\n");
        Write(user / "UserDict_z.TxT", "犬\t뒤파일\n");
        reload();
        checkTranslation(L"犬", L"뒤파일", "J2K reload skips bad file and loads other user files");
        Write(user / "HDTRANS.CONFIG.YAML", "ehnd: true\ncache: off\n");
        reload();
        checkTranslation(L"猫", L"검은고양이", "J2K nested config keeps outer Ehnd location");
        terminate();
        std::filesystem::remove_all(data, error);
        CheckDefaultLayouts(folder, [&] { return initialize(nullptr) == 1; },
            [&](bool) { reload(); }, [&] { terminate(); },
            [&](const std::string& input, const std::string& expected, const char* message) {
                checkTranslation(Wide(input).c_str(), Wide(expected).c_str(), message);
            });
    }
    FreeLibrary(module);
    std::filesystem::remove(dll, error);
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc != 2) return 2;
#else
    if (argc != 1) return 2;
#endif
    std::error_code error;
    const auto folder = std::filesystem::canonical(argv[0], error).parent_path();
    if (error) return 2;
    const auto config = folder / "hdtrans.config.yaml";
    if (std::filesystem::exists(config, error)) return 2;
    const auto fixtures = folder / "fixtures";
    std::filesystem::create_directories(folder / "Ehnd", error);
    Write(folder / "Ehnd/PostFilter_test.txt", "고양이\t검은고양이\t1\t0\n");
    std::filesystem::create_directories(fixtures / "first", error);
    std::filesystem::create_directories(fixtures / "second", error);
    std::filesystem::create_directories(fixtures / "Ehnd", error);
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n";
    Write(fixtures / "first/base.tsv", header + "猫\t고양이\tN110\t\t\tGN\t\t\t\t\t1\n");
    Write(fixtures / "second/base.tsv", header + "猫\t야옹이\tN110\t\t\tGN\t\t\t\t\t1\n");
    Write(fixtures / "user.tsv", "犬\t멍멍이\n");
    Write(fixtures / "Ehnd/UserDict_test.txt", "タマ\t나비\t1\n");
    Write(config, "dicts: fixtures/first\nuserdict: fixtures/user.tsv\nehnd: fixtures/Ehnd\ncache: off\n"
                  "cg: false\nmisread: true\nreference: false\n");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize with config dictionaries");
    Check(Translate("猫") == "고양이", "configured dictionary");
    for (const auto& sample : kUnicodeSamples) {
        const std::string result = Translate(sample.input);
        if (result != sample.expected) {
            std::fprintf(stderr, "UTF-8 expected [%s], got [%s]\n", sample.expected, result.c_str());
        }
        Check(result == sample.expected, sample.message);
    }
    Check(Translate("犬") == "멍멍이", "configured user dictionary");
    Check(Translate("タマ") == "나비", "configured Ehnd dictionary");
    Check(Errors().empty(), "valid config diagnostics");
    Write(fixtures / "second-user.txt", "犬\t뒤파일\n");
    Write(fixtures / "bad-user.tsv", "猫\n");
    Write(config, "dicts: fixtures/first\nuserdict: [fixtures/user.tsv, fixtures/bad-user.tsv, fixtures/second-user.txt]\ncache: off\n");
    Check(hdTrans_Reload() == -1, "default strict rejects one bad user file");
    Check(Translate("犬") == "멍멍이", "bad user file keeps old dictionary");
    Check(Errors().find("bad-user.tsv:1: empty KR") != std::string::npos, "failed user file diagnostics");
    Check(SetStrict(2) == 0, "skip malformed user rows");
    Check(hdTrans_Reload() == 0, "reload multiple user dictionaries with row warning");
    Check(Translate("犬") == "뒤파일", "later configured user file wins");
    Check(Errors().find("bad-user.tsv:1: empty KR") != std::string::npos, "bad user row diagnostics");
    Check(hdTrans_SetOptions(nullptr) == 0, "restore default dictionary options");
    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: ''\ncache: off\nunknown: value\n");
    Check(hdTrans_Reload() == 0, "reload config");
    Check(Translate("猫") == "야옹이", "reloaded dictionary");
    Check(Errors().find("hdtrans.config.yaml:5: unknown key: unknown") != std::string::npos, "config diagnostic path and line");
    Write(config, "dicts: fixtures/missing\ncache: off\n");
    Check(hdTrans_Reload() == -1, "failed reload");
    Check(Translate("猫") == "야옹이", "failed reload retains dictionaries");
    CheckDictionaryOptions(config, fixtures / "options-user.txt");
    CheckColdExclusions(config, fixtures);
    CheckAcceptedUserSources(config, fixtures);
    std::filesystem::create_directories(fixtures / "speech", error);
    Write(fixtures / "speech/base.tsv", header + "高\t$D3@\tH7N0\t\t\tGN\t\t\t\t\t1\nい\t1다\tT500\t\t\tGN\t\t\t\t\t2\n");
    Write(config, "dicts: fixtures/speech\nuserdict: ''\nehnd: ''\ncache: off\n");
    Check(hdTrans_Reload() == 0, "reload speech dictionary");
    Check(Translate("「高い」") == "「높다」", "quoted speech is strict by default");
    Write(config, "dicts: fixtures/speech\nuserdict: ''\nehnd: ''\ncache: off\nspeech: soft\n");
    Check(hdTrans_Reload() == 0, "reload soft speech");
    Check(Translate("「高い」") == "「높아」", "configured soft speech");
    Check(Translate("「高い。") == "「높아." && Translate("高い。」") == "높다.」", "a quote does not carry to the next call");
    Check(Translate("高い") == "높아" && Translate("高い。") == "높다.", "a bare line of speech is soft");
    Write(config, "dicts: fixtures/speech\nuserdict: ''\nehnd: ''\ncache: off\nspeech: strict\n");
    Check(hdTrans_Reload() == 0, "reload strict speech");
    Check(Translate("「高い」") == "「높다」", "configured strict speech");
    Write(config, "dicts: fixtures/speech\nuserdict: ''\nehnd: ''\ncache: off\nspeech: soft\nwrapped: true\n");
    Check(hdTrans_Reload() == 0, "reload wrapped");
    Check(Errors().empty(), "wrapped config diagnostics");
    const std::string opening = "「高い。";
    Check(TranslateContext(opening.data(), opening.size(), "高い。」") == "높아.」", "context lines give their quote");
    Check(TranslateContext(nullptr, 0, "高い。」") == "높다.」" && Translate("高い。」") == "높다.」",
          "no context is a plain call");
    Check(TranslateContext(opening.data(), 0, "高い。」") == "높다.」", "an empty context is none");
    const std::string lines = "高い\n高い\n";
    const std::string two = TranslateContext(lines.data(), lines.size(), "高い\n高い");
    Check(std::count(two.begin(), two.end(), '\n') == 1, "context keeps the line count");
    hdTrans_Terminate();
    {
        char* output = nullptr;
        std::size_t size = 0;
        Check(hdTrans_TranslateContext(nullptr, 0, "x", 1, &output, &size) == -1 && !output,
              "context translation fails when terminated");
    }

    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: ''\ncache: cache\n");
    std::string explicitDirs = Utf8(fixtures / "first");
    explicitDirs.append(2, '\0');
    Check(hdTrans_Initialize(explicitDirs.c_str()) == 0, "initialize explicit directories");
    Check(Translate("猫") == "고양이", "API directories override config; sibling Ehnd is opt-in");
    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: true\ncache: cache\n");
    Check(hdTrans_Reload() == 0, "enable sibling Ehnd on reload");
    Check(Translate("猫") == "검은고양이", "ehnd true uses sibling folder");
    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: false\ncache: cache\n");
    Check(hdTrans_Reload() == 0, "disable Ehnd on reload");
    Check(Translate("猫") == "고양이", "ehnd false disables sibling folder");
    std::filesystem::remove(config, error);
    Check(hdTrans_Reload() == 0, "reload without optional config");
    Check(Translate("猫") == "고양이", "no config preserves API dictionaries");
    Check(Errors().empty(), "removed config clears diagnostics");
    const char* cache = std::getenv("HDTRANS_CACHE_DIR");
    Check(cache && std::string(cache) == "off", "removed config restores cache environment");
    for (const bool missing : {false, true}) {
        if (missing) {
            Write(config, "userdict: fixtures/never-user.txt\nehnd: fixtures/never-ehnd\ncache: off\n");
            hdTrans_Terminate();
            Check(hdTrans_Initialize(explicitDirs.c_str()) == 0, "never-read explicit missing sources permit initialization");
            Check(Translate("猫") == "고양이", "optional missing sources preserve the base dictionary");
            const std::string warnings = Errors();
            Check(warnings.find("never-user.txt: the explicit user dictionary source is missing") != std::string::npos &&
                  warnings.find("never-ehnd") == std::string::npos,
                  "user error query contains only the missing user source warning");
            Check(warnings.find("기본 사전") == std::string::npos && warnings.find("Ehnd 활성화") == std::string::npos,
                  "source INFO is absent from the user error query");
        }
        char* output = nullptr;
        std::size_t size = 0;
        Check(hdTrans_FilterErrors(&output, &size) == 0, "query healthy or optional-missing filter diagnostics");
        const std::string filterErrors = output ? std::string(output, size) : std::string();
        Check(output && (missing ? filterErrors.find("never-ehnd") != std::string::npos : filterErrors.empty()),
              "only explicit missing Ehnd populates the filter error query");
        Check(filterErrors.find("never-user") == std::string::npos && filterErrors.find("Ehnd 활성화") == std::string::npos,
              "filter errors contain neither user-source warnings nor INFO");
        hdTrans_FreeMem(output);
    }
    std::filesystem::remove(config, error);
    hdTrans_Terminate();
    const auto data = folder / "HDTRANS";
    const auto user = data / "USER";
    std::filesystem::create_directories(data / "DiCtS", error);
    std::filesystem::create_directories(user, error);
    std::filesystem::copy_file(fixtures / "first/base.tsv", data / "DiCtS/base.tsv",
                               std::filesystem::copy_options::overwrite_existing, error);
    Write(user / "USERDICT.TSV", "犬\t새사전\n");
    Write(user / "HDTRANS.CONFIG.YAML", "dicts: ../DiCtS\ncache: off\n");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize case-insensitive nested layout");
    Check(Translate("猫") == "고양이", "nested dictionary path relative to user config");
    Check(Translate("犬") == "새사전", "case-insensitive nested user dictionary");
    hdTrans_Terminate();
    std::filesystem::remove_all(data, error);
    std::filesystem::create_directories(folder / "user", error);
    Write(folder / "user/hdtrans.config.yaml", "dicts: ../fixtures/first\ncache: off\n");
    Write(folder / "user/userdict.tsv", "犬\t새사전\n");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize flat user layout");
    Check(Translate("犬") == "새사전", "flat user dictionary");
    hdTrans_Terminate();
    std::filesystem::remove_all(folder / "user", error);
    CheckDefaultLayouts(folder, [] { return hdTrans_Initialize(nullptr) == 0; },
        [](bool expected) { Check((hdTrans_Reload() == 0) == expected, "C API default reload status"); },
        [] { hdTrans_Terminate(); },
        [](const std::string& input, const std::string& expected, const char* message) {
            Check(Translate(input) == expected, message);
        });
#ifdef _WIN32
    CheckJ2K(argv[1], folder);
#endif
    std::filesystem::remove_all(fixtures, error);
    std::filesystem::remove_all(folder / "Ehnd", error);
    return g_failures == 0 ? 0 : 1;
}
