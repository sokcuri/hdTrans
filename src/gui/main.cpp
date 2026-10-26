#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "core/config/cache.h"
#include "core/config/config.h"
#include "core/dict/folders.h"
#include "core/engine.h"
#include "frontend/module_dir.h"
#include "gui/resource.h"
#include "gui/text_file.h"
#include "gui/translation.h"
#include "gui/user_dicts.h"

namespace {

namespace fs = std::filesystem;
namespace gui = hdTrans::gui;
constexpr UINT kDone = WM_APP + 1;
constexpr UINT kPhase = WM_APP + 2;
constexpr UINT kFont = WM_APP + 3;
constexpr UINT kStart = WM_APP + 4;
constexpr UINT kRelist = WM_APP + 5;

enum Page { kTextPage, kDocsPage, kDictsPage };
constexpr const wchar_t* kPageNames[] = {L"단문 번역", L"문서 번역", L"사전"};

// Phases a file job reports to the document page.
enum Phase { kTranslating = 1, kSaving, kSaved, kFailed };

struct FileJob {
    fs::path input;
    fs::path output;
    std::size_t item = 0;  // index in App::inputFiles
};

struct App {
    HINSTANCE instance = nullptr;
    HWND main = nullptr;
    HWND tabs = nullptr;
    HWND text = nullptr;
    HWND docs = nullptr;
    HWND dicts = nullptr;
    HFONT font = nullptr;
    bool selftest = false;
    bool busy = false;
    bool closing = false;
    bool filling = false;  // list views are being refilled; their notifications are ignored
    hdTrans::internal::Config settings;
    hdTrans::internal::ConfigCache cache;
    std::unique_ptr<hdTrans::Engine> engine;
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::atomic<bool> reloadDicts{false};
    std::string result;
    std::wstring error;
    bool wasCancelled = false;

    std::vector<fs::path> inputFiles;
    std::vector<std::wstring> fileStates;
    fs::path outputFolder;
    bool sameFolder = true;
    std::vector<FileJob> jobs;
    std::size_t savedCount = 0;
    std::size_t skippedCount = 0;
    std::vector<std::wstring> failed;
    int overwriteAnswer = IDYES;

    std::vector<std::string> userSources;
    std::vector<gui::UserDict> userDicts;
    int dictIndex = -1;
    gui::DictText dictText;
    std::vector<gui::DictRow> rows;
    std::vector<std::size_t> rowLines;  // the line of dictText of each row

    ~App() {
        cancel = true;
        if (worker.joinable()) worker.join();
        engine.reset();
        static_cast<void>(cache.Apply(std::nullopt));
    }
};

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

std::wstring Title(int page) {
    return L"hdTrans v" + Wide(HDTRANS_VERSION) + L" - " + kPageNames[page];
}

std::wstring WindowText(HWND control) {
    const int count = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(count) + 1, L'\0');
    GetWindowTextW(control, text.data(), count + 1);
    text.resize(static_cast<std::size_t>(count));
    return text;
}

