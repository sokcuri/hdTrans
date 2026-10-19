/* Exercises J2KEngine.dll and sibling hdTransEngine.dll lifecycle and concurrent translation.
   Usage: j2k_host <path to J2KEngine.dll>
   Sample translations are written as UTF-8 to j2k_host.out.txt next to this executable. */
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "psapi.lib")

typedef int (__stdcall *InitializeExFn)(char*, char*);
typedef char* (__stdcall *TranslateMMNTFn)(HWND, char*);
typedef wchar_t* (__stdcall *TranslateMMNTWFn)(HWND, const wchar_t*);
typedef void (__stdcall *FreeMemFn)(void*);
typedef int (__stdcall *StopTranslationFn)(void*);
typedef int (__stdcall *TerminateFn)(void);
typedef void (__stdcall *ReloadUserDictFn)(void);

static const char* const kExports[] = {
    "J2K_Initialize", "J2K_InitializeEx", "J2K_Terminate", "J2K_TranslateMM", "J2K_TranslateMMEx",
    "J2K_TranslateMMNT", "J2K_TranslateMMNTW", "J2K_TranslateChat", "J2K_TranslateFM", "J2K_FreeMem",
    "J2K_StopTranslation", "J2K_ReloadUserDict", "J2K_SetField", "J2K_SetHnj2han", "J2K_SetDelJPN",
    "J2K_SetJWin", "J2K_SetPriorDict", "J2K_GetPriorDict", "J2K_SetProperty", "J2K_GetProperty",
    "J2K_GetJ2KMainDir",
};

static const char* const kLines[] = {
    "\xE7\xA7\x81\xE3\x81\xAF\xE5\xAD\xA6\xE7\x94\x9F\xE3\x81\xA7\xE3\x81\x99\xE3\x80\x82",
    "\xE4\xBB\x8A\xE6\x97\xA5\xE3\x81\xAF\xE5\xA4\xA9\xE6\xB0\x97\xE3\x81\x8C\xE3\x81\x84\xE3\x81\x84\xE3\x81\xA7\xE3\x81\x99\xE3\x81\xAD\xE3\x80\x82",
    "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\xE3\x80\x81\xE4\xB8\x96\xE7\x95\x8C\xE3\x80\x82",
};
#define LINE_COUNT (sizeof(kLines) / sizeof(kLines[0]))
static const wchar_t kWideLine[] = L"\x65E5\x672C\x8A9E\x3092\x52C9\x5F37\x3057\x3066\x3044\x307E\x3059\x3002";
/* The Shift-JIS lines, then the UTF-16 line. */
#define RESULT_COUNT (LINE_COUNT + 1)

static int g_failed;
static FILE* g_out;
static LARGE_INTEGER g_freq;

static void Check(int ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_failed = 1;
}

static double NowMs(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)g_freq.QuadPart;
}

typedef struct {
    int calls;
    double ms;
} Timing;

static void PrintTiming(const Timing* t) {
    printf("translate: %d calls, total %.2f ms\n", t->calls, t->ms);
}

static void PrintMemory(void) {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        printf("memory query failed\n");
        return;
    }
    printf("peak working set %.1f MB\n", pmc.PeakWorkingSetSize / 1048576.0);
    printf("private bytes %.1f MB\n", pmc.PrivateUsage / 1048576.0);
}

typedef struct {
    HMODULE module;
    InitializeExFn initializeEx;
    TranslateMMNTFn translate;
    TranslateMMNTWFn translateW;
    FreeMemFn freeMem;
    StopTranslationFn stop;
    TerminateFn terminate;
    ReloadUserDictFn reloadUserDict;
} Engine;

