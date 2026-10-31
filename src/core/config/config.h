#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/engine.h"
#include "core/dict/mapped_file.h"

namespace hdTrans::internal {

struct Config {
    std::optional<std::vector<std::string>> dicts;
    std::optional<std::vector<std::string>> userdict;
    std::optional<std::string> ehnd;
    bool useEhnd = false;
    std::optional<std::string> cache;
    std::optional<bool> strictDict;
    bool cg = true;
    bool misread = false;
    bool reference = false;
    Speech speech = Speech::Strict;
    SpeechLevels speechLevels = SpeechLevels::Auto;
    bool wrapped = false;
    bool predicateVariants = true;
};

struct ConfigDiagnostic {
    std::size_t line = 0;  // 1-based; 0 for file errors.
    std::string message;
    bool environment = false;  // from an environment variable, not the file
};

struct ConfigResult {
    Config settings;
    std::vector<ConfigDiagnostic> diagnostics;
    bool found = false;
    std::optional<dict::internal::FileStamp> sourceStamp;
};

// The speech level named soft or strict; false for another name.
bool SpeechOf(std::string_view name, Speech& out);

// The speech levels named auto or keep; false for another name.
bool SpeechLevelsOf(std::string_view name, SpeechLevels& out);

// Missing files are optional. HDTRANS_SPEECH_LEVELS (auto or keep) overrides speech_levels. Invalid entries are skipped; earlier valid entries survive syntax errors.
ConfigResult LoadConfig(std::string_view path);
std::string ConfigErrors(const ConfigResult& result, std::string_view path);

}  // namespace hdTrans::internal
