#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <memory>
#include <optional>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "core/config/cache.h"
#include "core/config/config.h"
#include "core/dict/folders.h"
#include "core/engine.h"
#include "frontend/module_dir.h"
#include "gui/resource.h"
#include "gui/text_file.h"
#include "gui/translation.h"

namespace {

namespace fs = std::filesystem;
namespace gui = hdTrans::gui;
constexpr UINT kDone = WM_APP + 1;
constexpr UINT kPhase = WM_APP + 2;
constexpr UINT kFont = WM_APP + 3;
constexpr UINT kStart = WM_APP + 4;

struct FileJob {
    fs::path input;
    fs::path output;
};

struct App {
    HINSTANCE instance = nullptr;
    HWND main = nullptr;
    HWND file = nullptr;
    HWND progress = nullptr;
    HFONT mainFont = nullptr;
    HFONT fileFont = nullptr;
    HFONT progressFont = nullptr;
    bool selftest = false;
    bool busy = false;
    bool closing = false;
    hdTrans::internal::Config settings;
    hdTrans::internal::ConfigCache cache;
    std::unique_ptr<hdTrans::Engine> engine;
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::string result;
    std::wstring error;
    bool wasCancelled = false;
    std::vector<fs::path> inputFiles;
    fs::path outputFolder;
    bool sameFolder = true;
    std::vector<FileJob> jobs;
    std::size_t savedCount = 0;
    std::size_t skippedCount = 0;
    std::vector<std::wstring> failed;
    int overwriteAnswer = IDYES;

    ~App() {
        cancel = true;
        if (worker.joinable()) worker.join();
        engine.reset();
        static_cast<void>(cache.Apply(std::nullopt));
    }
};

INT_PTR CALLBACK MainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK FileProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ProgressProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

std::wstring Wide(std::string_view text) {
    if (text.empty() || text.size() > INT_MAX) return {};
    const int size = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
    if (count == 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, out.data(), count);
    return out;
}

std::string Utf8(std::wstring_view text) {
    if (text.empty() || text.size() > INT_MAX) return {};
    const int size = static_cast<int>(text.size());
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr);
    if (count == 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, out.data(), count, nullptr, nullptr);
    return out;
}

std::wstring ControlText(HWND window, int id) {
    const HWND control = GetDlgItem(window, id);
    const int count = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(count) + 1, L'\0');
    GetWindowTextW(control, text.data(), count + 1);
    text.resize(static_cast<std::size_t>(count));
    return text;
}

std::string SourceText(HWND window) {
    const std::wstring source = ControlText(window, IDC_SOURCE);
    std::wstring text;
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == L'\r') {
            text += L'\n';
            if (index + 1 < source.size() && source[index + 1] == L'\n') ++index;
        } else {
            text += source[index];
        }
    }
    return Utf8(text);
}

std::wstring EditText(std::string_view text) {
    std::wstring out;
    for (wchar_t ch : Wide(text)) {
        if (ch == L'\n' && (out.empty() || out.back() != L'\r')) out += L'\r';
        out += ch;
    }
    return out;
}

int CALLBACK FoundFont(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM data) {
    *reinterpret_cast<bool*>(data) = true;
    return 0;
}

BOOL CALLBACK SetChildFont(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    return TRUE;
}

