#pragma once

#include <cstdint>
#include <vector>

#include "core/dict/load_info.h"

namespace hdTrans {

class Engine;

namespace internal {

struct LoadInfo {
    std::uint64_t generation = 0;
    std::vector<dict::LoadFileInfo> files{};
};

// Copies the active sources under the engine's mutex, independently of load errors.
LoadInfo LoadedSourceInfo(const Engine& engine);

}  // namespace internal
}  // namespace hdTrans