static int LoadEngine(Engine* e, const wchar_t* path) {
    memset(e, 0, sizeof(*e));
    e->module = LoadLibraryW(path);
    if (!e->module) return 0;
    e->initializeEx = (InitializeExFn)GetProcAddress(e->module, "J2K_InitializeEx");
    e->translate = (TranslateMMNTFn)GetProcAddress(e->module, "J2K_TranslateMMNT");
    e->translateW = (TranslateMMNTWFn)GetProcAddress(e->module, "J2K_TranslateMMNTW");
    e->freeMem = (FreeMemFn)GetProcAddress(e->module, "J2K_FreeMem");
    e->stop = (StopTranslationFn)GetProcAddress(e->module, "J2K_StopTranslation");
    e->terminate = (TerminateFn)GetProcAddress(e->module, "J2K_Terminate");
    e->reloadUserDict = (ReloadUserDictFn)GetProcAddress(e->module, "J2K_ReloadUserDict");
    if (e->initializeEx && e->translate && e->translateW && e->freeMem && e->stop && e->terminate &&
        e->reloadUserDict) return 1;
    FreeLibrary(e->module);
    memset(e, 0, sizeof(*e));
    return 0;
}

/* Converts a UTF-8 string to a newly allocated string in the given code page. */
static char* FromUtf8(const char* s, UINT codePage) {
    int wn = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, wn * sizeof(wchar_t));
    int n;
    char* out;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wn);
    n = WideCharToMultiByte(codePage, 0, w, -1, NULL, 0, NULL, NULL);
    out = (char*)HeapAlloc(GetProcessHeap(), 0, n);
    WideCharToMultiByte(codePage, 0, w, -1, out, n, NULL, NULL);
    HeapFree(GetProcessHeap(), 0, w);
    return out;
}

/* Converts a string in the given code page to a newly allocated UTF-16 string. */
static wchar_t* WideFrom(const char* s, UINT codePage) {
    int wn = MultiByteToWideChar(codePage, 0, s, -1, NULL, 0);
    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, wn * sizeof(wchar_t));
    MultiByteToWideChar(codePage, 0, s, -1, w, wn);
    return w;
}

static wchar_t* DupWide(const wchar_t* s) {
    size_t n = wcslen(s) + 1;
    wchar_t* d = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, n * sizeof(wchar_t));
    memcpy(d, s, n * sizeof(wchar_t));
    return d;
}

/* Writes <dir of path><leaf> into out; leaf alone when path has no directory part. */
static void SiblingPath(const wchar_t* path, const wchar_t* leaf, wchar_t* out, size_t cap) {
    const wchar_t* sep = wcsrchr(path, L'\\');
    const wchar_t* alt = wcsrchr(path, L'/');
    size_t dirLen = 0;
    if (alt && (!sep || alt > sep)) sep = alt;
    if (sep) dirLen = (size_t)(sep - path) + 1;
    _snwprintf(out, cap, L"%.*s%s", (int)dirLen, path, leaf);
    out[cap - 1] = L'\0';
}

static int DirExists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int FileExists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int ShippedPath(const wchar_t* module, wchar_t* out, size_t cap) {
    wchar_t data[MAX_PATH];
    SiblingPath(module, L"hdTrans", data, MAX_PATH);
    if (DirExists(data)) {
        SiblingPath(module, L"hdTrans\\dicts.hdsnap", out, cap);
        if (FileExists(out)) return 1;
        SiblingPath(module, L"hdTrans\\dicts", out, cap);
    } else {
        SiblingPath(module, L"dicts.hdsnap", out, cap);
        if (FileExists(out)) return 1;
        SiblingPath(module, L"dicts", out, cap);
    }
    return DirExists(out);
}

/* Prints a translation to stdout and to the output file, one line per translation. */
static void Emit(const char* label, const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char* u = (char*)HeapAlloc(GetProcessHeap(), 0, n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, n, NULL, NULL);
    printf("  %s: %s\n", label, u);
    if (g_out) fprintf(g_out, "%s\n", u);
    HeapFree(GetProcessHeap(), 0, u);
}

static void FreeResults(wchar_t* results[RESULT_COUNT]) {
    size_t i;
    for (i = 0; i < RESULT_COUNT; i++) {
        if (results[i]) HeapFree(GetProcessHeap(), 0, results[i]);
        results[i] = NULL;
    }
}

static int ContainsHangul(const wchar_t* w) {
    if (!w) return 0;
    for (; *w; w++) {
        if (*w >= 0xAC00 && *w <= 0xD7A3) return 1;
    }
    return 0;
}

