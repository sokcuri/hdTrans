#include "core/names/registry.h"
#include "core/names/given.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <set>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/names/chars.h"
#include "core/text/utf8.h"
#include "core/names/labels.h"
#include "core/names/readings.h"
#include "core/names/ruby.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"

namespace hdTrans::names {

using namespace internal;

namespace {

// The numbers one to ten read in katakana, after a name in a numbered name (サーラサン).
constexpr std::string_view kNumberReadings[] = {"イチ", "ニ", "サン", "ヨン", "ゴ", "ロク", "ナナ", "ハチ", "キュウ", "ジュウ"};

// Thresholds of the evidence score.
constexpr double kThreshold = 2.0;
constexpr double kSingleThreshold = 3.0;  // single-kanji surfaces without a label
constexpr int kHonorificCap = 4;          // honorific occurrences counted per surface

// Whether the dictionaries read t as one surname or given name (上田, 中山, 林太郎), which is no
// surname and given name of its own. A given name of the name layer counts up to three
// characters; a longer one is a full name there too (小林芳雄).
bool OneName(const DictFlags& f, std::string_view t) {
    return f.surname || f.given || f.layerSurname || (f.layerGiven && text::RuneCount(t) <= 3);
}

// The byte offsets of the code point starts of s, in order.
std::vector<std::size_t> RuneStarts(std::string_view s) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < s.size(); i += RuneAt(s, i).size) out.push_back(i);
    return out;
}

bool IsRuneStartByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }

// Whether the text after a word gives it as a name: と名乗る, と名付ける (シロとか名付けよう).
bool NamingCue(std::string_view after) {
    for (const std::string_view q : {"と", "とか", "って"}) {
        if (!after.starts_with(q)) continue;
        const std::string_view verb = after.substr(q.size());
        if (verb.starts_with("名乗") || verb.starts_with("名付け") || verb.starts_with("名づけ")) return true;
    }
    return false;
}

// Whether the text after a word makes it the doer of a favour in its clause: に with
// てもらう or 頼む and no other に before it (Xに作ってもらった, Xに頼んで), or が with てくれる
// (Xが作ってくれた).
bool AgentCue(std::string_view after) {
    std::string_view clause = after;
    for (const std::string_view end : {"。", "！", "？", "」", "『", "「", "、"}) {
        const std::size_t at = clause.find(end);
        if (at != std::string_view::npos) clause = clause.substr(0, at);
    }
    auto has = [&](std::string_view rest, std::initializer_list<std::string_view> list) {
        for (const std::string_view w : list) {
            if (rest.find(w) != std::string_view::npos) return true;
        }
        return false;
    };
    if (clause.starts_with("に")) {
        std::string_view rest = clause.substr(std::string_view("に").size());
        rest = rest.substr(0, rest.find("に"));
        return has(rest, {"てもら", "でもら", "頼"});
    }
    if (clause.starts_with("が")) return has(clause, {"てくれ", "でくれ"});
    return false;
}

// Whether text ends with suffix as a whole string with a non-empty head.
bool HasSuffixOf(std::string_view text, std::string_view suffix) {
    return text.size() > suffix.size() && text.ends_with(suffix);
}

// Whether the next text after a name ends the name: no hiragana, or a particle or an
// honorific.
bool NameEnd(std::string_view text) {
    if (text.empty()) return true;
    const char32_t c = RuneAt(text, 0).cp;
    if (!IsHiragana(c) || InRunes(c, "はがをのにへともやでかだ")) return true;
    return dict::StartsWithMember(HDTRANS_CLASS("honorific.name"), text) > 0;
}

// The non-empty fields of s separated by ・ or ＝.
std::vector<std::string> ChainParts(std::string_view s) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size();) {
        const bool sep = i < s.size() && (s.substr(i).starts_with("・") || s.substr(i).starts_with("＝"));
        if (i == s.size() || sep) {
            if (i > start) parts.emplace_back(s.substr(start, i - start));
            const std::size_t width = sep ? std::string_view("・").size() : 0;
            i += width == 0 ? 1 : width;
            start = i;
            continue;
        }
        i++;
    }
    return parts;
}

}  // namespace

Registry::Registry(const dict::Snapshot* snap) : snap_(snap), tables_(std::make_shared<Tables>()) {
    internal::AddBuiltinReadings(tables_->readings);
}

void Registry::SetName(std::string_view t, std::string_view kr) {
    if (!t.empty() && IsHangulText(kr)) {
        scoresValid_ = false;
        if (tables_.use_count() != 1) tables_ = std::make_shared<Tables>(*tables_);
        tables_->fixed[std::string(t)] = std::string(kr);
    }
}

void Registry::AddListed(const Listed& name) {
    if (name.ja.empty() || name.ko.empty()) return;
    scoresValid_ = false;
    listed_[name.ja] = name;
}

void Registry::SetReading(const std::string& key, const std::string& value) {
    if (value.empty()) return;
    if (reading_[key].empty()) reading_[key] = value;
}

void Registry::SetActor(const std::string& key, const std::string& value) {
    actors_[key] = value;
}

// Whether a gloss or ruby reading marks a word's reading, not a name: the base is a common
// word, or ends in one, and the reading is one too: its own reading (私（わたくし）,
// 夾叉（きょうさ）) or another word read in its place (提督（あなた）, 各艦（ふね）). A
// reading that is no word still names (三日（みか）, 千里（ちさと）), unless it is in no
// dictionary at all and the base is only a word, or a word that also names a place
// (用途（ようと）, 飯（めし）); and so does one that is also a name, unless a word of it has the
// base's Korean (秋刀魚（さんま）: 꽁치). A reading over a name, or one that is only a name, is none.
// Whether a gloss reading of a common word is the sound of its place row and not of any name-layer row:
// the word read aloud, the way the place took its name (大力（だいりき）: 큰힘, the place 다이리키, not the
// surname 오오리키); one that is a layer name's sound names a person (千鳥（ちどり）さん).
bool Registry::WordAloud(std::string_view base, std::string_view reading) {
    if (snap_ == nullptr) return false;
    const DictFlags b = Flags(base);
    if (!b.common || !b.word || b.person || !b.city || !(b.layerSurname || b.layerGiven)) return false;
    for (const dict::Record& rec : snap_->NameRecords(base)) {
        if (text::SpellsSound(reading, rec.target, false)) return false;
    }
    for (const dict::Candidate& rec : snap_->RetrieveRecords(base, dict::LookupMode::General)) {
        if (rec.codes != nullptr && rec.source == base && rec.codes->sem.HasCode("QCT") && text::SpellsSound(reading, rec.target, false)) {
            return true;
        }
    }
    return false;
}

bool Registry::Gikun(std::string_view base, std::string_view reading) {
    const DictFlags r = Flags(reading);
    // A reading over a name of no common sense names it when the reading is a name too or no
    // word of its own meaning (龍生《たつき》; not 水馬（あめんぼ）), and a reading that is a name
    // of the name layer and no common word names a base that is no word (二刃《ふたば》; not
    // 朝日（あさひ）).
    bool proper = true, sound = true;
    if (snap_ != nullptr) {
        for (const dict::Candidate& rec : snap_->RetrieveRecords(reading, dict::LookupMode::General)) {
            if (rec.codes == nullptr || rec.source != reading || rec.codes->sem.HasCode("QPN")) continue;
            proper = proper && rec.codes->part.ProperNoun();
            sound = sound && text::SpellsSound(reading, rec.target, false);
        }
    }
    const DictFlags named = Flags(base);
    const bool readingName = r.person || r.layerGiven || r.layerSurname;
    if ((named.person || named.layerGiven || named.layerSurname) && !named.common && (readingName || sound)) return false;
    if ((r.layerGiven || r.layerSurname) && proper && !named.common) return false;
    if (r.records == 0 && !r.layerSurname && !r.layerGiven) {
        const DictFlags b = Flags(base);
        return b.common && !b.person && (!b.city || b.word) && !b.layerSurname && !b.layerGiven;
    }
    if (!r.common) return false;
    if (r.person) {
        if (snap_ == nullptr) return false;
        const auto words = [&](std::string_view s) {
            std::set<std::string_view> out;
            for (const dict::Candidate& rec : snap_->RetrieveRecords(s, dict::LookupMode::General)) {
                if (rec.codes == nullptr || rec.source != s) continue;
                const std::string_view sem = rec.codes->sem.Text(), attr = rec.codes->gram.Text();
                if (sem.starts_with("QPN") || sem.starts_with("QCT") || attr.starts_with("Wa") || attr.starts_with("Wb")) {
                    continue;
                }
                out.insert(rec.target);
            }
            return out;
        };
        const std::set<std::string_view> own = words(base);
        const std::set<std::string_view> read = words(reading);
        if (std::none_of(read.begin(), read.end(), [&](std::string_view t) { return own.count(t) != 0; })) return false;
    }
    // The base, or the word that ends it (各艦（ふね）).
    for (std::string_view b = base; !b.empty(); b.remove_prefix(text::DecodeRune(b).size)) {
        const DictFlags f = Flags(b);
        if (f.common && !f.person) return true;
        if (text::DecodeRune(b).size == 0) break;
    }
    return false;
}

// The Korean of the words of the dictionaries written s, the kanji words s is a kana
// spelling of among them; names and places left out.
std::set<std::string_view> Registry::Words(std::string_view s) {
    std::set<std::string_view> out;
    if (snap_ == nullptr || s.empty()) return out;
    for (const dict::Candidate& rec : snap_->RetrieveRecords(s, dict::LookupMode::General)) {
        if (rec.codes == nullptr || rec.source != s || rec.target.empty()) continue;
        const std::string_view sem = rec.codes->sem.Text(), attr = rec.codes->gram.Text();
        if (sem.starts_with("QPN") || sem.starts_with("QCT") || attr.starts_with("Wa") || attr.starts_with("Wb")) continue;
        out.insert(rec.target);
    }
    for (const dict::UserEntry* e : snap_->TextPlain().Search(s, dict::LookupMode::General)) {
        if (e->key != s || e->nameLayer || e->target.empty()) continue;
        out.insert(e->target);
    }
    return out;
}

