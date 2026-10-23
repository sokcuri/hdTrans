#include "core/pipeline/sound.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline/protect.h"
#include "core/pipeline/restore.h"
#include "core/pipeline/runes.h"
#include "core/text/kana.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/word/conjugation.h"

namespace hdTrans::pipeline {

namespace internal {

namespace {

bool IsKana(char32_t r) { return IsHiraganaRune(r) || IsKatakanaRune(r) || r == U'～'; }

bool In(char32_t r, std::u32string_view set) { return set.find(r) != std::u32string_view::npos; }

constexpr std::u32string_view kSmallVowels = U"ァィゥェォ";
constexpr std::u32string_view kPlainVowels = U"アイウエオ";

constexpr std::u32string_view kVowelRows[5] = {
    U"アァカガサザタダナハバパマヤャラワ",
    U"イィキギシジチヂニヒビピミリ",
    U"ウゥクグスズツヅヌフブプムユュル",
    U"エェケゲセゼテデネヘベペメレ",
    U"オォコゴソゾトドノホボポモヨョロヲ",
};

// The vowel row of a katakana, -1 for none.
int VowelOf(char32_t r) {
    for (int v = 0; v < 5; v++) {
        if (In(r, kVowelRows[v])) return v;
    }
    return -1;
}

// Whether r lengthens a syllable ending in the stem character last: ー, ～, the small
// or plain kana of its vowel, or ン after ン.
bool Lengthens(char32_t r, char32_t last) {
    if (r == U'ー' || r == U'～') return true;
    if (last == U'ン') return r == U'ン';
    const int v = VowelOf(last);
    return v >= 0 && (r == kSmallVowels[v] || r == kPlainVowels[v]);
}

// A small vowel kana as the plain one (ァ ア), a plain vowel as itself, 0 otherwise.
char32_t PlainVowel(char32_t r) {
    if (const size_t i = kSmallVowels.find(r); i != std::u32string_view::npos) return kPlainVowels[i];
    if (In(r, kPlainVowels)) return r;
    return 0;
}

// A match that ends in hiragana と, before ー, ～ or small vowels that lengthen it
// (ありがとぉー).
bool EndsInTo(std::u32string_view m) {
    size_t k = m.size() - 1;
    while (k > 0 && (m[k] == U'ー' || m[k] == U'～' || In(m[k], U"ぁぃぅぇぉ"))) k--;
    return m[k] == U'と';
}

constexpr int kMaxLengthening = 4;

char32_t Syllable(int initial, int vowel, int final) {
    return static_cast<char32_t>(0xAC00 + (initial * 21 + vowel) * 28 + final);
}

// The vowel that lengthens a vowel (ㅑ ㅏ, ㅘ ㅏ, ㅟ ㅣ).
constexpr int kLongVowel[21] = {0, 1, 0, 1, 4, 5, 4, 5, 8, 0, 1, 5, 8, 13, 4, 5, 20, 13, 18, 20, 20};

// The particles that may stand before an ono row inside a hiragana run.
constexpr std::u32string_view kParticles = U"がをにはもでとへやのて";

// Cue words; kana in katakana.
constexpr std::u32string_view kEatingCues[] = {U"食", U"喰", U"カジ", U"齧", U"噛", U"音", U"漬", U"センベイ", U"煎餅"};
constexpr std::u32string_view kScratchingCues[] = {U"頭", U"掻", U"搔", U"痒", U"カユ", U"首", U"背中", U"頬"};
constexpr std::u32string_view kRefusals[] = {U"ヤメ", U"止メ", U"ダメ", U"駄目", U"ラメ", U"イヤ",
                                             U"嫌",   U"離",   U"放",   U"触",   U"ユルシ", U"許シ"};
constexpr std::u32string_view kDeclines[] = {U"無理", U"違", U"遠慮", U"結構", U"トンデモ", U"マサカ", U"ソンナコト", U"別ニ"};
constexpr std::u32string_view kProgressCues[] = {U"中", U"テオリ", U"テイマ"};
constexpr std::u32string_view kConditionals[] = {U"ナルト", U"ナッタラ", U"ナレバ", U"スルト",
                                                 U"シタラ", U"スレバ",   U"テモ",   U"デモ"};

// The characters that may stand between いやぁ and the words that decide its reading.
constexpr std::u32string_view kSoundSeparators = U"、，…‥・ 　";

template <size_t N>
bool ContainsAny(std::u32string_view s, const std::u32string_view (&subs)[N]) {
    for (const auto sub : subs) {
        if (s.find(sub) != std::u32string_view::npos) return true;
    }
    return false;
}

// The syllables that stand alone before a comma and a kanji word (あ、本当 is "ah,
// really"; が、 is a particle), so X、<kanji> is a stutter only for others.
constexpr std::u32string_view kNotStutters[] = {U"ア", U"ウ", U"エ", U"オ", U"ン", U"ネ", U"デ", U"マ", U"サ",
                                                U"ヤ", U"ヨ", U"ム", U"ガ", U"ヲ", U"ト", U"ジャ", U"チョ"};

// A kanji numeral or an ASCII or full-width digit.
bool NumeralRune(char32_t r) {
    return In(r, U"〇一二三四五六七八九十百千万億") || (r >= U'0' && r <= U'9') || (r >= U'０' && r <= U'９');
}

// Words that start like a stutter but say something of their own (あ、あれ、何;
// は、はい、仰る).
constexpr std::u32string_view kStandalonePieces[] = {U"コレ", U"ソレ", U"アレ", U"ドレ", U"コノ", U"ソノ", U"アノ",
                                                     U"ドノ", U"ココ", U"ソコ", U"ドコ", U"コウ", U"ソウ", U"アア",
                                                     U"ドウ", U"ナニ", U"ナン", U"アナタ", U"アノネ", U"ソンナ", U"コンナ",
                                                     U"アンナ", U"ドンナ", U"ハイ", U"イエ", U"イヤ", U"ウン", U"エエ",
                                                     U"ネエ", U"マア", U"サア", U"ヨシ", U"ホラ"};

// The kana whose syllable with ッ stands alone before a kanji (あっ提督, えっ本当,
// ぶっ殺す); for other kana the syllable and ッ directly before a kanji are a stutter
// (ちっ違う, しっ司令官), written as if a comma followed.
constexpr std::u32string_view kSoundsBeforeKanji = U"アウエオクケゲグサナネハヒフブヘホマムヤヨワン";

// The length of a stutter separator at rs[i:]: 、 ， or a run of … ‥ ― ─ — or of two or more ・.
size_t StutterSeparator(std::u32string_view rs, size_t i) {
    if (i >= rs.size()) return 0;
    switch (rs[i]) {
        case U'、':
        case U'，':
            return 1;
        case U'…':
        case U'‥':
        case U'―':
        case U'─':
        case U'—':
        case U'・': {
            size_t n = i;
            while (n < rs.size() && rs[n] == rs[i]) n++;
            if (rs[i] == U'・' && n - i < 2) return 0;
            return n - i;
        }
        default:
            return 0;
    }
}

// A character that a hiragana run after it may continue as okurigana, a particle or an
// ending.
bool InWord(char32_t r) { return IsKanjiRune(r) || IsKatakanaRune(r) || text::IsLetter(r) || text::IsDigit(r); }

// The end of the katakana run at p, with a ～ between katakana in it (ザ～ラ).
size_t KatakanaEnd(std::u32string_view rs, size_t p) {
    while (p < rs.size() && (IsKatakanaRune(rs[p]) || (rs[p] == U'～' && p + 1 < rs.size() && IsKatakanaRune(rs[p + 1])))) p++;
    return p;
}

// One piece of a stutter and the separator after it. A piece of one syllable repeats
// the first syllable of the word (さ、 in さ、最低); a longer one is kana characters of a
// word of word characters (ごめ、 of ごめんなさい: 2 of 6) and repeats that share of it.
struct StutterPiece {
    std::u32string_view separator;
    size_t kana = 1;
    size_t word = 0;
};

// A sound found in the text: the characters [start, end) are written as korean.
struct SoundMatch {
    size_t start = 0, end = 0, keyLen = 0;
    std::u32string_view key;  // in katakana
    std::string korean;
    std::string_view root;
    bool intj = false, lead = false, open = false;
    bool spaceAfter = false;  // a word that starts inside the match took its end
    bool phrase = false;      // an interjection of phrases.tsv, a word rather than a sound
};

// Whether two sounds in a row are written apart: both have two or more syllables, and
// b does not continue a by starting with its last kana (うふふ+ふっ).
bool Separate(const SoundMatch& a, const SoundMatch& b) {
    return text::RuneCount(a.korean) > 1 && text::RuneCount(b.korean) > 1 &&
           (a.key == b.key || b.key.empty() || b.key[0] != a.key.back());
}

bool IsAsciiLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }
bool IsAsciiAlnum(char c) { return IsAsciiLetter(c) || IsAsciiDigit(c); }
bool IsHexDigit(char c) { return IsAsciiDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

bool EqualFoldAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
        if (x != y) return false;
    }
    return true;
}

// A markup tag <...> of up to 1024 characters other than <BR> and <LI>.
size_t TagLength(std::string_view s) {
    if (s.size() < 2 || s[0] != '<' || !(IsAsciiLetter(s[1]) || s[1] == '!' || s[1] == '/' || s[1] == '?')) return 0;
    const size_t end = s.find_first_of("<>", 1);
    if (end == std::string_view::npos || s[end] != '>') return 0;
    const std::string_view tag = s.substr(0, end + 1);
    if (text::RuneCount(tag) > 1024 || EqualFoldAscii(tag, "<BR>") || EqualFoldAscii(tag, "<LI>")) return 0;
    return tag.size();
}

// A BBCode tag [b], [/b], [color=red] of up to 128 characters.
size_t BbCodeLength(std::string_view s) {
    size_t i = 0;
    if (i >= s.size() || s[i] != '[') return 0;
    i++;
    if (i < s.size() && s[i] == '/') i++;
    if (i >= s.size() || !IsAsciiLetter(s[i])) return 0;
    while (i < s.size() && (IsAsciiAlnum(s[i]) || s[i] == '_')) i++;
    if (i < s.size() && s[i] == '=') {
        const size_t close = s.find_first_of("]\n", i);
        if (close != std::string_view::npos && s[close] == ']') i = close;
    }
    if (i >= s.size() || s[i] != ']') return 0;
    const std::string_view tag = s.substr(0, i + 1);
    return text::RuneCount(tag) > 128 ? 0 : tag.size();
}

// An entity &name; (a letter and 1..31 letters or digits), &#123; (1..7 digits) or
// &#x1F; (1..6 hex digits).
size_t EntityLength(std::string_view s) {
    if (s.size() < 3 || s[0] != '&') return 0;
    const auto run = [&](size_t from, size_t max, bool (*in)(char)) {
        size_t n = from;
        while (n < s.size() && n - from < max && in(s[n])) n++;
        return n;
    };
    const auto closed = [&](size_t from, size_t to, size_t min) {
        return to - from >= min && to < s.size() && s[to] == ';' ? to + 1 : 0;
    };
    if (IsAsciiLetter(s[1])) return closed(2, run(2, 31, IsAsciiAlnum), 1);
    if (s[1] != '#') return 0;
    if (const size_t n = closed(2, run(2, 7, IsAsciiDigit), 1); n > 0) return n;
    if (s[2] == 'x') return closed(3, run(3, 6, IsHexDigit), 1);
    return 0;
}

size_t MarkerLength(std::string_view s, std::span<const std::string> markers) {
    size_t n = 0;
    for (const std::string& m : markers) {
        if (m.size() > n && s.starts_with(m)) n = m.size();
    }
    return n;
}

// The length of the opaque span at the start of s: a tag, BBCode tag, entity, style code
// or marker.
size_t OpaqueLength(std::string_view s, char before, std::span<const std::string> markers) {
    if (const size_t n = TagLength(s); n > 0) return n;
    if (const size_t n = BbCodeLength(s); n > 0) return n;
    if (const size_t n = EntityLength(s); n > 0) return n;
    if (const size_t n = StyleCodeLength(s, before); n > 0) return n;
    return MarkerLength(s, markers);
}

// Characters of one AB unit of a mimetic key.
constexpr size_t kAbabUnit = 2;

// An ono row whose key is ABAB (four kana, A other than B) with a Korean of two
// equal blocks (うずうず 근질근질).
bool AbabKey(const SoundRow& row) {
    if (row.intj || row.lead || row.phrase || row.key.size() != 2 * kAbabUnit) return false;
    const std::u32string_view key = row.key;
    if (key[0] != key[2] || key[1] != key[3] || key[0] == key[1]) return false;
    const std::u32string ko = DecodeText(row.korean);
    return ko.size() >= 2 && ko.size() % 2 == 0 && ko.substr(0, ko.size() / 2) == ko.substr(ko.size() / 2);
}

// The length in characters of the run of three or more AB units at f[p:], 0 for a
// shorter one. Each unit is written as one half of the row's Korean (うずうずうず
// 근질근질근질).
size_t AbabRun(const SoundRow& row, std::u32string_view f, size_t p) {
    const std::u32string_view unit = std::u32string_view(row.key).substr(0, kAbabUnit);
    size_t n = 0;
    while (p + n + unit.size() <= f.size() && f.substr(p + n, unit.size()) == unit) n += unit.size();
    return n >= 3 * unit.size() ? n : 0;
}

// Scans one text for sounds and stutters.
class SoundText {
public:
    SoundText(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap)
        : text_(text), ctx_(ctx), snap_(snap), rs_(DecodeText(text)), f_(ToKatakana(rs_)) {
        offsets_.reserve(rs_.size() + 1);
        size_t off = 0;
        for (const char32_t r : rs_) {
            offsets_.push_back(off);
            off += r >= kInvalidByte ? 1 : text::DecodeRune(text.substr(off)).size;
        }
        offsets_.push_back(off);
    }

