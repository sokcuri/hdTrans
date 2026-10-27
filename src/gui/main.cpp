#include <windows.h>
#include <commctrl.h>
#include <imm.h>
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
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <functional>
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
#include "core/dict/snapshot.h"
#include "core/engine.h"
#include "core/engine/load_info.h"
#include "core/engine/user_watch.h"
#include "frontend/load_info.h"
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
constexpr UINT kLog = WM_APP + 6;
constexpr UINT kChecked = WM_APP + 7;
constexpr UINT_PTR kWatchTimer = 1;
constexpr std::size_t kLogLimit = 300;

enum Page { kTextPage, kDocsPage, kDictsPage, kLogsPage };
constexpr const wchar_t* kPageNames[] = {L"단문 번역", L"문서 번역", L"사전", L"로그"};

struct UserSourceEdit {
    fs::path from;
    fs::path to;
    bool on = true;
};

struct LogEntry {
    std::wstring time;
    std::wstring kind;
    std::wstring message;
};

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
    HWND logs = nullptr;
    std::mutex mutex;  // logs and source edits shared with the worker
    std::vector<UserSourceEdit> sourceEdits;
    std::optional<std::vector<gui::UserDict>> relistSources;
    std::deque<LogEntry> logEntries;
    std::wstring logStatus;
    bool logPosted = false;
    HFONT font = nullptr;
    bool selftest = false;
    bool busy = false;
    bool closing = false;
    bool filling = false;  // list views are being refilled; their notifications are ignored
    hdTrans::internal::Config settings;
    hdTrans::internal::ConfigCache cache;
    std::unique_ptr<hdTrans::Engine> engine;
    std::vector<std::string> engineDictDirs;
    std::vector<std::string> engineUserPaths;
    std::vector<gui::UserDict> engineSources;  // ordered source slots, including files turned off in the GUI
    std::string engineEhndDir;
    std::string engineConfigPath;
    std::string engineModuleDir;
    hdTrans::internal::UserVersion loadedUserVersion;
    hdTrans::internal::FolderVersion loadedEhndVersion;
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
    int removeAnswer = IDYES;
    // Opens a folder in Explorer; the selftest replaces it.
    std::function<bool(const fs::path&)> openFolder;

    std::vector<std::string> userSources;
    std::vector<gui::UserDict> userDicts;
    int dictIndex = -1;
    gui::DictText dictText;
    std::optional<std::pair<std::uintmax_t, fs::file_time_type>> dictStamp;  // dictText as read
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

// Workers update bounded memory only; the main dialog paints it on kLog.
void AddLog(App& app, const wchar_t* kind, std::wstring message, std::wstring status = {}) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t time[32]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_FORCE24HOURFORMAT, &now, L"HH:mm:ss", time, 32);
    std::lock_guard lock(app.mutex);
    app.logEntries.push_front({time, kind, std::move(message)});
    if (app.logEntries.size() > kLogLimit) app.logEntries.pop_back();
    if (!status.empty()) app.logStatus = std::move(status);
    if (!app.logPosted && app.main) app.logPosted = PostMessageW(app.main, kLog, 0, 0) != FALSE;
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
    const int upper = bottom - bh - gap;
    p.Put(IDC_DICTS, m, m, left, upper - p(8) - m);
    p.Column(IDC_DICTS, 0, left - p.Scrollbar() - p(4));
    const int third = (left - 2 * gap) / 3;
    p.Put(IDC_DICT_NEW, m, bottom, third, bh);
    p.Put(IDC_DICT_IMPORT, m + third + gap, bottom, third, bh);
    p.Put(IDC_DICT_EXPORT, m + 2 * (third + gap), bottom, left - 2 * (third + gap), bh);
    p.Put(IDC_DICT_REMOVE, m, upper, third, bh);
    const int x = m + left + p(10), w = p.width - x - m;
    const int fields = bottom - p(8) - field;
    p.Put(IDC_ENTRIES, x, m, w, fields - p(8) - m);
    const int room = w - 4 * gap;
    const int widths[] = {room * 25 / 100, room * 25 / 100, room * 13 / 100, room * 15 / 100,
                          room - 2 * (room * 25 / 100) - room * 13 / 100 - room * 15 / 100};
    const int ids[] = {IDC_ENTRY_KEY, IDC_ENTRY_KO, IDC_ENTRY_POS, IDC_ENTRY_OPTIONS, IDC_ENTRY_NOTE};
    const int inner = w - p.Scrollbar() - p(4);
    for (int column = 0, at = x; column < 5; ++column) {
        p.Put(ids[column], at, fields, widths[column], ids[column] == IDC_ENTRY_POS ? p(200) : field);
        p.Column(IDC_ENTRIES, column, room > 0 ? MulDiv(widths[column], inner, room) : 0);
        at += widths[column] + gap;
    }
    const int bw = p(70);
    p.Put(IDC_ENTRY_ADD, p.width - m - 3 * bw - 2 * gap, bottom, bw, bh);
    p.Put(IDC_ENTRY_EDIT, p.width - m - 2 * bw - gap, bottom, bw, bh);
    p.Put(IDC_ENTRY_DELETE, p.width - m - bw, bottom, bw, bh);
    p.Put(IDC_DICT_FOLDER, x, bottom, p(80), bh);
}