namespace {

// The content-word classes of WordClasses.
constexpr unsigned kNounClass = 1, kVerbClass = 2, kAdjectiveClass = 4, kAdjectivalNounClass = 8;

}  // namespace

// The content-word classes of the words written s: noun, verb, adjective and adjectival
// noun (N11, N12 and I11, M4, H7, G8), names and places left out.
unsigned Registry::WordClasses(std::string_view s) {
    unsigned out = 0;
    if (snap_ == nullptr || s.empty()) return out;
    auto add = [&](const dict::Codes& codes) {
        const std::string_view sem = codes.sem.Text(), part = codes.partText;
        if (sem.starts_with("QPN") || sem.starts_with("QCT")) return;
        if (part.starts_with("N11") || part.starts_with("N12") || part.starts_with("I11")) out |= kNounClass;
        if (part.starts_with("M4")) out |= kVerbClass;
        if (part.starts_with("H7")) out |= kAdjectiveClass;
        if (part.starts_with("G8")) out |= kAdjectivalNounClass;
    };
    for (const dict::Candidate& rec : snap_->RetrieveRecords(s, dict::LookupMode::General)) {
        if (rec.codes != nullptr && rec.source == s) add(*rec.codes);
    }
    for (const dict::UserEntry* e : snap_->TextPlain().Search(s, dict::LookupMode::General)) {
        if (e->key == s && !e->nameLayer && e->codes != nullptr) add(*e->codes);
    }
    return out;
}

namespace {

// The ends of the first kana of after, up to three, after none: the okurigana a reading may
// go on with.
std::vector<std::size_t> KanaEnds(std::string_view after) {
    std::vector<std::size_t> ends{0};
    for (std::size_t k = 0; k < after.size() && ends.size() <= 3 && IsHiragana(RuneAt(after, k).cp);) {
        k += RuneAt(after, k).size;
        ends.push_back(k);
    }
    return ends;
}

// Whether every kanji run of line has a reading in 「」 after it, written straight after the
// run or after its closing ＞ (心「こころ」に刻「きざ」んだ, ＜場所＞「ばしょ」), two runs at
// least: a line styled with readings, whose 「」 after one kanji are readings too.
bool Styled(std::string_view line) {
    std::size_t runs = 0;
    for (std::size_t i = 0; i < line.size();) {
        if (!IsKanji(RuneAt(line, i).cp)) {
            i += RuneAt(line, i).size;
            continue;
        }
        while (i < line.size() && IsKanji(RuneAt(line, i).cp)) i += RuneAt(line, i).size;
        std::string_view rest = line.substr(i);
        if (rest.starts_with("＞")) rest.remove_prefix(std::string_view("＞").size());
        if (!rest.starts_with("「")) return false;
        runs++;
    }
    return runs >= 2;
}

}  // namespace

// Whether s and t are nouns of a kind: N11 records of both share a semantic code, or both
// are verbal nouns (Hd); names and places left out.
bool Registry::SameKind(std::string_view s, std::string_view t) {
    if (snap_ == nullptr) return false;
    auto codes = [&](std::string_view key) {
        std::set<std::string> out;
        for (const dict::Candidate& rec : snap_->RetrieveRecords(key, dict::LookupMode::General)) {
            if (rec.codes == nullptr || rec.source != key || !rec.codes->partText.starts_with("N11")) continue;
            const dict::SemCodes& sem = rec.codes->sem;
            if (sem.HasPrefix("QPN") || sem.HasPrefix("QCT")) continue;
            for (std::size_t i = 0; i < sem.Count(); i++) out.insert(std::string(sem.At(i)));
            if (rec.codes->gram.HasCode("Hd")) out.insert("Hd");
        }
        return out;
    };
    const std::set<std::string> a = codes(s);
    for (const std::string& c : codes(t)) {
        if (a.count(c) != 0) return true;
    }
    return false;
}

// Whether base and reading, each with the same first kana of after or none, are written
// words with a Korean in common, the reading being a word or a kana spelling of one
// (全「すべ」て: 全て and すべて 모두; 錆（さ）びない: 錆び and さび 녹; むね 胸 가슴).
bool Registry::SameWord(std::string_view base, std::string_view reading, std::string_view after) {
    for (const std::size_t k : KanaEnds(after)) {
        const std::set<std::string_view> own = Words(std::string(base) + std::string(after.substr(0, k)));
        for (const std::string_view t : Words(std::string(reading) + std::string(after.substr(0, k)))) {
            if (own.count(t) != 0) return true;
        }
    }
    return false;
}

// Whether a name or place record of base, in either dictionary, is written with the sound
// of reading (空 소라 and そら, 星 호시 and ほし): the dictionary reads base so.
bool Registry::NamedWithSound(std::string_view base, std::string_view reading) {
    if (snap_ == nullptr) return false;
    const std::string sound = text::FromKana(reading, text::Preset::Fan);
    if (sound.empty()) return false;
    // The names this registry writes drop the long vowels (Written).
    const std::string written = Written(reading);
    auto named = [&](const dict::Codes* codes, std::string_view target) {
        if (codes == nullptr || target.empty()) return false;
        const dict::SemCodes& sem = codes->sem;
        const std::string_view attr = codes->gram.Text();
        if (!sem.HasPrefix("QPN") && !sem.HasPrefix("QCT") && !attr.starts_with("Wa") && !attr.starts_with("Wb")) {
            return false;
        }
        return text::SameSound(target, sound) || text::SameSound(target, written);
    };
    for (const dict::Candidate& rec : snap_->RetrieveRecords(base, dict::LookupMode::General)) {
        if (rec.source == base && named(rec.codes, rec.target)) return true;
    }
    for (const dict::Record& rec : snap_->NameRecords(base)) {
        if (named(rec.codes, rec.target)) return true;
    }
    return false;
}

// Whether reading, in hiragana, is the reading of the kanji run base of a line that goes on
// with after. It is when it is the same word (SameWord), the sound a name record of base
// is written with (NamedWithSound: 空（そら）), or the reading of a name (水野澄香（すみか）);
// over one kanji, a word of the same class with the kana the word goes on with (駆「か」け),
// or with none a word of the same class that is no name (梁（はり）, but 猫（たま）,
// 妹（ゆき）), or in 「」 any reading in a styled line, one whose every kanji
// run has a 「」 reading (日「ひ」の誓「ちか」い); in parentheses, over a word of no
// dictionary (夾叉(きょうさ)), a noun of the same kind as the base (朝餉(あさがれい),
// 輜重(しちょう)), or no word at all (鮭（しゃけ）, 黄泉戸喫(よもつへぐい)), with a mora for
// each kanji of a word. A remark (結果（まあまあ）, 理由(なし)), a quote after a speaker
// (姉「うん」って), a name after a noun (店「さくら」で) or one that is shorter than its word
// (長良川鉄道株式会社（ながてつ）) is none.
bool Registry::BaseReading(std::string_view base, std::string_view reading, std::string_view after, bool bracket,
                           bool styled) {
    if (SameWord(base, reading, after) || NamedWithSound(base, reading)) return true;
    const DictFlags b = Flags(base), f = Flags(reading);
    const bool single = RuneCount(base) == 1;
    // A person of any dictionary, the name layer's too, but a common noun of one kanji that
    // is a name as well (猫（たま）).
    const bool person = (b.person || !b.layerName.empty()) && !(single && b.common);
    if (person && (f.person || !f.layerName.empty())) return true;
    // A quote after a person (姉「はい」って) is no reading of a word of the same class.
    if (bracket && after.starts_with("って") && (b.role || b.human || b.person)) return false;
    if (bracket && styled && single) return true;
    if (single) {
        // A verb or adjective stem that is no noun goes on with its okurigana: what stands
        // between them is its reading (励（はげ）んで).
        const unsigned own = WordClasses(base);
        if ((own & (kVerbClass | kAdjectiveClass)) != 0 && (own & kNounClass) == 0 && !after.empty() &&
            IsHiragana(RuneAt(after, 0).cp)) {
            return true;
        }
        const bool particle = !after.empty() && InRunes(RuneAt(after, 0).cp, "のにをはがでへもかや");
        // A name written over a noun (猫（たま）, 妹（ゆき）) names it.
        const bool name = f.person || f.layerGiven || f.layerSurname;
        for (const std::size_t k : KanaEnds(after)) {
            if (k == 0 && ((bracket && particle) || name)) continue;
            const std::string_view ok = after.substr(0, k);
            const std::string bk = std::string(base) + std::string(ok), rk = std::string(reading) + std::string(ok);
            if ((WordClasses(bk) & WordClasses(rk)) != 0) return true;
        }
    }
    if (bracket) return false;
    if (b.records == 0 && (snap_ == nullptr || snap_->NameRecords(base).empty())) return true;
    if (!single && SameKind(base, reading)) return true;
    if (f.records > 0 || f.layerSurname || f.layerGiven) return false;
    return person || ReadingMorae(reading) >= RuneCount(base);
}

std::string Registry::CaptionNameReading(std::string_view base, std::string_view reading) {
    const std::string written = text::FromKana(reading, text::Preset::FanShort, true);
    if (written.empty() || RuneCount(base) == 1) return written;
    const auto& names = Accepted();
    const auto found = names.find(std::string(base));
    if (found == names.end() || !found->second.hasParts) {
        return NamedWithSound(base, reading) ? written : std::string();
    }
    const std::vector<std::string>& parts = found->second.parts;
    if (parts.size() != 2 || parts[0] + parts[1] != base) return {};
    const std::string first = RenderPart(parts[0]), second = RenderPart(parts[1]);
    if (first.empty() || second.empty()) return {};
    std::string out;
    if (NamedWithSound(parts[0], reading)) out = written + " " + second;
    if (NamedWithSound(parts[1], reading)) {
        if (!out.empty()) return {};
        out = first + " " + written;
    }
    // Align only whole established parts; more than one matching span or kana cut abstains.
    for (const std::size_t at : RuneStarts(reading)) {
        if (at == 0) continue;
        const std::string_view left = reading.substr(0, at), right = reading.substr(at);
        // A known part anchors the cut; the other established part gets the supplied remainder.
        if (!NamedWithSound(parts[0], left) && !NamedWithSound(parts[1], right)) continue;
        const std::string leftWritten = text::FromKana(left, text::Preset::FanShort, true);
        const std::string rightWritten = text::FromKana(right, text::Preset::FanShort, true);
        if (leftWritten.empty() || rightWritten.empty()) continue;
        if (!out.empty()) return {};
        out = leftWritten + " " + rightWritten;
    }
    return out;
}