    std::string Write();

private:
    std::string_view Bytes(size_t from, size_t to) const {
        return text_.substr(offsets_[from], offsets_[to] - offsets_[from]);
    }
    bool UtteranceStart(size_t p) const;
    bool PauseAt(size_t p) const;
    void LongestKeys(size_t p, size_t& base, size_t& user, size_t& name, bool skipEnds = false) const;
    bool LongerWordAt(size_t p, size_t n) const;
    bool WordAt(size_t p, size_t n) const;
    bool MatchAt(size_t p, SoundMatch& best, bool inChain = false) const;
    std::string Reading(const SoundRow& s, size_t p, size_t end) const;
    bool PresentTime(size_t end) const;
    bool Scoff(size_t p, size_t end) const;
    std::u32string_view Clause(size_t i) const;
    std::u32string_view Line(size_t p) const;
    bool AfterClause(size_t p, size_t end) const;
    size_t NextWord(size_t i) const;
    std::string IyaReading(size_t p, size_t end) const;
    bool Stopped(const SoundMatch& m) const;
    std::vector<SoundMatch> Sounds(size_t p, bool afterParticle) const;
    bool Spaced(size_t i) const;
    bool StartsVerb(size_t i) const;
    size_t WordLen(size_t p) const;
    size_t Adverbial(size_t i) const;
    bool StartsLongerWord(size_t i) const;
    size_t PausedTo(size_t i) const;
    size_t Stutter(size_t p, std::vector<StutterPiece>& pieces) const;
    size_t Burst(size_t p) const;
    size_t MimeticTo(size_t p, size_t end, bool& hiragana) const;
    bool KanaWord(size_t p) const;
    size_t HiraganaMimeticTo(size_t p) const;
    std::string ExclaimedSound(size_t p, size_t end) const;
    size_t LoneCatch(size_t p) const;
    std::string_view LoneVoice(size_t p) const;