void SetFont(HWND window, HFONT& font) {
    LOGFONTW spec{};
    spec.lfCharSet = DEFAULT_CHARSET;
    std::wcscpy(spec.lfFaceName, L"맑은 고딕");
    bool found = false;
    const HDC dc = GetDC(window);
    EnumFontFamiliesExW(dc, &spec, FoundFont, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(window, dc);
    if (!found) std::wcscpy(spec.lfFaceName, L"Segoe UI");
    spec.lfHeight = -MulDiv(9, static_cast<int>(GetDpiForWindow(window)), 72);
    const HFONT replacement = CreateFontIndirectW(&spec);
    if (!replacement) return;
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    EnumChildWindows(window, SetChildFont, reinterpret_cast<LPARAM>(replacement));
    if (font) DeleteObject(font);
    font = replacement;
}

void InitOptions(HWND window) {
    CheckRadioButton(window, IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_SPACED, IDC_OUTPUT_TRANSLATION);
    CheckDlgButton(window, IDC_SKIP_BLANK, BST_CHECKED);
    CheckDlgButton(window, IDC_WHOLE_DOCUMENT, BST_CHECKED);
}

gui::TranslationOptions Options(HWND window, bool document) {
    gui::TranslationOptions options;
    if (IsDlgButtonChecked(window, IDC_OUTPUT_INTERLEAVED) == BST_CHECKED) options.output = gui::OutputMode::Interleaved;
    if (IsDlgButtonChecked(window, IDC_OUTPUT_SPACED) == BST_CHECKED) options.output = gui::OutputMode::Spaced;
    options.skipBlankLines = IsDlgButtonChecked(window, IDC_SKIP_BLANK) == BST_CHECKED;
    options.wholeDocument = document || IsDlgButtonChecked(window, IDC_WHOLE_DOCUMENT) == BST_CHECKED;
    return options;
}

void ReportError(App& app, HWND owner, const wchar_t* message) {
    if (app.selftest) std::fprintf(stderr, "GUI: %s\n", Utf8(message).c_str());
    else MessageBoxW(owner, message, L"hdTrans", MB_OK | MB_ICONERROR);
}

bool LoadEngine(App& app, std::string dir = {}) {
    if (app.engine && app.engine->loaded()) return true;
    if (dir.empty()) dir = hdTrans::frontend::ModuleDirectory();
    if (dir.empty()) {
        app.error = L"실행 파일의 위치를 찾지 못했습니다.";
        return false;
    }
    const std::string configPath = hdTrans::dict::ConfigFile(dir);
    const auto config = hdTrans::internal::LoadConfig(configPath);
    const std::string errors = hdTrans::internal::ConfigErrors(config, configPath);
    if (!errors.empty()) OutputDebugStringW(Wide("hdTransGUI: " + errors + "\n").c_str());
    app.settings = config.settings;
    const auto& settings = app.settings;
    if (!app.cache.Apply(settings.cache)) {
        app.error = L"캐시 폴더 설정을 적용하지 못했습니다.";
        return false;
    }
    app.engine = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto);
    const auto dirs = settings.dicts.value_or(std::vector<std::string>{hdTrans::dict::ShippedFolder(dir)});
    const auto userPaths = settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
    app.engine->OpenEhnd(settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "");
    if (!app.engine->OpenUserDicts(dirs, false, userPaths)) {
        app.error = L"사전을 읽지 못했습니다. 데이터 폴더의 dicts와 user/hdtrans.config.yaml 설정을 확인해 주세요.";
        return false;
    }
    const std::string userError = app.engine->UserFileError();
    if (!userError.empty()) OutputDebugStringW(Wide("hdTransGUI: " + userError + "\n").c_str());
    for (const auto& line : app.engine->FilterErrors()) OutputDebugStringW(Wide("hdTransGUI: " + line + "\n").c_str());
    return true;
}

gui::TranslationOptions ConfiguredOptions(const App& app, gui::TranslationOptions options) {
    options.cg = app.settings.cg;
    options.misread = app.settings.misread;
    options.speech = app.settings.speech;
    return options;
}

void ResetJob(App& app) {
    app.cancel = false;
    app.result.clear();
    app.error.clear();
    app.wasCancelled = false;
    app.busy = true;
}

void EnableShortControls(App& app, bool enabled) {
    for (int id : {IDC_SOURCE, IDC_TRANSLATE, IDC_CLEAR, IDC_FILE, IDC_WHOLE_DOCUMENT,
                   IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_INTERLEAVED, IDC_OUTPUT_SPACED, IDC_SKIP_BLANK}) {
        EnableWindow(GetDlgItem(app.main, id), enabled ? TRUE : FALSE);
    }
    SetDlgItemTextW(app.main, IDC_TRANSLATE, enabled ? L"번역하기" : L"번역 중...");
}

void StartShort(App& app) {
    if (app.busy) return;
    const std::string source = SourceText(app.main);
    const gui::TranslationOptions options = Options(app.main, false);
    ResetJob(app);
    EnableShortControls(app, false);
    app.worker = std::thread([&app, source, options] {
        if (!app.cancel && LoadEngine(app)) app.result = gui::TranslateText(*app.engine, source, ConfiguredOptions(app, options), app.cancel);
        app.wasCancelled = app.cancel.load();
        PostMessageW(app.main, kDone, 0, 0);
    });
}

void FinishShort(App& app) {
    if (app.worker.joinable()) app.worker.join();
    app.busy = false;
    EnableShortControls(app, true);
    if (app.closing) {
        DestroyWindow(app.main);
        return;
    }
    if (!app.error.empty()) ReportError(app, app.main, app.error.c_str());
    else if (!app.wasCancelled) SetDlgItemTextW(app.main, IDC_RESULT, EditText(app.result).c_str());
}

void CopyResult(App& app) {
    const std::wstring text = ControlText(app.main, IDC_RESULT);
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!block) return;
    void* memory = GlobalLock(block);
    if (!memory) {
        GlobalFree(block);
        return;
    }
    std::memcpy(memory, text.c_str(), bytes);
    GlobalUnlock(block);
    if (OpenClipboard(app.main)) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, block)) block = nullptr;
        CloseClipboard();
    }
    if (block) {
        GlobalFree(block);
        ReportError(app, app.main, L"클립보드에 복사하지 못했습니다.");
    }
}