namespace {

// Whether the kana gloss reading can be the reading of the kanji or katakana base: over two kanji or
// more at most three morae each, as the kanji of a word read
// (有仁（ありひと）, 小鳥遊（たかなし）); a longer one is a remark (同系統（ということにしている）).
bool GlossFits(std::string_view base, std::string_view reading) {
    if (!AllRunes(base, IsKanji)) return true;
    const std::size_t kanji = RuneCount(base), morae = ReadingMorae(reading);
    return kanji < 2 || morae <= 3 * kanji;
}

// The run before the gloss m of line and the two parts of its reading, when a kanji run and one space
// or ・ stand right before the base and the reading is two runs of kana with one space between, each that
// fits its run (双寺　有仁（そうじ　ありひと）).
std::optional<std::pair<std::pair<std::string, std::string>, std::string>> SpacedPair(std::string_view line, const RubyMatch& m) {
    std::size_t gap = std::string_view::npos, gapSize = 0;
    for (const std::string_view sp : {"　", " "}) {
        if (const std::size_t at = m.reading.find(sp); at != std::string_view::npos) {
            gap = at;
            gapSize = sp.size();
            break;
        }
    }
    if (gap == std::string_view::npos || gap == 0) return std::nullopt;
    const std::string_view left = m.reading.substr(0, gap), right = m.reading.substr(gap + gapSize);
    if (right.empty() || right.find(' ') != std::string_view::npos || right.find("　") != std::string_view::npos) return std::nullopt;
    const std::string_view before = line.substr(0, m.begin);
    std::size_t end = before.size();
    if (before.ends_with("　")) {
        end -= std::string_view("　").size();
    } else if (before.ends_with(" ")) {
        end -= 1;
    } else if (before.ends_with("・") || before.ends_with("･")) {
        end -= before.ends_with("・") ? std::string_view("・").size() : std::string_view("･").size();
    } else {
        return std::nullopt;
    }
    std::size_t start = end;
    while (start > 0 && IsKanji(RuneBefore(line, start).cp)) start -= RuneBefore(line, start).size;
    if (start == end) return std::nullopt;
    const std::string_view run = line.substr(start, end - start);
    // Each part needs a mora for each kanji of its run (14番 御影玲王(みかげ れお) is one name over the base).
    const auto holds = [](std::string_view kanji, std::string_view kana) {
        return GlossFits(kanji, kana) && ReadingMorae(kana) >= RuneCount(kanji);
    };
    if (!holds(run, left) || !holds(m.base, right)) return std::nullopt;
    return std::make_pair(std::make_pair(std::string(run), std::string(left)), std::string(right));
}

}  // namespace

std::string Registry::DropReadings(std::string_view line, std::string* cueText, std::vector<std::string>* captionNames) {
    if (cueText != nullptr) cueText->clear();
    if (captionNames != nullptr) captionNames->clear();
    if (line.find("（") == std::string_view::npos && line.find('(') == std::string_view::npos &&
        line.find("「") == std::string_view::npos) {
        return std::string(line);
    }
    struct Found {
        RubyMatch m;
        std::string hiragana;
        std::size_t bareBegin;
        bool bracket;
    };
    std::vector<Found> found;
    const bool styled = Styled(line);
    std::string bare;
    std::size_t at = 0;
    RubyMatch m;
    for (std::size_t from = 0; FindReading(line, from, m); from = m.end) {
        bare.append(line.substr(at, m.begin - at));
        found.push_back({m, ToHiragana(m.reading), bare.size(), line.substr(m.begin + m.base.size()).starts_with("「")});
        bare.append(m.base);
        at = m.end;
    }
    if (found.empty()) return std::string(line);
    bare.append(line.substr(at));
    std::string out;
    bool captionText = false;
    at = 0;
    for (const Found& x : found) {
        const DictFlags b = Flags(x.m.base);
        const char32_t open = RuneAt(line, x.m.begin + x.m.base.size()).cp;
        const char32_t close = RuneBefore(line, x.m.end).cp;
        const bool paired = (open == U'(' && close == U')') || (open == U'（' && close == U'）');
        const bool person = b.person || !b.layerName.empty() || (b.records == 0 && !b.city);
        const bool captionAt = !x.m.spaced && paired && person && !RoleLike(x.m.base) && !LabelRole(x.m.base) &&
                               !dict::InClass(HDTRANS_CLASS("noun.bracket-note"), x.m.base) &&
                               BracketLabelAt(std::string_view(bare).substr(0, x.bareBegin),
                                              std::string_view(bare).substr(x.bareBegin + x.m.base.size()));
        const std::string captionReading = captionAt ? CaptionNameReading(x.m.base, x.m.reading) : std::string();
        const bool caption = !captionReading.empty();
        if (!caption) {
            const DictFlags f = Flags(x.hiragana);
            if (IsKatakana(FirstRune(x.m.reading).cp) && (!f.common || f.person)) continue;
            // A reading of two spaced runs (双寺　有仁（そうじ　ありひと）) is one; a remark too long to be the
            // reading stays (同系統（ということにしている）).
            const bool pair = SpacedPair(line, x.m).has_value();
            if (!pair && !GlossFits(x.m.base, x.m.reading)) continue;
            if (pair) {
                out.append(line.substr(at, x.m.begin - at));
                out.append(x.m.base);
                at = x.m.end;
                continue;
            }
            if (f.pronoun && b.human) continue;
            // Written apart from the base, only a reading the dictionaries confirm is one (山田 (やまだ),
            // not 条件 (つよい) or 選択肢 (はい・いいえ)).
            if (x.m.spaced ? !(SameWord(x.m.base, x.hiragana, line.substr(x.m.end)) || NamedWithSound(x.m.base, x.hiragana))
                           : !BaseReading(x.m.base, x.hiragana, line.substr(x.m.end), x.bracket, styled)) {
                continue;
            }
        }
        if (caption && captionNames != nullptr) captionNames->emplace_back(x.m.base);
        if (caption && cueText != nullptr && !captionText) {
            *cueText = out;
            captionText = true;
        }
        if (captionText) {
            cueText->append(line.substr(at, x.m.begin - at));
            cueText->append(x.m.base);
        }
        out.append(line.substr(at, x.m.begin - at));
        // Resolve the written reading here, only in this label occurrence; it is not a document alias.
        out.append(caption ? captionReading : std::string(x.m.base));
        at = x.m.end;
    }
    out.append(line.substr(at));
    if (captionText) cueText->append(line.substr(at));
    return out;
}

namespace {

// Whether the rest of a caption line after a word at its start and a space calls the one the word names:
// it asks, exclaims or ends in a particle of address (甲児 どうした？, 夢葉 だめだよ, 舞 覚えてるか？),
// not a statement (外中 ２枚看板のチームだった).
bool CaptionCall(std::string_view after) {
    // The first phrase of the rest, up to the next space (甲児 どうした？ あっ; not 船影 急速に接近中 … 速い！).
    std::string_view a = TrimSpace(after);
    for (std::size_t i = 0; i < a.size();) {
        const text::Rune r = RuneAt(a, i);
        if (r.cp == U' ' || r.cp == U'　') {
            a = a.substr(0, i);
            break;
        }
        i += std::max<std::size_t>(1, r.size);
    }
    while (!a.empty()) {
        const text::Rune r = RuneBefore(a, a.size());
        if (!InRunes(r.cp, "。…‥～〜ー・、")) break;
        a.remove_suffix(r.size);
    }
    if (a.empty()) return false;
    for (const std::string_view end : {"？", "?", "！", "!", "よ", "ぞ", "ぜ", "ね", "か", "って", "ろ"}) {
        if (a.ends_with(end)) return true;
    }
    return false;
}

// Whether text before and after a word frame it as a person's name: a self-introduction (私は_です,
// 俺は_だ, _と申します, _っていいます), a greeting or a call before the name at the end of an utterance
// (ただいま、_, おはよう、_！), a speaker label at the line start (_「), or a caption call.
bool PersonFrame(std::string_view run, std::string_view before, std::string_view after) {
    std::string_view b = before;
    while (b.ends_with(" ") || b.ends_with("　")) b.remove_suffix(b.ends_with(" ") ? 1 : std::string_view("　").size());
    for (const std::string_view a : {"と申します", "と申す", "っていいます", "って言います", "といいます", "と言います", "っていうの", "って言うの"}) {
        if (after.starts_with(a)) return true;
    }
    bool self = false;
    for (const std::string_view p : {"私は", "俺は", "僕は", "わたしは", "あたしは", "ぼくは", "おれは", "わたくしは", "私の名前は", "名前は"}) {
        self = self || b.ends_with(p);
    }
    if (self) {
        for (const std::string_view a : {"です", "だ", "。", "！", "」"}) {
            if (after.starts_with(a)) return true;
        }
    }
    const bool ends = after.empty() || after.starts_with("！") || after.starts_with("？") || after.starts_with("」") || after.starts_with("。") ||
                      after.starts_with("…") || after.starts_with("!") || after.starts_with("?") || after.starts_with("、") || after.starts_with("っ");
    if (ends) {
        for (const std::string_view g : {"ただいま、", "おはよう、", "おかえり、", "こんにちは、", "こんばんは、", "ありがとう、", "ごめんね、", "ごめん、", "おやすみ、", "さようなら、", "じゃあね、", "またね、"}) {
            if (b.ends_with(g)) return true;
        }
    }
    const std::string_view t = TrimSpace(b);
    // A caption call: the name alone at the line start before a space (凶真 これからも…, 甲児 どうした？),
    // or after a calling interjection and a space (おい 鯨木 どうした).
    const bool spaceAfter = after.starts_with(" ") || after.starts_with("　");
    // Not a run that starts with a numeral or a prefix of kind or count (百倍, 一域, 各団: words).
    const bool prefixed = InRunes(RuneAt(run, 0).cp, "一二三四五六七八九十百千万億零各毎全諸両本当同某此其半第");
    if (t.empty() && spaceAfter && before.empty() && !prefixed && CaptionCall(after)) return true;
    const bool spaced = before.ends_with(" ") || before.ends_with("　");
    // Not before って or と, which quote the word (なあ 百鬼って何なんだ？).
    const bool callEnd = after.empty() || after.starts_with("！") || after.starts_with("？") || after.starts_with("!") ||
                         after.starts_with("?") || after.starts_with("…") || after.starts_with("。") || after.starts_with("｡") ||
                         after.starts_with("、");
    if (spaced && (spaceAfter || callEnd)) {
        // The word before the space, after the last space or mark (航太？ おい 航太…).
        std::size_t k = b.size();
        while (k > 0 && !InRunes(RuneBefore(b, k).cp, "「『（(【 　。！？!?…")) k -= RuneBefore(b, k).size;
        const std::string_view word = b.substr(k);
        if (dict::InClass(HDTRANS_CLASS("name.call-word"), word)) return true;
    }
    return t.empty() && after.starts_with("「");
}

}  // namespace

