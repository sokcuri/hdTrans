#include "core/text/kana.h"

#include <optional>
#include <vector>

#include "core/text/hangul.h"
#include "core/text/utf8.h"

namespace hdTrans::text {
namespace {

// One kana unit: a consonant class and a vowel. cons is "Q" for っ, "N" for ん,
// "L" for ー; vowel is empty for those.
struct Mora {
    std::string_view cons;
    std::string vowel;
};

struct KanaRow {
    std::string_view cons;
    char32_t kana[5];
};

constexpr std::string_view kRowVowels[5] = {"a", "i", "u", "e", "o"};

constexpr KanaRow kKanaRows[] = {
    {"", {U'あ', U'い', U'う', U'え', U'お'}}, {"k", {U'か', U'き', U'く', U'け', U'こ'}},
    {"g", {U'が', U'ぎ', U'ぐ', U'げ', U'ご'}}, {"s", {U'さ', U'し', U'す', U'せ', U'そ'}},
    {"z", {U'ざ', U'じ', U'ず', U'ぜ', U'ぞ'}}, {"t", {U'た', U'ち', U'つ', U'て', U'と'}},
    {"d", {U'だ', U'ぢ', U'づ', U'で', U'ど'}}, {"n", {U'な', U'に', U'ぬ', U'ね', U'の'}},
    {"h", {U'は', U'ひ', U'ふ', U'へ', U'ほ'}}, {"b", {U'ば', U'び', U'ぶ', U'べ', U'ぼ'}},
    {"p", {U'ぱ', U'ぴ', U'ぷ', U'ぺ', U'ぽ'}}, {"m", {U'ま', U'み', U'む', U'め', U'も'}},
    {"r", {U'ら', U'り', U'る', U'れ', U'ろ'}},
};

struct KanaException {
    char32_t kana;
    std::string_view cons;
    std::string_view vowel;
};

// Kana whose mora differs from its row, and kana outside the rows.
constexpr KanaException kKanaExceptions[] = {
    {U'し', "sh", "i"}, {U'じ', "j", "i"}, {U'ち', "ch", "i"}, {U'つ', "ts", "u"}, {U'ぢ', "j", "i"},
    {U'づ', "z", "u"},  {U'ふ', "f", "u"}, {U'や', "", "ya"},  {U'ゆ', "", "yu"},  {U'よ', "", "yo"},
    {U'わ', "", "wa"},  {U'を', "", "o"},  {U'ゐ', "", "i"},   {U'ゑ', "", "e"},   {U'ゔ', "v", "u"},
};

std::optional<Mora> KanaMora(char32_t r) {
    for (const auto& e : kKanaExceptions) {
        if (e.kana == r) return Mora{e.cons, std::string(e.vowel)};
    }
    for (const auto& row : kKanaRows) {
        for (int i = 0; i < 5; i++) {
            if (row.kana[i] == r) return Mora{row.cons, std::string(kRowVowels[i])};
        }
    }
    return std::nullopt;
}

// The vowel of a small ゃ ゅ ょ, "" for other characters.
std::string_view SmallY(char32_t r) {
    switch (r) {
        case U'ゃ': return "ya";
        case U'ゅ': return "yu";
        case U'ょ': return "yo";
    }
    return {};
}

// The vowel of a small ぁ ぃ ぅ ぇ ぉ, "" for other characters.
std::string_view SmallV(char32_t r) {
    switch (r) {
        case U'ぁ': return "a";
        case U'ぃ': return "i";
        case U'ぅ': return "u";
        case U'ぇ': return "e";
        case U'ぉ': return "o";
    }
    return {};
}

// The code points of s with katakana ァ..ヶ mapped to hiragana.
std::u32string ToHiragana(std::string_view s) {
    std::u32string out;
    while (!s.empty()) {
        const Rune r = DecodeRune(s);
        s.remove_prefix(r.size);
        out.push_back(r.cp >= U'ァ' && r.cp <= U'ヶ' ? r.cp - 0x60 : r.cp);
    }
    return out;
}

// Splits kana into morae; false when s is empty or holds anything else.
bool Morae(std::string_view s, std::vector<Mora>& out) {
    const std::u32string rs = ToHiragana(s);
    for (size_t i = 0; i < rs.size(); i++) {
        const char32_t next = i + 1 < rs.size() ? rs[i + 1] : 0;
        switch (rs[i]) {
            case U'っ': out.push_back({"Q", ""}); continue;
            case U'ん': out.push_back({"N", ""}); continue;
            case U'ー': out.push_back({"L", ""}); continue;
        }
        auto found = KanaMora(rs[i]);
        if (!found) {
            out.clear();
            return false;
        }
        Mora m = *found;
        if (const auto y = SmallY(next); !y.empty()) {
            m.vowel = y;
            i++;
        } else if (const auto v = SmallV(next); !v.empty()) {
            if (m.cons.empty() && m.vowel == "u" && v != "u") {  // ウィ ウェ ウォ
                m.vowel = "w" + std::string(v);
            } else if (m.cons == "f") {  // ファ フィ
                m.cons = "fv";
                m.vowel = v;
            } else {
                m.vowel = v;
            }
            i++;
        }
        out.push_back(m);
    }
    return !out.empty();
}

int VowelJamo(std::string_view v) {
    struct Entry {
        std::string_view vowel;
        int jamo;
    };
    static constexpr Entry kTable[] = {
        {"a", 0},   {"i", 20},  {"u", 13},  {"e", 5},   {"o", 8},   {"ya", 2},  {"yu", 17},
        {"yo", 12}, {"wa", 9},  {"wi", 16}, {"we", 15}, {"wo", 14}, {"ye", 7},
    };
    for (const auto& e : kTable) {
        if (e.vowel == v) return e.jamo;
    }
    return 0;
}

constexpr int kFinalN = 4;
constexpr int kFinalS = 19;

int Onset(std::string_view cons, bool initial, Preset p) {
    const bool aspirate = p != Preset::Official || !initial;
    if (cons == "k") return aspirate ? 15 : 0;
    if (cons == "g") return 0;
    if (cons == "s" || cons == "sh") return 9;
    if (cons == "z" || cons == "j") return 12;
    if (cons == "t") return aspirate ? 16 : 3;
    if (cons == "d") return 3;
    if (cons == "ch") return aspirate ? 14 : 12;
    if (cons == "ts") return p == Preset::Official ? 10 : 14;
    if (cons == "n") return 2;
    if (cons == "h" || cons == "f") return 18;
    if (cons == "fv" || cons == "p") return 17;
    if (cons == "b" || cons == "v") return 7;
    if (cons == "m") return 6;
    if (cons == "r") return 5;
    return 11;
}

// Whether vowel after prev is a long vowel that p does not write.
bool LongVowel(std::string_view prev, std::string_view vowel, Preset p) {
    const bool ou = ((prev == "o" || prev == "yo") && (vowel == "u" || vowel == "o")) ||
                    ((prev == "u" || prev == "yu") && vowel == "u");
    switch (p) {
        case Preset::FanShort: return ou;
        case Preset::Official: return ou || (prev == vowel && (vowel == "a" || vowel == "i" || vowel == "e"));
        case Preset::Fan: break;
    }
    return false;
}

}  // namespace

std::string FromKana(std::string_view reading, Preset p, bool singleLongPair) {
    std::vector<Mora> ms;
    if (!Morae(reading, ms)) return "";
    std::vector<Syllable> out;
    std::string prev;
    bool shortenedPair = false;
    for (const auto& m : ms) {
        if (m.cons == "Q" || m.cons == "N") {
            if (!out.empty() && out.back().final == 0) {
                out.back().final = m.cons == "N" ? kFinalN : kFinalS;
            }
            continue;
        }
        if (m.cons == "L") continue;
        if (m.cons.empty() && !out.empty() && out.back().final == 0 && LongVowel(prev, m.vowel, p) &&
            !(singleLongPair && p == Preset::FanShort && shortenedPair)) {
            shortenedPair = true;
            continue;
        }
        shortenedPair = false;
        int v = VowelJamo(m.vowel);
        const bool yVowel = m.vowel.size() == 2 && m.vowel[0] == 'y';
        if ((m.cons == "j" || m.cons == "ch") && yVowel && p != Preset::Official) {
            // 쟈 챠 keep the y vowel
        } else if ((m.cons == "j" || m.cons == "ch" || m.cons == "z") && yVowel) {
            v = VowelJamo(std::string_view(m.vowel).substr(1));
        } else if (m.cons == "sh" && m.vowel == "e") {
            v = 7;
        } else if ((m.cons == "ts" || m.cons == "s" || m.cons == "z") && m.vowel == "u") {
            v = 18;
        }
        out.push_back({Onset(m.cons, out.empty(), p), v, 0});
        prev = m.vowel;
    }
    std::string b;
    for (const auto& s : out) AppendRune(b, Join(s.initial, s.vowel, s.final));
    return b;
}

}  // namespace hdTrans::text
