#include <algorithm>
#include <span>
#include <unordered_map>

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
    for (const std::string_view kept : dict::ClassMembers(HDTRANS_CLASS("spelling.variant-kept"))) {
        const size_t k = kept.find(variant);
        if (k != std::string_view::npos && k <= pos && line.substr(pos - k).starts_with(kept)) return false;
    }
    const text::Rune last = internal::LastRune(line.substr(0, pos));
    const std::string_view before = line.substr(pos - last.size, last.size);
    const char32_t prev = last.cp;
    if (!text::IsKanji(text::DecodeRune(variant).cp)) {
        return !text::IsKanji(text::DecodeRune(spelling).cp) || dict::InClass(HDTRANS_CLASS("spelling.variant-kana-after"), before);
    }
    return internal::IsHiraganaRune(prev) || (KanjiOrKatakana(prev) && dict::InClass(HDTRANS_CLASS("spelling.variant-after-word"), variant));
}

// Whether r writes a word: a kana, a kanji, another letter or a digit.
bool WordRune(char32_t r) { return text::IsLetter(r) || text::IsDigit(r); }

// The distance from a katakana letter to its hiragana.
constexpr char32_t kKatakanaShift = 0x60;

// Whether r is a katakana letter (ァ..ヶ), not the long mark.
bool KatakanaLetter(char32_t r) { return internal::IsKatakanaRune(r) && internal::IsHiraganaRune(r - kKatakanaShift); }

}  // namespace

std::string RespellKatakanaEndings(std::string_view line) {
    // The katakana run that starts at line[pos] and its letters without the long marks.
    auto runAt = [&](size_t pos, std::string& core) {
        size_t end = pos;
        while (end < line.size()) {
            const text::Rune k = text::DecodeRune(line.substr(end));
            if (!internal::IsKatakanaRune(k.cp)) break;
            if (KatakanaLetter(k.cp)) core.append(line.substr(end, k.size));
            end += k.size;
        }
        return end;
    };
    auto ending = [&](std::string_view core, size_t pos) {
        const char32_t prev = internal::LastRune(line.substr(0, pos)).cp;
        if (dict::InClass(HDTRANS_CLASS("spelling.katakana-copula"), core)) {
            const std::string_view before = line.substr(0, pos);
            const std::span<const std::string_view> kept = dict::ClassMembers(HDTRANS_CLASS("spelling.katakana-copula-kept-after"));
            const std::span<const std::string_view> afterKanji = dict::ClassMembers(HDTRANS_CLASS("spelling.katakana-copula-kept-after-kanji"));
            return std::none_of(kept.begin(), kept.end(), [&](std::string_view k) { return before.ends_with(k); }) &&
                   std::none_of(afterKanji.begin(), afterKanji.end(), [&](std::string_view k) {
                       return before.ends_with(k) && text::IsKanji(internal::LastRune(before.substr(0, before.size() - k.size())).cp);
                   });
        }
        return dict::InClass(HDTRANS_CLASS("spelling.katakana-ending"), core) ||
               (internal::IsHiraganaRune(prev) && dict::InClass(HDTRANS_CLASS("spelling.katakana-ending-after-kana"), core)) ||
               ((internal::IsHiraganaRune(prev) || text::IsKanji(prev)) && dict::InClass(HDTRANS_CLASS("spelling.katakana-particle"), core));
    };
    // A line without hiragana that writes other okurigana or particles in katakana after a
    // kanji (解析シマス。原因ハ) is left whole to the reading of katakana speech.
    if (internal::RunOf(line, [](char32_t c) { return !internal::IsHiraganaRune(c); }) == line.size()) {
        char32_t before = 0;
        for (size_t pos = 0; pos < line.size();) {
            const text::Rune r = text::DecodeRune(line.substr(pos));
            if (KatakanaLetter(r.cp) && text::IsKanji(before)) {
                std::string core;
                const size_t end = runAt(pos, core);
                if (!ending(core, pos)) return std::string(line);
                before = internal::LastRune(line.substr(0, end)).cp;
                pos = end;
                continue;
            }
            before = r.cp;
            pos += r.size;
        }
    }
    std::string out;
    size_t copied = 0, pos = 0;
    char32_t prev = 0;
    while (pos < line.size()) {
        const text::Rune r = text::DecodeRune(line.substr(pos));
        if (!KatakanaLetter(r.cp) || internal::IsKatakanaRune(prev) || !WordRune(prev)) {
            prev = r.cp;
            pos += r.size;
            continue;
        }
        std::string core;
        const size_t end = runAt(pos, core);
        const bool closed = end == line.size() || !WordRune(text::DecodeRune(line.substr(end)).cp);
        if (closed && ending(core, pos)) {
            out.append(line.substr(copied, pos - copied));
            for (size_t k = pos; k < end;) {
                const text::Rune c = text::DecodeRune(line.substr(k));
                text::AppendRune(out, KatakanaLetter(c.cp) ? c.cp - kKatakanaShift : c.cp);
                k += c.size;
            }
            copied = end;
        }
        prev = internal::LastRune(line.substr(0, end)).cp;
        pos = end;
    }
    if (copied == 0) return std::string(line);
    out.append(line.substr(copied));
    return out;
}

