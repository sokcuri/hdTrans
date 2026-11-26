#pragma once
// The ezTrans XP install location that host tools read: HKCU\Software\ChangShin\ezTrans, value
// FilePath (REG_SZ, the folder holding J2KEngine.dll). Registering hdTrans writes that value and keeps
// the value it replaced in HKCU\Software\hdTrans\ezTrans (FilePath: the folder written,
// PreviousFilePath: the replaced value, absent when there was none) so that unregistering restores it.
// Only front ends call this, on an explicit user request; the library never touches the registry.
#include <optional>
#include <string>
#include <string_view>

namespace hdTrans::frontend {

// All paths are UTF-8.
struct EzTransState {
    std::optional<std::string> filePath;    // ChangShin FilePath
    std::optional<std::string> registered;  // the folder hdTrans wrote; set while a registration is saved
    std::optional<std::string> previous;    // the value it replaced; meaningful only with registered
};

enum class EzTransTarget {
    kNone,     // FilePath absent or empty
    kSelf,     // this hdTrans folder
    kOurs,     // the folder an earlier registration wrote, now elsewhere
    kMissing,  // a folder without J2KEngine.dll
    kEngine,   // a folder with J2KEngine.dll: ezTrans XP or another engine
};

enum class EzTransResult {
    kChanged,        // apply `after`
    kUnchanged,      // already in the requested state
    kNoEngine,       // J2KEngine.dll is not in the folder to register
    kOtherEngine,    // FilePath names another engine; needs force
    kNotRegistered,  // nothing saved to restore
    kModified,       // FilePath is no longer the registered folder; needs force
};

struct EzTransChange {
    EzTransResult result = EzTransResult::kUnchanged;
    EzTransState after;
};

// Windows path equality: separators, trailing separators and ASCII case do not count.
bool SameFolder(std::string_view a, std::string_view b);
bool FolderHasEngine(const std::string& folder);

// engineAtFilePath: FolderHasEngine(state.filePath).
EzTransTarget ClassifyFilePath(const EzTransState& state, std::string_view self, bool engineAtFilePath);
EzTransChange PlanRegister(const EzTransState& state, std::string_view self, bool engineAtSelf,
                           bool engineAtFilePath, bool force);
EzTransChange PlanUnregister(const EzTransState& state, bool force);

#ifdef _WIN32
// root is the HKCU subkey holding ChangShin and hdTrans; tests pass a scratch key.
bool ReadEzTransState(EzTransState& state, std::string& error, const std::wstring& root = L"Software");
// Writes the differences from before to after: the saved value before FilePath when registering,
// after it when restoring, so a failure part way never loses the replaced value.
bool WriteEzTransState(const EzTransState& before, const EzTransState& after, std::string& error,
                       const std::wstring& root = L"Software");
#endif

}  // namespace hdTrans::frontend