    std::string_view text_;
    const LineContext& ctx_;
    const dict::Snapshot& snap_;
    std::u32string rs_;
    std::u32string f_;  // rs_ in katakana
    std::vector<size_t> offsets_;  // byte offset of each character, and the end
    size_t stutterEnd_ = static_cast<size_t>(-1);  // the character after the last stutter
};

// Whether character p starts an utterance: the start of the text or of a line, after an
// opening bracket, a sentence end, a space or a stutter (お、おい).
bool SoundText::UtteranceStart(size_t p) const {
    if (p == 0 || p == stutterEnd_) return true;
    return In(rs_[p - 1], U"\n「『（(。！？!?♪ 　―");
}

// Whether there is a pause at character p: the end of the text or a comma, sentence end,
// closing bracket or lengthening mark (not an ellipsis: ちょっと…痛い).
bool SoundText::PauseAt(size_t p) const {
    return p >= rs_.size() || In(rs_[p], U"、，,！？!?。」』）)\n") || rs_[p] == kLengthenMark;
}

// The length in characters of the longest base key, plain-dictionary key and name-layer
// key that start at character p; kana spellings do not count, nor with skipEnds the
// endings of end rows.
void SoundText::LongestKeys(size_t p, size_t& base, size_t& user, size_t& name, bool skipEnds) const {
    base = user = name = 0;
    const size_t end = std::min(rs_.size(), p + 24);
    const dict::Candidates rd = snap_.GetWordInfo(Bytes(p, end), dict::LookupMode::General, ctx_.lookup);
    for (const dict::Candidate& c : rd.list) {
        if (c.user != nullptr && (c.user->kana || (skipEnds && c.user->end))) continue;
        const size_t n = text::RuneCount(c.source);
        if (c.user != nullptr && c.user->nameLayer) {
            name = std::max(name, n);
        } else if (c.user != nullptr) {
            user = std::max(user, n);
        } else {
            base = std::max(base, n);
        }
    }
}

// Whether a dictionary word other than a kana spelling or a name is exactly the n
// characters at p.
bool SoundText::WordAt(size_t p, size_t n) const {
    const dict::Candidates rd = snap_.GetWordInfo(Bytes(p, p + n), dict::LookupMode::General, ctx_.lookup);
    for (const dict::Candidate& c : rd.list) {
        if (c.user != nullptr && (c.user->kana || c.user->nameLayer)) continue;
        if (text::RuneCount(c.source) == n) return true;
    }
    return false;
}

// Whether a dictionary word longer than n characters, and longer than any sound read
// there, starts at character p (うずら, not がくっ or ざっと).
bool SoundText::LongerWordAt(size_t p, size_t n) const {
    std::string units;
    for (const SoundRow& s : SoundRows(f_[p], ctx_.extraBuiltin)) {
        size_t end = 0;
        if (MatchSound(s, f_, p, end, units)) n = std::max(n, end - p);
    }
    size_t base = 0, user = 0, name = 0;
    LongestKeys(p, base, user, name);
    return base > n || user > n;
}

// The sound at character p: the longest key, then the longest match. A dictionary word
// that is longer, or a plain-dictionary entry that is at least as long as the key,
// rules it out; a name-layer sense does not (ドーン, ガチャ).
bool SoundText::MatchAt(size_t p, SoundMatch& best, bool inChain) const {
    const SoundRow* chosen = nullptr;
    std::string units;
    const std::u32string_view f = f_;
    const bool ababText = p + 3 * kAbabUnit <= f.size() && f[p] != f[p + 1] &&
                          f.substr(p, kAbabUnit) == f.substr(p + kAbabUnit, kAbabUnit) &&
                          f.substr(p, kAbabUnit) == f.substr(p + 2 * kAbabUnit, kAbabUnit);
    bool skipped = false;  // a shorter key was passed over; only a run may follow
    // A small vowel that starts a word looks up the rows of its plain vowel (ぅあっ).
    const char32_t first = p == 0 || !text::IsLetter(f_[p - 1]) ? PlainVowel(f_[p]) : 0;
    for (const SoundRow& s : SoundRows(first != 0 ? first : f_[p], ctx_.extraBuiltin)) {
        const bool abab = AbabKey(s);
        if (chosen != nullptr && s.key.size() < chosen->key.size() && !abab) {
            if (!ababText) break;
            skipped = true;  // a run of AB units may still outrun the key (はあはあはあはあ)
            continue;
        }
        size_t end = 0;
        bool ok = MatchSound(s, f_, p, end, units);
        if (ok && s.open) {
            end = p + s.key.size();  // not lengthened: ていうかあたし, つーかあの
            units.clear();
        }
        std::string runKorean;
        bool spaceAfter = false;
        if (ok && abab) {
            const size_t plainEnd = end;
            size_t n = AbabRun(s, f_, p);
            // The end of a run at character e with the ー, ～ and ッ that close it
            // (ドキドキドキドキッ, ドキドキドキドキーッ, ドキドキドキドキー).
            const auto closed = [&](size_t e) {
                size_t j = e;
                while (j < f_.size() && In(f_[j], U"～ー")) j++;
                while (j < f_.size() && f_[j] == U'ッ') j++;
                if (j == e || (j < f_.size() && IsKana(f_[j]) && !In(f_[j], U"トテ～ー"))) return e;
                while (j < f_.size() && In(f_[j], U"～ー")) j++;
                return j;
            };
            // A word that starts in the last unit and runs past it (うずら in
            // うずうずうずら) takes that unit: the run ends before it and the word is
            // written apart from the run.
            while (n >= 3 * kAbabUnit && LongerWordAt(p + n - kAbabUnit, closed(p + n) - (p + n - kAbabUnit))) {
                n -= kAbabUnit;
                spaceAfter = true;
            }
            if (n >= 2 * kAbabUnit) {
                end = spaceAfter ? p + n : closed(p + n);
                const std::string_view half = std::string_view(s.korean).substr(0, s.korean.size() / 2);
                for (size_t i = 0; i < n / kAbabUnit; i++) runKorean += half;
                const std::u32string_view tail = f.substr(p + n, end - (p + n));
                if (tail.find_first_of(U"ー～") != std::u32string_view::npos) runKorean += '~';
            }
            // A run that does not end by itself is not one (タッタッタッする: タッタッタ + っする).
            SoundMatch run;
            run.start = p;
            run.end = end;
            if (runKorean.empty() || !Stopped(run)) {
                end = plainEnd;
                runKorean.clear();
                spaceAfter = false;
            }
        }
        if (skipped && runKorean.empty()) continue;
        if (ok && s.lead) {
            size_t from = p;
            if (s.open) {
                while (from > 0 && In(rs_[from - 1], U"…‥")) from--;  // ……ってか
            }
            bool start = UtteranceStart(from), pause = PauseAt(end);
            if (s.key == U"ハ" || s.key == U"ヘ") {
                // A bare は or へ is the particle after a word, a dash or a space ((人形) は、,
                // 私――は――) and before a closing bracket (「は」); it is a lead only where it
                // opens a line or a quote, and there a dash or an ellipsis after it is a pause
                // too (へ――面白い).
                size_t q = p;
                while (q > 0 && text::IsSpace(rs_[q - 1])) q--;
                start = q == 0 || In(rs_[q - 1], U"\n「『（(");
                pause = (pause && (end == rs_.size() || !In(rs_[end], U"」』）)"))) ||
                        (end == p + 1 && end < rs_.size() && In(rs_[end], U"―─—…‥"));
            }
            // おい also calls right before a name or a word in kanji or katakana (おい田中,
            // おいチビ), which no verb form of 置く or 老いる starts; 生い spelled in kana starts
            // おい茂る there.
            if (s.key == U"オイ" && end < rs_.size() && (text::IsKanji(rs_[end]) || IsKatakanaRune(rs_[end])) &&
                rs_[end] != U'茂') {
                pause = true;
            }
            if (!start || (!pause && !s.open) || IsKatakanaRune(rs_[p])) continue;
            if (s.open && end < rs_.size() && In(rs_[end], U"？?")) continue;  // 見ゆ！ってか？ quotes
            std::erase(units, '!');  // よしっ 좋아, not 좋앗
        }
        if (ok && runKorean.empty() && s.key.back() == U'ト' && !EndsInTo(std::u32string_view(rs_).substr(p, end - p))) {
            ok = false;  // a final と is the particle, not katakana ト
        }
        if (ok && (chosen == nullptr || end > best.end)) {
            std::string korean = !runKorean.empty()
                                     ? std::move(runKorean)
                                     : units.find('h') != std::string::npos
                                           ? text::FromKana(EncodeText(std::u32string_view(f_).substr(p, end - p)),
                                                            text::Preset::Fan)
                                           : Lengthen(Reading(s, p, end), units);
            // An open row before no pause is followed by a comma, so the words after it
            // read as the start of a clause (ってかなんで 그보다, 왜).
            if (s.open && !PauseAt(end)) {
                if (IsHiraganaRune(rs_[end])) korean += ',';
                spaceAfter = !In(rs_[end], U"…‥―─—");
            }
            best = SoundMatch{p, end, s.key.size(), s.key, std::move(korean), s.root, s.intj, s.lead, s.open, spaceAfter, s.phrase};
            chosen = &s;
        }
    }
    if (chosen == nullptr) return false;
    if (!Stopped(best) && !(inChain && best.intj && !chosen->phrase)) return false;
    // A phrase does not end at a ッ before テ: that is its verb in the te-form (イっちゃってぇ).
    if (chosen->phrase && best.end < f_.size() && f_[best.end - 1] == U'ッ' && f_[best.end] == U'テ') return false;
    if (best.lead) return true;  // an utterance-initial interjection before a pause wins over words
    // An interjection before a pause or an ellipsis is not the ending of an end row there
    // (見て！ぽい～！ 포이~!).
    const bool beforePause = PauseAt(best.end) || (best.end < rs_.size() && In(rs_[best.end], U"…‥"));
    size_t base = 0, user = 0, name = 0;
    LongestKeys(p, base, user, name, best.intj && beforePause);
    size_t keyLen = chosen->key.size();
    if (chosen->key.back() == U'ト') keyLen--;
    const size_t surface = best.end - p;
    return !(base > surface || (base == surface && surface > best.keyLen) || user >= keyLen);
}

// The Korean of the sound s at characters [p, end), for the sounds whose reading
// depends on the words around them:
//   - なっ after a clause on its line is "right?" (行こうぜ！ なっ！ 그치!);
//   - ぽりぽり on a line about eating is crunching (오독오독), unless the line is about
//     scratching;
//   - いやぁ (いやあ, いやー) is 싫어 when a refusal follows (やめて, だめ, …ないで), another
//     いや is next to it, or it stands alone before ！; 아니 before a clause that
//     declines (無理, 違う, 遠慮); 이야 otherwise;
//   - いざ is 막상 in a conditional clause (いざ講義になると), 자 otherwise;
//   - ただいま is 지금 before a clause (PresentTime), 다녀왔습니다 otherwise;
//   - はん is a scoff (흥) where it opens an utterance before ！ 、 or 。 and no sound
//     follows (Scoff), a moan (하앙) otherwise.
std::string SoundText::Reading(const SoundRow& s, size_t p, size_t end) const {
    const std::u32string_view key = s.key;
    if (key == U"ナッ") {
        if (AfterClause(p, end)) return "그치";
    } else if (key == U"サア" || key == U"サァ") {
        // さあ asked back (さあ？) is the shrug 글쎄, not the call 자.
        if (end < rs_.size() && In(rs_[end], U"？?")) return "글쎄";
    } else if (key == U"ヘ") {
        if (end < rs_.size() && In(rs_[end], U"？?")) return "어";  // へ？ asked back
    } else if (key == U"ウウン") {
        // ううん is the answer no (아니) said once and plainly; drawn out, cut off by っ or
        // asking before ？ it is a hum or a moan (으응).
        const bool asked = end < rs_.size() && In(rs_[end], U"？?");
        if (end - p != key.size() || asked || (end < rs_.size() && In(rs_[end], U"ッっー～"))) return "으응";
    } else if (key == U"ポリポリ") {
        const std::u32string_view line = Line(p);
        if (ContainsAny(line, kEatingCues) && !ContainsAny(line, kScratchingCues)) return "오독오독";
    } else if (key == U"イヤァ" || key == U"イヤア" || key == U"イヤー") {
        return IyaReading(p, end);
    } else if (key == U"イザ") {
        if (ContainsAny(Clause(end), kConditionals)) return "막상";
    } else if (key == U"タダイマ") {
        if (PresentTime(end)) return "지금";
    } else if (key == U"ハン") {
        if (Scoff(p, end)) return "흥";
    }
    return s.korean;
}

// Whether the ただいま that ends at character end means "now": a kanji follows it
// (ただいま満室です), or a comma and a clause of something in progress (ただいま、電話が
// 混雑しております).
bool SoundText::PresentTime(size_t end) const {
    if (end >= f_.size()) return false;
    if (IsKanjiRune(f_[end])) return true;
    return f_[end] == U'、' && ContainsAny(Clause(end), kProgressCues);
}

// Whether the はん at characters [p, end) is a scoff: it opens an utterance, ！ 、 or 。 follows,
// and the word after them is no sound (はん！こんなの; not はんっ、あんっ).
bool SoundText::Scoff(size_t p, size_t end) const {
    if (!UtteranceStart(p) || end >= rs_.size() || !In(rs_[end], U"！!、，。")) return false;
    size_t i = end;
    while (i < rs_.size() && In(rs_[i], U"！!、，。　 ")) i++;
    SoundMatch m;
    return i == rs_.size() || !MatchAt(i, m);
}

// The text from character i to the end of its sentence, in katakana.
std::u32string_view SoundText::Clause(size_t i) const {
    size_t end = i;
    while (end < rs_.size() && !In(rs_[end], U"。！？!?」』\n")) end++;
    return std::u32string_view(f_).substr(i, end - i);
}

// The line around character p, in katakana.
std::u32string_view SoundText::Line(size_t p) const {
    size_t start = p, end = p;
    while (start > 0 && f_[start - 1] != U'\n') start--;
    while (end < f_.size() && f_[end] != U'\n') end++;
    return std::u32string_view(f_).substr(start, end - start);
}

// Whether the sound なっ at characters [p, end) follows a clause on its line: a letter,
// digit or closing bracket comes before it with spaces and sentence punctuation (one at
// least) between, the kana word that ends there is not a sound nor a single kana (ん、
// なっ, ひぃっ！ なっ), and no word starting with な follows (なっ、なに, なっ…何).
bool SoundText::AfterClause(size_t p, size_t end) const {
    ptrdiff_t i = static_cast<ptrdiff_t>(p) - 1;
    while (i >= 0 && In(rs_[i], U" 　、。！？!?…‥・")) i--;
    if (i < 0 || i == static_cast<ptrdiff_t>(p) - 1 ||
        (!text::IsLetter(rs_[i]) && !text::IsDigit(rs_[i]) && !In(rs_[i], U"」』）)"))) {
        return false;
    }
    ptrdiff_t j = i;
    while (j >= 0 && IsKana(rs_[j])) j--;
    const std::u32string_view word = std::u32string_view(f_).substr(j + 1, i - j);
    if ((j < 0 || !text::IsLetter(rs_[j])) && (word.size() == 1 || (word.size() > 1 && IsSoundKey(word, ctx_.extraBuiltin)))) return false;
    const size_t next = NextWord(end);
    return next == f_.size() || !In(f_[next], U"ナ何");
}

// The first character at or after i that is not a sound separator.
size_t SoundText::NextWord(size_t i) const {
    while (i < rs_.size() && In(rs_[i], kSoundSeparators)) i++;
    return i;
}

// The reading of いやぁ at characters [p, end) (see Reading).
std::string SoundText::IyaReading(size_t p, size_t end) const {
    size_t before = p;
    while (before > 0 && In(rs_[before - 1], kSoundSeparators)) before--;
    if (before < p) {
        const size_t from = before >= 4 ? before - 4 : 0;
        if (std::u32string_view(f_).substr(from, before - from).find(U"イヤ") != std::u32string_view::npos) return "싫어";
    }
    const size_t i = NextWord(end);
    const std::u32string_view next = Clause(i);
    if (next.empty() && i < rs_.size() && In(rs_[i], U"！!")) {
        size_t j = i;
        while (j < rs_.size() && In(rs_[j], U"！!？?")) j++;
        if (j == rs_.size() || In(rs_[j], U"」』 　\n")) return "싫어";
    }
    for (const auto r : kRefusals) {
        if (next.starts_with(r)) return "싫어";
    }
    if (next.find(U"ナイデ") != std::u32string_view::npos) return "싫어";
    if (ContainsAny(next, kDeclines)) return "아니";
    return "이야";
}

// Whether a sound that ends in ッ ends there: what follows is no kana, or ト, テ, ー, ～,
// or the sound's first kana again (ぶるっぶる); otherwise the ッ belongs to a verb
// (くらっちゃった).
bool SoundText::Stopped(const SoundMatch& m) const {
    if (f_[m.end - 1] != U'ッ' || m.end == f_.size()) return true;
    const char32_t r = f_[m.end];
    return !IsKana(r) || In(r, U"トテー～") || r == f_[m.start];
}

// The chain of sounds that starts at character p, the last one taking the と that
// follows it, or an empty chain. afterParticle tells that the token before p is a lone
// particle.
std::vector<SoundMatch> SoundText::Sounds(size_t p, bool afterParticle) const {
    const std::u32string_view rs = rs_;
    const char32_t r = rs[p];
    const char32_t prev = p > 0 ? rs[p - 1] : 0;
    const bool katakana = IsKatakanaRune(r) && !IsKatakanaRune(prev);
    const bool runStart = IsHiraganaRune(r) && !IsHiraganaRune(prev) && prev != U'ー' && prev != U'～';
    afterParticle = afterParticle && IsHiraganaRune(r);
    std::vector<SoundMatch> chain;
    if (!katakana && !runStart && !afterParticle) return chain;
    bool loose = false;  // the first sound took a ッ that the next sound starts from
    for (size_t i = p; i < rs.size();) {
        SoundMatch m;
        if (!MatchAt(i, m, i > p)) break;
        // ねぇ and なぁ right after another sound are its closing particle, not a call (やだねぇ
        // 싫어, not 싫어 저기); they call only where an utterance starts, after a pause or
        // after another call (ねぇねぇねぇ).
        auto call = [](std::u32string_view k) { return k.size() >= 2 && In(k[0], U"ネナ") && In(k[1], U"エェアァー"); };
        if (!chain.empty() && m.key.size() == 2 && call(m.key) && !call(chain.back().key)) {
            chain.back().end = i = m.end;
            break;
        }
        i = m.end;
        chain.push_back(std::move(m));
    }
    if (chain.empty()) {
        // A run whose first sound ends in a ッ that the next sound starts from
        // (はぎいっくしゅん): retry with the end rule of a chain's later sounds. A
        // single sound still has to end by itself, so the ッ of a word that follows
        // (はっきり) stays with its word. A burst of breath said as one syllable is
        // written as heard (ぎゃっはっ, げっほっ), so it is not a run of sounds.
        if (Burst(p) > 0) return {};
        loose = true;
        for (size_t i = p; i < rs.size();) {
            SoundMatch m;
            if (!MatchAt(i, m, true)) break;
            i = m.end;
            chain.push_back(std::move(m));
        }
        if (chain.size() < 2) return {};
    }
    const SoundMatch& first = chain[0];
    if (katakana && IsKanjiRune(prev) && first.keyLen < 3) return {};
    if (katakana) {
        const size_t end = KatakanaEnd(rs, p);
        if (chain.back().end < end) return {};
        for (const SoundMatch& m : chain) {
            if (m.start < end && m.key != first.key && std::min(m.keyLen, first.keyLen) < 4) return {};
        }
    } else if ((afterParticle && first.intj) ||
               (runStart && InWord(prev) && (first.intj || first.keyLen < 4 || In(r, kParticles)))) {
        return {};
    }
    while (!chain.empty()) {
        SoundMatch& last = chain.back();
        size_t end = last.end;
        if (!last.intj) end += Adverbial(end);
        if (!last.intj || last.open || end == rs.size() || !IsKana(rs[end]) ||
            (IsHiraganaRune(rs[end - 1]) && IsKatakanaRune(rs[end]))) {
            last.end = end;
            if (loose && chain.size() < 2) return {};
            return chain;
        }
        chain.pop_back();
    }
    return chain;
}

// Whether a space goes between a sound and rs[i]: a kanji, katakana after hiragana, or
// kana that start a verb other than する, or follow a dropped と or て.
bool SoundText::Spaced(size_t i) const {
    const std::u32string_view rs = rs_;
    if (i >= rs.size()) return false;
    if (IsKanjiRune(rs[i]) || (IsKatakanaRune(rs[i]) && IsHiraganaRune(rs[i - 1]))) return true;
    if (!IsKana(rs[i]) || In(rs[i], U"しすさせ")) return false;
    if (In(rs[i - 1], U"とて")) return true;
    return IsHiraganaRune(rs[i]) && StartsVerb(i);
}

// Whether the longest dictionary word at character i is a verb.
bool SoundText::StartsVerb(size_t i) const {
    const dict::Candidates rd =
        snap_.GetWordInfo(Bytes(i, std::min(rs_.size(), i + 24)), dict::LookupMode::General, ctx_.lookup);
    size_t longest = 0;
    bool verb = false;
    for (const dict::Candidate& c : rd.list) {
        if (c.user != nullptr && c.user->kana) continue;
        const bool isVerb = c.codes != nullptr && !c.codes->part.Empty() && c.codes->part.Tag() == 'M';
        if (c.source.size() > longest) {
            longest = c.source.size();
            verb = isVerb;
        } else if (c.source.size() == longest && isVerb) {
            verb = true;
        }
    }
    return verb && longest > offsets_[i + 1] - offsets_[i];
}

// The length in characters of the longest dictionary key at character p, at least 1.
size_t SoundText::WordLen(size_t p) const {
    size_t base = 0, user = 0, name = 0;
    LongestKeys(p, base, user, name);
    return std::max({base, user, name, size_t{1}});
}

// The length of the と, or the て of って, that makes an ono row ending at character i
// an adverb, or 0: the particle must not start a longer word (とても) that reaches at
// least as far as the word after it (とお of とお尻 does not), nor join two sounds
// (ドキドキとワクワク); before another katakana word it stays adverbial (スタスタとベッドから).
size_t SoundText::Adverbial(size_t i) const {
    const std::u32string_view rs = rs_;
    if (i >= rs.size()) return 0;
    if (SoundMatch next; i + 1 < rs.size() && IsKatakanaRune(rs[i + 1]) && MatchAt(i + 1, next)) return 0;
    if (rs[i] != U'と' && !(rs[i] == U'て' && (rs[i - 1] == U'っ' || rs[i - 1] == U'ッ'))) return 0;
    return StartsLongerWord(i) ? 0 : 1;
}

// Whether a dictionary word of two or more characters starts at the particle at character
// i and reaches at least as far as the word after the particle (とても, but not とお of とお尻).
bool SoundText::StartsLongerWord(size_t i) const {
    if (i + 1 >= rs_.size() || !IsJapaneseRune(rs_[i + 1])) return false;
    size_t base = 0, user = 0, name = 0;
    LongestKeys(i, base, user, name);
    if (base <= 1) return false;
    size_t nextBase = 0, nextUser = 0, nextName = 0;
    LongestKeys(i + 1, nextBase, nextUser, nextName);
    return base >= 1 + std::max(nextBase, nextUser);
}

// The character after the と that a sound ending at character i takes across a pause: a
// run of 、 ！ ？ or an ellipsis, spaces, then と that starts no longer word (ぱさり、と
// 툭, 하고; ダンッ！　と 쾅! 하고; but not うん、とても or ふふ、という) and is followed by
// neither another と nor, after an ellipsis, a pause, which make it a stutter or a filler
// (うわ！ ととと, やっ……と、 ううう……と、), nor by a form of する (ぎゅっ、として), and that
// starts no stutter (ひいい！？　と、とと、溶ける); 0 otherwise.
size_t SoundText::PausedTo(size_t i) const {
    const std::u32string_view rs = rs_;
    size_t j = i;
    while (j < rs.size() && In(rs[j], U"、，！!？?…‥")) j++;
    if (j == i) return 0;
    while (j < rs.size() && text::IsSpace(rs[j])) j++;
    if (j >= rs.size() || rs[j] != U'と') return 0;
    if (j + 1 < rs.size() && (In(rs[j + 1], U"とトしすさせ") ||
                              (In(rs[j + 1], U"、，…‥") && rs.substr(i, j - i).find_first_of(U"…‥") != std::u32string_view::npos))) {
        return 0;
    }
    // A と that starts a stutter is the first sound of the next word (と、とと、溶ける).
    std::vector<StutterPiece> pieces;
    if (Stutter(j, pieces) > 0) return 0;
    return StartsLongerWord(j) ? 0 : j + 1;
}

// The end of the stutter at character p and its pieces, or 0: a kana syllable (with
// small ゃゅょ and ッ, but not a sound of sound.tsv such as あっ; phrases.tsv words such
// as おっ stutter; a small vowel stutters its plain vowel, ぃ――いや) at the start of a
// phrase, then a separator, repeated, then longer pieces that each begin the next (see
// the loops), before a word that starts with the same kana (but not で、 or じゃ、) or with
// a kanji that is no numeral (after a comma or a dash not for kNotStutters, after a dash
// not for a sigh, unless the syllable is said twice; with ッ not for kSoundsBeforeKanji:
// えっ……本当); or a syllable with ッ directly before a kanji, unless kSoundsBeforeKanji
// holds it, with the separator 、.
size_t SoundText::Stutter(size_t p, std::vector<StutterPiece>& pieces) const {
    const std::u32string_view rs = rs_, f = f_;
    pieces.clear();
    if (p > 0 && !text::IsSpace(rs[p - 1]) && !In(rs[p - 1], U"「『（(“‘〈《【。、，！？!?…‥―─—")) return 0;
    if ((!IsHiraganaRune(rs[p]) && !IsKatakanaRune(rs[p])) || In(f[p], U"ッンーャュョヮ")) return 0;
    // が that opens a line, not an utterance, is the conjunction (　が……本当に; 「が、頑張る」),
    // unless it is said again (が、が、頑張ります).
    if (rs[p] == U'が') {
        size_t q = p;
        while (q > 0 && In(rs[q - 1], U" 　")) q--;
        const size_t k = StutterSeparator(rs, p + 1);
        const bool again = k != 0 && p + 1 + k < rs.size() && rs[p + 1 + k] == U'が';
        if ((q == 0 || rs[q - 1] == U'\n') && !again) return 0;
    }
    // A small vowel stutters only the plain vowel that starts the word (ぃ――いや).
    const char32_t smallOf = In(f[p], U"ァィゥェォ") ? PlainVowel(f[p]) : 0;
    size_t n = 1;
    if (p + n < rs.size() && In(f[p + n], U"ャュョ")) n++;
    if (p + n < rs.size() && f[p + n] == U'ッ') n++;
    if (f[p + n - 1] == U'ッ' && p + n < rs.size() && IsKanjiRune(rs[p + n]) && !In(f[p], kSoundsBeforeKanji)) {
        pieces.push_back({U"、"});
        return p + n;
    }
    const std::u32string_view x = f.substr(p, n);
    if (n > 1 && IsSoundKey(x, ctx_.extraBuiltin)) return 0;
    size_t i = p;
    while (f.substr(i, n) == x) {
        const size_t k = StutterSeparator(rs, i + n);
        if (k == 0) break;
        pieces.push_back({rs.substr(i + n, k)});
        i += n + k;
    }
    if (pieces.empty() || i >= rs.size()) {
        pieces.clear();
        return 0;
    }
    // Longer pieces that each begin the next (ご、ごめ、ごめんなさい): a piece is up to four
    // kana before a separator. The pieces are taken only as far as each begins the next
    // and the last begins the kana word after it, so a piece is never read as a word of
    // its own; a piece that does not chain is the word itself (わ、わ、わたし……).
    std::vector<std::pair<size_t, size_t>> longer;  // start and length of each longer piece
    for (size_t j = i; j < rs.size() && f.substr(j, n) == x;) {
        size_t e = j + n;
        while (e < rs.size() && e - j < 4 && IsKana(rs[e]) && StutterSeparator(rs, e) == 0) e++;
        const size_t k = StutterSeparator(rs, e);
        if (k == 0 || e == j + n) break;
        longer.push_back({j, e - j});
        j = e + k;
    }
    // The kana word at character a.
    const auto kanaWord = [&](size_t a) {
        size_t z = a;
        while (z < rs.size() && IsKana(rs[z])) z++;
        return f.substr(a, z - a);
    };
    while (!longer.empty()) {
        const auto& [lastStart, lastLength] = longer.back();
        const size_t after = lastStart + lastLength + StutterSeparator(rs, lastStart + lastLength);
        const std::u32string_view word = kanaWord(after);
        // The word without its lengthening, and without an ending the dictionary does not
        // hold in the word (すきです: すき). A word said again is no piece of it
        // (おのれ、おのれぇーっ; すき、すきです).
        std::u32string_view stem = word;
        while (!stem.empty() && In(stem.back(), U"ァィゥェォッー～")) stem.remove_suffix(1);
        size_t base = 0, user = 0, name = 0;
        LongestKeys(after, base, user, name);
        if (const size_t key = std::max(base, user); key > lastLength && key < stem.size()) stem = stem.substr(0, key);
        bool chained = stem.size() > lastLength && word.starts_with(f.substr(lastStart, lastLength));
        for (size_t k = 0; chained && k + 1 < longer.size(); k++) {
            const std::u32string_view next = f.substr(longer[k + 1].first, longer[k + 1].second);
            chained = next.size() > longer[k].second && next.starts_with(f.substr(longer[k].first, longer[k].second));
        }
        if (chained) {
            for (const auto& [start, length] : longer) {
                const size_t e = start + length;
                pieces.push_back({rs.substr(e, StutterSeparator(rs, e)), length, stem.size()});
            }
            return after;
        }
        longer.pop_back();
    }
    // Before a kanji word the kana cannot be compared: a speaker who starts the word again
    // after a restart (、 or a dash) says a piece of two kana at most (ち、ちが、違う), so the
    // piece is never translated. A dictionary word, a sound or a demonstrative says
    // something of its own (お、おい、魅; あ、あれ、何), and a pause after
    // a word keeps the word (そ、そんな……嘘).
    for (size_t j = i; j < rs.size() && f.substr(j, n) == x;) {
        size_t e = j + n;
        while (e < rs.size() && e - j < 4 && IsKana(rs[e]) && StutterSeparator(rs, e) == 0) e++;
        const size_t k = StutterSeparator(rs, e);
        if (k == 0 || e == j + n || e - j > 2 || !In(rs[e], U"、，―─—") || e + k >= rs.size() ||
            !IsKanjiRune(rs[e + k]) || NumeralRune(rs[e + k])) {
            break;
        }
        const std::u32string_view piece = f.substr(j, e - j);
        bool word = IsSoundKey(piece, ctx_.extraBuiltin) || WordAt(j, e - j);
        for (const std::u32string_view w : kStandalonePieces) word = word || w == piece;
        if (word) break;
        pieces.push_back({rs.substr(e, k)});
        j = e + k;
        i = j;
    }
    const bool comma = In(rs[p + n], U"、，");
    std::u32string_view bare = x;
    if (bare.ends_with(U'ッ')) bare.remove_suffix(1);
    bool notStutter = false;
    for (const auto s : kNotStutters) notStutter = notStutter || s == bare;
    // Before a dash a sigh or groan stands alone too (う――痛い, ふ――危なかった).
    const bool dash = In(rs[p + n], U"―─—");
    if (dash && bare.size() == 1 && In(bare[0], U"ハヒフヘホ")) notStutter = true;
    // A syllable said twice is a stutter whatever it is (さ、さ、最低).
    if (pieces.size() > 1) notStutter = false;
    // The restore stage copies the next Korean syllable, which a numeral does not give.
    if (NumeralRune(rs[i])) {
        pieces.clear();
        return 0;
    }
    if (smallOf != 0) {
        if (f[i] == smallOf) return i;
        pieces.clear();
        return 0;
    }
    // で、 and じゃ、 inside a line are the conjunction (それで、でも); opening an utterance
    // before a word they start again they stutter it (で、でも 그, 그래도).
    if (f[i] == f[p] && ((x != U"デ" && x != U"ジャ") || UtteranceStart(p))) return i;
    const bool sound = x.ends_with(U'ッ') && In(f[p], kSoundsBeforeKanji);
    if (IsKanjiRune(rs[i]) && !((comma || dash) && notStutter) && !sound) return i;
    pieces.clear();
    return 0;
}

// The end of the burst at character p, or 0: a syllable, then ハ ヒ フ ヘ or ホ, with ッ
// after either or both (かはっ, だっは, かッハ, げはッ; hiragana, or hiragana and
// katakana), that starts an utterance or follows 、 or an ellipsis and ends before a
// pause, an ellipsis, a dash, ～ or a space, when no dictionary key of two or more
// characters other than an ending or the key of a sound row that is no lead row reaches
// into the second syllable (では, とは).
size_t SoundText::Burst(size_t p) const {
    const std::u32string_view rs = rs_, f = f_;
    if (!(IsHiraganaRune(rs[p]) || IsKatakanaRune(rs[p])) || In(f[p], U"ッンーァィゥェォャュョヮ")) return 0;
    if (!UtteranceStart(p) && !In(rs[p - 1], U"、…‥")) return 0;
    size_t i = p + 1;
    if (i < f.size() && In(f[i], U"ャュョ")) i++;
    const bool tsuBefore = i < f.size() && f[i] == U'ッ';
    if (tsuBefore) i++;
    if (i >= f.size() || !In(f[i], U"ハヒフヘホ")) return 0;
    const size_t second = i++;
    const bool tsuAfter = i < f.size() && f[i] == U'ッ';
    if (tsuAfter) i++;
    if (!tsuBefore && !tsuAfter) return 0;
    if (i < rs.size() && !PauseAt(i) && !In(rs[i], U"…‥―～ 　")) return 0;
    bool hiragana = false;
    for (size_t k = p; k < i; k++) hiragana = hiragana || IsHiraganaRune(rs[k]);
    if (!hiragana) return 0;
    for (size_t k = p; k <= second; k++) {
        const dict::Candidates rd = snap_.GetWordInfo(Bytes(k, i), dict::LookupMode::General, ctx_.lookup);
        for (const dict::Candidate& c : rd.list) {
            const size_t n = text::RuneCount(c.source);
            if (n < 2 || k + n <= second || c.codes == nullptr || c.codes->part.TagIn("SU")) continue;
            const std::u32string key = ToKatakana(DecodeText(c.source));
            const auto rows = SoundRows(key[0], ctx_.extraBuiltin);
            if (std::none_of(rows.begin(), rows.end(), [&](const SoundRow& r) { return r.key == key && !r.phrase && !r.lead; })) {
                return 0;
            }
        }
    }
    return i;
}

// The end of a lone っ or ッ run at p that an utterance holds after nothing but an
// ellipsis or a dash, before a pause, an ellipsis, a dash or the end (……ッ！, ――っ、), or 0:
// a caught breath, written 읏.
size_t SoundText::LoneCatch(size_t p) const {
    const std::u32string_view rs = rs_;
    if (rs[p] != U'っ' && rs[p] != U'ッ') return 0;
    size_t q = p;
    while (q > 0 && In(rs[q - 1], U"…‥―─")) q--;
    if (!UtteranceStart(q)) return 0;
    size_t end = p;
    while (end < rs.size() && (rs[end] == U'っ' || rs[end] == U'ッ')) end++;
    if (end < rs.size() && !In(rs[end], U"、。！？!?…‥」』）―─")) return 0;
    return end;
}

// The Korean of a lone ん, small vowel or hiragana く ぐ や は ひ ふ at p that stands between
// punctuation as a voice of its own (んっ、ん、んくっ; ……ぅ……; あっ、く、うぅ), or "".
std::string_view SoundText::LoneVoice(size_t p) const {
    const std::u32string_view rs = rs_;
    constexpr std::u32string_view kVoices = U"ンァィゥェォくぐやはひふ";
    constexpr std::string_view kKorean[] = {"응", "아", "이", "우", "에", "오", "크", "그", "야", "하", "히", "후"};
    size_t v = kVoices.find(f_[p]);
    if (v == std::u32string_view::npos) v = kVoices.find(rs[p]);  // hiragana only
    if (v == std::u32string_view::npos) return {};
    if (p > 0 && !In(rs[p - 1], U"「『（(、，…‥―─—\n！？!?。")) return {};
    if (p + 1 < rs.size() && !In(rs[p + 1], U"、，…‥―─—」』）)！？!?。\n")) return {};
    if (rs[p] == U'は') {
        // The particle after a word and a pause (木曾…は、, 私――は――) or before a closing
        // bracket (「は」).
        size_t q = p;
        while (q > 0 && In(rs[q - 1], U"…‥―─—")) q--;
        if (q < p && q > 0 && (text::IsLetter(rs[q - 1]) || In(rs[q - 1], U"）)」』"))) return {};
        if (p + 1 < rs.size() && In(rs[p + 1], U"」』）)")) return {};
    }
    return kKorean[v];
}

// The Korean of the ono row read by the katakana run [p, end) (or the run with ッ) when the
// run starts an utterance or follows 、 or an ellipsis and an exclamation mark and と or っと
// follow it (ドン！っと 쾅!), where a dictionary word would read it otherwise (ドン 돈); ""
// otherwise.
std::string SoundText::ExclaimedSound(size_t p, size_t end) const {
    const std::u32string_view rs = rs_;
    if (end + 1 >= rs.size() || !In(rs[end], U"！!") || !(UtteranceStart(p) || In(rs[p - 1], U"、…‥"))) return {};
    size_t i = end + 1;
    if (rs[i] == U'っ') i++;
    if (i >= rs.size() || rs[i] != U'と') return {};
    const std::u32string run(std::u32string_view(f_).substr(p, end - p));
    for (const SoundRow& s : SoundRows(f_[p], ctx_.extraBuiltin)) {
        if (!s.intj && !s.phrase && !s.lead && (s.key == run || s.key == run + U"ッ")) return s.korean;
    }
    return {};
}

// The end of the と or っと after the katakana run [p, end) when the run is a mimetic the
// sound lexicon lacks, or 0: a run of two or more characters ending in ッ (ジッと, バキャッと)
// or a unit of two to four characters said twice (メキメキと, ガッシャガッシャと), after a
// character other than katakana, that no dictionary or name-layer key reads whole, before
// と or っと that does not quote (メキメキという) or start する. hiragana is set when a
// dictionary word reads a run ending in ッ with と in hiragana (ジッと じっと).
size_t SoundText::MimeticTo(size_t p, size_t end, bool& hiragana) const {
    hiragana = false;
    const std::u32string_view f = f_;
    const size_t n = end - p;
    if (n < 2 || (p > 0 && IsKatakanaRune(rs_[p - 1]))) return 0;
    const bool tsu = f[end - 1] == U'ッ' && f[end - 2] != U'ッ' && f[p] != U'ッ' && f[p] != U'ー';
    const bool doubled = n % 2 == 0 && n / 2 >= 2 && n / 2 <= 4 && f.substr(p, n / 2) == f.substr(p + n / 2, n / 2) &&
                         f[p] != U'ッ' && f[p] != U'ー';
    if (!tsu && !doubled) return 0;
    size_t i = end;
    if (i < rs_.size() && rs_[i] == U'っ') i++;
    if (i >= rs_.size() || rs_[i] != U'と') return 0;
    i++;
    const std::u32string_view rest = std::u32string_view(rs_).substr(i);
    for (const std::u32string_view q : {U"いう", U"いっ", U"言", U"思", U"か", U"は", U"も", U"の"}) {
        if (rest.starts_with(q)) return 0;
    }
    // A key that covers the run, with the と or what follows (ホッと, デカデカと), reads it.
    const dict::Candidates rd =
        snap_.GetWordInfo(Bytes(p, std::min(rs_.size(), i + 4)), dict::LookupMode::General, ctx_.lookup);
    for (const dict::Candidate& c : rd.list) {
        if (text::RuneCount(c.source) >= n) return 0;
    }
    for (const char32_t r : std::u32string_view(rs_).substr(p, end - p)) {
        if (!IsKatakanaRune(r)) return 0;
    }
    std::u32string h(std::u32string_view(rs_).substr(p, i - p));
    for (char32_t& r : h) {
        if (r >= U'ァ' && r <= U'ヶ') r -= 0x60;
    }
    // An adverb with と in hiragana (じっと), but not before する (じっとして); a doubled
    // katakana unit is the sound rather than the hiragana adverb (メキメキと, not めきめきと).
    if (i < rs_.size() && In(rs_[i], U"しすさせ")) return 0;
    if (!tsu) return i;
    const dict::Candidates kana = snap_.GetWordInfo(EncodeText(h), dict::LookupMode::General, ctx_.lookup);
    for (const dict::Candidate& c : kana.list) {
        if (text::RuneCount(c.source) == i - p) hiragana = true;
    }
    return i;
}

// Whether the two kana at character p are a word rather than a sound, so that the unit
// said twice repeats the word: a call or discourse word of the sound lexicon (a lead row
// or a phrases.tsv row: ねえ, ほら, こら, いざ) or a verb in the dictionary form whose kana
// stem the dictionary reads by default (XXX: いく 가다; not ふる, which has no default).
bool SoundText::KanaWord(size_t p) const {
    const std::u32string_view unit = std::u32string_view(f_).substr(p, 2);
    for (const SoundRow& s : SoundRows(unit[0], ctx_.extraBuiltin)) {
        if (s.key == unit && (s.lead || s.phrase)) return true;
    }
    const std::string_view stem = Bytes(p, p + 1), ending = Bytes(p + 1, p + 2);
    const dict::Candidates rd = snap_.GetWordInfo(Bytes(p, p + 2), dict::LookupMode::General, ctx_.lookup);
    for (const dict::Candidate& c : rd.list) {
        if (c.source != stem || c.codes == nullptr || c.codes->part.Empty() || c.codes->part.Tag() != 'M') continue;
        if (c.codes->sem.HasCode("XXX") && word::Conjugation(c.codes->part.VerbKind(), 0) == ending) return true;
    }
    return false;
}

// The end of the と or っと after a hiragana mimetic the sound lexicon lacks at character
// p, or 0: a unit of two plain kana said twice (とてとてと, ふるふると) that is no word
// (KanaWord: ねえねえと, いくいくと) and that no dictionary or name-layer key of four or
// more characters reads there, before と or っと that does not quote (ふるふるという) or
// start する.
size_t SoundText::HiraganaMimeticTo(size_t p) const {
    const std::u32string_view rs = rs_, f = f_;
    constexpr size_t n = 4;
    if (p + n >= rs.size()) return 0;
    for (size_t k = p; k < p + n; k++) {
        if (!IsHiraganaRune(rs[k]) || In(rs[k], U"っゃゅょぁぃぅぇぉゎ")) return 0;
    }
    if (f.substr(p, 2) != f.substr(p + 2, 2) || f[p] == f[p + 1]) return 0;
    size_t i = p + n;
    if (rs[i] == U'っ') i++;
    if (i >= rs.size() || rs[i] != U'と') return 0;
    i++;
    const std::u32string_view rest = rs.substr(i);
    for (const std::u32string_view q : {U"いう", U"いっ", U"言", U"思", U"か", U"は", U"も", U"の"}) {
        if (rest.starts_with(q)) return 0;
    }
    if (i < rs.size() && In(rs[i], U"しすさせ")) return 0;
    if (KanaWord(p)) return 0;
    const dict::Candidates rd =
        snap_.GetWordInfo(Bytes(p, std::min(rs.size(), i + 4)), dict::LookupMode::General, ctx_.lookup);
    for (const dict::Candidate& c : rd.list) {
        if (text::RuneCount(c.source) >= n) return 0;
    }
    return i;
}

std::string SoundText::Write() {
    const std::u32string_view rs = rs_;
    std::string b;
    b.reserve(text_.size() + text_.size() / 2);
    bool afterParticle = false;  // the token before rs[i] is a lone particle
    size_t wordEnd = 0;          // the end of the last word of more than one character
    std::vector<StutterPiece> pieces;
    for (size_t i = 0; i < rs.size();) {
        if (const size_t skip = OpaqueLength(text_.substr(offsets_[i]), offsets_[i] > 0 ? text_[offsets_[i] - 1] : '\0', ctx_.markers); skip > 0) {
            afterParticle = false;
            b += text_.substr(offsets_[i], skip);
            const size_t end = offsets_[i] + skip;
            while (i < rs.size() && offsets_[i] < end) i++;
            continue;
        }
        size_t next = i + 1;
        bool particle = false;
        std::vector<SoundMatch> chain;
        if (const size_t end = Stutter(i, pieces); end > 0) {
            for (const StutterPiece& piece : pieces) {
                text::AppendRune(b, kStutterMark);
                if (piece.word > 0) {
                    for (size_t k = 1; k < piece.kana; k++) text::AppendRune(b, kStutterMark);
                    for (size_t k = piece.kana; k < piece.word; k++) text::AppendRune(b, kStutterRest);
                }
                b += EncodeText(piece.separator);
            }
            next = end;
            stutterEnd_ = end;
        } else if (chain = Sounds(i, afterParticle); !chain.empty()) {
            next = chain.back().end;
            const bool doing = next < rs.size() && In(rs[next], U"しすさせ");
            for (size_t k = 0; k < chain.size(); k++) {
                SoundMatch& m = chain[k];
                if (k > 0 && Separate(chain[k - 1], m)) b += ' ';
                if (k == chain.size() - 1 && doing && !m.root.empty()) {
                    b += m.root;
                } else {
                    b += m.korean;
                }
            }
            // A と after a pause quotes the sound: 하고 after the pause (ぱさり、と落ちた 툭, 하고 떨어졌다).
            if (const size_t to = chain.back().phrase || rs[next - 1] == U'と' ? 0 : PausedTo(next); to > 0) {
                b += Bytes(next, to - 1);
                if (!text::IsSpace(rs[to - 2])) b += ' ';
                b += "하고";
                next = to;
                if (next < rs.size() && !In(rs[next], U"、。，！？!?…‥」』）)")) b += ' ';
            } else if (Spaced(next) || chain.back().spaceAfter) {
                b += ' ';
            }
        } else if (const size_t lone = LoneCatch(i); lone > 0) {
            b += "읏";
            next = lone;
        } else if (const std::string_view voice = LoneVoice(i); !voice.empty()) {
            b += voice;
        } else if (const size_t burst = Burst(i); burst > 0) {
            b += text::FromKana(EncodeText(std::u32string_view(f_).substr(i, burst - i)), text::Preset::Fan);
            next = burst;
        } else if (const size_t to = IsHiraganaRune(rs[i]) && (i == 0 || !IsHiraganaRune(rs[i - 1]) || afterParticle)
                                         ? HiraganaMimeticTo(i)
                                         : 0;
                   to > 0) {
            // A hiragana sound the lexicon lacks, as heard, the と after it dropped (とてとてと 토테토테).
            b += text::FromKana(EncodeText(std::u32string_view(f_).substr(i, 4)), text::Preset::Fan);
            if (Spaced(to)) b += ' ';
            next = to;
        } else if (bool hiragana = false; IsKatakanaRune(rs[i]) && MimeticTo(i, KatakanaEnd(rs, i), hiragana) > 0) {
            const size_t run = KatakanaEnd(rs, i);
            if (hiragana) {
                // The dictionary reads it in hiragana (ジッと じっと 가만히).
                for (size_t k = i; k < run; k++) text::AppendRune(b, rs[k] >= U'ァ' && rs[k] <= U'ヶ' ? rs[k] - 0x60 : rs[k]);
                next = run;
            } else {
                // An ono sound: the と after it is dropped (メキメキと 메키메키).
                const size_t toEnd = MimeticTo(i, run, hiragana);
                b += text::FromKana(EncodeText(std::u32string_view(f_).substr(i, run - i)), text::Preset::Fan);
                if (Spaced(toEnd)) b += ' ';
                next = toEnd;
            }
        } else {
            if (IsKatakanaRune(rs[i])) {
                next = KatakanaEnd(rs, i);
                if (const std::string korean = ExclaimedSound(i, next); !korean.empty()) {
                    b += korean;
                    i = next;
                    afterParticle = false;
                    continue;
                }
            } else if (IsHiraganaRune(rs[i]) || IsKanjiRune(rs[i])) {
                next = std::min(rs.size(), i + WordLen(i));
                particle = next == i + 1 && In(rs[i], kParticles);
                if (next > i + 1) wordEnd = next;
                // A word in kanji that takes its particle (煙が, 香りが) ends like a lone one.
                if (next > i + 1 && IsKanjiRune(rs[i]) && In(rs[next - 1], U"はがをにもでへ")) particle = true;
                // は, が or を after a word stays a particle before a sound adverb in と that a
                // kana word would swallow (彼はこくりと, not はこ; 香りがもわりと, not がも).
                if (!particle && In(rs[i], U"はがを") && i > 0 && (IsKanjiRune(rs[i - 1]) || wordEnd == i)) {
                    const std::vector<SoundMatch> after = Sounds(i + 1, true);
                    if (!after.empty() && rs[after.back().end - 1] == U'と') {
                        next = i + 1;
                        particle = true;
                    }
                }
            }
            b += Bytes(i, next);
        }
        afterParticle = particle;
        i = next;
    }
    return b;
}

}  // namespace

