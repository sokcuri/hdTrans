#include "core/pipeline/runes.h"

#include <algorithm>
#include <cstdint>

#include "core/dict/classes.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
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
// or variant form. Computed once per character of U+3400..U+FAFF into a bitset.
bool ComputeChineseRune(char32_t r) {
    if (dict::InClassRune(HDTRANS_CLASS("script.chinese-han"), r)) return true;
    if (tokenizer::IsJapaneseHan(r)) return false;
    for (const std::string_view pair : dict::ClassMembers(HDTRANS_CLASS("spelling.old-form"))) {
        if (text::DecodeRune(pair).cp == r) return false;
    }
    return true;
}

constexpr char32_t kBitsFirst = 0x3400, kBitsEnd = 0xFB00;

bool ChineseRune(char32_t r) {
    static const std::vector<uint64_t> bits = [] {
        std::vector<uint64_t> v((kBitsEnd - kBitsFirst + 63) / 64);
        std::vector<char32_t> forms;
        for (const std::string_view pair : dict::ClassMembers(HDTRANS_CLASS("spelling.old-form"))) {
            forms.push_back(text::DecodeRune(pair).cp);
        }
        std::sort(forms.begin(), forms.end());
        for (char32_t c = kBitsFirst; c < kBitsEnd; c++) {
            const bool chinese = dict::InClassRune(HDTRANS_CLASS("script.chinese-han"), c) ||
                                 (text::IsKanji(c) && !tokenizer::IsJapaneseHan(c) &&
                                  !std::binary_search(forms.begin(), forms.end(), c));
            if (chinese) v[(c - kBitsFirst) / 64] |= uint64_t{1} << ((c - kBitsFirst) % 64);
        }
        return v;
    }();
    if (r < kBitsFirst) return false;
    if (r < kBitsEnd) return (bits[(r - kBitsFirst) / 64] >> ((r - kBitsFirst) % 64) & 1) != 0;
    return text::IsKanji(r) && ComputeChineseRune(r);
}


// Whether the Han run reads as Chinese by its grammar though every character of it is Japanese: it
// holds a sentence particle of Chinese (script.chinese-particle: 嗎, 呢) or a word of Chinese prose
// (script.chinese-word: 這個, 的意思, 讀作), or it has four characters or more, no okurigana after it
// (hiragana), and two marks of script.chinese-mark, one of script.chinese-mark-core (的, 了, 是) and
// one of script.chinese-mark-strict (這, 很): 是揶揄在吃火鍋的時候, 日語中 (not 精神的存在, 現存在的把握性,
// 補給完了, 是非御入湯被下候 or 傲骨從來與俗違, which old-style Japanese and kanbun write).
bool ChineseGrammarRun(std::string_view run, bool okurigana) {
    size_t marks = 0;
    bool core = false, strict = false;
    char32_t seen[8] = {};
    for (size_t i = 0; i < run.size();) {
        const std::string_view rest = run.substr(i);
        if (dict::StartsWithMember(HDTRANS_CLASS("script.chinese-word"), rest) > 0) return true;
        const text::Rune r = text::DecodeRune(rest);
        if (dict::InClassRune(HDTRANS_CLASS("script.chinese-particle"), r.cp)) return true;
        if (dict::InClassRune(HDTRANS_CLASS("script.chinese-mark"), r.cp) && std::find(seen, seen + marks, r.cp) == seen + marks &&
            marks < std::size(seen)) {
            seen[marks++] = r.cp;
            core = core || dict::InClassRune(HDTRANS_CLASS("script.chinese-mark-core"), r.cp);
            strict = strict || dict::InClassRune(HDTRANS_CLASS("script.chinese-mark-strict"), r.cp);
        }
        i += std::max<size_t>(1, r.size);
    }
    return !okurigana && text::RuneCount(run) >= 4 && marks >= 2 && core && strict;
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
        if (!chinese) chinese = ChineseGrammarRun(text.substr(i, n), IsHiraganaRune(text::DecodeRune(text.substr(i + n)).cp));
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