std::wstring ControlText(HWND window, int id) {
    return WindowText(GetDlgItem(window, id));
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

// Places the controls of a page in pixels scaled from 96 DPI.
struct Placer {
    HWND page;
    int dpi;
    int width;
    int height;

    explicit Placer(HWND window) : page(window), dpi(static_cast<int>(GetDpiForWindow(window))) {
        RECT client{};
        GetClientRect(window, &client);
        width = client.right;
        height = client.bottom;
    }
    int operator()(int value) const { return MulDiv(value, dpi, 96); }
    void Put(int id, int x, int y, int w, int h) const {
        SetWindowPos(GetDlgItem(page, id), nullptr, x, y, std::max(w, 0), std::max(h, 0), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void Column(int id, int column, int w) const {
        ListView_SetColumnWidth(GetDlgItem(page, id), column, std::max(w, (*this)(30)));
    }
    int Scrollbar() const { return GetSystemMetricsForDpi(SM_CXVSCROLL, static_cast<UINT>(dpi)); }
};

// The output group of the text and document pages; returns its height.
int PlaceOptions(const Placer& p, int x, int y, int w) {
    const int row = p(22);
    p.Put(IDC_OUTPUT_GROUP, x, y, w, p(68));
    p.Put(IDC_OUTPUT_TRANSLATION, x + p(10), y + p(20), p(80), row);
    p.Put(IDC_OUTPUT_INTERLEAVED, x + p(96), y + p(20), p(125), row);
    p.Put(IDC_OUTPUT_SPACED, x + p(227), y + p(20), std::min(p(170), w - p(237)), row);
    p.Put(IDC_SKIP_BLANK, x + p(10), y + p(42), p(200), row);
    return p(68);
}

void LayoutText(HWND page) {
    const Placer p(page);
    const int m = p(10), bh = p(26), full = p.width - 2 * m;
    p.Put(IDC_SOURCE_LABEL, m, m + p(3), p(100), p(18));
    p.Put(IDC_WHOLE_DOCUMENT, p.width - m - p(180), m, p(180), p(22));
    const int top = m + p(26);
    const int fixed = top + 2 * p(6) + bh + p(8) + p(68) + p(10) + bh + m;
    const int edit = std::max((p.height - fixed) / 2, p(40));
    p.Put(IDC_SOURCE, m, top, full, edit);
    const int middle = top + edit + p(6);
    p.Put(IDC_RESULT_LABEL, m, middle + p(6), p(100), p(18));
    p.Put(IDC_TRANSLATE, (p.width - p(110)) / 2, middle, p(110), bh);
    const int result = middle + bh + p(6);
    p.Put(IDC_RESULT, m, result, full, edit);
    const int options = result + edit + p(8);
    const int buttons = options + PlaceOptions(p, m, options, full) + p(10);
    p.Put(IDC_COPY, m, buttons, p(90), bh);
    p.Put(IDC_CLEAR, m + p(98), buttons, p(90), bh);
}

void LayoutDocs(HWND page) {
    const Placer p(page);
    const int m = p(10), bh = p(26), bw = p(90), gap = p(8), full = p.width - 2 * m, label = p(84);
    const int fixed = m + gap + bh + p(4) + p(22) + gap + gap + p(68) + gap + p(26) + bh + m;
    // On a short screen the preview is hidden so the translate row stays in view; the list keeps
    // room for its two buttons.
    int list = std::max((p.height - fixed) / 2, 2 * bh + p(6));
    const int preview = p.height - fixed - list;
    const bool showPreview = preview >= p(60);
    if (!showPreview) list = std::max(p.height - fixed + gap, list);
    p.Put(IDC_FILES, m, m, full - bw - gap, list);
    p.Column(IDC_FILES, 0, full - bw - gap - p(84) - p.Scrollbar() - p(4));
    p.Column(IDC_FILES, 1, p(80));
    p.Put(IDC_BROWSE_INPUT, p.width - m - bw, m, bw, bh);
    p.Put(IDC_REMOVE, p.width - m - bw, m + bh + p(6), bw, bh);
    int y = m + list + gap;
    p.Put(IDC_OUTPUT_LABEL, m, y + p(5), label, p(18));
    p.Put(IDC_OUTPUT_PATH, m + label, y + p(1), full - label - bw - gap, p(24));
    p.Put(IDC_BROWSE_OUTPUT, p.width - m - bw, y, bw, bh);
    y += bh + p(4);
    p.Put(IDC_SAME_FOLDER, m + label, y, full - label, p(22));
    y += p(22) + gap;
    for (int id : {IDC_PREVIEW_GROUP, IDC_PREVIEW}) ShowWindow(GetDlgItem(page, id), showPreview ? SW_SHOWNA : SW_HIDE);
    if (showPreview) {
        p.Put(IDC_PREVIEW_GROUP, m, y, full, preview);
        p.Put(IDC_PREVIEW, m + p(10), y + p(22), full - p(20), preview - p(32));
        y += preview + gap;
    }
    y += PlaceOptions(p, m, y, full) + gap;
    p.Put(IDC_STATUS, m, y, full, p(20));
    y += p(26);
    p.Put(IDC_PROGRESS, m, y + p(5), full - 2 * bw - 2 * gap, p(16));
    p.Put(IDC_TRANSLATE, p.width - m - 2 * bw - gap, y, bw, bh);
    p.Put(IDC_STOP, p.width - m - bw, y, bw, bh);
}

void LayoutDicts(HWND page) {
    const Placer p(page);
    const int m = p(10), bh = p(26), gap = p(6), field = p(24);
    const int left = std::clamp(p.width * 3 / 10, p(200), p(280));
    const int bottom = p.height - m - bh;
    p.Put(IDC_DICTS, m, m, left, bottom - p(8) - m);
    p.Column(IDC_DICTS, 0, left - p.Scrollbar() - p(4));
    const int third = (left - 2 * gap) / 3;
    p.Put(IDC_DICT_NEW, m, bottom, third, bh);
    p.Put(IDC_DICT_IMPORT, m + third + gap, bottom, third, bh);
    p.Put(IDC_DICT_EXPORT, m + 2 * (third + gap), bottom, left - 2 * (third + gap), bh);
    const int x = m + left + p(10), w = p.width - x - m;
    const int fields = bottom - p(8) - field;
    p.Put(IDC_ENTRIES, x, m, w, fields - p(8) - m);
    const int room = w - 3 * gap;
    const int widths[] = {room * 3 / 10, room * 3 / 10, room * 15 / 100, room - 2 * (room * 3 / 10) - room * 15 / 100};
    const int ids[] = {IDC_ENTRY_KEY, IDC_ENTRY_KO, IDC_ENTRY_POS, IDC_ENTRY_OPTIONS};
    const int inner = w - p.Scrollbar() - p(4);
    for (int column = 0, at = x; column < 4; ++column) {
        p.Put(ids[column], at, fields, widths[column], ids[column] == IDC_ENTRY_POS ? p(200) : field);
        p.Column(IDC_ENTRIES, column, room > 0 ? MulDiv(widths[column], inner, room) : 0);
        at += widths[column] + gap;
    }
    const int bw = p(70);
    p.Put(IDC_ENTRY_ADD, p.width - m - 3 * bw - 2 * gap, bottom, bw, bh);
    p.Put(IDC_ENTRY_EDIT, p.width - m - 2 * bw - gap, bottom, bw, bh);
    p.Put(IDC_ENTRY_DELETE, p.width - m - bw, bottom, bw, bh);
}

// The work area of the monitor that holds most of the window.
RECT WorkArea(HWND window) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
    return info.rcWork;
}

// A window of at most size, centred in work and no larger than it.
RECT FitInside(SIZE size, const RECT& work) {
    const LONG w = std::min(size.cx, work.right - work.left), h = std::min(size.cy, work.bottom - work.top);
    const LONG x = work.left + (work.right - work.left - w) / 2, y = work.top + (work.bottom - work.top - h) / 2;
    return {x, y, x + w, y + h};
}

void LayoutMain(App& app) {
    const Placer p(app.main);
    RECT item{};
    TabCtrl_GetItemRect(app.tabs, 0, &item);
    const int top = p(6), header = item.bottom + p(3);
    SetWindowPos(app.tabs, nullptr, p(8), top, std::max(p.width - p(16), 0), header, SWP_NOZORDER | SWP_NOACTIVATE);
    for (HWND page : {app.text, app.docs, app.dicts}) {
        SetWindowPos(page, nullptr, 0, top + header, p.width, std::max(p.height - top - header, 0), SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void LayoutAll(App& app) {
    LayoutMain(app);
    LayoutText(app.text);
    LayoutDocs(app.docs);
    LayoutDicts(app.dicts);
}

int CurrentPage(const App& app) {
    return std::clamp(TabCtrl_GetCurSel(app.tabs), 0, 2);
}

void ShowPage(App& app, int page) {
    TabCtrl_SetCurSel(app.tabs, page);
    const HWND pages[] = {app.text, app.docs, app.dicts};
    for (int index = 0; index < 3; ++index) ShowWindow(pages[index], index == page ? SW_SHOW : SW_HIDE);
    SetWindowTextW(app.main, Title(page).c_str());
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

void ReportError(App& app, const wchar_t* message) {
    if (app.selftest) std::fprintf(stderr, "GUI: %s\n", Utf8(message).c_str());
    else MessageBoxW(app.main, message, L"hdTrans", MB_OK | MB_ICONERROR);
}

std::vector<std::string> UserSources(const std::string& dir) {
    if (dir.empty()) return {};
    const auto config = hdTrans::internal::LoadConfig(hdTrans::dict::ConfigFile(dir));
    return config.settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
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

// The loaded engine, with the user dictionaries read again after edits on the dictionary page.
bool ReadyEngine(App& app) {
    const bool reload = app.reloadDicts.exchange(false);
    if (!app.engine || !app.engine->loaded()) return LoadEngine(app);
    if (reload) app.engine->Reload();
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
    for (int id : {IDC_SOURCE, IDC_TRANSLATE, IDC_CLEAR, IDC_WHOLE_DOCUMENT,
                   IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_INTERLEAVED, IDC_OUTPUT_SPACED, IDC_SKIP_BLANK}) {
        EnableWindow(GetDlgItem(app.text, id), enabled ? TRUE : FALSE);
    }
    SetDlgItemTextW(app.text, IDC_TRANSLATE, enabled ? L"번역하기" : L"번역 중...");
}

void StartShort(App& app) {
    if (app.busy) return;
    const std::string source = SourceText(app.text);
    const gui::TranslationOptions options = Options(app.text, false);
    ResetJob(app);
    EnableShortControls(app, false);
    app.worker = std::thread([&app, source, options] {
        if (!app.cancel && ReadyEngine(app)) app.result = gui::TranslateText(*app.engine, source, ConfiguredOptions(app, options), app.cancel);
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
    if (!app.error.empty()) ReportError(app, app.error.c_str());
    else if (!app.wasCancelled) SetDlgItemTextW(app.text, IDC_RESULT, EditText(app.result).c_str());
}

void CopyResult(App& app) {
    const std::wstring text = ControlText(app.text, IDC_RESULT);
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
        ReportError(app, L"클립보드에 복사하지 못했습니다.");
    }
}

constexpr const wchar_t* kTextFilter = L"텍스트 파일 (*.txt)\0*.txt\0모든 파일 (*.*)\0*.*\0\0";
constexpr const wchar_t* kDictFilter = L"사전 파일 (*.txt, *.tsv)\0*.txt;*.tsv\0모든 파일 (*.*)\0*.*\0\0";

bool PickFiles(HWND owner, const wchar_t* filter, std::vector<fs::path>& paths) {
    std::wstring buffer(1 << 18, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
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

void InsertColumns(HWND list, std::initializer_list<const wchar_t*> names, DWORD style) {
    ListView_SetExtendedListViewStyle(list, style);
    int index = 0;
    for (const wchar_t* name : names) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = 80;
        column.pszText = const_cast<wchar_t*>(name);
        ListView_InsertColumn(list, index++, &column);
    }
}

void SetCell(HWND list, int item, int column, const std::wstring& text) {
    ListView_SetItemText(list, item, column, const_cast<wchar_t*>(text.c_str()));
}

int Selected(HWND list) {
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

void Select(HWND list, int item) {
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (item < 0) return;
    ListView_SetItemState(list, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, item, FALSE);
}

// The document page.

fs::path OutputFolder(const App& app) {
    return app.sameFolder ? fs::path() : app.outputFolder;
}

void ShowOutput(App& app) {
    CheckDlgButton(app.docs, IDC_SAME_FOLDER, app.sameFolder ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(app.docs, IDC_BROWSE_OUTPUT), app.sameFolder || app.busy ? FALSE : TRUE);
    const fs::path folder = app.sameFolder ? (app.inputFiles.empty() ? fs::path() : app.inputFiles[0].parent_path()) : app.outputFolder;
    SetDlgItemTextW(app.docs, IDC_OUTPUT_PATH, folder.c_str());
}

void ShowPreview(App& app) {
    const int item = Selected(GetDlgItem(app.docs, IDC_FILES));
    std::string text;
    gui::TextEncoding encoding{};
    if (item < 0 || static_cast<std::size_t>(item) >= app.inputFiles.size() ||
        !gui::ReadTextFile(app.inputFiles[static_cast<std::size_t>(item)], text, encoding)) {
        SetDlgItemTextW(app.docs, IDC_PREVIEW, L"");
        return;
    }
    // The preview is bounded; translation reads each file again.
    constexpr std::size_t previewLimit = 16000;
    std::wstring preview = Wide(text);
    if (preview.size() > previewLimit) {
        preview.resize(previewLimit);
        if (preview.back() >= 0xd800 && preview.back() <= 0xdbff) preview.pop_back();
        preview += L"\r\n...";
    }
    SetDlgItemTextW(app.docs, IDC_PREVIEW, EditText(Utf8(preview)).c_str());
}

void SetFileState(App& app, std::size_t item, const wchar_t* state) {
    if (item >= app.fileStates.size()) return;
    app.fileStates[item] = state;
    SetCell(GetDlgItem(app.docs, IDC_FILES), static_cast<int>(item), 1, app.fileStates[item]);
}

// Refills the file list and selects item, or keeps the selected row when item is negative.
void ShowFiles(App& app, int item) {
    const HWND list = GetDlgItem(app.docs, IDC_FILES);
    if (item < 0) item = std::min(Selected(list), static_cast<int>(app.inputFiles.size()) - 1);
    if (item < 0 && !app.inputFiles.empty()) item = 0;
    app.filling = true;
    ListView_DeleteAllItems(list);
    for (std::size_t index = 0; index < app.inputFiles.size(); ++index) {
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = static_cast<int>(index);
        row.pszText = const_cast<wchar_t*>(app.inputFiles[index].c_str());
        ListView_InsertItem(list, &row);
        SetCell(list, static_cast<int>(index), 1, app.fileStates[index]);
    }
    Select(list, item);
    app.filling = false;
    ShowPreview(app);
    ShowOutput(app);
}

// Appends the readable files that are not in the list yet and selects the first of them.
bool AddInputFiles(App& app, const std::vector<fs::path>& paths) {
    std::vector<fs::path> readable;
    std::wstring unreadable;
    for (const fs::path& path : paths) {
        std::string text;
        gui::TextEncoding encoding{};
        if (gui::ReadTextFile(path, text, encoding)) readable.push_back(path);
        else unreadable += L"\n" + path.filename().native();
    }
    const std::size_t before = app.inputFiles.size();
    gui::AppendFiles(app.inputFiles, readable);
    app.fileStates.resize(app.inputFiles.size());
    ShowFiles(app, app.inputFiles.size() > before ? static_cast<int>(before) : -1);
    if (!unreadable.empty()) {
        ReportError(app, (L"읽지 못한 파일은 넣지 않았습니다. Shift-JIS, UTF-8 또는 UTF-16LE 텍스트 파일을 골라 주세요.\n" + unreadable).c_str());
    }
    return unreadable.empty();
}

void RemoveFiles(App& app) {
    const HWND list = GetDlgItem(app.docs, IDC_FILES);
    if (app.busy) return;
    int first = -1;
    for (int item = static_cast<int>(app.inputFiles.size()) - 1; item >= 0; --item) {
        if (!(ListView_GetItemState(list, item, LVIS_SELECTED) & LVIS_SELECTED)) continue;
        app.inputFiles.erase(app.inputFiles.begin() + item);
        app.fileStates.erase(app.fileStates.begin() + item);
        first = item;
    }
    if (first >= 0) ShowFiles(app, std::min(first, static_cast<int>(app.inputFiles.size()) - 1));
}

bool SaveResult(App& app, const fs::path& output, std::string_view lineBreak) {
    fs::path temporary = output;
    temporary += L".hdtrans-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".tmp";
    const HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    CloseHandle(handle);
    bool saved = gui::WriteTextFile(temporary, app.result, lineBreak);
    if (app.cancel) saved = false;
    if (saved) saved = MoveFileExW(temporary.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    std::error_code ignored;
    fs::remove(temporary, ignored);
    return saved;
}

void StartFileWorker(App& app) {
    const gui::TranslationOptions options = Options(app.docs, true);
    app.worker = std::thread([&app, options] {
        for (std::size_t index = 0; index < app.jobs.size() && !app.cancel; ++index) {
            const FileJob& job = app.jobs[index];
            const auto position = static_cast<LPARAM>(index);
            std::string text;
            gui::TextEncoding encoding{};
            std::string_view lineBreak;
            if (!gui::ReadTextFile(job.input, text, encoding, &lineBreak)) {
                app.failed.push_back(job.input.filename().native() + L" (읽기)");
                PostMessageW(app.docs, kPhase, kFailed, position);
                continue;
            }
            if (!ReadyEngine(app)) break;
            if (app.cancel) break;
            PostMessageW(app.docs, kPhase, kTranslating, position);
            app.result = gui::TranslateText(*app.engine, text, ConfiguredOptions(app, options), app.cancel);
            if (app.cancel) break;
            PostMessageW(app.docs, kPhase, kSaving, position);
            if (SaveResult(app, job.output, lineBreak)) {
                ++app.savedCount;
                PostMessageW(app.docs, kPhase, kSaved, position);
            } else if (!app.cancel) {
                app.failed.push_back(job.output.filename().native() + L" (저장)");
                PostMessageW(app.docs, kPhase, kFailed, position);
            }
        }
        app.wasCancelled = app.cancel.load() && app.savedCount + app.failed.size() < app.jobs.size();
        PostMessageW(app.docs, kDone, 0, 0);
    });
}

int AskOverwrite(App& app, const std::vector<fs::path>& existing) {
    if (app.selftest) return app.overwriteAnswer;
    std::wstring message = L"이미 있는 번역 파일 " + std::to_wstring(existing.size()) + L"개를 덮어쓸까요?\n";
    for (std::size_t index = 0; index < existing.size() && index < 10; ++index) message += L"\n" + existing[index].filename().native();
    if (existing.size() > 10) message += L"\n...";
    message += L"\n\n예: 모두 덮어쓰기, 아니요: 있는 파일은 건너뛰기, 취소: 번역하지 않기";
    return MessageBoxW(app.main, message.c_str(), L"hdTrans", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
}

bool PlanJobs(App& app) {
    app.jobs.clear();
    app.skippedCount = 0;
    if (app.inputFiles.empty()) {
        ReportError(app, L"번역할 파일을 골라 주세요.");
        return false;
    }
    if (!app.sameFolder && app.outputFolder.empty()) {
        ReportError(app, L"저장할 폴더를 골라 주세요.");
        return false;
    }
    std::error_code error;
    std::vector<fs::path> existing;
    for (std::size_t item = 0; item < app.inputFiles.size(); ++item) {
        const fs::path output = gui::TranslatedPath(app.inputFiles[item], OutputFolder(app));
        for (const fs::path& other : app.inputFiles) {
            if (fs::equivalent(other, output, error) || _wcsicmp(other.c_str(), output.c_str()) == 0) {
                ReportError(app, (L"원문을 덮어쓰게 됩니다. 저장할 폴더를 바꿔 주세요.\n\n" + output.native()).c_str());
                return false;
            }
        }
        for (const FileJob& job : app.jobs) {
            if (_wcsicmp(job.output.c_str(), output.c_str()) == 0) {
                ReportError(app, (L"같은 이름으로 저장되는 파일이 있습니다.\n\n" + output.native()).c_str());
                return false;
            }
        }
        if (fs::exists(output, error)) existing.push_back(output);
        app.jobs.push_back({app.inputFiles[item], output, item});
    }
    for (std::size_t item = 0; item < app.inputFiles.size(); ++item) SetFileState(app, item, L"");
    if (existing.empty()) return true;
    const int answer = AskOverwrite(app, existing);
    if (answer == IDCANCEL) return false;
    if (answer == IDNO) {
        std::vector<FileJob> kept;
        for (FileJob& job : app.jobs) {
            if (fs::exists(job.output, error)) {
                ++app.skippedCount;
                SetFileState(app, job.item, L"건너뜀");
            } else {
                kept.push_back(std::move(job));
            }
        }
        app.jobs = std::move(kept);
        if (app.jobs.empty()) {
            SetDlgItemTextW(app.docs, IDC_STATUS, L"모든 번역 파일이 이미 있어서 건너뛰었습니다.");
            return false;
        }
    }
    return true;
}

void EnableDocControls(App& app, bool enabled) {
    for (int id : {IDC_BROWSE_INPUT, IDC_REMOVE, IDC_SAME_FOLDER, IDC_TRANSLATE,
                   IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_INTERLEAVED, IDC_OUTPUT_SPACED, IDC_SKIP_BLANK}) {
        EnableWindow(GetDlgItem(app.docs, id), enabled ? TRUE : FALSE);
    }
    EnableWindow(GetDlgItem(app.docs, IDC_STOP), enabled ? FALSE : TRUE);
    ShowOutput(app);
}

void StartFile(App& app) {
    if (app.busy || !PlanJobs(app)) return;
    ResetJob(app);
    app.savedCount = 0;
    app.failed.clear();
    for (const FileJob& job : app.jobs) SetFileState(app, job.item, L"대기");
    SendDlgItemMessageW(app.docs, IDC_PROGRESS, PBM_SETRANGE32, 0, static_cast<LPARAM>(app.jobs.size()));
    SendDlgItemMessageW(app.docs, IDC_PROGRESS, PBM_SETPOS, 0, 0);
    SetDlgItemTextW(app.docs, IDC_STATUS, L"");
    EnableDocControls(app, false);
    // The worker starts from the message loop, so a stop sent before then cancels every file.
    PostMessageW(app.docs, kStart, 0, 0);
}

void ShowPhase(App& app, WPARAM phase, LPARAM index) {
    if (index < 0 || static_cast<std::size_t>(index) >= app.jobs.size()) return;
    const std::size_t item = app.jobs[static_cast<std::size_t>(index)].item;
    switch (phase) {
    case kTranslating: SetFileState(app, item, L"번역 중"); break;
    case kSaving: SetFileState(app, item, L"저장 중"); break;
    case kSaved: SetFileState(app, item, L"완료"); break;
    case kFailed: SetFileState(app, item, L"실패"); break;
    }
    if (phase == kSaved || phase == kFailed) SendDlgItemMessageW(app.docs, IDC_PROGRESS, PBM_SETPOS, static_cast<WPARAM>(index + 1), 0);
}

void FinishFile(App& app) {
    if (app.worker.joinable()) app.worker.join();
    app.busy = false;
    EnableDocControls(app, true);
    if (app.closing) {
        DestroyWindow(app.main);
        return;
    }
    for (const FileJob& job : app.jobs) {
        const std::wstring& state = app.fileStates[job.item];
        if (state == L"대기" || state == L"번역 중" || state == L"저장 중") SetFileState(app, job.item, L"중단");
    }
    std::wstring status = L"파일 " + std::to_wstring(app.savedCount) + L"개를 UTF-8 BOM으로 저장했습니다.";
    if (app.skippedCount) status += L" 이미 있는 " + std::to_wstring(app.skippedCount) + L"개는 건너뛰었습니다.";
    if (!app.error.empty()) {
        SetDlgItemTextW(app.docs, IDC_STATUS, L"번역하지 못했습니다.");
        ReportError(app, app.error.c_str());
    } else if (app.wasCancelled) {
        SetDlgItemTextW(app.docs, IDC_STATUS, (L"중단했습니다. " + status + L" 나머지는 저장하지 않았습니다.").c_str());
    } else if (!app.failed.empty()) {
        SetDlgItemTextW(app.docs, IDC_STATUS, status.c_str());
        std::wstring message = L"처리하지 못한 파일이 있습니다.\n";
        for (const std::wstring& name : app.failed) message += L"\n" + name;
        ReportError(app, message.c_str());
    } else {
        SetDlgItemTextW(app.docs, IDC_STATUS, (L"번역이 끝났습니다. " + status).c_str());
    }
}

void StopFile(App& app) {
    if (!app.busy) return;
    app.cancel = true;
    EnableWindow(GetDlgItem(app.docs, IDC_STOP), FALSE);
    SetDlgItemTextW(app.docs, IDC_STATUS, L"중단하는 중입니다. 지금 문서를 마치면 멈춥니다.");
}

// The dictionary page.

fs::path DictFile(const gui::UserDict& dict) {
    return dict.on ? dict.path : gui::OffPath(dict.path);
}

const gui::UserDict* CurrentDict(const App& app) {
    return app.dictIndex >= 0 && static_cast<std::size_t>(app.dictIndex) < app.userDicts.size()
               ? &app.userDicts[static_cast<std::size_t>(app.dictIndex)] : nullptr;
}

void ShowEntry(App& app, int row) {
    gui::DictRow entry;
    if (row >= 0 && static_cast<std::size_t>(row) < app.rows.size()) entry = app.rows[static_cast<std::size_t>(row)];
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, Wide(entry.key).c_str());
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, Wide(entry.ko).c_str());
    SetDlgItemTextW(app.dicts, IDC_ENTRY_POS, Wide(entry.pos).c_str());
    SetDlgItemTextW(app.dicts, IDC_ENTRY_OPTIONS, Wide(entry.options).c_str());
}

void ShowEntries(App& app, int row) {
    app.rows.clear();
    app.rowLines.clear();
    if (!app.dictText.column) {
        for (std::size_t line = 0; line < app.dictText.lines.size(); ++line) {
            if (!gui::EntryLine(app.dictText.lines[line])) continue;
            app.rows.push_back(gui::ParseRow(app.dictText.lines[line]));
            app.rowLines.push_back(line);
        }
    }
    const HWND grid = GetDlgItem(app.dicts, IDC_ENTRIES);
    const int count = static_cast<int>(app.rows.size());
    row = std::min(row, count - 1);
    app.filling = true;
    ListView_SetItemCountEx(grid, count, 0);
    Select(grid, row);
    app.filling = false;
    InvalidateRect(grid, nullptr, TRUE);
    const BOOL editable = CurrentDict(app) && !app.dictText.column ? TRUE : FALSE;
    for (int id : {IDC_ENTRIES, IDC_ENTRY_KEY, IDC_ENTRY_KO, IDC_ENTRY_POS, IDC_ENTRY_OPTIONS, IDC_ENTRY_ADD, IDC_ENTRY_EDIT, IDC_ENTRY_DELETE}) {
        EnableWindow(GetDlgItem(app.dicts, id), editable);
    }
    EnableWindow(GetDlgItem(app.dicts, IDC_DICT_EXPORT), CurrentDict(app) ? TRUE : FALSE);
    ShowEntry(app, row);
}

void OpenDict(App& app, int index) {
    app.dictIndex = index;
    app.dictText = {};
    const gui::UserDict* dict = CurrentDict(app);
    if (dict && !gui::ReadDictText(DictFile(*dict), app.dictText)) {
        app.dictText = {};
        app.dictText.column = true;
        ReportError(app, (L"사전 파일을 읽지 못했습니다.\n\n" + DictFile(*dict).native()).c_str());
    }
    ShowEntries(app, 0);
}

// Lists the dictionary files again and opens the one at path, or the open one when path is empty.
void ShowDicts(App& app, const fs::path& path) {
    const fs::path open = path.empty() && CurrentDict(app) ? CurrentDict(app)->path : path;
    app.userDicts = gui::ListUserDicts(app.userSources);
    const HWND list = GetDlgItem(app.dicts, IDC_DICTS);
    app.filling = true;
    ListView_DeleteAllItems(list);
    int found = app.userDicts.empty() ? -1 : 0;
    for (std::size_t index = 0; index < app.userDicts.size(); ++index) {
        const gui::UserDict& dict = app.userDicts[index];
        const std::wstring name = dict.path.filename().native();
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = static_cast<int>(index);
        row.pszText = const_cast<wchar_t*>(name.c_str());
        ListView_InsertItem(list, &row);
        ListView_SetCheckState(list, static_cast<int>(index), dict.on ? TRUE : FALSE);
        if (dict.path == open) found = static_cast<int>(index);
    }
    Select(list, found);
    app.filling = false;
    OpenDict(app, found);
}

bool SaveDict(App& app) {
    const gui::UserDict* dict = CurrentDict(app);
    if (!dict || !gui::WriteDictText(DictFile(*dict), app.dictText)) {
        ReportError(app, L"사전 파일을 저장하지 못했습니다.");
        return false;
    }
    app.reloadDicts = true;
    return true;
}

void ChangeEntry(App& app, int id) {
    const HWND grid = GetDlgItem(app.dicts, IDC_ENTRIES);
    const int row = Selected(grid);
    if (!CurrentDict(app) || app.dictText.column) return;
    if (id != IDC_ENTRY_ADD && (row < 0 || static_cast<std::size_t>(row) >= app.rows.size())) return;
    gui::DictRow entry{Utf8(ControlText(app.dicts, IDC_ENTRY_KEY)), Utf8(ControlText(app.dicts, IDC_ENTRY_KO)),
                       Utf8(ControlText(app.dicts, IDC_ENTRY_POS)), Utf8(ControlText(app.dicts, IDC_ENTRY_OPTIONS))};
    if (id != IDC_ENTRY_DELETE && entry.key.find_first_not_of(" \t") == std::string::npos) {
        SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_KEY));
        return;
    }
    const gui::DictText before = app.dictText;
    int next = row;
    if (id == IDC_ENTRY_ADD) {
        app.dictText.lines.push_back(gui::RowLine(entry));
        next = static_cast<int>(app.rows.size());
    } else {
        const std::size_t line = app.rowLines[static_cast<std::size_t>(row)];
        if (id == IDC_ENTRY_EDIT) app.dictText.lines[line] = gui::RowLine(entry, app.dictText.lines[line]);
        else app.dictText.lines.erase(app.dictText.lines.begin() + static_cast<std::ptrdiff_t>(line));
    }
    if (!SaveDict(app)) app.dictText = before;
    ShowEntries(app, next);
}

void ToggleDict(App& app, int index, bool on) {
    if (index < 0 || static_cast<std::size_t>(index) >= app.userDicts.size()) return;
    gui::UserDict& dict = app.userDicts[static_cast<std::size_t>(index)];
    if (dict.on == on) return;
    if (gui::SetUserDictOn(dict, on)) {
        app.reloadDicts = true;
        return;
    }
    app.filling = true;
    ListView_SetCheckState(GetDlgItem(app.dicts, IDC_DICTS), index, dict.on ? TRUE : FALSE);
    app.filling = false;
    ReportError(app, L"사전 파일의 이름을 바꾸지 못했습니다. 다른 프로그램이 쓰고 있는지 확인해 주세요.");
}

fs::path DictFolder(App& app) {
    const fs::path folder = gui::UserDictFolder(app.userSources);
    if (folder.empty()) ReportError(app, L"사용자 사전 폴더가 설정에 없습니다.");
    return folder;
}

void NewDict(App& app) {
    const fs::path folder = DictFolder(app);
    if (folder.empty()) return;
    std::error_code error;
    fs::create_directories(folder, error);
    fs::path path = gui::UserDictPath(folder, "새 사전");
    for (int number = 2; fs::exists(path, error) || fs::exists(gui::OffPath(path), error); ++number) {
        path = gui::UserDictPath(folder, "새 사전 " + std::to_string(number));
    }
    if (!gui::WriteTextFile(path, "")) {
        ReportError(app, L"사전 파일을 만들지 못했습니다.");
        return;
    }
    app.reloadDicts = true;
    ShowDicts(app, path);
    if (!app.selftest && app.dictIndex >= 0) {
        SetFocus(GetDlgItem(app.dicts, IDC_DICTS));
        ListView_EditLabel(GetDlgItem(app.dicts, IDC_DICTS), app.dictIndex);
    }
}

// Renames the file; the list shows the new name after kRelist.
bool RenameDict(App& app, int index, const wchar_t* name) {
    if (!name || index < 0 || static_cast<std::size_t>(index) >= app.userDicts.size()) return false;
    gui::UserDict& dict = app.userDicts[static_cast<std::size_t>(index)];
    const fs::path path = gui::UserDictPath(dict.path.parent_path(), Utf8(name));
    if (path.empty() || path == dict.path) return false;
    std::error_code error;
    const fs::path target = dict.on ? path : gui::OffPath(path);
    if (fs::exists(path, error) || fs::exists(gui::OffPath(path), error)) {
        ReportError(app, L"같은 이름의 사전이 있습니다.");
        return false;
    }
    fs::rename(DictFile(dict), target, error);
    if (error) {
        ReportError(app, L"사전 파일의 이름을 바꾸지 못했습니다.");
        return false;
    }
    dict.path = path;
    app.reloadDicts = true;
    return true;
}

void ImportDicts(App& app, const std::vector<fs::path>& paths) {
    const fs::path folder = DictFolder(app);
    if (folder.empty()) return;
    fs::path last;
    std::wstring failed;
    for (const fs::path& path : paths) {
        const fs::path imported = gui::ImportUserDict(path, folder);
        if (imported.empty()) failed += L"\n" + path.filename().native();
        else last = imported;
    }
    if (!last.empty()) {
        app.reloadDicts = true;
        ShowDicts(app, last);
    }
    if (!failed.empty()) ReportError(app, (L"가져오지 못한 파일이 있습니다.\n" + failed).c_str());
}

void ExportDict(App& app) {
    const gui::UserDict* dict = CurrentDict(app);
    if (!dict) return;
    std::wstring buffer(32768, L'\0');
    const std::wstring name = dict->path.filename().native();
    buffer.replace(0, name.size(), name);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.main;
    dialog.lpstrFilter = kDictFilter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&dialog)) return;
    std::error_code error;
    fs::copy_file(DictFile(*dict), fs::path(buffer.c_str()), fs::copy_options::overwrite_existing, error);
    if (error) ReportError(app, L"사전 파일을 내보내지 못했습니다.");
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

void TakeDrop(App& app, HDROP drop) {
    const std::vector<fs::path> paths = DroppedFiles(drop);
    if (paths.empty()) return;
    if (CurrentPage(app) == kDictsPage) {
        ImportDicts(app, paths);
        return;
    }
    if (app.busy) return;
    ShowPage(app, kDocsPage);
    AddInputFiles(app, paths);
}

App* DialogApp(HWND window, UINT message, LPARAM data) {
    if (message == WM_INITDIALOG) SetWindowLongPtrW(window, GWLP_USERDATA, data);
    return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

INT_PTR CALLBACK TextProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = DialogApp(window, message, lParam);
    if (!app) return FALSE;
    switch (message) {
    case WM_INITDIALOG:
        app->text = window;
        InitOptions(window);
        for (int id : {IDC_SOURCE, IDC_RESULT}) SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, INT_MAX - 1, 0);
        return FALSE;
    case WM_SIZE: LayoutText(window); return TRUE;
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
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK DocsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = DialogApp(window, message, lParam);
    if (!app) return FALSE;
    switch (message) {
    case WM_INITDIALOG:
        app->docs = window;
        InitOptions(window);
        InsertColumns(GetDlgItem(window, IDC_FILES), {L"파일", L"상태"}, LVS_EX_FULLROWSELECT);
        SendDlgItemMessageW(window, IDC_PROGRESS, PBM_SETRANGE32, 0, 1);
        ShowOutput(*app);
        return FALSE;
    case WM_SIZE: LayoutDocs(window); return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BROWSE_INPUT: {
            std::vector<fs::path> paths;
            if (!app->busy && PickFiles(app->main, kTextFilter, paths)) AddInputFiles(*app, paths);
            return TRUE;
        }
        case IDC_REMOVE: RemoveFiles(*app); return TRUE;
        case IDC_BROWSE_OUTPUT: {
            fs::path folder = app->outputFolder;
            if (folder.empty() && !app->inputFiles.empty()) folder = app->inputFiles[0].parent_path();
            if (PickFolder(app->main, folder)) {
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
        case IDC_STOP: StopFile(*app); return TRUE;
        }
        break;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->idFrom != IDC_FILES || app->filling) break;
        if (header->code == LVN_ITEMCHANGED) {
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
            if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED)) ShowPreview(*app);
        } else if (header->code == LVN_KEYDOWN && reinterpret_cast<const NMLVKEYDOWN*>(lParam)->wVKey == VK_DELETE) {
            RemoveFiles(*app);
        }
        break;
    }
    case kStart: StartFileWorker(*app); return TRUE;
    case kPhase: ShowPhase(*app, wParam, lParam); return TRUE;
    case kDone: FinishFile(*app); return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK DictsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = DialogApp(window, message, lParam);
    if (!app) return FALSE;
    switch (message) {
    case WM_INITDIALOG: {
        app->dicts = window;
        InsertColumns(GetDlgItem(window, IDC_DICTS), {L""}, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        InsertColumns(GetDlgItem(window, IDC_ENTRIES), {L"일본어", L"한국어", L"품사", L"옵션"}, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        for (const wchar_t* pos : {L"n", L"phrase", L"name", L"surname", L"place", L"intj", L"hon", L"v", L"v1", L"v5",
                                   L"adj", L"na", L"adv", L"ono", L"end", L"pat"}) {
            SendDlgItemMessageW(window, IDC_ENTRY_POS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pos));
        }
        const std::pair<int, const wchar_t*> cues[] = {{IDC_ENTRY_KEY, L"일본어"}, {IDC_ENTRY_KO, L"한국어"}, {IDC_ENTRY_OPTIONS, L"옵션"}};
        for (const auto& [id, cue] : cues) SendDlgItemMessageW(window, id, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
        SendDlgItemMessageW(window, IDC_ENTRY_POS, CB_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"품사"));
        app->userSources = UserSources(hdTrans::frontend::ModuleDirectory());
        ShowDicts(*app, {});
        return FALSE;
    }
    case WM_SIZE: LayoutDicts(window); return TRUE;
    case kRelist: ShowDicts(*app, {}); return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_DICT_NEW: NewDict(*app); return TRUE;
        case IDC_DICT_IMPORT: {
            std::vector<fs::path> paths;
            if (PickFiles(app->main, kDictFilter, paths)) ImportDicts(*app, paths);
            return TRUE;
        }
        case IDC_DICT_EXPORT: ExportDict(*app); return TRUE;
        case IDC_ENTRY_ADD:
        case IDC_ENTRY_EDIT:
        case IDC_ENTRY_DELETE: ChangeEntry(*app, LOWORD(wParam)); return TRUE;
        }
        break;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->idFrom == IDC_DICTS) {
            if (header->code == LVN_ENDLABELEDITW) {
                const auto* edit = reinterpret_cast<const NMLVDISPINFOW*>(lParam);
                if (RenameDict(*app, edit->item.iItem, edit->item.pszText)) PostMessageW(window, kRelist, 0, 0);
                SetWindowLongPtrW(window, DWLP_MSGRESULT, FALSE);
                return TRUE;
            }
            if (header->code != LVN_ITEMCHANGED || app->filling) break;
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
            if (!(change->uChanged & LVIF_STATE) || change->iItem < 0) break;
            if ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) {
                ToggleDict(*app, change->iItem, (change->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2));
            }
            if ((change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) OpenDict(*app, change->iItem);
        } else if (header->idFrom == IDC_ENTRIES) {
            if (header->code == LVN_GETDISPINFOW) {
                auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                const auto row = static_cast<std::size_t>(info->item.iItem);
                if ((info->item.mask & LVIF_TEXT) && row < app->rows.size() && info->item.cchTextMax > 0) {
                    const gui::DictRow& entry = app->rows[row];
                    const std::string* fields[] = {&entry.key, &entry.ko, &entry.pos, &entry.options};
                    const std::string* field = info->item.iSubItem >= 0 && info->item.iSubItem < 4 ? fields[info->item.iSubItem] : nullptr;
                    lstrcpynW(info->item.pszText, field ? Wide(*field).c_str() : L"", info->item.cchTextMax);
                }
                return TRUE;
            }
            if (header->code == LVN_ITEMCHANGED && !app->filling) {
                const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
                if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) {
                    ShowEntry(*app, change->iItem);
                }
            } else if (header->code == LVN_KEYDOWN && reinterpret_cast<const NMLVKEYDOWN*>(lParam)->wVKey == VK_DELETE) {
                ChangeEntry(*app, IDC_ENTRY_DELETE);
            }
        }
        break;
    }
    }
    return FALSE;
}

INT_PTR CALLBACK MainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = DialogApp(window, message, lParam);
    if (!app) return FALSE;
    switch (message) {
    case WM_INITDIALOG: {
        app->main = window;
        app->tabs = GetDlgItem(window, IDC_TABS);
        for (int page = 0; page < 3; ++page) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(kPageNames[page]);
            TabCtrl_InsertItem(app->tabs, page, &item);
        }
        const LPARAM data = reinterpret_cast<LPARAM>(app);
        if (!CreateDialogParamW(app->instance, MAKEINTRESOURCEW(IDD_PAGE_TEXT), window, TextProc, data) ||
            !CreateDialogParamW(app->instance, MAKEINTRESOURCEW(IDD_PAGE_DOCS), window, DocsProc, data) ||
            !CreateDialogParamW(app->instance, MAKEINTRESOURCEW(IDD_PAGE_DICTS), window, DictsProc, data)) {
            DestroyWindow(window);
            return FALSE;
        }
        DragAcceptFiles(window, TRUE);
        SetFont(window, app->font);
        const Placer p(window);
        const RECT frame = FitInside({p(640), p(680)}, WorkArea(window));
        SetWindowPos(window, nullptr, frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutAll(*app);
        ShowPage(*app, kTextPage);
        SetFocus(GetDlgItem(app->text, IDC_SOURCE));
        return FALSE;
    }
    case WM_SIZE: LayoutMain(*app); return TRUE;
    case WM_GETMINMAXINFO: {
        const Placer p(window);
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        const RECT fit = FitInside({p(560), p(560)}, WorkArea(window));
        limits->ptMinTrackSize = {fit.right - fit.left, fit.bottom - fit.top};
        return TRUE;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        PostMessageW(window, kFont, 0, 0);
        return TRUE;
    }
    case kFont:
        SetFont(window, app->font);
        LayoutAll(*app);
        return TRUE;
    case WM_NOTIFY:
        if (reinterpret_cast<const NMHDR*>(lParam)->idFrom == IDC_TABS && reinterpret_cast<const NMHDR*>(lParam)->code == TCN_SELCHANGE) {
            ShowPage(*app, CurrentPage(*app));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            SendMessageW(window, WM_CLOSE, 0, 0);
            return TRUE;
        }
        break;
    case WM_DROPFILES: TakeDrop(*app, reinterpret_cast<HDROP>(wParam)); return TRUE;
    case kDone: FinishShort(*app); return TRUE;
    case WM_CLOSE:
        if (app->busy) {
            app->closing = true;
            app->cancel = true;
        } else {
            DestroyWindow(window);
        }
        return TRUE;
    case WM_NCDESTROY:
        if (app->font) DeleteObject(app->font);
        app->font = nullptr;
        app->main = nullptr;
        if (!app->selftest) PostQuitMessage(0);
        break;
    }
    return FALSE;
}

void Dispatch(App& app, MSG& message) {
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

std::wstring TranslateShort(App& app, const wchar_t* source) {
    SetDlgItemTextW(app.text, IDC_SOURCE, source);
    NotifyClick(app.text, IDC_TRANSLATE);
    if (!WaitForJob(app) || !app.error.empty()) return L"(error)";
    return ControlText(app.text, IDC_RESULT);
}

// The dictionary page on the user folder of the default layout under program.
bool SelfTestDicts(App& app, const fs::path& program, const fs::path& user) {
    app.userSources = UserSources(Utf8(program.native()));
    ShowDicts(app, {});
    const HWND list = GetDlgItem(app.dicts, IDC_DICTS);
    const HWND grid = GetDlgItem(app.dicts, IDC_ENTRIES);
    const fs::path file = user / L"USERDICT.TSV";
    bool ok = Check(app.userDicts.size() == 1 && app.dictIndex == 0 && app.rows.size() == 1 && ListView_GetCheckState(list, 0),
                    "dictionary page lists the user dictionary");
    if (!ok) return false;
    Select(grid, 0);
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"고친사용자");
    NotifyClick(app.dicts, IDC_ENTRY_EDIT);
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"鳥");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"추가사용자");
    NotifyClick(app.dicts, IDC_ENTRY_ADD);
    std::string text;
    gui::TextEncoding encoding{};
    ok = Check(gui::ReadTextFile(file, text, encoding) && encoding == gui::TextEncoding::Utf8 &&
               text == "犬\t고친사용자\n鳥\t추가사용자\n", "edited dictionary file") && ok;
    ok = Check(TranslateShort(app, L"鳥\r\n犬") == L"추가사용자\r\n고친사용자", "translation reads the edited dictionary") && ok;

    ListView_SetCheckState(list, 0, FALSE);
    std::error_code error;
    ok = Check(!fs::exists(file, error) && fs::exists(gui::OffPath(file), error), "turning a dictionary off renames it") && ok;
    ok = Check(TranslateShort(app, L"犬") != L"고친사용자", "translation skips a dictionary turned off") && ok;
    ShowDicts(app, {});
    ok = Check(app.userDicts.size() == 1 && !app.userDicts[0].on && !ListView_GetCheckState(list, 0) && app.rows.size() == 2,
               "a dictionary stays off when listed again") && ok;
    ListView_SetCheckState(list, 0, TRUE);
    Select(grid, 1);
    NotifyClick(app.dicts, IDC_ENTRY_DELETE);
    ok = Check(gui::ReadTextFile(file, text, encoding) && text == "犬\t고친사용자\n", "turned on and deleted a row") && ok;

    NotifyClick(app.dicts, IDC_DICT_NEW);
    ok = Check(fs::exists(gui::UserDictPath(user, "새 사전"), error) && app.userDicts.size() == 2 && app.rows.empty(),
               "new dictionary") && ok;
    const fs::path places = program / L"places.txt";
    ok = Check(WriteFixture(places, "東京\t도쿄\n"), "write dictionary to import") && ok;
    ShowPage(app, kDictsPage);
    DropFiles(app.main, {places});
    ok = Check(CurrentPage(app) == kDictsPage && app.userDicts.size() == 3 && app.dictIndex >= 0 &&
               app.userDicts[static_cast<std::size_t>(app.dictIndex)].path == user / L"userdict-places.txt" && app.rows.size() == 1,
               "a file dropped on the dictionary page is imported") && ok;
    ShowPage(app, kTextPage);
    return ok;
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
    CheckRadioButton(app.text, IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_SPACED, IDC_OUTPUT_TRANSLATION);
    CheckDlgButton(app.text, IDC_WHOLE_DOCUMENT, BST_CHECKED);
    ok = Check(TranslateShort(app, L"猫\r\n犬") == L"설정사전\r\n사용자설정", "GUI configured dictionary and relative user path") && ok;
    fs::remove(config, error);
    if (!Check(!error, "remove optional config")) return false;
    app.engine.reset();
    if (!Check(LoadEngine(app, programDir), "load default mixed-case data layout")) return false;
    const wchar_t* restoredCache = _wgetenv(L"HDTRANS_CACHE_DIR");
    ok = Check(previousCache ? restoredCache && *previousCache == restoredCache : !restoredCache,
               "GUI restores cache environment") && ok;
    ok = Check(app.settings.cg && !app.settings.misread && !app.settings.reference, "GUI defaults without config") && ok;
    ok = Check(TranslateShort(app, L"猫\r\n犬") == L"기본사전\r\n기본사용자", "GUI default dictionary and user folder") && ok;
    ok = SelfTestDicts(app, program, user) && ok;
    app.engine.reset();
    return ok;
}

