#include "frontend/eztrans_registry.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

#include "hdtest.h"

namespace {

using hdTrans::frontend::ClassifyFilePath;
using hdTrans::frontend::EzTransResult;
using hdTrans::frontend::EzTransState;
using hdTrans::frontend::EzTransTarget;
using hdTrans::frontend::FolderHasEngine;
using hdTrans::frontend::PlanRegister;
using hdTrans::frontend::PlanUnregister;
using hdTrans::frontend::SameFolder;

using Text = std::optional<std::string>;

bool SameState(const EzTransState& a, const EzTransState& b) {
    return a.filePath == b.filePath && a.registered == b.registered && a.previous == b.previous;
}

constexpr const char* kSelf = "C:\\hdTrans";
constexpr const char* kEzTrans = "C:\\Program Files (x86)\\ChangShinSoft\\ezTrans XP";

}  // namespace

HD_TEST(EzTransSameFolder) {
    HD_CHECK(SameFolder("C:\\hdTrans", "c:\\HDTRANS\\"));
    HD_CHECK(SameFolder("C:\\hdTrans", "C:/hdTrans/"));
    HD_CHECK(SameFolder("C:\\", "C:\\"));
    HD_CHECK(SameFolder("C:\\하드트랜스", "c:\\하드트랜스"));
    HD_CHECK(!SameFolder("C:\\hdTrans", "C:\\hdTrans2"));
    HD_CHECK(!SameFolder("C:\\", "C:"));
    HD_CHECK(!SameFolder("", "C:\\hdTrans"));
}

HD_TEST(EzTransClassifyFilePath) {
    struct Case {
        Text filePath;
        Text registered;
        bool engine;
        EzTransTarget expected;
    };
    const Case cases[] = {
        {std::nullopt, std::nullopt, false, EzTransTarget::kNone},
        {"", std::nullopt, false, EzTransTarget::kNone},
        {"c:\\hdtrans\\", std::nullopt, true, EzTransTarget::kSelf},
        {"D:\\old\\hdTrans", "D:\\old\\hdTrans", true, EzTransTarget::kOurs},
        {kEzTrans, std::nullopt, true, EzTransTarget::kEngine},
        {kEzTrans, "D:\\old\\hdTrans", true, EzTransTarget::kEngine},
        {"D:\\removed", std::nullopt, false, EzTransTarget::kMissing},
    };
    for (const auto& c : cases) {
        const EzTransState state{c.filePath, c.registered, std::nullopt};
        HD_CHECK(ClassifyFilePath(state, kSelf, c.engine) == c.expected);
    }
}

HD_TEST(EzTransRegister) {
    struct Case {
        const char* name;
        EzTransState before;
        bool engineAtSelf;
        bool engineAtFilePath;
        bool force;
        EzTransResult result;
        EzTransState after;
    };
    const Case cases[] = {
        {"no value", {}, true, false, false, EzTransResult::kChanged, {kSelf, kSelf, std::nullopt}},
        {"empty value", {"", {}, {}}, true, false, false, EzTransResult::kChanged, {kSelf, kSelf, ""}},
        {"stale folder", {"D:\\removed", {}, {}}, true, false, false, EzTransResult::kChanged,
         {kSelf, kSelf, "D:\\removed"}},
        {"ezTrans kept", {kEzTrans, {}, {}}, true, true, false, EzTransResult::kOtherEngine, {kEzTrans, {}, {}}},
        {"ezTrans forced", {kEzTrans, {}, {}}, true, true, true, EzTransResult::kChanged, {kSelf, kSelf, kEzTrans}},
        {"no engine here", {}, false, false, true, EzTransResult::kNoEngine, {}},
        {"already", {kSelf, kSelf, kEzTrans}, true, true, false, EzTransResult::kUnchanged, {kSelf, kSelf, kEzTrans}},
        {"set by hand", {"c:\\hdtrans\\", {}, {}}, true, true, false, EzTransResult::kUnchanged,
         {"c:\\hdtrans\\", {}, {}}},
        {"moved folder keeps the first value", {"D:\\old", "D:\\old", kEzTrans}, true, true, false,
         EzTransResult::kChanged, {kSelf, kSelf, kEzTrans}},
        {"back to a folder registered before", {kSelf, "D:\\old", kEzTrans}, true, true, false,
         EzTransResult::kChanged, {kSelf, kSelf, kEzTrans}},
        {"changed after registering", {"D:\\other", "D:\\old", kEzTrans}, true, true, false,
         EzTransResult::kOtherEngine, {"D:\\other", "D:\\old", kEzTrans}},
        {"changed after registering, forced", {"D:\\other", "D:\\old", kEzTrans}, true, true, true,
         EzTransResult::kChanged, {kSelf, kSelf, "D:\\other"}},
        {"removed after registering keeps the saved value", {{}, "D:\\old", kEzTrans}, true, false, false,
         EzTransResult::kChanged, {kSelf, kSelf, kEzTrans}},
        {"stale after registering keeps the saved value", {"D:\\removed", "D:\\old", kEzTrans}, true, false, true,
         EzTransResult::kChanged, {kSelf, kSelf, kEzTrans}},
        {"registered twice keeps no value", {kSelf, kSelf, {}}, true, true, true, EzTransResult::kUnchanged,
         {kSelf, kSelf, {}}},
    };
    for (const auto& c : cases) {
        const auto change = PlanRegister(c.before, "C:\\hdTrans\\", c.engineAtSelf, c.engineAtFilePath, c.force);
        if (change.result != c.result || !SameState(change.after, c.after))
            hdtest::Fail(__FILE__, __LINE__, std::string("register: ") + c.name);
    }
}

