#include "core/pipeline/sound.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

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

// The kana whose syllable with ッ stands alone before a kanji (あっ提督, えっ本当,
// ぶっ殺す); for other kana the syllable and ッ directly before a kanji are a stutter
// (ちっ違う, しっ司令官), written as if a comma followed.
constexpr std::u32string_view kSoundsBeforeKanji = U"アウエオクケゲグサナネハヒフブヘホマムヤヨワン";

// The length of a stutter separator at rs[i:]: 、 ， or a run of … ‥ or of two or more ・.
size_t StutterSeparator(std::u32string_view rs, size_t i) {
    if (i >= rs.size()) return 0;
    switch (rs[i]) {
        case U'、':
        case U'，':
            return 1;
        case U'…':
        case U'‥':
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

size_t KatakanaEnd(std::u32string_view rs, size_t p) {
    while (p < rs.size() && IsKatakanaRune(rs[p])) p++;
    return p;
}

// A sound found in the text: the characters [start, end) are written as korean.
struct SoundMatch {
    size_t start = 0, end = 0, keyLen = 0;
    std::u32string_view key;  // in katakana
    std::string korean;
    std::string_view root;
    bool intj = false, lead = false;
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

// The length of the opaque span at the start of s: a tag, BBCode tag, entity or marker.
size_t OpaqueLength(std::string_view s, std::span<const std::string> markers) {
    if (const size_t n = TagLength(s); n > 0) return n;
    if (const size_t n = BbCodeLength(s); n > 0) return n;
    if (const size_t n = EntityLength(s); n > 0) return n;
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

// Whether the sound rows read f[at:] as exactly one AB unit.
bool UnitIsSound(std::u32string_view f, size_t at, std::u32string_view unit, bool extra) {
    std::string units;
    for (const SoundRow& row : SoundRows(f[at], extra)) {
        size_t end = 0;
        if (MatchSound(row, f, at, end, units) && end - at == unit.size()) return true;
    }
    return false;
}

// The length in characters of the run of AB units at f[p:] when a plain chain of
// sounds cannot read it: the lengthening after a unit takes the vowel that starts the
// next (うずうずうず), an odd last unit has no sound row of its own (ドキドキドキ), or a
// longer key splits the run (はあはあはあはあ). 0 otherwise. Pairs of units are
// written as one block each, so no half unit is left.
size_t AbabRun(const SoundRow& row, std::u32string_view f, size_t p, bool extra) {
    const std::u32string_view unit = std::u32string_view(row.key).substr(0, kAbabUnit);
    size_t n = 0;
    while (p + n + unit.size() <= f.size() && f.substr(p + n, unit.size()) == unit) n += unit.size();
    if (n < 3 * unit.size()) return 0;
    const size_t count = n / unit.size();
    if (!Lengthens(unit[0], unit[1])) {
        if (count % 2 == 0) return 0;
        if (UnitIsSound(f, p + n - unit.size(), unit, extra)) return 0;
    }
    return n;
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
    void LongestKeys(size_t p, size_t& base, size_t& user, size_t& name) const;
    bool LongerWordAt(size_t p, size_t n) const;
    bool MatchAt(size_t p, SoundMatch& best, bool inChain = false) const;
    std::string Reading(const SoundRow& s, size_t p, size_t end) const;
    bool PresentTime(size_t end) const;
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
    size_t Stutter(size_t p, std::vector<std::u32string_view>& seps) const;
    size_t Burst(size_t p) const;

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

// Whether there is a pause at character p: the end of the text or a comma, sentence end
// or closing bracket (not an ellipsis: ちょっと…痛い).
bool SoundText::PauseAt(size_t p) const { return p >= rs_.size() || In(rs_[p], U"、，,！？!?。」』）)\n"); }

// The length in characters of the longest base key, plain-dictionary key and name-layer
// key that start at character p; kana spellings do not count.
void SoundText::LongestKeys(size_t p, size_t& base, size_t& user, size_t& name) const {
    base = user = name = 0;
    const size_t end = std::min(rs_.size(), p + 24);
    const dict::Candidates rd = snap_.GetWordInfo(Bytes(p, end), dict::LookupMode::General, ctx_.lookup);
    for (const dict::Candidate& c : rd.list) {
        if (c.user != nullptr && c.user->kana) continue;
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

// Whether a dictionary word longer than n characters starts at character p.
bool SoundText::LongerWordAt(size_t p, size_t n) const {
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
    for (const SoundRow& s : SoundRows(f_[p], ctx_.extraBuiltin)) {
        const bool abab = AbabKey(s);
        if (chosen != nullptr && s.key.size() < chosen->key.size() && !abab) break;
        size_t end = 0;
        bool ok = MatchSound(s, f_, p, end, units);
        std::string runKorean;
        if (ok && abab) {
            size_t n = AbabRun(s, f_, p, ctx_.extraBuiltin);
            // A word that starts in the last unit and runs past it (うずら in
            // うずうずうずら) takes that unit: the run ends before it.
            while (n >= 3 * kAbabUnit && LongerWordAt(p + n - kAbabUnit, kAbabUnit)) n -= kAbabUnit;
            if (n >= 2 * kAbabUnit) {
                end = p + n;
                const size_t copies = (n / kAbabUnit + 1) / 2;
                runKorean.assign(s.korean);
                for (size_t i = 1; i < copies; i++) runKorean += ' ' + s.korean;
            }
        }
        if (ok && s.lead) {
            if (!UtteranceStart(p) || !PauseAt(end) || IsKatakanaRune(rs_[p])) continue;
            std::erase(units, '!');  // よしっ 좋아, not 좋앗
        }
        if (ok && s.key.back() == U'ト' && !EndsInTo(std::u32string_view(rs_).substr(p, end - p))) {
            ok = false;  // a final と is the particle, not katakana ト
        }
        if (ok && (chosen == nullptr || end > best.end)) {
            std::string korean = !runKorean.empty()
                                     ? std::move(runKorean)
                                     : units.find('h') != std::string::npos
                                           ? text::FromKana(EncodeText(std::u32string_view(f_).substr(p, end - p)),
                                                            text::Preset::Fan)
                                           : Lengthen(Reading(s, p, end), units);
            best = SoundMatch{p, end, s.key.size(), s.key, std::move(korean), s.root, s.intj, s.lead};
            chosen = &s;
        }
    }
    if (chosen == nullptr) return false;
    if (!Stopped(best) && !(inChain && best.intj && !chosen->phrase)) return false;
    if (best.lead) return true;  // an utterance-initial interjection before a pause wins over words
    size_t base = 0, user = 0, name = 0;
    LongestKeys(p, base, user, name);
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
//   - ただいま is 지금 before a clause (PresentTime), 다녀왔습니다 otherwise.
std::string SoundText::Reading(const SoundRow& s, size_t p, size_t end) const {
    const std::u32string_view key = s.key;
    if (key == U"ナッ") {
        if (AfterClause(p, end)) return "그치";
    } else if (key == U"ポリポリ") {
        const std::u32string_view line = Line(p);
        if (ContainsAny(line, kEatingCues) && !ContainsAny(line, kScratchingCues)) return "오독오독";
    } else if (key == U"イヤァ" || key == U"イヤア" || key == U"イヤー") {
        return IyaReading(p, end);
    } else if (key == U"イザ") {
        if (ContainsAny(Clause(end), kConditionals)) return "막상";
    } else if (key == U"タダイマ") {
        if (PresentTime(end)) return "지금";
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
        if (!last.intj || end == rs.size() || !IsKana(rs[end]) || (IsHiraganaRune(rs[end - 1]) && IsKatakanaRune(rs[end]))) {
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
// an adverb, or 0: the particle must not start a longer word (とても) nor come before
// katakana.
size_t SoundText::Adverbial(size_t i) const {
    const std::u32string_view rs = rs_;
    if (i >= rs.size() || (i + 1 < rs.size() && IsKatakanaRune(rs[i + 1]))) return 0;
    if (rs[i] != U'と' && !(rs[i] == U'て' && (rs[i - 1] == U'っ' || rs[i - 1] == U'ッ'))) return 0;
    if (i + 1 < rs.size() && IsJapaneseRune(rs[i + 1])) {
        size_t base = 0, user = 0, name = 0;
        LongestKeys(i, base, user, name);
        if (base > 1) return 0;
    }
    return 1;
}

// The end of the stutter at character p and its separators, or 0: a kana syllable
// (with small ゃゅょ and ッ, but not a sound of sound.tsv such as あっ; phrases.tsv words
// such as おっ stutter) at the start of a phrase, then a separator, repeated, before a
// word that starts with the same kana (but not で、 or じゃ、) or with a kanji (after a
// comma not for kNotStutters, and with ッ not for kSoundsBeforeKanji: えっ……本当); or a
// syllable with ッ directly before a kanji, unless kSoundsBeforeKanji holds it, with the
// separator 、.
size_t SoundText::Stutter(size_t p, std::vector<std::u32string_view>& seps) const {
    const std::u32string_view rs = rs_, f = f_;
    seps.clear();
    if (p > 0 && !text::IsSpace(rs[p - 1]) && !In(rs[p - 1], U"「『（(“‘〈《【。、！？!?…‥―")) return 0;
    if ((!IsHiraganaRune(rs[p]) && !IsKatakanaRune(rs[p])) || In(f[p], U"ッンーァィゥェォャュョヮ")) return 0;
    size_t n = 1;
    if (p + n < rs.size() && In(f[p + n], U"ャュョ")) n++;
    if (p + n < rs.size() && f[p + n] == U'ッ') n++;
    if (f[p + n - 1] == U'ッ' && p + n < rs.size() && IsKanjiRune(rs[p + n]) && !In(f[p], kSoundsBeforeKanji)) {
        seps.push_back(U"、");
        return p + n;
    }
    const std::u32string_view x = f.substr(p, n);
    if (n > 1 && IsSoundKey(x, ctx_.extraBuiltin)) return 0;
    size_t i = p;
    while (f.substr(i, n) == x) {
        const size_t k = StutterSeparator(rs, i + n);
        if (k == 0) break;
        seps.push_back(rs.substr(i + n, k));
        i += n + k;
    }
    if (seps.empty() || i >= rs.size()) {
        seps.clear();
        return 0;
    }
    const bool comma = In(rs[p + n], U"、，");
    std::u32string_view bare = x;
    if (bare.ends_with(U'ッ')) bare.remove_suffix(1);
    bool notStutter = false;
    for (const auto s : kNotStutters) notStutter = notStutter || s == bare;
    if (f[i] == f[p] && x != U"デ" && x != U"ジャ") return i;
    const bool sound = x.ends_with(U'ッ') && In(f[p], kSoundsBeforeKanji);
    if (IsKanjiRune(rs[i]) && !(comma && notStutter) && !sound) return i;
    seps.clear();
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

std::string SoundText::Write() {
    const std::u32string_view rs = rs_;
    std::string b;
    b.reserve(text_.size() + text_.size() / 2);
    bool afterParticle = false;  // the token before rs[i] is a lone particle
    std::vector<std::u32string_view> seps;
    for (size_t i = 0; i < rs.size();) {
        if (const size_t skip = OpaqueLength(text_.substr(offsets_[i]), ctx_.markers); skip > 0) {
            afterParticle = false;
            b += text_.substr(offsets_[i], skip);
            const size_t end = offsets_[i] + skip;
            while (i < rs.size() && offsets_[i] < end) i++;
            continue;
        }
        size_t next = i + 1;
        bool particle = false;
        std::vector<SoundMatch> chain;
        if (const size_t end = Stutter(i, seps); end > 0) {
            for (const auto sep : seps) {
                text::AppendRune(b, kStutterMark);
                b += EncodeText(sep);
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
            if (Spaced(next)) b += ' ';
        } else if (const size_t burst = Burst(i); burst > 0) {
            b += text::FromKana(EncodeText(std::u32string_view(f_).substr(i, burst - i)), text::Preset::Fan);
            next = burst;
        } else {
            if (IsKatakanaRune(rs[i])) {
                next = KatakanaEnd(rs, i);
            } else if (IsHiraganaRune(rs[i]) || IsKanjiRune(rs[i])) {
                next = std::min(rs.size(), i + WordLen(i));
                particle = next == i + 1 && In(rs[i], kParticles);
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
    for (size_t k = 0; k < stem.size(); k++) {
        const char32_t r = stem[k];
        if (f[p + k] != r && (row.key.size() < 3 || PlainVowel(r) == 0 || PlainVowel(f[p + k]) != PlainVowel(r))) {
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
    return true;
}

std::string Lengthen(std::string_view korean, std::string_view units) {
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
    const std::string completed = internal::CompleteCutOffs(internal::WriteMasked(normalized, ctx, snap), ctx, snap);
    std::string sounded = internal::RewriteCasualAdjectives(internal::WriteSounds(completed, ctx, snap), ctx, snap);
    if (std::string plain = internal::RewriteEmphasis(sounded, ctx, snap); plain != sounded) {
        sounded = internal::WriteSounds(plain, ctx, snap);
    }
    return sounded;
}

}  // namespace hdTrans::pipeline