namespace {

// The syllable a key repeats at its end, as kana: X or a small tsu with X (ハハ, ッハッハ,
// クッ+クッ), when the Korean also ends in a doubled syllable (아하하, 킥킥); empty otherwise
// and for a vowel, which lengthens instead (ぐおお).
std::u32string_view RepeatedUnit(const SoundRow& row) {
    const std::u32string_view key = row.key;
    const std::u32string ko = DecodeText(row.korean);
    if (ko.size() < 2 || ko[ko.size() - 1] != ko[ko.size() - 2]) return {};
    for (size_t n = 1; n <= 2 && 2 * n <= key.size(); n++) {
        const std::u32string_view unit = key.substr(key.size() - n);
        if (key.substr(key.size() - 2 * n, n) != unit) continue;
        size_t syllables = 0;
        for (const char32_t r : unit) {
            if (r == U'ッ') continue;
            if (In(r, U"ンーァィゥェォャュョ") || PlainVowel(r) != 0) return {};
            syllables++;
        }
        if (syllables == 1) return unit;
    }
    return {};
}

// The characters after f[i:] that repeat unit, one unit for each: 'h' when unit holds ッ,
// 'r' otherwise.
size_t Repeats(std::u32string_view unit, std::u32string_view f, size_t i, std::string& units) {
    const char mark = unit.find(U'ッ') != std::u32string_view::npos ? 'h' : 'r';
    size_t n = 0;
    while (!unit.empty() && f.substr(i + n).starts_with(unit)) {
        n += unit.size();
        units += mark;
    }
    return n;
}

}  // namespace

