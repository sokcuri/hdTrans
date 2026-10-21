#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include "core/engine.h"

namespace hdTrans::gui {

enum class OutputMode { Translation, Interleaved, Spaced };

struct TranslationOptions {
    OutputMode output = OutputMode::Translation;
    bool skipBlankLines = true;
    bool wholeDocument = true;
    bool cg = true;
    bool misread = false;
    Speech speech = Speech::Soft;
};

// Input is UTF-8 with LF line endings. Translation keeps blank lines; in mixed modes,
// skipBlankLines emits them once instead of as empty source/translation pairs.
// Spaced adds a blank line between pairs, not after the last.
// If filters change line counts, mixed modes show one complete source/translation block pair.
// Cancellation discards the result; an engine call already running cannot be interrupted.
std::string TranslateText(const hdTrans::Engine& engine, std::string_view text,
                          const TranslationOptions& options, const std::atomic<bool>& cancelled);

}  // namespace hdTrans::gui
