#include <windows.h>

#include <barrier>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <thread>

namespace {

using InitializeExFn = int(__stdcall*)(const char*, const char*);
using TranslateFn = char*(__stdcall*)(int, const char*);
using TranslateWFn = wchar_t*(__stdcall*)(int, const wchar_t*);
using FreeMemFn = void(__stdcall*)(void*);
using FreeMemIntFn = int(__stdcall*)(char*);
using StopFn = int(__stdcall*)(int);
using TerminateFn = int(__stdcall*)();
using TranslateAddressFn = int(__stdcall*)(int, const char*);
using TranslateWideAddressFn = int(__stdcall*)(int, const wchar_t*);
using FreeAddressFn = int(__stdcall*)(int);

constexpr const char* kExports[] = {
    "J2K_FreeMem", "J2K_GetPriorDict", "J2K_GetProperty", "J2K_Initialize", "J2K_InitializeEx",
    "J2K_ReloadUserDict", "J2K_SetDelJPN", "J2K_SetField", "J2K_SetHnj2han", "J2K_SetJWin",
    "J2K_SetPriorDict", "J2K_SetProperty", "J2K_StopTranslation", "J2K_Terminate", "J2K_TranslateChat",
    "J2K_TranslateFM", "J2K_TranslateMM", "J2K_TranslateMMEx", "J2K_TranslateMMNT", "J2K_TranslateMMNTW",
};

struct Sample {
    const wchar_t* input;
    const wchar_t* expected;
};

constexpr Sample kSamples[] = {
    {L"こんにちは", L"안녕하세요"},
    {L"日本語", L"일본어"},
};

bool Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

template <class Fn>
Fn Export(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    if (!address) std::printf("FAIL missing export: %s\n", name);
    return reinterpret_cast<Fn>(address);
}

std::string Encode(const wchar_t* text, UINT codePage) {
    const int size = WideCharToMultiByte(codePage, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(codePage, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool TranslateWide(TranslateWFn translate, FreeMemFn freeMem) {
    bool ok = true;
    for (int round = 0; round < 16; ++round) {
        for (const auto& sample : kSamples) {
            wchar_t* output = translate(0, sample.input);
            const bool returned = output != nullptr;
            const std::wstring copied = output ? output : L"";
            if (output) freeMem(output);
            const bool matches = returned && copied == sample.expected;
            if (!matches) std::printf("FAIL UTF-16 translation at round %d\n", round);
            ok = matches && ok;
        }
    }
    return Check(ok, "UTF-16 results copied and released with J2K_FreeMem");
}

template <class Free>
bool TranslateAnsi(TranslateFn translate, Free freeMem) {
    bool ok = true;
    for (int round = 0; round < 16; ++round) {
        for (const auto& sample : kSamples) {
            const std::string input = Encode(sample.input, 932);
            const std::string expected = Encode(sample.expected, 949);
            char* output = translate(0, input.c_str());
            const bool returned = output != nullptr;
            const std::string copied = output ? output : "";
            if (output) freeMem(output);
            const bool matches = returned && !expected.empty() && expected == copied;
            if (!matches) std::printf("FAIL CP932/CP949 translation at round %d\n", round);
            ok = matches && ok;
        }
    }
    return Check(ok, "CP932 input, CP949 results copied and released with J2K_FreeMem");
}

bool RunNative(HMODULE module, const std::wstring& missingDat, bool ansi, bool narrowPath) {
    FARPROC addresses[20]{};
    bool found = true;
    for (size_t index = 0; index < 20; ++index) {
        addresses[index] = GetProcAddress(module, kExports[index]);
        if (index < 19 && !addresses[index]) {
            std::printf("FAIL missing export: %s\n", kExports[index]);
            found = false;
        }
    }
    if (!Check(found, "all 19 mandatory exports resolve")) return false;
    if (!ansi && !Check(addresses[19] != nullptr, "optional Unicode export selects the Unicode branch")) return false;
    if (ansi) std::puts("ANSI fallback forced; the shipped DLL normally selects Unicode");
    Sleep(50);
    std::string datPath;
    if (narrowPath) {
        for (wchar_t value : missingDat) datPath.push_back(static_cast<char>(value));
    } else {
        datPath = Encode(missingDat.c_str(), CP_ACP);
    }
    const auto initialize = reinterpret_cast<InitializeExFn>(addresses[4]);
    if (!Check(initialize("CSUSER123455", datPath.c_str()) != 0, "InitializeEx(key, missing Dat) loads DLL-adjacent dicts"))
        return false;
    const auto freeMem = reinterpret_cast<FreeMemFn>(addresses[0]);
    if (ansi) return TranslateAnsi(reinterpret_cast<TranslateFn>(addresses[18]), freeMem);
    return TranslateWide(reinterpret_cast<TranslateWFn>(addresses[19]), freeMem);
}

bool WriteBytes(const std::wstring& path, const std::string& bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(file);
    return ok;
}

bool TranslatesTo(TranslateWFn translate, FreeMemFn freeMem, const wchar_t* input, const wchar_t* expected) {
    wchar_t* output = translate(0, input);
    const std::wstring copied = output ? output : L"";
    if (output) freeMem(output);
    if (copied != expected) std::printf("  got \"%ls\" for \"%ls\"\n", copied.c_str(), input);
    return copied == expected;
}

// Anemone's dictionary: anedic.txt next to the executable, read only while the process
// shows Anemone's window. Anemone calls J2K_ReloadUserDict only when the file changes.
bool RunAnemoneDictionary(HMODULE module, bool window) {
    wchar_t exePath[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= static_cast<DWORD>(MAX_PATH)) return false;
    std::wstring aneDic(exePath);
    aneDic.resize(aneDic.find_last_of(L'\\'));
    aneDic += L"\\anedic.txt";
    bool ok = Check(WriteBytes(aneDic, "\xEF\xBB\xBF// comment\r\n" +
                                           Encode(L"こんにちは\t곤니치와\t0\r\n日本語\t니혼고\t1\r\n", CP_UTF8)),
                    "UTF-8 anedic.txt next to the executable");
    const auto initialize = Export<InitializeExFn>(module, "J2K_InitializeEx");
    const auto translate = Export<TranslateWFn>(module, "J2K_TranslateMMNTW");
    const auto freeMem = Export<FreeMemFn>(module, "J2K_FreeMem");
    const auto reload = Export<void(__stdcall*)()>(module, "J2K_ReloadUserDict");
    if (!initialize || !translate || !freeMem || !reload) return false;
    ok = Check(initialize("CSUSER123455", "C:\\missing\\Dat") != 0, "InitializeEx") && ok;
    if (!window) {
        ok = Check(TranslatesTo(translate, freeMem, L"こんにちは", L"안녕하세요"), "without Anemone's window anedic.txt is not read") && ok;
        reload();
        ok = Check(TranslatesTo(translate, freeMem, L"日本語", L"일본어"), "nor after J2K_ReloadUserDict") && ok;
        DeleteFileW(aneDic.c_str());
        return ok;
    }
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"AneParentClass";
    const HWND hwnd = RegisterClassW(&windowClass) ? CreateWindowExW(WS_EX_TOOLWINDOW, L"AneParentClass", L"",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, windowClass.hInstance, nullptr) : nullptr;
    ok = Check(hwnd != nullptr, "Anemone's window class after InitializeEx") && ok;
    ok = Check(TranslatesTo(translate, freeMem, L"こんにちは", L"곤니치와"), "the first translation reads anedic.txt") && ok;
    ok = Check(TranslatesTo(translate, freeMem, L"日本語", L"니혼고"), "noun row of anedic.txt") && ok;
    ok = Check(WriteBytes(aneDic, Encode(L"こんにちは\t안녕\t0\n", CP_UTF8)), "edited anedic.txt") && ok;
    reload();
    ok = Check(TranslatesTo(translate, freeMem, L"こんにちは", L"안녕"), "J2K_ReloadUserDict reads anedic.txt again") && ok;
    ok = Check(TranslatesTo(translate, freeMem, L"日本語", L"일본어"), "removed row no longer applies") && ok;
    DeleteFileW(aneDic.c_str());
    reload();
    ok = Check(TranslatesTo(translate, freeMem, L"こんにちは", L"안녕하세요"), "removed anedic.txt no longer applies") && ok;
    if (hwnd) DestroyWindow(hwnd);
    return ok;
}

bool RunServer(HMODULE module, const std::wstring& missingDat) {
    const auto initialize = Export<InitializeExFn>(module, "J2K_InitializeEx");
    if (!initialize) return false;
    const std::string datPath = Encode(missingDat.c_str(), CP_ACP);
    if (!Check(initialize("CSUSER123455", datPath.c_str()) != 0, "InitializeEx(key, missing Dat) loads DLL-adjacent dicts"))
        return false;
    const auto freeMem = Export<FreeMemFn>(module, "J2K_FreeMem");
    const auto translate = Export<TranslateWFn>(module, "J2K_TranslateMMNTW");
    if (!freeMem || !translate) return false;
    return TranslateWide(translate, freeMem);
}

struct AralEngine {
    HMODULE module = nullptr;
    InitializeExFn initialize = nullptr;
    TranslateFn translate = nullptr;
    FreeMemIntFn freeMem = nullptr;
    StopFn stop = nullptr;
    std::wstring datPath;
};

bool LoadAral(const std::wstring& dllPath, AralEngine& engine) {
    const size_t slash = dllPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    engine.datPath = dllPath.substr(0, slash + 1) + L"Dat";
    for (wchar_t& value : engine.datPath) {
        if (value == L'/') value = L'\\';
    }
    if (!Check(GetFileAttributesW(engine.datPath.c_str()) == INVALID_FILE_ATTRIBUTES, "DLL-adjacent Dat does not exist"))
        return false;
    const std::wstring loadPath = engine.datPath + L"\\..\\J2KEngine.dll";
    engine.module = LoadLibraryW(loadPath.c_str());
    if (!Check(engine.module != nullptr, "LoadLibraryW through Dat\\..\\J2KEngine.dll without a Dat folder")) return false;
    engine.initialize = Export<InitializeExFn>(engine.module, "J2K_InitializeEx");
    engine.translate = Export<TranslateFn>(engine.module, "J2K_TranslateMMNT");
    engine.freeMem = Export<FreeMemIntFn>(engine.module, "J2K_FreeMem");
    engine.stop = Export<StopFn>(engine.module, "J2K_StopTranslation");
    const auto terminate = Export<TerminateFn>(engine.module, "J2K_Terminate");
    return engine.initialize && engine.translate && engine.freeMem && engine.stop && terminate;
}

bool TranslateAral(AralEngine& engine) {
    const std::string datPath = Encode(engine.datPath.c_str(), CP_ACP);
    const bool initialized = Check(engine.initialize("CSUSER123455", datPath.c_str()) != 0,
                                   "worker InitializeEx(key, ANSI Dat)");
    return TranslateAnsi(engine.translate, engine.freeMem) && initialized;
}

bool RunAral(const std::wstring& dllPath, bool splitThreads) {
    bool ok = false;
    if (splitThreads) {
        AralEngine callerEngine;
        if (!LoadAral(dllPath, callerEngine)) return false;
        std::thread worker([&callerEngine, &ok] { ok = TranslateAral(callerEngine); });
        worker.join();
        callerEngine.stop(0);
        std::puts("PASS caller StopTranslation(0) after the worker exits");
    } else {
        std::thread worker([&dllPath, &ok] {
            AralEngine workerEngine;
            if (!LoadAral(dllPath, workerEngine)) return;
            ok = TranslateAral(workerEngine);
            workerEngine.stop(0);
            std::puts("PASS worker StopTranslation(0)");
        });
        worker.join();
    }
    std::puts("No J2K_Terminate or FreeLibrary; module lifetime ends with the process");
    return ok;
}

#ifndef _WIN64
struct VnrEngine {
    HMODULE module = nullptr;
    TranslateAddressFn translate = nullptr;
    TranslateWideAddressFn translateW = nullptr;
    FreeAddressFn freeMem = nullptr;
    bool ansi = false;
};

bool TranslateVnr(VnrEngine& engine, const wchar_t* text, const char* sjis, const wchar_t* expected) {
    if (!engine.translate && !engine.translateW) {
        const auto wideAddress = GetProcAddress(engine.module, "J2K_TranslateMMNTW");
        if (!engine.ansi && wideAddress) {
            engine.translateW = reinterpret_cast<TranslateWideAddressFn>(wideAddress);
        } else {
            engine.translate = Export<TranslateAddressFn>(engine.module, "J2K_TranslateMMNT");
            if (!engine.translate) return false;
        }
    }
    const int address = engine.translateW ? engine.translateW(0, text) : engine.translate(0, sjis);
    if (!address) return false;
    const auto* raw = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(address)));
    bool matches;
    if (engine.translateW) {
        const std::wstring copied = static_cast<const wchar_t*>(raw);
        matches = expected ? copied == expected : !copied.empty();
    } else {
        const std::string copied = static_cast<const char*>(raw);
        matches = expected ? copied == Encode(expected, 949) : !copied.empty();
    }
    if (!engine.freeMem) engine.freeMem = Export<FreeAddressFn>(engine.module, "J2K_FreeMem");
    if (!engine.freeMem) return false;
    engine.freeMem(address);
    return matches;
}

bool RunVnr(const std::wstring& dllPath, bool ansi) {
    const size_t slash = dllPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    const std::wstring directory = dllPath.substr(0, slash);
    const DWORD pathSize = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring searchPath(pathSize, L'\0');
    if (pathSize) searchPath.resize(GetEnvironmentVariableW(L"PATH", searchPath.data(), pathSize));
    if (!searchPath.empty()) searchPath += L';';
    searchPath += directory;
    SetEnvironmentVariableW(L"PATH", searchPath.c_str());
    VnrEngine engine;
    engine.ansi = ansi;
    engine.module = LoadLibraryW(L"J2KEngine.dll");
    if (!Check(engine.module != nullptr, "bare-name J2KEngine.dll load with configured directory appended to PATH")) return false;
    HMODULE native = LoadLibraryW(L"J2KEngine.dlx");
    if (!native) native = LoadLibraryW(L"J2KEngine.dll");
    if (!Check(native == engine.module, "native-directory probe: .dlx first, then .dll")) return false;
    char modulePath[MAX_PATH]{};
    if (!GetModuleFileNameA(native, modulePath, MAX_PATH)) return false;
    const std::string originalPath = modulePath;
    const size_t originalSlash = originalPath.find_last_of("\\/");
    if (originalSlash == std::string::npos) return false;
    const std::string datPath = originalPath.substr(0, originalSlash + 1) + "Dat";
    if (!Check(GetFileAttributesA(datPath.c_str()) == INVALID_FILE_ATTRIBUTES, "native-directory Dat does not exist")) return false;
    const auto initialize = Export<InitializeExFn>(engine.module, "J2K_InitializeEx");
    if (!initialize || !Check(initialize("CSUSER123455", datPath.c_str()) == 1, "InitializeEx(key, native-directory Dat) returns exactly 1"))
        return false;
    if (ansi) std::puts("ANSI fallback forced; Shift-JIS fixtures use the sjis codec, not CP932 extensions");
    bool ok = Check(TranslateVnr(engine, L"あ", "\x82\xa0", nullptr), "main-thread warmup Translate(0, text) and FreeMem");
    if (!ansi) ok = Check(engine.translateW != nullptr, "Unicode export selects the Unicode branch") && ok;
    bool workerOk = true;
    std::thread worker([&engine, &workerOk] {
        constexpr const char* inputs[] = {"\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd", "\x93\xfa\x96\x7b\x8c\xea"};
        for (int round = 0; round < 16; ++round) {
            for (size_t index = 0; index < 2; ++index) {
                workerOk = TranslateVnr(engine, kSamples[index].input, inputs[index], kSamples[index].expected) && workerOk;
            }
        }
    });
    worker.join();
    ok = Check(workerOk, "worker translations preserve c_int pointer bits and release original addresses") && ok;
    const auto terminate = Export<TerminateFn>(engine.module, "J2K_Terminate");
    if (!terminate) return false;
    terminate();
    std::puts("PASS explicit Loader.destroy: Terminate without StopTranslation or FreeLibrary");
    return ok;
}
#endif

bool RunWeb(HMODULE module) {
    const auto initialize = Export<InitializeExFn>(module, "J2K_InitializeEx");
    const auto translate = Export<TranslateWFn>(module, "J2K_TranslateMMNTW");
    if (!initialize || !translate) return false;
    constexpr char datPath[] = R"(C:\Program Files (x86)\ChangShinSoft\ezTrans XP\Dat)";
    if (!Check(GetFileAttributesA(datPath) == INVALID_FILE_ATTRIBUTES, "hardcoded Dat path does not exist")) return false;
    if (!Check(initialize("CSUSER123455", datPath) != 0, "InitializeEx with the hardcoded ANSI Dat path")) return false;
    bool correct[2]{true, true};
    std::thread workers[2];
    std::barrier<> start(2);
    for (size_t workerIndex = 0; workerIndex < 2; ++workerIndex) {
        workers[workerIndex] = std::thread([translate, &correct, &start, workerIndex] {
            start.arrive_and_wait();
            for (int round = 0; round < 16; ++round) {
                for (const auto& sample : kSamples) {
                    const wchar_t* output = translate(0, sample.input);
                    const std::wstring copied = output ? output : L"";
                    if (!output || copied != sample.expected) correct[workerIndex] = false;
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    std::puts("No J2K_FreeMem call; returned buffers remain allocated");
    return Check(correct[0] && correct[1], "two unlocked callers copy UTF-16 results concurrently");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::puts("usage: tool_compat <luna|luna-ansi|anemone|anemone-ansi|anemone-dict|anemone-nowindow|aral02|aral03|vnr|vnr-ansi|eztransserver|eztransweb> <DLL>");
        return 2;
    }
    wchar_t cwd[MAX_PATH]{};
    if (!GetCurrentDirectoryW(MAX_PATH, cwd)) return 1;
    const std::wstring missingDat = std::wstring(cwd) + L"\\missing\\Dat";
    if (!Check(GetFileAttributesW(missingDat.c_str()) == INVALID_FILE_ATTRIBUTES, "host Dat path does not exist")) return 1;
    if (!Check(GetFileAttributesW(L"dicts") == INVALID_FILE_ATTRIBUTES, "working directory has no dictionaries")) return 1;
    SetEnvironmentVariableW(L"HDTRANS_DICTS", L"missing-dictionary-override");
    if (std::wcscmp(argv[1], L"aral02") == 0 || std::wcscmp(argv[1], L"aral03") == 0) {
        return RunAral(argv[2], std::wcscmp(argv[1], L"aral03") == 0) ? 0 : 1;
    }
#ifndef _WIN64
    if (std::wcscmp(argv[1], L"vnr") == 0 || std::wcscmp(argv[1], L"vnr-ansi") == 0) {
        return RunVnr(argv[2], std::wcscmp(argv[1], L"vnr-ansi") == 0) ? 0 : 1;
    }
#endif
    const HMODULE module = LoadLibraryW(argv[2]);
    if (!Check(module != nullptr, "LoadLibraryW")) return 1;
    bool ok = false;
    if (std::wcscmp(argv[1], L"luna") == 0 || std::wcscmp(argv[1], L"luna-ansi") == 0) {
        ok = RunNative(module, missingDat, std::wcscmp(argv[1], L"luna-ansi") == 0, false);
    } else if (std::wcscmp(argv[1], L"anemone") == 0 || std::wcscmp(argv[1], L"anemone-ansi") == 0) {
        ok = RunNative(module, missingDat, std::wcscmp(argv[1], L"anemone-ansi") == 0, true);
    } else if (std::wcscmp(argv[1], L"anemone-dict") == 0 || std::wcscmp(argv[1], L"anemone-nowindow") == 0) {
        ok = RunAnemoneDictionary(module, std::wcscmp(argv[1], L"anemone-dict") == 0);
    } else if (std::wcscmp(argv[1], L"eztransserver") == 0) {
        ok = RunServer(module, missingDat);
    } else if (std::wcscmp(argv[1], L"eztransweb") == 0) {
        ok = RunWeb(module);
    }
    std::puts("No J2K_Terminate or FreeLibrary; module lifetime ends with the process");
    return ok ? 0 : 1;
}