void LayoutLogs(HWND page) {
    const Placer p(page);
    const int m = p(10), bh = p(26), full = p.width - 2 * m;
    p.Put(IDC_LOG_ENTRIES, m, m, full, p.height - 2 * m - bh - p(8));
    p.Column(IDC_LOG_ENTRIES, 0, p(76));
    p.Column(IDC_LOG_ENTRIES, 1, p(60));
    p.Column(IDC_LOG_ENTRIES, 2, std::max(full - p(140) - p.Scrollbar(), p(300)));
    p.Put(IDC_DICT_CHECK, m, p.height - m - bh, p(90), bh);
    p.Put(IDC_LOG_FOLDER, m + p(98), p.height - m - bh, p(90), bh);
    p.Put(IDC_LOG_COPY, m + p(196), p.height - m - bh, p(90), bh);
    p.Put(IDC_LOG_CLEAR, m + p(294), p.height - m - bh, p(90), bh);
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
    for (HWND page : {app.text, app.docs, app.dicts, app.logs}) {
        SetWindowPos(page, nullptr, 0, top + header, p.width, std::max(p.height - top - header - p(24), 0), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    p.Put(IDC_LOG_STATUS, p(10), p.height - p(22), p.width - p(20), p(20));
}

void LayoutAll(App& app) {
    LayoutMain(app);
    LayoutText(app.text);
    LayoutDocs(app.docs);
    LayoutDicts(app.dicts);
    LayoutLogs(app.logs);
}

int CurrentPage(const App& app) {
    return std::clamp(TabCtrl_GetCurSel(app.tabs), 0, static_cast<int>(kLogsPage));
}

void ShowPage(App& app, int page) {
    TabCtrl_SetCurSel(app.tabs, page);
    const HWND pages[] = {app.text, app.docs, app.dicts, app.logs};
    for (int index = 0; index <= kLogsPage; ++index) ShowWindow(pages[index], index == page ? SW_SHOW : SW_HIDE);
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
    const wchar_t* kind = CurrentPage(app) == kDictsPage ? L"사전" : CurrentPage(app) == kDocsPage ? L"문서" : L"엔진";
    AddLog(app, kind, message, std::wstring(kind) + L" 오류");
    if (app.selftest) std::fprintf(stderr, "GUI: %s\n", Utf8(message).c_str());
    else MessageBoxW(app.main, message, L"hdTrans", MB_OK | MB_ICONERROR);
}

std::vector<std::string> UserSources(const std::string& dir) {
    if (dir.empty()) return {};
    const auto config = hdTrans::internal::LoadConfig(hdTrans::dict::ConfigFile(dir));
    return config.settings.userdict.value_or(hdTrans::dict::UserPaths(dir));
}

void QueueSourceEdit(App& app, const fs::path& from, const fs::path& to, bool on) {
    std::lock_guard lock(app.mutex);
    app.sourceEdits.push_back({from, to, on});
    app.reloadDicts = true;
}

void ApplySourceEdits(std::vector<gui::UserDict>& sources, const std::vector<UserSourceEdit>& edits) {
    for (const auto& edit : edits) {
        for (auto& source : sources) {
            if (source.path != edit.from) continue;
            source.path = edit.to;
            source.on = edit.on;
        }
    }
}

std::vector<UserSourceEdit> TakeSourceEdits(App& app, std::vector<gui::UserDict>& sources) {
    std::vector<UserSourceEdit> edits;
    {
        std::lock_guard lock(app.mutex);
        edits.swap(app.sourceEdits);
    }
    ApplySourceEdits(sources, edits);
    return edits;
}

void RestoreSourceEdits(App& app, const std::vector<UserSourceEdit>& edits) {
    if (edits.empty()) return;
    std::lock_guard lock(app.mutex);
    app.sourceEdits.insert(app.sourceEdits.begin(), edits.begin(), edits.end());
}

std::vector<std::string> ActiveUserPaths(const std::vector<gui::UserDict>& sources) {
    std::vector<std::string> paths;
    for (const auto& source : sources) {
        if (source.on && !source.path.empty()) paths.push_back(Utf8(source.path.native()));
    }
    return paths;
}

std::vector<hdTrans::dict::UserSource> RequestedSources(const std::vector<std::string>& paths, bool explicitPath) {
    std::vector<hdTrans::dict::UserSource> sources;
    for (const auto& path : paths) {
        if (path.empty()) continue;
        hdTrans::dict::UserSource source;
        source.path = path;
        source.explicitPath = explicitPath;
        sources.push_back(std::move(source));
    }
    return sources;
}

std::size_t LogDiagnosticLines(App& app, const wchar_t* kind, std::string_view text) {
    std::size_t count = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const auto line = text.substr(0, end);
        if (!line.empty()) {
            AddLog(app, kind, Wide(line));
            OutputDebugStringW(Wide("hdTransGUI: " + std::string(line) + "\n").c_str());
            ++count;
        }
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return count;
}

void LogDictionaryResult(App& app, const hdTrans::Engine& engine, bool loaded, bool reload, std::size_t configWarnings) {
    std::size_t warnings = configWarnings + LogDiagnosticLines(app, L"사전", engine.UserFileError());
    for (const auto& line : engine.FilterErrors()) warnings += LogDiagnosticLines(app, L"사전", line);
    const std::wstring message = loaded ? (reload ? L"사전을 다시 읽었습니다." : L"사전을 읽었습니다.") :
        (reload ? L"사전을 다시 읽지 못했습니다. 이전 사전 유지." : L"사전을 읽지 못했습니다.");
    const std::wstring status = !loaded ? (reload ? L"사전 다시 읽기 실패" : L"사전 읽기 실패") :
        warnings ? (configWarnings ? L"경고 " : L"사전 경고 ") + std::to_wstring(warnings) + L"건" : L"사전 읽음";
    AddLog(app, L"사전", message, status);
}

void LogActiveSourceInfo(App& app) {
    if (!app.engine || !app.engine->loaded()) return;
    const auto info = hdTrans::internal::LoadedSourceInfo(*app.engine);
    for (const auto& file : info.files) {
        LogDiagnosticLines(app, L"INFO", hdTrans::frontend::FormatLoadFileInfo(file, app.engineModuleDir));
    }
}

void CommitSettings(App& app, const hdTrans::internal::Config& settings) {
    if (app.settings.dicts != settings.dicts) app.settings.dicts = settings.dicts;
    if (app.settings.userdict != settings.userdict) app.settings.userdict = settings.userdict;
    if (app.settings.ehnd != settings.ehnd) app.settings.ehnd = settings.ehnd;
    if (app.settings.useEhnd != settings.useEhnd) app.settings.useEhnd = settings.useEhnd;
    if (app.settings.cache != settings.cache) app.settings.cache = settings.cache;
    if (app.settings.strictDict != settings.strictDict) app.settings.strictDict = settings.strictDict;
    if (app.settings.cg != settings.cg) app.settings.cg = settings.cg;
    if (app.settings.misread != settings.misread) app.settings.misread = settings.misread;
    if (app.settings.reference != settings.reference) app.settings.reference = settings.reference;
    if (app.settings.speech != settings.speech) app.settings.speech = settings.speech;
    if (app.settings.wrapped != settings.wrapped) app.settings.wrapped = settings.wrapped;
}

bool ReadDictionaries(App& app, const hdTrans::internal::ConfigResult& config, const std::string& dir,
                      const std::string& configPath, hdTrans::internal::FileVersion configVersion) {
    const bool initial = !app.engine || !app.engine->loaded();
    if (config.sourceStamp) {
        configVersion.present = true;
        configVersion.stamp = *config.sourceStamp;
    }
    const bool configChanged = configVersion != app.loadedUserVersion.config;
    const auto& settings = config.settings;
    const bool newSources = initial || settings.userdict != app.settings.userdict;
    const auto dirs = initial || settings.dicts != app.settings.dicts ?
        settings.dicts.value_or(std::vector<std::string>{hdTrans::dict::ShippedFolder(dir)}) : app.engineDictDirs;
    auto sources = app.engineSources;
    if (newSources) {
        sources.clear();
        for (const auto& path : settings.userdict.value_or(hdTrans::dict::UserPaths(dir)))
            sources.push_back({fs::path(Wide(path)), true});
    }
    const auto edits = TakeSourceEdits(app, sources);
    const auto userPaths = ActiveUserPaths(sources);
    // GUI edits change paths, not whether their source came from YAML.
    const auto requested = RequestedSources(userPaths, settings.userdict.has_value());
    const bool newEhnd = initial || settings.useEhnd != app.settings.useEhnd || settings.ehnd != app.settings.ehnd;
    const std::string ehndDir = newEhnd ?
        (settings.useEhnd ? settings.ehnd.value_or(hdTrans::dict::EhndFolder(dir)) : "") : app.engineEhndDir;
    auto userVersion = hdTrans::internal::UserVersionOf(requested, configPath);
    // Without a parse-entry stamp, retain the conservative pre-parse observation.
    userVersion.config = configVersion;
    const auto ehndVersion = hdTrans::internal::FolderVersionOf(ehndDir);
    const auto report = [&](const hdTrans::Engine& engine, bool loaded) {
        // Keep diagnostics newer than INFO in the bounded log.
        LogActiveSourceInfo(app);
        auto configWarnings = LogDiagnosticLines(app, L"엔진", hdTrans::internal::ConfigErrors(config, configPath));
        for (const auto& line : hdTrans::dict::FolderWarnings(dir)) configWarnings += LogDiagnosticLines(app, L"사전", line);
        LogDictionaryResult(app, engine, loaded, !initial, configWarnings);
    };
    const bool cacheChanged = settings.cache != app.settings.cache;
    if (cacheChanged && !app.cache.Apply(settings.cache)) {
        app.error = L"캐시 폴더 설정을 적용하지 못했습니다.";
        if (!app.cache.Apply(app.settings.cache)) app.error += L"\n이전 캐시 설정을 복원하지 못했습니다.";
        RestoreSourceEdits(app, edits);
        app.reloadDicts = true;
        LogActiveSourceInfo(app);
        LogDiagnosticLines(app, L"엔진", hdTrans::internal::ConfigErrors(config, configPath));
        AddLog(app, L"엔진", app.error, L"설정 적용 실패");
        return false;
    }
    std::unique_ptr<hdTrans::Engine> candidate;
    hdTrans::Engine* attempt = app.engine.get();
    if (configChanged || newEhnd || settings.reference != app.settings.reference) {
        hdTrans::EngineOptions options;
        options.defaultStrictDict = false;
        options.configPath = configPath;
        candidate = std::make_unique<hdTrans::Engine>(settings.reference, hdTrans::CacheWrite::Off, hdTrans::UserReload::Off, options);
        if (!initial) {
            std::optional<hdTrans::dict::UserSource> acceptedEhnd;
            auto accepted = hdTrans::internal::AcceptedUserSources(*app.engine, acceptedEhnd);
            hdTrans::internal::SeedUserSources(*candidate, std::move(accepted), std::move(acceptedEhnd));
        }
        hdTrans::dict::UserSource ehndSource;
        ehndSource.path = ehndDir;
        ehndSource.explicitPath = settings.useEhnd && settings.ehnd.has_value();
        hdTrans::internal::OpenEhndSource(*candidate, ehndSource, config);
        attempt = candidate.get();
    }
    const bool reconfigure = candidate || newSources || dirs != app.engineDictDirs || userPaths != app.engineUserPaths;
    bool loaded = reconfigure ? hdTrans::internal::OpenUserSources(*attempt, dirs, false, requested, config) :
        hdTrans::internal::ReloadWithConfig(*attempt, config);
    if (loaded && candidate && !initial) {
        const auto errors = hdTrans::internal::UserFileErrors(*attempt);
        loaded = std::none_of(errors.begin(), errors.end(), [](const auto& error) { return hdTrans::dict::FailsLoad(error.kind); }) &&
                 !hdTrans::internal::FilterFileFailed(*attempt);
    }
    if (!loaded) {
        report(*attempt, false);
        app.error = initial ? L"사전을 읽지 못했습니다." : L"사전을 다시 읽지 못했습니다.";
        const std::string userError = attempt->UserFileError();
        if (!userError.empty()) app.error += L"\n" + Wide(userError);
        for (const auto& line : attempt->FilterErrors()) app.error += L"\n" + Wide(line);
        if (cacheChanged && !app.cache.Apply(app.settings.cache)) {
            app.error += L"\n이전 캐시 설정을 복원하지 못했습니다.";
            AddLog(app, L"엔진", app.error, L"설정 복원 실패");
        }
        RestoreSourceEdits(app, edits);
        app.reloadDicts = true;
        return false;
    }
    if (candidate) app.engine = std::move(candidate);
    CommitSettings(app, settings);
    app.engineDictDirs = dirs;
    app.engineUserPaths = userPaths;
    {
        std::lock_guard lock(app.mutex);
        if (newSources || app.relistSources) {
            app.relistSources = sources;
            PostMessageW(app.dicts, kRelist, 0, 0);
        }
    }
    app.engineSources = std::move(sources);
    app.engineEhndDir = ehndDir;
    app.engineConfigPath = configPath;
    app.engineModuleDir = dir;
    app.loadedUserVersion = userVersion;
    app.loadedEhndVersion = ehndVersion;
    app.error.clear();
    report(*app.engine, true);
    return true;
}

bool LoadEngine(App& app, std::string dir = {}) {
    if (app.engine && app.engine->loaded()) return true;
    app.reloadDicts.exchange(false);
    if (dir.empty()) dir = hdTrans::frontend::ModuleDirectory();
    if (dir.empty()) {
        app.error = L"실행 파일의 위치를 찾지 못했습니다.";
        AddLog(app, L"엔진", app.error, L"엔진 초기화 실패");
        app.reloadDicts = true;
        return false;
    }
    const std::string configPath = hdTrans::dict::ConfigFile(dir);
    const auto version = hdTrans::internal::VersionOf(configPath);
    return ReadDictionaries(app, hdTrans::internal::LoadConfig(configPath), dir, configPath, version);
}

// Reloads changed sources before translation; unchanged healthy sources do not reload.
bool ReadyEngine(App& app) {
    if (!app.engine || !app.engine->loaded()) return LoadEngine(app);
    const bool reload = app.reloadDicts.exchange(false);
    const auto requested = RequestedSources(app.engineUserPaths, app.settings.userdict.has_value());
    const auto userVersion = hdTrans::internal::UserVersionOf(requested, app.engineConfigPath);
    const auto ehndVersion = hdTrans::internal::FolderVersionOf(app.engineEhndDir);
    bool pending;
    {
        std::lock_guard lock(app.mutex);
        pending = !app.sourceEdits.empty();
    }
    if (!reload && !pending && userVersion == app.loadedUserVersion && ehndVersion == app.loadedEhndVersion) return true;
    return ReadDictionaries(app, hdTrans::internal::LoadConfig(app.engineConfigPath), app.engineModuleDir,
                            app.engineConfigPath, userVersion.config);
}

gui::TranslationOptions ConfiguredOptions(const App& app, gui::TranslationOptions options) {
    options.cg = app.settings.cg;
    options.misread = app.settings.misread;
    options.speech = app.settings.speech;
    options.wrapped = app.settings.wrapped;
    return options;
}

void ResetJob(App& app) {
    app.cancel = false;
    app.result.clear();
    app.error.clear();
    app.wasCancelled = false;
    app.busy = true;
    EnableWindow(GetDlgItem(app.logs, IDC_DICT_CHECK), FALSE);
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
    EnableWindow(GetDlgItem(app.logs, IDC_DICT_CHECK), TRUE);
    EnableShortControls(app, true);
    if (app.closing) {
        DestroyWindow(app.main);
        return;
    }
    if (app.error.empty() && !app.wasCancelled) SetDlgItemTextW(app.text, IDC_RESULT, EditText(app.result).c_str());
}

void StartDictionaryCheck(App& app) {
    if (app.busy) return;
    ResetJob(app);
    for (HWND page : {app.text, app.docs}) EnableWindow(GetDlgItem(page, IDC_TRANSLATE), FALSE);
    app.reloadDicts = true;
    app.worker = std::thread([&app] {
        if (!app.cancel) ReadyEngine(app);
        PostMessageW(app.main, kChecked, 0, 0);
    });
}

void FinishDictionaryCheck(App& app) {
    if (app.worker.joinable()) app.worker.join();
    app.busy = false;
    EnableWindow(GetDlgItem(app.logs, IDC_DICT_CHECK), TRUE);
    for (HWND page : {app.text, app.docs}) EnableWindow(GetDlgItem(page, IDC_TRANSLATE), TRUE);
    if (app.closing) DestroyWindow(app.main);
}

void CopyText(App& app, const std::wstring& text) {
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

void CopyResult(App& app) {
    CopyText(app, ControlText(app.text, IDC_RESULT));
}

void CopyLogs(App& app) {
    std::wstring text;
    {
        std::lock_guard lock(app.mutex);
        for (const auto& entry : app.logEntries) text += entry.time + L"\t" + entry.kind + L"\t" + entry.message + L"\r\n";
    }
    CopyText(app, text);
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

void ShowLogs(App& app) {
    std::deque<LogEntry> entries;
    std::wstring status;
    {
        std::lock_guard lock(app.mutex);
        entries = app.logEntries;
        status = app.logStatus;
        app.logPosted = false;
    }
    const HWND list = GetDlgItem(app.logs, IDC_LOG_ENTRIES);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    int index = 0;
    for (const auto& entry : entries) {
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = index;
        row.pszText = const_cast<wchar_t*>(entry.time.c_str());
        ListView_InsertItem(list, &row);
        SetCell(list, index, 1, entry.kind);
        SetCell(list, index++, 2, entry.message);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    SetDlgItemTextW(app.main, IDC_LOG_STATUS, status.c_str());
}

void ClearLogs(App& app) {
    {
        std::lock_guard lock(app.mutex);
        app.logEntries.clear();
        app.logStatus.clear();
    }
    ShowLogs(app);
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
    bool unreadable = false;
    for (const fs::path& path : paths) {
        std::string text;
        gui::TextEncoding encoding{};
        if (gui::ReadTextFile(path, text, encoding)) readable.push_back(path);
        else {
            unreadable = true;
            AddLog(app, L"문서", path.native() + L": 텍스트 파일을 읽지 못했습니다.", L"문서 읽기 오류");
        }
    }
    const std::size_t before = app.inputFiles.size();
    gui::AppendFiles(app.inputFiles, readable);
    app.fileStates.resize(app.inputFiles.size());
    ShowFiles(app, app.inputFiles.size() > before ? static_cast<int>(before) : -1);
    return !unreadable;
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
                AddLog(app, L"문서", job.input.native() + L": 파일을 읽지 못했습니다.", L"문서 읽기 오류");
                PostMessageW(app.docs, kPhase, kFailed, position);
                continue;
            }
            if (!ReadyEngine(app)) {
                app.failed.push_back(job.input.filename().native() + L" (사전)");
                AddLog(app, L"문서", job.input.native() + L": " + app.error);
                PostMessageW(app.docs, kPhase, kFailed, position);
                break;
            }
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
                AddLog(app, L"문서", job.output.native() + L": 파일을 저장하지 못했습니다.", L"문서 저장 오류");
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
    EnableWindow(GetDlgItem(app.logs, IDC_DICT_CHECK), TRUE);
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
    } else if (app.wasCancelled) {
        SetDlgItemTextW(app.docs, IDC_STATUS, (L"중단했습니다. " + status + L" 나머지는 저장하지 않았습니다.").c_str());
    } else if (!app.failed.empty()) {
        SetDlgItemTextW(app.docs, IDC_STATUS, (L"처리하지 못한 파일 " + std::to_wstring(app.failed.size()) + L"개. " + status).c_str());
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
    SetDlgItemTextW(app.dicts, IDC_ENTRY_NOTE, Wide(entry.note).c_str());
}

// Lists the rows of the open file and selects row; fill also shows that row in the entry fields.
void ShowEntries(App& app, int row, bool fill = true) {
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
    for (int id : {IDC_ENTRIES, IDC_ENTRY_KEY, IDC_ENTRY_KO, IDC_ENTRY_POS, IDC_ENTRY_OPTIONS, IDC_ENTRY_NOTE, IDC_ENTRY_ADD,
                   IDC_ENTRY_EDIT, IDC_ENTRY_DELETE}) {
        EnableWindow(GetDlgItem(app.dicts, id), editable);
    }
    EnableWindow(GetDlgItem(app.dicts, IDC_DICT_EXPORT), CurrentDict(app) ? TRUE : FALSE);
    EnableWindow(GetDlgItem(app.dicts, IDC_DICT_REMOVE), CurrentDict(app) ? TRUE : FALSE);
    if (fill) ShowEntry(app, row);
}

std::optional<std::pair<std::uintmax_t, fs::file_time_type>> FileStamp(const fs::path& path) {
    std::error_code sizeError, timeError;
    const auto size = fs::file_size(path, sizeError);
    const auto time = fs::last_write_time(path, timeError);
    if (sizeError || timeError) return std::nullopt;
    return std::pair{size, time};
}

std::string ProgramDir(const App& app) {
    return app.engineModuleDir.empty() ? hdTrans::frontend::ModuleDirectory() : app.engineModuleDir;
}

// path relative to the data folder, with / separators.
std::string DataRelative(const App& app, const fs::path& path) {
    const fs::path data(Wide(hdTrans::dict::DataFolder(ProgramDir(app))));
    const auto text = path.lexically_proximate(data).generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// Logs "[file] text" as kind; INFO for changes, 사전 for failures and warnings.
void LogDict(App& app, const wchar_t* kind, const fs::path& file, const std::string& text) {
    LogDiagnosticLines(app, kind, "[" + DataRelative(app, file) + "] " + text);
}

// Reads the open file into dictText; false when it cannot be read.
bool ReadOpenDict(App& app) {
    app.dictText = {};
    app.dictStamp.reset();
    const gui::UserDict* dict = CurrentDict(app);
    if (!dict) return true;
    app.dictStamp = FileStamp(DictFile(*dict));
    if (gui::ReadDictText(DictFile(*dict), app.dictText)) return true;
    app.dictText = {};
    app.dictText.column = true;
    return false;
}

void OpenDict(App& app, int index) {
    app.dictIndex = index;
    if (!ReadOpenDict(app))
        ReportError(app, (L"사전 파일을 읽지 못했습니다.\n\n" + DictFile(*CurrentDict(app)).native()).c_str());
    ShowEntries(app, 0);
}

// Rereads the open file when it changed on disk, keeping the selection and the typed entry.
bool RefreshChangedDict(App& app) {
    const gui::UserDict* dict = CurrentDict(app);
    if (!dict || FileStamp(DictFile(*dict)) == app.dictStamp) return false;
    const int row = Selected(GetDlgItem(app.dicts, IDC_ENTRIES));
    ReadOpenDict(app);
    LogDict(app, L"INFO", DictFile(*dict), "외부에서 바뀜, 다시 읽음");
    ShowEntries(app, row, false);
    return true;
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

void RelistDicts(App& app) {
    std::optional<std::vector<gui::UserDict>> sources;
    {
        std::lock_guard lock(app.mutex);
        if (app.relistSources) {
            sources = std::move(app.relistSources);
            app.relistSources.reset();
            ApplySourceEdits(*sources, app.sourceEdits);
        }
    }
    if (sources) {
        app.userSources.clear();
        for (const auto& source : *sources) app.userSources.push_back(Utf8(source.path.native()));
    }
    ShowDicts(app, {});
}

bool SaveDict(App& app) {
    const gui::UserDict* dict = CurrentDict(app);
    if (!dict || !gui::WriteDictText(DictFile(*dict), app.dictText)) {
        ReportError(app, L"사전 파일을 저장하지 못했습니다.");
        return false;
    }
    app.dictStamp = FileStamp(DictFile(*dict));
    app.reloadDicts = true;
    return true;
}

// What the loader compares besides the key when a later row replaces an earlier one: the POS
// (n by default) and the register and stative options.
std::string RowKind(const gui::DictRow& row) {
    std::string kind = row.pos.empty() ? "n" : row.pos;
    for (std::size_t at = 0; at < row.options.size();) {
        const std::size_t space = row.options.find(' ', at);
        const std::string option = row.options.substr(at, space == std::string::npos ? std::string::npos : space - at);
        if (option == "stative" || option.starts_with("register=")) kind += ' ' + option;
        if (space == std::string::npos) break;
        at = space + 1;
    }
    return kind;
}

bool SameRow(const gui::DictRow& a, const gui::DictRow& b) {
    return a.key == b.key && a.ko == b.ko && a.pos == b.pos && a.options == b.options;
}

void ChangeEntry(App& app, int id) {
    const HWND grid = GetDlgItem(app.dicts, IDC_ENTRIES);
    int row = Selected(grid);
    if (!CurrentDict(app) || app.dictText.column) return;
    if (id != IDC_ENTRY_ADD && (row < 0 || static_cast<std::size_t>(row) >= app.rows.size())) return;
    const fs::path file = DictFile(*CurrentDict(app));
    const std::string action = id == IDC_ENTRY_ADD ? "사전 추가" : id == IDC_ENTRY_EDIT ? "사전 수정" : "사전 삭제";
    gui::DictRow entry{Utf8(ControlText(app.dicts, IDC_ENTRY_KEY)), Utf8(ControlText(app.dicts, IDC_ENTRY_KO)),
                       Utf8(ControlText(app.dicts, IDC_ENTRY_POS)), Utf8(ControlText(app.dicts, IDC_ENTRY_OPTIONS)),
                       Utf8(ControlText(app.dicts, IDC_ENTRY_NOTE))};
    // A key starting with # or // makes the line a comment; it is saved as typed.
    const bool comment = !gui::EntryLine(entry.key);
    if (id != IDC_ENTRY_DELETE && entry.key.find_first_not_of(" \t") == std::string::npos) {
        SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_KEY));
        return;
    }
    if (id != IDC_ENTRY_DELETE && !comment) {
        // A row the loader would skip is not saved; the fields stay for correction.
        const auto snapshot = app.engine ? app.engine->Dictionaries() : nullptr;
        const gui::RowProblem problem = gui::CheckRow(entry, snapshot ? &snapshot->Base() : nullptr);
        if (!problem.message.empty()) {
            constexpr int fields[] = {IDC_ENTRY_KEY, IDC_ENTRY_KO, IDC_ENTRY_POS, IDC_ENTRY_OPTIONS};
            LogDict(app, L"사전", file, action + " 실패: " + entry.key + " (" + problem.message + ")");
            SetFocus(GetDlgItem(app.dicts, fields[static_cast<int>(problem.field)]));
            return;
        }
    }
    // A file changed on disk since it was read is read again first, so a save never writes
    // stale rows; an edit or delete then acts on the same row if it is still there.
    std::optional<gui::DictRow> old;
    if (id != IDC_ENTRY_ADD) old = app.rows[static_cast<std::size_t>(row)];
    if (RefreshChangedDict(app) && old) {
        const auto found = std::find_if(app.rows.begin(), app.rows.end(), [&](const auto& r) { return SameRow(r, *old); });
        if (found == app.rows.end()) {
            LogDict(app, L"사전", file, action + " 실패: " + old->key + " (파일이 바뀌어 그 줄이 없습니다)");
            return;
        }
        row = static_cast<int>(found - app.rows.begin());
    }
    if (id == IDC_ENTRY_ADD && !comment) {
        bool same = false, other = false;
        for (const auto& r : app.rows) {
            if (r.key != entry.key) continue;
            (RowKind(r) == RowKind(entry) ? same : other) = true;
        }
        if (same) LogDict(app, L"사전", file, "같은 단어가 이미 있습니다: " + entry.key + " (앞 줄이 우선합니다)");
        else if (other) LogDict(app, L"사전", file, "같은 단어가 다른 품사로 이미 있습니다: " + entry.key);
    }
    const gui::DictText before = app.dictText;
    int next = row;
    if (id == IDC_ENTRY_ADD) {
        app.dictText.lines.push_back(gui::RowLine(entry));
        next = static_cast<int>(app.rows.size());
    } else {
        const std::size_t line = app.rowLines[static_cast<std::size_t>(row)];
        if (id == IDC_ENTRY_EDIT) app.dictText.lines[line] = gui::RowLine(entry);
        else app.dictText.lines.erase(app.dictText.lines.begin() + static_cast<std::ptrdiff_t>(line));
    }
    const bool saved = SaveDict(app);
    if (!saved) {
        app.dictText = before;
        LogDict(app, L"사전", file, action + " 실패: " + (old ? old->key : entry.key) + " (파일을 쓸 수 없습니다)");
    } else if (comment && id != IDC_ENTRY_DELETE) {
        LogDict(app, L"INFO", file, "주석으로 저장됨: " + gui::RowLine(entry));
    } else if (id == IDC_ENTRY_ADD) {
        LogDict(app, L"INFO", file, action + ": " + gui::RowLine(entry));
    } else if (id == IDC_ENTRY_EDIT) {
        // The previous row without its key when the key stayed, so the line reads as the row it replaced.
        const std::string previous = old->key == entry.key ? gui::RowLine({"", old->ko, old->pos, old->options, old->note}).substr(1)
                                                           : gui::RowLine(*old);
        LogDict(app, L"INFO", file, action + ": " + gui::RowLine(entry) + " (이전: " + previous + ")");
    } else {
        LogDict(app, L"INFO", file, action + ": " + gui::RowLine(*old));
    }
    if (id != IDC_ENTRY_ADD) {
        ShowEntries(app, saved ? next : row);
        return;
    }
    // A failed add keeps the typed entry for correction; a saved one selects the new row and
    // clears the fields for the next entry.
    ShowEntries(app, saved ? next : row, false);
    if (saved) ShowEntry(app, -1);
    SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_KEY));
}

void ToggleDict(App& app, int index, bool on) {
    if (index < 0 || static_cast<std::size_t>(index) >= app.userDicts.size()) return;
    gui::UserDict& dict = app.userDicts[static_cast<std::size_t>(index)];
    if (dict.on == on) return;
    if (gui::SetUserDictOn(dict, on)) {
        QueueSourceEdit(app, dict.path, dict.path, on);
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
    LogDict(app, L"INFO", path, "새 사전");
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
    const fs::path previous = dict.path;
    for (auto& source : app.userSources) {
        if (fs::path(Wide(source)) == previous) source = Utf8(path.native());
    }
    dict.path = path;
    QueueSourceEdit(app, previous, path, dict.on);
    LogDict(app, L"INFO", previous, "사전 이름 바꿈: " + DataRelative(app, path));
    return true;
}

// Moves the open dictionary file to the Recycle Bin after a confirmation naming it and its rows.
void RemoveDict(App& app) {
    const gui::UserDict* dict = CurrentDict(app);
    if (!dict) return;
    const fs::path file = DictFile(*dict);
    const std::wstring question = L"이 사전 파일을 휴지통으로 옮길까요?\n\n" + file.filename().native() + L"\n항목 " +
                                  std::to_wstring(app.rows.size()) + L"개";
    const int answer = app.selftest ? app.removeAnswer
                                    : MessageBoxW(app.main, question.c_str(), L"hdTrans", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return;
    std::wstring from = file.native();
    from.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.hwnd = app.main;
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    if (SHFileOperationW(&operation) != 0 || operation.fAnyOperationsAborted) {
        LogDict(app, L"사전", file, "사전 삭제 실패 (파일을 옮길 수 없습니다)");
        ReportError(app, L"사전 파일을 휴지통으로 옮기지 못했습니다.");
        return;
    }
    // A GUI removal, not a disappearance: the reload drops the source.
    QueueSourceEdit(app, dict->path, dict->path, false);
    LogDict(app, L"INFO", file, "사전 삭제됨 (휴지통)");
    ShowDicts(app, {});
}

// Opens folder in Explorer, creating it first when it is missing.
void OpenFolder(App& app, const fs::path& folder) {
    std::error_code error;
    fs::create_directories(folder, error);
    const bool opened = !error && fs::is_directory(folder, error) &&
        (app.openFolder ? app.openFolder(folder)
                        : reinterpret_cast<INT_PTR>(ShellExecuteW(app.main, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32);
    if (!opened) LogDiagnosticLines(app, L"사전", "폴더를 열 수 없습니다: " + Utf8(folder.native()));
}

fs::path DataFolderPath(const App& app) { return fs::path(Wide(hdTrans::dict::DataFolder(ProgramDir(app)))); }
fs::path UserFolderPath(const App& app) { return fs::path(Wide(hdTrans::dict::UserFolder(ProgramDir(app)))); }

// Picks up edits made outside the GUI: the open file is read again, and changed sources reload
// the engine through the dictionary check. Runs once a second while no job is running.
void CheckExternalEdits(App& app) {
    if (app.busy || !app.dicts) return;
    RefreshChangedDict(app);
    if (!app.engine || !app.engine->loaded()) return;
    const auto requested = RequestedSources(app.engineUserPaths, app.settings.userdict.has_value());
    if (hdTrans::internal::UserVersionOf(requested, app.engineConfigPath) != app.loadedUserVersion ||
        hdTrans::internal::FolderVersionOf(app.engineEhndDir) != app.loadedEhndVersion)
        StartDictionaryCheck(app);
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
        InsertColumns(GetDlgItem(window, IDC_ENTRIES), {L"일본어", L"한국어", L"품사", L"옵션", L"메모"}, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
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
    case kRelist: RelistDicts(*app); return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_DICT_NEW: NewDict(*app); return TRUE;
        case IDC_DICT_IMPORT: {
            std::vector<fs::path> paths;
            if (PickFiles(app->main, kDictFilter, paths)) ImportDicts(*app, paths);
            return TRUE;
        }
        case IDC_DICT_EXPORT: ExportDict(*app); return TRUE;
        case IDC_DICT_REMOVE: RemoveDict(*app); return TRUE;
        case IDC_DICT_FOLDER: OpenFolder(*app, UserFolderPath(*app)); return TRUE;
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
                    const std::string* fields[] = {&entry.key, &entry.ko, &entry.pos, &entry.options, &entry.note};
                    const std::string* field = info->item.iSubItem >= 0 && info->item.iSubItem < 5 ? fields[info->item.iSubItem] : nullptr;
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

INT_PTR CALLBACK LogsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = DialogApp(window, message, lParam);
    if (!app) return FALSE;
    switch (message) {
    case WM_INITDIALOG:
        app->logs = window;
        InsertColumns(GetDlgItem(window, IDC_LOG_ENTRIES), {L"시간", L"종류", L"메시지"}, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        return FALSE;
    case WM_SIZE: LayoutLogs(window); return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_DICT_CHECK: StartDictionaryCheck(*app); return TRUE;
        case IDC_LOG_FOLDER: OpenFolder(*app, DataFolderPath(*app)); return TRUE;
        case IDC_LOG_COPY: CopyLogs(*app); return TRUE;
        case IDC_LOG_CLEAR: ClearLogs(*app); return TRUE;
        }
        break;
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
        for (int page = 0; page <= kLogsPage; ++page) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(kPageNames[page]);
            TabCtrl_InsertItem(app->tabs, page, &item);
        }
        const LPARAM data = reinterpret_cast<LPARAM>(app);
        if (!CreateDialogParamW(app->instance, MAKEINTRESOURCEW(IDD_PAGE_TEXT), window, TextProc, data) ||
            !CreateDialogParamW(app->instance, MAKEINTRESOURCEW(IDD_PAGE_DOCS), window, DocsProc, data) ||
            !CreateDialogParamW(app->instance, MAKEINTRESOURCEW(IDD_PAGE_DICTS), window, DictsProc, data) ||
            !CreateDialogParamW(app->instance, MAKEINTRESOURCEW(IDD_PAGE_LOGS), window, LogsProc, data)) {
            DestroyWindow(window);
            return FALSE;
        }
        DragAcceptFiles(window, TRUE);
        if (!app->selftest) SetTimer(window, kWatchTimer, 1000, nullptr);
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
        if (LOWORD(wParam) == IDC_LOG_STATUS && HIWORD(wParam) == STN_CLICKED) {
            ShowPage(*app, kLogsPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            SendMessageW(window, WM_CLOSE, 0, 0);
            return TRUE;
        }
        break;
    case WM_DROPFILES: TakeDrop(*app, reinterpret_cast<HDROP>(wParam)); return TRUE;
    case kLog: ShowLogs(*app); return TRUE;
    case kChecked: FinishDictionaryCheck(*app); return TRUE;
    case WM_TIMER:
        if (wParam == kWatchTimer) CheckExternalEdits(*app);
        return TRUE;
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

// The entry fields of the dictionary page, including the edit box inside the POS combo box.
bool EntryField(const App& app, HWND window) {
    if (!app.dicts || !window) return false;
    for (const int id : {IDC_ENTRY_KEY, IDC_ENTRY_KO, IDC_ENTRY_OPTIONS, IDC_ENTRY_NOTE})
        if (window == GetDlgItem(app.dicts, id)) return true;
    const HWND pos = GetDlgItem(app.dicts, IDC_ENTRY_POS);
    return (window == pos || GetParent(window) == pos) && !SendMessageW(pos, CB_GETDROPPEDSTATE, 0, 0);
}

// Whether an IME holds an uncommitted composition in window.
bool Composing(HWND window) {
    const HIMC context = ImmGetContext(window);
    if (!context) return false;
    const LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
    ImmReleaseContext(window, context);
    return bytes > 0;
}

void Dispatch(App& app, MSG& message) {
    // Enter in an entry field adds the entry, as the 추가 button does. Enter that an IME takes to
    // commit its composition (VK_PROCESSKEY, or a composition still open) does not add.
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && EntryField(app, message.hwnd) &&
        !Composing(message.hwnd)) {
        if (IsWindowEnabled(GetDlgItem(app.dicts, IDC_ENTRY_ADD))) ChangeEntry(app, IDC_ENTRY_ADD);
        return;
    }
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

// Sends a key press as the message loop delivers it, with Shift held when shift is set.
void PressKey(App& app, UINT key, bool shift = false) {
    BYTE keys[256]{};
    GetKeyboardState(keys);
    const BYTE before = keys[VK_SHIFT];
    keys[VK_SHIFT] = shift ? 0x80 : 0;
    SetKeyboardState(keys);
    MSG down{GetFocus(), WM_KEYDOWN, key, 0, 0, {}};
    Dispatch(app, down);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) Dispatch(app, message);
    keys[VK_SHIFT] = before;
    SetKeyboardState(keys);
}

bool Logged(App& app, const wchar_t* kind, const std::wstring& message) {
    std::lock_guard lock(app.mutex);
    return std::any_of(app.logEntries.begin(), app.logEntries.end(),
                       [&](const auto& entry) { return entry.kind == kind && entry.message == message; });
}

bool FocusIn(HWND page, int id) {
    const HWND focus = GetFocus();
    const HWND control = GetDlgItem(page, id);
    return focus && (focus == control || GetParent(focus) == control);
}

// Tab walks the entry fields of an open, empty dictionary and Enter adds the entry; the
// translation page keeps its Tab order and Enter in the source box is a line break.
bool SelfTestEntryKeys(App& app) {
    ShowPage(app, kDictsPage);
    SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_KEY));
    if (!Check(FocusIn(app.dicts, IDC_ENTRY_KEY), "focus the Japanese entry field")) return false;
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"ＺＺ0");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"키보드");
    const std::wstring where = L"[" + Wide(DataRelative(app, DictFile(*CurrentDict(app)))) + L"] ";
    bool ok = true;
    for (const int id : {IDC_ENTRY_KO, IDC_ENTRY_POS, IDC_ENTRY_OPTIONS, IDC_ENTRY_NOTE, IDC_ENTRY_ADD}) {
        PressKey(app, VK_TAB);
        ok = Check(FocusIn(app.dicts, id), "Tab moves to the next entry control") && ok;
    }
    PressKey(app, VK_TAB, true);
    ok = Check(FocusIn(app.dicts, IDC_ENTRY_NOTE), "Shift+Tab moves back from 추가") && ok;
    PressKey(app, VK_RETURN);
    ok = Check(app.rows.size() == 1 && app.rows[0].key == "ＺＺ0" && app.rows[0].ko == "키보드" &&
               ControlText(app.dicts, IDC_ENTRY_KEY).empty() && FocusIn(app.dicts, IDC_ENTRY_KEY) && IsWindow(app.main),
               "Enter in an entry field adds the entry and returns to the Japanese field") && ok;
    PressKey(app, VK_RETURN);
    ok = Check(app.rows.size() == 1, "Enter with an empty Japanese field adds nothing") && ok;
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"ＺＺ9");
    PressKey(app, VK_RETURN);
    ok = Check(app.rows.size() == 1 && ControlText(app.dicts, IDC_ENTRY_KEY) == L"ＺＺ9" && FocusIn(app.dicts, IDC_ENTRY_KO) &&
               Logged(app, L"사전", where + L"사전 추가 실패: ＺＺ9 (한국어가 비어 있습니다)"),
               "an empty Korean field is not saved, keeps the entry and focuses the Korean field") && ok;
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"목록");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_POS, L"zzz");
    SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_KEY));
    PressKey(app, VK_RETURN);
    ok = Check(app.rows.size() == 1 && ControlText(app.dicts, IDC_ENTRY_POS) == L"zzz" && FocusIn(app.dicts, IDC_ENTRY_POS) &&
               Logged(app, L"사전", where + L"사전 추가 실패: ＺＺ9 (알 수 없는 품사입니다: zzz)"),
               "an unknown POS is not saved and focuses the POS field") && ok;
    ShowEntry(app, -1);
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"ＺＺ0");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"키보드2");
    SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_KEY));
    PressKey(app, VK_RETURN);
    ok = Check(app.rows.size() == 2 && Logged(app, L"사전", where + L"같은 단어가 이미 있습니다: ＺＺ0 (앞 줄이 우선합니다)"),
               "a duplicate key is added with a warning") && ok;
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"ＺＺ0");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"키보드3");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_POS, L"name");
    SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_KEY));
    PressKey(app, VK_RETURN);
    ok = Check(app.rows.size() == 3 && Logged(app, L"사전", where + L"같은 단어가 다른 품사로 이미 있습니다: ＺＺ0") &&
               Logged(app, L"INFO", where + L"사전 추가: ＺＺ0\t키보드3\tname"),
               "a key with another POS is added with its own warning and logged as a full row") && ok;
    ShowEntry(app, -1);
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"#メモ");
    SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_KEY));
    PressKey(app, VK_RETURN);
    std::string saved;
    gui::TextEncoding savedEncoding{};
    const fs::path open = DictFile(*CurrentDict(app));
    ok = Check(gui::ReadTextFile(open, saved, savedEncoding) && saved.ends_with("#メモ\n") && app.rows.size() == 3 &&
               Logged(app, L"INFO", where + L"주석으로 저장됨: #メモ"), "a key starting with # is saved as a comment line") && ok;
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"ＺＺ7");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"메모있음");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_NOTE, L"출처 확인");
    SetFocus(GetDlgItem(app.dicts, IDC_ENTRY_NOTE));
    PressKey(app, VK_RETURN);
    ok = Check(gui::ReadTextFile(open, saved, savedEncoding) && saved.ends_with("ＺＺ7\t메모있음\t// 출처 확인\n") && app.rows.size() == 4 &&
               app.rows[3].note == "출처 확인" && Logged(app, L"INFO", where + L"사전 추가: ＺＺ7\t메모있음\t// 출처 확인"),
               "a note is saved as a trailing // field, listed and logged") && ok;
    // An IME composition takes Enter; where no IME can compose, this only reports the skip.
    const HWND keyField = GetDlgItem(app.dicts, IDC_ENTRY_KEY);
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"ＺＺ8");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"고양");
    SetFocus(keyField);
    if (const HIMC context = ImmGetContext(keyField)) {
        ImmSetOpenStatus(context, TRUE);
        const wchar_t composing[] = L"이";
        ImmSetCompositionStringW(context, SCS_SETSTR, const_cast<wchar_t*>(composing), sizeof(wchar_t), nullptr, 0);
        if (Composing(keyField)) {
            PressKey(app, VK_RETURN);
            ok = Check(app.rows.size() == 2, "Enter during an IME composition does not add") && ok;
            ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        } else {
            std::fprintf(stderr, "SKIP no IME composition is available for the Enter check\n");
        }
        ImmReleaseContext(keyField, context);
    } else {
        std::fprintf(stderr, "SKIP no IME context is available for the Enter check\n");
    }
    ShowEntry(app, -1);

    ShowPage(app, kTextPage);
    SetFocus(GetDlgItem(app.text, IDC_WHOLE_DOCUMENT));
    PressKey(app, VK_TAB);
    ok = Check(FocusIn(app.text, IDC_SOURCE), "Tab on the translation page still reaches the source box") && ok;
    SetDlgItemTextW(app.text, IDC_SOURCE, L"日本語");
    SendDlgItemMessageW(app.text, IDC_SOURCE, EM_SETSEL, 3, 3);
    PressKey(app, VK_RETURN);
    ok = Check(ControlText(app.text, IDC_SOURCE) == L"日本語\r\n" && !app.busy && app.rows.size() == 4,
               "Enter in the source box is a line break, not a translation or an add") && ok;
    SetDlgItemTextW(app.text, IDC_SOURCE, L"");
    ShowPage(app, kDictsPage);
    return ok;
}

