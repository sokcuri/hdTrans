// Writing the composed items into their token texts, the spacing rules, and the output
// of a sentence.
#include <algorithm>

#include "core/dict/char_readings.h"
#include "core/output/cleanup.h"
#include "core/output/compose.h"
#include "core/output/particle.h"
#include "core/output/text.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/tokenizer/canonical.h"
#include "core/tokenizer/classify.h"

namespace hdTrans::output {

namespace {

using internal::At;
using internal::Contains;
using internal::First;
using internal::Selected;
using internal::WidthOf;

constexpr std::string_view kAsciiBlockedSymbols = "`~!@#$%^&*()_+-={}[]:;\"'|\\<>,.?/";
constexpr std::string_view kBlockedSymbolSet = "‘￣！＠＃＄％＾＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・";
constexpr std::string_view kNumeralDigits[] = {"○", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
constexpr std::string_view kNumeralUnits[] = {"十", "百", "千", "万", "億"};

// The symbol set read from inside one of its characters: decoded from each odd byte
// offset of its Shift-JIS form.
constexpr std::string_view kBlockedMisreads[] = {
    "e￣！＠＃＄％＾＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "P！＠＃＄％＾＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "I＠＃＄％＾＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "浴煤吹刀O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "煤吹刀O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "吹刀O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "刀O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "普磨i）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "磨i）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "i）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "j＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "Qー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "[＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "{＝｛「｝」：；”’｜￥＜、＞。？・",
    "＝o「｝」：；”’｜￥＜、＞。？・",
    "o「｝」：；”’｜￥＜、＞。？・",
    "u｝」：；”’｜￥＜、＞。？・",
    "p」：；”’｜￥＜、＞。？・",
    "v：；”’｜￥＜、＞。？・",
    "F；”’｜￥＜、＞。？・",
    "G”’｜￥＜、＞。？・",
    "h’｜￥＜、＞。？・",
    "f｜￥＜、＞。？・",
    "b￥＜、＞。？・",
    "潤メA＞。？・",
    "メA＞。？・",
    "A＞。？・",
    "пB？・",
    "B？・",
    "H・",
    "E",
};

// One-kana particles that close a sentence; one left alone after a pause (静かだ……な)
// is not written.
constexpr std::string_view kFinalParticles = "なねよさわぞぜかの";

std::string_view EntryText(const ResolvedItem& it) { return Selected(it).text; }
std::string_view Code(const ResolvedItem& it) { return Selected(it).code; }
std::string_view GAttr(const ResolvedItem& it) { return Selected(it).gattr; }

int ClassifySelected(const ResolvedItem& it) {
    const std::string_view g = GAttr(it);
    if (Contains(g, "Z6")) return 500;
    if (Contains(g, "Z7")) return 510;
    return 0;
}

char32_t FirstRune(std::string_view s) { return text::DecodeRune(s).cp; }

// Whether an adverb is a mimetic said again right after itself (そぼそぼ そぼそぼ): the
// same surface and Korean, a Korean of two equal halves (부슬부슬).
bool RepeatedMimetic(const ResolvedItem& a, const ResolvedItem& b) {
    const std::string_view ko = EntryText(a);
    const size_t half = ko.size() / 2;
    return First(Code(a)) == '3' && First(Code(b)) == '3' && a.surface == b.surface && ko == EntryText(b) &&
           half > 0 && ko.size() % 2 == 0 && ko.substr(0, half) == ko.substr(half);
}

bool HasRune(std::u32string_view set, char32_t r) { return set.find(r) != std::u32string_view::npos; }

// A character after which a word may start: a space, punctuation other than a closing
// bracket, or a symbol such as ♪.
bool Closes(char32_t r) { return text::IsClosePunctuation(r); }
bool Opens(char32_t r) { return text::IsSpace(r) || (text::IsPunct(r) && !Closes(r)) || text::IsOtherSymbol(r); }

bool IsKanji(char32_t r) {
    return (r >= 0x4E00 && r <= 0x9FFF) || (r >= 0x3400 && r <= 0x4DBF) || (r >= 0xF900 && r <= 0xFAFF) || r == U'々';
}

// Hiragana, katakana and ー.
bool IsKanaRune(char32_t r) { return (r >= U'ぁ' && r <= U'ゖ') || (r >= U'ァ' && r <= U'ヺ') || r == U'ー'; }

// The bytes of the kana that start s.
size_t KanaPrefix(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        if (!IsKanaRune(r.cp)) return i;
        i += r.size;
    }
    return s.size();
}

bool Blank(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        if (!text::IsSpace(r.cp) || (r.cp == text::kReplacement && r.size == 1)) return false;
        i += r.size;
    }
    return true;
}

// An item that no dictionary word reads its text as: its surface is the text, and the
// text is one kana or none of its entries holds Hangul.
bool Unread(const ResolvedItem& it, std::string_view text) {
    if (it.surface != text) return false;
    if (text::RuneCount(text) == 1) return true;
    if (internal::HasSyllable(it.first.text)) return false;
    if (it.selected > 0 && internal::HasSyllable(it.entry.text)) return false;
    return !it.otherHangul;
}

// b-a not counting the opaque span tokens between them.
int TokenDistance(const std::vector<TokenText>& tokens, int a, int b) {
    int d = b - a;
    for (int i = a + 1; i < b; i++) {
        if (tokens[i].span != 0 && tokens[i].type == tokenizer::kTypeMarkup) d--;
    }
    return d;
}

// The token types that are written without a space next to a word span when the input
// has none: ASCII letters and digits and other word spans.
bool KeepsAdjacency(int type) {
    switch (type) {
        case tokenizer::kTypeNumber: case tokenizer::kTypeAlpha: case 15: case 16: case tokenizer::kTypeProtected:
            return true;
    }
    return false;
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool AllDigits(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), IsDigit); }

// s without its last character.
std::string_view DropLastRune(std::string_view s) {
    size_t k = s.size();
    while (k > 0 && (static_cast<unsigned char>(s[k - 1]) & 0xC0) == 0x80) k--;
    return s.substr(0, k > 0 ? k - 1 : 0);
}

// The last character of s, 0 when s is empty.
char32_t LastRune(std::string_view s) { return s.empty() ? 0 : text::DecodeRune(s.substr(DropLastRune(s).size())).cp; }

// A Korean counter counted with native Korean numerals (두 마리, 세 번, 네 장); counters
// counted with Sino-Korean numerals (8체, 3층, 2호) are not listed and keep digits. A Korean
// counter that also counts with Sino-Korean numerals lists the Japanese counters it is
// native for: 2번 for 番 and 3장 for 章 stay digits. Some counters form words with 한 (한번,
// 한잔, 한마디) and are not split from it.
struct NativeCounter {
    std::string_view korean;
    std::u32string_view sources;  // one-character Japanese counters; empty for any
    bool joins = false;
    std::string_view native = {};  // the counter read after a native numeral when it differs (회 번)
};
constexpr NativeCounter kNativeCounters[] = {
    {"마리", U""},          {"개", U""},          {"명", U""},           {"사람", U""},         {"권", U"冊"},
    {"자루", U""},          {"그루", U""},        {"잔", U"", true},     {"대", U"台"},         {"장", U"枚張"},
    {"살", U""},            {"가지", U"", true},  {"군데", U""},         {"채", U"軒棟"},       {"척", U"隻艘"},
    {"벌", U"着"},          {"통", U"通樽", true}, {"켤레", U""},        {"송이", U""},         {"알", U"", true},
    {"병", U"本瓶"},        {"그릇", U""},        {"끼", U"", true},     {"가닥", U"", true},   {"쌍", U""},
    {"곳", U""},            {"마디", U"", true},  {"걸음", U"", true},   {"접시", U""},         {"봉지", U""},
    {"상자", U""},          {"바퀴", U""},        {"발", U"発"},         {"차례", U"", true},   {"번", U"回度遍", true},
    {"번째", U""},          {"개째", U""},        {"명째", U""},         {"마리째", U""},       {"면", U"面"},
    {"덩어리", U""},        {"회", U"回", false, "번"},
};

std::string_view NativeForm(const NativeCounter& c) { return c.native.empty() ? c.korean : c.native; }

const NativeCounter* FindNativeCounter(std::string_view counter, std::string_view korean) {
    const char32_t source = text::DecodeRune(counter).cp;
    for (const NativeCounter& c : kNativeCounters) {
        if (korean == c.korean) return c.sources.empty() || HasRune(c.sources, source) ? &c : nullptr;
    }
    return nullptr;
}

// The words a native count starts with, and the characters of the Japanese numbers and
// question words they translate.
constexpr std::string_view kNativeCounts[] = {"한", "두", "세", "네", "다섯", "여섯", "일곱", "여덟", "아홉", "열", "몇", "여러"};
constexpr std::u32string_view kCountLeads = U"0123456789０１２３４５６７８９一二三四五六七八九十何数幾";

// The native Korean numeral that a count of one to nine written in digits or kanji
// reads before a native counter (二 두, 3 세); 十 reads 열. Empty for any other number.
std::string_view NativeNumeral(std::string_view digits, std::string_view surface) {
    constexpr std::string_view kNative[] = {"한", "두", "세", "네", "다섯", "여섯", "일곱", "여덟", "아홉"};
    if (surface == "十" && digits == "10") return "열";
    if (digits.size() != 1 || digits[0] == '0') return {};
    for (size_t k = 0; k < surface.size();) {
        const auto r = text::DecodeRune(surface.substr(k));
        if (!(r.cp >= U'0' && r.cp <= U'9') && !(r.cp >= U'０' && r.cp <= U'９') &&
            !HasRune(U"一二三四五六七八九", r.cp)) {
            return {};
        }
        k += r.size;
    }
    return kNative[digits[0] - '1'];
}

// Whether counter item k counts an order: the 째 of an ordinal is part of its text or the
// item after it (1番目 1번째, 1回目 1회 + 째, 1匹目 1 + 마리 + 째).
bool Ordinal(const std::vector<ResolvedItem>& items, int k) {
    if (k < 0 || k >= static_cast<int>(items.size())) return false;
    if (EntryText(items[k]).ends_with("째")) return true;
    if (k + 1 >= static_cast<int>(items.size())) return false;
    const ResolvedItem& a = items[k];
    const ResolvedItem& b = items[k + 1];
    return b.tokenIndex == a.tokenIndex && b.offset == a.offset + a.length && EntryText(b).starts_with("째");
}

// A native count and its counter as written (두 마리, 몇 번, 세 번째). One in an ordinal is
// 첫 and is counted with 번 (첫 번째, not 한 번째 or 한 마리째).
std::string WriteNativeCount(std::string_view count, const NativeCounter& c, bool ordinal) {
    std::string_view form = NativeForm(c);
    if (count == "한" && (ordinal || form.ends_with("째"))) {
        return form.ends_with("째") ? "첫 번째" : "첫 번";
    }
    return std::string(count) + ' ' + std::string(form);
}

std::string_view TextTail(const Pieces& p) {
    return !p.empty() && p.back().span == SpanText ? std::string_view(p.back().text) : std::string_view();
}

// A word read as a particle that a noun before it takes without a space (바위뿐, 평소대로,
// 크기밖에, 밭마다, 그만큼), keyed by the Japanese it translates.
bool AttachesToNoun(std::string_view surface, std::string_view korean) {
    struct Particle {
        std::string_view japanese;
        std::string_view korean;
    };
    constexpr Particle kParticles[] = {
        {"ばかり", "뿐"}, {"だけ", "뿐"},   {"のみ", "뿐"},   {"通り", "대로"},   {"どおり", "대로"},
        {"しか", "밖에"}, {"にしか", "밖에"}, {"毎", "마다"}, {"ごと", "마다"},  {"ほど", "만큼"},
        {"くらい", "만큼"}, {"ぐらい", "만큼"},
    };
    for (const Particle& p : kParticles) {
        if (surface.starts_with(p.japanese) && korean.starts_with(p.korean)) return true;
    }
    return false;
}

// Whether the item continues a number or a range of numbers written before it (第2回,
// 1、2個, 2～3本, 百二十).
bool InsideNumber(const std::vector<TokenText>& tokens, const ResolvedItem& item) {
    std::string before(std::string_view(tokens[item.tokenIndex].text).substr(0, item.offset));
    for (int k = item.tokenIndex - 1; k >= 0 && text::RuneCount(before) < 2; k--) before.insert(0, tokens[k].text);
    char32_t r = LastRune(before);
    if (HasRune(U"、，,.~～〜-－・", r)) r = LastRune(DropLastRune(before));
    return r != 0 && (text::IsDigit(r) || HasRune(U"〇○一二三四五六七八九十百千万億第", r));
}

// Whether the counter c of item k is followed in its token by a noun or adverb of one
// kanji (with kana after it) read with its Sino-Korean syllable: they form a compound
// (三人組 3인조, 六人用 6인용, 七回帰 7회귀, 1回当たり 1회당). 分 counts a share of the
// count (네 마리분) except after a counter read differently with native numerals (5回分
// 5회분).
bool CompoundAfter(const std::vector<ResolvedItem>& items, int k, const NativeCounter& c) {
    if (k < 0 || k + 1 >= static_cast<int>(items.size())) return false;
    const ResolvedItem& a = items[k];
    const ResolvedItem& b = items[k + 1];
    const auto r = text::DecodeRune(b.surface);
    const std::string_view rest = std::string_view(b.surface).substr(r.size);
    return b.tokenIndex == a.tokenIndex && b.offset == a.offset + a.length &&
           (First(Code(b)) == '0' || First(Code(b)) == '1' || First(Code(b)) == '3') &&
           (rest.empty() || !IsKanji(text::DecodeRune(rest).cp)) && (r.cp != U'分' || !c.native.empty()) &&
           dict::Written(r.cp) == text::DecodeRune(EntryText(b)).cp;
}

// A number and its counter read as one word (三箇所 3군데, 八本 8 개, ２人 2명): a count
// of one to nine counted natively is written in native Korean apart from the counter
// (세 군데, 여덟 개, 두 명), a larger one in digits joined to it.
std::string CountWithin(const std::vector<TokenText>& tokens, const std::vector<ResolvedItem>& items, int i,
                        std::string t) {
    const ResolvedItem& item = items[i];
    // A native count read as one word with its counter (一匹 한마리, ２人 두명, 何個 몇개)
    // stands apart from it (한 마리, 두 명, 몇 개).
    for (const std::string_view count : kNativeCounts) {
        if (!t.starts_with(count) || t.size() == count.size() || t[count.size()] == ' ') continue;
        const std::string_view surface = item.surface;
        if (!HasRune(kCountLeads, text::DecodeRune(surface).cp)) break;
        size_t numeral = 0;
        while (numeral < surface.size()) {
            const auto r = text::DecodeRune(surface.substr(numeral));
            if (!HasRune(kCountLeads, r.cp)) break;
            numeral += r.size;
        }
        const NativeCounter* c = FindNativeCounter(surface.substr(numeral), std::string_view(t).substr(count.size()));
        if (c != nullptr && !(c->joins && count == "한" && !Ordinal(items, i))) {
            t = WriteNativeCount(count, *c, Ordinal(items, i));
        }
        break;
    }
    size_t digits = 0;
    while (digits < t.size() && IsDigit(t[digits])) digits++;
    if (digits == 0 || digits == t.size()) return t;
    const std::string_view counter = std::string_view(t).substr(t[digits] == ' ' ? digits + 1 : digits);
    std::string_view surface = item.surface;
    size_t numeral = 0;
    while (numeral < surface.size()) {
        const auto r = text::DecodeRune(surface.substr(numeral));
        if (!text::IsDigit(r.cp) && !HasRune(U"〇○一二三四五六七八九十", r.cp)) break;
        numeral += r.size;
    }
    const NativeCounter* c = numeral == 0 ? nullptr : FindNativeCounter(surface.substr(numeral), counter);
    if (c == nullptr || numeral == surface.size()) return t;
    if (InsideNumber(tokens, item) || CompoundAfter(items, i, *c)) return t;
    const std::string_view native = NativeNumeral(std::string_view(t).substr(0, digits), surface.substr(0, numeral));
    if (native.empty()) return t.substr(0, digits) + std::string(counter);
    return WriteNativeCount(native, *c, Ordinal(items, i));
}

// A word of two kanji and 中, 後 or 前 read with their Sino-Korean syllables (作業中 작업중,
// 使用後 사용후) is written with the time noun apart (작업 중, 사용 후), like the noun
// 中, 後 or 前 after a word.
std::string SplitTimeNoun(const ResolvedItem& item, std::string t) {
    std::u32string key, ko;
    for (size_t k = 0; k < item.surface.size();) {
        const auto r = text::DecodeRune(std::string_view(item.surface).substr(k));
        key.push_back(r.cp);
        k += r.size;
    }
    for (size_t k = 0; k < t.size();) {
        const auto r = text::DecodeRune(std::string_view(t).substr(k));
        ko.push_back(r.cp);
        k += r.size;
    }
    if (key.size() != 3 || ko.size() != 3 || !HasRune(U"中後前", key[2])) return t;
    for (size_t k = 1; k < 3; k++) {
        if (dict::Written(key[k]) != ko[k]) return t;
    }
    if (!IsKanji(key[0]) || ko[0] < 0xAC00 || ko[0] > 0xD7A3) return t;
    const size_t last = DropLastRune(t).size();
    return t.substr(0, last) + ' ' + t.substr(last);
}

// Whether item j writes a native count (二匹 두 마리, 三箇所 세 군데, 何個 몇 개, 数 before 頭
// 몇 마리).
bool WritesNativeCount(const std::vector<ResolvedItem>& items, const std::vector<TokenText>& tokens, int j) {
    if (j < 0 || j >= static_cast<int>(items.size())) return false;
    const ResolvedItem& item = items[j];
    const std::string_view t = EntryText(item);
    if (t.empty() || !HasRune(kCountLeads, text::DecodeRune(item.surface).cp)) return false;
    const NativeCounter* c = j + 1 < static_cast<int>(items.size()) &&
                                     TokenDistance(tokens, item.tokenIndex, items[j + 1].tokenIndex) < 2
                                 ? FindNativeCounter(items[j + 1].surface, EntryText(items[j + 1]))
                                 : nullptr;
    const bool beforeCounter = c != nullptr && !CompoundAfter(items, j + 1, *c);
    if (AllDigits(t)) return beforeCounter && !InsideNumber(tokens, item) && !NativeNumeral(t, item.surface).empty();
    if (t == "몇" || t == "여러") return beforeCounter;
    const std::string written = CountWithin(tokens, items, j, std::string(t));
    return std::any_of(std::begin(kNativeCounts), std::end(kNativeCounts), [&](std::string_view count) {
        return written.starts_with(count) && written.size() > count.size() && written[count.size()] == ' ';
    });
}

}  // namespace