static int AllContainHangul(wchar_t* results[RESULT_COUNT]) {
    size_t i;
    for (i = 0; i < RESULT_COUNT; i++) {
        if (!ContainsHangul(results[i])) return 0;
    }
    return 1;
}

/* Translates every line into results (each a newly allocated UTF-16 string, or NULL). */
static void TranslateAll(Engine* e, Timing* timing, wchar_t* results[RESULT_COUNT]) {
    size_t i;
    wchar_t* w;
    double t0;
    for (i = 0; i < LINE_COUNT; i++) {
        char* sjis = FromUtf8(kLines[i], 932);
        char* result;
        t0 = NowMs();
        result = e->translate(NULL, sjis);
        timing->ms += NowMs() - t0;
        timing->calls++;
        if (result) {
            results[i] = WideFrom(result, 949);
            Emit("TranslateMMNT", results[i]);
            e->freeMem(result);
        } else {
            results[i] = NULL;
            printf("  TranslateMMNT: NULL (not initialised)\n");
        }
        HeapFree(GetProcessHeap(), 0, sjis);
    }
    t0 = NowMs();
    w = e->translateW(NULL, kWideLine);
    timing->ms += NowMs() - t0;
    timing->calls++;
    if (w) {
        results[LINE_COUNT] = DupWide(w);
        Emit("TranslateMMNTW", results[LINE_COUNT]);
        e->freeMem(w);
    } else {
        results[LINE_COUNT] = NULL;
        printf("  TranslateMMNTW: NULL (not initialised)\n");
    }
}

static int InitEngine(Engine* e) {
    char key[] = "CSUSER123455";
    char path[] = "";
    double t0 = NowMs();
    int ret = e->initializeEx(key, path);
    printf("init %.2f ms\n", NowMs() - t0);
    return ret;
}

static void CheckTerminated(Engine* e) {
    char text[] = "text";
    char* result;
    wchar_t* wide;
    e->reloadUserDict();
    result = e->translate(NULL, text);
    wide = e->translateW(NULL, kWideLine);
    Check(result == NULL && wide == NULL, "reload and both translations are safe after Terminate");
    e->freeMem(result);
    e->freeMem(wide);
}

typedef int (__cdecl *CInitializeFn)(const char*);
typedef void (__cdecl *CTerminateFn)(void);
typedef int (__cdecl *CTranslateFn)(const char*, size_t, char**, size_t*);
typedef int (__cdecl *CReloadFn)(void);
typedef void (__cdecl *CFreeMemFn)(void*);

typedef struct {
    CInitializeFn initialize;
    CTerminateFn terminate;
    CTranslateFn translate;
    CReloadFn reload;
    CFreeMemFn freeMem;
} CEngine;

static void CheckCTerminated(CEngine* e) {
    char* out = NULL;
    size_t n = 0;
    Check(e->reload() == -1, "C API Reload fails after Terminate");
    Check(e->translate(kLines[0], strlen(kLines[0]), &out, &n) == -1,
          "C API Translate fails after Terminate");
    e->freeMem(out);
}

static void CheckCTranslation(CEngine* e) {
    char* out = NULL;
    size_t n = 0;
    int ret = e->translate(kLines[0], strlen(kLines[0]), &out, &n);
    wchar_t* wide = out ? WideFrom(out, CP_UTF8) : NULL;
    Check(ret == 0 && out && n == strlen(out) && ContainsHangul(wide), "C API translates after Initialize");
    if (wide) HeapFree(GetProcessHeap(), 0, wide);
    e->freeMem(out);
}