HD_TEST(EzTransUnregister) {
    struct Case {
        const char* name;
        EzTransState before;
        bool force;
        EzTransResult result;
        EzTransState after;
    };
    const Case cases[] = {
        {"restores", {kSelf, kSelf, kEzTrans}, false, EzTransResult::kChanged, {kEzTrans, {}, {}}},
        {"restores no value", {kSelf, kSelf, {}}, false, EzTransResult::kChanged, {}},
        {"restores empty", {kSelf, kSelf, ""}, false, EzTransResult::kChanged, {"", {}, {}}},
        {"nothing saved", {kSelf, {}, {}}, false, EzTransResult::kNotRegistered, {kSelf, {}, {}}},
        {"changed since", {"D:\\other", kSelf, kEzTrans}, false, EzTransResult::kModified,
         {"D:\\other", kSelf, kEzTrans}},
        {"removed since", {{}, kSelf, kEzTrans}, false, EzTransResult::kModified, {{}, kSelf, kEzTrans}},
        {"changed since, forced", {"D:\\other", kSelf, kEzTrans}, true, EzTransResult::kChanged, {kEzTrans, {}, {}}},
        {"stopped before clearing", {kEzTrans, kSelf, kEzTrans}, false, EzTransResult::kChanged, {kEzTrans, {}, {}}},
        {"stopped before clearing no value", {{}, kSelf, {}}, false, EzTransResult::kChanged, {}},
    };
    for (const auto& c : cases) {
        const auto change = PlanUnregister(c.before, c.force);
        if (change.result != c.result || !SameState(change.after, c.after))
            hdtest::Fail(__FILE__, __LINE__, std::string("unregister: ") + c.name);
    }
}

HD_TEST(EzTransFolderHasEngine) {
    std::error_code error;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = std::filesystem::temp_directory_path(error) / ("hdtrans-eztrans-" + std::to_string(stamp));
    HD_REQUIRE(!error);
    HD_REQUIRE(std::filesystem::create_directories(folder / "Dat", error));
    const auto utf8 = folder.u8string();
    const std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    HD_CHECK(!FolderHasEngine(path));
    std::ofstream(folder / "J2KEngine.dll").put('M');
    HD_CHECK(FolderHasEngine(path));
    HD_CHECK(!FolderHasEngine(path + "/Dat"));
    HD_CHECK(!FolderHasEngine(""));
    std::filesystem::remove_all(folder, error);
}