bool IsOutputBuilderBlockedText(std::string_view text) {
    if (text.empty()) return false;
    if (kAsciiBlockedSymbols.find(text[0]) != std::string_view::npos) return true;
    if (text == "（" || text == "）" || text == "？" || text == "。" || text == "「" || text == "」") return true;
    if (kBlockedSymbolSet.find(text) != std::string_view::npos) return true;
    for (const auto m : kBlockedMisreads) {
        if (m.starts_with(text)) return true;
    }
    return false;
}

std::string FilterKana(std::string_view text, const Settings& s) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const auto rune = text::DecodeRune(text.substr(i));
        i += rune.size;
        char32_t r = rune.cp;
        if (r < 0x80) {
            out += static_cast<char>(r);
            continue;
        }
        const int type = tokenizer::Class(r);
        if (type == tokenizer::kTypeJapanese && s.hanjaToHangul == 1) {
            if (const auto w = dict::Written(r)) r = *w;
            text::AppendRune(out, r);
        } else if (type == tokenizer::kTypeJapanese ||
                   (type != tokenizer::kClassHiragana && type != tokenizer::kClassKatakana && r != U'、')) {
            text::AppendRune(out, r);
        } else if (s.deleteJapan != 1 || r == U'・' || r == U'、') {
            // The canonical form cut to two width units; a shorter form ends the text.
            std::string canonical;
            if (const auto f = tokenizer::CanonicalForm(r)) {
                canonical = *f;
            } else {
                text::AppendRune(canonical, r);
            }
            const std::string_view form = internal::CutFirst(canonical, 2).head;
            out += form;
            if (WidthOf(form) < 2) return out;
        }
    }
    return out;
}