// Writes text to file as another program would, with a later modification time.
bool WriteExternally(const fs::path& file, const std::string& text) {
    std::error_code error;
    const auto before = fs::last_write_time(file, error);
    if (!gui::WriteTextFile(file, text)) return false;
    fs::last_write_time(file, before + std::chrono::seconds(5), error);
    return !error;
}

// Edits made to the open file outside the GUI: the list rereads them without touching the typed
// entry, the engine reloads, and a GUI change never writes over them.
bool SelfTestExternalEdits(App& app, const fs::path& file, const std::wstring& where) {
    const HWND grid = GetDlgItem(app.dicts, IDC_ENTRIES);
    std::string text;
    gui::TextEncoding encoding{};
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"メモ");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"입력중");
    if (!Check(WriteExternally(file, "犬\t고친사용자\n象\t외부사전\n"), "edit the open file externally")) return false;
    CheckExternalEdits(app);
    bool ok = Check(app.rows.size() == 2 && app.rows[1].key == "象" && ControlText(app.dicts, IDC_ENTRY_KEY) == L"メモ" &&
                    ControlText(app.dicts, IDC_ENTRY_KO) == L"입력중" && Logged(app, L"INFO", where + L"외부에서 바뀜, 다시 읽음"),
                    "an external edit refreshes the list, keeps the typed entry and is logged");
    if (!WaitForJob(app)) return false;
    ok = Check(TranslateShort(app, L"象") == L"외부사전", "an external edit reloads the engine") && ok;
    ShowPage(app, kDictsPage);
    if (!Check(WriteExternally(file, "犬\t고친사용자\n象\t외부사전\n蛇\t뱀사전\n"), "edit the file again externally")) return false;
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"熊");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"곰사전");
    NotifyClick(app.dicts, IDC_ENTRY_ADD);
    ok = Check(gui::ReadTextFile(file, text, encoding) && text == "犬\t고친사용자\n象\t외부사전\n蛇\t뱀사전\n熊\t곰사전\n",
               "an add after an unseen external edit keeps that edit") && ok;
    Select(grid, 1);
    if (!Check(WriteExternally(file, "猿\t원숭이\n犬\t고친사용자\n象\t외부사전\n蛇\t뱀사전\n熊\t곰사전\n"),
               "insert a row above the selection externally")) return false;
    NotifyClick(app.dicts, IDC_ENTRY_DELETE);
    ok = Check(gui::ReadTextFile(file, text, encoding) && text == "猿\t원숭이\n犬\t고친사용자\n蛇\t뱀사전\n熊\t곰사전\n",
               "a delete after an external edit removes the selected row, not the one now at its index") && ok;
    if (!Check(WriteExternally(file, "犬\t고친사용자\n"), "restore the dictionary file")) return false;
    CheckExternalEdits(app);
    if (!WaitForJob(app)) return false;
    ShowPage(app, kDictsPage);
    return Check(app.rows.size() == 1, "the restored file is listed") && ok;
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
    const gui::DictRow original = app.rows[0];
    const std::wstring where = L"[" + user.filename().native() + L"/USERDICT.TSV] ";
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"고친사용자");
    NotifyClick(app.dicts, IDC_ENTRY_EDIT);
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"鳥");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"추가사용자");
    NotifyClick(app.dicts, IDC_ENTRY_ADD);
    std::string text;
    gui::TextEncoding encoding{};
    ok = Check(gui::ReadTextFile(file, text, encoding) && encoding == gui::TextEncoding::Utf8 &&
               text == "犬\t고친사용자\n鳥\t추가사용자\n", "edited dictionary file") && ok;
    ok = Check(ControlText(app.dicts, IDC_ENTRY_KEY).empty() && ControlText(app.dicts, IDC_ENTRY_KO).empty() &&
               Selected(grid) == 1 && app.rows[1].key == "鳥", "a saved add clears the fields and selects the new row") && ok;
    ok = Check(Logged(app, L"INFO", where + L"사전 수정: " + Wide(gui::RowLine({"犬", "고친사용자", original.pos, original.options, original.note})) +
                                        L" (이전: " + Wide(gui::RowLine({"", original.ko, original.pos, original.options, original.note}).substr(1)) + L")") &&
               Logged(app, L"INFO", where + L"사전 추가: 鳥\t추가사용자"), "edit and add are logged as full rows") && ok;
    ok = Check(TranslateShort(app, L"鳥\r\n犬") == L"추가사용자\r\n고친사용자", "translation reads the edited dictionary") && ok;
    const HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!Check(locked != INVALID_HANDLE_VALUE, "deny writes to the dictionary file")) return false;
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, L"魚");
    SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"실패사용자");
    NotifyClick(app.dicts, IDC_ENTRY_ADD);
    CloseHandle(locked);
    ok = Check(ControlText(app.dicts, IDC_ENTRY_KEY) == L"魚" && ControlText(app.dicts, IDC_ENTRY_KO) == L"실패사용자" &&
               app.rows.size() == 2 && gui::ReadTextFile(file, text, encoding) && text == "犬\t고친사용자\n鳥\t추가사용자\n",
               "a failed add keeps the typed entry and the file") && ok;
    ok = Check(Logged(app, L"사전", where + L"사전 추가 실패: 魚 (파일을 쓸 수 없습니다)"), "a failed write is logged") && ok;

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
    ok = Check(Logged(app, L"INFO", where + L"사전 삭제: 鳥\t추가사용자"), "a delete is logged as the full row") && ok;
    ok = SelfTestExternalEdits(app, file, where) && ok;

    NotifyClick(app.dicts, IDC_DICT_NEW);
    ok = Check(fs::exists(gui::UserDictPath(user, "새 사전"), error) && app.userDicts.size() == 2 && app.rows.empty(),
               "new dictionary") && ok;
    ok = SelfTestEntryKeys(app) && ok;
    for (int n = 1; n < 41; ++n) {
        SetDlgItemTextW(app.dicts, IDC_ENTRY_KEY, (L"ＺＺ" + std::to_wstring(n)).c_str());
        SetDlgItemTextW(app.dicts, IDC_ENTRY_KO, L"목록");
        NotifyClick(app.dicts, IDC_ENTRY_ADD);
    }
    ok = Check(app.rows.size() == 44 && Selected(grid) == 43 && ListView_IsItemVisible(grid, 43) && ListView_GetTopIndex(grid) > 0,
               "the list scrolls to the newest added row") && ok;
    const fs::path created = gui::UserDictPath(user, "새 사전");
    const std::wstring createdWhere = L"[" + user.filename().native() + L"/" + created.filename().native() + L"] ";
    ok = Check(Logged(app, L"INFO", createdWhere + L"새 사전"), "a new dictionary is logged") && ok;
    app.removeAnswer = IDNO;
    NotifyClick(app.dicts, IDC_DICT_REMOVE);
    ok = Check(fs::exists(created, error) && app.userDicts.size() == 2, "declining 사전 삭제 keeps the file") && ok;
    app.removeAnswer = IDYES;
    NotifyClick(app.dicts, IDC_DICT_REMOVE);
    ok = Check(!fs::exists(created, error) && app.userDicts.size() == 1 && Logged(app, L"INFO", createdWhere + L"사전 삭제됨 (휴지통)"),
               "사전 삭제 moves the file away, lists the rest and logs it") && ok;
    std::vector<fs::path> opened;
    app.openFolder = [&](const fs::path& path) { opened.push_back(path); return true; };
    NotifyClick(app.dicts, IDC_DICT_FOLDER);
    NotifyClick(app.logs, IDC_LOG_FOLDER);
    ok = Check(opened == std::vector<fs::path>{UserFolderPath(app), DataFolderPath(app)} && fs::equivalent(opened[0], user, error) &&
               fs::equivalent(opened[1], user.parent_path(), error), "폴더 열기 opens the user folder and the data folder") && ok;
    app.openFolder = [](const fs::path&) { return false; };
    NotifyClick(app.logs, IDC_LOG_FOLDER);
    ok = Check(Logged(app, L"사전", L"폴더를 열 수 없습니다: " + DataFolderPath(app).native()), "a folder that cannot open is logged") && ok;
    app.openFolder = nullptr;
    const fs::path places = program / L"places.txt";
    ok = Check(WriteFixture(places, "東京\t가져온장소\n"), "write dictionary to import") && ok;
    ShowPage(app, kDictsPage);
    DropFiles(app.main, {places});
    ok = Check(CurrentPage(app) == kDictsPage && app.userDicts.size() == 2 && app.dictIndex >= 0 &&
               app.userDicts[static_cast<std::size_t>(app.dictIndex)].path == user / L"userdict-places.txt" && app.rows.size() == 1,
               "a file dropped on the dictionary page is imported") && ok;
    const auto folderSources = app.engineUserPaths;
    const fs::path renamed = gui::UserDictPath(user, "places-renamed");
    if (!Check(RenameDict(app, app.dictIndex, L"places-renamed"), "rename a dictionary inside a default folder source")) return false;
    ok = Check(Logged(app, L"INFO", L"[" + user.filename().native() + L"/userdict-places.txt] 사전 이름 바꿈: " +
                                        user.filename().native() + L"/" + renamed.filename().native()), "a rename is logged") && ok;
    ShowDicts(app, renamed);
    ok = Check(TranslateShort(app, L"東京") == L"가져온장소" && app.engineUserPaths == folderSources,
               "folder child rename preserves the configured folder source") && ok;
    ListView_SetCheckState(list, app.dictIndex, FALSE);
    ok = Check(TranslateShort(app, L"東京") != L"가져온장소" && app.engineUserPaths == folderSources,
               "folder child off does not remove or replace the folder source") && ok;
    ListView_SetCheckState(list, app.dictIndex, TRUE);
    ok = Check(TranslateShort(app, L"東京") == L"가져온장소" && app.engineUserPaths == folderSources,
               "folder child on keeps the folder source and restores the row") && ok;
    ShowPage(app, kTextPage);
    return ok;
}