void Registry::Add(std::string_view rawLine) {
    const std::string_view line0 = TrimCR(rawLine);
    if (TrimSpace(line0).starts_with("//")) return;
    scoresValid_ = false;
    auto count = [&](std::map<std::string, int>& values, const std::string& key) { values[key]++; };
    std::string line(line0);
    for (std::size_t from = 0; from <= line.size();) {
        RubyMatch m;
        if (!FindRuby(line, from, m)) break;
        const std::string base(m.base), kana(m.reading);
        if (IsKanaText(kana) && !Gikun(base, kana)) {
            count(gloss_, base);
            SetReading(base, kana);
        }
        from = m.end;
    }
    line = StripRuby(line);
    // A hiragana run in parentheses after kanji that is no reading of them and no word
    // (長良川鉄道株式会社（ながてつ）), or after one kanji a name of the name layer that is no
    // noun (猫（たま）), is a name given to what they name: it reads as one.
    for (std::size_t from = 0; from <= line.size();) {
        RubyMatch m;
        if (!FindReading(line, from, m)) break;
        from = m.end;
        if (line.substr(m.begin + m.base.size()).starts_with("「") || !AllRunes(m.reading, IsHiragana)) continue;
        const DictFlags f = Flags(m.reading);
        const bool layerName = (f.layerSurname || f.layerGiven) && !f.person && RuneCount(m.base) == 1 &&
                               (WordClasses(m.reading) & kNounClass) == 0;
        if (!layerName && (ReadingMorae(m.reading) < 3 || f.records > 0 || f.layerSurname || f.layerGiven)) continue;
        if (BaseReading(m.base, m.reading, std::string_view(line).substr(m.end), false, false)) continue;
        count(gloss_, std::string(m.reading));
    }
    bool script = false;
    if (const std::string label = Label(line, script); !label.empty()) {
        if (script) {
            count(labels_, label);
        } else {
            count(prefixes_, label);
        }
    }
    for (const std::string& t : KanaHonorificNames(line)) count(kanaHon_, t);
    const auto bare = [](char32_t p) { return p == U'は' || p == U'を' || p == U'へ'; };
    for (const LooseUse& u : FreeKanaNames(line)) {
        count(kanaFree_, u.t);
        if (bare(u.particle)) count(kanaBare_, u.t);
    }
    for (const LooseUse& u : LooseKanaUses(line)) {
        Loose& l = kanaLoose_[u.t];
        (u.closed ? l.closed : l.open)++;
        if (u.closed && bare(u.particle)) l.bare++;
        l.particles.insert(u.particle);
    }
    // A line that is a name alone, a speaker line before a quote, names the speaker of the
    // quote.
    const std::string_view body = TrimSpace(line);
    if (!nameLine_.empty() && (body.starts_with("「") || body.starts_with("『") || body.starts_with("（"))) {
        count(prefixes_, nameLine_);
    }
    nameLine_.clear();
    if (const std::size_t n = RuneCount(body); n >= 1 && n <= 8 && PlainLabel(body) && !LabelRole(body)) {
        nameLine_ = std::string(body);
    }
    const std::vector<Run> runs = RunsOf(line);
    for (const std::string& t : ProseRuns(line)) count(prose_, t);
    // A whole katakana run of three kana or more before a case particle or も (オレーリアは, ヤーナも).
    for (const Run& r : RunsOf(line)) {
        if (r.kanji || !AllRunes(r.text, IsKatakana) || RuneCount(r.text) < 3 || r.text.ends_with("ッ")) continue;
        if (r.start > 0 && (IsKatakana(RuneBefore(line, r.start).cp) || RuneBefore(line, r.start).cp == U'・' || RuneBefore(line, r.start).cp == U'＝')) continue;
        const std::string_view after = std::string_view(line).substr(r.end);
        if (dict::StartsWithMember(HDTRANS_CLASS("name.katakana-use-after"), after) == 0) continue;
        count(kataUse_, std::string(r.text));
        if (dict::StartsWithMember(HDTRANS_CLASS("name.katakana-subject-after"), after) > 0) count(kataSubject_, std::string(r.text));
        if (dict::StartsWithMember(HDTRANS_CLASS("name.katakana-place-after"), after) > 0) count(kataPlace_, std::string(r.text));
    }
    std::size_t chain = 0;  // first run of the current katakana chain
    for (std::size_t i = 0; i < runs.size(); i++) {
        const Run& run = runs[i];
        if (run.kanji) {
            count(runs_, std::string(run.text));
            // A kanji run in a frame that only a person's name takes: a self-introduction, a call or a
            // greeting with the name, or a speaker label (spec LEX-4833).
            // Not a run that ends in a kanji honorific (姫氏「: 姫 and 氏).
            const bool honorificEnd = run.text.ends_with("氏") || run.text.ends_with("君") || run.text.ends_with("様") ||
                                      run.text.ends_with("殿") || run.text.ends_with("嬢");
            if (const std::size_t n = RuneCount(run.text); !honorificEnd && n >= 2 && n <= 3 && PersonFrame(run.text, std::string_view(line).substr(0, run.start), std::string_view(line).substr(run.end))) {
                count(framed_, std::string(run.text));
                honGuess_.insert(std::string(run.text));
            }
            // A name written with a kanji honorific in the same run (甲児君, 夢結様) is written there
            // too (spec LEX-4827). Not 殿, 嬢 or the titles, which also end words (大魔殿, 令嬢).
            for (const std::string_view h : {std::string_view("君"), std::string_view("様"), std::string_view("氏")}) {
                if (run.text.size() > h.size() && run.text.ends_with(h) && RuneCount(run.text.substr(0, run.text.size() - h.size())) >= 2) {
                    count(runs_, std::string(run.text.substr(0, run.text.size() - h.size())));
                    break;
                }
            }
            if (NameEnd(line.substr(run.end))) count(ended_, std::string(run.text));
        } else {
            count(kana_, std::string(run.text));
        }
        bool linked = false;
        if (i + 1 < runs.size()) {
            const Run& next = runs[i + 1];
            const std::string_view gap = std::string_view(line).substr(run.end, next.start - run.end);
            if (run.kanji && !next.kanji && next.start == run.end) {
                const std::array<std::string, 3> key = {std::string(run.text), "", std::string(next.text)};
                joined_[key]++;
            } else if (run.kanji && next.kanji && (gap == "　" || gap == " ")) {
                const std::array<std::string, 3> key = {std::string(run.text), " ", std::string(next.text)};
                joined_[key]++;
            } else if (!run.kanji && !next.kanji && next.start == run.end + std::string_view("・").size()) {
                const std::string_view sep = std::string_view(line).substr(run.end, next.start - run.end);
                linked = sep == "・" || sep == "＝";
            }
        }
        if (!linked) {
            if (chain < i) count(chains_, std::string(line.substr(runs[chain].start, run.end - runs[chain].start)));
            chain = i + 1;
        }
        if (const std::string t = HonorificRun(line, run); !t.empty()) {
            count(hon_, t);
            // The honorific after it, in the run or after it. 殿 also ends words (大魔殿) and 様 follows
            // gods and spirits (水虎様), so a guessed name needs those twice (spec LEX-4827).
            const std::string_view after = run.text.size() > t.size() && run.text.starts_with(t)
                                               ? run.text.substr(t.size())
                                               : std::string_view(line).substr(run.end);
            // The く of a verb before んだ is no くん (鍵開くんだよ: 開く).
            const text::Rune last = RuneBefore(t, t.size());
            const bool verbKu = after.starts_with("くん") && !after.starts_with("くん、") &&
                                (after.substr(std::string_view("くん").size()).starts_with("だ") ||
                                 after.substr(std::string_view("くん").size()).starts_with("で") ||
                                 after.substr(std::string_view("くん").size()).starts_with("じゃ")) &&
                                InflectedRun(std::string(t.substr(t.size() - last.size)) + "く") &&
                                !AdjectiveKanji(t.substr(t.size() - last.size));  // 咲太くん: 太く is an adjective's
            if (!after.starts_with("殿") && !after.starts_with("様") && !after.starts_with("さま") && !verbKu) honGuess_.insert(t);
        }
        if (!run.kanji && RuneCount(run.text) >= 2 && AgentCue(std::string_view(line).substr(run.end))) {
            count(agents_, std::string(run.text));
        }
        if (!run.kanji && RuneCount(run.text) >= 2 && NamingCue(std::string_view(line).substr(run.end)) &&
            !RoleLike(run.text) && !Flags(run.text).role) {
            count(gloss_, std::string(run.text));
        }
    }
    // A two-part reading after a space and a spaced base (双寺 有仁 (そうじ ありひと)), which FindGloss,
    // reading no space before the parenthesis, does not see: each run takes its part (spec LEX-4950).
    for (std::size_t from = 0; from <= line.size();) {
        RubyMatch m;
        if (!FindReading(line, from, m)) break;
        from = m.end;
        if (!m.spaced) continue;
        if (const auto split = SpacedPair(line, m); split.has_value()) {
            count(gloss_, split->first.first);
            count(gloss_, std::string(m.base));
            SetReading(split->first.first, split->first.second);
            SetReading(std::string(m.base), split->second);
        }
    }
    for (std::size_t from = 0; from <= line.size();) {
        RubyMatch m;
        if (!FindGloss(line, from, m)) break;
        const std::string base(m.base);
        // A word read aloud is no name unless an honorific follows (陸(くが)さん).
        if (Gikun(base, m.reading) || (WordAloud(base, m.reading) && KanaHonorificAt(std::string_view(line).substr(m.end)).empty())) {
            from = m.end;
            continue;
        }
        // A reading in two parts over a base after a kanji run and a space reads the two runs, a part
        // each (双寺　有仁（そうじ　ありひと）: 双寺 そうじ, 有仁 ありひと).
        if (const auto split = SpacedPair(line, m); split.has_value()) {
            count(gloss_, split->first.first);
            count(gloss_, base);
            SetReading(split->first.first, split->first.second);
            SetReading(base, split->second);
            from = m.end;
            continue;
        }
        if (!GlossFits(base, m.reading)) {
            from = m.end;
            continue;
        }
        count(gloss_, base);
        if (reading_[base].empty()) {
            std::string r;
            for (std::size_t i = 0; i < m.reading.size();) {
                const std::string_view rest = m.reading.substr(i);
                if (rest.starts_with("・")) {
                    i += std::string_view("・").size();
                } else if (rest.starts_with(" ") || rest.starts_with("　")) {
                    i += rest.starts_with(" ") ? 1 : std::string_view("　").size();
                } else {
                    r += m.reading[i];
                    i++;
                }
            }
            SetReading(base, r);
        }
        from = m.end;
    }
}