std::string EncodeNumericPair(std::string_view text) {
    if (static_cast<unsigned char>(First(text)) < 0x80) return std::string(text);
    std::vector<std::string_view> rs;
    for (size_t i = 0; i < text.size();) {
        const size_t n = text::DecodeRune(text.substr(i)).size;
        rs.push_back(text.substr(i, n));
        i += n;
    }
    // The digit value of character i in set; past the end of text it is 0.
    auto position = [&](std::span<const std::string_view> set, size_t i) {
        if (i >= rs.size()) return 0;
        for (size_t k = 0; k < set.size(); k++) {
            if (set[k] == rs[i]) return static_cast<int>(k);
        }
        return -1;
    };
    if (position(kNumeralUnits, 0) != -1) {
        if (const int p = position(kNumeralDigits, 1); p != -1) return std::string{'1', static_cast<char>(p + '0')};
        return "10";
    }
    std::string digits(1, static_cast<char>(std::max(position(kNumeralDigits, 0), 0) + '0'));
    if (rs.size() > 1 && position(kNumeralUnits, 1) != -1) {
        if (const int p = position(kNumeralDigits, 2); p != -1) digits += static_cast<char>(p + '0');
    }
    return digits;
}

void Composer::Start(std::vector<ResolvedItem>& items, const Settings& settings, const Families& generated) {
    items_ = &items;
    settings_ = settings;
    generated_ = &generated;
    state_.resize(items.size());
    for (auto& s : state_) {
        s.text.clear();
        s.saved.clear();
        s.spoken = false;
    }
    written_.clear();
    inflected.clear();
}