static void RunCApi(const wchar_t* j2kPath, const wchar_t* dictsPath, int hasDicts) {
    wchar_t path[MAX_PATH];
    char dirs[MAX_PATH * 4 + 1];
    int n, round;
    SiblingPath(j2kPath, L"hdTransEngine.dll", path, MAX_PATH);
    n = WideCharToMultiByte(CP_UTF8, 0, dictsPath, -1, dirs, (int)sizeof(dirs) - 1, NULL, NULL);
    Check(n > 0, "C API dictionary path is UTF-8");
    if (n == 0) return;
    dirs[n] = '\0';
    for (round = 0; round < 20; round++) {
        HMODULE module = LoadLibraryW(path);
        CEngine e;
        int ret;
        Check(module != NULL, "load sibling C API DLL");
        if (!module) return;
        e.initialize = (CInitializeFn)GetProcAddress(module, "hdTrans_Initialize");
        e.terminate = (CTerminateFn)GetProcAddress(module, "hdTrans_Terminate");
        e.translate = (CTranslateFn)GetProcAddress(module, "hdTrans_Translate");
        e.reload = (CReloadFn)GetProcAddress(module, "hdTrans_Reload");
        e.freeMem = (CFreeMemFn)GetProcAddress(module, "hdTrans_FreeMem");
        Check(e.initialize && e.terminate && e.translate && e.reload && e.freeMem, "C API lifecycle exports resolve");
        if (!e.initialize || !e.terminate || !e.translate || !e.reload || !e.freeMem) {
            FreeLibrary(module);
            return;
        }
        if (round == 0) {
            e.terminate();
            e.terminate();
            CheckCTerminated(&e);
        }
        ret = e.initialize(round % 2 == 0 ? dirs : NULL);
        if (hasDicts) Check(ret == 0, "C API Initialize loads explicit and default dictionaries");
        if (ret == 0) {
            CheckCTranslation(&e);
            Check(e.reload() == 0, "C API reloads explicit and default dictionaries");
            CheckCTranslation(&e);
        }
        if (round == 0) {
            e.terminate();
            e.terminate();
            CheckCTerminated(&e);
            ret = e.initialize(NULL);
            if (hasDicts) Check(ret == 0, "C API reinitializes default dictionaries in the same load");
            if (ret == 0) CheckCTranslation(&e);
        }
        if (round % 2 == 0) {
            e.terminate();
            e.terminate();
            CheckCTerminated(&e);
        }
        Check(FreeLibrary(module) != 0, round % 2 == 0 ? "C API unload with Terminate" : "C API unload without Terminate");
    }
}

typedef struct {
    Engine* engine;
    int crashed;
    int iterations;
    Timing timing;
} ThreadJob;

static DWORD WINAPI ThreadMain(LPVOID param) {
    ThreadJob* job = (ThreadJob*)param;
    char* sjis = FromUtf8(kLines[1], 932);
    int i;
    double t0;
    for (i = 0; i < job->iterations; i++) {
        char* result;
        t0 = NowMs();
        result = job->engine->translate(NULL, sjis);
        job->timing.ms += NowMs() - t0;
        job->timing.calls++;
        if (result) job->engine->freeMem(result);
    }
    HeapFree(GetProcessHeap(), 0, sjis);
    return 0;
}