bool PickInputs(HWND owner, std::vector<fs::path>& paths) {
    std::wstring buffer(1 << 18, L'\0');
    if (!paths.empty() && paths[0].native().size() < 32768) buffer.replace(0, paths[0].native().size(), paths[0].native());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"텍스트 파일 (*.txt)\0*.txt\0모든 파일 (*.*)\0*.*\0\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) return false;
    // One file: the full path. Several: "folder\0name\0name\0\0", with nFileOffset past the folder's NUL.
    const fs::path first(buffer.c_str());
    paths.clear();
    if (dialog.nFileOffset == 0 || buffer[dialog.nFileOffset - 1] != L'\0') {
        paths.push_back(first);
        return true;
    }
    for (const wchar_t* name = buffer.c_str() + dialog.nFileOffset; *name; name += std::wcslen(name) + 1) paths.push_back(first / name);
    return !paths.empty();
}

bool PickFolder(HWND owner, fs::path& folder) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
    bool picked = false;
    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options)) &&
        SUCCEEDED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR))) {
        IShellItem* start = nullptr;
        if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
            dialog->SetFolder(start);
            start->Release();
        }
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                folder = path;
                picked = true;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return picked;
}

fs::path OutputFolder(const App& app) {
    return app.sameFolder ? fs::path() : app.outputFolder;
}

void ShowOutput(App& app) {
    CheckDlgButton(app.file, IDC_SAME_FOLDER, app.sameFolder ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(app.file, IDC_BROWSE_OUTPUT), app.sameFolder ? FALSE : TRUE);
    const fs::path folder = app.sameFolder ? (app.inputFiles.empty() ? fs::path() : app.inputFiles[0].parent_path()) : app.outputFolder;
    SetDlgItemTextW(app.file, IDC_OUTPUT_PATH, folder.c_str());
}

bool SetInputFiles(App& app, const std::vector<fs::path>& paths) {
    std::string text;
    gui::TextEncoding encoding{};
    if (paths.empty() || !gui::ReadTextFile(paths[0], text, encoding)) {
        ReportError(app, app.file, L"파일을 읽지 못했습니다. Shift-JIS, UTF-8 또는 UTF-16LE 텍스트 파일을 골라 주세요.");
        return false;
    }
    app.inputFiles = paths;
    std::wstring shown = paths[0].native();
    if (paths.size() > 1) shown += L" 외 " + std::to_wstring(paths.size() - 1) + L"개";
    SetDlgItemTextW(app.file, IDC_INPUT_PATH, shown.c_str());
    ShowOutput(app);
    // The preview shows the first file and is bounded; translation reads each file again.
    constexpr std::size_t previewLimit = 16000;
    std::wstring preview = Wide(text);
    if (preview.size() > previewLimit) {
        preview.resize(previewLimit);
        if (preview.back() >= 0xd800 && preview.back() <= 0xdbff) preview.pop_back();
        preview += L"\r\n...";
    }
    const std::string previewUtf8 = Utf8(preview);
    SetDlgItemTextW(app.file, IDC_PREVIEW, EditText(previewUtf8).c_str());
    SetDlgItemTextW(app.file, IDC_STATUS, paths.size() > 1
        ? (L"파일 " + std::to_wstring(paths.size()) + L"개를 차례로 번역해 이름_번역.txt로 저장합니다. 미리보기는 첫 파일입니다.").c_str()
        : L"문서 전체를 묶어서 번역하고 이름_번역.txt에 UTF-8 BOM으로 저장합니다.");
    return true;
}