void Composer::Compose(std::vector<ResolvedItem>& items, std::vector<TokenText>& tokens, int first, int last,
                       std::span<tokenizer::ControlMarker> markers, const Settings& settings,
                       const Families& generated, Pieces& out) {
    BuildItems(items, tokens, nodes_);
    FinalOutputPrepass(items, nodes_, tokens, first, last, markers, settings, generated);
    EmitSynchronousOutput(tokens, first, last, settings, out);
}

void Composer::FinalOutputPrepass(std::vector<ResolvedItem>& items, const Nodes& nodes,
                                  std::vector<TokenText>& tokens, int first, int last,
                                  std::span<tokenizer::ControlMarker> markers,
                                  const Settings& settings, const Families& generated) {
    Start(items, settings, generated);
    tokens_ = &tokens;
    if (settings.text) {
        if (source_.size() < tokens.size()) source_.resize(tokens.size());
        for (int i = first; i <= last; i++) source_[i] = tokens[i].text;
        if (settings.casual || settings.asked) WriteCasual(first, last);
    }
    for (int n = static_cast<int>(nodes.size()) - 1; n >= 0; n--) {
        const int begin = n > 0 ? nodes[n - 1] : 0;
        for (int index = nodes[n] - 1; index >= begin; index--) WriteItem(index, index == nodes[n] - 1, markers);
    }
}

void Composer::WriteItem(int i, bool isLast, std::span<tokenizer::ControlMarker> markers) {
    std::vector<ResolvedItem>& items = *items_;
    std::vector<TokenText>& tokens = *tokens_;
    const ResolvedItem& item = items[i];
    const int next = i + 1 < static_cast<int>(items.size()) ? i + 1 : -1;
    const int previous = i - 1;
    const std::string tags(GAttr(item));
    const char kind = First(Code(item));
    std::string nextTags;
    char nextKind = ' ';
    if (next >= 0) {
        nextTags = GAttr(items[next]);
        nextKind = First(Code(items[next]));
    }
    const std::string& tokenText = tokens[item.tokenIndex].text;
    const std::string prefix = tokenText.substr(0, std::min<size_t>(static_cast<size_t>(item.offset), tokenText.size()));
    std::string remainder;
    bool converted = false;
    if (const size_t end = static_cast<size_t>(item.offset + item.length); end < tokenText.size()) {
        remainder = tokenText.substr(end);
        if (std::string_view(remainder).starts_with("、")) {
            converted = true;
            remainder = FilterKana(remainder, settings_);
        }
    }
    std::string translated = WriteCount(i, ApplyParenthesisTextFilter(Build(i, 0, {}), i));
    if (settings_.literal) translated = SplitTimeNoun(item, std::move(translated));
    const bool protectedToken = tokens[item.tokenIndex].type == tokenizer::kTypeProtected;
    if (settings_.cleanup && !protectedToken) RemoveGlosses(translated);
    std::string composed;
    if (record) composed = translated;
    auto [spaced, space] = Spacing(i, std::move(translated), tags, nextTags, kind, nextKind, isLast, converted, remainder);
    if (record) inflected.push_back({i, std::move(composed), spaced, space});
    if (protectedToken) {
        spaced = tokenText;
    } else if (settings_.text) {
        spaced = KeepDropped(i, std::move(spaced), remainder);
    }
    if (space) spaced += ' ';
    for (auto& m : markers) {
        if (m.markerKind == 0 || item.tokenIndex != m.tokenIndex) continue;
        const std::string_view marker = m.markerKind == 1 ? "&#CS" : "</a>";
        if (item.offset != m.textLength) {
            if (item.offset <= m.textLength) {
                spaced += marker;
                m.markerKind = 0;
                continue;
            }
            if (previous < 0 || items[previous].offset >= m.textLength) continue;
        }
        spaced.insert(0, marker);
        m.markerKind = 0;
    }
    std::string& left = tokens[item.tokenIndex].text;
    left.clear();
    left += prefix;
    left += spaced;
    left += remainder;
}

