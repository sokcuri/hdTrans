#include "frontend/eztrans_registry.h"

#include <filesystem>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hdTrans::frontend {
namespace {

bool IsSeparator(char c) { return c == '\\' || c == '/'; }

// Without trailing separators, except the one of a drive root (C:\).
std::string_view TrimFolder(std::string_view path) {
    while (path.size() > 1 && IsSeparator(path.back()) && !(path.size() == 3 && path[1] == ':')) path.remove_suffix(1);
    return path;
}

char FoldChar(char c) {
    if (c == '/') return '\\';
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

}  // namespace

bool SameFolder(std::string_view a, std::string_view b) {
    a = TrimFolder(a);
    b = TrimFolder(b);
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (FoldChar(a[i]) != FoldChar(b[i])) return false;
    }
    return true;
}

bool FolderHasEngine(const std::string& folder) {
    if (folder.empty()) return false;
    const std::filesystem::path base(std::u8string(reinterpret_cast<const char8_t*>(folder.data()), folder.size()));
    std::error_code error;
    return std::filesystem::is_regular_file(base / "J2KEngine.dll", error);
}

EzTransTarget ClassifyFilePath(const EzTransState& state, std::string_view self, bool engineAtFilePath) {
    if (!state.filePath || TrimFolder(*state.filePath).empty()) return EzTransTarget::kNone;
    if (SameFolder(*state.filePath, self)) return EzTransTarget::kSelf;
    if (state.registered && SameFolder(*state.filePath, *state.registered)) return EzTransTarget::kOurs;
    return engineAtFilePath ? EzTransTarget::kEngine : EzTransTarget::kMissing;
}

EzTransChange PlanRegister(const EzTransState& state, std::string_view self, bool engineAtSelf,
                           bool engineAtFilePath, bool force) {
    EzTransChange change{EzTransResult::kUnchanged, state};
    if (!engineAtSelf) {
        change.result = EzTransResult::kNoEngine;
        return change;
    }
    const std::string folder(TrimFolder(self));
    switch (ClassifyFilePath(state, self, engineAtFilePath)) {
    case EzTransTarget::kSelf:
        // Set by hand, or by a registration of this folder; a saved value then follows this folder.
        if (state.registered && !SameFolder(*state.registered, folder)) {
            change.after.registered = folder;
            change.result = EzTransResult::kChanged;
        }
        return change;
    case EzTransTarget::kOurs:
        change.after.filePath = folder;
        change.after.registered = folder;
        change.result = EzTransResult::kChanged;
        return change;
    case EzTransTarget::kEngine:
        if (!force) {
            change.result = EzTransResult::kOtherEngine;
            return change;
        }
        break;
    case EzTransTarget::kNone:
    case EzTransTarget::kMissing:
        // A saved value is replaced only by an engine folder, with force.
        if (state.registered) {
            change.after.filePath = folder;
            change.after.registered = folder;
            change.result = EzTransResult::kChanged;
            return change;
        }
        break;
    }
    change.after.filePath = folder;
    change.after.registered = folder;
    change.after.previous = state.filePath;
    change.result = EzTransResult::kChanged;
    return change;
}

EzTransChange PlanUnregister(const EzTransState& state, bool force) {
    EzTransChange change{EzTransResult::kUnchanged, state};
    if (!state.registered) {
        change.result = EzTransResult::kNotRegistered;
        return change;
    }
    const bool atRegistered = state.filePath && SameFolder(*state.filePath, *state.registered);
    // FilePath already holds the saved value when an earlier run stopped before clearing it.
    const bool restored = state.filePath == state.previous;
    if (!force && !atRegistered && !restored) {
        change.result = EzTransResult::kModified;
        return change;
    }
    change.after.filePath = state.previous;
    change.after.registered.reset();
    change.after.previous.reset();
    change.result = EzTransResult::kChanged;
    return change;
}