bool SaveResult(App& app, const fs::path& output) {
    fs::path temporary = output;
    temporary += L".hdtrans-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".tmp";
    const HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    CloseHandle(handle);
    bool saved = gui::WriteTextFile(temporary, app.result);
    if (app.cancel) saved = false;
    if (saved) saved = MoveFileExW(temporary.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    std::error_code ignored;
    fs::remove(temporary, ignored);
    return saved;
}

void StartFileWorker(App& app) {
    const gui::TranslationOptions options = Options(app.file, true);
    app.worker = std::thread([&app, options] {
        for (std::size_t index = 0; index < app.jobs.size() && !app.cancel; ++index) {
            const FileJob& job = app.jobs[index];
            std::string text;
            gui::TextEncoding encoding{};
            if (!gui::ReadTextFile(job.input, text, encoding)) {
                app.failed.push_back(job.input.filename().native() + L" (읽기)");
                continue;
            }
            if (!LoadEngine(app)) break;
            if (app.cancel) break;
            PostMessageW(app.progress, kPhase, 1, static_cast<LPARAM>(index));
            app.result = gui::TranslateText(*app.engine, text, ConfiguredOptions(app, options), app.cancel);
            if (app.cancel) break;
            PostMessageW(app.progress, kPhase, 2, static_cast<LPARAM>(index));
            if (SaveResult(app, job.output)) ++app.savedCount;
            else if (!app.cancel) app.failed.push_back(job.output.filename().native() + L" (저장)");
        }
        app.wasCancelled = app.cancel.load() && app.savedCount + app.failed.size() < app.jobs.size();
        PostMessageW(app.progress, kDone, 0, 0);
    });
}

int AskOverwrite(App& app, const std::vector<fs::path>& existing) {
    if (app.selftest) return app.overwriteAnswer;
    std::wstring message = L"이미 있는 번역 파일 " + std::to_wstring(existing.size()) + L"개를 덮어쓸까요?\n";
    for (std::size_t index = 0; index < existing.size() && index < 10; ++index) message += L"\n" + existing[index].filename().native();
    if (existing.size() > 10) message += L"\n...";
    message += L"\n\n예: 모두 덮어쓰기, 아니요: 있는 파일은 건너뛰기, 취소: 번역하지 않기";
    return MessageBoxW(app.file, message.c_str(), L"hdTrans", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
}

bool PlanJobs(App& app) {
    app.jobs.clear();
    app.skippedCount = 0;
    if (app.inputFiles.empty()) {
        ReportError(app, app.file, L"번역할 파일을 골라 주세요.");
        return false;
    }
    if (!app.sameFolder && app.outputFolder.empty()) {
        ReportError(app, app.file, L"저장할 폴더를 골라 주세요.");
        return false;
    }
    std::error_code error;
    std::vector<fs::path> existing;
    for (const fs::path& input : app.inputFiles) {
        const fs::path output = gui::TranslatedPath(input, OutputFolder(app));
        for (const fs::path& other : app.inputFiles) {
            if (fs::equivalent(other, output, error) || _wcsicmp(other.c_str(), output.c_str()) == 0) {
                ReportError(app, app.file, (L"원문을 덮어쓰게 됩니다. 저장할 폴더를 바꿔 주세요.\n\n" + output.native()).c_str());
                return false;
            }
        }
        for (const FileJob& job : app.jobs) {
            if (_wcsicmp(job.output.c_str(), output.c_str()) == 0) {
                ReportError(app, app.file, (L"같은 이름으로 저장되는 파일이 있습니다.\n\n" + output.native()).c_str());
                return false;
            }
        }
        if (fs::exists(output, error)) existing.push_back(output);
        app.jobs.push_back({input, output});
    }
    if (existing.empty()) return true;
    const int answer = AskOverwrite(app, existing);
    if (answer == IDCANCEL) return false;
    if (answer == IDNO) {
        std::vector<FileJob> kept;
        for (FileJob& job : app.jobs) {
            if (fs::exists(job.output, error)) ++app.skippedCount;
            else kept.push_back(std::move(job));
        }
        app.jobs = std::move(kept);
        if (app.jobs.empty()) {
            SetDlgItemTextW(app.file, IDC_STATUS, L"모든 번역 파일이 이미 있어서 건너뛰었습니다.");
            return false;
        }
    }
    return true;
}

void StartFile(App& app) {
    if (app.busy || !PlanJobs(app)) return;
    ResetJob(app);
    app.savedCount = 0;
    app.failed.clear();
    app.progress = CreateDialogParamW(app.instance, MAKEINTRESOURCEW(IDD_FILE_TRANSWIN_PROG), app.file,
                                     ProgressProc, reinterpret_cast<LPARAM>(&app));
    if (!app.progress) {
        app.busy = false;
        app.error = L"진행 창을 만들지 못했습니다.";
        ReportError(app, app.file, app.error.c_str());
        return;
    }
    EnableWindow(app.file, FALSE);
    if (!app.selftest) ShowWindow(app.progress, SW_SHOW);
}

void ShowPhase(App& app, WPARAM phase, LPARAM index) {
    if (app.cancel || index < 0 || static_cast<std::size_t>(index) >= app.jobs.size()) return;
    const std::wstring text = L"[" + std::to_wstring(index + 1) + L"/" + std::to_wstring(app.jobs.size()) + L"] " +
                              app.jobs[static_cast<std::size_t>(index)].input.filename().native() + L"\n" +
                              (phase == 1 ? L"문서 전체를 번역하고 있습니다." : L"UTF-8 BOM으로 저장하고 있습니다.");
    SetDlgItemTextW(app.progress, IDC_STATUS, text.c_str());
}

void FinishFile(App& app) {
    if (app.worker.joinable()) app.worker.join();
    app.busy = false;
    DestroyWindow(app.progress);
    app.progress = nullptr;
    EnableWindow(app.file, TRUE);
    if (!app.selftest) SetForegroundWindow(app.file);
    std::wstring status = L"파일 " + std::to_wstring(app.savedCount) + L"개를 UTF-8 BOM으로 저장했습니다.";
    if (app.skippedCount) status += L" 이미 있는 " + std::to_wstring(app.skippedCount) + L"개는 건너뛰었습니다.";
    if (!app.error.empty()) {
        SetDlgItemTextW(app.file, IDC_STATUS, L"번역하지 못했습니다.");
        ReportError(app, app.file, app.error.c_str());
    } else if (app.wasCancelled) {
        SetDlgItemTextW(app.file, IDC_STATUS, (L"중단했습니다. " + status + L" 나머지는 저장하지 않았습니다.").c_str());
    } else if (!app.failed.empty()) {
        SetDlgItemTextW(app.file, IDC_STATUS, status.c_str());
        std::wstring message = L"처리하지 못한 파일이 있습니다.\n";
        for (const std::wstring& name : app.failed) message += L"\n" + name;
        ReportError(app, app.file, message.c_str());
    } else {
        SetDlgItemTextW(app.file, IDC_STATUS, (L"번역이 끝났습니다. " + status).c_str());
    }
}

void OpenFileDialog(App& app) {
    if (app.busy || app.file) return;
    app.file = CreateDialogParamW(app.instance, MAKEINTRESOURCEW(IDD_FILE_TRANSWIN), app.main,
                                 FileProc, reinterpret_cast<LPARAM>(&app));
    if (!app.file) {
        ReportError(app, app.main, L"문서 번역 창을 만들지 못했습니다.");
        return;
    }
    EnableWindow(app.main, FALSE);
    if (!app.selftest) ShowWindow(app.file, SW_SHOW);
}

// Regular files among the dropped paths; folders are skipped. Releases drop.
std::vector<fs::path> DroppedFiles(HDROP drop) {
    std::vector<fs::path> paths;
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT index = 0; index < count; ++index) {
        std::wstring path(static_cast<std::size_t>(DragQueryFileW(drop, index, nullptr, 0)) + 1, L'\0');
        path.resize(DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size())));
        std::error_code error;
        if (!path.empty() && fs::is_regular_file(path, error)) paths.emplace_back(path);
    }
    DragFinish(drop);
    return paths;
}