bool LogContains(App& app, std::wstring_view text) {
    SendMessageW(app.main, kLog, 0, 0);
    const HWND list = GetDlgItem(app.logs, IDC_LOG_ENTRIES);
    for (int index = 0; index < ListView_GetItemCount(list); ++index) {
        wchar_t message[1024]{};
        ListView_GetItemText(list, index, 2, message, 1024);
        if (std::wstring_view(message).find(text) != std::wstring_view::npos) return true;
    }
    return false;
}

bool SelfTestConfig(App& app, const fs::path& folder) {
    const fs::path program = folder / L"설정 배치";
    const fs::path data = program / L"HdTrAnS-DaTa";
    const fs::path snapshot = data / L"dicts.hdsnap";
    const fs::path source = program / L"snapshot-source";
    const fs::path user = data / L"USER";
    const fs::path config = user / L"HDTRANS.CONFIG.YAML";
    std::error_code error;
    for (const auto& directory : {source, data / L"別辞書", user}) {
        fs::create_directories(directory, error);
        if (!Check(!error, "create config fixture folder")) return false;
    }
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n";
    if (!Check(WriteFixture(source / L"base.tsv", header + "猫\t기본사전\tN110\t\t\tGN\t\t\t\t\t1\n") &&
               hdTrans::dict::CompileSnapshot({Utf8(source.native())}, Utf8(snapshot.native())), "write default snapshot fixture")) return false;
    fs::remove_all(source, error);
    if (!Check(!error, "remove snapshot source folder")) return false;
    if (!Check(WriteFixture(data / L"別辞書/base.tsv", header + "猫\t설정사전\tN110\t\t\tGN\t\t\t\t\t1\n") &&
               WriteFixture(user / L"USERDICT.TSV", "犬\t기본사용자\n") &&
               WriteFixture(user / L"custom.tsv", "犬\t앞파일\n") &&
               WriteFixture(user / L"custom.txt", "犬\t사용자설정\n") &&
               WriteFixture(config, "dicts: ../別辞書\nuserdict: [custom.tsv, custom.txt]\ncache: off\n"
                                    "cg: false\nmisread: true\nreference: true\n"
                                    "unknown_first: true\nunknown_second: true\n"), "write config fixtures")) return false;
    const wchar_t* initialCache = _wgetenv(L"HDTRANS_CACHE_DIR");
    const std::optional<std::wstring> previousCache = initialCache ? std::optional<std::wstring>(initialCache) : std::nullopt;
    app.engine.reset();
    const std::string programDir = Utf8(program.native());
    if (!Check(LoadEngine(app, programDir), "load mixed-case data-root config")) return false;
    bool ok = Check(!app.settings.cg && app.settings.misread && app.settings.reference, "GUI config session settings");
    ok = Check(LogContains(app, L"HDTRANS.CONFIG.YAML:7:") && LogContains(app, L"HDTRANS.CONFIG.YAML:8:") &&
               ControlText(app.main, IDC_LOG_STATUS).find(L"경고") != std::wstring::npos,
               "initial config diagnostics have separate log rows and a visible warning status") && ok;
    const wchar_t* configuredCache = _wgetenv(L"HDTRANS_CACHE_DIR");
    ok = Check(configuredCache && std::wstring(configuredCache) == L"off", "GUI config cache setting") && ok;
    CheckRadioButton(app.text, IDC_OUTPUT_TRANSLATION, IDC_OUTPUT_SPACED, IDC_OUTPUT_TRANSLATION);
    CheckDlgButton(app.text, IDC_WHOLE_DOCUMENT, BST_CHECKED);
    ok = Check(TranslateShort(app, L"猫\r\n犬") == L"설정사전\r\n사용자설정", "GUI configured dictionary and relative user path") && ok;
    fs::remove(config, error);
    if (!Check(!error, "remove optional config")) return false;
    app.engine.reset();
    if (!Check(LoadEngine(app, programDir), "load default mixed-case data layout")) return false;
    const auto* withoutConfig = app.engine.get();
    app.reloadDicts = true;
    ok = Check(ReadyEngine(app) && app.engine.get() == withoutConfig,
               "reload without a config file reuses the active engine") && ok;
    const wchar_t* restoredCache = _wgetenv(L"HDTRANS_CACHE_DIR");
    ok = Check(previousCache ? restoredCache && *previousCache == restoredCache : !restoredCache,
               "GUI restores cache environment") && ok;
    ok = Check(app.settings.cg && !app.settings.misread && !app.settings.reference, "GUI defaults without config") && ok;
    ok = Check(LogContains(app, L"Ehnd 활성화가 꺼져 있어 필터 및 사전 불러오기를 건너뜁니다") &&
               ControlText(app.main, IDC_LOG_STATUS).find(L"경고") == std::wstring::npos,
               "disabled Ehnd is visible as INFO without a warning status") && ok;
    ok = Check(TranslateShort(app, L"猫\r\n犬") == L"기본사전\r\n기본사용자", "GUI default dictionary and user folder") && ok;
    ok = SelfTestDicts(app, program, user) && ok;
    app.engine.reset();
    return ok;
}

