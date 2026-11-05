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


// Whether the hiragana r after a kanji run goes on with the word or marks its case (自己的な, 中登場する,
// 系列中の), not a word in kana of its own after it (這不是ちぃ, 唸作おばさん).
bool Okurigana(char32_t r) {
    return RuneIn(r, U"なにのはがをもへとやでだすしさせるれらかきくけこっう");
}

// Whether the Han run reads as Chinese by its grammar though every character of it is Japanese: it
// holds a sentence particle of Chinese (script.chinese-particle: 嗎, 呢) or a word of Chinese prose
// (script.chinese-word: 這個, 意思是, 讀作); or a word that Japanese also spells inside its own words
// (script.chinese-word-weak: 中登場, 的意思, 不是) with a second sign: another such word, a mark of
// script.chinese-mark outside the words (系列中的), a free edge of the run that the word starts or
// ends, or a run of six characters or more with no okurigana after it (莫非是在故作孤独). A free edge
// is no katakana before a word that starts the run and no okurigana after the run (ちぃ不是人類,
// 不是的 いえ), and after a word that ends it no kana but a hiragana that starts a word of its own
// (我不是ちぃ; not アニメ中登場キャラ, where katakana goes on with the word). A weak word right before
// okurigana (attached) is no sign, and one inside a shorter or attached kanji word is a single sign
// (作中登場人物は, 政治的意思決定の, 系列中最大の are Japanese); word reports one to ChineseRuns. Or the run has four characters or more, no okurigana
// after it (hiragana), and two marks of script.chinese-mark, one of script.chinese-mark-core (的, 了, 是)
// and one of script.chinese-mark-strict (這, 很): 是揶揄在吃火鍋的時候, 日語中 (not 精神的存在,
// 現存在的把握性, 補給完了, 是非御入湯被下候 or 傲骨從來與俗違, which old-style Japanese and kanbun write).
bool ChineseGrammarRun(std::string_view run, bool okurigana, bool attached, bool startFree, bool endFree, bool& word) {
    size_t marks = 0, words = 0, outside = 0, wordEnd = 0;
    bool core = false, strict = false;
    char32_t seen[8] = {};
    for (size_t i = 0; i < run.size();) {
        const std::string_view rest = run.substr(i);
        if (dict::StartsWithMember(HDTRANS_CLASS("script.chinese-word"), rest) > 0) return true;
        const text::Rune r = text::DecodeRune(rest);
        if (dict::InClassRune(HDTRANS_CLASS("script.chinese-particle"), r.cp)) return true;
        if (i >= wordEnd) {
            if (const size_t w = dict::StartsWithMember(HDTRANS_CLASS("script.chinese-word-weak"), rest); w > 0) {
                word = true;
                const bool edge = (i == 0 && startFree && !attached) || (i + w == run.size() && endFree) ||
                                  (!attached && text::RuneCount(run) >= 6);
                if (i + w < run.size() || !attached) words += edge ? 2 : 1;
                wordEnd = i + w;
            } else if (dict::InClassRune(HDTRANS_CLASS("script.chinese-mark"), r.cp)) {
                outside++;
            }
        }
        if (dict::InClassRune(HDTRANS_CLASS("script.chinese-mark"), r.cp) && std::find(seen, seen + marks, r.cp) == seen + marks &&
            marks < std::size(seen)) {
            seen[marks++] = r.cp;
            core = core || dict::InClassRune(HDTRANS_CLASS("script.chinese-mark-core"), r.cp);
            strict = strict || dict::InClassRune(HDTRANS_CLASS("script.chinese-mark-strict"), r.cp);
        }
        i += std::max<size_t>(1, r.size);
    }
    if (words > 0 && words + outside >= 2) return true;
    return !okurigana && text::RuneCount(run) >= 4 && marks >= 2 && core && strict;
}

}  // namespace

std::vector<std::pair<size_t, size_t>> ChineseRuns(std::string_view text) {
    std::vector<std::pair<size_t, size_t>> runs, loose;
    // Runs that hold a word of script.chinese-word-weak that alone is no sign (就是ちぃ, 不是よ):
    // Chinese when another run of the line is.
    std::vector<std::pair<size_t, size_t>> worded;
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
        bool word = false;
        if (!chinese) {
            const char32_t after = text::DecodeRune(text.substr(i + n)).cp;
            chinese = ChineseGrammarRun(text.substr(i, n), IsHiraganaRune(after), Okurigana(after),
                                        !IsKatakanaRune(LastRune(text.substr(0, i)).cp) &&
                                            !(LastRune(text.substr(0, i)).cp >= 0xFF66 && LastRune(text.substr(0, i)).cp <= 0xFF9F),
                                        !IsKanaRune(after) || (IsHiraganaRune(after) && !Okurigana(after)), word);
        }
        if (chinese) {
            runs.push_back({i, i + n});
        } else if (word) {
            worded.push_back({i, i + n});
        } else if (!IsKanaRune(LastRune(text.substr(0, i)).cp) && !IsKanaRune(text::DecodeRune(text.substr(i + n)).cp)) {
            loose.push_back({i, i + n});
        }
        i += n;
    }
    if (runs.empty()) return runs;
    runs.insert(runs.end(), worded.begin(), worded.end());
    runs.insert(runs.end(), loose.begin(), loose.end());
    std::sort(runs.begin(), runs.end());
    return runs;
}

}  // namespace hdTrans::pipeline::internal