void StopFile(App& app) {
    app.cancel = true;
    EnableWindow(GetDlgItem(app.progress, IDC_STOP), FALSE);
    SetDlgItemTextW(app.progress, IDC_STATUS, L"중단 요청됨. 현재 문서 처리가 끝나면 닫습니다.");
}

App* DialogApp(HWND window, UINT message, LPARAM data) {
    if (message == WM_INITDIALOG) SetWindowLongPtrW(window, GWLP_USERDATA, data);
    return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

INT_PTR CALLBACK MainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = DialogApp(window, message, lParam);
    if (!app) return FALSE;
    switch (message) {
    case WM_INITDIALOG:
        app->main = window;
        InitOptions(window);
        DragAcceptFiles(window, TRUE);
        for (int id : {IDC_SOURCE, IDC_RESULT}) SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, INT_MAX - 1, 0);
        SetFont(window, app->mainFont);
        return TRUE;
    case WM_DPICHANGED:
        PostMessageW(window, kFont, 0, 0);
        return FALSE;
    case kFont:
        SetFont(window, app->mainFont);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_TRANSLATE: StartShort(*app); return TRUE;
        case IDC_COPY: CopyResult(*app); return TRUE;
        case IDC_CLEAR:
            if (!app->busy) {
                SetDlgItemTextW(window, IDC_SOURCE, L"");
                SetDlgItemTextW(window, IDC_RESULT, L"");
                SetFocus(GetDlgItem(window, IDC_SOURCE));
            }
            return TRUE;
        case IDC_FILE: OpenFileDialog(*app); return TRUE;
        case IDCANCEL: SendMessageW(window, WM_CLOSE, 0, 0); return TRUE;
        }
        break;
    case WM_DROPFILES: {
        const std::vector<fs::path> paths = DroppedFiles(reinterpret_cast<HDROP>(wParam));
        if (paths.empty() || app->busy) return TRUE;
        OpenFileDialog(*app);
        if (app->file) SetInputFiles(*app, paths);
        return TRUE;
    }
    case kDone: FinishShort(*app); return TRUE;
    case WM_CLOSE:
        if (app->file) return TRUE;
        if (app->busy) {
            app->closing = true;
            app->cancel = true;
            SetDlgItemTextW(window, IDC_TRANSLATE, L"중단 중...");
        } else DestroyWindow(window);
        return TRUE;
    case WM_NCDESTROY:
        if (app->mainFont) DeleteObject(app->mainFont);
        app->mainFont = nullptr;
        app->main = nullptr;
        if (!app->selftest) PostQuitMessage(0);
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK FileProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = DialogApp(window, message, lParam);
    if (!app) return FALSE;
    switch (message) {
    case WM_INITDIALOG:
        app->file = window;
        InitOptions(window);
        DragAcceptFiles(window, TRUE);
        SetFont(window, app->fileFont);
        if (app->inputFiles.empty() || !SetInputFiles(*app, app->inputFiles)) ShowOutput(*app);
        return TRUE;
    case WM_DPICHANGED: PostMessageW(window, kFont, 0, 0); return FALSE;
    case kFont: SetFont(window, app->fileFont); return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BROWSE_INPUT: {
            std::vector<fs::path> paths = app->inputFiles;
            if (PickInputs(window, paths)) SetInputFiles(*app, paths);
            return TRUE;
        }
        case IDC_BROWSE_OUTPUT: {
            fs::path folder = app->outputFolder;
            if (folder.empty() && !app->inputFiles.empty()) folder = app->inputFiles[0].parent_path();
            if (PickFolder(window, folder)) {
                app->outputFolder = folder;
                ShowOutput(*app);
            }
            return TRUE;
        }
        case IDC_SAME_FOLDER:
            app->sameFolder = IsDlgButtonChecked(window, IDC_SAME_FOLDER) == BST_CHECKED;
            ShowOutput(*app);
            return TRUE;
        case IDC_TRANSLATE: StartFile(*app); return TRUE;
        case IDCANCEL: SendMessageW(window, WM_CLOSE, 0, 0); return TRUE;
        }
        break;
    case WM_DROPFILES: {
        const std::vector<fs::path> paths = DroppedFiles(reinterpret_cast<HDROP>(wParam));
        if (!paths.empty() && !app->busy) SetInputFiles(*app, paths);
        return TRUE;
    }
    case WM_CLOSE:
        if (!app->busy) {
            DestroyWindow(window);
            EnableWindow(app->main, TRUE);
            if (!app->selftest) SetForegroundWindow(app->main);
        }
        return TRUE;
    case WM_NCDESTROY:
        if (app->fileFont) DeleteObject(app->fileFont);
        app->fileFont = nullptr;
        app->file = nullptr;
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK ProgressProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = DialogApp(window, message, lParam);
    if (!app) return FALSE;
    switch (message) {
    case WM_INITDIALOG:
        app->progress = window;
        SetFont(window, app->progressFont);
        SendDlgItemMessageW(window, IDC_PROGRESS, PBM_SETMARQUEE, TRUE, 35);
        PostMessageW(window, kStart, 0, 0);
        return TRUE;
    case WM_DPICHANGED: PostMessageW(window, kFont, 0, 0); return FALSE;
    case kFont: SetFont(window, app->progressFont); return TRUE;
    case kStart: StartFileWorker(*app); return TRUE;
    case kPhase: ShowPhase(*app, wParam, lParam); return TRUE;
    case kDone: FinishFile(*app); return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_STOP || LOWORD(wParam) == IDCANCEL) {
            StopFile(*app);
            return TRUE;
        }
        break;
    case WM_CLOSE: StopFile(*app); return TRUE;
    case WM_NCDESTROY:
        if (app->progressFont) DeleteObject(app->progressFont);
        app->progressFont = nullptr;
        break;
    }
    return FALSE;
}

