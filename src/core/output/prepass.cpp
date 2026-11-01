// Writing the composed items into their token texts, the spacing rules, and the output
// of a sentence.
#include <algorithm>

#include "core/dict/char_readings.h"
#include "core/dict/classes.h"
#include "core/output/cleanup.h"
#include "core/output/compose.h"
#include "core/output/inflect.h"
#include "core/output/particle.h"
#include "core/output/text.h"
#include "core/text/hangul.h"
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

// Whether a word without an ending is said again right after itself as one syllable or that
// syllable repeated (はいはい|はい 네네|네, うん|うん 응|응): both sources repeat one unit of two
// characters or more (not a drawn-out あ) and both Koreans one syllable.
bool RepeatedSyllable(const ResolvedItem& a, const ResolvedItem& b) {
    if (a.directIndex >= 0 || b.directIndex >= 0 || a.tokenIndex != b.tokenIndex || a.offset + a.length != b.offset) {
        return false;
    }
    const auto repeats = [](std::string_view s, std::string_view unit) {
        if (s.empty() || unit.empty() || s.size() % unit.size() != 0) return false;
        for (std::size_t at = 0; at < s.size(); at += unit.size()) {
            if (s.substr(at, unit.size()) != unit) return false;
        }
        return true;
    };
    // The shortest unit that s repeats.
    const auto unitOf = [&](std::string_view s) {
        for (std::size_t n = 0; n < s.size();) {
            n += std::max<std::size_t>(1, text::DecodeRune(s.substr(n)).size);
            if (repeats(s, s.substr(0, n))) return s.substr(0, n);
        }
        return s;
    };
    const std::string_view ko = EntryText(b), source = unitOf(b.surface);
    const text::Rune r = text::DecodeRune(ko);
    if (ko.empty() || r.cp < text::kBase || r.cp > text::kLast || text::RuneCount(source) < 2) return false;
    const std::string_view syllable = ko.substr(0, r.size);
    return repeats(ko, syllable) && repeats(EntryText(a), syllable) && repeats(a.surface, source);
}

// Whether item i and item next are written as one: copies of one syllable (RepeatedSyllable)
// after a word the dictionary already writes doubled (はいはい|はい 네네네, はいはい|はいはい|はい
// 네네네네네; not ほら|ほら 자 자).
bool JoinedCopies(const std::vector<ResolvedItem>& items, int i, int next) {
    if (i < 0 || next != i + 1 || next >= static_cast<int>(items.size()) || !RepeatedSyllable(items[i], items[next])) {
        return false;
    }
    return text::RuneCount(EntryText(items[i])) > 1 || JoinedCopies(items, i - 1, i);
}

// t with the space dropped between a word of ko.joined-before-doe that starts t or follows a
// space and a form of 되다 after it (안 돼 안돼, 안 된다 안된다; LAY-11).
void JoinBeforeDoe(std::string& t) {
    for (const std::string_view w : dict::ClassMembers(HDTRANS_CLASS("ko.joined-before-doe"))) {
        for (std::size_t at = t.find(w); at != std::string::npos; at = t.find(w, at + 1)) {
            const std::size_t space = at + w.size();
            if ((at > 0 && t[at - 1] != ' ') || space >= t.size() || t[space] != ' ') continue;
            const auto doe = text::Split(text::DecodeRune(std::string_view(t).substr(space + 1)).cp);
            if (doe && doe->initial == 3 && (doe->vowel == 10 || doe->vowel == 11)) t.erase(space, 1);  // ㄷ, ㅙ ㅚ
        }
    }
}

// Whether s is two equal halves of at least min characters each (ふわふわ, 둥실둥실).
bool Doubled(std::string_view s, size_t min) {
    const size_t half = s.size() / 2;
    return s.size() % 2 == 0 && text::RuneCount(s.substr(0, half)) >= min && s.substr(0, half) == s.substr(half);
}

