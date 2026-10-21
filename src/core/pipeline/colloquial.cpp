#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline/protect.h"
#include "core/pipeline/restore.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/sound.h"

namespace hdTrans::pipeline::internal {

namespace {

bool In(char32_t r, std::u32string_view set) { return set.find(r) != std::u32string_view::npos; }

bool IsSmallVowel(char32_t r) { return In(r, U"ぁぃぅぇぉ"); }

bool HasKanji(std::u32string_view s) { return std::any_of(s.begin(), s.end(), IsKanjiRune); }

bool SpelledTail(std::u32string_view text);

// Whether text is one of words.
bool In(std::u32string_view text, std::initializer_list<std::u32string_view> words) {
    for (const std::u32string_view w : words) {
        if (text == w) return true;
    }
    return false;
}

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

    // Whether the longest key that ends at character at of rs and starts at or after from
    // is a verb or adjective stem and no noun (痛, not 提督).
    bool EndsInStem(std::u32string_view rs, size_t from, size_t at) const {
        for (size_t j = from; j < at; j++) {
            const std::string tags = Tags(rs.substr(j, at - j));
            if (!tags.empty()) return tags.find_first_of("MH") != std::string::npos && tags.find('N') == std::string::npos;
        }
        return false;
    }

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

    // Whether a dictionary key of min or more characters, a kana spelling included, is the
    // text from a word start between from and at - 1 to at with tail after it (せんせ + い).
    bool WordWithEnds(std::u32string_view rs, size_t from, size_t at, std::u32string_view tail, size_t min = 2) const {
        for (size_t j = from; j + 1 < at; j++) {
            if (at - j + tail.size() < min) break;
            if (j > 0 && (IsHiraganaRune(rs[j - 1]) || IsKanjiRune(rs[j - 1]))) continue;
            std::u32string w(rs.substr(j, at - j));
            w += tail;
            const std::string key = EncodeText(w);
            const dict::Candidates rd = snap_.GetWordInfo(key, dict::LookupMode::General, ctx_.lookup);
            for (const dict::Candidate& c : rd.list) {
                if (c.source.size() == key.size()) return true;
            }
        }
        return false;
    }

    // How far (exclusive) the dictionary keys, kana spellings included, extend in rs that
    // start at a word start between from and at.
    size_t KanaReach(std::u32string_view rs, size_t from, size_t at) const {
        size_t far = 0;
        for (size_t j = from; j < at; j++) {
            if (j > 0 && (IsHiraganaRune(rs[j - 1]) || IsKanjiRune(rs[j - 1]))) continue;
            const std::string text = EncodeText(rs.substr(j, std::min(rs.size() - j, kMaxKeyRunes)));
            const dict::Candidates rd = snap_.GetWordInfo(text, dict::LookupMode::General, ctx_.lookup);
            for (const dict::Candidate& c : rd.list) far = std::max(far, j + text::RuneCount(c.source));
        }
        return far;
    }

    // Whether key is a name: a record of the name layer with a person's sense (QPN), with
    // anyName any record of the name layer, or a candidate of the name layer or with a
    // person's sense that is exactly key.
    bool NameKey(std::u32string_view key, bool anyName = false) const {
        const std::string text = EncodeText(key);
        for (const dict::Record& r : snap_.NameRecords(text)) {
            if (anyName || (r.codes != nullptr && r.codes->sem.Contains("QPN"))) return true;
        }
        const dict::Candidates rd = snap_.GetWordInfo(text, dict::LookupMode::General, ctx_.lookup);
        for (const dict::Candidate& c : rd.list) {
            if (c.source.size() != text.size()) continue;
            if ((c.user != nullptr && c.user->nameLayer) || (c.codes != nullptr && c.codes->sem.Contains("QPN"))) return true;
        }
        return false;
    }

    // The longest key at rs[p:] other than a name, kana spellings included, in characters,
    // and whether a key of that length is a verb or adjective stem.
    size_t WordAt(std::u32string_view rs, size_t p, bool& stem) const {
        const std::string text = EncodeText(rs.substr(p, std::min(rs.size() - p, kMaxKeyRunes)));
        const dict::Candidates rd = snap_.GetWordInfo(text, dict::LookupMode::General, ctx_.lookup);
        size_t best = 0;
        stem = false;
        for (const dict::Candidate& c : rd.list) {
            if (c.user != nullptr && c.user->nameLayer) continue;
            if (c.codes != nullptr && c.codes->sem.Contains("QPN")) continue;
            const size_t n = text::RuneCount(c.source);
            const bool s = c.codes != nullptr && (c.codes->part.Tag() == 'M' || c.codes->part.Tag() == 'H');
            if (n > best) {
                best = n;
                stem = s;
            } else if (n == best) {
                stem = stem || s;
            }
        }
        return best;
    }