bool MatchSound(const SoundRow& row, std::u32string_view f, size_t p, size_t& end, std::string& units) {
    units.clear();
    const std::u32string_view unit = RepeatedUnit(row);
    std::u32string_view stem = row.key;
    char32_t tail = 0;
    if (const size_t n = row.key.size(); n >= 2 && (row.key[n - 1] == U'ン' || row.key[n - 1] == U'ッ')) {
        stem = stem.substr(0, n - 1);
        tail = row.key[n - 1];
    }
    if (p + stem.size() > f.size()) return false;
    bool tilde = false;  // a ～ stands for the key's ー (う～ん)
    for (size_t k = 0; k < stem.size(); k++) {
        const char32_t r = stem[k];
        if (r == U'ー' && f[p + k] == U'～') {
            tilde = true;
            continue;
        }
        // A small vowel that starts a word stands for the plain one (ぅあっ as うあっ).
        const bool weakOnset = k == 0 && In(f[p], kSmallVowels) && PlainVowel(f[p]) == r && (p == 0 || !text::IsLetter(f[p - 1]));
        if (f[p + k] != r && !weakOnset &&
            (row.key.size() < 3 || PlainVowel(r) == 0 || PlainVowel(f[p + k]) != PlainVowel(r))) {
            return false;
        }
    }
    char32_t last = stem.back();
    if (last == U'ー' && stem.size() > 1) last = stem[stem.size() - 2];
    size_t i = p + stem.size();
    if (tail == 0) i += Repeats(unit, f, i, units);
    for (; i < f.size() && Lengthens(f[i], last); i++) {
        if (f[i] != U'～') {
            units += 'v';
        } else if (units.empty() || units.back() != '~') {
            units += '~';
        }
    }
    if (tail != 0) {
        if (i >= f.size() || f[i] != tail) return false;
        i++;
        i += Repeats(unit, f, i, units);
        for (; i < f.size() && (f[i] == U'ー' || f[i] == U'～' || f[i] == U'ッ' || f[i] == tail); i++) {
        }
        end = i;
        if (tilde) units += 't';
        return true;
    }
    size_t j = i;
    while (j < f.size() && f[j] == U'ッ') j++;
    if (j > i && (j == f.size() || !IsKana(f[j]) || In(f[j], U"トテ～ー") || (row.intj && !row.phrase))) {
        units += '!';
        for (i = j; i < f.size() && In(f[i], U"～ー"); i++) {
        }
    }
    end = i;
    if (tilde) units += 't';
    return true;
}