bool SelfTestReload(App& app, const fs::path& folder) {
    const fs::path program = folder / L"reload";
    const fs::path data(Wide(hdTrans::dict::DataFolder(Utf8(program.native()))));
    const fs::path user = data / L"user";
    const fs::path shipped = data / L"toy-dicts/base.tsv";
    const fs::path first = user / L"userdict-a.txt";
    const fs::path second = user / L"userdict-b.txt";
    const fs::path ehnd = data / L"ehnd/UserDict_10.txt";
    const fs::path config = user / L"hdtrans.config.yaml";
    const std::string settings = "dicts: ../toy-dicts\ncache: off\nehnd: ../ehnd\nuserdict: [userdict-a.txt, userdict-b.txt]\n";
    std::error_code error;
    for (const auto& directory : {shipped.parent_path(), user, ehnd.parent_path()}) {
        fs::create_directories(directory, error);
        if (!Check(!error, "create reload fixture folder")) return false;
    }
    const std::string base = "#! layer=base\n# Neutral lookup fixtures\n"
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "学生\tBASE_STUDENT\tN110\t\t\tGN\t\tneutral:1\n"
        "猫\tBASE_CAT\tN110\t\t\tGN\t\tneutral:2\n"
        "犬\tBASE_DOG\tN110\t\t\tGN\t\tneutral:3\n"
        "日本語\tBASE_LANGUAGE\tN110\t\t\tGN\t\tneutral:4\n";
    if (!Check(WriteFixture(shipped, base) &&
               WriteFixture(first, "学生\tUSER_A_OLD\tn\n") &&
               WriteFixture(second, "猫\tUSER_B_OLD\tn\n") &&
               WriteFixture(ehnd, "学生\tEHND_STUDENT\t0\n犬\tEHND_DOG\t0\n") &&
               WriteFixture(config, settings),
               "write reload fixtures")) return false;
    if (!Check(LoadEngine(app, Utf8(program.native())), "load reload fixtures")) return false;
    gui::TranslationOptions options;
    options.wholeDocument = true;
    const auto translate = [&] {
        return gui::TranslateText(*app.engine, "学生 猫 犬", ConfiguredOptions(app, options), app.cancel);
    };
    bool ok = Check(translate() == "USER_A_OLD USER_B_OLD EHND_DOG", "reload initial literal");
    const auto dictionaries = app.engine->Dictionaries();
    const auto filters = app.engine->Filters();
    const auto loadedUserVersion = app.loadedUserVersion;
    const auto loadedEhndVersion = app.loadedEhndVersion;
    if (!Check(WriteFixture(shipped, "key\tko\n学生\tBROKEN\n"), "prepare unchanged-source control")) return false;
    ok = Check(ReadyEngine(app) && ReadyEngine(app) && app.error.empty() &&
               app.engine->Dictionaries() == dictionaries && app.engine->Filters() == filters,
               "unchanged healthy sources do not reload shipped dictionaries") && ok;
    if (!Check(WriteFixture(first, "学生\tUSER_A_NEW\tn\n") &&
               WriteFixture(shipped, "key\tko\n学生\tBROKEN\n") &&
               WriteFixture(ehnd, "学生\tEHND_STUDENT_NEW\t0\n犬\tEHND_DOG_NEW\t0\n"),
               "write fatal reload mutation")) return false;
    app.reloadDicts = true;
    ok = Check(!ReadyEngine(app), "readiness rejects a failed reload") && ok;
    ok = Check(app.reloadDicts.load() && !app.error.empty() && !app.engine->UserFileError().empty() &&
               LogContains(app, L"이전 사전 유지"), "failed reload keeps pending work and logs its error and old stack") && ok;
    ok = Check(app.engine->Dictionaries() == dictionaries && app.engine->Filters() == filters &&
               app.loadedUserVersion == loadedUserVersion && app.loadedEhndVersion == loadedEhndVersion &&
               translate() == "USER_A_OLD USER_B_OLD EHND_DOG", "failed reload keeps the complete old stack and stamps") && ok;
    ok = Check(!ReadyEngine(app), "the next readiness request retries a failed reload") && ok;
    if (!Check(WriteFixture(shipped, base), "repair fatal reload fixture")) return false;
    ok = Check(ReadyEngine(app), "readiness recovers after repair") && ok;
    ok = Check(app.error.empty() && !app.reloadDicts.load() && translate() == "USER_A_NEW USER_B_OLD EHND_DOG_NEW",
               "recovery publishes the complete new stack") && ok;
    ok = Check(app.loadedUserVersion == hdTrans::internal::UserVersionOf(RequestedSources(app.engineUserPaths, app.settings.userdict.has_value()), app.engineConfigPath) &&
               app.loadedEhndVersion == hdTrans::internal::FolderVersionOf(app.engineEhndDir),
               "successful reload commits source stamps") && ok;
    if (!Check(WriteFixture(first, "学生\tUSER_A_EXTERNAL\tn\n"), "write external user edit")) return false;
    ok = Check(!app.reloadDicts.load() && translate() == "USER_A_NEW USER_B_OLD EHND_DOG_NEW" && ReadyEngine(app) &&
               translate() == "USER_A_EXTERNAL USER_B_OLD EHND_DOG_NEW", "readiness reads external edits to opened user paths") && ok;
    if (!Check(WriteFixture(ehnd, "学生\tEHND_STUDENT_NEW\t0\n犬\tEHND_DOG_EXTERNAL\t0\n"), "write external Ehnd edit")) return false;
    ok = Check(!app.reloadDicts.load() && ReadyEngine(app) && translate() == "USER_A_EXTERNAL USER_B_OLD EHND_DOG_EXTERNAL",
               "readiness reads external edits to the enabled Ehnd folder") && ok;
    const auto beforeUserFailure = app.engine->Dictionaries();
    const auto beforeUserFilters = app.engine->Filters();
    const auto beforeUserVersion = app.loadedUserVersion;
    if (!Check(WriteFixture(first, "学生\tUSER_A_RECOVERED\tn\n"), "change a valid file beside an unreadable file")) return false;
    const HANDLE blocked = CreateFileW(second.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!Check(blocked != INVALID_HANDLE_VALUE, "hold an exclusive user file handle")) return false;
    const bool rejectedUser = !ReadyEngine(app);
    ok = Check(rejectedUser && app.reloadDicts.load() && !app.error.empty() &&
               app.engine->Dictionaries() == beforeUserFailure && app.engine->Filters() == beforeUserFilters &&
               app.loadedUserVersion == beforeUserVersion && translate() == "USER_A_EXTERNAL USER_B_OLD EHND_DOG_EXTERNAL",
               "an unreadable user file rejects partial publication") && ok;
    CloseHandle(blocked);
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "USER_A_RECOVERED USER_B_OLD EHND_DOG_EXTERNAL",
               "user file readability recovery retries the pending load") && ok;
    const auto beforeEhndFailure = app.engine->Dictionaries();
    const auto beforeEhndFilters = app.engine->Filters();
    const auto beforeEhndUserVersion = app.loadedUserVersion;
    const auto beforeEhndVersion = app.loadedEhndVersion;
    const fs::path away = data / L"ehnd-away";
    if (!Check(WriteFixture(first, "学生\tUSER_A_EXTERNAL\tn\n"), "change a valid file before Ehnd failure")) return false;
    fs::rename(ehnd.parent_path(), away, error);
    if (!Check(!error, "move the enabled Ehnd folder away")) return false;
    ok = Check(!ReadyEngine(app) && app.reloadDicts.load() && !app.error.empty() &&
               app.engine->Dictionaries() == beforeEhndFailure && app.engine->Filters() == beforeEhndFilters &&
               app.loadedUserVersion == beforeEhndUserVersion && app.loadedEhndVersion == beforeEhndVersion &&
               translate() == "USER_A_RECOVERED USER_B_OLD EHND_DOG_EXTERNAL", "Ehnd I/O failure keeps the complete old stack") && ok;
    fs::rename(away, ehnd.parent_path(), error);
    if (!Check(!error, "restore the enabled Ehnd folder")) return false;
    ok = Check(hdTrans::internal::FolderVersionOf(app.engineEhndDir) == beforeEhndVersion && ReadyEngine(app) && app.error.empty() &&
               translate() == "USER_A_EXTERNAL USER_B_OLD EHND_DOG_EXTERNAL", "restoring identical Ehnd stamps retries the pending load") && ok;
    if (!Check(WriteFixture(second, "#! layer=user\nkey\tko\tpos\tid\n"
                           "猫\tUSER_B_VALID\tN110\t1\n不明\tDISCARD\tunknown-tag\t2\n"),
               "write typed user row skip control")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "USER_A_EXTERNAL USER_B_VALID EHND_DOG_EXTERNAL",
               "a skipped malformed row does not fail readiness") && ok;
    const HWND logList = GetDlgItem(app.logs, IDC_LOG_ENTRIES);
    ok = Check(LogContains(app, L"userdict-b.txt:4:") && ControlText(app.main, IDC_LOG_STATUS).starts_with(L"사전 경고 "),
               "a skipped row warning is visible in the log tab and status") && ok;
    SendMessageW(app.main, WM_COMMAND, MAKEWPARAM(IDC_LOG_STATUS, STN_CLICKED),
                 reinterpret_cast<LPARAM>(GetDlgItem(app.main, IDC_LOG_STATUS)));
    ok = Check(CurrentPage(app) == kLogsPage && WindowText(app.main) == Title(kLogsPage), "warning status opens the log tab") && ok;
    const std::wstring sourceBeforeCheck = ControlText(app.text, IDC_SOURCE);
    const std::wstring resultBeforeCheck = ControlText(app.text, IDC_RESULT);
    const auto beforeCheck = app.engine->Dictionaries();
    NotifyClick(app.logs, IDC_DICT_CHECK);
    ok = Check(app.busy && !IsWindowEnabled(GetDlgItem(app.logs, IDC_DICT_CHECK)), "dictionary check uses the shared worker") && ok;
    const auto checkWorker = app.worker.get_id();
    NotifyClick(app.logs, IDC_DICT_CHECK);
    ok = Check(app.worker.get_id() == checkWorker, "a busy dictionary check cannot start another worker") && ok;
    if (!WaitForJob(app)) return false;
    ok = Check(app.error.empty() && app.engine->Dictionaries() != beforeCheck &&
               ControlText(app.text, IDC_SOURCE) == sourceBeforeCheck && ControlText(app.text, IDC_RESULT) == resultBeforeCheck &&
               LogContains(app, L"userdict-b.txt:4:"), "dictionary check reloads and logs warnings without translating or replacing text") && ok;
    ShowPage(app, kTextPage);
    const auto warningDictionaries = app.engine->Dictionaries();
    const auto warningFilters = app.engine->Filters();
    const auto* engine = app.engine.get();
    if (!Check(WriteFixture(config, settings + "strict_dict: true\ncg: false\n"), "enable strict parsing in config only")) return false;
    ok = Check(!app.reloadDicts.load() && !ReadyEngine(app) && !app.error.empty() && app.reloadDicts.load() &&
               app.engine->Dictionaries() == warningDictionaries && app.engine->Filters() == warningFilters &&
               translate() == "USER_A_EXTERNAL USER_B_VALID EHND_DOG_EXTERNAL",
               "config-only strict change rejects bad rows and keeps the old stack") && ok;
    if (!Check(WriteFixture(config, settings + "strict_dict: false\ncg: false\n"), "disable strict parsing in config only")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && !app.reloadDicts.load() && app.engine.get() != engine && !app.settings.cg &&
               translate() == "USER_A_EXTERNAL USER_B_VALID EHND_DOG_EXTERNAL",
               "config-only recovery publishes a new engine and the changed session setting together") && ok;
    if (!Check(WriteFixture(config, settings + "strict_dict: not-a-bool\n"), "write invalid config value")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && LogContains(app, L"hdtrans.config.yaml:5:"),
               "config entry diagnostics are visible without changing the reader's fallback policy") && ok;
    if (!Check(WriteFixture(ehnd.parent_path() / L"PreFilter_20.txt", "(\tBAD_PRE\t10\t1\n"),
               "write regex warning control")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && !app.engine->FilterErrors().empty() &&
               translate() == "USER_A_EXTERNAL USER_B_VALID EHND_DOG_EXTERNAL", "a regex warning does not fail readiness") && ok;
    const fs::path nextDicts = data / L"replacement-dicts";
    const fs::path nextEhnd = data / L"replacement-ehnd";
    const fs::path nextUser = user / L"custom-live.txt";
    for (const auto& directory : {nextDicts, nextEhnd}) {
        fs::create_directories(directory, error);
        if (!Check(!error, "create config replacement folders")) return false;
    }
    if (!Check(WriteFixture(nextDicts / L"base.tsv", "key\tko\n学生\tBROKEN\n") &&
               WriteFixture(nextUser, "学生\tUSER_LIVE\tn\n") &&
               WriteFixture(nextEhnd / L"UserDict_10.txt", "犬\tLIVE_DOG\t0\n"), "write config replacement files")) return false;
    const auto priorDicts = app.engineDictDirs;
    const auto priorUsers = app.engineUserPaths;
    const auto priorEhnd = app.engineEhndDir;
    const auto priorVersion = app.loadedUserVersion;
    const auto priorStack = app.engine->Dictionaries();
    const auto priorFilters = app.engine->Filters();
    const auto priorSettings = app.settings;
    const auto uiOptions = Options(app.text, false);
    // Reference mode keeps no space after 学生.
    const std::string nextConfig = "dicts: ../replacement-dicts\nuserdict: [custom-live.txt]\nehnd: ../replacement-ehnd\n"
        "strict_dict: false\ncg: false\nmisread: true\nreference: true\nspeech: soft\nwrapped: true\n";
    if (!Check(WriteFixture(config, nextConfig), "change dictionary, filter and session settings together")) return false;
    ok = Check(!ReadyEngine(app) && app.engine->Dictionaries() == priorStack && app.engine->Filters() == priorFilters &&
               app.engineDictDirs == priorDicts && app.engineUserPaths == priorUsers && app.engineEhndDir == priorEhnd &&
               app.loadedUserVersion == priorVersion && app.settings.cg == priorSettings.cg &&
               app.settings.reference == priorSettings.reference && app.settings.cache == priorSettings.cache &&
               _wgetenv(L"HDTRANS_CACHE_DIR") && std::wstring(_wgetenv(L"HDTRANS_CACHE_DIR")) == L"off",
               "failed config candidate keeps settings and whole source context and restores the cache") && ok;
    if (!Check(WriteFixture(nextDicts / L"base.tsv", base), "repair config replacement dictionary")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && !app.settings.cg && app.settings.misread && app.settings.reference &&
               app.settings.speech == hdTrans::Speech::Soft && app.settings.wrapped && !app.settings.cache &&
               app.engineDictDirs == std::vector<std::string>{Utf8(nextDicts.native())} &&
               app.engineUserPaths == std::vector<std::string>{Utf8(nextUser.native())} && app.engineEhndDir == Utf8(nextEhnd.native()) &&
               translate() == "USER_LIVEBASE_CAT LIVE_DOG", "successful config candidate commits all requested settings and sources") && ok;
    SendMessageW(app.dicts, kRelist, 0, 0);
    const auto afterOptions = Options(app.text, false);
    ok = Check(afterOptions.output == uiOptions.output && afterOptions.wholeDocument == uiOptions.wholeDocument &&
               afterOptions.skipBlankLines == uiOptions.skipBlankLines && app.userSources == app.engineUserPaths,
               "config reload refreshes committed source rows without resetting translation controls") && ok;
    const auto publishedSettings = app.settings;
    app.engine.reset();
    ClearLogs(app);
    const HANDLE blockedInitial = CreateFileW(nextUser.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!Check(blockedInitial != INVALID_HANDLE_VALUE, "deny a user file during the first candidate Open")) return false;
    const bool loadedInitialUser = LoadEngine(app, Utf8(program.native()));
    CloseHandle(blockedInitial);
    if (!Check(loadedInitialUser && app.engine && app.engine->loaded(), "initial unreadable user file does not block base Open")) return false;
    ok = Check(!app.reloadDicts.load() && app.error.empty() && app.settings.cache == publishedSettings.cache &&
               translate() == "BASE_STUDENTBASE_CAT LIVE_DOG" &&
               std::any_of(app.logEntries.begin(), app.logEntries.end(), [](const auto& entry) { return entry.kind == L"INFO"; }) &&
               LogContains(app, L"custom-live.txt"), "initial Open keeps base and healthy Ehnd with the rejected user file logged") && ok;
    app.reloadDicts = true;
    if (!Check(ReadyEngine(app) && translate() == "USER_LIVEBASE_CAT LIVE_DOG", "reload the recovered user file")) return false;
    const fs::path badEncoding = nextUser.parent_path() / L"bad-encoding.txt";
    std::string encodingConfig = nextConfig;
    encodingConfig.replace(encodingConfig.find("userdict: [custom-live.txt]"), sizeof("userdict: [custom-live.txt]") - 1,
                           "userdict: [custom-live.txt, bad-encoding.txt]");
    if (!Check(WriteFixture(badEncoding, "\xff\n") && WriteFixture(config, encodingConfig), "write an invalid UTF-8 user sibling")) return false;
    app.engine.reset();
    ClearLogs(app);
    if (!Check(LoadEngine(app, Utf8(program.native())) && app.engine && app.engine->loaded(), "invalid UTF-8 user file permits startup")) return false;
    ok = Check(app.error.empty() && translate() == "USER_LIVEBASE_CAT LIVE_DOG" && LogContains(app, L"bad-encoding.txt"),
               "cold skip mode keeps the good user file and base with the bad sibling diagnosed") && ok;
    const fs::path badStrictUser = nextUser.parent_path() / L"strict-user.txt";
    std::string strictUserConfig = nextConfig;
    strictUserConfig.replace(strictUserConfig.find("userdict: [custom-live.txt]"), sizeof("userdict: [custom-live.txt]") - 1,
                             "userdict: [custom-live.txt, strict-user.txt]");
    strictUserConfig.replace(strictUserConfig.find("strict_dict: false"), sizeof("strict_dict: false") - 1, "strict_dict: true");
    if (!Check(WriteFixture(badStrictUser, "猫\tREJECTED_CAT\tn\n学生\tREJECTED_STUDENT\tunknown-tag\n") &&
               WriteFixture(config, strictUserConfig), "write a strict user-file rejection")) return false;
    app.engine.reset();
    ClearLogs(app);
    if (!Check(LoadEngine(app, Utf8(program.native())) && app.engine && app.engine->loaded(), "strict user-file rejection permits startup")) return false;
    ok = Check(app.error.empty() && app.settings.strictDict == true && translate() == "USER_LIVEBASE_CAT LIVE_DOG" &&
               LogContains(app, L"strict-user.txt"), "strict cold Open rejects the whole bad user file, not the engine") && ok;
    if (!Check(WriteFixture(config, nextConfig), "restore the healthy user source configuration")) return false;
    app.engine.reset();
    if (!Check(LoadEngine(app, Utf8(program.native())), "load the healthy source stack before active strict rollback")) return false;
    const auto beforeStrictFilter = app.engine->Dictionaries();
    const auto beforeStrictFilters = app.engine->Filters();
    const auto* beforeStrictEngine = app.engine.get();
    std::string strictConfig = nextConfig;
    strictConfig.replace(strictConfig.find("strict_dict: false"), sizeof("strict_dict: false") - 1, "strict_dict: true");
    const fs::path rejectedFilter = nextEhnd / L"PreFilter_99.txt";
    if (!Check(WriteFixture(rejectedFilter, "学生\t犬\t10\t0\n(\tBAD_PRE\t10\t1\n") && WriteFixture(config, strictConfig),
               "write a strict filter-file rejection")) return false;
    ok = Check(!ReadyEngine(app) && app.engine.get() == beforeStrictEngine && app.engine->Dictionaries() == beforeStrictFilter &&
               app.engine->Filters() == beforeStrictFilters && app.settings.strictDict == publishedSettings.strictDict &&
               LogContains(app, L"PreFilter_99.txt"), "strict filter candidate keeps the old engine and settings") && ok;
    app.engine.reset();
    ClearLogs(app);
    if (!Check(LoadEngine(app, Utf8(program.native())) && app.engine && app.engine->loaded(), "strict filter-file rejection permits base Open")) return false;
    ok = Check(!app.reloadDicts.load() && app.error.empty() && app.settings.strictDict == true &&
               hdTrans::internal::FilterFileFailed(*app.engine) && translate() == "USER_LIVEBASE_CAT LIVE_DOG" &&
               LogContains(app, L"PreFilter_99.txt"), "initial strict filter failure excludes its whole file and logs the diagnostic") && ok;
    if (!Check(WriteFixture(rejectedFilter, "ZZSTRICTCONTROL\tSAFE\t10\t0\n"), "repair the rejected strict filter file")) return false;
    app.reloadDicts = true;
    ok = Check(ReadyEngine(app) && !hdTrans::internal::FilterFileFailed(*app.engine) && app.error.empty(),
               "a healthy filter reload succeeds after strict rejection") && ok;
    const auto cheapConfigVersion = app.loadedUserVersion.config;
    const auto* beforeParsedEngine = app.engine.get();
    const fs::path parsedUser = nextUser.parent_path() / L"parsed-user.txt";
    std::string parsedConfig = nextConfig;
    parsedConfig.replace(parsedConfig.find("userdict: [custom-live.txt]"), sizeof("userdict: [custom-live.txt]") - 1,
                         "userdict: [parsed-user.txt]");
    if (!Check(WriteFixture(parsedUser, "学生\tPARSED_STUDENT\tn\n猫\tREJECTED_CAT\tunknown-tag\n") &&
               WriteFixture(config, parsedConfig), "change config after the cheap metadata observation")) return false;
    const auto parsed = hdTrans::internal::LoadConfig(Utf8(config.native()));
    if (!Check(parsed.sourceStamp && ReadDictionaries(app, parsed, Utf8(program.native()), Utf8(config.native()), cheapConfigVersion),
               "parse-entry stamp replaces the stale cheap config observation")) return false;
    ok = Check(app.engine.get() != beforeParsedEngine && app.settings.strictDict == false &&
               app.engineUserPaths == std::vector<std::string>{Utf8(parsedUser.native())} &&
               translate() == "PARSED_STUDENTBASE_CAT LIVE_DOG" && LogContains(app, L"parsed-user.txt"),
               "one parsed config supplies replacement paths and skip policy") && ok;
    const auto* parsedEngine = app.engine.get();
    const auto parsedDictionaries = app.engine->Dictionaries();
    const auto parsedFilters = app.engine->Filters();
    const auto parsedVersion = app.loadedUserVersion;
    std::string laterConfig = parsedConfig;
    laterConfig.replace(laterConfig.find("strict_dict: false"), sizeof("strict_dict: false") - 1, "strict_dict: true");
    if (!Check(WriteFixture(config, laterConfig), "change config after parsing")) return false;
    ok = Check(!ReadDictionaries(app, parsed, Utf8(program.native()), Utf8(config.native()), parsedVersion.config) &&
               app.engine.get() == parsedEngine && app.engine->Dictionaries() == parsedDictionaries && app.engine->Filters() == parsedFilters &&
               app.loadedUserVersion == parsedVersion && app.settings.strictDict == false && app.reloadDicts.load() &&
               translate() == "PARSED_STUDENTBASE_CAT LIVE_DOG", "a stale supplied parse cannot publish paths or policy") && ok;
    if (!Check(WriteFixture(parsedUser, "学生\tPARSED_REPAIRED\tn\n"), "repair the user file for the new strict config")) return false;
    ok = Check(ReadyEngine(app) && app.settings.strictDict == true && app.error.empty() &&
               translate() == "PARSED_REPAIREDBASE_CAT LIVE_DOG", "the next request reparses config and recovers") && ok;
    NotifyClick(app.logs, IDC_LOG_CLEAR);
    ok = Check(ListView_GetItemCount(logList) == 0 && ControlText(app.main, IDC_LOG_STATUS).empty(), "clear log entries and status") && ok;
    for (std::size_t index = 0; index <= kLogLimit; ++index) AddLog(app, L"엔진", std::to_wstring(index));
    SendMessageW(app.main, kLog, 0, 0);
    ok = Check(app.logEntries.size() == kLogLimit && app.logEntries.front().message == L"300" &&
               app.logEntries.back().message == L"1" && ListView_GetItemCount(logList) == static_cast<int>(kLogLimit),
               "log keeps and displays the newest 300 entries in memory") && ok;
    NotifyClick(app.logs, IDC_LOG_CLEAR);
    app.engine.reset();
    return ok;
}