    // Whether the dictionary reads all of w as words: keys other than names and single
    // particles one after the other, the last of them a verb or adjective stem before its
    // ending or followed by a particle (どんなかんじ, あげる, ふざけないで, いいけど); a
    // lengthening at the end is left out (しゅつげきぃ).
    bool ReadsAll(std::u32string_view w) const {
        while (!w.empty() && In(w.back(), U"ぁぃぅぇぉっーッ～")) w.remove_suffix(1);  // しゅつげきぃ, おはよ～
        for (size_t p = 0; p < w.size();) {
            bool stem = false;
            const size_t n = WordAt(w, p, stem);
            if (n >= 2 || (n == 1 && IsKanjiRune(w[p]))) {
                if (stem) return true;
                p += n;
                continue;
            }
            if (n == 1 && stem) return true;  // し + てたら, し + よっか
            if (In(w[p], U"にをがはもでとのへや")) {  // a particle: 誰にしよっか
                p++;
                continue;
            }
            return SpelledTail(w.substr(p)) || In(w.substr(p), {U"けど", U"な", U"わ", U"ぞ", U"さ"});
        }
        return true;
    }

    // Whether a key other than a name, kana spellings included, is exactly w.
    bool WordKey(std::u32string_view w) const {
        bool stem = false;
        return WordAt(w, 0, stem) == w.size();
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

// Whether the run of kana and kanji that ends at character i, from its start (at most
// twelve characters back), with or without the lengthening run up to end, is a key of the
// sound lexicon (ふぅー, うふふー).
bool SoundRun(std::u32string_view rs, size_t i, size_t end, bool extra) {
    size_t start = i;
    while (start > 0 && i - start < kMaxKeyRunes && (IsHiraganaRune(rs[start - 1]) || IsKanjiRune(rs[start - 1]))) start--;
    return SoundWord(rs, start, i, extra) || SoundWord(rs, start, end, extra);
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

// The end of a piece of a word spelled out with ・ at rs[p:], or p: a kana or kanji (っ not
// first), a small kana that ends its syllable, and one ー, ～ or っ.
size_t SpelledPiece(std::u32string_view rs, size_t p, bool first) {
    if (p >= rs.size()) return p;
    const char32_t c = rs[p];
    const bool small = In(c, U"ぁぃぅぇぉゃゅょゎァィゥェォャュョヮ");
    if ((!IsHiraganaRune(c) && !IsKatakanaRune(c) && !IsKanjiRune(c)) || c == U'ー' || small ||
        (first && In(c, U"っッ"))) {
        return p;
    }
    size_t q = p + 1;
    if (q < rs.size() && In(rs[q], U"ぁぃぅぇぉゃゅょゎァィゥェォャュョヮ")) q++;
    if (q < rs.size() && In(rs[q], U"ー～っッ")) q++;
    return q;
}

std::u32string ToHiragana(std::u32string_view rs) {
    std::u32string out(rs);
    for (char32_t& r : out) {
        if (r >= U'ァ' && r <= U'ヶ') r -= 0x60;
    }
    return out;
}

// Whether text starts with a copula or a particle that may follow a word spelled out with
// ・ (さ・い・て・いです, 大・好・きだよ).
bool SpelledTail(std::u32string_view text) {
    for (const std::u32string_view t :
         {U"です", U"でした", U"でしょ", U"だ", U"じゃ", U"って", U"よ", U"ね", U"なの", U"の", U"か"}) {
        if (text.starts_with(t)) return true;
    }
    return false;
}

// Whether the run of single kana pieces at rs[i:end] (・ between them) follows the order of
// the kana table or of the iroha, three pieces or more: a list (あ・い・う, は・ひ・ふ・へ・ほ,
// い・ろ・は).
bool KanaOrder(std::u32string_view rs, size_t i, size_t end) {
    std::u32string kana;
    for (size_t k = i; k < end; k++) {
        if (rs[k] == U'・') continue;
        if (k + 1 < end && rs[k + 1] != U'・') return false;  // a piece of more than one character
        kana.push_back(rs[k]);
    }
    if (kana.size() < 3) return false;
    for (const std::u32string_view order :
         {std::u32string_view(U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"),
          std::u32string_view(U"あかさたなはまやらわ"),
          std::u32string_view(U"いろはにほへとちりぬるをわかよたれそつねならむうゐのおくやまけふこえてあさきゆめみしゑひもせす")}) {
        bool follows = true;
        for (size_t k = 0; k + 1 < kana.size() && follows; k++) {
            const size_t at = order.find(kana[k]);
            follows = at != std::u32string_view::npos && at + 1 < order.size() && order[at + 1] == kana[k + 1];
        }
        if (follows) return true;
    }
    return false;
}

// Whether the run at rs[i] follows a label of a list or a question (①あ・い, 問１　あ・い・う,
// (1) あ・い): a digit, a circled number, 問 or a closing bracket or period after a number.
bool AfterLabel(std::u32string_view rs, size_t i) {
    size_t k = i;
    while (k > 0 && (rs[k - 1] == U' ' || rs[k - 1] == U'　')) k--;
    if (k == 0) return false;
    const char32_t c = rs[k - 1];
    return (c >= U'0' && c <= U'9') || (c >= U'０' && c <= U'９') || (c >= 0x2460 && c <= 0x24FF) ||
           (c >= 0x3220 && c <= 0x3243) || c == U'問' || In(c, U")）.．:：");
}

bool IsWordRune(char32_t r) { return IsHiraganaRune(r) || IsKatakanaRune(r) || IsKanjiRune(r) || r == U'・'; }

// A character that ends a spoken phrase: punctuation, a closing bracket, a space, a mark
// of tone, or a line break.
bool IsPause(char32_t r) {
    return In(r, U"、。，．,.！？!?…‥」』）)］】♪☆★♥♡❤～~ 　\n") || r == kLengthenMark || r == kEmphasisClose;
}

// The ーs and ぇs of the e row, at least one ー, of rs[i:end] (てぇー, せー).
bool LongE(std::u32string_view rs, size_t i, size_t end) {
    bool dash = false;
    for (size_t k = i; k < end; k++) {
        if (rs[k] != U'ー' && rs[k] != U'ぇ') return false;
        dash = dash || rs[k] == U'ー';
    }
    return dash;
}

// The kana after which a drawn small vowel is dropped (ていとくぅ, ないでぇ, なのぉ): the word
// stage reads them only without it.
constexpr std::u32string_view kDrawlKana = U"てでのるだくぐよ";

// Whether the ね at rs[k] is the sentence particle and not the casual negative (知らねー,
// じゃねー): after だ よ の す る う, after わ of a sentence end (いいわね), after た
// that does not end a kanji stem (疲れたね, not 勝たね), after an adjective's い (いいね,
// 眠いね, not 誰もいね), after のか, and after ごめん, さん, ちゃん, くん.
bool ParticleNe(std::u32string_view rs, size_t k) {
    if (k < 2) return false;
    const char32_t p = rs[k - 1], q = rs[k - 2];
    if (In(p, U"だよのするう")) return true;
    if (p == U'わ') return In(q, U"いるただすよ");
    if (p == U'た') return !IsKanjiRune(q);
    if (p == U'い') return In(q, U"いしたなく") || IsKanjiRune(q);
    if (p == U'か') return q == U'の';
    if (p == U'ん') return In(q, U"めさゃく");
    return false;
}

// Whether a lengthening run at rs[i:] that ends a phrase may be written as ～ after the
// word before it: after a kanji that ends no verb or adjective stem (提督ー), or after
// hiragana in a run of two or more characters, except a kana of the e row other than
// the て of a te form or an imperative (待ってー, 助けてー, 待てー; not the casual たい of
// やりてー), the で of a te form (読んでー, ないでー) and the particle ね (casual forms such
// as やべー, 知らねー stay); not the particle さ, nor な after る or て, which reads as a
// prohibition before ～.
bool LengthensWord(const Emphasis& e, std::u32string_view rs, size_t from, size_t i) {
    const char32_t prev = rs[i - 1];
    if (IsKanjiRune(prev)) return !e.EndsInStem(rs, from, i);
    if (IsSmallVowel(prev) || i < 2 || (!IsHiraganaRune(rs[i - 2]) && !IsKanjiRune(rs[i - 2])) || IsSmallVowel(rs[i - 2])) {
        return false;  // a lone kana: ほー, よー, ぃやぁ, すぅー
    }
    if (prev == U'さ') return false;                       // 提督さぁー: the particle さ
    if (prev == U'な' && In(rs[i - 2], U"るて")) return false;  // あげるなー, やめてなー: a request or a prohibition
    if (HiraganaVowel(prev) != 3) return true;
    if (prev == U'て') {
        const char32_t q = rs[i - 2];
        return In(q, U"っん") || HiraganaVowel(q) == 3 || IsKanjiRune(q);  // not the casual たい: やりてー
    }
    if (prev == U'で') {
        const char32_t q = rs[i - 2];
        return q == U'ん' || (q == U'い' && i >= 3 && rs[i - 3] == U'な');
    }
    return prev == U'ね' && ParticleNe(rs, i - 1);
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
                } else if (!plainStart && (end >= rs.size() || IsPause(rs[end])) &&
                           std::any_of(rs.begin() + i, rs.begin() + end, [](char32_t c) { return c == U'ー'; }) &&
                           !SoundRun(rs, i, end, ctx.extraBuiltin)) {
                    if (v == 3 && LongE(rs, i, end) && !In(prev, U"てでね") && !e.WordWithEnds(rs, from, i, U"") &&
                        e.WordWithEnds(rs, from, i, U"い", 4)) {
                        rs = Spliced(rs, i, U"い", end);  // the long vowel spelled: せんせー せんせい
                        changed = true;
                    } else if (LengthensWord(e, rs, from, i) &&
                               !(prev == U'て' && rs[i - 2] == U'っ' && end < rs.size() && In(rs[end], U"？?"))) {  // 作るってー？
                        // 提督ー 提督～, 待ってー 待って～, だなぁー だなぁ～ (a particle keeps its vowel)
                        size_t keep = i;
                        if (!IsKanjiRune(prev) && !In(prev, kDrawlKana)) {
                            while (keep < end && IsSmallVowel(rs[keep])) keep++;
                        }
                        rs = Spliced(rs, keep, U"～", end);
                        changed = true;
                    }
                } else if (!plainStart && (end >= rs.size() || IsPause(rs[end])) && In(prev, kDrawlKana) &&
                           !(prev == U'て' && i >= 3 && rs[i - 2] == U'っ' && rs[i - 3] == U'だ') &&  // 何だってぇ
                           std::all_of(rs.begin() + i, rs.begin() + end, [&](char32_t c) { return c == kEmphasisVowels[v][0]; }) &&
                           !SoundRun(rs, i, end, ctx.extraBuiltin) && LengthensWord(e, rs, from, i)) {
                    rs = Spliced(rs, i, U"", end);  // a drawl: ていとくぅ, ないでぇ, なのぉ
                    changed = true;
                    i--;
                }
                continue;
            }
            if (plainStart) continue;
            const bool inWord = i >= 2 && (IsHiraganaRune(rs[i - 2]) || IsKanjiRune(rs[i - 2]));  // おねー, not うー
            if (IsSmallVowel(rs[end]) ||
                (rs[end] == U'っ' && (end + 1 >= rs.size() || !IsHiraganaRune(rs[end + 1]) || rs[end + 1] == U'て')) ||
                (In(prev, U"かなねさわ") && rs[end] != U'ん' && !e.LongStem(rs, from, i) &&
                 !(Nickname(std::u32string_view(rs).substr(end)) && inWord)) ||  // a particle: お花見か～いいわね
                In(rs[end], U"おご") ||
                (Nickname(std::u32string_view(rs).substr(end)) && !inWord)) {
                continue;  // うーちゃん, むーお姉ちゃん
            }
            cands = {U""};
            if (v == 1) cands.push_back(U"い");
            if (v == 2 || v == 4) cands.push_back(U"う");
            if (v == 3 && LongE(rs, i, end)) cands.insert(cands.end(), {U"い", U"え"});
            if (v == 3 && end == i + 1 && r == U'ぇ') cands.push_back(U"え");  // おめぇ おめえ
            if (Nickname(std::u32string_view(rs).substr(end))) {
                // おねーさん, おにーちゃん: the long vowel, when a word, kana spellings included, then
                // reaches the honorific.
                size_t start = i - 1;
                while (start > 0 && i - start < 8 && (IsHiraganaRune(rs[start - 1]) || IsKanjiRune(rs[start - 1]))) start--;
                for (size_t k = 1; k < cands.size(); k++) {
                    if (e.KanaReach(Spliced(rs, i, cands[k], end), start, i) >= i + cands[k].size()) {
                        rs = Spliced(rs, i, cands[k], end);
                        changed = true;
                        break;
                    }
                }
                continue;
            }
        } else {
            continue;
        }
        if (rs[i] != U'っ' && rs[end] == U'い' && e.StemTagged(rs, from, i, "H")) cands.resize(1);  // すごーい, おそーい
        if (v == 3 && LongE(rs, i, end)) {
            // A word of the e row spelled with ー that goes on two or more characters past it to
            // the end of the kana, over a sound that the mark would otherwise belong to: てーとく
            // ていとく (てー is a sound), but not へーそうなんだ.
            const std::u32string_view tails[] = {U"い", U"え"};
            int found = -1;
            for (int k = 0; k < 2 && found < 0; k++) {
                const std::u32string m = Spliced(rs, i, tails[k], end);
                const size_t reach = e.KanaReach(m, from, i);
                if (reach >= i + 3 && (reach >= m.size() || !IsHiraganaRune(m[reach]) || IsSmallVowel(m[reach]))) found = k;
            }
            if (found >= 0) {
                rs = Spliced(rs, i, tails[found], end);
                changed = true;
                continue;
            }
        }
        const size_t before = e.Reach(rs, from, i);
        if (before > end || SoundCovers(rs, from, i, ctx.extraBuiltin) || SoundWord(rs, from, end, ctx.extraBuiltin)) continue;  // もっと, せーの, おーい
        if (cands.empty()) continue;
        int best = -1;
        bool spelled = false;
        size_t bestReach = std::max(before, end), wordEnd = 0;
        for (size_t k = 0; k < cands.size(); k++) {
            const std::u32string_view c = cands[k];
            const std::u32string m = Spliced(rs, i, c, end);
            // The long vowel of the e row only within a word that starts before the kana it
            // lengthens (せんせーが, not the めい of てめーは).
            size_t reach = v == 3 && !c.empty() ? (i >= 2 ? e.Reach(m, from, i - 1) : 0) : e.Reach(m, from, i);
            if (reach <= i + c.size()) continue;
            const size_t far = reach;
            reach += end - i - c.size();
            if (reach > bestReach) {
                best = static_cast<int>(k);
                bestReach = reach;
                wordEnd = far;
            } else if (reach == bestReach && best == 0) {
                spelled = true;  // the long vowel reads as far: だいじょーぶ
            }
        }
        bool read = best >= 0;  // a key reaches past the mark
        if (best < 0 && In(rs[end], kConjKana) &&
            ((rs[i] == U'っ' && IsKanjiRune(prev) && In(rs[end], U"かきくけこがぎぐげご")) ||
             (rs[i] != U'っ' && (IsKanjiRune(prev) || e.StemEnds(rs, from, i))))) {
            best = 0;
            read = rs[i] != U'っ';  // a stem before its ending: やーめーて
        }
        if (best < 0 && rs[i] == U'ー') {
            // A longer word, kana spellings included, that starts further back in the run and
            // goes past the mark: おにいちゃーん おにいちゃん.
            size_t start = i - 1;
            while (start > 0 && i - start < 8 && (IsHiraganaRune(rs[start - 1]) || IsKanjiRune(rs[start - 1]))) start--;
            const size_t plain = std::max(e.KanaReach(rs, start, i), end);
            for (size_t k = 0; k < cands.size() && best < 0; k++) {
                const size_t reach = e.KanaReach(Spliced(rs, i, cands[k], end), start, i);
                if (reach > i + cands[k].size() && reach + (end - i - cands[k].size()) > plain) {
                    best = static_cast<int>(k);
                    wordEnd = reach;
                }
            }
            read = best >= 0;
        }
        if (best < 0) continue;
        const bool lengthened = read && !spelled && cands[best].empty() &&
                                std::any_of(rs.begin() + i, rs.begin() + end, [](char32_t c) { return c == U'ー' || c == U'～'; });
        rs = Spliced(rs, i, cands[best], end);
        changed = true;
        if (lengthened) {
            // The lengthening of the last syllable of a phrase is marked at its end: せんぱーい,
            // やーめーて. Not when no word read reaches past a syllable with a small kana
            // (だーにゃ), nor for an adjective before a comma (深～い、訳).
            size_t q = i + 1;
            if (q < rs.size() && In(rs[q], U"ゃゅょ")) q++;
            const bool pause = q == rs.size() || (IsPause(rs[q]) && rs[q] != U'～' && rs[q] != kLengthenMark &&
                                                  !(rs[q] == U'、' && rs[i] == U'い' && e.StemTagged(rs, from, i, "H")));
            if (IsHiraganaRune(rs[i]) && (q == i + 1 || wordEnd >= q) && pause) rs.insert(rs.begin() + q, kLengthenMark);
        }
    }
    if (!changed) return std::string(text);
    return EncodeText(rs);
}

std::string JoinSpelledOut(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    if (text.find("・") == std::string_view::npos) return std::string(text);
    const std::u32string rs = DecodeText(text);
    const Emphasis e(ctx, snap);
    std::u32string out;
    bool changed = false;
    size_t at = 0;
    for (size_t i = 0; i < rs.size(); i++) {
        if (i > 0 && IsWordRune(rs[i - 1])) continue;
        std::vector<size_t> ends;
        for (size_t p = i;;) {
            const size_t q = SpelledPiece(rs, p, ends.empty());
            if (q == p) break;
            ends.push_back(q);
            if (q + 1 >= rs.size() || rs[q] != U'・') break;
            p = q + 1;
        }
        if (ends.size() < 2) continue;
        const size_t end = ends.back();
        if (end < rs.size() && IsWordRune(rs[end]) && !SpelledTail(std::u32string_view(rs).substr(end))) continue;
        std::u32string joined;
        bool kanjiOnly = true;
        for (size_t k = i; k < end; k++) {
            if (rs[k] == U'・') continue;
            joined.push_back(rs[k]);
            kanjiOnly = kanjiOnly && IsKanjiRune(rs[k]);
        }
        if (kanjiOnly) continue;  // a list (赤・青・黄) or a compound read character by character
        size_t kata = 0;
        while (kata < joined.size() && IsKatakanaRune(joined[kata])) kata++;
        if (kata > 0 && joined.find(U'ー') != std::u32string::npos) continue;  // a name: スー・チー
        if (kata > 0) {
            std::u32string key = joined.substr(0, kata);
            while (!key.empty() && In(key.back(), U"ーッ")) key.pop_back();
            if (key.size() < 2 || (e.Tags(key).empty() && e.Tags(ToHiragana(key)).empty())) continue;  // a name (ア・イ)
            if (ends.size() == 2 && e.NameKey(key)) continue;  // a name that is also a word: ユ・リ, モ・モ
        } else {
            const bool kana = std::all_of(joined.begin(), joined.end(), [](char32_t c) { return IsHiraganaRune(c); });
            // A word the dictionary reads whole; two kana pieces only as a word before a
            // pause. A list, a label's choices or a name are read kana by kana (あ・い・う 아·이·우).
            const bool beforePause =
                end < rs.size() && (IsPause(rs[end]) || SpelledTail(std::u32string_view(rs).substr(end)));
            const bool word = !AfterLabel(rs, i) && !KanaOrder(rs, i, end) && !(kana && e.NameKey(joined, true) && !e.WordKey(joined)) &&
                              e.ReadsAll(joined) &&
                              (!kana || ends.size() > 2 || (e.WordWithEnds(joined, 0, joined.size(), U"") && beforePause));
            if (!word) {
                if (!kana) continue;
                const std::string heard = ReadKana(EncodeText(joined));
                out.append(rs, at, i - at);
                out.push_back(kEmphasisOpen);
                out += DecodeText(heard);
                out.push_back(kEmphasisClose);
                at = end;
                i = end - 1;
                changed = true;
                continue;
            }
        }
        out.append(rs, at, i - at);
        out.push_back(kEmphasisOpen);
        out += joined;
        out.push_back(kEmphasisClose);
        at = end;
        i = end - 1;
        changed = true;
    }
    if (!changed) return std::string(text);
    out.append(rs, at, std::u32string::npos);
    return EncodeText(out);
}

}  // namespace hdTrans::pipeline::internal