void Dispatch(App& app, MSG& message) {
    if (app.progress && IsDialogMessageW(app.progress, &message)) return;
    if (app.file && IsDialogMessageW(app.file, &message)) return;
    if (app.main && IsDialogMessageW(app.main, &message)) return;
    TranslateMessage(&message);
    DispatchMessageW(&message);
}

bool WaitForJob(App& app) {
    const ULONGLONG end = GetTickCount64() + 120000;
    while (app.busy && GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) Dispatch(app, message);
        if (app.busy) MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
    }
    if (!app.busy) return true;
    app.cancel = true;
    if (app.worker.joinable()) app.worker.join();
    std::fprintf(stderr, "FAIL GUI worker timeout\n");
    return false;
}

bool Check(bool condition, const char* message) {
    std::fprintf(condition ? stdout : stderr, "%s %s\n", condition ? "PASS" : "FAIL", message);
    return condition;
}

void NotifyClick(HWND window, int id) {
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                 reinterpret_cast<LPARAM>(GetDlgItem(window, id)));
}

// Sends WM_DROPFILES as Explorer does; the receiver frees the block.
void DropFiles(HWND window, const std::vector<fs::path>& paths) {
    std::wstring list;
    for (const fs::path& path : paths) list += path.native() + L'\0';
    list += L'\0';
    const HGLOBAL block = GlobalAlloc(GHND, sizeof(DROPFILES) + list.size() * sizeof(wchar_t));
    if (!block) return;
    auto* header = static_cast<DROPFILES*>(GlobalLock(block));
    header->pFiles = sizeof(DROPFILES);
    header->fWide = TRUE;
    std::memcpy(header + 1, list.data(), list.size() * sizeof(wchar_t));
    GlobalUnlock(block);
    SendMessageW(window, WM_DROPFILES, reinterpret_cast<WPARAM>(block), 0);
}

bool WriteFixture(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return static_cast<bool>(file);
}