bool SelfTestExplicitSources(App& app, const fs::path& folder) {
    const fs::path program = folder / L"configured-sources";
    const std::string programDir = Utf8(program.native());
    const fs::path data(Wide(hdTrans::dict::DataFolder(programDir)));
    const fs::path dicts = data / L"toy-dicts";
    const fs::path user(Wide(hdTrans::dict::UserFolder(programDir)));
    const fs::path first = user / L"custom.txt";
    const fs::path second = user / L"userdict-b.txt";
    const std::string fixtureConfig = "dicts: ../toy-dicts\ncache: off\nuserdict: [custom.txt, userdict-b.txt]\n";
    std::error_code error;
    for (const auto& directory : {dicts, user}) {
        fs::create_directories(directory, error);
        if (!Check(!error, "create explicit-source fixture folder")) return false;
    }
    if (!Check(WriteFixture(dicts / L"base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n"
                            "猫\t고양이\tN110\t\t\tGN\t\t\t\t\t1\n犬\t개\tN110\t\t\tGN\t\t\t\t\t2\n") &&
               WriteFixture(first, "猫\t앞사전\n") && WriteFixture(second, "犬\t뒤사전\n") &&
               WriteFixture(user / L"hdtrans.config.yaml", fixtureConfig), "write explicit-source fixtures")) return false;
    app.engine.reset();
    if (!Check(LoadEngine(app, programDir), "load explicit sources")) return false;
    app.userSources = UserSources(programDir);
    ShowDicts(app, first);
    const int index = app.dictIndex;
    if (!Check(index >= 0 && app.userDicts[static_cast<std::size_t>(index)].path == first,
               "GUI selects the opened explicit file")) return false;
    gui::TranslationOptions options;
    const auto translate = [&] {
        return gui::TranslateText(*app.engine, "猫 犬", ConfiguredOptions(app, options), app.cancel);
    };
    bool ok = Check(translate() == "앞사전뒤사전", "explicit sources initial literal");
    ToggleDict(app, index, false);
    ok = Check(ReadyEngine(app) && translate() == "고양이뒤사전" &&
               std::find(app.engineUserPaths.begin(), app.engineUserPaths.end(), Utf8(first.native())) == app.engineUserPaths.end(),
               "GUI off explicitly removes an active file source") && ok;
    ToggleDict(app, index, true);
    ok = Check(ReadyEngine(app) && translate() == "앞사전뒤사전" && app.engineUserPaths.front() == Utf8(first.native()),
               "GUI on restores the explicit file in its original source order") && ok;
    const auto dictionaries = app.engine->Dictionaries();
    const auto filters = app.engine->Filters();
    const auto paths = app.engineUserPaths;
    const auto version = app.loadedUserVersion;
    const HANDLE blocked = CreateFileW(second.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!Check(blocked != INVALID_HANDLE_VALUE, "deny reads during explicit reconfiguration")) return false;
    const bool renamed = RenameDict(app, index, L"renamed");
    if (!Check(renamed, "rename the explicit file in the GUI")) {
        CloseHandle(blocked);
        return false;
    }
    const fs::path renamedPath = app.userDicts[static_cast<std::size_t>(index)].path;
    ok = Check(!ReadyEngine(app) && app.reloadDicts.load() && !app.error.empty() &&
               app.engineUserPaths == paths && app.loadedUserVersion == version &&
               app.engine->Dictionaries() == dictionaries && app.engine->Filters() == filters && translate() == "앞사전뒤사전",
               "failed reconfiguration keeps old sources, stamps and the whole stack") && ok;
    ToggleDict(app, index, false);
    CloseHandle(blocked);
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "고양이뒤사전",
               "reconfiguration recovery preserves a newer GUI off edit") && ok;
    ShowDicts(app, renamedPath);
    if (!Check(app.dictIndex >= 0 && app.userDicts[static_cast<std::size_t>(app.dictIndex)].path == renamedPath,
               "renamed explicit source stays visible in the dictionary page")) return false;
    const auto offPaths = app.engineUserPaths;
    if (!Check(WriteFixture(user / L"hdtrans.config.yaml", fixtureConfig + "cg: false\nreference: true\n"),
               "change scalar config fields after an explicit rename and off")) return false;
    ok = Check(ReadyEngine(app) && !app.settings.cg && app.settings.reference && app.engineUserPaths == offPaths &&
               translate() == "고양이뒤사전", "scalar config changes preserve an existing GUI off and renamed source slot") && ok;
    ToggleDict(app, app.dictIndex, true);
    ok = Check(ReadyEngine(app) && translate() == "앞사전뒤사전" && app.engineUserPaths.front() == Utf8(renamedPath.native()) &&
               std::find(app.engineUserPaths.begin(), app.engineUserPaths.end(), Utf8(first.native())) == app.engineUserPaths.end(),
               "renamed explicit source recovers without leaving the old path active") && ok;
    const auto beforeMissing = app.engine->Dictionaries();
    const auto beforeMissingFilters = app.engine->Filters();
    fs::path away = renamedPath;
    away += L".away";
    fs::rename(renamedPath, away, error);
    if (!Check(!error, "remove an explicit source without a GUI intent")) return false;
    ok = Check(!ReadyEngine(app) && app.reloadDicts.load() && !app.error.empty() &&
               app.engine->Dictionaries() == beforeMissing && app.engine->Filters() == beforeMissingFilters &&
               translate() == "앞사전뒤사전", "unannounced explicit-file disappearance is fatal, not a GUI off") && ok;
    fs::rename(away, renamedPath, error);
    if (!Check(!error, "restore the disappeared explicit source")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "앞사전뒤사전",
               "same-path source restoration retries the pending load") && ok;
    app.engine.reset();
    return ok;
}