#ifdef _WIN32
namespace {

// HKCU\Software is shared by the 32-bit and 64-bit views; the hosts are 32-bit, so name that view anyway.
constexpr REGSAM kView = KEY_WOW64_32KEY;

// Empty when text is not valid UTF-8.
std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

bool Utf8(std::wstring_view text, std::string& out) {
    out.clear();
    if (text.empty()) return true;
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return false;
    out.resize(static_cast<std::size_t>(size));
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
    return true;
}

std::string KeyName(const std::wstring& key) {
    std::string name;
    Utf8(key, name);
    return "HKEY_CURRENT_USER\\" + name;
}

bool Failed(LSTATUS status, const std::wstring& key, std::string& error) {
    if (status == ERROR_SUCCESS) return false;
    error = KeyName(key) + ": error " + std::to_string(status);
    return true;
}

bool ExpandVariables(std::wstring& text) {
    std::wstring out(text.size() + 1, L'\0');
    for (;;) {
        const DWORD count = ExpandEnvironmentStringsW(text.c_str(), out.data(), static_cast<DWORD>(out.size()));
        if (count == 0) return false;
        if (count <= out.size()) {
            out.resize(count - 1);
            text = std::move(out);
            return true;
        }
        out.resize(count);
    }
}

// A missing key or value is success with no value. The value ends at its first NUL, as the hosts read it;
// REG_EXPAND_SZ is expanded. Other types are an error, so a value that cannot be saved is never replaced.
bool QueryString(const std::wstring& key, const wchar_t* name, std::optional<std::string>& value, std::string& error) {
    value.reset();
    HKEY handle = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE | kView, &handle);
    if (status == ERROR_FILE_NOT_FOUND) return true;
    if (Failed(status, key, error)) return false;
    DWORD type = 0;
    DWORD bytes = 0;
    std::wstring data;
    status = RegQueryValueExW(handle, name, nullptr, &type, nullptr, &bytes);
    while (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        data.assign(bytes / sizeof(wchar_t) + 1, L'\0');
        DWORD size = static_cast<DWORD>(data.size() * sizeof(wchar_t));
        status = RegQueryValueExW(handle, name, nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &size);
        if (status == ERROR_MORE_DATA) {
            bytes = size;
            status = ERROR_SUCCESS;
            continue;
        }
        if (status == ERROR_SUCCESS) data.resize(size / sizeof(wchar_t));
        break;
    }
    RegCloseKey(handle);
    if (status == ERROR_FILE_NOT_FOUND) return true;
    if (Failed(status, key, error)) return false;
    std::string text;
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        Utf8(name, text);
        error = KeyName(key) + ": " + text + " is not a string value";
        return false;
    }
    const std::size_t end = data.find(L'\0');
    if (end != std::wstring::npos) data.resize(end);
    if ((type == REG_EXPAND_SZ && !ExpandVariables(data)) || !Utf8(data, text)) {
        Utf8(name, text);
        error = KeyName(key) + ": " + text + " cannot be read as a path";
        return false;
    }
    value = std::move(text);
    return true;
}

LSTATUS SetString(const std::wstring& key, const wchar_t* name, const std::string& value) {
    const std::wstring data = Wide(value);
    if (data.empty() && !value.empty()) return ERROR_NO_UNICODE_TRANSLATION;
    HKEY handle = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                     KEY_SET_VALUE | kView, nullptr, &handle, nullptr);
    if (status != ERROR_SUCCESS) return status;
    status = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
                            static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
    return status;
}

LSTATUS DeleteValue(const std::wstring& key, const wchar_t* name) {
    HKEY handle = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE | kView, &handle);
    if (status == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (status != ERROR_SUCCESS) return status;
    status = RegDeleteValueW(handle, name);
    RegCloseKey(handle);
    return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
}

// Best effort: a key left behind does not change what the hosts read.
void DeleteKeyIfEmpty(const std::wstring& key) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE | kView, &handle) != ERROR_SUCCESS) return;
    DWORD subkeys = 0;
    DWORD values = 0;
    const LSTATUS status = RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values,
                                            nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(handle);
    if (status == ERROR_SUCCESS && subkeys == 0 && values == 0) RegDeleteKeyExW(HKEY_CURRENT_USER, key.c_str(), kView, 0);
}

struct Keys {
    std::wstring vendor, ezTrans, own, saved;
};

Keys KeysOf(const std::wstring& root) {
    Keys keys;
    keys.vendor = root + L"\\ChangShin";
    keys.ezTrans = keys.vendor + L"\\ezTrans";
    keys.own = root + L"\\hdTrans";
    keys.saved = keys.own + L"\\ezTrans";
    return keys;
}

}  // namespace

bool ReadEzTransState(EzTransState& state, std::string& error, const std::wstring& root) {
    const Keys keys = KeysOf(root);
    EzTransState read;
    if (!QueryString(keys.ezTrans, L"FilePath", read.filePath, error)) return false;
    if (!QueryString(keys.saved, L"FilePath", read.registered, error)) return false;
    if (!QueryString(keys.saved, L"PreviousFilePath", read.previous, error)) return false;
    if (!read.registered) read.previous.reset();
    state = std::move(read);
    return true;
}

bool WriteEzTransState(const EzTransState& before, const EzTransState& after, std::string& error,
                       const std::wstring& root) {
    const Keys keys = KeysOf(root);
    const auto writeFilePath = [&]() {
        if (after.filePath == before.filePath) return true;
        if (Failed(after.filePath ? SetString(keys.ezTrans, L"FilePath", *after.filePath)
                                  : DeleteValue(keys.ezTrans, L"FilePath"), keys.ezTrans, error)) return false;
        if (!after.filePath) {
            DeleteKeyIfEmpty(keys.ezTrans);
            DeleteKeyIfEmpty(keys.vendor);
        }
        return true;
    };
    // FilePath under the saved key marks a saved registration: written last, deleted first.
    const auto writeSaved = [&]() {
        if (after.registered == before.registered && after.previous == before.previous) return true;
        if (!after.registered) {
            if (Failed(DeleteValue(keys.saved, L"FilePath"), keys.saved, error)) return false;
            DeleteValue(keys.saved, L"PreviousFilePath");
            DeleteKeyIfEmpty(keys.saved);
            DeleteKeyIfEmpty(keys.own);
            return true;
        }
        if (Failed(after.previous ? SetString(keys.saved, L"PreviousFilePath", *after.previous)
                                  : DeleteValue(keys.saved, L"PreviousFilePath"), keys.saved, error)) return false;
        return !Failed(SetString(keys.saved, L"FilePath", *after.registered), keys.saved, error);
    };
    if (!after.registered || (before.registered && after.previous == before.previous)) {
        return writeFilePath() && writeSaved();
    }
    return writeSaved() && writeFilePath();
}
#endif

}  // namespace hdTrans::frontend