std::vector<std::string> Registry::Names() {
    std::vector<std::string> out;
    for (const auto& [t, n] : Accepted()) out.push_back(t);
    return out;
}

std::vector<std::string> Registry::FullNameParts() {
    std::set<std::string> out;
    for (const auto& [t, c] : runs_) {
        (void)c;
        if (!AllRunes(t, IsKanji) || RuneCount(t) < 3 || RuneCount(t) > 6) continue;
        const DictFlags ft = Flags(t);
        // A common word or a place (竜宮城) is no full name.
        if (ft.common || ft.place || ft.city) continue;
        bool whole = false;
        if (snap_ != nullptr) {
            for (const dict::Record& rec : snap_->NameRecords(t)) whole = whole || (rec.codes != nullptr && rec.codes->sem.HasPrefix("QPN"));
        }
        for (std::size_t at = RuneAt(t, 0).size; at < t.size(); at += RuneAt(t, at).size) {
            const std::string x = t.substr(0, at), y = t.substr(at);
            if (RuneCount(x) > 3 || RuneCount(y) > 3) continue;
            const DictFlags fx = Flags(x), fy = Flags(y);
            if (!fx.layerSurname || !fy.layerGiven || fy.role) continue;
            // The name layer writes the whole run as a person (古手梨花 후루데 리카), or the given name is no
            // common word (竜宮礼奈: 礼奈); not two common words (砂糖菓子: 菓子).
            if (!whole && fy.common) continue;
            // A part of one kanji needs the whole run written as a person (見目麗しい: 見目 and 麗 are no name).
            if (!whole && (RuneCount(x) == 1 || RuneCount(y) == 1)) continue;
            // A part of one kanji names the person alone only when it is no common word (林 숲, 誠 진심).
            if (RuneCount(x) >= 2 || !fx.common) out.insert(x);
            if (RuneCount(y) >= 2 || !fy.common) out.insert(y);
        }
    }
    return {out.begin(), out.end()};
}

std::vector<std::string> Registry::HonorificOnlyNames() {
    std::vector<std::string> out;
    for (const auto& [t, n] : Accepted()) {
        if (n.honorificOnly) out.push_back(t);
    }
    return out;
}

