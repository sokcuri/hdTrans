#include "core/names/registry.h"

#include <algorithm>
#include <array>
#include <initializer_list>
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
// dictionary at all and the base is only a word (用途（ようと）); and so does one that is
// also a name, unless a word of it has the base's Korean (秋刀魚（さんま）: 꽁치).
bool Registry::Gikun(std::string_view base, std::string_view reading) {
    const DictFlags r = Flags(reading);
    if (r.records == 0 && !r.layerSurname && !r.layerGiven) {
        const DictFlags b = Flags(base);
        return b.common && !b.person && !b.city && !b.layerSurname && !b.layerGiven;
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
    auto named = [&](const dict::Codes* codes, std::string_view target) {
        if (codes == nullptr || target.empty()) return false;
        const dict::SemCodes& sem = codes->sem;
        const std::string_view attr = codes->gram.Text();
        if (!sem.HasPrefix("QPN") && !sem.HasPrefix("QCT") && !attr.starts_with("Wa") && !attr.starts_with("Wb")) {
            return false;
        }
        return text::SameSound(target, sound);
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
    std::size_t chain = 0;  // first run of the current katakana chain
    for (std::size_t i = 0; i < runs.size(); i++) {
        const Run& run = runs[i];
        if (run.kanji) {
            count(runs_, std::string(run.text));
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
        if (const std::string t = HonorificRun(line, run); !t.empty()) count(hon_, t);
        if (!run.kanji && RuneCount(run.text) >= 2 && AgentCue(std::string_view(line).substr(run.end))) {
            count(agents_, std::string(run.text));
        }
        if (!run.kanji && RuneCount(run.text) >= 2 && NamingCue(std::string_view(line).substr(run.end)) &&
            !RoleLike(run.text) && !Flags(run.text).role) {
            count(gloss_, std::string(run.text));
        }
    }
    for (std::size_t from = 0; from <= line.size();) {
        RubyMatch m;
        if (!FindGloss(line, from, m)) break;
        const std::string base(m.base);
        if (Gikun(base, m.reading)) {
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
            // a role (放送, 司会), not a name.
            if (plain) {
                const DictFlags f = Flags(p);
                if (f.common && !f.person && !f.layerSurname && !f.layerGiven) continue;
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
        } else if (f.common && !f.person && !f.city && PersonName(t).empty()) {
            w = 0.5;
        }
        Name& n = all[t];
        n.score += static_cast<double>(std::min(c, kHonorificCap)) * w;
        n.person = true;
    }
    for (const auto& [t, c] : kanaHon_) {
        Name& n = all[t];
        n.score += static_cast<double>(std::min(c, kHonorificCap));
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
    for (const auto& [x, c] : hon_) {
        (void)c;
        if (att.count(x) || RuneCount(x) < 2 || !AllRunes(x, IsKatakana) || Flags(x).role) continue;
        for (const auto& [t, kc] : kana_) {
            if (t.size() > x.size() && t.starts_with(x) && (att.count(t) || (kc >= 2 && Flags(t).records == 0))) {
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
        // A common word is no full name (沢山 is 沢 + 山 only as a name).
        const DictFlags fj = Flags(joined);
        if (fj.common && !fj.person) return;
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
    for (const auto& [t, c] : kana_) {
        if (c < 2 || att.count(t) || Flags(t).records > 0) continue;
        for (const std::string& x : att) {
            if (x.size() < t.size() && t.starts_with(x) && RuneCount(x) >= 2 && AllRunes(x, IsKatakana)) {
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
    scoresValid_ = true;
    for (const std::string& t : Names()) {
        Name& n = scores_[t];
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
// characters or more. A katakana word is not split.
bool Registry::Split(std::string_view t, std::vector<std::string>& parts) {
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
        if (r != reading_.end() && !r->second.empty()) return text::FromKana(r->second, preset);
        const auto v = tables_->readings.find(part);
        if (v != tables_->readings.end() && !v->second.empty()) {
            if (IsKanaText(v->second)) return text::FromKana(v->second, preset);
            return v->second;
        }
        if (!AllRunes(part, IsKanji)) return text::FromKana(part, preset);
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
                if (text::FromKana(rs.substr(at[idx]), p) == h) {
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
                if (text::FromKana(rs.substr(0, at[idx]), p) == sh) {
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
    const std::map<std::string, Name> acc = Accepted();
    std::vector<Occurrence> out;
    if (acc.empty()) return out;
    std::map<char32_t, std::vector<std::string>> byFirst;
    for (const auto& [t, n] : acc) byFirst[RuneAt(t, 0).cp].push_back(t);
    for (auto& [c, list] : byFirst) {
        std::stable_sort(list.begin(), list.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    }
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