bool SelfTestConfig(App& app, const fs::path& folder) {
    const fs::path program = folder / L"설정 배치";
    const fs::path data = program / L"HDTRANS";
    const fs::path user = data / L"USER";
    const fs::path config = user / L"HDTRANS.CONFIG.YAML";
    std::error_code error;
    for (const auto& directory : {data / L"DiCtS", data / L"別辞書", user}) {
        fs::create_directories(directory, error);
        if (!Check(!error, "create config fixture folder")) return false;
    }
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n";
    if (!Check(WriteFixture(data / L"DiCtS/base.tsv", header + "猫\t기본사전\tN110\t\t\tGN\t\t\t\t\t1\n") &&
               WriteFixture(data / L"別辞書/base.tsv", header + "猫\t설정사전\tN110\t\t\tGN\t\t\t\t\t1\n") &&
               WriteFixture(user / L"USERDICT.TSV", "犬\t기본사용자\n") &&
               WriteFixture(user / L"custom.tsv", "犬\t앞파일\n") &&
               WriteFixture(user / L"custom.txt", "犬\t사용자설정\n") &&
               WriteFixture(config, "dicts: ../別辞書\nuserdict: [custom.tsv, custom.txt]\ncache: off\n"
                                    "cg: false\nmisread: true\nreference: true\n"), "write config fixtures")) return false;
    const wchar_t* initialCache = _wgetenv(L"HDTRANS_CACHE_DIR");
    const std::optional<std::wstring> previousCache = initialCache ? std::optional<std::wstring>(initialCache) : std::nullopt;
    app.engine.reset();
    const std::string programDir = Utf8(program.native());
    if (!Check(LoadEngine(app, programDir), "load nested case-insensitive config")) return false;
    bool ok = Check(!app.settings.cg && app.settings.misread && app.settings.reference, "GUI config session settings");
    const wchar_t* configuredCache = _wgetenv(L"HDTRANS_CACHE_DIR");
    ok = Check(configuredCache && std::wstring(configuredCache) == L"off", "GUI config cache setting") && ok;
    CheckRadioButton(app.main, IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_SPACED, IDC_OUTPUT_TRANSLATION);
    CheckDlgButton(app.main, IDC_WHOLE_DOCUMENT, BST_CHECKED);
    SetDlgItemTextW(app.main, IDC_SOURCE, L"猫\r\n犬");
    NotifyClick(app.main, IDC_TRANSLATE);
    if (!WaitForJob(app)) return false;
    ok = Check(app.error.empty() && ControlText(app.main, IDC_RESULT) == L"설정사전\r\n사용자설정",
               "GUI configured dictionary and relative user path") && ok;
    fs::remove(config, error);
    if (!Check(!error, "remove optional config")) return false;
    app.engine.reset();
    if (!Check(LoadEngine(app, programDir), "load default mixed-case data layout")) return false;
    const wchar_t* restoredCache = _wgetenv(L"HDTRANS_CACHE_DIR");
    ok = Check(previousCache ? restoredCache && *previousCache == restoredCache : !restoredCache,
               "GUI restores cache environment") && ok;
    ok = Check(app.settings.cg && !app.settings.misread && !app.settings.reference, "GUI defaults without config") && ok;
    NotifyClick(app.main, IDC_TRANSLATE);
    if (!WaitForJob(app)) return false;
    ok = Check(app.error.empty() && ControlText(app.main, IDC_RESULT) == L"기본사전\r\n기본사용자",
               "GUI default dictionary and user folder") && ok;
    app.engine.reset();
    return ok;
}