std::string Lengthen(std::string_view korean, std::string_view units) {
    if (const size_t t = units.find('t'); t != std::string_view::npos) {
        std::string rest(units);
        std::erase(rest, 't');
        std::string out = Lengthen(korean, rest);
        if (!out.empty() && out.find('~') == std::string::npos) out += '~';
        return out;
    }
    std::u32string rs = DecodeText(korean);
    if (rs.empty() || units.empty()) return std::string(korean);
    if (units[0] == 'r') {
        const size_t n = std::min(units.find_first_not_of('r'), units.size());
        rs.append(n, rs.back());
        std::string rest(units.substr(n));
        if (std::erase(rest, '!') > 0 && rs.back() >= 0xAC00 && rs.back() <= 0xD7A3 && (rs.back() - 0xAC00) % 28 == 0) {
            rs.back() += static_cast<char32_t>(19);  // ㅅ
        }
        return Lengthen(EncodeText(rs), rest);
    }
    const char32_t last = rs.back();
    if (last < 0xAC00 || last > 0xD7A3) return std::string(korean);
    const int s = static_cast<int>(last - 0xAC00);
    const int initial = s / 588, vowel = s % 588 / 28;
    int final = s % 28;
    if (rs.size() > 1 && initial != 11) {
        if (units.find_first_of("v~") != std::string_view::npos) return std::string(korean) + "~";
        return std::string(korean);
    }
    rs.back() = Syllable(initial, vowel, 0);
    int added = 0;
    for (const char u : units) {
        if (u == 'v' && added < kMaxLengthening) {
            rs.push_back(Syllable(11, kLongVowel[vowel], 0));
            added++;
        } else if (u == '~') {
            rs.push_back(U'~');
        } else if (u == '!' && final == 0) {
            final = 19;  // ㅅ
        }
    }
    if (final != 0) {
        if (rs.back() == U'~') {
            rs.push_back(Syllable(11, kLongVowel[vowel], final));
        } else {
            rs.back() += static_cast<char32_t>(final);
        }
    }
    return EncodeText(rs);
}

