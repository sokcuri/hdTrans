#include "gui/translation.h"

#include <utility>
#include <vector>

#include "core/engine.h"
#include "core/text/lines.h"

namespace hdTrans::gui {
namespace {

std::vector<std::string_view> DocumentLines(std::string_view value) {
    auto lines = text::Lines(value);
    // The final LF terminates a line; it does not start another pair.
    if (value.empty() || value.ends_with('\n')) lines.pop_back();
    return lines;
}

struct TextOutput {
    std::string value;
    bool hasLine = false;
    bool hasPair = false;

    void Append(std::string_view source, std::string_view translated, const TranslationOptions& options) {
        const bool singleBlank = source.empty() && translated.empty() &&
            (options.output == OutputMode::Translation || options.skipBlankLines);
        if (hasLine) value += '\n';
        if (!singleBlank) {
            if (hasPair && options.output == OutputMode::Spaced) value += '\n';
            if (options.output != OutputMode::Translation) {
                value += source;
                value += '\n';
            }
            value += translated;
            hasPair = true;
        }
        hasLine = true;
    }
};

std::string Block(const std::vector<std::string_view>& lines, const std::atomic<bool>& cancelled) {
    std::string out;
    bool hasLine = false;
    for (const auto line : lines) {
        if (cancelled.load(std::memory_order_relaxed)) return {};
        if (hasLine) out += '\n';
        out += line;
        hasLine = true;
    }
    return out;
}

std::string FormatDocument(std::string_view source, std::string_view translated,
                           const TranslationOptions& options, const std::atomic<bool>& cancelled) {
    const auto sourceLines = DocumentLines(source);
    const auto translatedLines = DocumentLines(translated);
    TextOutput out;
    if (sourceLines.size() == translatedLines.size()) {
        for (std::size_t i = 0; i < sourceLines.size(); ++i) {
            if (cancelled.load(std::memory_order_relaxed)) return {};
            out.Append(sourceLines[i], translatedLines[i], options);
        }
    } else {
        // Filters can insert newlines. Keep both complete blocks rather than guessing alignment.
        const auto sourceBlock = Block(sourceLines, cancelled);
        const auto translatedBlock = Block(translatedLines, cancelled);
        out.Append(sourceBlock, translatedBlock, options);
    }
    if (out.hasLine && (source.ends_with('\n') || translated.ends_with('\n'))) out.value += '\n';
    if (cancelled.load(std::memory_order_relaxed)) return {};
    return std::move(out.value);
}

}  // namespace

std::string TranslateText(const hdTrans::Engine& engine, std::string_view text,
                          const TranslationOptions& options, const std::atomic<bool>& cancelled) {
    if (cancelled.load(std::memory_order_relaxed)) return {};
    std::string translated;
    if (options.wholeDocument) {
        Session session(engine, false, options.cg, options.misread);
        session.SetSpeech(options.speech);
        translated = session.Translate(text);
    } else {
        const auto lines = DocumentLines(text);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (cancelled.load(std::memory_order_relaxed)) return {};
            if (i > 0) translated += '\n';
            if (!options.skipBlankLines || !lines[i].empty()) {
                Session session(engine, false, options.cg, options.misread);
                session.SetSpeech(options.speech);
                translated += session.Translate(lines[i]);
            }
            if (cancelled.load(std::memory_order_relaxed)) return {};
        }
        if (text.ends_with('\n')) translated += '\n';
    }
    if (cancelled.load(std::memory_order_relaxed)) return {};
    return FormatDocument(text, translated, options, cancelled);
}

}  // namespace hdTrans::gui
