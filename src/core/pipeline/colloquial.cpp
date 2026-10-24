#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/pipeline/protect.h"
#include "core/pipeline/restore.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/sound.h"
#include "core/word/conjugation.h"

namespace hdTrans::pipeline::internal {

namespace {

// The small tsu in hiragana and katakana.
constexpr std::u32string_view kTsu = U"っッ";

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

// Whether r lengthens the syllable before it as a mark (ー, ～).
bool IsDash(char32_t r) { return r == U'ー' || r == U'～'; }

class Emphasis {
public:
    Emphasis(const LineContext& ctx, const dict::Snapshot& snap) : ctx_(ctx), snap_(snap) {}

    // The tags of the keys spelled s. A syllable and the vowel that draws it out (けぇ) is no
    // word as the ending of an end row there: 逆転負けぇえぇッ is 負け drawn out, not the けぇ of
    // 書くけぇ.
    std::string Tags(std::u32string_view s) const {
        const bool drawn = s.size() == 2 && HiraganaVowel(s[0]) >= 0 && HiraganaVowel(s[0]) == HiraganaVowel(s[1]) &&
                           (IsSmallVowel(s[1]) || IsSmallVowel(s[1] - 1));
        return KeyTags(EncodeText(s), ctx_, snap_, !drawn);
    }

    // Whether s is a noun of the dictionary or a person of the name layer.
    bool NounOrPerson(std::u32string_view s) const {
        if (Tags(s).find('N') != std::string::npos) return true;
        for (const dict::Record& rec : snap_.NameRecords(EncodeText(s))) {
            if (rec.codes != nullptr && rec.codes->sem.Text().starts_with("QPN")) return true;
        }
        return false;
    }

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