bool SelfTest(App& app) {
    bool ok = Check(!IsWindowVisible(app.main), "hidden window");
    const RECT fit = FitInside({640, 680}, {0, 0, 800, 552});
    ok = Check(fit.left == 80 && fit.top == 0 && fit.right == 720 && fit.bottom == 552, "window fits the work area") && ok;
    ok = Check(WindowText(app.main) == Title(kTextPage) && Title(kTextPage).starts_with(L"hdTrans v"), "window title") && ok;
    ok = Check(TranslateShort(app, L"日本語") == L"일본어", "short translation through dialog messages") && ok;
    CheckRadioButton(app.text, IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_SPACED, IDC_OUTPUT_INTERLEAVED);
    ok = Check(TranslateShort(app, L"日本語") == L"日本語\r\n일본어", "source and translation output") && ok;
    NotifyClick(app.text, IDC_CLEAR);
    ok = Check(ControlText(app.text, IDC_SOURCE).empty() && ControlText(app.text, IDC_RESULT).empty(), "clear button") && ok;

    std::error_code error;
    const fs::path base = fs::temp_directory_path(error);
    if (!Check(!error, "temporary folder")) return false;
    const fs::path folder = base / (L"hdtrans-gui-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!Check(fs::create_directory(folder, error), "create test folder")) return false;
    const fs::path input = folder / L"원문.txt";
    const fs::path output = folder / L"원문_번역.txt";
    ok = Check(gui::WriteTextFile(input, "日本語\n\n日本語\n"), "create input file") && ok;
    ShowPage(app, kDocsPage);
    ok = Check(WindowText(app.main) == Title(kDocsPage) && (GetWindowLongW(app.docs, GWL_STYLE) & WS_VISIBLE) &&
               !(GetWindowLongW(app.text, GWL_STYLE) & WS_VISIBLE),
               "document tab and title") && ok;
    ok = Check(AddInputFiles(app, {input}) && ControlText(app.docs, IDC_PREVIEW) == L"日本語\r\n\r\n日本語\r\n", "preview") && ok;
    ok = Check(IsDlgButtonChecked(app.docs, IDC_SAME_FOLDER) == BST_CHECKED && ControlText(app.docs, IDC_OUTPUT_PATH) == folder.native() &&
               !IsWindowEnabled(GetDlgItem(app.docs, IDC_BROWSE_OUTPUT)), "same folder is the default output") && ok;
    NotifyClick(app.docs, IDC_TRANSLATE);
    ok = Check(app.busy && IsWindowEnabled(GetDlgItem(app.docs, IDC_STOP)), "translation shows stop") && ok;
    if (!WaitForJob(app)) return false;
    std::string text;
    gui::TextEncoding encoding{};
    ok = Check(app.error.empty() && gui::ReadTextFile(output, text, encoding) &&
               encoding == gui::TextEncoding::Utf8Bom && text == "일본어\n\n일본어\n",
               "file translation and BOM output") && ok;
    ok = Check(app.fileStates == std::vector<std::wstring>{L"완료"} && IsWindowEnabled(GetDlgItem(app.docs, IDC_TRANSLATE)) &&
               !IsWindowEnabled(GetDlgItem(app.docs, IDC_STOP)), "document page reenabled after completion") && ok;
    ok = Check(gui::WriteTextFile(output, "unchanged"), "prepare existing output") && ok;
    app.overwriteAnswer = IDNO;
    NotifyClick(app.docs, IDC_TRANSLATE);
    ok = Check(!app.busy && gui::ReadTextFile(output, text, encoding) && text == "unchanged" && app.fileStates[0] == L"건너뜀",
               "declining overwrite skips existing output") && ok;
    app.overwriteAnswer = IDYES;
    NotifyClick(app.docs, IDC_TRANSLATE);
    NotifyClick(app.docs, IDC_STOP);
    if (!WaitForJob(app)) return false;
    ok = Check(app.wasCancelled && gui::ReadTextFile(output, text, encoding) && text == "unchanged", "stop preserves existing output") && ok;

    const fs::path second = folder / L"둘째.txt";
    const fs::path outputFolder = folder / L"출력";
    ok = Check(gui::WriteTextFile(second, "日本語\n", "\r\n") && fs::create_directory(outputFolder, error), "create batch fixtures") && ok;
    ok = Check(AddInputFiles(app, {second, input}) && app.inputFiles == std::vector<fs::path>{input, second}, "add files without duplicates") && ok;
    app.outputFolder = outputFolder;
    CheckDlgButton(app.docs, IDC_SAME_FOLDER, BST_UNCHECKED);
    NotifyClick(app.docs, IDC_SAME_FOLDER);
    ok = Check(!app.sameFolder && ControlText(app.docs, IDC_OUTPUT_PATH) == outputFolder.native() &&
               IsWindowEnabled(GetDlgItem(app.docs, IDC_BROWSE_OUTPUT)), "chosen output folder") && ok;
    NotifyClick(app.docs, IDC_TRANSLATE);
    if (!WaitForJob(app)) return false;
    std::string secondText;
    std::string_view firstBreak;
    std::string_view secondBreak;
    ok = Check(app.error.empty() && app.failed.empty() && app.savedCount == 2 &&
               gui::ReadTextFile(outputFolder / L"원문_번역.txt", text, encoding, &firstBreak) && text == "일본어\n\n일본어\n" &&
               gui::ReadTextFile(outputFolder / L"둘째_번역.txt", secondText, encoding, &secondBreak) && secondText == "일본어\n" &&
               firstBreak == "\n" && secondBreak == "\r\n",
               "batch translation into a folder keeps each source's line breaks") && ok;

    CheckDlgButton(app.docs, IDC_SAME_FOLDER, BST_CHECKED);
    NotifyClick(app.docs, IDC_SAME_FOLDER);
    Select(GetDlgItem(app.docs, IDC_FILES), 1);
    NotifyClick(app.docs, IDC_REMOVE);
    ok = Check(app.inputFiles == std::vector<fs::path>{input}, "remove a file") && ok;
    ok = Check(AddInputFiles(app, {output}), "add a source and its output name") && ok;
    NotifyClick(app.docs, IDC_TRANSLATE);
    ok = Check(!app.busy && gui::ReadTextFile(output, text, encoding) && text == "unchanged",
               "batch never overwrites a source file") && ok;
    ShowPage(app, kTextPage);
    DropFiles(app.main, {outputFolder});
    ok = Check(CurrentPage(app) == kTextPage && app.inputFiles.size() == 2, "a dropped folder alone is ignored") && ok;
    DropFiles(app.main, {outputFolder, second, input});
    ok = Check(CurrentPage(app) == kDocsPage && app.inputFiles == std::vector<fs::path>{input, output, second} &&
               ControlText(app.docs, IDC_PREVIEW) == L"日本語\r\n", "dropped files are appended without duplicates") && ok;
    ShowPage(app, kTextPage);
    ok = SelfTestConfig(app, folder) && ok;
    fs::remove_all(folder, error);
    return ok;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),
                                  ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
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
    app.main = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainProc, reinterpret_cast<LPARAM>(&app));
    if (!app.main || !IsWindow(app.main)) return 1;
    if (app.selftest) {
        const bool passed = SelfTest(app);
        app.cancel = true;
        if (app.worker.joinable()) app.worker.join();
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