bool SelfTestOptionalSources(App& app, const fs::path& folder) {
    const fs::path program = folder / L"optional-sources";
    const fs::path dicts = program / L"toy-dicts";
    const std::string programDir = Utf8(program.native());
    const fs::path config(Wide(hdTrans::dict::ConfigFile(programDir)));
    const fs::path userFile = config.parent_path() / L"missing-user.txt";
    const fs::path ehnd = config.parent_path() / L"missing-ehnd";
    std::error_code error;
    for (const auto& directory : {dicts, config.parent_path()}) {
        fs::create_directories(directory, error);
        if (!Check(!error, "create optional-source fixture folders")) return false;
    }
    const fs::path relativeDicts = fs::relative(dicts, config.parent_path(), error);
    if (!Check(!error, "resolve the toy dictionary path")) return false;
    const std::string common = "dicts: '" + Utf8(relativeDicts.native()) + "'\ncache: off\nstrict_dict: true\n";
    if (!Check(WriteFixture(dicts / L"base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n"
                            "猫\tBASE_CAT\tN110\t\t\tGN\t\t\t\t\t1\n犬\tBASE_DOG\tN110\t\t\tGN\t\t\t\t\t2\n") &&
               WriteFixture(config, common + "ehnd: true\n"), "write default-source absence control")) return false;
    app.engine.reset();
    ClearLogs(app);
    if (!Check(LoadEngine(app, programDir), "load with an absent default Ehnd folder")) return false;
    SendMessageW(app.main, kLog, 0, 0);
    gui::TranslationOptions options;
    const auto translate = [&] {
        return gui::TranslateText(*app.engine, "猫 犬", ConfiguredOptions(app, options), app.cancel);
    };
    const auto defaultStack = app.engine->Dictionaries();
    const auto defaultRows = app.logEntries.size();
    bool ok = Check(app.engine->UserFileError().empty() && app.engine->FilterErrors().empty() &&
                    ControlText(app.main, IDC_LOG_STATUS).find(L"경고") == std::wstring::npos &&
                    std::any_of(app.logEntries.begin(), app.logEntries.end(), [](const auto& entry) { return entry.kind == L"INFO"; }) &&
                    !LogContains(app, L"Ehnd 활성화가 꺼져 있어 필터 및 사전 불러오기를 건너뜁니다") &&
                    ReadyEngine(app) && ReadyEngine(app) && app.engine->Dictionaries() == defaultStack && app.logEntries.size() == defaultRows,
                    "absent default Ehnd is warning-free with INFO and unchanged healthy readiness does not reload");
    const fs::path defaultEhnd(Wide(app.engineEhndDir));
    if (!Check(!defaultEhnd.empty(), "default helper preserves the enabled expected Ehnd path")) return false;
    fs::create_directories(defaultEhnd, error);
    if (!Check(!error && WriteFixture(defaultEhnd / L"UserDict_10.txt", "犬\tDEFAULT_EHND\t0\n"), "create the default Ehnd source later")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "BASE_CAT DEFAULT_EHND",
               "later default Ehnd creation is detected without a config or GUI edit") && ok;
    fs::path defaultAway = defaultEhnd;
    defaultAway += L".away";
    fs::rename(defaultEhnd, defaultAway, error);
    if (!Check(!error, "remove an accepted default Ehnd source")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "BASE_CATBASE_DOG",
               "a default Ehnd folder that disappears is removed") && ok;
    fs::rename(defaultAway, defaultEhnd, error);
    if (!Check(!error, "restore the accepted default Ehnd source")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "BASE_CAT DEFAULT_EHND", "restore the default source at the same path") && ok;
    const std::string explicitConfig = common + "userdict: missing-user.txt\nehnd: missing-ehnd\n";
    if (!Check(WriteFixture(config, explicitConfig), "write explicit missing-source control")) return false;
    app.engine.reset();
    ClearLogs(app);
    if (!Check(LoadEngine(app, programDir), "explicit missing sources do not fail initial strict loading")) return false;
    const auto userWarnings = app.engine->UserFileError();
    const auto filterWarnings = app.engine->FilterErrors();
    const auto ehndLogRows = std::count_if(app.logEntries.begin(), app.logEntries.end(), [](const auto& entry) {
        return entry.kind == L"사전" && entry.message.find(L"missing-ehnd") != std::wstring::npos;
    });
    ok = Check(!hdTrans::internal::FilterFileFailed(*app.engine) && userWarnings.find("missing-user.txt") != std::string::npos &&
               userWarnings.find("missing-ehnd") == std::string::npos && filterWarnings.size() == 1 &&
               filterWarnings.front().find("missing-ehnd") != std::string::npos && ehndLogRows == 1 &&
               LogContains(app, L"missing-user.txt") && LogContains(app, L"missing-ehnd") &&
               ControlText(app.main, IDC_LOG_STATUS) == L"사전 경고 2건",
               "explicit missing-source warnings use separate getters and log each warning once without failing startup") && ok;
    fs::create_directories(ehnd, error);
    if (!Check(!error && WriteFixture(userFile, "猫\tUSER_LOADED\n") &&
               WriteFixture(ehnd / L"UserDict_10.txt", "犬\tEHND_LOADED\t0\n"), "create previously missing sources")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "USER_LOADED EHND_LOADED",
               "newly available explicit sources are detected without a GUI edit") && ok;
    const auto loaded = app.engine->Dictionaries();
    const auto filters = app.engine->Filters();
    const auto version = app.loadedUserVersion;
    fs::path userAway = userFile;
    userAway += L".away";
    fs::path ehndAway = ehnd;
    ehndAway += L".away";
    fs::rename(userFile, userAway, error);
    if (!Check(!error, "remove the previously loaded explicit file")) return false;
    fs::rename(ehnd, ehndAway, error);
    if (!Check(!error, "remove the previously loaded explicit Ehnd folder")) return false;
    ok = Check(!ReadyEngine(app) && app.reloadDicts.load() && app.engine->Dictionaries() == loaded &&
               app.engine->Filters() == filters && app.loadedUserVersion == version && translate() == "USER_LOADED EHND_LOADED",
               "previously loaded missing sources are fatal and keep the old whole stack") && ok;
    fs::rename(userAway, userFile, error);
    if (!Check(!error, "restore the explicit file")) return false;
    fs::rename(ehndAway, ehnd, error);
    if (!Check(!error, "restore the explicit Ehnd folder")) return false;
    ok = Check(ReadyEngine(app) && app.error.empty() && translate() == "USER_LOADED EHND_LOADED",
               "same-path restoration retries with unchanged file contents") && ok;
    const auto beforeConfig = app.engine->Dictionaries();
    const auto beforeFilters = app.engine->Filters();
    fs::rename(ehnd, ehndAway, error);
    if (!Check(!error && WriteFixture(config, explicitConfig + "cg: false\n"), "change config while a loaded Ehnd folder is absent")) return false;
    ok = Check(!ReadyEngine(app) && app.settings.cg && app.engine->Dictionaries() == beforeConfig &&
               app.engine->Filters() == beforeFilters && translate() == "USER_LOADED EHND_LOADED",
               "a config candidate inherits accepted Ehnd history and cannot downgrade disappearance to an optional warning") && ok;
    fs::rename(ehndAway, ehnd, error);
    if (!Check(!error, "restore Ehnd before retrying the config candidate")) return false;
    ok = Check(ReadyEngine(app) && !app.settings.cg && app.error.empty() && translate() == "USER_LOADED EHND_LOADED",
               "config and restored Ehnd commit together after history-preserving retry") && ok;
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
    ok = SelfTestReload(app, folder) && ok;
    ok = SelfTestExplicitSources(app, folder) && ok;
    ok = SelfTestOptionalSources(app, folder) && ok;
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