std::string RespellKanjiCries(std::string_view line) {
    std::string out;
    size_t copied = 0, pos = 0;
    char32_t prev = 0;
    const std::span<const std::string_view> pairs = dict::ClassMembers(HDTRANS_CLASS("spelling.cry-kanji"));
    auto drawn = [&](size_t at) {
        const text::Rune r = text::DecodeRune(line.substr(at));
        const std::string_view c = line.substr(at, r.size);
        return r.size > 0 && (dict::InClass(HDTRANS_CLASS("kana.drawn-out"), c) || dict::InClass(HDTRANS_CLASS("mark.clip"), c));
    };
    while (pos < line.size()) {
        const text::Rune r = text::DecodeRune(line.substr(pos));
        size_t taken = 0;
        for (const std::string_view pair : WordRune(prev) ? std::span<const std::string_view>() : pairs) {
            const size_t eq = pair.find('=');
            const std::string_view kanji = pair.substr(0, eq), kana = pair.substr(eq + 1);
            if (!line.substr(pos).starts_with(kanji)) continue;
            size_t end = pos + kanji.size();
            int run = 0;
            bool clipped = false;
            while (end < line.size() && drawn(end)) {
                const size_t n = text::DecodeRune(line.substr(end)).size;
                clipped = clipped || dict::InClass(HDTRANS_CLASS("mark.clip"), line.substr(end, n));
                end += n;
                run++;
            }
            const std::string_view after = line.substr(end, text::DecodeRune(line.substr(end)).size);
            if (run == 0 || (!after.empty() && WordRune(text::DecodeRune(after).cp))) continue;
            // One drawn kana before a pause other than an exclamation or a comma reads as the
            // interjection いやぁ (이야), which the kanji is not.
            if (run == 1 && !clipped && !dict::InClass(HDTRANS_CLASS("mark.exclaim"), after) && !dict::InClass(HDTRANS_CLASS("mark.comma"), after)) {
                continue;
            }
            out.append(line.substr(copied, pos - copied));
            out.append(kana);
            taken = kanji.size();
            copied = pos + taken;
            break;
        }
        prev = r.cp;
        pos += taken > 0 ? taken : r.size;
    }
    if (copied == 0) return std::string(line);
    out.append(line.substr(copied));
    return out;
}

std::string RespellVariants(std::string_view line, WordKeepsVariant keeps, const void* context) {
    std::string out;
    size_t copied = 0, pos = 0;
    const std::span<const std::string_view> pairs = dict::ClassMembers(HDTRANS_CLASS("spelling.variant"));
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

std::string RespellOldForms(std::string_view line, WordHoldsRun holds, const void* context) {
    static const auto forms = [] {
        std::unordered_map<char32_t, std::string_view> m;
        for (const std::string_view pair : dict::ClassMembers(HDTRANS_CLASS("spelling.old-form"))) {
            const size_t eq = pair.find('=');
            m.emplace(text::DecodeRune(pair).cp, pair.substr(eq + 1));
        }
        return m;
    }();
    std::string out;
    size_t copied = 0;
    for (size_t pos = 0; pos < line.size();) {
        const text::Rune r = text::DecodeRune(line.substr(pos));
        const size_t end = pos + std::max<size_t>(1, r.size);
        if (const auto it = forms.find(r.cp); it != forms.end() && !holds(context, line, pos, end)) {
            out.append(line.substr(copied, pos - copied));
            out.append(it->second);
            copied = end;
        }
        pos = end;
    }
    if (copied == 0) return std::string(line);
    out.append(line.substr(copied));
    return out;
}

}  // namespace hdTrans::pipeline