std::string WriteSounds(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    SoundText t(text, ctx, snap);
    return t.Write();
}

namespace {

// Whether a dash or an ellipsis starts at rs[i]: ― — … ‥, or a run of two ・ or ー.
bool CutMarkAt(std::u32string_view rs, size_t i) {
    if (i >= rs.size()) return false;
    if (In(rs[i], U"―—…‥")) return true;
    return In(rs[i], U"・ー") && i + 1 < rs.size() && rs[i + 1] == rs[i];
}

// The kana that complete the cut-off verb frag (kanji up to kanjiEnd, then hiragana),
// or "".
std::string Completion(std::u32string_view frag, size_t kanjiEnd, const LineContext& ctx, const dict::Snapshot& snap) {
    for (size_t s = 0; s < kanjiEnd; s++) {
        const std::string text = EncodeText(frag.substr(s));
        const dict::Candidates rd = snap.GetWordInfo(text, dict::LookupMode::General, ctx.lookup);
        for (const dict::Candidate& c : rd.list) {
            if (c.codes == nullptr || c.codes->part.Tag() != 'M' || c.codes->part.ClassDigit() != '4') continue;
            if (s + text::RuneCount(c.source) < kanjiEnd || !text.starts_with(c.source)) continue;
            const std::string_view rest = std::string_view(text).substr(c.source.size());
            const int kind = c.codes->part.VerbKind();
            const std::string_view te = word::Conjugation(kind, 7), neg = word::Conjugation(kind, 8);
            if (!rest.empty() && te.size() > rest.size() && te.starts_with(rest)) return std::string(te.substr(rest.size()));
            if (!neg.empty() && rest.size() == neg.size() + 3 && rest.starts_with(neg) && rest.ends_with("な")) return "い";
        }
    }
    return "";
}

}  // namespace