    // An adjective stem of two or more characters that ends at character at of rs and
    // starts at or after from (こわ in こわ～い).
    bool AdjectiveStemBefore(std::u32string_view rs, size_t from, size_t at) const {
        for (size_t j = from; j + 2 <= at; j++) {
            if (Tags(rs.substr(j, at - j)).find('H') != std::string::npos) return true;
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
    // that does (どしゃ). An ending that starts with the dictionary ending of しまう (うよ of
    // 食べちゃうよ) ends a word only after a contraction of stem.contracted-shimau (not 歌うよ).
    bool WordEnds(std::u32string_view rs, size_t from, size_t at) const {
        const std::string_view shimauEnd = word::Conjugation(word::kKindWa, 0);
        bool word = false;
        for (size_t j = from; j < at; j++) {
            if (LexiconKey(ToKatakana(rs.substr(j, at - j)), ctx_.extraBuiltin)) return false;
            if (at - j < 2 && !IsKanjiRune(rs[j])) continue;
            const std::string tags = Tags(rs.substr(j, at - j));
            const bool contracted = tags.find_first_not_of('S') == std::string::npos && EncodeText(rs.substr(j, 1)) == shimauEnd;
            word = word || (!tags.empty() && (!contracted || (j >= 2 && dict::InClass("stem.contracted-shimau", EncodeText(rs.substr(j - 2, 2))))));
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

    // Whether keys other than names read the run of kana and kanji before character at of
    // rs (ReadsFrom), from its start or, when the run is longer than twelve characters, from
    // some character of its last twelve (だめ, す+るか, …場所で一緒は嫌+だな, い+こ; not ぎや,
    // うりゃ, ひ+ぃや, の alone).
    bool ReadsTo(std::u32string_view rs, size_t at) const {
        size_t p = at;
        while (p > 0 && at - p <= kMaxKeyRunes && (IsHiraganaRune(rs[p - 1]) || IsKanjiRune(rs[p - 1]))) p--;
        if (at - p <= kMaxKeyRunes) return p < at && ReadsFrom(rs.substr(p, at - p));
        for (p = at - kMaxKeyRunes; p < at; p++) {
            if (In(rs[p], U"ぁぃぅぇぉゃゅょっんー")) continue;
            if (ReadsFrom(rs.substr(p, at - p))) return true;
        }
        return false;
    }

    // Whether w reads as a key of two or more characters, with a kanji, or a verb or
    // adjective stem with its ending, then keys or the kana of particles and endings.
    bool ReadsFrom(std::u32string_view w) const {
        for (size_t k = 0; k < w.size();) {
            bool stem = false;
            size_t n = std::min(WordAt(w, k, stem), w.size() - k);
            if (k == 0 && n < 2 && !IsKanjiRune(w[0]) && !stem) return false;
            // A stem of one kana goes on with its ending (い+こ; not ひ+ぃや, の alone).
            if (k == 0 && n == 1 && !IsKanjiRune(w[0]) &&
                (w.size() == 1 || !In(w[1], U"っってただでるろれなかけばうくぐすつぬぶむ"))) {
                return false;
            }
            if (n == 0) {
                if (!In(w[k], U"ってただでるろよぞぜかなねのわさいくければうこそ")) return false;
                n = 1;
            }
            k += n;
        }
        return true;
    }

    // Whether a common noun or a word other than a noun, not a name, is exactly key (マジ,
    // ゴミ; not the 백합 of ユリ, a noun of class N130 like the names).
    bool CommonWordKey(std::u32string_view key) const {
        const std::string text = EncodeText(key);
        const dict::Candidates rd = snap_.GetWordInfo(text, dict::LookupMode::General, ctx_.lookup);
        for (const dict::Candidate& c : rd.list) {
            if (c.source.size() != text.size() || (c.user != nullptr && c.user->nameLayer) || c.codes == nullptr) continue;
            if (c.codes->sem.Contains("QPN") || c.codes->partText.starts_with("N13")) continue;
            return true;
        }
        return false;
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
    if (prev == U'よ') {
        // て or で ends an ichidan stem only after its kanji (捨てよー); after kana it is a て form
        // and よ the particle (連れていってよー, 見ててよー, 遊んでよー).
        if (In(rs[i - 2], U"てで") && (i < 3 || !IsKanjiRune(rs[i - 3]))) return false;
        return rs[i - 2] == U'し' || HiraganaVowel(rs[i - 2]) == 3;
    }
    return false;
}

// A key of the sound lexicon that starts between from and i, holds the mark at character
// i and goes past it (おーい, えーっと, くらいのちょこっと), and does not start inside a
// verb or adjective stem that ends at the mark (not the ゴォ inside すご of すごぉぉい).
bool SoundCovers(const Emphasis& e, std::u32string_view rs, size_t from, size_t i, bool extra) {
    const std::u32string f = ToKatakana(rs);
    std::string units;
    for (size_t j = from; j < i; j++) {
        bool inStem = false;
        for (size_t k = from; k < j && !inStem; k++) {
            inStem = e.Tags(rs.substr(k, i - k)).find_first_of("MH") != std::string::npos;
        }
        if (inStem) continue;
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
        if (q < rs.size() && !In(rs[q], U"なよぞしかぜわっ") && (IsHiraganaRune(rs[q]) || IsDash(rs[q]))) {
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

// The end of a piece of a word spelled out with ・ at rs[p:], or p: a kana or kanji, a small
// kana that ends its syllable, and one ー, ～ or っ; after the first piece also a lone small
// kana, っ or ー.
size_t SpelledPiece(std::u32string_view rs, size_t p, bool first) {
    if (p >= rs.size()) return p;
    const char32_t c = rs[p];
    const bool small = In(c, U"ぁぃぅぇぉゃゅょゎァィゥェォャュョヮ");
    if ((!IsHiraganaRune(c) && !IsKatakanaRune(c) && !IsKanjiRune(c)) || (first && (c == U'ー' || small || In(c, kTsu)))) {
        return p;
    }
    if (c == U'ー' || small) return p + 1;  // a lone small kana or ー after the first piece: き・ゃ・あ, す・ー・ご・い
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
        kana.push_back(rs[k] >= U'ァ' && rs[k] <= U'ヶ' ? rs[k] - 0x60 : rs[k]);  // あ・イ・う
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
// 問　あ・い, 第１問 あ・い, (1) あ・い, 問題ア．あ・い): a digit, a circled number, 問, or a digit,
// a Latin letter or a katakana standing alone or after a kanji before ）) .．:：; not a word
// before them ((笑) さ・い・て・い, 答え：ひ・み・つ).
bool AfterLabel(std::u32string_view rs, size_t i) {
    size_t k = i;
    while (k > 0 && (rs[k - 1] == U' ' || rs[k - 1] == U'　')) k--;
    if (k == 0) return false;
    const auto digit = [](char32_t c) { return (c >= U'0' && c <= U'9') || (c >= U'０' && c <= U'９'); };
    const auto letter = [](char32_t c) {
        return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || (c >= U'Ａ' && c <= U'Ｚ') ||
               (c >= U'ａ' && c <= U'ｚ') || (IsKatakanaRune(c) && c != U'ー');
    };
    const char32_t c = rs[k - 1];
    if (digit(c) || c == U'問' || (c >= 0x2460 && c <= 0x24FF) || (c >= 0x3220 && c <= 0x3243) ||
        (c >= 0x3251 && c <= 0x32BF)) {
        return true;
    }
    if (!In(c, U")）.．:：") || k < 2) return false;
    const char32_t l = rs[k - 2];
    if (digit(l)) return true;
    return letter(l) && (k < 3 || !letter(rs[k - 3]));
}

// Whether the two-piece run at rs[i:end] is two particles (て・を, の・に, は・が).
bool ParticlePair(std::u32string_view rs, size_t i, size_t end) {
    return end == i + 3 && In(rs[i], U"はがをにのでとへもやかて") && In(rs[i + 2], U"はがをにのでとへもやかて");
}

// The run at rs[i:end] read piece by piece as heard, ・ between the pieces and a lone ー kept
// (あ・い 아・이, あ・ー 아・ー, あ・ゃ 아・야).
std::u32string Literal(std::u32string_view rs, size_t i, size_t end) {
    std::u32string out;
    for (size_t k = i; k < end;) {
        size_t q = k;
        while (q < end && rs[q] != U'・') q++;
        const std::u32string piece(rs.substr(k, q - k));
        if (piece == U"ー") {
            out += piece;
        } else {
            out += DecodeText(ReadKana(EncodeText(ToHiragana(piece))));
        }
        if (q < end) out.push_back(U'・');
        k = q + 1;
    }
    return out;
}

// The start of the dictionary word of three characters or more that rs[i], a small vowel or
// ー, ends, from from on (おめぇ, てめぇ, てめー; not the さぁ of 筧さぁん): not a verb or adjective
// stem (おいしー), and after other than hiragana or after a particle (だぞてめー, not あけおめー).
// npos for none.
size_t SpelledWordStart(const Emphasis& e, std::u32string_view rs, size_t from, size_t i) {
    for (size_t j = from; j + 2 <= i; j++) {
        if (j > 0 && IsHiraganaRune(rs[j - 1]) && !In(rs[j - 1], U"はがをにのでとへもやよねぞぜなわさか")) continue;
        const std::u32string_view w = rs.substr(j, i + 1 - j);
        if (e.WordKey(w) && e.Tags(w).find_first_of("MH") == std::string::npos) return j;
    }
    return std::u32string::npos;
}

bool IsWordRune(char32_t r) { return IsHiraganaRune(r) || IsKatakanaRune(r) || IsKanjiRune(r) || r == U'・'; }

// Whether s has a character other than い: no cry of い alone (いいいい).
bool NotAllI(std::u32string_view s) { return s.find_first_not_of(U'い') != std::u32string_view::npos; }

// Whether rs[k - 2] and rs[k - 1] are the adjective いい after words that start at or after
// from, of two or more characters or ending in a kanji, and that no key reads past reach
// (気持ちいい, きもちいい, これいい; not the cries ふいい, きいい, いいい).
bool GoodAfterWord(const Emphasis& e, std::u32string_view rs, size_t from, size_t k, size_t reach) {
    return k >= 3 && k - 2 > from && rs[k - 2] == U'い' && rs[k - 1] == U'い' && !In(rs[k - 3], U"いぃ") &&
           (k - 2 - from >= 2 || IsKanjiRune(rs[k - 3])) && e.Reach(rs, from, k - 2) <= reach &&
           e.Tags(U"い").find('H') != std::string::npos;
}

// A character that ends a spoken phrase: punctuation, a closing bracket, a space, a mark
// of tone, or a line break.
bool IsPause(char32_t r) {
    return In(r, U"、。，．,.！？!?…‥」』）)］】♪☆★♥♡❤～~ 　\n") || output::IsClosingMark(r);
}

// The drawn marks that stand for the vowel kana of rs[i:end] dropped after a word: one
// for each when there are three or more (やめてぇぇぇ), none for one or two (やめてぇ).
// A shout's っ that the marks take closes the last drawn syllable with ㅅ: AbsorbClip.
std::u32string Drawn(std::u32string_view rs, size_t i, size_t end) {
    size_t n = 0;
    for (size_t k = i; k < end; k++) n += rs[k] != U'ー' && rs[k] != U'～';
    return n >= 3 ? std::u32string(std::min<size_t>(n, 16), kDrawnMark) : std::u32string();
}

// The end of the shout's っ and ッ after rs[end] that drawn marks take, and marks with
// output::kClipMark added when they take one (やめてぇぇぇっ 그만둬어어엇).
size_t AbsorbClip(std::u32string_view rs, size_t end, std::u32string& marks) {
    size_t to = end;
    while (!marks.empty() && to < rs.size() && In(rs[to], U"っッ")) to++;
    if (to > end) marks.push_back(output::kClipMark);
    return to;
}

// Whether the word that ends at rs[at] is said twice right before it (待って待って before ぇぇ).
bool CopiedBefore(std::u32string_view rs, size_t at) {
    for (size_t n = 2; n <= 8 && 2 * n <= at; n++) {
        if (rs.substr(at - n, n) == rs.substr(at - 2 * n, n)) return true;
    }
    return false;
}

// Whether rs[i:] ends a phrase: the end, a pause, or a shout's っ before one (だめーーっ！).
bool ShoutEnd(std::u32string_view rs, size_t i) {
    while (i < rs.size() && (rs[i] == U'っ' || rs[i] == U'ッ')) i++;
    return i >= rs.size() || IsPause(rs[i]);
}

// The length of the tail of a sulky tag of ending.taunt (よーだ, もんねーっだ) whose drawn mark
// starts at i and ends at end, after a word and before a pause or the end; 0 when there is none.
// After a kana of ending.taunt-not-after the tag is ようだ spelled long (夢のよーだ).
size_t TauntTail(std::u32string_view rs, size_t i, size_t end) {
    for (const std::string_view member : dict::ClassMembers("ending.taunt")) {
        const std::u32string t = DecodeText(member);
        const size_t d = static_cast<size_t>(std::find_if(t.begin(), t.end(), IsDash) - t.begin());
        if (d == 0 || d >= t.size() || i < d + 1) continue;
        const std::u32string_view head = std::u32string_view(t).substr(0, d), tail = std::u32string_view(t).substr(d + 1);
        if (rs.substr(i - d, d) != head || !IsWordRune(rs[i - d - 1]) || rs.substr(end, tail.size()) != tail ||
            dict::InClass("ending.taunt-not-after", EncodeText(rs.substr(i - d - 1, 1)))) {
            continue;
        }
        if (end + tail.size() >= rs.size() || IsPause(rs[end + tail.size()])) return tail.size();
    }
    return 0;
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
// the end of a word that is no verb or adjective stem (だめー), the て of a te form or an
// imperative (待ってー, 助けてー, 待てー; not the casual たい of やりてー), the で of a te
// form (読んでー, ないでー) and the particle ね (casual forms such as やべー, 知らねー stay);
// not the particle さ, nor な after る or て, which reads as a prohibition before ～.
bool LengthensWord(const Emphasis& e, std::u32string_view rs, size_t from, size_t i) {
    const char32_t prev = rs[i - 1];
    if (IsKanjiRune(prev)) return !e.EndsInStem(rs, from, i);
    if (IsSmallVowel(prev) || i < 2 || (!IsHiraganaRune(rs[i - 2]) && !IsKanjiRune(rs[i - 2])) || IsSmallVowel(rs[i - 2])) {
        return false;  // a lone kana: ほー, よー, ぃやぁ, すぅー
    }
    if (prev == U'さ') return false;                       // 提督さぁー: the particle さ
    if (prev == U'な' && In(rs[i - 2], U"るて")) return false;  // あげるなー, やめてなー: a request or a prohibition
    if (HiraganaVowel(prev) != 3) return true;
    // A word of the e row that is no verb or adjective stem (だめー; not やべー, the casual
    // forms of adjectives).
    if (e.WordEnds(rs, from, i) && !e.StemTagged(rs, from, i, "MH")) return true;
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

std::string RewriteVolitionalSo(std::string_view text, const dict::Snapshot& snap) {
    const std::u32string rs = DecodeText(text);
    std::u32string out;
    size_t at = 0;
    for (size_t i = 1; i + 3 <= rs.size(); i++) {
        if (rs[i] != U'そ' || rs[i + 1] != U'っ' || rs[i + 2] != U'か' || !IsKanjiRune(rs[i - 1])) continue;
        if (i + 3 < rs.size() && !In(rs[i + 3], U"なねぁあー～？?！!。、…‥」』）)")) continue;
        size_t start = i - 1;
        while (start > 0 && IsKanjiRune(rs[start - 1])) start--;
        // The longest kanji run before そ that the dictionary has decides (了解 is a noun, 見逃 a
        // verb stem).
        bool verb = false;
        for (size_t k = start; k < i; k++) {
            const std::string key = EncodeText(rs.substr(k, i - k));
            const std::vector<dict::Candidate> records = snap.RetrieveRecords(key, dict::LookupMode::General);
            if (records.empty()) continue;
            for (const dict::Candidate& c : records) {
                if (c.codes != nullptr && c.codes->partText.size() == 4 && c.codes->partText[0] == 'M' &&
                    word::Conjugation(c.codes->partText[3] - '0', 5) == "そう") {
                    verb = true;
                }
            }
            break;
        }
        if (!verb) continue;
        out.append(rs, at, i + 1 - at);
        out.push_back(U'う');
        at = i + 2;
    }
    if (at == 0) return std::string(text);
    out.append(rs, at, std::u32string::npos);
    return EncodeText(out);
}

std::string RewriteCasualAdjectives(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    const std::u32string rs = DecodeText(text);
    const Emphasis e(ctx, snap);
    std::u32string out, full;
    bool changed = false;
    size_t at = 0;
    // A kanji that starts a word clipped with っ before a mark or the end, when the kanji is
    // the stem of an adjective and no name (キャリア浅っ 浅い, 高っ！; not 蒼っ！).
    for (size_t k = 0; k + 1 < rs.size(); k++) {
        if (!IsKanjiRune(rs[k]) || rs[k + 1] != U'っ' || (k > 0 && IsKanjiRune(rs[k - 1]))) continue;
        size_t q = k + 1;
        while (q < rs.size() && rs[q] == U'っ') q++;
        if (q < rs.size() && !In(rs[q], U"！!？?。、…‥」』")) continue;
        const std::u32string_view stem = std::u32string_view(rs).substr(k, 1);
        if (e.Tags(stem).find('H') == std::string::npos || e.NameKey(stem, true)) continue;
        out.append(rs, at, k - at);
        out.push_back(rs[k]);
        out.push_back(U'い');
        at = q;
        changed = true;
    }
    if (changed) return RewriteCasualAdjectives(EncodeText(out + rs.substr(at)), ctx, snap);
    for (size_t s = 0; s < rs.size();) {
        size_t stemLen = 0, runEnd = 0, end = 0;
        if (!CasualAdjectiveAt(rs, s, stemLen, runEnd, end)) {
            s++;
            continue;
        }
        // A word spelled with the small vowel or ー is no casual adjective (おめぇ、 not 重い).
        const size_t mark = s + stemLen + 1;
        size_t from = s + stemLen;
        while (from > 0 && mark - from < 4 && (IsHiraganaRune(rs[from - 1]) || IsKanjiRune(rs[from - 1]))) from--;
        if (runEnd == mark + 1 && SpelledWordStart(e, rs, from, mark) != std::u32string::npos) {
            s = end;
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

namespace {

// Whether text has the stem of a member of honorific.drawn-out (the member without its last
// character) followed by ー, ～ or a vowel kana: the only places RewriteDrawnHonorifics changes.
bool HasDrawnStem(std::string_view text) {
    for (const std::string_view member : dict::ClassMembers("honorific.drawn-out")) {
        const std::u32string h = DecodeText(member);
        if (h.size() < 2) continue;
        const std::string stem = EncodeText(std::u32string_view(h).substr(0, h.size() - 1));
        for (size_t at = text.find(stem); at != std::string_view::npos; at = text.find(stem, at + 1)) {
            const std::u32string next = DecodeText(text.substr(at + stem.size(), 4));
            if (!next.empty() && (IsDash(next[0]) || IsSmallVowel(next[0]) || IsSmallVowel(next[0] - 1))) return true;
        }
    }
    return false;
}

}  // namespace

std::string RewriteDrawnHonorifics(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    if (!HasDrawnStem(text)) return std::string(text);
    std::u32string rs = DecodeText(text);
    const Emphasis e(ctx, snap);
    bool changed = false;
    for (const std::string_view member : dict::ClassMembers("honorific.drawn-out")) {
        const std::u32string h = DecodeText(member);
        if (h.size() < 2) continue;
        const std::u32string_view stem = std::u32string_view(h).substr(0, h.size() - 1);
        const int v = HiraganaVowel(stem.back());
        if (v < 0) continue;
        for (size_t i = 0; i + stem.size() + 1 < rs.size(); i++) {
            if (std::u32string_view(rs).substr(i, stem.size()) != stem) continue;
            size_t end = i + stem.size();
            // ー, ～ or the small or plain kana of the vowel (each plain vowel follows its small one).
            while (end < rs.size() &&
                   (IsDash(rs[end]) || (HiraganaVowel(rs[end]) == v && (IsSmallVowel(rs[end]) || IsSmallVowel(rs[end] - 1))))) {
                end++;
            }
            if (end == i + stem.size() || end >= rs.size() || rs[end] != h.back()) continue;
            size_t last = end + 1;
            while (last < rs.size() && rs[last] == h.back()) last++;  // the ん drawn too (さああんん)
            // The honorific spelled plainly, and the longest dictionary word it starts there.
            std::u32string plain = Spliced(rs, i, h, last);
            const auto holdsNone = [&](std::string_view w) {
                const std::u32string word = DecodeText(w);
                return word.size() >= h.size() && i + h.size() >= word.size() &&
                       std::u32string_view(plain).substr(i + h.size() - word.size(), word.size()) == word;
            };
            const auto none = dict::ClassMembers("honorific.drawn-out-word");
            if (std::any_of(none.begin(), none.end(), holdsNone)) continue;
            const size_t word = e.LongestKey(std::u32string_view(plain).substr(i));
            // A name before it: kanji or katakana, or two or more hiragana (みどりさああん; not the
            // cry うくぅんん or the sound びちゃーん).
            bool afterWord = i > 0 && (IsKatakanaRune(rs[i - 1]) || IsKanjiRune(rs[i - 1]) ||
                                       (i > 1 && IsHiraganaRune(rs[i - 1]) && IsHiraganaRune(rs[i - 2])));
            // Not after a verb in its continuative, a stem of two or more characters that is no
            // noun and one kana, where the stem starts before any noun or person name that ends at
            // the honorific (かみつきさあん; not みどりさああん, つぐみちゃーん, みなさーん).
            if (afterWord && IsHiraganaRune(rs[i - 1])) {
                size_t from = i - 1;
                while (from > 0 && i - from < kMaxKeyRunes && (IsHiraganaRune(rs[from - 1]) || IsKanjiRune(rs[from - 1]))) from--;
                for (size_t j = from; j + 1 < i; j++) {
                    if (e.NounOrPerson(std::u32string_view(rs).substr(j, i - j)) ||
                        e.NounOrPerson(std::u32string_view(plain).substr(j, i - j + h.size()))) {
                        break;  // a noun before the honorific, or one with it (おとうちゃん)
                    }
                    const std::string tags = e.Tags(std::u32string_view(rs).substr(j, i - 1 - j));
                    if (i - 1 - j >= 2 && tags.find_first_of("MH") != std::string::npos && tags.find('N') == std::string::npos) {
                        afterWord = false;
                        break;
                    }
                }
            }
            if (word <= h.size() && !afterWord) continue;
            const size_t mark = i + std::max(word, h.size());
            // The honorific, alone or ending a word, takes drawn marks: the restore stage writes its last
            // syllable drawn out. A word that goes on past it takes the lengthening.
            if (word > h.size()) {
                plain.insert(plain.begin() + static_cast<std::ptrdiff_t>(mark), kLengthenMark);  // ちゃーんと 제대로~
            } else {
                // Drawn out by one ー or ～, the lengthening and a drawn mark (ちゃ～ん 짜~앙, くーん 구~운);
                // by more, or by vowel kana, a drawn mark for each (くーーーん 구우우운, ちゃあああん 짜아아앙).
                const size_t run = end - i - stem.size();
                std::u32string marks;
                if (run == 1 && IsDash(rs[i + stem.size()])) {
                    marks = {kLengthenMark, kDrawnMark};
                } else {
                    marks.assign(std::min<size_t>(run, 16), kDrawnMark);
                }
                plain.insert(static_cast<size_t>(mark), marks);
                i = mark + marks.size() - 1;
            }
            rs = std::move(plain);
            changed = true;
            if (word > h.size()) i = mark;
        }
    }
    return changed ? EncodeText(rs) : std::string(text);
}

std::string RewriteEmphasis(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    std::u32string rs = DecodeText(text);
    const Emphasis e(ctx, snap);
    bool changed = false;
    // The tic っぽい drawn out after a word (行くっぽーい, 寝ちゃったっぽ～い) is っぽい with the
    // lengthening mark.
    for (size_t i = 1; i + 2 < rs.size(); i++) {
        if (rs[i] != U'ぽ' || !(IsHiraganaRune(rs[i - 1]) || IsKanjiRune(rs[i - 1]))) continue;
        size_t end = i + 1;
        while (end < rs.size() && In(rs[end], U"ー～ぉ")) end++;
        if (end == i + 1 || end >= rs.size() || rs[end] != U'い') continue;
        if (end + 1 < rs.size() && !IsPause(rs[end + 1])) continue;
        rs = Spliced(rs, i + 1, U"い", end + 1);
        rs.insert(rs.begin() + static_cast<std::ptrdiff_t>(i + 2), kLengthenMark);
        changed = true;
    }
    // An adjective drawn out at the end of a phrase: the い and ぃ after the い that ends it,
    // before a pause or a shout's っ, dropped (ずるいぃ, 痛いぃぃ, おいしいいっ). The adjective
    // is a stem that is not い alone, いい after a word (きもち+いいいいい), たい after a verb
    // stem (見てみたいいい) or a kana stem said again (こわいこわいいいい); a run of い alone is a
    // cry (いいいいい).
    for (size_t k = 1; k < rs.size(); k++) {
        if (rs[k - 1] != U'い' || !In(rs[k], U"いぃ")) continue;
        size_t end = k;
        while (end < rs.size() && In(rs[end], U"いぃ")) end++;
        size_t q = end;
        while (q < rs.size() && In(rs[q], kTsu)) q++;
        if (q < rs.size() && !IsPause(rs[q])) continue;
        size_t from = k - 1;
        while (from > 0 && k - 1 - from < kMaxKeyRunes && (IsHiraganaRune(rs[from - 1]) || IsKanjiRune(rs[from - 1]))) {
            from--;
        }
        const std::u32string_view before = std::u32string_view(rs).substr(from, k - 1 - from);
        const bool stem = e.StemTagged(rs, from, k - 1, "H") && NotAllI(before);
        const bool good = k >= 2 && GoodAfterWord(e, rs, from, k, k - 2);
        const bool wish = before.ends_with(U"た") && k >= 3 && e.StemEnds(rs, from, k - 2);  // 見てみたいいい
        // A kana adjective said again right after itself (こわいこわいいいい).
        bool copy = false;
        for (size_t n = 2; !copy && 2 * n + 1 < k && n <= kMaxKeyRunes; n++) {
            const std::u32string_view own = std::u32string_view(rs).substr(k - 1 - n, n + 1);
            copy = own == std::u32string_view(rs).substr(k - 2 - 2 * n, n + 1) && e.Tags(own.substr(0, n)).find('H') != std::string::npos;
        }
        if (!stem && !good && !wish && !copy) continue;
        std::u32string marks = Drawn(rs, k, end);
        const size_t to = AbsorbClip(rs, end, marks);
        rs = Spliced(rs, k, marks, to);
        changed = true;
    }
    // A word drawn out syllable by syllable, a ー after two or more of its kana (とーにーかーく,
    // だーかーらー): the word without the marks, drawn out at the end when the last mark
    // ends the run, when the dictionary has it.
    for (size_t p = 0; p + 3 < rs.size(); p++) {
        if (!IsHiraganaRune(rs[p]) || (p > 0 && (IsHiraganaRune(rs[p - 1]) || IsKanjiRune(rs[p - 1]) || rs[p - 1] == U'ー'))) {
            continue;
        }
        // Kana each followed by a mark, and one kana without it at the end.
        std::u32string plain;
        size_t q = p, marks = 0;
        while (q < rs.size() && IsHiraganaRune(rs[q])) {
            plain += rs[q++];
            if (q >= rs.size() || rs[q] != U'ー') break;
            marks++;
            q++;
        }
        // Of two kana, a mark after a bare vowel lengthens it (いーなー いいな, あーあー).
        if (marks < 2 || (plain.size() == 2 && In(plain[0], U"あいうえお")) || !e.WordKey(plain)) continue;
        const bool drawn = rs[q - 1] == U'ー';
        rs = Spliced(rs, p, drawn ? plain + U"～" : plain, q);
        changed = true;
        p += plain.size();
    }
    for (size_t i = 1; i < rs.size(); i++) {
        const char32_t prev = rs[i - 1];
        if ((!IsHiraganaRune(prev) && !IsKanjiRune(prev)) || prev == U'っ') continue;
        size_t from = i - 1;
        while (from > 0 && i - from < 4 && (IsHiraganaRune(rs[from - 1]) || IsKanjiRune(rs[from - 1]))) from--;
        std::vector<std::u32string_view> cands;
        bool vowelRun = false, stemRun = false;
        size_t end = i + 1;
        const int v = HiraganaVowel(prev);
        const char32_t r = rs[i];
        if (r == U'っ') {
            if (end >= rs.size() || !IsHiraganaRune(rs[end]) || In(rs[end], U"てたちつっーゃゅょぁぃぅぇぉす") || prev == U'う' ||
                (rs[end] == U'し' && end + 1 < rs.size() && In(rs[end + 1], U"ゃゅょ")) || (prev == U'た' && rs[end] == U'け')) {
                continue;
            }
            cands = {U""};
        } else if (IsDash(r) || IsSmallVowel(r) ||
                   (v >= 0 && r == kEmphasisVowels[v][1] && r != prev && i + 1 < rs.size() &&
                    (rs[i + 1] == kEmphasisVowels[v][0] || rs[i + 1] == r))) {
            // ああっ, あぁすみません: the lengthened interjection; but the い of an adjective stem
            // takes a mark for its own い (かわいー).
            const bool dashedI = prev == U'い' && IsDash(r);
            const bool stemI = dashedI && e.LongStem(rs, from, i);
            // So does the い that ends an adjective before a pause (かわいいー！ かわいい～！), but
            // not a cry of い alone (いいー).
            size_t dashEnd = i;
            while (dashEnd < rs.size() && IsDash(rs[dashEnd])) dashEnd++;
            const bool adjectiveI = dashedI && ShoutEnd(rs, dashEnd) && i >= 2 &&
                                    (e.StemTagged(rs, from, i - 1, "H") || e.AdjectiveStemBefore(rs, from, i - 1) ||
                                     GoodAfterWord(e, rs, from, i, i)) &&  // もういいー
                                    NotAllI(std::u32string_view(rs).substr(from, i - from));
            // Before a sentence particle and a pause the mark goes after the particle (かわいいーよ！
            // かわいいよ～！).
            if (dashedI && !adjectiveI && dashEnd < rs.size() && In(rs[dashEnd], U"よねわ") && ShoutEnd(rs, dashEnd + 1) &&
                i >= 2 && (e.StemTagged(rs, from, i - 1, "H") || e.AdjectiveStemBefore(rs, from, i - 1)) &&
                NotAllI(std::u32string_view(rs).substr(from, i - from))) {
                rs = Spliced(rs, i, std::u32string{rs[dashEnd], U'～'}, dashEnd + 1);
                changed = true;
                continue;
            }
            if ((IsSmallVowel(r) && v >= 0 && r != kEmphasisVowels[v][0]) || (In(prev, U"あいうえお") && !stemI && !adjectiveI)) {
                continue;
            }
            const bool plainStart = !IsSmallVowel(r) && r != U'ー' && r != U'～';
            bool small = IsSmallVowel(r) || plainStart;
            while (end < rs.size()) {
                const char32_t c = rs[end];
                if (IsDash(c) || (IsSmallVowel(c) && (v < 0 || c == kEmphasisVowels[v][0])) ||
                    (small && ((v < 0 && In(c, U"あいうえお")) || (v >= 0 && c == kEmphasisVowels[v][1])))) {
                    small = small || IsSmallVowel(c);
                    end++;
                    continue;
                }
                break;
            }
            // The long vowel of an adjective in しい, or in いい after a stem of three or more
            // kana, spelled with ー or ～ (おいしー, うれしー, かわいー, たのしーね): the stem of
            // an adjective, not also a noun unless it ends in い (かわい is also a name), ends
            // before the mark, which stands for its い; drawn out before a pause (おいしい～！)
            // and plain before a particle.
            if (!plainStart && v == 1 && (prev != U'い' || stemI) &&
                std::any_of(rs.begin() + i, rs.begin() + end, IsDash) &&
                (end >= rs.size() || !IsHiraganaRune(rs[end]) || In(rs[end], U"よねなのじゃわしかさぞ")) &&
                e.AdjectiveStemBefore(rs, from, i) && (stemI || !e.StemTagged(rs, from, i, "N"))) {
                const bool pause = end >= rs.size() || IsPause(rs[end]);
                rs = Spliced(rs, i, pause ? U"い～" : U"い", end);
                changed = true;
                continue;
            }
            // A shout's っ after the run, before a pause, ends the phrase: for the ～ below when
            // the run has a ー and dictionary keys read the text up to the mark (だめーっ; not the
            // cry ぎやぁーっ), and for a drawl of three or more vowel kana (たすけてぇぇぇっ).
            // A sulky tag after a drawn sentence particle is tone, not a copula: its tail is dropped
            // and the particle drawn out (分かりましたよーだ 알았어요~).
            if (const size_t taunt = std::any_of(rs.begin() + i, rs.begin() + end, IsDash) ? TauntTail(rs, i, end) : 0; taunt > 0) {
                rs = Spliced(rs, end, std::u32string(), end + taunt);
                changed = true;
            }
            const bool tsuEnd = end < rs.size() && In(rs[end], kTsu) && ShoutEnd(rs, end);
            const bool dashed = std::any_of(rs.begin() + i, rs.begin() + end, [](char32_t c) { return c == U'ー'; });
            const bool shoutTsu = tsuEnd && dashed && e.ReadsTo(rs, i);
            // Two kana that draw out a word said twice (待って待ってぇぇ) are drawn out like three.
            const bool copyDrawl = end - i == 2 && CopiedBefore(rs, i);
            const bool longTsu = tsuEnd && !dashed && (!Drawn(rs, i, end).empty() || copyDrawl);
            if (end >= rs.size() || !IsHiraganaRune(rs[end]) || shoutTsu || longTsu) {
                // The tokenizer ends a hiragana run at a ー before other than kana or kanji, so there
                // a word spelled with it that the mark may not lengthen (おはよー おはよ～) is written
                // with the small vowel of the same sound when the dictionary has that spelling too
                // (てめー！ てめぇ！).
                if (r == U'ー' && end == i + 1 && v >= 0 && (end >= rs.size() || !IsWordRune(rs[end]))) {
                    if (const size_t j = SpelledWordStart(e, rs, from, i);
                        j != std::u32string::npos && !LengthensWord(e, rs, from, i)) {
                        std::u32string w = rs.substr(j, end - j);
                        w.back() = kEmphasisVowels[v][0];
                        if (e.WordKey(w) && e.Tags(w).find_first_of("MH") == std::string::npos) {
                            rs[i] = w.back();
                            changed = true;
                        }
                        continue;
                    }
                }
                if (Volitional(rs, i) && e.StemEnds(rs, from, i - 1) && e.Reach(rs, from, i - 1) < i) {
                    rs = Spliced(rs, i, U"う", end);
                    changed = true;
                } else if (!shoutTsu && !e.StemTagged(rs, from, i, "H") &&  // おいしぃ～: the い of an adjective
                           !In(prev, U"あいうえおぁぃぅぇぉ") &&
                           ((end < rs.size() && In(rs[end], U"ッっ！？!?") && end - i >= 2 &&
                             std::any_of(rs.begin() + i, rs.begin() + end, IsSmallVowel) && e.Reach(rs, from, i) <= i &&
                             e.WordEnds(rs, from, i)) ||
                            // Three or more plain vowel kana after words, before a pause or a ッ
                            // (待ったああああっ, 言うなあああっ: a key such as なあ may take one).
                            (plainStart && !Drawn(rs, i, end).empty() && e.Reach(rs, from, i) <= i + 1 &&
                             e.ReadsTo(rs, i) && (end >= rs.size() || IsPause(rs[end]) || In(rs[end], kTsu))))) {
                    // A shout: そりゃああぁあッ, 何ぃいいぃッ, 待ったああああっ, its vowel kana
                    // drawn marks when there are three or more, which take the ッ after them.
                    std::u32string marks = Drawn(rs, i, end);
                    const size_t to = AbsorbClip(rs, end, marks);
                    rs = Spliced(rs, i, marks, to);
                    changed = true;
                    i--;
                } else if (!plainStart && (end >= rs.size() || IsPause(rs[end]) || shoutTsu) &&
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
                        size_t to = end;
                        while (shoutTsu && to < rs.size() && rs[to] == U'っ') to++;  // だめーっ！ だめ～！
                        rs = Spliced(rs, keep, U"～", to);
                        changed = true;
                    }
                } else if (!plainStart && (end >= rs.size() || IsPause(rs[end]) || longTsu) &&
                           (In(prev, kDrawlKana) || (!Drawn(rs, i, end).empty() && e.ReadsTo(rs, i))) &&
                           !(prev == U'て' && i >= 3 && rs[i - 2] == U'っ' && rs[i - 3] == U'だ') &&  // 何だってぇ
                           v >= 0 && std::all_of(rs.begin() + i, rs.begin() + end,
                                                 [&](char32_t c) { return c == kEmphasisVowels[v][0]; }) &&
                           !SoundRun(rs, i, end, ctx.extraBuiltin) && LengthensWord(e, rs, from, i)) {
                    // A drawl: ていとくぅ, ないでぇ, なのぉ; a shout's っ goes with a long one. Two
                    // kana that draw out a word said twice are a cry drawn out too (待って待ってぇぇ).
                    std::u32string marks = Drawn(rs, i, end);
                    if (marks.empty() && copyDrawl) marks.assign(2, kDrawnMark);
                    const size_t to = longTsu ? AbsorbClip(rs, end, marks) : end;
                    rs = Spliced(rs, i, marks, to);
                    changed = true;
                    i--;
                }
                continue;
            }
            if (plainStart) {
                // A run of three or more plain vowels of the kana before it inside a word
                // (すごおおおい, ながあああい, せんぱあああい), or of two before the い of an
                // adjective stem (さむううい): the word with one vowel kana when it has it
                // (おばあああちゃん), else without the run, when a key that starts before the
                // kana then reads past the run or the stem before its い.
                if (v < 0) continue;
                vowelRun = true;
                stemRun = rs[end] == U'い' && e.StemTagged(rs, from, i, "H");
                if (end - i < 3 && !stemRun) continue;  // いいよ, ええもん
                cands = {std::u32string_view(&kEmphasisVowels[v][1], 1), U""};
            } else {
                const bool inWord = i >= 2 && (IsHiraganaRune(rs[i - 2]) || IsKanjiRune(rs[i - 2]));  // おねー, not うー
                if (IsSmallVowel(rs[end]) ||
                    (rs[end] == U'っ' && (end + 1 >= rs.size() || !IsHiraganaRune(rs[end + 1]) || rs[end + 1] == U'て')) ||
                    (In(prev, U"かなねさわ") && rs[end] != U'ん' && !e.LongStem(rs, from, i) &&
                     (IsSmallVowel(r) ||  // かぁいい
                      (e.Reach(Spliced(rs, i, U"", end), from, i - 1) <= i &&       // in a word: なんとなーく, なさーい
                       !(rs[end] == U'い' && e.AdjectiveStemBefore(rs, from, i)))) &&  // めんどくさーい
                     !(Nickname(std::u32string_view(rs).substr(end)) && inWord)) ||  // a particle: お花見か～いいわね
                    (In(rs[end], U"おご") && !(v == 4 && e.Reach(Spliced(rs, i, U"う", end), from, i) > end + 1)) ||
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
            }
        } else {
            continue;
        }
        if (!vowelRun && rs[i] != U'っ' && rs[end] == U'い' && e.StemTagged(rs, from, i, "H")) {
            cands.resize(1);  // すごーい, おそーい
        }
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
        if ((IsSmallVowel(r) || r == U'ー') && end == i + 1 && SpelledWordStart(e, rs, from, i) != std::u32string::npos) {
            // A word spelled with the mark keeps it, unless the long vowel spelled out makes a
            // longer word with what follows (おはよーございます おはようございます, not おはよー
            // and ございます).
            const size_t spelled = e.Reach(rs, from, i);
            bool longer = false;
            for (size_t k = 1; k < cands.size() && !longer; k++) {
                longer = e.Reach(Spliced(rs, i, cands[k], end), from, i) > spelled + cands[k].size();
            }
            if (!longer) continue;
        }
        const size_t before = e.Reach(rs, from, i);
        // A sound that ends in the っ stands only before と; before other kana the っ is emphasis
        // when a verb or adjective stem or a word of two kana or more past it follows (ひっどい,
        // おっかしい; not あっぶね, さっお).
        const bool sound = SoundWord(rs, from, end, ctx.extraBuiltin);
        const bool soundTsu = sound && r == U'っ' && rs[end] != U'と';
        if (before > end || SoundCovers(e, rs, from, i, ctx.extraBuiltin) || (sound && !soundTsu)) continue;  // もっと, せーの, おーい
        if (cands.empty()) continue;
        int best = -1;
        bool spelled = false;
        size_t bestReach = std::max(before, end), wordEnd = 0;
        for (size_t k = 0; k < cands.size(); k++) {
            const std::u32string_view c = cands[k];
            const std::u32string m = Spliced(rs, i, c, end);
            // The long vowel of the e row only within a word that starts before the kana it
            // lengthens (せんせーが, not the めい of てめーは).
            size_t reach = (v == 3 && !c.empty()) || vowelRun ? (i >= 2 ? e.Reach(m, from, i - 1) : 0) : e.Reach(m, from, i);
            if (reach <= i + c.size() || (soundTsu && reach < i + 2 && !e.StemTagged(m, from, reach, "H"))) continue;
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
        if (best < 0 && stemRun) {
            best = 1;  // an adjective stem before its い: すごおおおい
            read = true;
        } else if (best < 0 && !vowelRun && In(rs[end], kConjKana) &&
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
            // A word that starts after another inside the run of kana (あんな + しょーもない
            // しょうもない): the long vowel spelled when it then goes past the mark.
            for (size_t j = start + 1; j < i && best < 0; j++) {
                if (!IsHiraganaRune(rs[start]) || !e.WordKey(std::u32string_view(rs).substr(start, j - start))) continue;
                for (size_t k = 1; k < cands.size() && best < 0; k++) {
                    bool stem = false;
                    const std::u32string m = Spliced(rs, i, cands[k], end);
                    const size_t n = e.WordAt(m, j, stem);
                    // A stem before its ending, or a word that ends the run of kana (お年玉 +
                    // ちょーだい); not one that stops inside it (しゅうりょ of しゅーりょー).
                    if (!stem && j + n < m.size() && (IsHiraganaRune(m[j + n]) || m[j + n] == U'ー')) continue;
                    if (j + n > i + cands[k].size()) {
                        best = static_cast<int>(k);
                        wordEnd = j + n;
                    }
                }
            }
            read = best >= 0;
        }
        if (best < 0) continue;
        // Three or more small vowels inside a word draw out its last syllable (いたぁぁぁい
        // いたい + 3 drawn marks, 아파아아아), where plain vowels and dashes lengthen it (すごおおおい).
        const std::u32string drawn =
            !vowelRun && cands[best].empty() && std::all_of(rs.begin() + i, rs.begin() + end, IsSmallVowel) ? Drawn(rs, i, end)
                                                                                                               : std::u32string();
        const bool lengthened =
            read && (vowelRun || !drawn.empty() ||
                     (!spelled && cands[best].empty() && std::any_of(rs.begin() + i, rs.begin() + end, IsDash)));
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
            if (IsHiraganaRune(rs[i]) && (q == i + 1 || wordEnd >= q) && (pause || (!drawn.empty() && ShoutEnd(rs, q)))) {
                if (drawn.empty()) {
                    rs.insert(rs.begin() + q, kLengthenMark);
                } else {
                    std::u32string marks = drawn;
                    const size_t to = AbsorbClip(rs, q, marks);  // いたぁぁぁいっ 아파아아앗
                    rs = Spliced(rs, q, marks, to);
                }
            }
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
        // Kana only, hiragana or both: a run that is no word is read kana by kana.
        const bool kana = std::none_of(joined.begin(), joined.end(), [](char32_t c) { return IsKanjiRune(c); });
        const bool hiragana = std::any_of(joined.begin(), joined.end(), [](char32_t c) { return IsHiraganaRune(c); });
        // Two pieces are a word only before explicit punctuation (ば・か！, not ば・か at the end
        // of a line); a lone ー piece keeps the run literal (あ・ー), and a run with a lone ー
        // or small kana piece that is not joined is read piece by piece (あ・ゃ 아・야).
        const bool closed = end < rs.size() && In(rs[end], U"！？!?…‥、。」』）");
        bool dash = false, lone = false;
        for (size_t k = i + 1; k < end; k++) {
            if (rs[k - 1] != U'・' || (k + 1 < end && rs[k + 1] != U'・')) continue;
            dash = dash || rs[k] == U'ー';
            lone = lone || rs[k] == U'ー' || IsSmallVowel(rs[k]) || In(rs[k], U"ゃゅょゎャュョヮァィゥェォ");
        }
        bool word = !dash;
        size_t kata = 0;
        while (kata < joined.size() && IsKatakanaRune(joined[kata])) kata++;
        if (kata > 0) {
            std::u32string key = joined.substr(0, kata);
            while (!key.empty() && In(key.back(), U"ーッ")) key.pop_back();
            word = key.size() >= 2 && key.find(U'ー') == std::u32string::npos &&  // a name: スー・チー
                   (ends.size() > 2 || closed) &&                                  // バ・ン
                   (!e.Tags(key).empty() || !e.Tags(ToHiragana(key)).empty()) &&  // a name: ア・イ
                   !(ends.size() == 2 && e.NameKey(key) && !e.CommonWordKey(key));  // ユ・リ, not マ・ジ
            word = word && !dash;
            if (!word && !hiragana && !lone) continue;  // katakana stay for the word stage (ア・イ 아·이)
        } else {
            // A word the dictionary reads; two kana pieces only before punctuation and not two
            // particles (て・を). A list, a label's choices or a name are read kana by kana
            // (あ・い・う 아·이·우).
            const std::u32string core = ToHiragana(joined);  // kana in hiragana: さ・イ・テ・い
            word = !dash && !AfterLabel(rs, i) && !KanaOrder(rs, i, end) &&
                   !(hiragana && e.NameKey(core, true) && !e.WordKey(core)) &&
                   (e.ReadsAll(core) || Emphasis::LexiconKey(ToKatakana(core), ctx.extraBuiltin)) &&
                   (!kana || ends.size() > 2 || (closed && !ParticlePair(rs, i, end)));
            joined = core;
        }
        if (!word) {
            if (!kana) continue;
            out.append(rs, at, i - at);
            if (lone) {
                out += Literal(rs, i, end);
            } else {
                out.push_back(kEmphasisOpen);
                out += DecodeText(ReadKana(EncodeText(ToHiragana(joined))));  // か・ん・こ・れ 칸・코・레
                out.push_back(kEmphasisClose);
            }
            at = end;
            i = end - 1;
            changed = true;
            continue;
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