static int RunThreads(Engine* e, Timing* total) {
    ThreadJob jobs[2];
    HANDLE threads[2];
    int i;
    for (i = 0; i < 2; i++) {
        jobs[i].engine = e;
        jobs[i].crashed = 0;
        jobs[i].iterations = 200;
        jobs[i].timing.calls = 0;
        jobs[i].timing.ms = 0.0;
        threads[i] = CreateThread(NULL, 0, ThreadMain, &jobs[i], 0, NULL);
        if (!threads[i]) return 0;
    }
    if (WaitForMultipleObjects(2, threads, TRUE, 60000) != WAIT_OBJECT_0) return 0;
    for (i = 0; i < 2; i++) {
        CloseHandle(threads[i]);
        total->calls += jobs[i].timing.calls;
        total->ms += jobs[i].timing.ms;
    }
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    Engine e;
    Timing timing;
    wchar_t exePath[MAX_PATH], outPath[MAX_PATH], dictsPath[MAX_PATH];
    wchar_t* results[RESULT_COUNT];
    size_t i;
    int missing = 0, round, ret, hasDicts;
    if (argc < 2) {
        printf("usage: j2k_host <J2KEngine.dll>\n");
        return 2;
    }

    SetConsoleOutputCP(CP_UTF8);
    QueryPerformanceFrequency(&g_freq);
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    SiblingPath(exePath, L"j2k_host.out.txt", outPath, MAX_PATH);
    hasDicts = ShippedPath(argv[1], dictsPath, MAX_PATH);
    g_out = _wfopen(outPath, L"wb");
    Check(g_out != NULL, "open j2k_host.out.txt next to the executable");

    Check(LoadEngine(&e, argv[1]), "load DLL and resolve the host entry points");
    if (!e.module) return 1;
    for (i = 0; i < sizeof(kExports) / sizeof(kExports[0]); i++) {
        if (!GetProcAddress(e.module, kExports[i])) {
            printf("  missing export: %s\n", kExports[i]);
            missing++;
        }
    }
    Check(missing == 0, "every export of the .def resolves");
    if (!e.initializeEx) return 1;

    ret = InitEngine(&e);
    printf("  J2K_InitializeEx returned %d\n", ret);
    printf("memory after first init:\n");
    PrintMemory();
    Check(!hasDicts || ret == 1, "J2K_InitializeEx loads dictionaries when present");
    timing.calls = 0;
    timing.ms = 0.0;
    TranslateAll(&e, &timing, results);
    PrintTiming(&timing);
    Check(1, "translate Shift-JIS and UTF-16 lines");
    if (hasDicts) {
        Check(AllContainHangul(results), "every translation is non-empty and contains Hangul");
    } else {
        printf("SKIP translation content check: no dictionary next to the DLL\n");
    }
    FreeResults(results);
    e.reloadUserDict();
    Check(1, "J2K_ReloadUserDict returns");
    TranslateAll(&e, &timing, results);
    if (hasDicts) Check(AllContainHangul(results), "translations work after reloading dictionaries");
    FreeResults(results);
    Check(e.stop(NULL) || 1, "J2K_StopTranslation(NULL) returns");
    ret = e.terminate();
    printf("  J2K_Terminate returned %d\n", ret);
    Check(ret == 1, "J2K_Terminate succeeds");
    Check(e.terminate() == 1, "J2K_Terminate succeeds twice");
    CheckTerminated(&e);
    ret = InitEngine(&e);
    if (hasDicts) Check(ret == 1, "reinitialize in the same DLL load");
    timing.calls = 0;
    timing.ms = 0.0;
    TranslateAll(&e, &timing, results);
    if (hasDicts) Check(AllContainHangul(results), "translations work after reinitializing");
    FreeResults(results);
    Check(FreeLibrary(e.module) != 0, "FreeLibrary after reinitializing (no Terminate)");

    for (round = 0; round < 20; round++) {
        wchar_t* result;
        Check(LoadEngine(&e, argv[1]), "load DLL for lifecycle cycle");
        if (!e.module) return 1;
        ret = InitEngine(&e);
        if (hasDicts) Check(ret == 1, "lifecycle Initialize loads dictionaries");
        result = e.translateW(NULL, kWideLine);
        if (ret == 1) Check(ContainsHangul(result), "lifecycle translation succeeds");
        e.freeMem(result);
        if (round % 2 == 0) {
            Check(e.terminate() == 1, "lifecycle Terminate succeeds");
            CheckTerminated(&e);
        }
        Check(FreeLibrary(e.module) != 0, round % 2 == 0 ? "unload with Terminate" : "unload without Terminate");
    }

    for (round = 1; round <= 2; round++) {
        char what[64];
        Check(LoadEngine(&e, argv[1]), "load DLL for the concurrent run");
        if (!e.module) return 1;
        InitEngine(&e);
        sprintf(what, "two threads translating concurrently, round %d", round);
        timing.calls = 0;
        timing.ms = 0.0;
        Check(RunThreads(&e, &timing), what);
        PrintTiming(&timing);
        e.terminate();
        Check(FreeLibrary(e.module) != 0, "FreeLibrary after the concurrent run");
    }

    RunCApi(argv[1], dictsPath, hasDicts);

    printf("memory at end:\n");
    PrintMemory();
    if (g_out) fclose(g_out);
    printf(g_failed ? "RESULT FAIL\n" : "RESULT PASS\n");
    return g_failed ? 1 : 0;
}
