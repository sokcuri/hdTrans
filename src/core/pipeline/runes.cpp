#include "core/pipeline/runes.h"

#include <algorithm>

#include "core/dict/classes.h"
#include "core/text/unicode.h"
#include "core/tokenizer/charsets.h"

namespace hdTrans::pipeline::internal {

text::Rune LastRune(std::string_view s) {
    if (s.empty()) return {text::kReplacement, 0};
    const auto last = static_cast<unsigned char>(s.back());
    if (last < 0x80) return {last, 1};
    // The character starts at the nearest byte before the last one that is not a
    // continuation byte, at most three back.
    const size_t end = s.size();
    const size_t lim = end >= 4 ? end - 4 : 0;
    for (size_t j = end - 1; j-- > lim;) {
        if ((static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) continue;
        const text::Rune r = text::DecodeRune(s.substr(j));
        if (j + r.size != end) break;
        return r;
    }
    return {text::kReplacement, 1};
}

bool ValidUtf8(std::string_view s) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.cp == text::kReplacement && r.size == 1) return false;
        s.remove_prefix(r.size);
    }
    return true;
}

bool RuneIn(char32_t c, std::u32string_view set) { return set.find(c) != std::u32string_view::npos; }

namespace {

bool IsKanaRune(char32_t r) { return IsHiraganaRune(r) || IsKatakanaRune(r) || (r >= 0xFF66 && r <= 0xFF9F); }

// Whether r is written only in Chinese: in script.chinese-han, or outside Japanese and no old
// or variant form.
bool ChineseRune(char32_t r) {
    static const std::vector<char32_t> forms = [] {
        std::vector<char32_t> v;
        for (const std::string_view pair : dict::ClassMembers(HDTRANS_CLASS("spelling.old-form"))) {
            v.push_back(text::DecodeRune(pair).cp);
        }
        std::sort(v.begin(), v.end());
        return v;
    }();
    if (dict::InClassRune(HDTRANS_CLASS("script.chinese-han"), r)) return true;
    return !tokenizer::IsJapaneseHan(r) && !std::binary_search(forms.begin(), forms.end(), r);
}

}  // namespace

std::vector<std::pair<size_t, size_t>> ChineseRuns(std::string_view text) {
    std::vector<std::pair<size_t, size_t>> runs, loose;
    for (size_t i = 0; i < text.size();) {
        const size_t n = RunOf(text.substr(i), text::IsKanji);
        if (n == 0) {
            i += std::max<size_t>(1, text::DecodeRune(text.substr(i)).size);
            continue;
        }
        bool chinese = false;
        for (size_t k = i; k < i + n && !chinese;) {
            const text::Rune r = text::DecodeRune(text.substr(k));
            chinese = ChineseRune(r.cp);
            k += r.size;
        }
        if (chinese) {
            runs.push_back({i, i + n});
        } else if (!IsKanaRune(LastRune(text.substr(0, i)).cp) && !IsKanaRune(text::DecodeRune(text.substr(i + n)).cp)) {
            loose.push_back({i, i + n});
        }
        i += n;
    }
    if (runs.empty() || loose.empty()) return runs;
    runs.insert(runs.end(), loose.begin(), loose.end());
    std::sort(runs.begin(), runs.end());
    return runs;
}

}  // namespace hdTrans::pipeline::internal
