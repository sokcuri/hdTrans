#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace hdTrans::dict {

enum class LoadFileKind : std::uint8_t {
    Shipped,
    NativeUser,
    NativePreFilter,
    NativePostFilter,
    EhndPreFilter,
    EhndPostFilter,
    EhndSkipLayer,
    EhndUserDict,
    CompiledSnapshot,
    CachedDictionary,
    MemoryUser,
    MemoryEhndPreFilter,
    MemoryEhndPostFilter,
    MemoryEhndSkipLayer,
    MemoryEhndUserDict,
    EhndDisabled,
};

struct LoadFileInfo {
    std::string path{};
    LoadFileKind kind = LoadFileKind::Shipped;
    // Accepted source entries, not expanded records or trie keys; absent when not recorded.
    std::optional<std::size_t> acceptedCount{};
};

}  // namespace hdTrans::dict
