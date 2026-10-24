#include <algorithm>
#include <span>

#include "core/dict/classes.h"
#include "core/pipeline/normalize.h"
#include "core/pipeline/runes.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline {

namespace {

bool KanjiOrKatakana(char32_t r) { return text::IsKanji(r) || internal::IsKatakanaRune(r); }

// Whether the text around line[pos, pos + variant.size()) lets the variant be the grammatical word.
bool StandsAsWord(std::string_view line, size_t pos, std::string_view variant, std::string_view spelling) {
    for (const std::string_view kept : dict::ClassMembers("spelling.variant-kept")) {
        const size_t k = kept.find(variant);
        if (k != std::string_view::npos && k <= pos && line.substr(pos - k).starts_with(kept)) return false;
    }
    const text::Rune last = internal::LastRune(line.substr(0, pos));
    const std::string_view before = line.substr(pos - last.size, last.size);
    const char32_t prev = last.cp;
    if (!text::IsKanji(text::DecodeRune(variant).cp)) {
        return !text::IsKanji(text::DecodeRune(spelling).cp) || dict::InClass("spelling.variant-kana-after", before);
    }
    return internal::IsHiraganaRune(prev) || (KanjiOrKatakana(prev) && dict::InClass("spelling.variant-after-word", variant));
}

}  // namespace

std::string RespellVariants(std::string_view line, WordKeepsVariant keeps, const void* context) {
    std::string out;
    size_t copied = 0, pos = 0;
    const std::span<const std::string_view> pairs = dict::ClassMembers("spelling.variant");
    while (pos < line.size()) {
        size_t taken = 0;
        for (const std::string_view pair : pairs) {
            const size_t eq = pair.find('=');
            const std::string_view variant = pair.substr(0, eq), spelling = pair.substr(eq + 1);
            if (!line.substr(pos).starts_with(variant) || !StandsAsWord(line, pos, variant, spelling) ||
                keeps(context, line, pos, variant, spelling)) {
                continue;
            }
            out.append(line.substr(copied, pos - copied));
            out.append(spelling);
            taken = variant.size();
            copied = pos + taken;
            break;
        }
        pos += taken > 0 ? taken : std::max<size_t>(1, text::DecodeRune(line.substr(pos)).size);
    }
    if (copied == 0) return std::string(line);
    out.append(line.substr(copied));
    return out;
}

}  // namespace hdTrans::pipeline