std::string CompleteCutOffs(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    const std::u32string rs = DecodeText(text);
    std::string out;
    size_t done = 0;
    for (size_t i = 1; i < rs.size(); i++) {
        if (!CutMarkAt(rs, i)) continue;
        size_t kana = i;
        while (kana > 0 && i - kana < 4 && IsHiraganaRune(rs[kana - 1])) kana--;
        if (kana == i || kana == 0 || !IsKanjiRune(rs[kana - 1])) continue;
        size_t start = kana - 1;
        while (start > 0 && IsKanjiRune(rs[start - 1])) start--;
        const std::string add = Completion(std::u32string_view(rs).substr(start, i - start), kana - start, ctx, snap);
        if (add.empty()) continue;
        out += EncodeText(std::u32string_view(rs).substr(done, i - done)) + add;
        done = i;
    }
    if (done == 0) return std::string(text);
    return out + EncodeText(std::u32string_view(rs).substr(done));
}

}  // namespace internal

std::string Sound(std::string_view normalized, const LineContext& ctx, const dict::Snapshot& snap) {
    const std::string completed =
        internal::CompleteCutOffs(internal::WriteMasked(internal::JoinSpelledOut(normalized, ctx, snap), ctx, snap), ctx, snap);
    std::string sounded =
        internal::RewriteCasualAdjectives(internal::WriteSounds(internal::RewriteVolitionalSo(completed, snap), ctx, snap), ctx, snap);
    if (std::string plain = internal::RewriteEmphasis(sounded, ctx, snap); plain != sounded) {
        sounded = internal::WriteSounds(plain, ctx, snap);
    }
    return sounded;
}

}  // namespace hdTrans::pipeline