const std::map<std::string, Name>& Registry::Accepted() {
    if (scoresValid_) return scores_;
    std::map<std::string, Name> all;
    std::set<std::string> labelled;
    auto addLabel = [&](const std::string& raw, bool plain) {
        for (const std::string& p : LabelParts(raw)) {
            std::string x;
            if (ParentLabel(p, x)) {
                all[x].score++;
                all[x].person = true;
                continue;
            }
            if (p.empty() || RoleLike(p) || LabelRole(p) || !AllRunes(p, IsNameRune)) continue;
            // A plain-text label that is a common word and no person of the name layer is
            // a role (放送, 司会), and one of common katakana words a field (セットスキル：), not
            // a name.
            if (plain) {
                const DictFlags f = Flags(p);
                if (f.common && !f.person && !f.layerSurname && !f.layerGiven) continue;
                if (CommonWords(p)) continue;
            }
            labelled.insert(p);
        }
    };
    for (const auto& [raw, n] : labels_) addLabel(raw, false);
    for (const auto& [raw, n] : prefixes_) {
        if (n >= 3) addLabel(raw, true);
    }
    for (const std::string& t : labelled) {
        Name& n = all[t];
        n.label = true;
        n.person = true;
        n.score += Flags(t).role ? 1.5 : 3;
    }
    for (const auto& [t, c] : hon_) {
        const DictFlags f = Flags(t);
        double w = 1.0;
        if (f.role) {
            w = 0;
        } else if ((f.common || CommonWords(t)) && !f.person && !f.city && PersonName(t).empty()) {
            w = 0.5;
        }
        Name& n = all[t];
        n.score += static_cast<double>(std::min(c, kHonorificCap)) * w;
        // A kanji name no dictionary has stands for the person after one honorific, as it is no
        // word a bare use could mean (千莉ちゃん … 千莉は; spec LEX-4827).
        if (f.records == 0 && AllRunes(t, IsKanji) && !GuessedNameBlocked(t) && !OldFormWord(t) && !internal::GuessedName(t, GuessFallback(t)).empty()) n.score += 1;
        n.person = true;
    }
    // A kanji name no dictionary has, in a person frame, is a name its kanji's readings give (LEX-4833).
    for (const auto& [t, c] : framed_) {
        (void)c;
        if (Flags(t).records > 0 || !AllRunes(t, IsKanji) || GuessedNameBlocked(t) || OldFormWord(t) || internal::GuessedName(t, GuessFallback(t)).empty()) continue;
        Name& n = all[t];
        n.score = std::max(n.score, kThreshold);
        n.person = true;
    }
    for (const auto& [t, c] : kanaHon_) {
        Name& n = all[t];
        n.score += static_cast<double>(std::min(c, kHonorificCap));
        // Kana no dictionary has stand for the person after one honorific (みいちゃん … みいは; spec
        // LEX-4826): they are no word a bare use could mean.
        if (Flags(t).records == 0) n.score += 1;
        n.person = true;
    }
    // Kana no dictionary has that the document writes three times or more where a name stands, before a
    // particle and a word (みいは自分の, みいが来た), are a name there too (spec LEX-4826).
    // Kana whose halves are words (あい, いか) need more: five uses or more before two particles or more,
    // three of them a whole run closed by its particle; the uses after a particle inside a run and those
    // with kana after the particle count once those three are there (spec LEX-4828).
    // A run that is also a verb or adjective form of the dictionary (しのぶ: 忍ぶ) may be one before の, が, と
    // or に (a nominaliser, but, if, to): it needs two closed uses before は, を or へ, which no plain predicate
    // form takes, before the counts apply. Not も, which also ends names (やくも: やく and も).
    const auto inflectedOk = [&](const std::string& t, int bare) { return bare >= 2 || !InflectedRun(t); };
    std::set<std::string> free;
    for (const auto& [t, c] : kanaFree_) {
        const auto b = kanaBare_.find(t);
        if (c >= 3 && inflectedOk(t, b == kanaBare_.end() ? 0 : b->second)) free.insert(t);
    }
    for (const auto& [t, l] : kanaLoose_) {
        if (l.closed >= 3 && l.closed + l.open >= 5 && l.particles.size() >= 2 && inflectedOk(t, l.bare)) free.insert(t);
    }
    // A katakana run no dictionary or name layer has that the document writes three times or more before
    // a particle (オレーリアは, オレーリアの, オレーリアが) is a name, read whole by its sound, when a Japanese
    // word written in katakana inside it would garble it (オレ 나: 나리어), unless common katakana words
    // cover it (プリンセスドレス: プリンセス, ドレス; spec LEX-4953).
    for (const auto& [t, c] : kataUse_) {
        if (c < 3 || Flags(t).records > 0 || (snap_ != nullptr && !snap_->NameRecords(t).empty()) || CommonWords(t) ||
            CoveredByWords(t) || TranslatedWord(t) || HiraganaWord(t) || !GarbledByWord(t) || LayerFullName(t)) {
            continue;
        }
        // Only a person: the document makes it a subject or a topic twice or more (オレーリアは) and more often
        // than it puts it before に, へ, で, を or から, as a place or a thing (フラーゼルに行く, ノストラに潜入,
        // トリマキを), which keeps the reading of its words.
        const auto sub = kataSubject_.find(t);
        const auto place = kataPlace_.find(t);
        const int subjects = sub == kataSubject_.end() ? 0 : sub->second, places = place == kataPlace_.end() ? 0 : place->second;
        if (subjects < 2 || subjects <= places) continue;
        Name& n = all[t];
        n.score = std::max(n.score, kThreshold);
        n.person = true;
    }
    for (const std::string& t : free) {
        if (Flags(t).records > 0 || WordThenParticle(t)) continue;
        Name& n = all[t];
        n.score += kThreshold;
        n.person = true;
    }
    for (const auto& [t, c] : gloss_) {
        Name& n = all[t];
        n.score += 2 * static_cast<double>(c);
        n.person = true;
    }
    // A plain-text label seen once or twice adds to the evidence of a surface an honorific
    // or a gloss shows.
    for (const auto& [raw, c] : prefixes_) {
        if (c >= 3) continue;
        for (const std::string& p : LabelParts(raw)) {
            const auto it = all.find(p);
            if (it == all.end() || it->second.score <= 0 || labelled.count(p) > 0 || RoleLike(p) || LabelRole(p)) continue;
            it->second.score += static_cast<double>(c);
        }
    }
    // A person of the name layer that no other dictionary reads needs one piece of evidence
    // less, a hiragana one too when an honorific has followed it (まきちゃん), but not one
    // written in katakana.
    for (auto& [t, n] : all) {
        if (!n.person || n.label || n.score <= 0 || AllRunes(t, IsKatakana)) continue;
        if (AllRunes(t, IsHiragana) && kanaHon_.count(t) == 0) continue;
        const DictFlags f = Flags(t);
        if ((f.layerSurname || f.layerGiven) && !f.common && !f.person && !f.role) n.score += 1;
    }
    for (const auto& [t, c] : runs_) {
        const auto rd = tables_->readings.find(t);
        if (rd != tables_->readings.end() && !rd->second.empty() && RuneCount(t) >= 2 && Flags(t).records == 0) {
            all[t].score += static_cast<double>(std::min(c, 2));
        }
    }
    for (const auto& [t, c] : agents_) {
        // A place (ウチに来てもらう) is where the favour is done, not who does it.
        if (c < 2 || !TranslatedWord(t) || Flags(t).place || Flags(ToHiragana(t)).place) continue;
        Name& n = all[t];
        n.score += static_cast<double>(std::min(c, kHonorificCap));
        n.person = true;
    }
    for (const auto& [t, c] : prose_) {
        if (c >= 2 && NicknameCompound(t)) {
            Name& n = all[t];
            n.score = std::max(n.score, kThreshold);
            n.person = true;
        }
    }
    std::set<std::string> att;
    for (const auto& [t, n] : all) {
        if (n.score >= kThreshold) att.insert(t);
    }
    for (const auto& [t, kr] : tables_->fixed) att.insert(t);
    for (const auto& [t, l] : listed_) {
        if (PersonKind(l.kind)) att.insert(t);
    }
    // Whether the katakana run t goes on after its head x in words of two kana or more: common
    // words of the dictionaries (ダンジョンゲート) or a run the lines write on its own
    // (ゴブリンアーチャー beside アーチャー). Such a run is a compound of x, not a longer name; x
    // written twice (メイメイ) is one.
    auto compound = [&](std::string_view t, std::string_view x) {
        const std::string_view rest = t.substr(x.size());
        return RuneCount(rest) >= 2 && rest != x && (CommonWords(rest) || kana_.count(std::string(rest)) > 0);
    };
    // A katakana run before an honorific that starts a longer name or unknown run is that name's
    // short form (アルくん, アルガルド), but a common word is the word (ダンジョンさん).
    for (const auto& [x, c] : hon_) {
        (void)c;
        if (att.count(x) || RuneCount(x) < 2 || !AllRunes(x, IsKatakana) || Flags(x).role || CommonWords(x)) continue;
        for (const auto& [t, kc] : kana_) {
            if (t.size() > x.size() && t.starts_with(x) && (att.count(t) || (kc >= 2 && Flags(t).records == 0)) &&
                !compound(t, x)) {
                Name& n = all[x];
                n.score = std::max(n.score, kThreshold);
                n.person = true;
                att.insert(x);
                break;
            }
        }
    }
    auto surnameLike = [&](const std::string& x) {
        const DictFlags f = Flags(x);
        const std::size_t k = RuneCount(x);
        // Not a numeral (二, 三十), which no dictionary row reads either.
        const bool numeral = AllRunes(x, [](char32_t c) {
            return std::u32string_view(U"〇一二三四五六七八九十百千万億零").find(c) != std::u32string_view::npos;
        });
        // A single kanji no dictionary knows (改) is no surname without a surname reading.
        return k >= 1 && k <= 3 && AllRunes(x, IsKanji) && !numeral &&
               (f.surname || f.layerSurname || (f.records == 0 && k >= 2));
    };
    auto full = [&](const std::string& x, const std::string& y, const std::string& joined, int c) {
        // A common word or a place of the dictionaries is no full name (沢山 is 沢 + 山 only as a
        // name, 東京), and a surname or given name they know is one name (上田 우에다, not 우에 다).
        const DictFlags fj = Flags(joined);
        if (fj.common || (fj.records > 0 && !fj.person && fj.layerName.empty())) return;
        const bool one = OneName(fj, joined);
        // The surname stands for the person alone too, but a common word (北, 上) reads as
        // that word wherever the full name is not written; a kanji whose only common reading is
        // the kanji's own (牧 목) is no such word.
        if (!Flags(x).word) {
            Name& s = all[x];
            s.score += c;
            s.person = true;
            s.surname = true;
        }
        Name& j = all[joined];
        j.score += kThreshold;
        j.person = true;
        if (one) return;
        j.hasParts = true;
        j.parts = {x, y};
    };
    for (const auto& [t, c] : runs_) {
        for (const std::string& y : att) {
            if (HasSuffixOf(t, y)) {
                const std::string x = t.substr(0, t.size() - y.size());
                if (!x.empty() && surnameLike(x)) full(x, y, t, c);
            }
        }
    }
    for (const auto& [k, c] : joined_) {
        const std::string& x = k[0];
        const std::string& y = k[2];
        if (k[1].empty()) {
            if (att.count(y) && surnameLike(x)) full(x, y, x + y, c);
            continue;
        }
        // A surname of two kanji or more, a space and a given name of up to three kanji that is no
        // word (小鳥遊　聖): the given name names the person.
        const DictFlags fx = Flags(x), fy = Flags(y);
        if (RuneCount(x) < 2 || RuneCount(y) > 3 || !(fx.surname || fx.layerSurname) || fx.word || fx.role ||
            !(fy.given || fy.layerGiven) || fy.word || fy.role) {
            continue;
        }
        Name& g = all[y];
        g.score = std::max(g.score, kSingleThreshold);
        g.person = true;
        g.given = true;
    }
    for (const auto& [t, c] : ended_) {
        const auto [x, y] = GivenSplit(t);
        if (y.empty()) continue;
        full(x, y, t, c);
        // The given name stands for the person alone too, but a common word (大森海: 海)
        // reads as that word wherever the full name is not written, unless it has two kanji or
        // more and the document writes the full name more than once (椎名真昼 twice: 真昼 is the
        // person).
        if (Flags(y).word && (c < 2 || RuneCount(y) < 2)) continue;
        // Nor the end of a word that starts in the surname (今一瞬: 一瞬).
        bool tail = false;
        for (const std::size_t j : RuneStarts(x)) tail = tail || (j > 0 && Flags(t.substr(j)).common);
        if (tail) continue;
        Name& g = all[y];
        g.score = std::max(g.score, kSingleThreshold);
        g.person = true;
        g.given = true;
    }
    // An unknown katakana run that starts with a katakana name is a longer name, unless it is a
    // compound of the name.
    for (const auto& [t, c] : kana_) {
        if (c < 2 || att.count(t) || Flags(t).records > 0) continue;
        for (const std::string& x : att) {
            if (x.size() < t.size() && t.starts_with(x) && RuneCount(x) >= 2 && AllRunes(x, IsKatakana) &&
                !compound(t, x)) {
                all[t].score += kThreshold;
                all[t].person = true;
                break;
            }
        }
    }
    for (const auto& [t, c] : chains_) {
        const std::vector<std::string> parts = ChainParts(t);
        double score = static_cast<double>(c);
        bool fresh = false;
        for (const std::string& p : parts) {
            if (Flags(p).records == 0 && RuneCount(p) >= 2) fresh = true;
        }
        if (fresh) {
            score = std::max(score, kThreshold);
        } else {
            bool any = false;
            for (const std::string& p : parts) any = any || att.count(p) > 0;
            if (!any) continue;
        }
        // A common word in the chain (ギア・バーラー) stays a word on its own.
        for (const std::string& p : parts) {
            const DictFlags f = Flags(p);
            if (f.common && !f.person) continue;
            all[p].score += score;
            all[p].person = true;
        }
        Name& j = all[t];
        j.score += kThreshold;
        j.person = true;
        j.hasParts = true;
        j.parts = parts;
    }
    // The numbered names of a name: two or more katakana runs of the document that are the
    // name and a number read in katakana (サーライチ, サーラニ, サーラヨン beside サーラ), each of which
    // no dictionary has, read as the name's reading and the number's.
    numbered_.clear();
    std::map<std::string, std::vector<std::string>> numbers;
    for (const auto& [t, c] : kana_) {
        (void)c;
        for (const std::string_view y : kNumberReadings) {
            if (t.size() <= y.size() || !std::string_view(t).ends_with(y)) continue;
            const std::string x = t.substr(0, t.size() - y.size());
            const DictFlags f = Flags(x);
            if (RuneCount(x) >= 2 && AllRunes(x, IsKatakana) &&
                (att.count(x) > 0 || f.person || f.layerGiven || f.layerSurname)) {
                numbers[x].push_back(t);
            }
        }
    }
    for (const auto& [x, ts] : numbers) {
        if (ts.size() < 2) continue;
        for (const std::string& t : ts) {
            if (Flags(t).records > 0 || (snap_ != nullptr && !snap_->NameRecords(t).empty())) continue;
            numbered_[t] = x;
            Name& n = all[t];
            n.score = std::max(n.score, kThreshold);
            n.person = true;
        }
    }
    scores_.clear();
    FindSiblingNames();
    for (const auto& [t, n] : all) {
        double need = kThreshold;
        if (RuneCount(t) == 1 && AllRunes(t, IsKanji) && !n.label) need = kSingleThreshold;
        if (AllRunes(t, IsHiragana) && !n.label) {
            // A hiragana run seen before an honorific is a name there; it stands for the person
            // alone only when its score reaches the threshold, as after two honorifics, or twice
            // the threshold for a common word (ひかり), and it is no predicate form (あおいさん,
            // but あおい空).
            const auto k = kanaHon_.find(t);
            if (k == kanaHon_.end() && free.count(t) != 0 && n.score >= need && Flags(t).records == 0) {
                scores_[t] = n;
                continue;
            }
            if (k == kanaHon_.end()) {
                // A name given in parentheses (Add) stands alone.
                if (gloss_.count(t) != 0 && n.score >= need) scores_[t] = n;
                continue;
            }
            if (n.score < (Flags(t).common ? 2 * need : need) || PredicateForm(t)) {
                Name h = n;
                h.honorificOnly = true;
                scores_[t] = h;
                continue;
            }
        }
        if (n.score >= need) scores_[t] = n;
    }
    for (const auto& [t, l] : listed_) {
        if (!PersonKind(l.kind)) {
            scores_.erase(t);
            continue;
        }
        // hon keeps a name the lines show standing alone.
        const auto learned = scores_.find(t);
        Name n = all[t];
        n.person = true;
        n.honorificOnly = l.honorificOnly && (learned == scores_.end() || learned->second.honorificOnly);
        n.hasParts = false;
        n.parts.clear();
        scores_[t] = n;
    }
    scoresValid_ = true;
    for (const std::string& t : Names()) {
        Name& n = scores_[t];
        if (listed_.count(t) > 0) continue;
        if (!n.hasParts) {
            std::vector<std::string> parts;
            if (Split(t, parts)) {
                n.parts = parts;
                n.hasParts = true;
            }
        }
        const auto kanaIt = reading_.find(t);
        if (kanaIt != reading_.end() && !kanaIt->second.empty() && n.parts.size() == 2) {
            const std::string kana = kanaIt->second;
            SplitReading(n.parts[0], n.parts[1], kana);
        }
    }
    scoresBuilt_++;
    return scores_;
}

