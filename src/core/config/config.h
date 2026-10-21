#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/engine.h"

namespace hdTrans::internal {

struct Config {
    std::optional<std::vector<std::string>> dicts;
    std::optional<std::vector<std::string>> userdict;
    std::optional<std::string> ehnd;
    bool useEhnd = false;
    std::optional<std::string> cache;
    bool cg = true;
    bool misread = false;
    bool reference = false;
    Soften soften = Soften::Quotes;
};

struct ConfigDiagnostic {
    std::size_t line = 0;  // 1-based; 0 for file errors.
    std::string message;
};

struct ConfigResult {
    Config settings;
    std::vector<ConfigDiagnostic> diagnostics;
    bool found = false;
};

// The mode named off, quotes, marked or dialogue; false for another name.
bool SoftenOf(std::string_view name, Soften& out);

// Missing files are optional. Invalid entries are skipped; earlier valid entries survive syntax errors.
ConfigResult LoadConfig(std::string_view path);
std::string ConfigErrors(const ConfigResult& result, std::string_view path);

}  // namespace hdTrans::internal