bool SelfTest(App& app) {
    bool ok = Check(!IsWindowVisible(app.main), "hidden short translation dialog");
    SetDlgItemTextW(app.main, IDC_SOURCE, L"日本語");
    NotifyClick(app.main, IDC_TRANSLATE);
    if (!Check(app.busy, "translate button starts worker") || !WaitForJob(app)) return false;
    ok = Check(app.error.empty() && ControlText(app.main, IDC_RESULT) == L"일본어", "short translation through dialog messages") && ok;
    CheckRadioButton(app.main, IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_SPACED, IDC_OUTPUT_INTERLEAVED);
    NotifyClick(app.main, IDC_TRANSLATE);
    if (!WaitForJob(app)) return false;
    ok = Check(ControlText(app.main, IDC_RESULT) == L"日本語\r\n일본어", "source and translation output") && ok;
    NotifyClick(app.main, IDC_CLEAR);
    ok = Check(ControlText(app.main, IDC_SOURCE).empty() && ControlText(app.main, IDC_RESULT).empty(), "clear button") && ok;

    std::error_code error;
    const fs::path base = fs::temp_directory_path(error);
    if (!Check(!error, "temporary folder")) return false;
    const fs::path folder = base / (L"hdtrans-gui-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!Check(fs::create_directory(folder, error), "create test folder")) return false;
    const fs::path input = folder / L"원문.txt";
    const fs::path output = folder / L"원문_번역.txt";
    app.inputFiles = {input};
    ok = Check(gui::WriteTextFile(input, "日本語\n\n日本語\n"), "create input file") && ok;
    NotifyClick(app.main, IDC_FILE);
    if (!Check(app.file != nullptr, "create file dialog")) {
        fs::remove_all(folder, error);
        return false;
    }
    ok = Check(!IsWindowVisible(app.file) && ControlText(app.file, IDC_PREVIEW) == L"日本語\r\n\r\n日本語\r\n", "hidden file dialog and preview") && ok;
    ok = Check(IsDlgButtonChecked(app.file, IDC_SAME_FOLDER) == BST_CHECKED && ControlText(app.file, IDC_OUTPUT_PATH) == folder.native() &&
               !IsWindowEnabled(GetDlgItem(app.file, IDC_BROWSE_OUTPUT)), "same folder is the default output") && ok;
    NotifyClick(app.file, IDC_TRANSLATE);
    ok = Check(app.progress && !IsWindowVisible(app.progress), "hidden progress dialog") && ok;
    if (!WaitForJob(app)) return false;
    std::string text;
    gui::TextEncoding encoding{};
    ok = Check(app.error.empty() && gui::ReadTextFile(output, text, encoding) &&
               encoding == gui::TextEncoding::Utf8Bom && text == "일본어\n\n일본어\n", "file translation and BOM output") && ok;

    ok = Check(IsWindowEnabled(app.file) && IsWindowEnabled(GetDlgItem(app.file, IDC_TRANSLATE)),
               "file dialog reenabled after completion") && ok;
    ok = Check(gui::WriteTextFile(output, "unchanged"), "prepare existing output") && ok;
    app.overwriteAnswer = IDNO;
    NotifyClick(app.file, IDC_TRANSLATE);
    ok = Check(!app.busy && !app.progress && gui::ReadTextFile(output, text, encoding) && text == "unchanged",
               "declining overwrite skips existing output") && ok;
    app.overwriteAnswer = IDYES;
    NotifyClick(app.file, IDC_TRANSLATE);
    if (!Check(app.progress != nullptr, "create cancellation dialog")) return false;
    NotifyClick(app.progress, IDC_STOP);
    if (!WaitForJob(app)) return false;
    ok = Check(app.wasCancelled && gui::ReadTextFile(output, text, encoding) && text == "unchanged", "stop preserves existing output") && ok;

    const fs::path second = folder / L"둘째.txt";
    const fs::path outputFolder = folder / L"출력";
    ok = Check(gui::WriteTextFile(second, "日本語\n") && fs::create_directory(outputFolder, error), "create batch fixtures") && ok;
    ok = Check(SetInputFiles(app, {input, second}), "select several files") && ok;
    app.outputFolder = outputFolder;
    CheckDlgButton(app.file, IDC_SAME_FOLDER, BST_UNCHECKED);
    NotifyClick(app.file, IDC_SAME_FOLDER);
    ok = Check(!app.sameFolder && ControlText(app.file, IDC_OUTPUT_PATH) == outputFolder.native() &&
               IsWindowEnabled(GetDlgItem(app.file, IDC_BROWSE_OUTPUT)), "chosen output folder") && ok;
    NotifyClick(app.file, IDC_TRANSLATE);
    if (!WaitForJob(app)) return false;
    std::string secondText;
    ok = Check(app.error.empty() && app.failed.empty() && app.savedCount == 2 &&
               gui::ReadTextFile(outputFolder / L"원문_번역.txt", text, encoding) && text == "일본어\n\n일본어\n" &&
               gui::ReadTextFile(outputFolder / L"둘째_번역.txt", secondText, encoding) && secondText == "일본어\n",
               "batch translation into a folder") && ok;

    CheckDlgButton(app.file, IDC_SAME_FOLDER, BST_CHECKED);
    NotifyClick(app.file, IDC_SAME_FOLDER);
    ok = Check(SetInputFiles(app, {input, output}), "select a source and its output name") && ok;
    NotifyClick(app.file, IDC_TRANSLATE);
    ok = Check(!app.busy && !app.progress && gui::ReadTextFile(output, text, encoding) && text == "unchanged",
               "batch never overwrites a source file") && ok;
    DropFiles(app.file, {outputFolder, second, input});
    ok = Check(app.inputFiles == std::vector<fs::path>{second, input} && ControlText(app.file, IDC_PREVIEW) == L"日本語\r\n",
               "files dropped on the document window") && ok;
    SendMessageW(app.file, WM_CLOSE, 0, 0);
    DropFiles(app.main, {outputFolder});
    ok = Check(!app.file, "a dropped folder alone is ignored") && ok;
    DropFiles(app.main, {input});
    ok = Check(app.file && app.inputFiles == std::vector<fs::path>{input}, "files dropped on the main window") && ok;
    if (app.file) SendMessageW(app.file, WM_CLOSE, 0, 0);
    ok = SelfTestConfig(app, folder) && ok;
    fs::remove_all(folder, error);
    return ok;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) return 1;
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
    App app;
    app.instance = instance;
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 1;
    app.selftest = count == 2 && std::wcscmp(arguments[1], L"--selftest") == 0;
    const bool valid = count == 1 || app.selftest;
    LocalFree(arguments);
    if (!valid) return 2;
    app.main = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_TRANSWIN), nullptr, MainProc, reinterpret_cast<LPARAM>(&app));
    if (!app.main) return 1;
    if (app.selftest) {
        const bool passed = SelfTest(app);
        app.cancel = true;
        if (app.worker.joinable()) app.worker.join();
        if (app.progress) DestroyWindow(app.progress);
        if (app.file) DestroyWindow(app.file);
        if (app.main) DestroyWindow(app.main);
        return passed ? 0 : 1;
    }
    ShowWindow(app.main, show);
    MSG message{};
    BOOL received;
    while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) Dispatch(app, message);
    app.cancel = true;
    if (app.worker.joinable()) app.worker.join();
    return received < 0 ? 1 : static_cast<int>(message.wParam);
}
