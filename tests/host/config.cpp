#include "core/hdtrans.h"
#include "core/dict/snapshot.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

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

std::string Errors() {
    char* output = nullptr;
    std::size_t size = 0;
    Check(hdTrans_UserDictError(&output, &size) == 0, "diagnostics");
    const std::string result = output ? std::string(output, size) : std::string();
    hdTrans_FreeMem(output);
    return result;
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
    Check(hdTrans_Reload() == 0, "reload multiple user dictionaries with one bad file");
    Check(Translate("犬") == "뒤파일", "later configured user file wins");
    Check(Errors().find("bad-user.tsv:1: empty KR") != std::string::npos, "bad user file diagnostics");
    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: ''\ncache: off\nunknown: value\n");
    Check(hdTrans_Reload() == 0, "reload config");
    Check(Translate("猫") == "야옹이", "reloaded dictionary");
    Check(Errors().find("hdtrans.config.yaml:5: unknown key: unknown") != std::string::npos, "config diagnostic path and line");
    Write(config, "dicts: fixtures/missing\ncache: off\n");
    Check(hdTrans_Reload() == -1, "failed reload");
    Check(Translate("猫") == "야옹이", "failed reload retains dictionaries");
    std::filesystem::create_directories(fixtures / "speech", error);
    Write(fixtures / "speech/base.tsv", header + "高\t$D3@\tH7N0\t\t\tGN\t\t\t\t\t1\nい\t1다\tT500\t\t\tGN\t\t\t\t\t2\n");
    Write(config, "dicts: fixtures/speech\nuserdict: ''\nehnd: ''\ncache: off\n");
    Check(hdTrans_Reload() == 0, "reload speech dictionary");
    Check(Translate("「高い」") == "「높아」", "quoted speech is soft by default");
    Check(Translate("「高い。") == "「높아." && Translate("高い」") == "높아」", "a quote carries to the next call");
    Write(config, "dicts: fixtures/speech\nuserdict: ''\nehnd: ''\ncache: off\nspeech: strict\n");
    Check(hdTrans_Reload() == 0, "reload strict speech");
    Check(Translate("「高い」") == "「높다」", "configured strict speech");
    hdTrans_Terminate();

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