std::string Composer::WriteCount(int i, std::string text) const {
    if (!settings_.literal) return text;
    const std::vector<ResolvedItem>& items = *items_;
    const std::vector<TokenText>& tokens = *tokens_;
    text = CountWithin(tokens, items, i, std::move(text));
    const int count = static_cast<int>(items.size());
    if (AllDigits(text) && i + 1 < count && WritesNativeCount(items, tokens, i)) {
        const std::string_view native = NativeNumeral(text, items[i].surface);
        if (!native.empty()) text = native == "한" && Ordinal(items, i + 1) ? "첫" : native;
    } else if (const NativeCounter* c = FindNativeCounter(items[i].surface, text);
               c != nullptr && WritesNativeCount(items, tokens, i - 1)) {
        const std::string written =
            WriteNativeCount(NativeNumeral(EntryText(items[i - 1]), items[i - 1].surface), *c, Ordinal(items, i));
        text = written.substr(written.find(' ') + 1);
    }
    return text;
}

std::string Composer::KeepDropped(int i, std::string translated, std::string_view remainder) {
    const ResolvedItem& item = (*items_)[i];
    const int token = item.tokenIndex, start = item.offset;
    const std::string_view src = source_[token];
    const int end = start + item.length;
    if (end > static_cast<int>(src.size())) return translated;
    const bool blank = Blank(translated);
    const bool empty = blank && Unread(item, src.substr(start, end - start));
    auto find = [&](int pos) -> const Written* {
        for (const Written& w : written_) {
            if (w.token == token && w.start == pos) return &w;
        }
        return nullptr;
    };
    if (Written* w = const_cast<Written*>(find(start))) {
        *w = {token, start, end, blank, empty};
    } else {
        written_.push_back({token, start, end, blank, empty});
    }
    if (!empty || KanaPrefix(src.substr(start, end - start)) != static_cast<size_t>(end - start)) return translated;
    const TextBefore before_(src.substr(0, start));
    const auto beforeRune = DecodeLastRune(before_, start);
    const char32_t before = beforeRune.cp;
    if (start > 0 && !Opens(before)) return translated;
    const int runEnd = end + static_cast<int>(KanaPrefix(src.substr(end)));
    int pos = end;
    bool particle = false;  // the run ends before a written one-kana particle
    while (pos < runEnd) {
        const Written* next = find(pos);
        if (next != nullptr && !next->blank && next->end <= runEnd && settings_.text &&
            text::RuneCount(src.substr(start, pos - start)) >= 2 &&
            text::RuneCount(src.substr(pos, next->end - pos)) == 1) {
            particle = true;
            break;
        }
        if (next == nullptr || (!next->blank && next->end <= runEnd)) return translated;
        if (!next->empty) break;
        pos = next->end;
    }
    const std::string_view run = src.substr(start, pos - start);
    const char32_t after = FirstRune(src.substr(pos));
    bool kanjiBefore = false;
    if (start > 0) {
        const std::string_view head = src.substr(0, start - beforeRune.size);
        kanjiBefore = IsKanji(DecodeLastRune(TextBefore(head), head.size()).cp);
    }
    const bool alone = settings_.text && pos == runEnd &&
                       (pos == static_cast<int>(src.size()) || Closes(after) || HasRune(U"…‥！？!?", after)) &&
                       kFinalParticles.find(run) == std::string_view::npos;
    bool onlySmall = true;  // run is only っ, ッ and ー
    for (size_t k = 0; k < run.size();) {
        const auto r = text::DecodeRune(run.substr(k));
        if (r.cp != U'っ' && r.cp != U'ッ' && r.cp != U'ー') onlySmall = false;
        k += r.size;
    }
    if ((text::RuneCount(run) < 2 && !alone) || onlySmall ||
        ((run.ends_with("っ") || run.ends_with("ッ")) && pos == runEnd && pos < static_cast<int>(src.size()) &&
         !Opens(after) && !Closes(after)) ||
        (kanjiBefore && HasRune(U"(（《「", before) && HasRune(U")）》」", after))) {
        return translated;
    }
    std::string marked;
    for (size_t k = 0; k < run.size();) {
        const auto r = text::DecodeRune(run.substr(k));
        text::AppendRune(marked, r.cp + kDropShift);
        k += r.size;
    }
    if (const char32_t r = FirstRune(remainder); !particle && !remainder.empty() && (text::IsLetter(r) || text::IsDigit(r))) {
        marked += ' ';
    }
    while (!translated.empty() && translated.back() == ' ') translated.pop_back();
    translated += marked;
    return translated;
}

