#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline/protect.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/sound.h"

namespace hdTrans::pipeline::internal {

namespace {

bool In(char32_t r, std::u32string_view set) { return set.find(r) != std::u32string_view::npos; }

bool IsSmallVowel(char32_t r) { return In(r, U"ぁぃぅぇぉ"); }

bool HasKanji(std::u32string_view s) { return std::any_of(s.begin(), s.end(), IsKanjiRune); }

constexpr std::u32string_view kVowelRows[5] = {
    U"アァカガサザタダナハバパマヤャラワ",
    U"イィキギシジチヂニヒビピミリ",
    U"ウゥクグスズツヅヌフブプムユュル",
    U"エェケゲセゼテデネヘベペメレ",
    U"オォコゴソゾトドノホボポモヨョロヲ",
};

// The vowel row of a hiragana, -1 for none.
int HiraganaVowel(char32_t r) {
    if (!IsHiraganaRune(r)) return -1;
    for (int v = 0; v < 5; v++) {
        if (In(r + 0x60, kVowelRows[v])) return v;
    }
    return -1;
}

// The kana that may follow a verb stem in a conjugation.
constexpr std::u32string_view kConjKana = U"かきくけこがぎぐげごさしすせそたちつてとなにぬねのばびぶべぼまみむめもらりるれろわいうえおっん";

// The small and plain vowel kana by vowel row.
constexpr char32_t kEmphasisVowels[5][2] = {{U'ぁ', U'あ'}, {U'ぃ', U'い'}, {U'ぅ', U'う'}, {U'ぇ', U'え'}, {U'ぉ', U'お'}};

// The longest dictionary keys rewriteEmphasis looks for, in characters.
constexpr size_t kMaxKeyRunes = 12;

class Emphasis {
public:
    Emphasis(const LineContext& ctx, const dict::Snapshot& snap) : ctx_(ctx), snap_(snap) {}

    std::string Tags(std::u32string_view s) const { return KeyTags(EncodeText(s), ctx_, snap_); }

    // The length of the longest dictionary key (base or plain) that starts rs.
    size_t LongestKey(std::u32string_view rs) const {
        for (size_t n = std::min(rs.size(), kMaxKeyRunes); n > 0; n--) {
            if (!Tags(rs.substr(0, n)).empty()) return n;
        }
        return 0;
    }

    // How far (exclusive) the dictionary keys extend in rs that start at a word start
    // between from and at: the start of a run of kana and kanji, a kanji, or the
    // character before at.
    size_t Reach(std::u32string_view rs, size_t from, size_t at) const {
        size_t far = 0;
        for (size_t j = from; j < at; j++) {
            if (j > 0 && IsHiraganaRune(rs[j]) && (IsHiraganaRune(rs[j - 1]) || IsKanjiRune(rs[j - 1])) && j != at - 1) {
                continue;
            }
            far = std::max(far, j + LongestKey(rs.substr(j)));
        }
        return far;
    }

    // Whether a key with a tag of tags ends at character at of rs and starts at or
    // after from, at the start of a run of kana and kanji, after a て form or with a
    // kanji.
    bool StemTagged(std::u32string_view rs, size_t from, size_t at, std::string_view tags) const {
        for (size_t j = from; j < at; j++) {
            if (j > 0 && (IsHiraganaRune(rs[j - 1]) || IsKanjiRune(rs[j - 1])) && !In(rs[j - 1], U"てで") &&
                !HasKanji(rs.substr(j, at - j))) {
                continue;
            }
            if (Tags(rs.substr(j, at - j)).find_first_of(tags) != std::string::npos) return true;
        }
        return false;
    }

    // A verb or adjective stem key that ends at character at of rs and starts in the
    // word before it.
    bool StemEnds(std::u32string_view rs, size_t from, size_t at) const { return StemTagged(rs, from, at, "MH"); }

    // A verb or adjective stem of three or more characters that ends at character at
    // of rs and starts at or after from (うるさ in うるさ～い).
    bool LongStem(std::u32string_view rs, size_t from, size_t at) const {
        for (size_t j = from; j + 3 <= at; j++) {
            if (Tags(rs.substr(j, at - j)).find_first_of("MH") != std::string::npos) return true;
        }
        return false;
    }