// The surname of a given-name split (椎名真昼: 椎名, 真昼): a kanji run that is not a
// base word, split at a boundary where the left part is a surname of two or three
// kanji and the right part a given name of one to three, with no base word crossing
// the boundary, and the right part not a person suffix or honorific.
std::pair<std::string, std::string> Registry::GivenSplit(std::string_view t) {
    if (!AllRunes(t, IsKanji) || Flags(t).records > 0) return {"", ""};
    // Three kanji at most on each side.
    if (RuneCount(t) > 6) return {"", ""};
    for (const std::size_t i : RuneStarts(t)) {
        const std::string_view x = t.substr(0, i), y = t.substr(i);
        const std::size_t nx = RuneCount(x);
        if (nx < 2 || nx > 3 || RuneCount(y) > 3) continue;
        const DictFlags fx = Flags(x), fy = Flags(y);
        if (!(fx.surname || fx.layerSurname) || fx.common || fx.role) continue;
        if (!(fy.given || fy.layerGiven) || fy.role) continue;
        if (dict::InClass(HDTRANS_CLASS("suffix.person-after-surname"), y) || dict::InClass(HDTRANS_CLASS("honorific.name"), y) || Crossed(t, i)) {
            continue;
        }
        return {std::string(x), std::string(y)};
    }
    return {"", ""};
}

// Whether a base word of t starts before byte i and ends after it.
bool Registry::Crossed(std::string_view t, std::size_t i) {
    for (const std::size_t j : RuneStarts(t.substr(0, i))) {
        for (std::size_t k = i + 1; k <= t.size(); k++) {
            const bool startK = IsRuneStartByte(t[k - 1]);
            const bool endOk = k == t.size() || IsRuneStartByte(t[k]);
            if (startK && endOk && Flags(t.substr(j, k - j)).common) return true;
        }
    }
    return false;
}

// The surname and given name of a full name t: at the boundary between its kanji and
// katakana, or before or after another accepted name when the other part has two
// characters or more. A katakana word is not split, nor a word the dictionaries read whole.
bool Registry::Split(std::string_view t, std::vector<std::string>& parts) {
    if (const DictFlags f = Flags(t); (f.records > 0 && !f.person && f.layerName.empty()) || OneName(f, t)) return false;
    for (const Run& rn : RunsOf(t)) {
        if (rn.start > 0 && rn.end == t.size()) {
            parts = {std::string(t.substr(0, rn.start)), std::string(t.substr(rn.start))};
            return true;
        }
    }
    if (AllRunes(t, IsKatakana)) return false;
    for (const std::size_t i : RuneStarts(t)) {
        if (i == 0) continue;
        const std::string_view x = t.substr(0, i), y = t.substr(i);
        const bool xName = scores_.count(std::string(x)) > 0, yName = scores_.count(std::string(y)) > 0;
        if ((yName && (xName || RuneCount(x) >= 2)) || (xName && RuneCount(y) >= 2)) {
            parts = {std::string(x), std::string(y)};
            return true;
        }
    }
    return false;
}

// Gives the parts of a full name the readings of its whole, when the other part is known.
void Registry::SplitReading(const std::string& x, const std::string& y, const std::string& kana) {
    if (!reading_[x].empty() && !reading_[y].empty()) return;
    auto known = [&](const std::string& part) -> std::string {
        const auto r = reading_.find(part);
        if (r != reading_.end() && !r->second.empty()) return Written(r->second);
        const auto v = tables_->readings.find(part);
        if (v != tables_->readings.end() && !v->second.empty()) {
            if (IsKanaText(v->second)) return Written(v->second);
            return v->second;
        }
        if (!AllRunes(part, IsKanji)) return Written(part);
        return "";
    };
    // The longest tail that renders as the known given name, else the longest head that
    // renders as the known surname.
    const std::string_view rs = kana;
    std::vector<std::size_t> at = RuneStarts(rs);
    at.push_back(rs.size());
    const std::array<text::Preset, 4> presets = {preset, text::Preset::Fan, text::Preset::FanShort, text::Preset::Official};
    const std::string h = known(y);
    if (!h.empty() && reading_[x].empty()) {
        for (std::size_t idx = 1; idx + 1 < at.size(); idx++) {
            for (const text::Preset p : presets) {
                if (text::FromKana(rs.substr(at[idx]), p) == h || Written(rs.substr(at[idx])) == h) {
                    reading_[x] = std::string(rs.substr(0, at[idx]));
                    return;
                }
            }
        }
    }
    const std::string sh = known(x);
    if (!sh.empty() && reading_[y].empty()) {
        for (std::size_t idx = at.size() - 1; idx-- > 1;) {
            for (const text::Preset p : presets) {
                if (text::FromKana(rs.substr(0, at[idx]), p) == sh || Written(rs.substr(0, at[idx])) == sh) {
                    reading_[y] = std::string(rs.substr(at[idx]));
                    return;
                }
            }
        }
    }
}

// Whether the katakana word w is a Japanese word written in katakana, which its hiragana
// spells too, and the dictionary translates rather than transliterates (ヤカン やかん 주전자,
// フトン 이불).
bool Registry::TranslatedWord(std::string_view w) {
    if (snap_ == nullptr) return false;
    const DictFlags f = Flags(w);
    if (!f.common || f.person || f.role) return false;
    std::string hiragana;
    for (std::size_t i = 0; i < w.size(); i += RuneAt(w, i).size) {
        const char32_t c = RuneAt(w, i).cp;
        if (c < U'ァ' || c > U'ヶ') return false;
        text::AppendRune(hiragana, c - 0x60);
    }
    if (!Flags(hiragana).common) return false;
    const std::string sound = text::FromKana(w, text::Preset::Fan);
    bool any = false;
    for (const dict::Candidate& c : snap_->RetrieveRecords(w, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.codes->sem.HasPrefix("QPN") || !IsHangulText(c.target)) continue;
        if (text::SameSound(c.target, sound)) return false;
        any = true;
    }
    return any;
}

// Whether the katakana run t, no word of the dictionaries, splits into a Japanese word
// written in katakana that the dictionary translates rather than transliterates (ヤカン
// やかん 주전자) and a person name (レン): a nickname joined to a name (ヤカンレン), which reads
// as one name rather than as the word and the name.
bool Registry::NicknameCompound(std::string_view t) {
    if (snap_ == nullptr || !AllRunes(t, IsKatakana) || RuneCount(t) < 4 || Flags(t).records > 0 ||
        !snap_->NameRecords(t).empty()) {
        return false;
    }
    auto name = [&](std::string_view w) {
        const DictFlags f = Flags(w);
        return f.person && !f.common && RuneCount(w) >= 2;
    };
    const std::vector<std::size_t> starts = RuneStarts(t);
    for (std::size_t k = 2; k + 2 <= starts.size(); k++) {
        const std::string_view a = t.substr(0, starts[k]), b = t.substr(starts[k]);
        if ((TranslatedWord(a) && name(b)) || (name(a) && TranslatedWord(b))) return true;
    }
    return false;
}

// Whether words of two kana or more with a common sense in the dictionaries, names of the layer too,
// cover the katakana run s (ラッキーアイテム: ラッキー, アイテム; not オレーリア, whose ーリア is none).
// Whether the katakana run s spells in hiragana a word of the dictionaries (トリマキ: とりまき 取り巻き),
// a Japanese word written in katakana.
bool Registry::HiraganaWord(std::string_view s) {
    std::string hiragana;
    for (std::size_t i = 0; i < s.size(); i += RuneAt(s, i).size) {
        const char32_t c = RuneAt(s, i).cp;
        if (c < U'ァ' || c > U'ヶ') return false;
        text::AppendRune(hiragana, c - 0x60);
    }
    return Flags(hiragana).records > 0;
}

// Whether the katakana run s is a surname and a given name of the name layer written together (カワムラユキエ:
// カワムラ, ユキエ), which reads as the two.
bool Registry::LayerFullName(std::string_view s) {
    const std::vector<std::size_t> starts = RuneStarts(s);
    // Either half of three kana or more a person of the name layer or the dictionaries (ユキエ).
    for (std::size_t k = 3; k + 3 <= starts.size(); k++) {
        const DictFlags a = Flags(s.substr(0, starts[k])), b = Flags(s.substr(starts[k]));
        if (a.layerSurname || a.surname || a.person || b.layerGiven || b.given || b.person) return true;
        // Or a proper noun of the dictionaries (カワムラ 카와무라).
        if (snap_ != nullptr) {
            for (const std::string_view half : {s.substr(0, starts[k]), s.substr(starts[k])}) {
                for (const dict::Candidate& c : snap_->RetrieveRecords(half, dict::LookupMode::General)) {
                    if (c.codes != nullptr && c.source == half && c.codes->part.ProperNoun()) return true;
                }
            }
        }
    }
    return false;
}