std::pair<std::string, bool> Composer::Spacing(int i, std::string t, std::string_view tags,
                                               std::string_view nextTags, char kind, char nextKind, bool isLast,
                                               bool converted, std::string_view remainder) const {
    const std::vector<ResolvedItem>& items = *items_;
    const std::vector<TokenText>& tokens = *tokens_;
    const ResolvedItem& item = items[i];
    const int count = static_cast<int>(items.size());
    int nextIndex = i + 1;
    if (kind == '3') {
        // Omitted trailing particles do not erase an adverb's word boundary.
        while (nextIndex < count && items[nextIndex].tokenIndex == item.tokenIndex &&
               First(Code(items[nextIndex])) == '2' && EntryText(items[nextIndex]).empty()) {
            nextIndex++;
        }
        if (nextIndex != i + 1) {
            isLast = true;
            if (nextIndex < count) {
                nextTags = GAttr(items[nextIndex]);
                nextKind = First(Code(items[nextIndex]));
            }
        }
    }
    if (nextIndex == i + 1) {
        // An honorific the rules left untranslated (魔王ちゃんの 마왕의) leaves the spacing to
        // the word after it.
        constexpr std::string_view kHonorifics[] = {"ちゃん", "さん", "くん", "君", "たん", "様", "さま", "殿", "氏"};
        // So does a verb whose stem and ending the rules emptied (行こうと思って 가려고).
        while (nextIndex < count && EntryText(items[nextIndex]).empty() &&
               (std::find(std::begin(kHonorifics), std::end(kHonorifics), items[nextIndex].surface) !=
                    std::end(kHonorifics) ||
                (items[nextIndex].detailClass == DetailClass::Yougo && items[nextIndex].directIndex >= 0 &&
                 items[nextIndex].ending.text.empty()))) {
            nextIndex++;
        }
        if (nextIndex != i + 1 && nextIndex < count) {
            nextTags = GAttr(items[nextIndex]);
            nextKind = First(Code(items[nextIndex]));
        }
    }
    if (nextIndex == count) nextIndex = -1;
    const int previous = i - 1;
    // A place or time noun after a の that the rules left untranslated starts a word
    // (가게 안, 침대 아래, 아이 때).
    constexpr std::string_view kPlaceNouns[] = {"上", "中", "下", "時", "前", "後", "外", "奥", "横", "隣",
                                                "裏", "内", "間", "際", "頃", "底", "側", "周り"};
    if (!t.empty() && previous >= 0 && EntryText(items[previous]).empty() &&
        (item.surface == "上" ||
         (kind == '1' && items[previous].surface == "の" && !Contains(tags, "Ct") &&
          std::find(std::begin(kPlaceNouns), std::end(kPlaceNouns), item.surface) != std::end(kPlaceNouns)))) {
        t.insert(0, " ");
    }
    // みたい read as the adjective 같다 is written apart from the word before it; the
    // particle 같이 stays attached (너 같은 놈, 아이같이).
    if (item.surface == "みたい" && previous >= 0 && !EntryText(items[previous]).empty() &&
        t.starts_with("같") && !t.starts_with("같이")) {
        t.insert(0, " ");
    }
    using Result = std::pair<std::string, bool>;
    if (nextIndex < 0 || t.empty() || item.selectionFlag == 1 ||
        TokenDistance(tokens, item.tokenIndex, items[nextIndex].tokenIndex) >= 2) {
        return Result(std::move(t), false);
    }
    const ResolvedItem& next = items[nextIndex];
    if (const int a = tokens[item.tokenIndex].type, b = tokens[next.tokenIndex].type;
        (a == tokenizer::kTypeProtected || b == tokenizer::kTypeProtected) && KeepsAdjacency(a) && KeepsAdjacency(b)) {
        return Result(std::move(t), false);
    }
    // The word before a native count stands apart from it (개 두 마리, not 개두 마리), also
    // across items that write nothing.
    for (int j = settings_.literal ? nextIndex : count; j < count && TokenDistance(tokens, item.tokenIndex, items[j].tokenIndex) < 2; j++) {
        if (WritesNativeCount(items, tokens, j)) {
            if (!t.ends_with(' ')) return Result(std::move(t), true);
            break;
        }
        if (!EntryText(items[j]).empty()) break;
    }
    // A count before a native counter: one to nine, written in native Korean by WriteCount,
    // and 몇 or 여러 stand apart from the counter (二匹 두 마리, 3回 세 번, 何匹 몇 마리), a
    // larger number in digits joins it (20마리). A number inside a longer one or a range
    // (第2回, 1、2個, 2～3本) or before a compound stays as it is.
    if (settings_.literal && FindNativeCounter(next.surface, EntryText(next)) != nullptr) {
        if (t.find(' ') == std::string::npos && WritesNativeCount(items, tokens, i)) return Result(std::move(t), true);
        if (AllDigits(t)) return Result(std::move(t), false);
    }
    // 몇 and 여러 stand apart from any counter (몇 체, 몇 층).
    if (settings_.literal && (t == "몇" || t == "여러") && Contains(nextTags, "Ct")) return Result(std::move(t), true);
    // A number word in Hangul stands apart from the counter after it (수십 명, 몇백 년).
    if (constexpr std::string_view kNumberWords[] = {"수십", "수백", "수천", "수만", "몇십", "몇백", "몇천", "몇만"};
        settings_.literal && Contains(nextTags, "Ct") &&
        std::find(std::begin(kNumberWords), std::end(kNumberWords), t) != std::end(kNumberWords)) {
        return Result(std::move(t), true);
    }
    auto nd = [](std::string_view s) { return Contains(s, "Nd") || Contains(s, "Nj"); };
    bool allowSpacing = true;
    if (t == "\\") {
        if (previous >= 0 && nd(internal::EntryAt(items[previous], 0).gattr)) {
            t = "Y";
            allowSpacing = false;
        }
        if (t == "\\" && nd(nextTags)) return Result("Y", false);
        if (!allowSpacing) return Result(std::move(t), false);
    }
    const std::string_view nextEntry = EntryText(next);
    if (RepeatedMimetic(item, next)) return Result(std::move(t), false);  // 부슬부슬부슬부슬
    if (nextEntry == "「") return Result(std::move(t), true);
    if (!allowSpacing || nextEntry == "%") return Result(std::move(t), false);
    // 씨 and 군 after a name are dependent nouns, written apart (카케이 씨, 타나카 군).
    if (kind == '1' && (nextEntry == "씨" || nextEntry == "군")) {
        constexpr std::string_view kSpacedHonorifics[] = {"さん", "くん", "君", "氏"};
        if (std::find(std::begin(kSpacedHonorifics), std::end(kSpacedHonorifics), next.surface) !=
            std::end(kSpacedHonorifics)) {
            return Result(std::move(t), true);
        }
    }
    // A noun of degree, time, listing or span read as a free noun after a word stands apart
    // (50배 이상, 4시 무렵, 세 명 정도, 7년 전, 안젤리카 등, 회의 중); 等 after a number is a rank (1등).
    struct FreeNoun {
        std::string_view surface;
        std::string_view korean;
        bool literal = false;  // only with settings.literal
    };
    constexpr FreeNoun kFreeNouns[] = {
        {"以上", "이상"}, {"以下", "이하"}, {"未満", "미만"}, {"頃", "무렵"}, {"ほど", "정도"},
        {"程", "정도"},   {"等", "등"},     {"など", "등"},   {"前", "전"},   {"後", "후"},
        {"前", "앞"},     {"後", "뒤"},     {"中", "중"},     {"くらい", "정도", true}, {"ぐらい", "정도", true},
        {"など", "같은", true},
    };
    if (settings_.literal && kind == '1' && AttachesToNoun(next.surface, nextEntry)) return Result(std::move(t), false);
    // A direction noun and 쪽 make one word (위쪽, 앞쪽, 오른쪽); 쪽 after other nouns stands
    // apart (목소리 쪽).
    if (constexpr std::string_view kDirections[] = {"위", "아래", "앞", "뒤", "옆", "안", "밖", "바깥", "오른", "왼",
                                                    "동", "서", "남", "북", "이", "그", "저"};
        settings_.literal && kind == '1' && nextEntry == "쪽" &&
        std::find(std::begin(kDirections), std::end(kDirections), t) != std::end(kDirections)) {
        return Result(std::move(t), false);
    }
    if (kind == '1' && !t.ends_with(' ') && Contains(GAttr(next), "Tu") == false) {
        for (const FreeNoun& f : kFreeNouns) {
            if (f.literal && !settings_.literal) continue;
            const std::string_view ns = next.surface;
            const bool same = ns == f.surface || (ns.starts_with(f.surface) && ns.substr(f.surface.size()) == "の");
            if (!same || !nextEntry.starts_with(f.korean)) continue;
            if (f.korean == "등" && (IsDigit(First(t)) || nd(tags))) break;
            return Result(std::move(t), true);
        }
    }
    // A number joins a noun after it that starts with a counter read in Sino-Korean (2000년
    // 후, 14일째), and an ordinal after 第 joins the noun it numbers (제17구축대, 제1함대).
    if (settings_.literal && AllDigits(t) && nextKind == '1') {
        constexpr std::u32string_view kCounters = U"年日月週時分秒歳才回度個本匹頭枚冊台件名倍階号番章話巻隻杯体人";
        const char32_t lead = text::DecodeRune(next.surface).cp;
        const bool ordinal = previous >= 0 && items[previous].surface == "第";
        if (IsKanji(lead) && (ordinal || HasRune(kCounters, lead)) &&
            dict::Written(lead) == text::DecodeRune(EntryText(next)).cp) {
            return Result(std::move(t), false);
        }
    }
    if (!Contains(nextTags, "Tu")) {
        if (Contains(nextTags, "Ha")) return Result(std::move(t), true);
    } else if (t != ",") {
        return Result(std::move(t), false);
    }
    if (Contains(tags, "Wa") || Contains(tags, "Wb")) {
        if ((Contains(nextTags, "Wa") || Contains(nextTags, "Wb")) && !Contains(nextTags, "Z7")) {
            return Result(std::move(t), true);
        }
        if (nextKind == '1' && WidthOf(nextEntry) > 2) return Result(std::move(t), true);
    }
    if (kind == '1' && At(Code(item), 1) == '3' && nextKind == '1' && At(Code(next), 1) == '3') {
        return Result(std::move(t), true);
    }
    if (Contains(tags, "Z7") && nextKind == '1') {
        if (WidthOf(nextEntry) > 2) return Result(std::move(t), true);
        for (const std::string_view p : {"お", "ご", "御"}) {
            if (next.surface == p) return Result(std::move(t), true);
        }
    }
    if (Contains(nextEntry, "」") || (!nextEntry.empty() && Closes(FirstRune(nextEntry)))) {
        return Result(std::move(t), false);
    }
    if (nextKind != '2') {
        if (Contains(tags, "Bt")) return Result(std::move(t), false);
        if (Contains(tags, "Bh")) return Result(std::move(t), true);
    }
    if (kind == '1' && nextKind == '1' && Contains(tags, "Tm")) return Result(std::move(t), true);
    if (!isLast) {
        if (kind != '1' || WidthOf(t) != 2) return Result(std::move(t), false);
        if (nextKind != '1' || WidthOf(nextEntry) <= 2) return Result(std::move(t), false);
        const bool space = Contains(tags, "Z7") || !Contains(tags, "Z6");
        return Result(std::move(t), space);
    }
    switch (kind) {
        case '0': case '2':
            return Result(std::move(t), nextKind != '2' && nextKind != '6');
        case '1': {
            const TokenText& tok = tokens[item.tokenIndex];
            if (t == "(" || tok.span != 0 || WidthOf(tok.text) == 1 || nextKind == '2') return Result(std::move(t), false);
            if (nextKind == '6') return Result(std::move(t), nd(nextTags));
            const std::string_view nextFirst = internal::EntryAt(next, 0).text;
            if (nextFirst == "(") break;
            // A verbal noun (Hd, a kanji compound or a katakana word) joins the する or できる after it
            // read with 하다 or 되다 (即答する 즉답하다, キスしたい 키스하고 싶다, 我慢できない
            // 자제할 수 없다).
            constexpr std::string_view kSuru[] = {"する", "し", "さ", "せ", "すれ", "しろ", "しよ"};
            constexpr std::string_view kSuruStarts[] = {"し", "す", "さ", "せ", "でき"};
            constexpr std::string_view kDoing[] = {"하", "할", "한", "해", "했", "함", "되", "될", "된", "됐", "돼"};
            // A compound of two or more kanji before する is a verbal noun (即答する 즉답하다),
            // unless it tells a time, a frequency or an amount, which stays an adverb apart from
            // the verb (毎日する 매일 한다, 全部します 전부 합니다).
            auto kanjiCompound = [](std::string_view s) {
                static constexpr std::u32string_view kTimeStarts = U"毎今明昨何半";
                static constexpr std::u32string_view kTimeEnds = U"日朝晩週年月夜時頃";
                static constexpr std::string_view kAdverbNouns[] = {
                    "全部", "全員", "全然", "一緒", "本当", "普通", "大体", "結構", "多少", "十分", "一番",
                    "一度", "二度", "三度", "最初", "最後", "永遠", "同時", "直接", "自分", "先程", "以前",
                };
                if (std::find(std::begin(kAdverbNouns), std::end(kAdverbNouns), s) != std::end(kAdverbNouns)) return false;
                int n = 0;
                char32_t first = 0, last = 0;
                for (std::size_t k = 0; k < s.size(); n++) {
                    const text::Rune r = text::DecodeRune(s.substr(k));
                    if (r.size == 0 || !text::IsKanji(r.cp)) return false;
                    if (n == 0) first = r.cp;
                    last = r.cp;
                    k += r.size;
                }
                return n >= 2 && kTimeStarts.find(first) == std::u32string_view::npos &&
                       kTimeEnds.find(last) == std::u32string_view::npos;
            };
            // A noun made of a verb stem (長生き, 仲直り, お祝い) or お, ご and a kanji compound
            // (お約束, ご案内) before する is a verbal noun too (장수하다, 화해하다, 약속할 수);
            // お and one kanji (お茶) or a noun ending in a kanji (焼き芋) is not.
            auto stemOrHonorific = [](std::string_view s) {
                std::u32string rs;
                for (std::size_t k = 0; k < s.size();) {
                    const text::Rune r = text::DecodeRune(s.substr(k));
                    rs.push_back(r.cp);
                    k += r.size;
                }
                std::u32string_view body = rs;
                if (!body.empty() && (body[0] == U'お' || body[0] == U'ご')) {
                    body.remove_prefix(1);
                    if (body.size() >= 2 && std::all_of(body.begin(), body.end(), IsKanji)) return true;
                }
                return body.size() >= 2 && IsKanji(body[0]) && HasRune(U"いきぎしちひびみりえけげせてでねべめれ", body.back()) &&
                       std::none_of(body.begin(), body.end(), [](char32_t r) { return r == U'な' || r == U'の'; });
            };
            const bool verbalNoun = Contains(tags, "Hd") ||
                                    tokenizer::Class(FirstRune(item.surface)) == tokenizer::kClassKatakana ||
                                    kanjiCompound(item.surface) || (settings_.literal && stemOrHonorific(item.surface));
            auto startsWithAny = [](std::string_view s, std::span<const std::string_view> list) {
                return std::any_of(list.begin(), list.end(), [&](std::string_view p) { return s.starts_with(p); });
            };
            if (verbalNoun && nextEntry == "&AV@" &&
                std::find(std::begin(kSuru), std::end(kSuru), next.surface) != std::end(kSuru)) {
                return Result(std::move(t), false);
            }
            if (verbalNoun && startsWithAny(next.surface, kSuruStarts) && startsWithAny(nextEntry, kDoing)) {
                return Result(std::move(t), false);
            }
            if (nextFirst == ")") return Result(std::move(t), false);
            if (Contains(tags, "Wa") || Contains(tags, "Wb")) {
                const bool space = Contains(tags, "Wb") && nextKind != '1';
                return Result(std::move(t), space);
            }
            if (IsDigit(First(t))) {
                if (nextKind == '1') return Result(std::move(t), !Contains(nextTags, "Ct"));
                if (item.tokenIndex - 1 >= 0 && previous >= 0 && tokens[next.tokenIndex].type != 15 &&
                    tokens[next.tokenIndex].type != tokenizer::kTypeNumber &&
                    item.tokenIndex - items[previous].tokenIndex == 1) {
                    t.insert(0, " ");
                }
                // A number is written apart from a following one-word predicate (12 올랐다,
                // 0600 같다); a counter (3개, 10엔인) and a predicate that starts with the noun it
                // counts (3시간 들였다) stay attached.
                const bool predicate = nextKind == '4' || (nextKind == '7' && next.surface != "円");
                return Result(std::move(t), predicate && nextEntry.find(' ') == std::string_view::npos);
            }
            if (nd(nextTags)) return Result(std::move(t), true);
            if (WidthOf(t) == 2 && previous >= 0) {
                if ((nd(GAttr(items[previous])) && Contains(tags, "Ct")) || nd(tags)) return Result(std::move(t), true);
            }
            if (tokenizer::Class(FirstRune(item.surface)) == tokenizer::kClassKatakana) return Result(std::move(t), true);
            // A pronoun forms no compound with the noun after it (私無し 나 없이, not 나없음).
            if (Code(item).starts_with("12") && nextKind == '1') return Result(std::move(t), true);
            const bool spaced = !converted && (WidthOf(t) > 2 || nextKind != kind);
            if (Contains(nextEntry, "&AV@") || Contains(nextEntry, "&C6@") || Contains(nextEntry, "%A1@") ||
                Contains(nextEntry, "%D5@") || Contains(nextEntry, "$AB@") || Contains(nextEntry, "$AE@")) {
                return Result(std::move(t), spaced);
            }
            if (ClassifySelected(item) == 510) {
                t += ' ';
                return Result(std::move(t), false);
            }
            return Result(std::move(t), spaced);
        }
        case '3': case '4': case '5': case '7': case '8': case '9': {
            // A predicate that writes nothing (an auxiliary いる folded into the verb before
            // it) leaves the spacing to the word after it.
            int after = nextIndex;
            while (kind == '4' && First(Code(items[after])) == '4' && EntryText(items[after]).empty() &&
                   (items[after].directIndex < 0 || items[after].ending.text.empty()) && after + 1 < count &&
                   items[after + 1].tokenIndex == items[after].tokenIndex) {
                after++;
            }
            const char np = First(Code(items[after]));
            const bool space = np != '2' && np != '6' && (!IsOutputBuilderBlockedText(remainder) || WidthOf(remainder) > 4);
            return Result(std::move(t), space);
        }
        case '6':
            return Result(std::move(t), t == "," || (t == ")" && nextKind != '2' && nextKind != '6'));
    }
    return Result(std::move(t), false);
}