// Whether an adverb is a doubled mimetic read as a doubled Korean mimetic before a form of
// する read with 하다, which Korean writes as one verb (두근두근했다, 부들부들하고).
bool MimeticBeforeSuru(const ResolvedItem& a, const ResolvedItem& b, std::string_view nextEntry) {
    if (First(Code(a)) != '3' || !Doubled(a.surface, 2) || !Doubled(EntryText(a), 2)) return false;
    bool suru = false;
    for (const std::string_view k : {"し", "す", "さ", "せ"}) suru = suru || b.surface.starts_with(k);
    if (!suru) return false;
    if (nextEntry == "&AV@") return true;
    for (const std::string_view k : {"하", "해", "했", "한", "할", "함", "합"}) {
        if (nextEntry.starts_with(k)) return true;
    }
    return false;
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

// Whether item i ends its Japanese token and a run of symbols comes next (、 before ％＆＄,
// ……, ※).
bool SymbolsFollow(const std::vector<TokenText>& tokens, const std::vector<ResolvedItem>& items, int i) {
    const int t = items[i].tokenIndex;
    if (tokens[t].type != tokenizer::kTypeJapanese) return false;
    if (i + 1 < static_cast<int>(items.size()) && items[i + 1].tokenIndex == t) return false;
    if (t + 1 >= static_cast<int>(tokens.size())) return false;
    const TokenText& next = tokens[t + 1];
    if (next.type != tokenizer::kTypeSymbol || next.span != 0 || next.text.empty()) return false;
    // Closing brackets and quotes (】 ” ’ » and 〃 written for 〟) stay attached.
    const char32_t first = text::DecodeRune(next.text).cp;
    return !text::IsClosePunctuation(first) && first != 0xBB && first != 0x2019 && first != 0x201D &&
           first != 0x203A && first != 0x3003;
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

// The native Korean numeral that a count of one to nine written in kanji reads before a
// native counter (二 두, 三 세); 十 reads 열. Empty for any other number, and for a number
// written in Arabic digits, which stays in digits (2回 2회, 7枚 7장, 1個 1개) as a number
// does in a count, a table or a rule text, where the digits of one to nine and of larger
// numbers would otherwise mix.
// An Arabic 1 in an idiom of counting (IdiomaticOne) reads 한 as 一 does.
std::string_view NativeNumeral(std::string_view digits, std::string_view surface, bool idiom = false) {
    constexpr std::string_view kNative[] = {"한", "두", "세", "네", "다섯", "여섯", "일곱", "여덟", "아홉"};
    if (surface == "十" && digits == "10") return "열";
    if (idiom && digits == "1" && (surface == "1" || surface == "１")) return kNative[0];
    if (digits.size() != 1 || digits[0] == '0') return {};
    for (size_t k = 0; k < surface.size();) {
        const auto r = text::DecodeRune(surface.substr(k));
        if (!HasRune(U"一二三四五六七八九", r.cp)) return {};
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
// 첫 and is counted with 번 (첫 번째, not 한 번째 or 한 마리째); people in an ordinal are
// counted with 번 too (두 번째, not 두 사람째 or 두 명째).
std::string WriteNativeCount(std::string_view count, const NativeCounter& c, bool ordinal) {
    std::string_view form = NativeForm(c);
    if (count == "한" && (ordinal || form.ends_with("째"))) {
        return form.ends_with("째") ? "첫 번째" : "첫 번";
    }
    if (ordinal && (form == "사람" || form == "명")) form = "번";
    return std::string(count) + ' ' + std::string(form);
}

// Whether s is one word of Hangul syllables.
bool HangulWord(std::string_view s) {
    if (s.empty()) return false;
    for (size_t k = 0; k < s.size();) {
        const auto r = text::DecodeRune(s.substr(k));
        if (r.cp < 0xAC00 || r.cp > 0xD7A3) return false;
        k += r.size;
    }
    return true;
}

// Whether item n is a verbal noun that is the predicate: one with する after it, or one that
// ends its word run (HPが1回復した, HPが1回復).
bool PredicateNoun(const std::vector<ResolvedItem>& items, int n) {
    if (!Contains(GAttr(items[n]), "Hd")) return false;
    if (n + 1 >= static_cast<int>(items.size()) || items[n + 1].tokenIndex != items[n].tokenIndex) return true;
    return dict::StartsWithMember(HDTRANS_CLASS("suru.predicate-noun"), items[n + 1].surface) > 0;
}

// Kanji counters read in Sino-Korean that a number joins (2000년, 14일째, 3회).
constexpr std::u32string_view kSinoCounters = U"年日月週時分秒歳才回度個本匹頭枚冊台件名倍階号番章話巻隻杯体人";

// The kanji digits a number is written with digit by digit (二〇二六).
constexpr std::u32string_view kNumerals = U"〇○一二三四五六七八九";

// Whether item k is written straight after item k - 1 in the source.
bool Adjoins(const std::vector<ResolvedItem>& items, int k) {
    return k > 0 && items[k].tokenIndex == items[k - 1].tokenIndex &&
           items[k].offset == items[k - 1].offset + items[k - 1].length;
}

// Whether r is a digit or a Latin letter, also a full-width one.
bool DigitOrLatin(char32_t r) {
    const char32_t a = r >= U'０' && r <= U'ｚ' ? r - 0xFEE0 : r;
    return (a >= U'0' && a <= U'9') || (a >= U'A' && a <= U'Z') || (a >= U'a' && a <= U'z');
}

// Whether the noun of item n is written straight after item i in the source, with no space:
// a number or a Latin letter that ends the Korean of item i then joins it (1レベル 1레벨,
// Aランク A랭크, 10cm, 第五戦隊 제5전대, 改二改装 개2개장), and with a space in the source it
// stays apart (1 レベル 1 레벨). The noun starts with kanji, katakana or a Latin letter: a
// number written on (第2回, 百二十) is no noun after it, and a word in hiragana is a pronoun or
// a word of its own (≫758それ 그것). A predicate, a verbal noun with する or a word read as a
// verb or an adjective, stands apart after a number (HPが1回復した 1 회복했다); after Latin
// letters it is a noun like any other (SP消費 SP 소비).
bool JoinsNoun(const std::vector<TokenText>& tokens, const std::vector<ResolvedItem>& items, int i, int n) {
    if (n != i + 1) return false;
    // A number written with signs is a token of its own (20~30層): a noun that starts the next
    // token is written straight after it.
    const TokenText& number = tokens[static_cast<std::size_t>(items[i].tokenIndex)];
    const bool afterSigned = number.type == tokenizer::kTypeAsciiSymbol && tokenizer::SignedNumber(number.text) &&
                             items[n].tokenIndex == items[i].tokenIndex + 1 && items[n].offset == 0;
    if (!Adjoins(items, n) && !afterSigned) return false;
    const ResolvedItem& b = items[n];
    char32_t end = LastRune(items[i].surface);
    if (end >= U'Ａ' && end <= U'ｚ') end -= 0xFEE0;
    const bool latin = (end >= U'A' && end <= U'Z') || (end >= U'a' && end <= U'z');
    const char32_t lead = text::DecodeRune(b.surface).cp;
    if (HasRune(kNumerals, lead) || b.surface.starts_with("第")) return false;
    if (!IsKanji(lead) && tokenizer::Class(lead) != tokenizer::kClassKatakana && !DigitOrLatin(lead)) return false;
    const std::string_view code = Code(b);
    return code.size() >= 2 && code[0] == '1' && code[1] != '2' && !EntryText(b).ends_with("@") &&
           (latin || !PredicateNoun(items, n));
}

// Whether the number or the Latin word that ends item i in the source stands apart from the
// noun of item n written straight after it. A number before a noun counts it and joins it; a
// number after a word labels that word and stands apart from the next noun. A number joins a
// counter or a unit after it, and both join a suffix (5ポイント 5포인트, 1939年, 10cm, 1000G, Iowa級 Iowa급, Java風 Java풍,
// ID欄 ID란), a noun of one character (34世 34세), a noun counted in an ordinal (1ゲーム目
// 1게임째), after 第 (第2エリア 제2에리어), when the number counts the noun (3学期 3학기, 8等分
// 8등분, 22コンボ 22콤보, 3セット 3세트), and after one letter or a letter written two or three
// times (Aランク A랭크, SSランク SS랭크). They stand apart when the number is the label of the
// word written straight before it in katakana or Latin letters, or the end of a label
// (ステージ3ボス戦 스테이지 3 보스전, Lv.10スキル Lv.10 스킬, No.2-1キャラクター No.2-1 캐릭터,
// R1ボタン R1 버튼, ver2.0アップデート ver2.0 업데이트); when a word in Latin capitals, an
// abbreviation, is before a noun (ATBゲージ ATB 게이지), or a word with small letters, a name or
// a word, before a noun of two characters or more (class空母 class 항공모함, Bismarck追撃戦
// Bismarck 추격전, Googleカレンダー Google 캘린더); and for a reply number after ≫ or >>
// (≫372過疎 ≫372 과소), a time (23：40追記 23：40 추기) and a number of four digits before a noun
// in kanji, a year or a time (2026特別 2026 특별, 2300提督 2300 제독). Katakana after a word with
// small letters that is no common noun, unknown to the dictionary or a name, may be a spoken
// ending and joins it (Christmasダヨ Christmas다요, Decemberネー December네).
bool ApartFromNoun(const std::vector<TokenText>& tokens, const std::vector<ResolvedItem>& items, int i, int n) {
    const ResolvedItem& a = items[i];
    const ResolvedItem& b = items[n];
    if (Contains(GAttr(b), "Z7") || Ordinal(items, n)) return false;
    const auto ascii = [](char32_t r) { return r >= U'！' && r <= U'～' ? r - 0xFEE0 : r; };
    const auto latin = [](char32_t r) { return (r >= U'A' && r <= U'Z') || (r >= U'a' && r <= U'z'); };
    const auto digit = [](char32_t r) { return r >= U'0' && r <= U'9'; };
    const char32_t lead = text::DecodeRune(b.surface).cp;
    std::string_view rest = std::string_view(tokens[a.tokenIndex].text).substr(0, static_cast<std::size_t>(a.offset + a.length));
    std::size_t digits = 0;
    bool grouped = false;
    for (char32_t r = ascii(LastRune(rest)); r != 0; r = ascii(LastRune(rest))) {
        if (r == U',' || r == U'.' || HasRune(U"十百千万億", r)) {
            grouped = true;
        } else if (digit(r)) {
            digits++;
        } else {
            break;
        }
        rest = DropLastRune(rest);
    }
    if (digits > 0) {
        if (Contains(GAttr(b), "Ct")) return false;
        std::string before(rest);
        for (int k = a.tokenIndex - 1; k >= 0 && text::RuneCount(before) < 2; k--) before.insert(0, tokens[k].text);
        if (before.ends_with("≫") || before.ends_with(">>") || before.ends_with("＞＞")) return true;
        const std::string_view head = before;
        const char32_t mark = ascii(LastRune(head)), prior = ascii(LastRune(DropLastRune(head)));
        if (mark == U':' && prior >= U'0' && prior <= U'9') return true;
        // An ordinal after 第 numbers the noun (第2エリア 제2에리어).
        if (mark == U'第') return false;
        // A noun of one character after a number is its counter or suffix (34世 34세, 657コ 657코),
        // and Latin letters are its unit (10cm, 1000G).
        if (text::RuneCount(b.surface) == 1 || latin(ascii(lead))) return false;
        if (IsKanji(lead) && digits == 4 && !grouped && !HasRune(U"十百千万億兆", lead)) return true;
        // The number goes on a label written before it (Lv.10, No.2-1): past a dot or a hyphen,
        // a Latin letter, a digit or katakana. Latin letters after a number are its unit, and a
        // number after them counts anew (12cm30連装 12cm30연장).
        std::string_view label = mark == U'.' || mark == U'-' ? DropLastRune(head) : head;
        if (latin(ascii(LastRune(label)))) {
            while (latin(ascii(LastRune(label)))) label = DropLastRune(label);
            return !digit(ascii(LastRune(label)));
        }
        const char32_t r = ascii(LastRune(label));
        return digit(r) || tokenizer::Class(r) == tokenizer::kClassKatakana;
    }
    std::size_t letters = 0;
    bool small = false, same = true;
    const char32_t last = ascii(LastRune(rest));
    for (char32_t r = last; latin(r); r = ascii(LastRune(rest))) {
        letters++;
        small = small || r >= U'a';
        same = same && r == last;
        rest = DropLastRune(rest);
    }
    if (letters == 0 || digit(ascii(LastRune(rest)))) return false;
    if (letters == 1 || (same && !small && letters <= 3)) return false;
    // One kanji after a Latin word is a suffix (ID欄 ID란, USS風 USS풍).
    if (IsKanji(lead) && text::RuneCount(b.surface) == 1) return false;
    return !small || IsKanji(lead) || (!Contains(GAttr(b), "Nm") && !Code(b).starts_with("130"));
}

// A count the dictionary reads as one word, 하루 for 一日: in an ordinal the first day is 첫날.
constexpr std::string_view kOneDay = "하루";
constexpr std::string_view kFirstDay = "첫날";

// In an ordinal, a count of one period that the dictionary reads t, starting surface: the
// first period is 첫 and the period, with no 째 (一日目 하루 첫날, 一ヶ月目 한 달 첫 달). Empty
// for any other count; a count of things is 첫 번째.
std::string FirstUnit(std::string_view t, std::string_view surface, bool ordinal) {
    if (!ordinal || !HasRune(kCountLeads, text::DecodeRune(surface).cp)) return {};
    if (t == kOneDay) return std::string(kFirstDay);
    constexpr std::string_view kPeriods[] = {"한 달", "한 주", "한 해"};
    if (std::find(std::begin(kPeriods), std::end(kPeriods), t) == std::end(kPeriods)) return {};
    return "첫 " + std::string(t.substr(std::string_view("한 ").size()));
}

std::string_view TextTail(const Pieces& p) {
    return !p.empty() && p.back().span == SpanText ? std::string_view(p.back().text) : std::string_view();
}

// A word read as a particle or suffix that a noun before it takes without a space (바위뿐,
// 평소대로, 크기밖에, 밭마다, 그만큼, 한 번당), keyed by the Japanese it translates.
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
    return korean == "당" && (surface == "あたり" || surface == "当たり" || surface == "当り");
}

// Whether a noun's Korean is an adverb, which forms no compound with the noun after it (結局昼
// 결국 낮, 正直頭 솔직히 머리): a row read not as a bare noun (Nn) or a word of ko.adverb-noun.
bool AdverbKorean(std::string_view korean, std::string_view tags) {
    if (!HangulWord(korean) || text::RuneCount(korean) < 2) return false;
    return Contains(tags, "Nn") || dict::InClass(HDTRANS_CLASS("ko.adverb-noun"), korean);
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

// Whether Korean t is a verb phrase made a noun, which stays apart from a する after it: two
// words or more ending in 기 (대강 읽기, 함부로 버리기).
bool VerbPhraseNoun(std::string_view t) { return t.find(' ') != std::string_view::npos && t.ends_with("기"); }

constexpr std::string_view kHagi = "하기";

// Whether item i is a noun whose Korean t is a 하다 verb made a noun (출항하기, 직장 구하기)
// written straight before a する read 하다: the noun is then that verb, written without
// 하기 and joined to the する (출항합니다, 직장 구한다). 하기 itself, a proper noun and a noun
// ending in 期 (夏期 하기, 氷河期 빙하기) are Sino-Korean nouns.
bool HadaNounBeforeSuru(const std::vector<ResolvedItem>& items, int i, std::string_view t) {
    const ResolvedItem& noun = items[i];
    if (First(Code(noun)) != '1' || At(Code(noun), 1) == '3' || !t.ends_with(kHagi) || t == kHagi ||
        noun.surface.ends_with("期") || i + 1 >= static_cast<int>(items.size()) || !Adjoins(items, i + 1)) {
        return false;
    }
    const ResolvedItem& next = items[i + 1];
    if (EntryText(next) != "&AV@") return false;
    return dict::InClass(HDTRANS_CLASS("suru.verbal-noun-whole"), next.surface) ||
           dict::StartsWithMember(HDTRANS_CLASS("suru.verbal-noun-start"), next.surface) > 0;
}

// Whether the next item that writes something, written straight after item i, attaches to
// the last syllable of i: a particle or the copula (not one written as a space), a word
// written attached (Tu) that starts with a syllable and is no counter (들, 에 대한), or the
// する of a noun the dictionary writes with a space at its end. That space then goes (백년
// 해로를, 백년 해로한다, 싶을 정도입니다, 우리에 대한); before a word it stays (백년 해로 생활).
bool AttachesAfter(const std::vector<ResolvedItem>& items, const std::vector<TokenText>& tokens, int i) {
    int n = i + 1;
    const int count = static_cast<int>(items.size());
    while (n < count && EntryText(items[n]).empty() && (items[n].directIndex < 0 || items[n].ending.text.empty())) {
        n++;
    }
    if (n >= count || TokenDistance(tokens, items[i].tokenIndex, items[n].tokenIndex) >= 2) return false;
    const ResolvedItem& next = items[n];
    const std::string_view text = EntryText(next);
    if (text.empty() || text.front() == ' ') return false;
    if (First(Code(next)) == '2' ||
        (Contains(GAttr(next), "Tu") && !Contains(GAttr(next), "Ct") && text::Final(text::DecodeRune(text).cp) >= 0)) {
        return true;
    }
    const std::string_view gloss = EntryText(items[i]);
    const std::string_view word = gloss.substr(0, gloss.find_last_not_of(' ') + 1);
    if (First(Code(items[i])) != '1' || word.size() == gloss.size() || First(Code(next)) != '4' ||
        VerbPhraseNoun(word)) {
        return false;
    }
    return dict::InClass(HDTRANS_CLASS("suru.verbal-noun-whole"), next.surface) ||
           dict::StartsWithMember(HDTRANS_CLASS("suru.verbal-noun-start"), next.surface) > 0;
}

// Whether text is one sign read as a word after a number: %, ％ or a sign of the sign.final-*
// classes (℃, ㎏, °).
bool ReadSign(std::string_view text) {
    const text::Rune r = text::DecodeRune(text);
    if (text.empty() || r.size != text.size()) return false;
    return r.cp == U'%' || r.cp == U'％' || dict::InClassRune(HDTRANS_CLASS("sign.final-vowel"), r.cp) ||
           dict::InClassRune(HDTRANS_CLASS("sign.final-consonant"), r.cp) ||
           dict::InClassRune(HDTRANS_CLASS("sign.final-rieul"), r.cp);
}

// Whether an item of a token of the given type is one letter or number written as a symbol (①, Ⅳ, α),
// a word of its own.
bool SymbolLetter(int type, std::string_view surface) {
    const text::Rune r = text::DecodeRune(surface);
    return type == tokenizer::kTypeSymbol && !surface.empty() && r.size == surface.size() && r.cp >= 0x80 &&
           !text::IsKanji(r.cp) && !text::IsSymbol(r.cp) && !text::IsPunct(r.cp);
}

// Whether item k is a word that starts with a sign: a number led by + or - (+10, -5) or a symbol
// letter (①, Ⅳ, α). Reads the source surface: the token text of a later item is already written.
bool SignLedWord(const std::vector<TokenText>& tokens, const ResolvedItem& item) {
    const TokenText& tok = tokens[static_cast<std::size_t>(item.tokenIndex)];
    if (tok.span != 0 || item.offset != 0) return false;
    if (SymbolLetter(tok.type, item.surface)) return true;
    return tok.type == tokenizer::kTypeAsciiSymbol && (item.surface.starts_with('+') || item.surface.starts_with('-')) &&
           tokenizer::SignedNumber(item.surface);
}

// Whether the form 8 a stem writes is its noun form: a stem row written as a word (重さ 무게) or one
// whose marker family writes form 8 by the rule (재$C1@ 재빠름), not by an exception of the family
// (그%C5@ 그래, 시퍼%C5@ 시퍼래).
bool NominalForm(std::string_view stem) {
    if (stem.size() < 4 || stem.back() != '@') return true;
    const std::string_view key = stem.substr(stem.size() - 4);
    if (key.find_first_of("&$%") != 0) return true;
    const internal::KoreanFamily* family = internal::FindKoreanFamily(key);
    if (family == nullptr) return true;
    return std::none_of(family->exceptions.begin(), family->exceptions.end(),
                        [](const internal::InflectionException& e) { return e.code == '8'; });
}

// Whether a word that starts with a sign stands apart after item by the kind of the row that
// produced it. It is written on only to a noun row other than a pronoun (pos x120: 우리 μ's, 이것 α)
// that reads as a bare noun (no gram Nn: 答えは 대답은, 当然 당연히 have it) and to an adjective or adjectival noun whose ending row makes it a
// noun (ending.nominal) in its noun form (素早さ 재빠름); after any other row it stands apart (여기서
// ①, 그 ①, 좋아 +1, 그래 +1).
bool ApartAfterRow(const ResolvedItem& item, char kind, std::string_view tags) {
    if (item.directIndex >= 0) {
        const std::string row = item.endingSurface + "=" + item.ending.text;
        return !((kind == '7' || kind == '8') && dict::InClass(HDTRANS_CLASS("ending.nominal"), row) &&
                 NominalForm(EntryText(item)));
    }
    return kind != '1' || Code(item).starts_with("120") || Contains(tags, "Nn");
}

// Whether item k follows item k - 1 with nothing between them: in the same token, or at the
// start of the next one (a number token after a word).
bool Follows(const std::vector<ResolvedItem>& items, int k) {
    return Adjoins(items, k) || (k > 0 && items[k].tokenIndex == items[k - 1].tokenIndex + 1 && items[k].offset == 0);
}

// The source of the items written straight before item k, and after it, up to n
// characters (the token texts hold the Korean of the items composed so far).
std::string SourceBefore(const std::vector<ResolvedItem>& items, int k, std::size_t n) {
    std::string before;
    for (int j = k; j > 0 && Follows(items, j) && text::RuneCount(before) < n; j--) before.insert(0, items[j - 1].surface);
    return before;
}

std::string SourceAfter(const std::vector<ResolvedItem>& items, int k, std::size_t n) {
    std::string after;
    for (int j = k + 1; j < static_cast<int>(items.size()) && Follows(items, j) && text::RuneCount(after) < n; j++) {
        after += items[j].surface;
    }
    return after;
}

// Whether the count of item number, an Arabic 1, and its counter, item counter (the same
// item when they are one word), form an idiom that Korean says with the native 한 as for
// 一 (NUM-5): none left (1人残らず 한 명 남김없이), one after another (1人また1人 한 명 또
// 한 명) and once in a period (100年に1度 100년에 한 번, 1日に1回 1일에 한 번).
bool IdiomaticOne(const std::vector<TokenText>& tokens, const std::vector<ResolvedItem>& items, int number, int counter) {
    if (number < 0 || counter >= static_cast<int>(items.size())) return false;
    const std::string_view surface = items[number].surface;
    const bool one = surface.starts_with("1") ? surface.size() == 1 || !IsDigit(surface[1]) : surface.starts_with("１");
    if (!one || (surface.starts_with("１") && surface.substr(std::string_view("１").size()).starts_with("１"))) return false;
    if (number > 0 && Adjoins(items, number) && InsideNumber(tokens, items[number])) return false;
    std::string_view word = items[counter].surface;
    if (number == counter) word.remove_prefix(surface.starts_with("1") ? 1 : std::string_view("１").size());
    if (word.empty()) return false;
    const std::string after = SourceAfter(items, counter, 8);
    const std::string before = SourceBefore(items, number, 8);
    if (std::string_view(after).starts_with("残らず")) return true;
    for (const std::string_view digit : {"1", "１"}) {
        if (std::string_view(after).starts_with("また" + std::string(digit) + std::string(word))) return true;
        const std::string earlier = std::string(digit) + std::string(word) + "また";
        if (std::string_view(before).ends_with(earlier) &&
            !text::IsDigit(LastRune(std::string_view(before).substr(0, before.size() - earlier.size())))) {
            return true;
        }
    }
    if (word != "度" && word != "回") return false;
    if (!std::string_view(before).ends_with("に")) return false;
    const char32_t period = LastRune(std::string_view(before).substr(0, before.size() - std::string_view("に").size()));
    return HasRune(U"年日月週生間分秒晩夜朝回度", period);
}

// A number and its counter read as one word (三箇所 3군데, 八本 8 개, 二人 2명): a count
// of one to nine in kanji counted natively is written in native Korean apart from the
// counter (세 군데, 여덟 개, 두 명), a larger one or one in Arabic digits (２人 2명) in digits
// joined to it.
std::string CountWithin(const std::vector<TokenText>& tokens, const std::vector<ResolvedItem>& items, int i,
                        std::string t) {
    const ResolvedItem& item = items[i];
    // A native count read as one word with its counter (一匹 한마리, ２人 두명, 何個 몇개)
    // stands apart from it (한 마리, 두 명, 몇 개).
    // A count the dictionary reads apart from its counter (一度 한 번, 一枚 한 장, 二人 두 사람)
    // is written as written but in an ordinal, where one is the first of a period (FirstUnit)
    // or else 첫 번.
    const bool ordinal = Ordinal(items, i);
    if (std::string first = FirstUnit(t, item.surface, ordinal); !first.empty()) return first;
    for (const std::string_view& count : kNativeCounts) {
        if (!t.starts_with(count) || t.size() == count.size()) continue;
        const bool apart = t[count.size()] == ' ';
        if (apart && !ordinal) continue;
        const std::string_view surface = item.surface;
        if (!HasRune(kCountLeads, text::DecodeRune(surface).cp)) break;
        size_t numeral = 0;
        while (numeral < surface.size()) {
            const auto r = text::DecodeRune(surface.substr(numeral));
            if (!HasRune(kCountLeads, r.cp)) break;
            numeral += r.size;
        }
        const std::string_view korean = std::string_view(t).substr(count.size() + (apart ? 1 : 0));
        if (apart && count == "한") {
            if (HangulWord(korean)) t = "첫 번";
            break;
        }
        const NativeCounter* c = FindNativeCounter(surface.substr(numeral), korean);
        // A word of an Arabic digit and its counter (1杯 한잔) counts in digits in an ordinal
        // (1杯目 1잔째, as 2杯目 2잔째).
        const auto value = &count - std::begin(kNativeCounts) + 1;
        if (c != nullptr && ordinal && value <= 9 && text::IsDigit(text::DecodeRune(surface).cp)) {
            t = std::to_string(value) + std::string(korean);
            break;
        }
        if (c != nullptr && !(c->joins && count == "한" && !ordinal)) t = WriteNativeCount(count, *c, ordinal);
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
    const std::string_view native =
        NativeNumeral(std::string_view(t).substr(0, digits), surface.substr(0, numeral), IdiomaticOne(tokens, items, i, i));
    // A number in digits stays in digits; in an ordinal its counter is read as after a native
    // numeral (1回目 1번째 as 2回目 2번째).
    if (native.empty()) return t.substr(0, digits) + std::string(Ordinal(items, i) ? NativeForm(*c) : counter);
    // つ counts with the bare native numeral, as 一つ reads 하나 (1つ残らず 하나 남김없이).
    if (native == kNativeCounts[0] && dict::InClass(HDTRANS_CLASS("counter.bare-native-one"), surface.substr(numeral)) && !Ordinal(items, i)) {
        return "하나";
    }
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
    if (AllDigits(t)) {
        return beforeCounter && !InsideNumber(tokens, item) &&
               !NativeNumeral(t, item.surface, IdiomaticOne(tokens, items, j, j + 1)).empty();
    }
    if (t == "몇" || t == "여러") return beforeCounter;
    const std::string written = CountWithin(tokens, items, j, std::string(t));
    return std::any_of(std::begin(kNativeCounts), std::end(kNativeCounts), [&](std::string_view count) {
        return written.starts_with(count) && written.size() > count.size() && written[count.size()] == ' ';
    });
}

// Whether item k is past the items or starts with a mark that may end a word said again: a
// clipping っ, a mark of mark.cry or pause.copy, or a closing bracket.
bool EndsCopy(const std::vector<ResolvedItem>& items, int k) {
    if (k >= static_cast<int>(items.size())) return true;
    const std::string_view s = items[k].surface;
    const std::string_view mark = s.substr(0, text::DecodeRune(s).size);
    return dict::InClass(HDTRANS_CLASS("mark.clip"), mark) || dict::InClass(HDTRANS_CLASS("mark.cry"), mark) || dict::InClass(HDTRANS_CLASS("pause.copy"), mark) ||
           Closes(FirstRune(s));
}

// Whether item i is a copy of a word of two characters or more with its ending said again, with
// its clipping っ, writing nothing, right after it (だめ|っ|だめ, 痛い|っ|、|痛い|っ|……; not
// ぜん|っ|ぜん仕事).
bool ClippedCopy(const std::vector<ResolvedItem>& items, int i) {
    const int count = static_cast<int>(items.size()), next = i + 1;
    if (next >= count || !EntryText(items[next]).empty() || !dict::InClass(HDTRANS_CLASS("mark.clip"), items[next].surface) ||
        text::RuneCount(items[i].surface) + text::RuneCount(items[i].endingSurface) < 2) {
        return false;
    }
    const auto same = [&](int k) {
        return items[k].surface == items[i].surface && items[k].endingSurface == items[i].endingSurface;
    };
    if (next + 1 < count && same(next + 1) && EndsCopy(items, next + 2)) return true;
    int before = i - 1;
    while (before >= 0 &&
           (dict::InClass(HDTRANS_CLASS("mark.clip"), items[before].surface) || dict::InClass(HDTRANS_CLASS("pause.copy"), items[before].surface))) {
        before--;
    }
    return before >= 0 && same(before);
}

// Whether item i ends a node of items before end: it is the last, or a ClippedCopy whose っ ends
// the node (だめ|っ 안돼 안돼).
bool ClipsEnd(const std::vector<ResolvedItem>& items, int i, int end) {
    return i + 1 == end || (i + 2 == end && ClippedCopy(items, i));
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
        const bool kana = type == tokenizer::kClassHiragana || type == tokenizer::kClassKatakana;
        if (type == tokenizer::kTypeJapanese && s.hanjaToHangul == 1) {
            if (const auto w = dict::Written(r)) r = *w;
            text::AppendRune(out, r);
        } else if (type == tokenizer::kTypeJapanese || (!kana && r != U'、')) {
            text::AppendRune(out, r);
        } else if (s.deleteJapan != 1 || r == U'・' || !kana) {  // r is kana or 、 here
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
    tokens_ = nullptr;
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

namespace {
void KeepParticlesWithCutWord(const std::vector<ResolvedItem>& items, const std::vector<TokenText>& tokens,
                              std::span<tokenizer::ControlMarker> markers);
}  // namespace

void Composer::FinalOutputPrepass(std::vector<ResolvedItem>& items, const Nodes& nodes,
                                  std::vector<TokenText>& tokens, int first, int last,
                                  std::span<tokenizer::ControlMarker> markers,
                                  const Settings& settings, const Families& generated) {
    Start(items, settings, generated);
    tokens_ = &tokens;
    first_ = first;
    if (settings.text) {
        if (source_.size() < tokens.size()) source_.resize(tokens.size());
        for (int i = first; i <= last; i++) source_[i] = tokens[i].text;
        if (settings.casual || settings.asked) WriteCasual(first, last);
    }
    if (settings.markerBeforeWord) KeepParticlesWithCutWord(items, tokens, markers);
    for (int n = static_cast<int>(nodes.size()) - 1; n >= 0; n--) {
        const int begin = n > 0 ? nodes[n - 1] : 0;
        for (int index = nodes[n] - 1; index >= begin; index--) WriteItem(index, ClipsEnd(items, index, nodes[n]), markers);
    }
    JoinParticlesAfterSpace(items, tokens, first, last);
}

void JoinParticlesAfterSpace(const std::vector<ResolvedItem>& items, std::vector<TokenText>& tokens, int first,
                             int last) {
    for (const ResolvedItem& item : items) {
        const int t = item.tokenIndex;
        if (item.offset != 0 || t - 2 < first || t > last || tokens[t].type != tokenizer::kTypeJapanese) continue;
        TokenText& space = tokens[t - 1];
        const TokenText& word = tokens[t - 2];
        if (space.type != tokenizer::kTypeAsciiSpace || space.span != 0 || !KeepsAdjacency(word.type) ||
            word.text.empty() || std::string_view(" .,:;!?").find(word.text.back()) != std::string_view::npos) {
            continue;
        }
        const std::string& written = tokens[t].text;
        // A particle row written apart (Ha: まだ 아직) keeps the space (いえ まだです 아뇨 아직입니다).
        if (First(Code(item)) != '2' || Contains(GAttr(item), "Ha") || written.empty() || written.front() == ' ') continue;
        space.text.clear();
    }
}

namespace {

// Whether a marker at byte at of the token text, inside item, goes before the word rather than after
// it: when more of the word's Japanese follows the marker than precedes it, unless what precedes it
// is one particle the word took in (に揺れる) and more words follow (last is false).
bool WritesMarkerBefore(const ResolvedItem& item, std::string_view tokenText, int at, bool last) {
    const int end = item.offset + item.length;
    if (at <= item.offset || at >= end || static_cast<std::size_t>(end) > tokenText.size()) return false;
    const std::string_view head = tokenText.substr(static_cast<std::size_t>(item.offset),
                                                   static_cast<std::size_t>(at - item.offset));
    const std::string_view tail = tokenText.substr(static_cast<std::size_t>(at), static_cast<std::size_t>(end - at));
    const std::size_t headRunes = text::RuneCount(head);
    if (!last && headRunes == 1 &&
        std::u32string_view(U"にをがでとはもへの").find(text::DecodeRune(head).cp) != std::u32string_view::npos) {
        return false;
    }
    return headRunes < text::RuneCount(tail);
}

// A break of joined lines inside a word (ControlMarker::inWord) that keeps the word on the line before
// (パーテ / ィーを 파티 / 를) moves past the particles and copulas in kana after the word in its token,
// which go on the word's line (파티를 / 개최했다), unless only marks are left after them.
void KeepParticlesWithCutWord(const std::vector<ResolvedItem>& items, const std::vector<TokenText>& tokens,
                              std::span<tokenizer::ControlMarker> markers) {
    for (tokenizer::ControlMarker& m : markers) {
        if (m.markerKind != 1 || !m.inWord) continue;
        for (int i = 0; i < static_cast<int>(items.size()); i++) {
            const ResolvedItem& item = items[i];
            if (item.tokenIndex != m.tokenIndex || m.textLength <= item.offset || m.textLength >= item.offset + item.length) {
                continue;
            }
            bool last = true;
            for (int k = i + 1; k < static_cast<int>(items.size()) && last; k++) {
                last = items[k].tokenIndex != item.tokenIndex || First(Code(items[k])) == '6';
            }
            if (WritesMarkerBefore(item, tokens[item.tokenIndex].text, m.textLength, last)) break;
            int end = i;
            // A particle or copula written in kana, not a suffix noun (向け 전용).
            const auto attached = [&](const ResolvedItem& p) {
                if (First(Code(p)) != '2' || p.surface.empty()) return false;
                for (std::size_t at = 0; at < p.surface.size();) {
                    const text::Rune r = text::DecodeRune(std::string_view(p.surface).substr(at));
                    if (r.cp < U'ぁ' || r.cp > U'ゖ') return false;
                    at += std::max<std::size_t>(1, r.size);
                }
                return true;
            };
            while (end + 1 < static_cast<int>(items.size()) && items[end + 1].tokenIndex == item.tokenIndex &&
                   items[end + 1].offset == items[end].offset + items[end].length && attached(items[end + 1])) {
                end++;
            }
            // The next line keeps them when no letter or digit would be left for it, only marks (始ま / りです。).
            const auto words = [](std::string_view s) {
                for (std::size_t at = 0; at < s.size();) {
                    const text::Rune r = text::DecodeRune(s.substr(at));
                    if (text::IsLetter(r.cp) || text::IsDigit(r.cp)) return true;
                    at += std::max<std::size_t>(1, r.size);
                }
                return false;
            };
            bool rest = words(std::string_view(tokens[item.tokenIndex].text)
                                  .substr(std::min(tokens[item.tokenIndex].text.size(),
                                                   static_cast<std::size_t>(items[end].offset + items[end].length))));
            for (std::size_t t = static_cast<std::size_t>(item.tokenIndex) + 1; t < tokens.size() && !rest; t++) {
                rest = words(tokens[t].text);
            }
            if (!rest) end = i;
            if (end > i) m.textLength = items[end].offset + items[end].length;
            break;
        }
    }
}

}  // namespace

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
    // No marker family reaches the output as text (DictionaryForms).
    std::string translated =
        WriteCount(i, ApplyParenthesisTextFilter(DictionaryForms(Build(i, 0, {}), *generated_), i)) + item.appended;
    if (settings_.literal) translated = SplitTimeNoun(item, std::move(translated));
    const bool protectedToken = tokens[item.tokenIndex].type == tokenizer::kTypeProtected;
    if (settings_.cleanup && !protectedToken) RemoveGlosses(translated);
    // A word span keeps its text unless a rewrite read it anew (ちょ 잠깐 → 완전 before an adjective);
    // one the rewrite emptied writes nothing (ギシギシ before 軋む).
    if (protectedToken && item.reread) {
        tokens[item.tokenIndex].spanText = translated;
        if (translated.empty()) tokens[item.tokenIndex].text.clear();
    }
    const bool hadaNoun = HadaNounBeforeSuru(items, i, translated);
    if (hadaNoun) translated.resize(translated.size() - kHagi.size());
    std::string composed;
    if (record) composed = translated;
    auto [spaced, space] = hadaNoun ? std::pair{std::move(translated), false}
                                    : Spacing(i, std::move(translated), tags, nextTags, kind, nextKind, isLast, converted, remainder);
    if (!space && !hadaNoun && AttachesAfter(items, tokens, i)) {
        const size_t end = spaced.find_last_not_of(' ');
        if (end != std::string::npos) spaced.erase(end + 1);
    }
    if (record) inflected.push_back({i, std::move(composed), spaced, space});
    if (protectedToken) {
        spaced = tokenText;
    } else if (settings_.text) {
        spaced = KeepDropped(i, std::move(spaced), remainder);
    }
    if (space && !(protectedToken && tokenText.empty())) spaced += ' ';
    for (auto& m : markers) {
        if (m.markerKind == 0 || item.tokenIndex != m.tokenIndex) continue;
        const std::string_view marker = m.markerKind == 1 ? "&#CS" : "</a>";
        if (item.offset != m.textLength) {
            bool last = true;
            for (int k = i + 1; k < static_cast<int>(items.size()) && last; k++) {
                last = items[k].tokenIndex != item.tokenIndex || First(Code(items[k])) == '6';
            }
            const bool before = settings_.markerBeforeWord && WritesMarkerBefore(item, tokenText, m.textLength, last);
            if (item.offset <= m.textLength && !before) {
                spaced += marker;
                m.markerKind = 0;
                continue;
            }
            // The word before, when it holds the marker, writes it.
            const bool held = settings_.markerBeforeWord && previous >= 0 && items[previous].tokenIndex == m.tokenIndex &&
                              items[previous].offset < m.textLength &&
                              m.textLength < items[previous].offset + items[previous].length;
            if (!before && (previous < 0 || items[previous].offset >= m.textLength || held)) continue;
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
    // The first of a period takes no 째 (一日目 첫날, 一ヶ月目 첫 달).
    if (i > 0 && std::string_view(text).starts_with("째") &&
        !FirstUnit(EntryText(items[i - 1]), items[i - 1].surface, Ordinal(items, i - 1)).empty()) {
        return text.substr(std::string_view("째").size());
    }
    text = CountWithin(tokens, items, i, std::move(text));
    const int count = static_cast<int>(items.size());
    if (AllDigits(text) && i + 1 < count && WritesNativeCount(items, tokens, i)) {
        const std::string_view native = NativeNumeral(text, items[i].surface, IdiomaticOne(tokens, items, i, i + 1));
        if (!native.empty()) text = native == "한" && Ordinal(items, i + 1) ? "첫" : native;
    } else if (const NativeCounter* c = FindNativeCounter(items[i].surface, text);
               c != nullptr && WritesNativeCount(items, tokens, i - 1)) {
        const std::string written =
            WriteNativeCount(NativeNumeral(EntryText(items[i - 1]), items[i - 1].surface, IdiomaticOne(tokens, items, i - 1, i)),
                             *c, Ordinal(items, i));
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
        if (!next->empty || next->end <= pos) break;
        pos = next->end;
    }
    const std::string_view run = src.substr(start, pos - start);
    const char32_t after = FirstRune(src.substr(pos));
    bool kanjiBefore = false;
    if (start > 0) {
        const std::string_view head = src.substr(0, start - beforeRune.size);
        kanjiBefore = IsKanji(DecodeLastRune(TextBefore(head), head.size()).cp);
    }
    // A sound said on its own before a comma (む、無理 무, 무리): the token of the sentence before it, if
    // any, ends in a space, an opening or other mark, not in a word or a closing mark (がば|り、 벌떡|り、,
    // [・]ば、).
    bool opens = true;
    for (int t = token - 1; start == 0 && t >= first_; t--) {
        if (source_[t].empty()) continue;
        const char32_t last = DecodeLastRune(TextBefore(source_[t]), source_[t].size()).cp;
        opens = Opens(last) || (text::IsSymbol(last) && !Closes(last));
        break;
    }
    const bool alone = settings_.text && pos == runEnd &&
                       (pos == static_cast<int>(src.size()) || Closes(after) || HasRune(U"…‥！？!?", after) ||
                        (HasRune(U"、，,", after) && opens)) &&
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
        // So does a verb whose stem and ending the rules emptied (行こうと思って 가려고), and the
        // clipping っ of a copy (ClippedCopy: 痛い|っ|…… 아팟……).
        while (nextIndex < count && EntryText(items[nextIndex]).empty() &&
               (dict::InClass(HDTRANS_CLASS("honorific.prepass"), items[nextIndex].surface) ||
                (nextIndex == i + 1 && ClippedCopy(items, i)) ||
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
    if (!t.empty() && previous >= 0 && EntryText(items[previous]).empty() &&
        (item.surface == "上" ||
         (kind == '1' && items[previous].surface == "の" && !Contains(tags, "Ct") &&
          dict::InClass(HDTRANS_CLASS("noun.prepass-place"), item.surface)))) {
        t.insert(0, " ");
    }
    // みたい read as the adjective 같다 is written apart from the word before it; the
    // particle 같이 stays attached (너 같은 놈, 아이같이).
    if (item.surface == "みたい" && previous >= 0 && !EntryText(items[previous]).empty() &&
        t.starts_with("같") && !t.starts_with("같이")) {
        t.insert(0, " ");
    }
    using Result = std::pair<std::string, bool>;
    JoinBeforeDoe(t);
    // An auxiliary verb (gram Ds) read as a word of ko-aux.joined after the 아/어 form is
    // written joined to it (食べてしまった 먹어버렸다); the main verb stays apart (拾って捨てた
    // 주워 버렸다; LAY-11).
    if (nextIndex == i + 1 && First(item.ending.text) == '6' && Contains(GAttr(items[nextIndex]), "Ds") &&
        dict::InClass(HDTRANS_CLASS("ko-aux.joined"), EntryText(items[nextIndex]))) {
        return Result(std::move(t), false);
    }
    // A comma keeps its space before a run of symbols as before a word (마인, ％＆＄＃).
    if (settings_.text && kind == '6' && t == "," && SymbolsFollow(tokens, items, i)) {
        return Result(std::move(t), true);
    }
    if (nextIndex < 0 || t.empty() || item.selectionFlag == 1 ||
        TokenDistance(tokens, item.tokenIndex, items[nextIndex].tokenIndex) >= 2) {
        return Result(std::move(t), false);
    }
    const ResolvedItem& next = items[nextIndex];
    if (item.spaceAfter && (nextKind != '2' || Contains(nextTags, "Z0"))) return Result(std::move(t), true);
    // A word that starts with a sign (+10, ①, Ⅳ, α) written straight on to a noun stays joined to it
    // (방패+10, 스테이지Ⅳ, 재빠름+2); after a particle or an ending it stands apart (레벨이 +1,
    // 이것은 ①입니다, 있기 때문에 α).
    if (kind != '6' && SignLedWord(tokens, next) && Follows(items, nextIndex)) {
        return Result(std::move(t), ApartAfterRow(item, kind, tags));
    }
    if (const int a = tokens[item.tokenIndex].type, b = tokens[next.tokenIndex].type;
        (a == tokenizer::kTypeProtected || b == tokenizer::kTypeProtected) && KeepsAdjacency(a) && KeepsAdjacency(b)) {
        return Result(std::move(t), false);
    }
    // The 째 of an ordinal joins the count before it (一度目 첫 번째, 二人目 두 번째).
    if (nextIndex == i + 1 && Ordinal(items, i) && next.surface.starts_with("目") && EntryText(next).starts_with("째")) {
        return Result(std::move(t), false);
    }
    // The word before a native count stands apart from it (개 두 마리, not 개두 마리), also
    // across items that write nothing; punctuation keeps its own spacing (두 번·한 번).
    for (int j = settings_.literal && !Opens(LastRune(t)) ? nextIndex : count;
         j < count && TokenDistance(tokens, item.tokenIndex, items[j].tokenIndex) < 2; j++) {
        if (WritesNativeCount(items, tokens, j)) {
            if (!t.ends_with(' ')) return Result(std::move(t), true);
            break;
        }
        if (!EntryText(items[j]).empty()) break;
    }
    // A count before a native counter: one to nine in kanji, written in native Korean by
    // WriteCount, and 몇 or 여러 stand apart from the counter (二匹 두 마리, 三回 세 번, 何匹 몇
    // 마리), a number in digits joins it (20마리, 3回 3회). A number inside a longer one or a range
    // (第2回, 1、2個, 2～3本) or before a compound stays as it is.
    if (settings_.literal && FindNativeCounter(next.surface, EntryText(next)) != nullptr) {
        if (t.find(' ') == std::string::npos && WritesNativeCount(items, tokens, i)) return Result(std::move(t), true);
        if (AllDigits(t)) return Result(std::move(t), false);
    }
    // 몇 and 여러 stand apart from any counter (몇 체, 몇 층).
    if (settings_.literal && (t == "몇" || t == "여러") && Contains(nextTags, "Ct")) return Result(std::move(t), true);
    // A number word in Hangul, also one that goes on with 만 or 억 (몇십만, 수백억), stands
    // apart from the counter after it (수십 명, 몇백 년, 백 번, 천 명) and joins 万 or 億.
    if (settings_.literal && Contains(nextTags, "Ct")) {
        constexpr std::string_view kNumberWords[] = {"수십", "수백", "수천", "수만", "몇십", "몇백", "몇천", "몇만"};
        auto numberWord = [&](std::string_view w) {
            return std::find(std::begin(kNumberWords), std::end(kNumberWords), w) != std::end(kNumberWords);
        };
        // A number word the dictionary reads from a kanji numeral in Hangul (百 백, 千 천), unless
        // it goes on from digits (2000만엔, NUM-1).
        auto hangulNumber = [&](std::string_view w) {
            if (w.empty() || !(Contains(tags, "Nj") || Contains(tags, "Nd"))) return false;
            if (previous >= 0) {
                const std::string_view before = EntryText(items[previous]);
                if (!before.empty() && IsDigit(before.back())) return false;
            }
            for (std::size_t k = 0; k < w.size();) {
                const text::Rune r = text::DecodeRune(w.substr(k));
                if (!text::Split(r.cp)) return false;
                k += r.size;
            }
            return true;
        };
        const bool myriad = next.surface == "万" || next.surface == "億";
        if (numberWord(t) || hangulNumber(t)) return Result(std::move(t), !myriad);
        if ((item.surface == "万" || item.surface == "億") && previous >= 0 && numberWord(EntryText(items[previous])) &&
            !myriad) {
            return Result(std::move(t), true);
        }
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
    if (JoinedCopies(items, i, nextIndex)) return Result(std::move(t), false);  // 네네네
    if (MimeticBeforeSuru(item, next, nextEntry)) return Result(std::move(t), false);
    if (nextEntry == "「") return Result(std::move(t), true);
    // A number keeps its unit sign (50%, 30℃).
    if (!allowSpacing || nextEntry == "%" || (!t.empty() && IsDigit(t.back()) && ReadSign(next.surface))) return Result(std::move(t), false);
    // 씨 and 군 after a name or its mask are dependent nouns, written apart (카케이 씨, 타나카 군,
    // ○○ 씨).
    bool mask = !t.empty();
    for (std::size_t k = 0; mask && k < t.size();) {
        const text::Rune r = text::DecodeRune(std::string_view(t).substr(k));
        mask = HasRune(U"○●◯◎×✕＊*■□△▲◇◆", r.cp);
        k += r.size;
    }
    if ((kind == '1' || mask) && (nextEntry == "씨" || nextEntry == "군")) {
        if (dict::InClass(HDTRANS_CLASS("honorific.prepass-spaced"), next.surface)) return Result(std::move(t), true);
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
        if (settings_.literal && nextKind == '1' && !Code(item).starts_with("13") && AdverbKorean(t, tags) &&
            !Contains(nextTags, "Ct") && !(Code(item).starts_with("12") && Contains(nextTags, "Z7")) &&
            HangulWord(nextEntry)) {
            return Result(std::move(t), true);
        }
    }
    // A number joins a noun after it that starts with a counter read in Sino-Korean (2000년
    // 후, 14일째) unless it is the verbal noun of the predicate (1 회복했다), and an ordinal
    // after 第 joins the noun it numbers (제17구축대, 제1함대).
    if (settings_.literal && AllDigits(t) && nextKind == '1') {
        const char32_t lead = text::DecodeRune(next.surface).cp;
        const bool ordinal = previous >= 0 && items[previous].surface == "第";
        if (IsKanji(lead) && (ordinal || HasRune(kSinoCounters, lead)) &&
            dict::Written(lead) == text::DecodeRune(EntryText(next)).cp && (ordinal || !PredicateNoun(items, nextIndex))) {
            return Result(std::move(t), false);
        }
    }
    // A digit or a Latin letter is written with the noun after it apart when the source writes
    // a space between them (JoinsNoun); without one, they join when the number counts the noun
    // or the letter grades it and stand apart when the number labels the word before it
    // (ApartFromNoun).
    if (nextKind == '1' && DigitOrLatin(LastRune(t)) && JoinsNoun(tokens, items, i, nextIndex)) {
        return Result(std::move(t), ApartFromNoun(tokens, items, i, nextIndex));
    }
    // The する that a word written attached starts with (しやすくなる 하기 쉬워지다, させる, される) joins
    // the noun before it, not a particle (攻撃もしやすくなる 공격도 하기 쉬워진다).
    const bool suruAfterParticle =
        kind == '2' && (nextKind == '4' || nextKind == '7') &&
        (next.surface.starts_with("し") || next.surface.starts_with("さ") || next.surface.starts_with("せ") ||
         next.surface.starts_with("す")) &&
        std::u32string_view(U"하해했할한함합시되돼됐당").find(text::DecodeRune(nextEntry).cp) != std::u32string_view::npos;
    if (!Contains(nextTags, "Tu")) {
        if (Contains(nextTags, "Ha")) return Result(std::move(t), true);
    } else if (t != "," && !suruAfterParticle) {
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
    // A name suffix written onto the name (짱, 군) ends it: a noun after it in the same token
    // that is no suffix and starts with no particle stands apart (田中ちゃんお仕事 타나카짱 일,
    // 魅ぃちゃんごめんね 매짱 미안해; not 圭一さんにしては 케이이치 씨치고는).
    if (kind == '1' && nextKind == '1' && next.tokenIndex == item.tokenIndex && Contains(tags, "Tu") &&
        dict::InClass(HDTRANS_CLASS("honorific.candidate"), item.surface) &&
        !Contains(nextTags, "Tu") && !Contains(nextTags, "Z7") && WidthOf(nextEntry) >= 2 &&
        dict::StartsWithMember(HDTRANS_CLASS("particle.after-name"), next.surface) == 0) {
        return Result(std::move(t), true);
    }
    // A noun that starts with a kanji and is said again right after itself is a word of its own
    // (終わり終わり 끝 끝).
    if (kind == '1' && nextKind == '1' && next.tokenIndex == item.tokenIndex && next.surface == item.surface &&
        nextEntry == t && text::RuneCount(item.surface) >= 2 && IsKanji(FirstRune(item.surface))) {
        return Result(std::move(t), true);
    }
    if (!isLast) {
        // A pronoun written in kanji or two kana or more forms no compound with a noun next
        // to it (誰か僕を 누군가 나를, そもそも俺は 원래 나는), but takes a suffix or a word of one
        // syllable (僕ら 우리, 自分と 자신과, 私流 나류).
        const auto pronoun = [](const ResolvedItem& w) {
            return Code(w).starts_with("12") && (text::RuneCount(w.surface) >= 2 || IsKanji(FirstRune(w.surface)));
        };
        // A pronoun the token ends with before Latin, digits or a symbol is part of a name
        // written on (カフェ俺@cafe 카페나); after a digit or a Latin letter the source spacing
        // holds (A私 A내가).
        const bool namedOn = next.tokenIndex + 1 < static_cast<int>(tokens.size()) &&
                             (nextIndex + 1 >= count || items[nextIndex + 1].tokenIndex != next.tokenIndex) &&
                             (tokens[next.tokenIndex + 1].type == tokenizer::kTypeAlpha ||
                              tokens[next.tokenIndex + 1].type == tokenizer::kTypeNumber ||
                              tokens[next.tokenIndex + 1].type == tokenizer::kTypeAsciiSymbol);
        if (kind == '1' && nextKind == '1' && !Contains(nextTags, "Z7") && !Contains(tags, "Z6") &&
            ((pronoun(item) && WidthOf(nextEntry) > 2) || (pronoun(next) && !namedOn && !DigitOrLatin(LastRune(t))))) {
            return Result(std::move(t), true);
        }
        if (kind != '1' || WidthOf(t) != 2) return Result(std::move(t), false);
        if (nextKind != '1' || WidthOf(nextEntry) <= 2) return Result(std::move(t), false);
        const bool space = Contains(tags, "Z7") || !Contains(tags, "Z6");
        return Result(std::move(t), space);
    }
    switch (kind) {
        case '0': case '2':
            return Result(std::move(t), nextKind != '2' && nextKind != '6');
        case '1': {
            // A sign written after a number is its unit (50％, 30℃, 180°): a predicate, an adverb or a
            // noun after it stands apart as after the number (50% 늘어났다, 180° 바뀌었다, 30℃ 이상).
            if (const std::string_view number = previous >= 0 ? EntryText(items[previous]) : std::string_view();
                ReadSign(item.surface) && Follows(items, i) && !number.empty() && IsDigit(number.back())) {
                // A counter, a closing bracket and a noun of one character (150㎝台 150㎝대) stay attached.
                if (text::IsClosePunctuation(text::DecodeRune(nextEntry).cp)) return Result(std::move(t), false);
                if (nextKind == '1') {
                    return Result(std::move(t), !Contains(nextTags, "Ct") && text::RuneCount(next.surface) > 1);
                }
                const bool adverb = nextKind == '3' && !Contains(nextTags, "Ct");
                const bool predicate = nextKind == '4' || adverb || nextKind == '7';
                return Result(std::move(t), predicate && nextEntry.find(' ') == std::string_view::npos);
            }
            const TokenText& tok = tokens[item.tokenIndex];
            // A token of one narrow character (at most three bytes) keeps the word after it.
            const bool narrow = tok.text.size() <= 3 && WidthOf(tok.text) == 1;
            if (t == "(" || tok.span != 0 || narrow || nextKind == '2') return Result(std::move(t), false);
            // 第 read 제, the ordinal prefix, is written with the number after it, also when a word
            // comes before it in the token or the row (제19구축대, 서장 제2화, 겸 제1함대).
            if ((t == "제" || t.ends_with(" 제")) && (item.surface.ends_with("第") || Contains(tags, "Z6")) && nd(nextTags)) {
                return Result(std::move(t), false);
            }
            if (nextKind == '6') return Result(std::move(t), nd(nextTags));
            const std::string_view nextFirst = internal::EntryAt(next, 0).text;
            if (nextFirst == "(") break;
            // A verbal noun (Hd, a kanji compound or a katakana word) joins the する, なさる or できる
            // after it read with 하다 or 되다 (即答する 즉답하다, キスしたい 키스하고 싶다, 我慢できない
            // 자제할 수 없다, 購入なさる 구입하신다).
            // A compound of two or more kanji before する is a verbal noun (即答する 즉답하다),
            // unless it tells a time, a frequency or an amount, which stays an adverb apart from
            // the verb (毎日する 매일 한다, 全部します 전부 합니다), or it is a kanji doubled with 々,
            // an adverb or a な noun rather than a する noun (色々されまして 여러 가지 되어서).
            auto kanjiCompound = [](std::string_view s) {
                static constexpr std::u32string_view kTimeStarts = U"毎今明昨何半";
                static constexpr std::u32string_view kTimeEnds = U"日朝晩週年月夜時頃";
                if (dict::InClass(HDTRANS_CLASS("noun.not-verbal"), s)) return false;
                int n = 0;
                char32_t first = 0, last = 0;
                for (std::size_t k = 0; k < s.size(); n++) {
                    const text::Rune r = text::DecodeRune(s.substr(k));
                    if (r.size == 0 || !text::IsKanji(r.cp)) return false;
                    if (n == 0) first = r.cp;
                    if (n == 1 && r.cp == U'々') return false;
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
            // Its Korean must not be a verb phrase made a noun (流し読み 대강 읽기, お留守番 집 지키기).
            const bool koreanVerbalNoun = !VerbPhraseNoun(t);
            const bool verbalNoun = Contains(tags, "Hd") ||
                                    tokenizer::Class(FirstRune(item.surface)) == tokenizer::kClassKatakana ||
                                    kanjiCompound(item.surface) ||
                                    (settings_.literal && koreanVerbalNoun && stemOrHonorific(item.surface));
            if (verbalNoun && nextEntry == "&AV@" &&
                dict::InClass(HDTRANS_CLASS("suru.verbal-noun-whole"), next.surface)) {
                return Result(std::move(t), false);
            }
            // される is written with the marker of 되다; its written Korean shows whether 되다
            // follows the noun (マージされた 머지되었다, but 温存され過ぎた 온존 너무 되었다).
            const bool passive = next.surface.starts_with("さ") && nextEntry == "&C6@" &&
                                 dict::StartsWithMember(HDTRANS_CLASS("ko.doing"), remainder) > 0;
            if (verbalNoun && dict::StartsWithMember(HDTRANS_CLASS("suru.verbal-noun-start"), next.surface) > 0 &&
                (dict::StartsWithMember(HDTRANS_CLASS("ko.doing"), nextEntry) > 0 || passive)) {
                return Result(std::move(t), false);
            }
            if (nextFirst == ")") return Result(std::move(t), false);
            if (Contains(tags, "Wa") || Contains(tags, "Wb")) {
                const bool space = Contains(tags, "Wb") && nextKind != '1';
                return Result(std::move(t), space);
            }
            // A number joins a counter after it (3개); a noun written on to it was joined above
            // (JoinsNoun), so before other nouns it stands apart.
            if (IsDigit(First(t))) {
                if (text::IsClosePunctuation(text::DecodeRune(nextEntry).cp)) return Result(std::move(t), false);
                if (nextKind == '1') return Result(std::move(t), !Contains(nextTags, "Ct"));
                if (item.tokenIndex - 1 >= 0 && previous >= 0 && tokens[next.tokenIndex].type != 15 &&
                    tokens[next.tokenIndex].type != tokenizer::kTypeNumber &&
                    item.tokenIndex - items[previous].tokenIndex == 1) {
                    t.insert(0, " ");
                }
                // A number is written apart from a following one-word predicate or adverb (12 올랐다,
                // 0600 같다, 1개 주세요); a counter (3개, 10엔인) and a predicate that starts with
                // the noun it counts (3시간 들였다) stay attached.
                const bool adverb = nextKind == '3' && !Contains(nextTags, "Ct") &&
                                    !HasRune(kSinoCounters, text::DecodeRune(next.surface).cp);
                const bool predicate = nextKind == '4' || adverb || (nextKind == '7' && next.surface != "円");
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
            // Past a folded verb, a word that is written apart (Ha) still is (읽지도 않고 버렸다).
            if (after != nextIndex && Contains(GAttr(items[after]), "Ha") && !Contains(GAttr(items[after]), "Tu")) {
                return Result(std::move(t), true);
            }
            const bool space = np != '2' && np != '6' && (!IsOutputBuilderBlockedText(remainder) || WidthOf(remainder) > 4);
            return Result(std::move(t), space);
        }
        case '6':
            return Result(std::move(t), t == "," || (t == ")" && nextKind != '2' && nextKind != '6'));
    }
    return Result(std::move(t), false);
}

namespace {

// Whether a symbol token starts with an ellipsis or a mark of tone, which closes the word
// before it (그건……, 좀 더♪).
bool ClosesWord(std::string_view t) {
    return !t.empty() && std::u32string_view(U"…‥♪☆★♡♥❤❣♫♬").find(text::DecodeRune(t).cp) != std::u32string_view::npos;
}

// Whether out ends in spaces written after a syllable.
bool SpacedAfterSyllable(const Pieces& out) {
    const std::string_view t = TextTail(out);
    const size_t end = t.find_last_not_of(' ');
    return end != std::string_view::npos && end + 1 < t.size() &&
           text::Final(DecodeLastRune(TextBefore(t.substr(0, end + 1)), end + 1).cp) >= 0;
}

// Whether out ends in a word or a comma: a syllable, a run of Latin letters and digits that
// does not follow \ or % (an escape such as \n or %s), the particle mark of a word span, or ",".
bool EndsInWord(const Pieces& out) {
    if (out.empty() || (out.back().span != SpanText && out.back().span != SpanWord)) return false;
    const std::string_view t = out.back().text;
    if (t.empty()) return false;
    const char32_t r = DecodeLastRune(TextBefore(t), t.size()).cp;
    if (text::Final(r) >= 0 || r == U',' || r == kVowelMark || r == kConsonantMark || r == kRieulMark) return true;
    size_t k = t.size();
    const auto latin = [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    while (k > 0 && latin(t[k - 1])) k--;
    return k < t.size() && (k == 0 || (t[k - 1] != '\\' && t[k - 1] != '%'));
}

}  // namespace

void EmitSynchronousOutput(const std::vector<TokenText>& tokens, int first, int last, const Settings& s, Pieces& out) {
    out.clear();
    std::string text;
    for (int i = first; i <= last; i++) {
        const TokenText& tok = tokens[i];
        // A word span that a rewrite emptied writes nothing, nor the space after it.
        if (tok.span != 0 && tok.text.empty() && tok.spanText.empty()) {
            if (i + 1 <= last && tokens[i + 1].type == tokenizer::kTypeAsciiSpace) i++;
            continue;
        }
        switch (tok.type) {
            case tokenizer::kTypeSymbol:
                if (tok.span == 0 && i > first && tokens[i - 1].type != tokenizer::kTypeAsciiSpace &&
                    ClosesWord(tok.text) && SpacedAfterSyllable(out)) {
                    TrimRight(out);
                }
                break;
            case tokenizer::kTypeNumber: case tokenizer::kTypeAlpha: case 15: case 16:
            case tokenizer::kTypeJapanese: case tokenizer::kTypeProtected:
                break;
            case tokenizer::kTypeAsciiSymbol:
                // A number written with signs is a word (50%, +1) and keeps the space before it, also
                // when the space after it is written (레벨을 +1 한다).
                if (tokenizer::SignedNumber(std::string_view(tok.text).substr(0, tok.text.find_last_not_of(' ') + 1))) break;
                [[fallthrough]];
            default:
                if (tok.span == 0 && !out.empty() && i - 1 >= 0 && tokens[i - 1].type != tokenizer::kTypeAsciiSpace) {
                    TrimRight(out);
                    if (const char32_t r = tok.text.empty() ? 0 : text::DecodeRune(tok.text).cp;
                        tok.type == tokenizer::kTypeBracket && IsOpening(r) && r != U'〔' && EndsInWord(out)) {
                        Add(out, " ");
                    }
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
    if (tokens[next].span == 0) {
        const std::string_view text = tokens[next].text;
        if (dict::StartsWithMember(HDTRANS_CLASS("mark.close-after-space"), text) > 0) return;
    }
    Add(out, " ");
}

}  // namespace hdTrans::output