    // A dictionary key of two or more characters, or with a kanji, that ends at
    // character at of rs and starts at or after from, and no key of the sound lexicon
    // that does (どしゃ).
    bool WordEnds(std::u32string_view rs, size_t from, size_t at) const {
        bool word = false;
        for (size_t j = from; j < at; j++) {
            if (LexiconKey(ToKatakana(rs.substr(j, at - j)), ctx_.extraBuiltin)) return false;
            word = word || ((at - j >= 2 || IsKanjiRune(rs[j])) && !Tags(rs.substr(j, at - j)).empty());
        }
        return word;
    }

    static bool LexiconKey(std::u32string_view key, bool extra) {
        if (key.empty()) return false;
        for (const SoundRow& s : SoundRows(key[0], extra)) {
            if (s.key == key) return true;
        }
        return false;
    }

private:
    const LineContext& ctx_;
    const dict::Snapshot& snap_;
};

// A ー or ～ at character i that lengthens the volitional ending before it (いこー,
// やろー, しよー, 食べよー).
bool Volitional(std::u32string_view rs, size_t i) {
    if ((rs[i] != U'ー' && rs[i] != U'～') || i < 2) return false;
    const char32_t prev = rs[i - 1];
    if (In(prev, U"こごそぼ")) return true;
    if (prev == U'ろ') {
        const int v = HiraganaVowel(rs[i - 2]);
        return v != 1 && v != 3;  // やろー, 帰ろー; not the imperative やめろー, 見ろー
    }
    if (prev == U'よ') return rs[i - 2] == U'し' || HiraganaVowel(rs[i - 2]) == 3;
    return false;
}

// A key of the sound lexicon that starts between from and i, holds the mark at
// character i and goes past it (おーい, えーっと).
bool SoundCovers(std::u32string_view rs, size_t from, size_t i, bool extra) {
    const std::u32string f = ToKatakana(rs);
    std::string units;
    for (size_t j = from; j < i; j++) {
        for (const SoundRow& s : SoundRows(f[j], extra)) {
            size_t end = 0;
            if (MatchSound(s, f, j, end, units) && end > i + 1 && s.key.find(f[i]) != std::u32string::npos) return true;
        }
    }
    return false;
}

// Text that starts with a familiar honorific, which follows a lengthened nickname
// (うーちゃん, ふーちゃん).
bool Nickname(std::u32string_view text) {
    for (const std::u32string_view h : {U"ちゃん", U"くん", U"たん", U"さん", U"さま", U"っち"}) {
        if (text.starts_with(h)) return true;
    }
    return false;
}

// A key of the sound lexicon that is the text from the start of a run of kana and
// kanji at or after from to end: the mark belongs to an interjection (まぁいいんじゃない,
// さぁて).
bool SoundWord(std::u32string_view rs, size_t from, size_t end, bool extra) {
    for (size_t j = from; j < end; j++) {
        if (j > 0 && (IsHiraganaRune(rs[j - 1]) || IsKanjiRune(rs[j - 1]))) continue;
        if (Emphasis::LexiconKey(ToKatakana(rs.substr(j, end - j)), extra)) return true;
    }
    return false;
}

std::u32string Spliced(std::u32string_view rs, size_t i, std::u32string_view c, size_t end) {
    std::u32string m;
    m.reserve(rs.size() + c.size());
    m.append(rs.substr(0, i)).append(c).append(rs.substr(end));
    return m;
}

constexpr std::u32string_view kKanaColumns[] = {U"アイウエオ", U"カキクケコ", U"ガギグゲゴ", U"サシスセソ", U"ザジズゼゾ",
                                                U"タチツテト", U"ダヂヅデド", U"ナニヌネノ", U"ハヒフヘホ", U"バビブベボ",
                                                U"パピプペポ", U"マミムメモ", U"ラリルレロ"};

// The hiragana of the column of kana in vowel row v, 0 for none.
char32_t WithVowel(char32_t kana, int v) {
    const char32_t k = kana + 0x60;
    for (const auto g : kKanaColumns) {
        if (In(k, g)) return g[v] - 0x60;
    }
    return 0;
}

bool InCasualStem(char32_t r) { return IsHiraganaRune(r) || (r >= U'一' && r <= U'龥') || r == U'々'; }

// A match of the casual adjective ending at rs[s:]: up to four characters of stem
// (fewest first), the kana of the ending, a run of ぇぃー～ ending at runEnd, and the
// character after it (a sentence particle or a character other than hiragana, ー and
// ～) or the end of the text, ending at end. false when no match starts at s.
bool CasualAdjectiveAt(std::u32string_view rs, size_t s, size_t& stemLen, size_t& runEnd, size_t& end) {
    for (size_t k = 0; k <= 4 && s + k < rs.size(); k++) {
        if (k > 0 && !InCasualStem(rs[s + k - 1])) return false;
        const bool recovered = rs[s + k] == U'ね' && rs.substr(s, k) == U"危";
        if (!In(rs[s + k], U"えけげせぜてでべめれりきち") && !recovered) continue;
        size_t q = s + k + 1;
        while (q < rs.size() && In(rs[q], recovered ? U"ぇぃー～え" : U"ぇぃー～")) q++;
        if (q == s + k + 1) continue;
        if (q < rs.size() && !In(rs[q], U"なよぞしかぜわっ") && (IsHiraganaRune(rs[q]) || rs[q] == U'ー' || rs[q] == U'～')) {
            continue;
        }
        stemLen = k;
        runEnd = q;
        end = q < rs.size() ? q + 1 : q;
        return true;
    }
    return false;
}

// The casual adjective of a match, written in full, or false.
bool FullAdjective(const Emphasis& e, std::u32string_view stem, char32_t kana, std::u32string& out) {
    const size_t n = stem.size();
    if (kana == U'ぜ' && stem != U"う") return false;  // the particle ぜ: 歌おうぜぇ (うぜぇ is うざい)
    if ((n > 0 && In(stem[n - 1], U"っん")) || (In(kana, U"てで") && n > 0 && !IsKanjiRune(stem[n - 1]) && stem != U"い")) {
        return false;  // a て form: 当たってぇ, 届いてぇ, いたしまして～ (冷てぇ, いてぇ are adjectives)
    }
    int rows[2] = {-1, -1};
    switch (HiraganaVowel(kana)) {
        case 3: rows[0] = 0, rows[1] = 4; break;  // -ai, -oi
        case 1: rows[0] = 2; break;               // -ui
        default: break;
    }
    for (size_t k = 0; k < n; k++) {
        if (k > 0 && (IsHiraganaRune(stem[k - 1]) || IsKanjiRune(stem[k - 1]))) continue;  // inside a word: おのれぇ
        for (const int row : rows) {
            if (row < 0) continue;
            const char32_t r = WithVowel(kana, row);
            if (r == 0) continue;
            std::u32string key(stem.substr(k));
            key.push_back(r);
            if (e.Tags(key).find('H') != std::string::npos) {
                out.assign(stem);
                out.push_back(r);
                out.push_back(U'い');
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::string RewriteCasualAdjectives(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    const std::u32string rs = DecodeText(text);
    const Emphasis e(ctx, snap);
    std::u32string out, full;
    bool changed = false;
    size_t at = 0;
    for (size_t s = 0; s < rs.size();) {
        size_t stemLen = 0, runEnd = 0, end = 0;
        if (!CasualAdjectiveAt(rs, s, stemLen, runEnd, end)) {
            s++;
            continue;
        }
        if (FullAdjective(e, std::u32string_view(rs).substr(s, stemLen), rs[s + stemLen], full)) {
            out.append(rs, at, s - at);
            out += full;
            out.append(rs, runEnd, end - runEnd);
            at = end;
            changed = true;
        }
        s = end;
    }
    if (!changed) return std::string(text);
    out.append(rs, at, std::u32string::npos);
    return EncodeText(out);
}

std::string RewriteEmphasis(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    std::u32string rs = DecodeText(text);
    const Emphasis e(ctx, snap);
    bool changed = false;
    for (size_t i = 1; i < rs.size(); i++) {
        const char32_t prev = rs[i - 1];
        if ((!IsHiraganaRune(prev) && !IsKanjiRune(prev)) || prev == U'っ') continue;
        size_t from = i - 1;
        while (from > 0 && i - from < 4 && (IsHiraganaRune(rs[from - 1]) || IsKanjiRune(rs[from - 1]))) from--;
        std::vector<std::u32string_view> cands;
        size_t end = i + 1;
        const int v = HiraganaVowel(prev);
        const char32_t r = rs[i];
        if (r == U'っ') {
            if (end >= rs.size() || !IsHiraganaRune(rs[end]) || In(rs[end], U"てたちつっーゃゅょぁぃぅぇぉす") || prev == U'う' ||
                (rs[end] == U'し' && end + 1 < rs.size() && In(rs[end + 1], U"ゃゅょ")) || (prev == U'た' && rs[end] == U'け')) {
                continue;
            }
            cands = {U""};
        } else if (r == U'ー' || r == U'～' || IsSmallVowel(r) ||
                   (v >= 0 && r == kEmphasisVowels[v][1] && r != prev && i + 1 < rs.size() &&
                    (rs[i + 1] == kEmphasisVowels[v][0] || rs[i + 1] == r))) {
            if ((IsSmallVowel(r) && v >= 0 && r != kEmphasisVowels[v][0]) || In(prev, U"あいうえお")) {
                continue;  // ああっ, あぁすみません: the lengthened interjection
            }
            const bool plainStart = !IsSmallVowel(r) && r != U'ー' && r != U'～';
            bool small = IsSmallVowel(r) || plainStart;
            while (end < rs.size()) {
                const char32_t c = rs[end];
                if (c == U'ー' || c == U'～' || (IsSmallVowel(c) && (v < 0 || c == kEmphasisVowels[v][0])) ||
                    (small && ((v < 0 && In(c, U"あいうえお")) || (v >= 0 && c == kEmphasisVowels[v][1])))) {
                    small = small || IsSmallVowel(c);
                    end++;
                    continue;
                }
                break;
            }
            if (end >= rs.size() || !IsHiraganaRune(rs[end])) {
                if (Volitional(rs, i) && e.StemEnds(rs, from, i - 1) && e.Reach(rs, from, i - 1) < i) {
                    rs = Spliced(rs, i, U"う", end);
                    changed = true;
                } else if (end < rs.size() && In(rs[end], U"ッっ！？!?") && end - i >= 2 &&
                           std::any_of(rs.begin() + i, rs.begin() + end, IsSmallVowel) && e.Reach(rs, from, i) <= i &&
                           !e.StemTagged(rs, from, i, "H") &&  // おいしぃ～: the い of an adjective
                           !In(prev, U"あいうえおぁぃぅぇぉ") && e.WordEnds(rs, from, i)) {
                    rs = Spliced(rs, i, U"", end);  // a shout: そりゃああぁあッ, 何ぃいいぃッ
                    changed = true;
                    i--;
                }
                continue;
            }
            if (plainStart) continue;
            if (IsSmallVowel(rs[end]) ||
                (rs[end] == U'っ' && (end + 1 >= rs.size() || !IsHiraganaRune(rs[end + 1]) || rs[end + 1] == U'て')) ||
                (In(prev, U"かなねさわ") && rs[end] != U'ん' && !e.LongStem(rs, from, i)) ||  // a particle: お花見か～いいわね
                In(rs[end], U"おご") || Nickname(std::u32string_view(rs).substr(end))) {
                continue;  // うーちゃん, むーお姉ちゃん
            }
            cands = {U""};
            if (v == 1) cands.push_back(U"い");
            if (v == 2 || v == 4) cands.push_back(U"う");
        } else {
            continue;
        }
        if (rs[i] != U'っ' && rs[end] == U'い' && e.StemTagged(rs, from, i, "H")) cands.resize(1);  // すごーい, おそーい
        const size_t before = e.Reach(rs, from, i);
        if (before > end || SoundCovers(rs, from, i, ctx.extraBuiltin) || SoundWord(rs, from, end, ctx.extraBuiltin)) continue;  // もっと, せーの, おーい
        int best = -1;
        size_t bestReach = std::max(before, end);
        for (size_t k = 0; k < cands.size(); k++) {
            const std::u32string_view c = cands[k];
            const std::u32string m = Spliced(rs, i, c, end);
            size_t reach = e.Reach(m, from, i);
            if (reach <= i + c.size()) continue;
            reach += end - i - c.size();
            if (reach > bestReach) {
                best = static_cast<int>(k);
                bestReach = reach;
            }
        }
        if (best < 0 && In(rs[end], kConjKana) &&
            ((rs[i] == U'っ' && IsKanjiRune(prev) && In(rs[end], U"かきくけこがぎぐげご")) ||
             (rs[i] != U'っ' && (IsKanjiRune(prev) || e.StemEnds(rs, from, i))))) {
            best = 0;
        }
        if (best < 0) continue;
        rs = Spliced(rs, i, cands[best], end);
        changed = true;
    }
    if (!changed) return std::string(text);
    return EncodeText(rs);
}

}  // namespace hdTrans::pipeline::internal