// Whether a Japanese word written in katakana that the dictionary translates (TranslatedWord: オレ 나,
// トロ 다랑어) stands inside the katakana run s, which a reading by its words would garble.
bool Registry::GarbledByWord(std::string_view s) {
    const std::vector<std::size_t> starts = [&] {
        std::vector<std::size_t> v = RuneStarts(s);
        v.push_back(s.size());
        return v;
    }();
    if (snap_ == nullptr) return false;
    // A piece whose hiragana spelling is a word and whose Korean is no sound of it (オレ おれ 나).
    auto japanese = [&](std::string_view w) {
        std::string hiragana;
        for (std::size_t i = 0; i < w.size(); i += RuneAt(w, i).size) {
            const char32_t c = RuneAt(w, i).cp;
            if (c < U'ァ' || c > U'ヶ') return false;
            text::AppendRune(hiragana, c - 0x60);
        }
        if (Flags(hiragana).records == 0) return false;
        const std::string sound = text::FromKana(w, text::Preset::Fan);
        bool any = false;
        for (const dict::Candidate& c : snap_->RetrieveRecords(w, dict::LookupMode::General)) {
            if (c.codes == nullptr || c.source != w || c.codes->sem.HasPrefix("QPN") || !IsHangulText(c.target)) continue;
            if (text::SameSound(c.target, sound)) return false;
            any = true;
        }
        return any;
    };
    for (std::size_t i = 0; i + 2 < starts.size(); i++) {
        for (std::size_t j = i + 2; j < starts.size() && j <= i + 4; j++) {
            // At the start or the end of the run (オレ|ーリア, デル|トロ; not ス|トック|バーン).
            if ((i == 0 || j + 1 == starts.size()) && japanese(s.substr(starts[i], starts[j] - starts[i]))) return true;
        }
    }
    return false;
}

bool Registry::CoveredByWords(std::string_view s) {
    if (!AllRunes(s, IsKatakana)) return false;
    std::vector<std::size_t> starts = RuneStarts(s);
    starts.push_back(s.size());
    std::vector<bool> split(starts.size(), false);
    split[0] = true;
    for (std::size_t i = 0; i + 2 < starts.size(); i++) {
        if (!split[i]) continue;
        for (std::size_t j = i + 2; j < starts.size(); j++) {
            if (!split[j] && Flags(s.substr(starts[i], starts[j] - starts[i])).common) split[j] = true;
        }
    }
    return split.back();
}

bool Registry::CommonWords(std::string_view s) {
    if (!AllRunes(s, IsKatakana)) return false;
    auto word = [&](std::string_view w) {
        const DictFlags f = Flags(w);
        return f.common && !f.person && !f.layerSurname && !f.layerGiven;
    };
    if (word(s)) return true;
    std::vector<std::size_t> starts = RuneStarts(s);
    starts.push_back(s.size());
    // split[k]: s up to starts[k] is a sequence of such words.
    std::vector<bool> split(starts.size(), false);
    split[0] = true;
    for (std::size_t i = 0; i + 2 < starts.size(); i++) {
        if (!split[i]) continue;
        for (std::size_t j = i + 2; j < starts.size(); j++) {
            if (!split[j] && word(s.substr(starts[i], starts[j] - starts[i]))) split[j] = true;
        }
    }
    return split.back();
}

const std::set<std::string>& Registry::SiblingNames() {
    if (scoresValid_) return siblings_;
    Accepted();
    return siblings_;
}

void Registry::FindSiblingNames() {
    siblings_.clear();
    for (const auto& [t, c] : kana_) {
        (void)c;
        std::string_view head;
        for (const std::string_view h : dict::ClassMembers(HDTRANS_CLASS("honorific.katakana-name"))) {
            if (t.size() > h.size() && std::string_view(t).ends_with(h)) head = std::string_view(t).substr(0, t.size() - h.size());
        }
        if (RuneCount(head) < 2) continue;
        int siblings = 0;
        for (auto it = kana_.lower_bound(std::string(head)); it != kana_.end() && it->first.starts_with(head); ++it) {
            const std::string_view tail = std::string_view(it->first).substr(head.size());
            if (tail.empty() || it->first == t || tail.starts_with("ー") || prose_.count(it->first) == 0) continue;
            if (dict::StartsWithMember(HDTRANS_CLASS("honorific.katakana-name"), tail) == 0) siblings++;
        }
        if (siblings >= 2) siblings_.insert(t);
    }
}

bool Registry::Named(std::string_view t) {
    const std::string key(t);
    if (const auto l = listed_.find(key); l != listed_.end()) return PersonKind(l->second.kind);
    if (Accepted().count(key) > 0) return true;
    if (SiblingNames().count(key) > 0) return true;
    const auto h = hon_.find(key), g = gloss_.find(key);
    if ((h != hon_.end() && h->second > 0) || (g != gloss_.end() && g->second > 0)) return true;
    for (const auto* m : {&labels_, &prefixes_}) {
        for (const auto& [k, n] : *m) {
            for (const std::string& p : LabelParts(k)) {
                if (p == key) return true;
            }
        }
    }
    for (const auto& [k, n] : chains_) {
        for (const std::string& p : LabelParts(k)) {
            if (p == key) return true;
        }
    }
    return false;
}

std::vector<std::string> Registry::NamedSurfaces() {
    std::set<std::string> named;
    for (const auto* m : {&hon_, &gloss_}) {
        for (const auto& [t, n] : *m) {
            if (n > 0) named.insert(t);
        }
    }
    for (const auto* m : {&labels_, &prefixes_}) {
        for (const auto& [k, n] : *m) {
            for (const std::string& t : LabelParts(k)) named.insert(t);
        }
    }
    for (const auto& [k, n] : chains_) {
        for (const std::string& t : LabelParts(k)) named.insert(t);
    }
    for (const std::string& t : SiblingNames()) named.insert(t);
    for (const auto& [t, l] : listed_) {
        if (!PersonKind(l.kind)) named.erase(t);
    }
    return std::vector<std::string>(named.begin(), named.end());
}

const std::map<std::string, int>& Registry::ProseKatakana() const { return prose_; }

std::vector<std::string> ProseRuns(std::string_view line) {
    std::size_t katakana = 0, japanese = 0;
    bool hiragana = false;
    for (std::size_t i = 0; i < line.size(); i += RuneAt(line, i).size) {
        const char32_t c = RuneAt(line, i).cp;
        katakana += IsKatakana(c) ? 1 : 0;
        japanese += IsNameRune(c) || c == U'々' ? 1 : 0;
        hiragana = hiragana || IsHiragana(c);
    }
    std::vector<std::string> out;
    if (!hiragana || 2 * katakana >= japanese) return out;
    for (const Run& run : RunsOf(line)) {
        if (!run.kanji) out.emplace_back(run.text);
    }
    return out;
}

std::string Registry::Key() {
    std::string out = Dict();
    out.push_back('\0');
    bool first = true;
    for (const std::string& t : Names()) {
        if (!first) out += "\t";
        out += t;
        first = false;
    }
    out.push_back('\0');
    first = true;
    for (const std::string& t : NamedSurfaces()) {
        if (!first) out += "\t";
        out += t;
        first = false;
    }
    out.push_back('\0');
    for (const auto& [a, v] : actors_) {
        out += a + "=" + v + "\t";
    }
    out.push_back('\0');
    for (const auto& [t, n] : prose_) out += t + "\t";
    return out;
}

std::vector<Occurrence> Registry::Occurrences(std::string_view line) {
    const std::map<std::string, Name>& acc = Accepted();
    std::vector<Occurrence> out;
    if (acc.empty()) return out;
    if (byFirstBuilt_ != scoresBuilt_) {
        byFirst_.clear();
        for (const auto& [t, n] : acc) byFirst_[RuneAt(t, 0).cp].push_back(t);
        for (auto& [c, list] : byFirst_) {
            std::stable_sort(list.begin(), list.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
        }
        byFirstBuilt_ = scoresBuilt_;
    }
    const std::map<char32_t, std::vector<std::string>>& byFirst = byFirst_;
    const std::vector<Run> runs = RunsOf(line);
    auto okRight = [&](std::string_view p) {
        if (p.empty() || acc.count(std::string(p)) > 0) return true;
        for (const std::string_view k : dict::ClassMembers(HDTRANS_CLASS("suffix.title-in-name"))) {
            if (!p.starts_with(k)) continue;
            const std::string_view rest = p.substr(k.size());
            if (rest.empty() || acc.count(std::string(rest)) > 0) return true;
        }
        return false;
    };
    for (std::size_t i = 0; i < line.size();) {
        const text::Rune c = RuneAt(line, i);
        std::string found;
        const auto list = byFirst.find(c.cp);
        if (list != byFirst.end()) {
            for (const std::string& t : list->second) {
                if (line.substr(i).starts_with(t)) {
                    found = t;
                    break;
                }
            }
        }
        if (found.empty()) {
            i += c.size;
            continue;
        }
        const std::size_t a = i, b = i + found.size();
        i = b;
        bool ok = true;
        for (const Run& rn : runs) {
            if (rn.start <= a && b <= rn.end) {
                const std::string_view left = line.substr(rn.start, a - rn.start), right = line.substr(b, rn.end - b);
                ok = (left.empty() || acc.count(std::string(left)) > 0) && okRight(right);
                break;
            }
        }
        const bool honorific = !KanaHonorificAt(line.substr(b)).empty();
        // A hiragana name starts the line, follows other than hiragana or stands before an
        // honorific (妹のさよりちゃん, ねえあおいさん), but not a creature, food or adverb there.
        if (AllRunes(found, IsHiragana)) {
            ok = a == 0 || !IsHiragana(internal::RuneBefore(line, a).cp) || (honorific && !Flags(found).thing);
        }
        const bool bare = acc.at(found).honorificOnly && !honorific;
        if (ok) {
            out.push_back(Occurrence{a, b, found, bare || Guarded(line, a, b)});
        } else if (AllRunes(found, IsHiragana) && Flags(found).common && !acc.at(found).label) {
            // A common word that only honorifics name stays the word inside a hiragana run
            // (最後のひかり).
            out.push_back(Occurrence{a, b, found, true});
        }
    }
    return out;
}

}  // namespace hdTrans::names