void EmitSynchronousOutput(const std::vector<TokenText>& tokens, int first, int last, const Settings& s, Pieces& out) {
    out.clear();
    std::string text;
    for (int i = first; i <= last; i++) {
        const TokenText& tok = tokens[i];
        switch (tok.type) {
            case tokenizer::kTypeNumber: case tokenizer::kTypeAlpha: case 15: case 16: case tokenizer::kTypeSymbol:
            case tokenizer::kTypeJapanese: case tokenizer::kTypeProtected:
                break;
            default:
                if (tok.span == 0 && !out.empty() && i - 1 >= 0 && tokens[i - 1].type != tokenizer::kTypeAsciiSpace) {
                    TrimRight(out);
                }
        }
        if (const std::string_view t = TextTail(out); t.ends_with("</A>") || t.ends_with("</a>") || t.ends_with("&#CS")) {
            Add(out, " ");
        }
        if (tok.span != 0) {
            std::string extra = tok.text.substr(std::min(tok.spanText.size(), tok.text.size()));
            if (const std::string_view t = TextTail(out); tok.span == SpanClose && i > first &&
                                                         tokens[i - 1].type != tokenizer::kTypeAsciiSpace &&
                                                         t.ends_with(" ")) {
                std::string head(t.substr(0, t.size() - 1));
                out.pop_back();
                Add(out, head);
                extra += ' ';
            }
            out.push_back({tok.spanText, static_cast<SpanKind>(tok.span)});
            Add(out, extra);
            continue;
        }
        text = tok.text;
        if (s.cleanup) {
            if (tok.type == tokenizer::kTypeAsciiSpace) {
                TrimRight(out);
            } else if (tok.type != tokenizer::kTypeMarkup) {
                SqueezeSpaces(text);
                if (TextTail(out).ends_with(" ")) text.erase(0, std::min(text.find_first_not_of(' '), text.size()));
            }
        }
        Add(out, text);
    }
    ApplyOutputPostSpacing(tokens, last, out);
}

void ApplyOutputPostSpacing(const std::vector<TokenText>& tokens, int index, Pieces& out) {
    const TokenText& tok = tokens[index];
    if (tok.type == tokenizer::kTypeWideSpace || tok.type == 78 || tok.span != 0 ||
        tok.text.empty() || tok.text.find_first_not_of(".!?") != std::string::npos ||
        (tok.text.size() > 1 && tok.text.find('.') != std::string::npos)) {
        return;
    }
    const size_t next = static_cast<size_t>(index) + 1;
    if (next >= tokens.size()) return;
    switch (tokens[next].type) {
        case tokenizer::kTypeAsciiPunct: case tokenizer::kTypeNewline: case tokenizer::kTypeEnd:
        case tokenizer::kTypeSentenceEnd: case tokenizer::kTypeTruncated: case tokenizer::kTypeWideSpace: case 78:
            return;
    }
    if (tokens[next].span != 0 || !std::string_view(tokens[next].text).starts_with("」")) Add(out, " ");
}

}  // namespace hdTrans::output