#ifdef _WIN32
namespace {

// Deletes the scratch key on every return path of the test.
struct ScratchKey {
    std::wstring root = L"Software\\hdTrans-test-" + std::to_wstring(GetCurrentProcessId()) + L"-"
        + std::to_wstring(GetTickCount64());
    ScratchKey() = default;
    ScratchKey(const ScratchKey&) = delete;
    ScratchKey& operator=(const ScratchKey&) = delete;
    ~ScratchKey() {
        HKEY handle = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE
                          | KEY_SET_VALUE | KEY_WOW64_32KEY, &handle) == ERROR_SUCCESS) {
            RegDeleteTreeW(handle, nullptr);
            RegCloseKey(handle);
        }
        RegDeleteKeyExW(HKEY_CURRENT_USER, root.c_str(), KEY_WOW64_32KEY, 0);
    }
};

bool KeyExists(const std::wstring& key) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &handle) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

bool SetRaw(const std::wstring& key, DWORD type, const void* data, std::size_t bytes) {
    HKEY handle = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE | KEY_WOW64_32KEY, nullptr, &handle, nullptr) != ERROR_SUCCESS) return false;
    const LSTATUS status = RegSetValueExW(handle, L"FilePath", 0, type, static_cast<const BYTE*>(data),
                                           static_cast<DWORD>(bytes));
    RegCloseKey(handle);
    return status == ERROR_SUCCESS;
}

}  // namespace

HD_TEST(EzTransRegistryRoundTrip) {
    using hdTrans::frontend::ReadEzTransState;
    using hdTrans::frontend::WriteEzTransState;
    ScratchKey scratch;
    const std::wstring& root = scratch.root;
    HD_REQUIRE(root != L"Software");
    std::string error;
    EzTransState state;
    HD_CHECK(ReadEzTransState(state, error, root));
    HD_CHECK(SameState(state, {}));

    const EzTransState installed{kEzTrans, {}, {}};
    HD_CHECK(WriteEzTransState(state, installed, error, root));
    HD_CHECK(ReadEzTransState(state, error, root));
    HD_CHECK(SameState(state, installed));

    const auto registered = PlanRegister(state, kSelf, true, true, true);
    HD_CHECK(registered.result == EzTransResult::kChanged);
    HD_CHECK(WriteEzTransState(state, registered.after, error, root));
    HD_CHECK(ReadEzTransState(state, error, root));
    HD_CHECK(SameState(state, registered.after));

    const auto restored = PlanUnregister(state, false);
    HD_CHECK(restored.result == EzTransResult::kChanged);
    HD_CHECK(WriteEzTransState(state, restored.after, error, root));
    HD_CHECK(ReadEzTransState(state, error, root));
    HD_CHECK(SameState(state, installed));

    // Restoring "no value" removes the keys it leaves empty.
    HD_CHECK(WriteEzTransState(state, {}, error, root));
    HD_CHECK(!KeyExists(root + L"\\ChangShin"));
    HD_CHECK(!KeyExists(root + L"\\hdTrans"));

    const std::wstring ezTrans = root + L"\\ChangShin\\ezTrans";
    const wchar_t unterminated[] = {L'D', L':', L'\\', L'x'};
    HD_CHECK(SetRaw(ezTrans, REG_SZ, unterminated, sizeof(unterminated)));
    HD_CHECK(ReadEzTransState(state, error, root));
    HD_CHECK(state.filePath == Text("D:\\x"));

    const wchar_t embedded[] = L"D:\\x\0y";
    HD_CHECK(SetRaw(ezTrans, REG_SZ, embedded, sizeof(embedded)));
    HD_CHECK(ReadEzTransState(state, error, root));
    HD_CHECK(state.filePath == Text("D:\\x"));

    const wchar_t expand[] = L"%SystemRoot%\\x";
    HD_CHECK(SetRaw(ezTrans, REG_EXPAND_SZ, expand, sizeof(expand)));
    HD_CHECK(ReadEzTransState(state, error, root));
    HD_CHECK(state.filePath && state.filePath->find('%') == std::string::npos && state.filePath->ends_with("\\x"));

    const DWORD number = 1;
    HD_CHECK(SetRaw(ezTrans, REG_DWORD, &number, sizeof(number)));
    HD_CHECK(!ReadEzTransState(state, error, root));
    HD_CHECK(error.find("not a string") != std::string::npos);
}
#endif
