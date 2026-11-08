// The per-token step: segmentation of a token into dictionary words, candidate groups
// and the record of words the dictionary does not know.
#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

#include "core/dict/char_readings.h"
#include "core/dict/classes.h"
#include "core/output/cleanup.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/unicode.h"
#include "core/tokenizer/canonical.h"
#include "core/tokenizer/charsets.h"
#include "core/tokenizer/classify.h"
#include "core/word/conjugation.h"
#include "core/word/kana.h"
#include "core/word/strings.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

// A kanji digit, ○ or 〇.
bool IsNumeral(std::string_view c) { return (!c.empty() && Has(kNumeralDigits, c)) || c == kNumeralCircle; }

// The run of letters, digits, ',' and '.' at s.
std::string_view AsciiRun(std::string_view s) {
    std::size_t n = 0;
    while (n < s.size() && (IsDigit(s[n]) || IsAlpha(s[n]) || s[n] == ',' || s[n] == '.')) n++;
    return s.substr(0, n);
}

// The first two width units of s: one wide character or two narrow ones; a narrow
// character before a wide one stands alone.
std::string_view PairText(std::string_view s) {
    const text::Rune r = text::DecodeRune(s);
    if (Width(r.cp) == 2) return s.substr(0, r.size);
    const text::Rune r2 = text::DecodeRune(s.substr(r.size));
    if (r2.size > 0 && Width(r2.cp) == 1) return s.substr(0, r.size + r2.size);
    return s.substr(0, r.size);
}

// 、（）「」『』.
bool StartsWithSymbol(std::string_view s) { return !s.empty() && Has("、（）「」『』", FirstChar(s)); }

// A kanji numeral of up to two digits as ASCII digits (二十五 -> "25", 十 -> "10",
// 二十 -> "20", 三 -> "3"); a first character that is not a numeral reads as 0. Text
// that starts with an ASCII character is returned as is.
std::string EncodeNumericPair(std::string_view text) {
    if (text.empty() || static_cast<unsigned char>(text[0]) < 0x80) return std::string(text);
    std::u32string rs;
    for (std::string_view s = text; !s.empty();) {
        const text::Rune r = text::DecodeRune(s);
        rs.push_back(r.cp);
        s.remove_prefix(r.size);
    }
    auto position = [&](std::u32string_view set, std::size_t i) -> int {
        if (i >= rs.size()) return 0;
        const std::size_t k = set.find(rs[i]);
        return k == std::u32string_view::npos ? -1 : static_cast<int>(k);
    };
    constexpr std::u32string_view kDigits = U"○一二三四五六七八九";
    constexpr std::u32string_view kUnits = U"十百千万億";
    if (position(kUnits, 0) != -1) {
        if (const int p = position(kDigits, 1); p != -1) return std::string{'1', static_cast<char>(p + '0')};
        return "10";
    }
    std::string digits(1, static_cast<char>(std::max(position(kDigits, 0), 0) + '0'));
    if (rs.size() > 1 && position(kUnits, 1) != -1) {
        if (const int p = position(kDigits, 2); p != -1) digits += static_cast<char>(p + '0');
    }
    return digits;
}

// Whether the text before cursor ends with 万 or 億: the number after it is a part of the
// larger number (三万五千 3만5천).
bool AfterMyriad(std::string_view full, int cursor) {
    const std::string_view head = full.substr(0, static_cast<std::size_t>(cursor));
    return head.ends_with("万") || head.ends_with("億");
}

// A kanji number at the start of s read with 十, 百 and 千 (百二十 120, 五百 500, 二千五百
// 2500): the bytes it takes and its value in digits. Two digits in a row end it (三四十 is
// three, then forty), and so does a unit not smaller than the one before. A number that goes
// on after 万 or 億 is read whole and written in groups of four digits (一万二千三百
// 1만 2300, 三億五千万 3억 5000만); one that ends at 万 or 億 leaves it to the word after.
struct KanjiNumber {
    std::size_t size = 0;
    std::string digits;
};

// The part of a number below 万 at the start of s: its bytes and value.
std::pair<std::size_t, long> ParseKanjiGroup(std::string_view s) {
    constexpr std::u32string_view kDigits = U"〇一二三四五六七八九";
    long total = 0, lastUnit = 10000;
    int digit = -1;
    std::size_t k = 0, used = 0;
    while (k < s.size()) {
        const text::Rune r = text::DecodeRune(s.substr(k));
        const std::size_t d = r.cp == U'○' ? 0 : kDigits.find(r.cp);
        const long unit = r.cp == U'十' ? 10 : r.cp == U'百' ? 100 : r.cp == U'千' ? 1000 : 0;
        if (d != std::u32string_view::npos) {
            if (digit >= 0) break;
            digit = static_cast<int>(d);
            k += r.size;
            continue;
        }
        if (unit == 0 || unit >= lastUnit) break;
        total += (digit >= 0 ? digit : 1) * unit;
        digit = -1;
        lastUnit = unit;
        k += r.size;
        used = k;
    }
    if (digit >= 0) {
        total += digit;
        used = k;
    }
    return {used, total};
}

KanjiNumber ParseKanjiNumber(std::string_view s) {
    struct Myriad {
        std::string_view kanji;
        std::string_view korean;
    };
    constexpr Myriad kMyriads[] = {{"億", "억"}, {"万", "만"}};
    KanjiNumber n;
    std::string written;
    std::size_t at = 0;
    int next = 0;  // the first myriad still allowed
    for (;;) {
        const auto [size, value] = ParseKanjiGroup(s.substr(at));
        if (size == 0) break;
        const std::string_view after = s.substr(at + size);
        int m = next;
        while (m < 2 && !after.starts_with(kMyriads[m].kanji)) m++;
        // A group closed by 万 or 億 belongs to the number only when another group follows.
        if (m < 2 && value > 0 && ParseKanjiGroup(after.substr(kMyriads[m].kanji.size())).first > 0) {
            if (!written.empty()) written += ' ';
            written += std::to_string(value);
            written += kMyriads[m].korean;
            at += size + kMyriads[m].kanji.size();
            next = m + 1;
            continue;
        }
        if (value > 0 || written.empty()) {
            if (!written.empty()) written += ' ';
            written += std::to_string(value);
        }
        at += size;
        break;
    }
    if (at == 0 || written.empty()) return n;
    n.size = at;
    n.digits = std::move(written);
    return n;
}

// The Sino-Korean reading of a number of up to four digits (15 십오, 120 백이십, 2000 이천);
// empty for any other text.
std::string SinoKoreanNumber(std::string_view digits) {
    constexpr std::string_view kDigits[] = {"", "일", "이", "삼", "사", "오", "육", "칠", "팔", "구"};
    constexpr std::string_view kUnits[] = {"", "십", "백", "천"};
    if (digits.empty() || digits.size() > 4 || digits[0] == '0') return {};
    std::string out;
    for (std::size_t k = 0; k < digits.size(); k++) {
        if (!IsDigit(digits[k])) return {};
        const int d = digits[k] - '0';
        const std::size_t unit = digits.size() - 1 - k;
        if (d == 0) continue;
        if (d > 1 || unit == 0) out += kDigits[d];
        out += kUnits[unit];
    }
    return out;
}

// After a noun parent, the particle records (kind 2) that follow the first verb or
// adjective record move to the front (behind the leading plain-dictionary records).
bool AllHiragana(std::string_view s);
bool HasKanji(std::string_view s);

// Whether every particle among records is a single character.
bool OneKanaParticles(const Record* records) {
    for (const Record* r = records; r != nullptr; r = r->child) {
        if (At(r->partCode, 1) == '2' && text::RuneCount(r->source) > 1) return false;
    }
    return true;
}

// Whether s starts with the past or て form of an adjective after its stem (かった, かっ, くて) (LEX-4720).
bool AdjectiveFormAfter(std::string_view s) {
    for (const std::string_view f : {"かった", "かっ", "くて"}) {
        if (s.starts_with(f)) return true;
    }
    return false;
}

Record* ReorderAfterNoun(const Item& parent, Record* records, const dict::Snapshot& snapshot, std::string_view text) {
    if (parent.EntryText(0) == kKoCloseQuote || parent.EntryText(0) == ")" || At(parent.Code(0), 0) != '1') {
        return records;
    }
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    // A counter after a numeral keeps its place (三か所, not 三 + か + 所).
    if (const std::string_view g = parent.GAttr(0);
        first != nullptr && (Has(g, "Nj") || Has(g, "Nd")) && Has(first->attr, "Ct")) {
        return records;
    }
    bool reorder = false;
    Record* detached = nullptr;
    for (Record* p = first; p != nullptr; p = p->child) {
        const std::string_view part = p->partCode;
        if (At(part, 0) >= 'A' && At(part, 0) < 'G') break;
        if (const char k = At(part, 1); k == '4') {
            if (Has(p->attr, "Pj")) break;
            if (!reorder) {
                if (WidthOf(p->source) == 2) {
                    Record* next = p->child;
                    while (next != nullptr && (next->source != p->source || At(next->partCode, 1) != '2')) next = next->child;
                    reorder = next != nullptr;
                } else {
                    reorder = true;
                }
            }
        } else if (k > '6' && k <= '8') {
            if (Has(p->attr, "Pj")) break;
            // An adjective stem of three or more kana keeps its place before particles of one kana
            // after which no word of two characters or more starts: the particle would leave a piece
            // of the adjective as a word (水着かわいい, not か + わ + いい; but それはかなわない). So does a
            // shorter one before a past or て form that makes it three kana or more (ホントよかった, not
            // よ + 買った).
            if (k == '7' && AllHiragana(p->source) && OneKanaParticles(first)) {
                if (text::RuneCount(p->source) < 3 && AdjectiveFormAfter(text.substr(p->source.size()))) break;
                const std::string_view rest = text.substr(text::DecodeRune(text).size);
                if (text::RuneCount(p->source) >= 3 && text::RuneCount(rest.substr(0, snapshot.WordAt(rest))) < 2) break;
            }
            reorder = true;
        }
        if (!reorder) continue;
        if (Record* c = p->child; c != nullptr && At(c->partCode, 1) == '2' && c->source != kJaNa) {
            detached = c;
            p->child = nullptr;
            break;
        }
    }
    if (!reorder || detached == nullptr) return records;
    Record* last = detached;
    while (last->child != nullptr) last = last->child;
    last->child = first;
    if (owned == nullptr) return detached;
    owned->child = detached;
    return records;
}

// After a noun parent, a suffix noun of persons that attaches to it (gram Tu, sense VHn:
// たち) moves ahead of the verb or adjective records of a shorter key it starts with
// (犬たちに: 들, not 断ち + に).
Record* SuffixAfterNoun(const Item& parent, Record* records) {
    if (At(parent.Code(0), 0) != '1') return records;
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr || (At(first->partCode, 1) != '4' && At(first->partCode, 1) != '7')) return records;
    std::string_view suffix;
    for (Record* r = first; r != nullptr; r = r->child) {
        if (At(r->partCode, 1) == '1' && Has(r->attr, "Tu") && Has(r->semantic, "VHn") &&
            r->source.size() > first->source.size() && r->source.starts_with(first->source)) {
            suffix = r->source;
            break;
        }
    }
    if (suffix.empty()) return records;
    Record* nouns = nullptr;
    Record* rest = nullptr;
    Record** nounTail = &nouns;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (At(r->partCode, 1) == '1' && r->source == suffix) {
            *nounTail = r;
            nounTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *nounTail = rest;
    if (owned == nullptr) return nouns;
    owned->child = nouns;
    return records;
}

bool SuffixHost(const Item* before, const dict::Snapshot& snapshot);

// After a name or a noun of persons or animals (SuffixHost) read as a noun, the noun records of an
// honorific (honorific.candidate) move ahead of a longer hiragana noun that starts with it
// (田中さんまで 다나카 씨까지, not さんま 꽁치 + で; 敵さんかい 적 씨야, not さんかい 산회; but
// 大小さまざま, 進みさまざま), unless a particle row spells the honorific with what follows
// (節子さんや 씨랑). Plain-dictionary records stay in front.
Record* HonorificAfterNoun(const Item& parent, Record* records, const dict::Snapshot& snapshot) {
    if (At(parent.Code(0), 0) != '1' || !SuffixHost(&parent, snapshot)) return records;
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr) return records;
    std::string_view honorific;
    for (Record* r = first; r != nullptr && honorific.empty(); r = r->child) {
        if (At(r->partCode, 1) == '1' && dict::InClass(HDTRANS_CLASS("honorific.candidate"), r->source)) honorific = r->source;
    }
    bool longer = false;
    for (Record* r = first; r != nullptr && !honorific.empty(); r = r->child) {
        if (r->source.size() <= honorific.size() || !r->source.starts_with(honorific)) continue;
        // A row that spells the honorific with a particle (さんや 씨랑) reads it already.
        if (FirstChar(r->partCode) == "P") return records;
        longer = longer || (At(r->partCode, 0) == 'N' && At(r->partCode, 1) == '1' && AllHiragana(r->source));
    }
    if (!longer) return records;
    Record* nouns = nullptr;
    Record* rest = nullptr;
    Record** nounTail = &nouns;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (At(r->partCode, 1) == '1' && r->source == honorific) {
            *nounTail = r;
            nounTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *nounTail = rest;
    if (owned == nullptr) return nouns;
    owned->child = nouns;
    return records;
}

// Text that starts with a particle or punctuation, or is empty.
bool NounEnd(std::string_view text) {
    if (text.empty()) return true;
    const std::string_view c = FirstChar(text);
    // や that lists nouns, before a mark, a space or a word not in hiragana (おもてなしや サービス; not やはり).
    if (c == "や") {
        const text::Rune next = text::DecodeRune(text.substr(c.size()));
        return next.size == 0 || !(next.cp >= U'ぁ' && next.cp <= U'ゖ');
    }
    return text::DecodeRune(c).cp != text::kReplacement && Has("にがをはもでとへの、。，．！？」』）…", c);
}

// Whether before, the word ahead of a suffix, is a name or a noun of the dictionary with a sense
// of persons or animals among its candidates, those of its group or the other records of its surface
// (モモ 복숭아 beside the name モモ), or a proper noun of no common sense that the name layer
// reads as a person (ふたば, not さくら 벚꽃); not itself a suffix written attached (ら, たち).
bool SuffixHost(const Item* before, const dict::Snapshot& snapshot) {
    if (before == nullptr) return false;
    if (before->IsNameItem()) return true;
    auto person = [](std::string_view code, std::string_view sem, std::string_view gram) {
        return code.starts_with('1') && (Has(sem, "VHn") || Has(sem, "QPN") || Has(sem, "QAA")) && !Has(gram, "Tu");
    };
    bool proper = false, common = false;
    for (const Entry* e : before->group->entries) {
        if (person(e->code, e->attr, e->gattr)) return true;
        const bool properCode = dict::PartCode(std::string(1, e->cls) + std::string(e->code)).ProperNoun();
        if (!properCode) common = true;
        proper = proper || properCode;
    }
    for (const Record* r = before->group->records; r != nullptr; r = r->child) {
        if (r->source != before->group->surface) continue;
        if (person(From(r->partCode, 1), r->semantic, r->attr)) return true;
        const bool properCode = dict::PartCode(r->partCode).ProperNoun();
        if (!properCode) common = true;
        proper = proper || properCode;
    }
    if (!proper || common) return false;
    return snapshot.NamePerson(before->group->surface);
}

}  // namespace

// Text path: a suffix of honorific.after-person (たん, っち) at full[cursor:] is a word only
// after a name or a noun of persons or animals (先生たん, 小林っち, 猫たん) and when no longer
// dictionary word starts at that noun and goes past it (たまご + っち of たまごっち), nor a longer
// noun at the suffix that ends before a particle, a mark or a character other than hiragana
// (たんこぶ, たんす); elsewhere its records leave the candidates, so that the longer word or the
// kana around it are read instead. Where the suffix is a word, a longer word other than a noun
// that starts with it leaves instead (姫たん + と, not the adverb たんと).
Record* Worker::DropStraySuffixes(Record* records, std::string_view full, int cursor, int token, Item* parent) {
    std::size_t suffix = 0;
    for (const Record* r = records; r != nullptr; r = r->child) {
        if (dict::InClass(HDTRANS_CLASS("honorific.after-person"), r->source)) suffix = std::max(suffix, r->source.size());
    }
    if (suffix == 0) return records;
    bool covered = false;
    for (const Record* r = records; r != nullptr && !covered; r = r->child) {
        const std::string_view at = From(full, cursor);
        if (At(r->partCode, 1) != '1' || r->source.size() <= suffix || !at.starts_with(r->source)) continue;
        const std::string_view after = at.substr(r->source.size());
        const text::Rune next = text::DecodeRune(after);
        covered = NounEnd(after) || next.cp < U'ぁ' || next.cp > U'ゖ';
    }
    const Item* before = WordBefore(cursor, token, parent);
    if (before != nullptr && before->tokenIndex == token && before->start < cursor) {
        for (const dict::Candidate& c : snapshot.GetWordInfo(From(full, before->start), dict::LookupMode::General, {}).list) {
            covered = covered || static_cast<int>(c.source.size()) > cursor - before->start;
        }
    }
    const bool hosted = !covered && SuffixHost(before, snapshot);
    const auto overSuffix = [&](const Record* r) {
        if (!hosted || From(r->partCode, 1).starts_with('1')) return false;
        for (const std::string_view m : dict::ClassMembers(HDTRANS_CLASS("honorific.after-person"))) {
            if (r->source.size() > m.size() && r->source.starts_with(m)) return true;
        }
        return false;
    };
    Record* out = nullptr;
    Record** tail = &out;
    for (Record* r = records; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if ((!dict::InClass(HDTRANS_CLASS("honorific.after-person"), r->source) || hosted) && !overSuffix(r)) {
            *tail = r;
            tail = &r->child;
        }
        r = next;
    }
    return out;
}

namespace {

// The lengths in characters of the keys of tag letter tag that start text in mode, as
// a bit set (lengths of 63 or more share the top bit).
uint64_t KeyLengths(const dict::Snapshot& snapshot, std::string_view text, dict::LookupMode mode, char tag) {
    uint64_t lengths = 0;
    for (const dict::Candidate& c : snapshot.GetWordInfo(text, mode, {}).list) {
        if (c.codes->part.Tag() == tag) lengths |= uint64_t{1} << std::min<std::size_t>(text::RuneCount(c.source), 63);
    }
    return lengths;
}

// Whether text starts with what follows a noun rather than a verb form: a particle of
// two or more characters (から, まで), or a key that is both a particle and an ending
// after an adjectival noun (だ, です, か, よ), with no longer verb ending at text (ない,
// なさい over な; て, た and ます are verb endings only).
bool NounGoesOn(const dict::Snapshot& snapshot, std::string_view text) {
    const uint64_t particle = KeyLengths(snapshot, text, dict::LookupMode::General, 'P');
    const uint64_t noun = (particle & KeyLengths(snapshot, text, dict::LookupMode::AdjectivalNounEnding, 'U')) |
                          (particle & ~uint64_t{3});
    const uint64_t verb = KeyLengths(snapshot, text, dict::LookupMode::VerbEnding, 'S');
    return noun != 0 && std::bit_width(noun) >= std::bit_width(verb);
}

// Whether a key that ends in hiragana runs into kana or a kanji that a noun cannot take
// (経験あり + ます, 取り分け + て, 経験あり + そうだ, 追って書き + 込んで): the key spells a
// verb form there. A noun takes a particle, punctuation or a copula form.
bool RunsIntoWord(const dict::Snapshot& snapshot, std::string_view key, std::string_view after) {
    const int next = NameClassOf(after);
    return NameClassOf(LastChar(key)) == kClassHiragana && next != kClassKatakana && next != kClassOther &&
           !NounEnd(after) && !NounGoesOn(snapshot, after);
}

// A noun row of tag letter I, K or N: a common or proper noun. The other letters of
// class-1 rows mark phrase and function rows (すみません, このあと, ぶり).
bool NounRow(const Record& r) { return At(r.partCode, 1) == '1' && Has("IKN", FirstChar(r.partCode)); }

// Whether the noun row r at text is read whole after a shorter noun row.
bool ReadsWhole(const dict::Snapshot& snapshot, const Record& r, std::string_view text) {
    return NounRow(r) && !RunsIntoWord(snapshot, r.source, text.substr(r.source.size()));
}

// Text path: candidates are ordered by part tag before length, so a shorter record
// came before a longer dictionary noun it starts. That noun moves to the front:
// - after a noun row of another letter, unless the noun runs on as a verb form: the
//   letters of noun rows do not rank word boundaries, length does, as within one
//   letter (治験薬, not 治験 + 薬);
// - after an adnominal in の written from a kanji (大の, 大粒の), when a particle,
//   punctuation or the end of the text follows it (大の字, 大粒の雨). Demonstratives
//   (あの人, この世) and adverbs keep their order: their compound rows read worse than
//   the parts.
// The shorter record is the first one that reads at the text (lead), not a record before it that
// does not (おも of 思う before おもて + なし at おもてなし). Plain-dictionary records stay in front.
// Whether s starts with a form of the copula (not the で of できる), the な of an adnominal or
// of なの, なんだ (not しっかりなさい), or a question か that ends a clause or goes on to a final
// particle (アリか？, ばっちりかな; not しっかりかけて).
bool CopulaFollows(std::string_view s) {
    for (const std::string_view c : {"だ", "です", "でし", "じゃ", "なの", "なん", "かな", "かよ", "かも", "かしら"}) {
        if (s.starts_with(c)) return true;
    }
    for (const std::string_view c : {"な", "か"}) {
        if (!s.starts_with(c)) continue;
        const text::Rune next = text::DecodeRune(s.substr(c.size()));
        return next.size == 0 || !(next.cp >= U'ぁ' && next.cp <= U'ゖ');
    }
    return false;
}

// Whether a verdict of word.copula-verdict may stand after the text before it: at the start, after a
// mark (、 「 …) or a space, or after a particle of particle.before-verdict (それもありだな, それってありなの);
// after any other word it is that word's label of being there or not (駐車場ありだ, あなたなしじゃ).
bool VerdictPlace(std::string_view before) {
    while (!before.empty() && text::IsSpace(DecodeLastRune(before).cp)) before.remove_suffix(DecodeLastRune(before).size);
    if (before.empty()) return true;
    for (const std::string_view p : dict::ClassMembers(HDTRANS_CLASS("particle.before-verdict"))) {
        if (before.ends_with(p)) return true;
    }
    const char32_t r = DecodeLastRune(before).cp;
    return !text::IsLetter(r) && !text::IsDigit(r);
}

// An adverb or a noun of word.copula-predicate is its adjectival noun before a copula, an adnominal
// な or a question か (ばっちりだ 완벽하다, アリなのか 괜찮은 건가): the adjectival noun records of the
// key go first, and the others stay behind them for an ending they do not take.
Record* CopulaPredicateFirst(Record* records, std::string_view text) {
    if (records == nullptr || !dict::InClass(HDTRANS_CLASS("word.copula-predicate"), records->source) ||
        !CopulaFollows(text.substr(std::min(text.size(), records->source.size())))) {
        return records;
    }
    const std::string_view key = records->source;
    Record* front = nullptr;
    Record* back = nullptr;
    Record** frontTail = &front;
    Record** backTail = &back;
    for (Record* r = records; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (r->source == key && At(r->partCode, 1) == '8') {
            *frontTail = r;
            frontTail = &r->child;
        } else {
            *backTail = r;
            backTail = &r->child;
        }
        r = next;
    }
    *frontTail = back;
    return front != nullptr ? front : back;
}

// The adjectival noun records of a verdict of word.copula-verdict read only before a copula, an
// adnominal な or a question か and in a VerdictPlace (それもありだな 그것도 괜찮네); elsewhere they are
// left out when other records remain (駐車場ありだ, 関係ありません, 許可無しに, ありそう).
Record* VerdictsInPlace(Record* records, std::string_view before, std::string_view text) {
    const auto stray = [&](const Record* r) {
        if (At(r->partCode, 1) != '8' || !dict::InClass(HDTRANS_CLASS("word.copula-verdict"), r->source)) return false;
        const std::string_view after = text.substr(std::min(text.size(), r->source.size()));
        // A bare question か after one of word.verdict-asks-there asks whether the thing is there
        // (所持品は無しか); かな, かも and the copula still give the verdict (それもなしかな).
        const bool asks = after.starts_with("か") && !CopulaFollows(after.substr(std::string_view("か").size())) &&
                          !after.starts_with("かな") && !after.starts_with("かよ") && !after.starts_with("かも") &&
                          !after.starts_with("かしら") && dict::InClass(HDTRANS_CLASS("word.verdict-asks-there"), r->source);
        // A sentence-final な after it is no copula of the verdict (デートはナシな).
        const bool finalNa = after.starts_with("な") && !after.starts_with("なの") && !after.starts_with("なん");
        return asks || finalNa || !(CopulaFollows(after) && VerdictPlace(before));
    };
    bool any = false;
    bool others = false;
    for (const Record* r = records; r != nullptr; r = r->child) (stray(r) ? any : others) = true;
    if (!any || !others) return records;
    Record* kept = nullptr;
    Record** tail = &kept;
    for (Record* r = records; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (!stray(r)) {
            *tail = r;
            tail = &r->child;
        }
        r = next;
    }
    return kept;
}

// LEX-5000: a kana noun of word.adjective-noun-lead spells an adjective and a formal noun (いいわけ: いい + わけ).
// Before a predicate ending (じゃ, では, で, だ, です, ない, ある, ね, よ, か, が or も before ない or ある, a stop or
// the end) and not after a modifier (の, な, という), the noun records of the key go behind the others, so the
// adjective and its pattern read there (すればいいわけ？ 하면 되는 거?, いいわけがない 좋을 리가 없다); 男のいいわけ,
// いいわけを keep the noun.
Record* AdjectiveOverKanaNoun(Record* records, std::string_view before, std::string_view text) {
    if (records == nullptr || records->user != nullptr ||
        !dict::InClass(HDTRANS_CLASS("word.adjective-noun-lead"), records->source) || !text.starts_with(records->source)) {
        return records;
    }
    while (!before.empty() && text::IsSpace(DecodeLastRune(before).cp)) before.remove_suffix(DecodeLastRune(before).size);
    if (before.ends_with("の") || before.ends_with("な") || before.ends_with("という") || before.ends_with("っていう")) {
        return records;
    }
    const std::string_view key = records->source;
    std::string_view after = text.substr(key.size());
    if (after.starts_with("が") || after.starts_with("も")) {
        after.remove_prefix(text::DecodeRune(after).size);
        if (!after.starts_with("な") && !after.starts_with("あ")) return records;
    }
    bool predicate = after.empty() || RuneIn(text::DecodeRune(after).cp, "。、！？!?…‥―ー～」』）) 　");
    for (const std::string_view c : {"じゃ", "では", "で", "だ", "です", "でし", "な", "あ", "ね", "よ", "か"}) {
        predicate = predicate || after.starts_with(c);
    }
    if (!predicate) return records;
    Record* front = nullptr;
    Record* back = nullptr;
    Record** frontTail = &front;
    Record** backTail = &back;
    for (Record* r = records; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (r->source == key && At(r->partCode, 1) == '1') {
            *backTail = r;
            backTail = &r->child;
        } else {
            *frontTail = r;
            frontTail = &r->child;
        }
        r = next;
    }
    *frontTail = back;
    return front != nullptr ? front : back;
}

bool KatakanaWordRune(char32_t r) { return (r >= U'ァ' && r <= U'ヶ') || r == U'ー'; }

// LEX-95: where the longest word at a point of a katakana run is no name and leaves, beside common
// words, a stray piece: a word the dictionary reads only as a person's name, or kana no dictionary
// word starts at, read by their sound (フリースリーカード: フリース, リー, カード; アイスミルクティー:
// アイスミルク, ティー), a shorter word goes first when common words of three or more kana cover the
// rest of the run with no more words (フリー, スリー, カード; アイス, ミルクティー); of those words the
// longest. Words are dictionary keys of two or more kana inside the run, as the lookup of the word
// stage finds them. A plain word with force keeps its place, and so does every word of a run longer
// than 24 kana. records holds the words at full[cursor:], longest first.
Record* KatakanaCoverFirst(const Worker& worker, Record* records, std::string_view full, std::size_t cursor) {
    constexpr std::size_t kMaxRun = 24;
    if (records == nullptr || (records->user != nullptr && records->user->force)) return records;
    cursor = std::min(cursor, full.size());
    if (cursor == full.size() || !KatakanaWordRune(text::DecodeRune(full.substr(cursor)).cp)) return records;
    // The kana of the run before the cursor, and the rune boundaries of the run from it.
    std::size_t before = 0;
    for (std::size_t at = cursor; at > 0 && before <= kMaxRun;) {
        std::size_t start = at - 1;
        while (start > 0 && (static_cast<unsigned char>(full[start]) & 0xC0) == 0x80) start--;
        if (!KatakanaWordRune(text::DecodeRune(full.substr(start)).cp)) break;
        at = start;
        before++;
    }
    const std::string_view text = full.substr(cursor);
    std::vector<std::size_t> bounds{0};
    for (std::string_view s = text; !s.empty() && before + bounds.size() <= kMaxRun + 1;) {
        const text::Rune r = text::DecodeRune(s);
        if (!KatakanaWordRune(r.cp)) break;
        bounds.push_back(bounds.back() + r.size);
        s.remove_prefix(r.size);
    }
    const std::size_t run = bounds.back();
    const std::size_t greedy = records->source.size();
    if (bounds.size() < 5 || before + bounds.size() > kMaxRun + 1 || greedy >= run ||
        !std::binary_search(bounds.begin(), bounds.end(), greedy)) {
        return records;
    }
    if (const std::string_view rest = text.substr(run); !rest.empty() && KatakanaWordRune(text::DecodeRune(rest).cp)) {
        return records;
    }
    constexpr int kNone = 1 << 20;
    const std::size_t n = bounds.size();
    // The pieces at bounds[i]: the index in bounds of the end, whether the piece is a stray one (a
    // word the dictionary reads only as a person's name, or kana read by their sound), and whether
    // it is a common word (no reading as a proper noun nor in Latin letters, such as ホワイ WHY).
    struct Piece {
        std::size_t end;
        bool stray, common;
    };
    std::vector<std::vector<Piece>> pieces(n);
    for (std::size_t i = 0; i + 2 < n; i++) {
        const dict::Candidates found = worker.WordInfo(text.substr(bounds[i]), kModeGeneral);
        for (std::size_t j = i + 2; j < n; j++) {
            const std::size_t length = bounds[j] - bounds[i];
            bool any = false, person = true, proper = false;
            for (const dict::Candidate& c : found.list) {
                if (c.source.size() != length || c.codes == nullptr) continue;
                any = true;
                person = person && c.codes->partText == "N130" && c.codes->sem.Text().find("QPN") != std::string_view::npos;
                proper = proper || c.codes->partText == "N130" ||
                         std::any_of(c.target.begin(), c.target.end(), [](char ch) {
                             return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
                         });
            }
            if (any) pieces[i].push_back({j, person, !proper});
        }
    }
    // Kana where no dictionary word is found are read by their sound up to where a word starts (or
    // the run ends). Such a piece counts as a stray piece when it ends the run and has two or more
    // kana, the first not ー, ン or a small kana; one kana or such a start belongs to the word before
    // it (ジャーニー|ズ, データベースサーバ|ー, フルーティ|ング), and a piece inside the run may be a part
    // of a word the dictionary lacks (ゴールド|アーキッド).
    for (std::size_t i = 0; i + 1 < n; i++) {
        if (!pieces[i].empty()) continue;
        std::size_t j = i + 1;
        while (j + 1 < n && pieces[j].empty()) j++;
        const char32_t r = text::DecodeRune(text.substr(bounds[i])).cp;
        const bool bound = r == U'ー' || std::u32string_view(U"ンァィゥェォッャュョヮヵヶ").find(r) != std::u32string_view::npos;
        pieces[i].push_back({j, j - i >= 2 && !bound && j == n - 1, false});
    }
    // persons[i][w]: the fewest stray pieces of a cover of the run from bounds[i] by w pieces.
    std::vector<std::vector<int>> persons(n, std::vector<int>(n, kNone));
    persons[n - 1][0] = 0;
    for (std::size_t i = n - 1; i-- > 0;) {
        for (const Piece& p : pieces[i]) {
            for (std::size_t w = 0; w + 1 < n; w++) {
                if (persons[p.end][w] == kNone) continue;
                persons[i][w + 1] = std::min(persons[i][w + 1], persons[p.end][w] + (p.stray ? 1 : 0));
            }
        }
    }
    // The cover after the longest word, a word that is no name, by its fewest words.
    std::size_t words = 0;
    int greedyPersons = kNone;
    for (const Piece& p : pieces[0]) {
        if (bounds[p.end] != greedy || p.stray) continue;
        for (std::size_t w = 0; w + 1 < n && greedyPersons == kNone; w++) {
            if (persons[p.end][w] != kNone) {
                words = w + 1;
                greedyPersons = persons[p.end][w];
            }
        }
    }
    if (greedyPersons == kNone || greedyPersons == 0) return records;
    // covered[i][w]: whether common words of three or more kana cover the run from bounds[i] by w
    // words.
    std::vector<std::vector<char>> covered(n, std::vector<char>(n, 0));
    covered[n - 1][0] = 1;
    for (std::size_t i = n - 1; i-- > 0;) {
        for (const Piece& p : pieces[i]) {
            if (!p.common || p.end - i < 3) continue;
            for (std::size_t w = 0; w + 1 < n; w++) covered[i][w + 1] |= covered[p.end][w];
        }
    }
    std::size_t chosen = 0;
    for (const Piece& p : pieces[0]) {
        if (bounds[p.end] >= greedy || bounds[p.end] <= chosen || !p.common || p.end < 3) continue;
        for (std::size_t w = 0; w < words; w++) {
            if (covered[p.end][w]) chosen = bounds[p.end];
        }
    }
    if (chosen == 0) return records;
    Record* front = nullptr;
    Record* back = nullptr;
    Record** frontTail = &front;
    Record** backTail = &back;
    for (Record* r = records; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        Record**& tail = r->source.size() == chosen ? frontTail : backTail;
        *tail = r;
        tail = &r->child;
        r = next;
    }
    if (front == nullptr) return records;
    *frontTail = back;
    return front;
}

// LEX-5106: where the longest word at a point of a kanji run ends inside the run and the rest of the
// run, read word by word, takes more words than a cover that starts with a shorter word (米中 + 間選 +
// 挙 against 米 + 中間選挙; 敵機 + 動 + 部隊 against 敵 + 機動部隊), the first word of the cover with the
// fewest words goes first; of those covers the one whose first word is longest. A piece is a dictionary
// key of two or more kanji found at a point of the run, or one kanji; a piece that starts with 々 counts
// as two, as 々 repeats the kanji before it (江戸 + 家 + 代々, not 家代 + 々), and so does a piece with 々
// that the dictionary reads only as a name (親睦会 + 々 + 場, not 親睦 + the name 会々 + 場). A run from a
// kanji digit or 第 is left to the readings of numbers. A plain word with force keeps its place, and so
// does every word of a run longer than 16 kanji. A first word of one kanji is also read as that kanji
// where its rows do not read there (軽 of 軽乗用車, an adjective stem). records holds the words at
// full[cursor:], longest first.
Record* KanjiCoverFirst(Worker& worker, Record* records, std::string_view full, std::size_t cursor, int token) {
    constexpr std::size_t kMaxRun = 16;
    if (records == nullptr || (records->user != nullptr && records->user->force)) return records;
    cursor = std::min(cursor, full.size());
    const std::string_view text = full.substr(cursor);
    if (text.empty() || !text::IsKanji(text::DecodeRune(text).cp) || text.starts_with("々") || IsNumeral(FirstChar(text)) ||
        text.starts_with("第")) {
        return records;
    }
    std::vector<std::size_t> bounds{0};
    for (std::string_view s = text; !s.empty() && bounds.size() <= kMaxRun + 1;) {
        const text::Rune r = text::DecodeRune(s);
        if (!text::IsKanji(r.cp)) break;
        bounds.push_back(bounds.back() + r.size);
        s.remove_prefix(r.size);
    }
    const std::size_t n = bounds.size();
    std::size_t greedy = 0;
    for (const Record* r = records; r != nullptr; r = r->child) greedy = std::max(greedy, r->source.size());
    if (n < 4 || n > kMaxRun + 1 || greedy >= bounds.back() || !std::binary_search(bounds.begin(), bounds.end(), greedy)) {
        return records;
    }
    // ends[i]: the indexes in bounds of the ends of the pieces at bounds[i]; names: i * n + j of the
    // pieces from bounds[i] to bounds[j] that the dictionary reads only as a name (N130).
    std::vector<std::vector<std::size_t>> ends(n);
    std::vector<std::size_t> names;
    for (std::size_t i = 0; i + 1 < n; i++) {
        ends[i].push_back(i + 1);
        if (i + 2 >= n) continue;
        const dict::Candidates found = worker.WordInfo(text.substr(bounds[i]), kModeGeneral);
        for (std::size_t j = i + 2; j < n; j++) {
            bool any = false, name = true;
            for (const dict::Candidate& c : found.list) {
                if (c.source.size() != bounds[j] - bounds[i] || c.codes == nullptr) continue;
                any = true;
                name = name && c.codes->partText == "N130";
            }
            if (any) ends[i].push_back(j);
            if (any && name) names.push_back(i * n + j);
        }
    }
    // A piece costs one; one that starts with 々, or a name with 々 in it, costs two.
    auto cost = [&](std::size_t i, std::size_t j) {
        const std::string_view piece = text.substr(bounds[i], bounds[j] - bounds[i]);
        const bool name = Has(piece, "々") && std::find(names.begin(), names.end(), i * n + j) != names.end();
        return piece.starts_with("々") || name ? 2 : 1;
    };
    // fewest[i]: the fewest pieces, by cost, of a cover of the run from bounds[i].
    constexpr int kNone = 1 << 20;
    std::vector<int> fewest(n, kNone);
    fewest[n - 1] = 0;
    for (std::size_t i = n - 1; i-- > 0;) {
        for (const std::size_t j : ends[i]) fewest[i] = std::min(fewest[i], fewest[j] + cost(i, j));
    }
    std::size_t greedyEnd = 0;
    while (bounds[greedyEnd] != greedy) greedyEnd++;
    if (fewest[greedyEnd] + cost(0, greedyEnd) <= fewest[0]) return records;
    std::size_t chosen = 0;
    for (const std::size_t j : ends[0]) {
        if (fewest[j] + cost(0, j) == fewest[0]) chosen = std::max(chosen, bounds[j]);
    }
    Record* front = nullptr;
    Record* back = nullptr;
    Record** frontTail = &front;
    Record** backTail = &back;
    for (Record* r = records; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        Record**& tail = r->source.size() == chosen ? frontTail : backTail;
        *tail = r;
        tail = &r->child;
        r = next;
    }
    if (text::DecodeRune(text).size == chosen) {
        *frontTail = worker.CreateSyntheticRecord(token, text.substr(0, chosen), false);
        frontTail = &(*frontTail)->child;
    }
    if (front == nullptr) return back;
    *frontTail = back;
    return front;
}

Record* LongerNounFirst(const dict::Snapshot& snapshot, Record* records, std::string_view text, const Record* lead) {
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr) return records;
    if (lead == nullptr) lead = first;
    const bool afterNoun = NounRow(*lead);
    if (!afterNoun && (At(lead->partCode, 1) != '0' || !lead->source.ends_with("の") ||
                       !text::IsKanji(text::DecodeRune(lead->source).cp))) {
        return records;
    }
    std::string_view noun;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (r->user == nullptr && r->source.size() > lead->source.size() && r->source.size() > noun.size() &&
            r->source.starts_with(lead->source) && text.starts_with(r->source) &&
            (afterNoun ? ReadsWhole(snapshot, *r, text)
                       : At(r->partCode, 1) == '1' && NounEnd(text.substr(r->source.size())))) {
            noun = r->source;
        }
    }
    if (noun.empty()) return records;
    Record* nouns = nullptr;
    Record* rest = nullptr;
    Record** nounTail = &nouns;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (At(r->partCode, 1) == '1' && r->source == noun) {
            *nounTail = r;
            nounTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *nounTail = rest;
    if (owned == nullptr) return nouns;
    owned->child = nouns;
    return records;
}

// Text path: the noun records of records move ahead of a verb whose continuative they
// spell, the stem and one kana of the i or e row (争い, not 争 + い), or of an adjective with kanji
// whose dictionary form they spell (変な臭いが 냄새가, not 구리지만), when no record is
// longer, the word before is a modifier a verb cannot follow (の, a word in の or な, この) and
// a particle or punctuation follows the noun. Plain-dictionary records stay in front.
Record* NounAfterModifier(const Item& parent, Record* records, std::string_view text) {
    const std::string_view p = parent.group->surface;
    if (parent.detail->Yougo() ||
        (!p.ends_with("の") && !p.ends_with("な") && p != "この" && p != "その" && p != "あの" && p != "どの")) {
        return records;
    }
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr) return records;
    const bool adjective = At(first->partCode, 0) == 'H' && At(first->partCode, 1) == '7' && HasKanji(first->source);
    if (At(first->partCode, 1) != '4' && !adjective) return records;
    std::string_view noun;
    for (Record* r = first; r != nullptr; r = r->child) {
        if (!r->source.starts_with(first->source)) continue;
        const std::string_view tail = r->source.substr(first->source.size());
        if (At(r->partCode, 0) == 'N' && text::RuneCount(tail) == 1 &&
            (adjective ? tail == "い" : Has("いきしちにひみりぎじびぴえけせてねへめれげぜでべぺ", tail)) && text.starts_with(r->source) &&
            NounEnd(text.substr(r->source.size()))) {
            noun = r->source;
            break;
        }
    }
    if (noun.empty()) return records;
    for (Record* r = first; r != nullptr; r = r->child) {
        if (r->source.size() > noun.size()) return records;
    }
    Record* nouns = nullptr;
    Record* rest = nullptr;
    Record** nounTail = &nouns;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (At(r->partCode, 0) == 'N' && r->source == noun) {
            *nounTail = r;
            nounTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *nounTail = rest;
    if (owned == nullptr) return nouns;
    owned->child = nouns;
    return records;
}

// Text path: the noun records of records move ahead of an adjective with kanji whose dictionary
// form they spell, the stem and い (臭い 냄새, not 臭 + い 구린), where an adjective cannot stand: before を,
// before に that starts no word in hiragana (but には, にも), before も that starts no もの or もん, before は
// that starts no はず, and after の or な before が (臭いも減り, 形容しがたい臭いに, 血の臭いが; but 臭いものに蓋,
// 臭いにおい, 臭いはずだ, 部屋は臭いが). After a modifier NounAfterModifier moves them. Plain-dictionary
// records stay in front.
Record* NounBeforeCaseParticle(Record* records, std::string_view before, std::string_view text) {
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    // After a modifier a longer record that no hiragana follows does not read on (臭いが of 臭いがする at 血の臭いが).
    const bool modifier = before.ends_with("の") || before.ends_with("な");
    const auto readsOn = [&](const Record* r) {
        return !text.starts_with(r->source) || NameClassOf(FirstChar(text.substr(r->source.size()))) == kClassHiragana;
    };
    const Record* adjective = first;
    while (modifier && adjective != nullptr && At(adjective->partCode, 1) != '7' && !readsOn(adjective)) {
        adjective = adjective->child;
    }
    if (adjective == nullptr || At(adjective->partCode, 0) != 'H' || At(adjective->partCode, 1) != '7' ||
        !HasKanji(adjective->source)) {
        return records;
    }
    std::string noun(adjective->source);
    noun += "い";
    if (!text.starts_with(noun)) return records;
    const std::string_view after = text.substr(noun.size());
    const std::string_view ni = after.starts_with("に") ? after.substr(std::string_view("に").size()) : std::string_view();
    const bool caseNi = after.starts_with("に") &&
                        (NameClassOf(FirstChar(ni)) != kClassHiragana || ni.starts_with("は") || ni.starts_with("も"));
    const bool modifiedGa = after.starts_with("が") && modifier;
    if (adjective != first && !modifiedGa) return records;
    if (!after.starts_with("を") && !caseNi && !modifiedGa &&
        !(after.starts_with("も") && !after.starts_with("もの") && !after.starts_with("もん")) &&
        !(after.starts_with("は") && !after.starts_with("はず"))) {
        return records;
    }
    bool found = false;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (r->source.size() > noun.size() && (!modifiedGa || readsOn(r))) return records;
        found = found || (NounRow(*r) && r->source == noun);
    }
    if (!found) return records;
    Record* nouns = nullptr;
    Record* rest = nullptr;
    Record** nounTail = &nouns;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (NounRow(*r) && r->source == noun) {
            *nounTail = r;
            nounTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *nounTail = rest;
    if (owned == nullptr) return nouns;
    owned->child = nouns;
    return records;
}

// Text path: the adverb records of records move ahead of a verb of a hiragana stem whose past or
// て form they spell (たった: た + った), where no case particle, amount or noun that the verb
// would take comes right before them (年がたった頃, 東京へたったあと, 三日程たった頃) and a word
// that no ending of the verb starts follows right after them: a kanji, katakana, digit or
// bracket, or a hiragana noun (たった二人 단 두 사람, たったひとりで; but 席をたった。, たったら).
// Plain-dictionary records stay in front.
Record* AdverbOverKanaVerb(const dict::Snapshot& snapshot, Record* records, std::string_view full, int cursor) {
    const std::string_view text = From(full, cursor);
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr || !AllHiragana(first->source)) return records;
    auto isAdverb = [](const Record& r) { return r.user == nullptr && At(r.partCode, 0) == 'N' && At(r.partCode, 1) == '3'; };
    std::string_view adverb;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (!isAdverb(*r) || !AllHiragana(r->source) || !text.starts_with(r->source)) continue;
        for (const Record* v = first; v != nullptr; v = v->child) {
            if (At(v->partCode, 0) != 'M' || At(v->partCode, 1) != '4' || v->partCode.size() != 4 ||
                !r->source.starts_with(v->source) || v->source.size() >= r->source.size()) {
                continue;
            }
            const std::string_view form = r->source.substr(v->source.size());
            const int kind = v->partCode[3] - '0';
            if (form == Conjugation(kind, 6) || form == Conjugation(kind, 7)) adverb = r->source;
        }
    }
    if (adverb.empty()) return records;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (r->source.size() > adverb.size() && text.starts_with(r->source)) return records;  // たった一人
    }
    const std::string_view before = full.substr(0, static_cast<std::size_t>(cursor));
    for (const std::string_view p : {"が", "を", "へ", "ほど", "くらい", "ぐらい"}) {
        if (before.ends_with(p)) return records;
    }
    if (!before.empty()) {
        const std::string_view last = LastChar(before);
        if (HasKanji(last) || NameClassOf(last) == kClassKatakana || IsDigit(At(last, 0)) || Has("０１２３４５６７８９", last)) {
            return records;
        }
    }
    const std::string_view after = text.substr(adverb.size());
    if (after.empty() || NounEnd(after) || Has("…‥―ー～", FirstChar(after))) return records;
    if (AllHiragana(FirstChar(after)) && (KeyLengths(snapshot, after, dict::LookupMode::General, 'N') & ~uint64_t{3}) == 0) {
        return records;
    }
    Record* adverbs = nullptr;
    Record* rest = nullptr;
    Record** adverbTail = &adverbs;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (isAdverb(*r) && r->source == adverb) {
            *adverbTail = r;
            adverbTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *adverbTail = rest;
    if (owned == nullptr) return adverbs;
    owned->child = adverbs;
    return records;
}

// Text path: after a case particle or a comma, the verb records of the kana stem か (かく) whose
// past or て form the text spells move ahead of the other records: no particle か follows there
// (汗もかいた 땀도 흘렸다, 手紙をかいて 편지를 써서; but 何匹かいた, 誰かいた after a noun). A word that
// the text starts with and that spells the form or more keeps its place.
// Plain-dictionary records stay in front.
Record* KanaVerbAfterParticle(const Item& parent, Record* records, std::string_view text) {
    const std::string_view p = parent.group->surface;
    if (p != "を" && p != "も" && p != "は" && p != "が" && p != "に" && p != "で" && p != "、") return records;
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr) return records;
    auto spells = [&](const Record& v) {
        if (At(v.partCode, 0) != 'M' || At(v.partCode, 1) != '4' || v.partCode.size() != 4 || v.source != "か") {
            return std::string_view();
        }
        const int kind = v.partCode[3] - '0';
        for (const int branch : {6, 7}) {
            const std::string_view form = Conjugation(kind, branch);
            if (!form.empty() && text.starts_with(v.source) && text.substr(v.source.size()).starts_with(form)) {
                return text.substr(0, v.source.size() + form.size());
            }
        }
        return std::string_view();
    };
    std::string_view verb;
    for (const Record* r = first; r != nullptr && verb.empty(); r = r->child) verb = spells(*r);
    if (verb.empty() || !spells(*first).empty()) return records;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (!text.starts_with(r->source) || !spells(*r).empty()) continue;
        if (r->source.size() > verb.size() || (r->source.size() == verb.size() && !AnyOf("PSTU", At(r->partCode, 0)))) {
            return records;
        }
    }
    Record* verbs = nullptr;
    Record* rest = nullptr;
    Record** verbTail = &verbs;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (!spells(*r).empty()) {
            *verbTail = r;
            verbTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *verbTail = rest;
    if (owned == nullptr) return verbs;
    owned->child = verbs;
    return records;
}

// Text path: the verb records of records move ahead of a longer record that is no verb but
// spells a form of the verb before a polite ます (喜び + ます: 喜ぶ, not the noun 喜び; 急ぎます,
// 頑張れます): the verb's stem and its continuative or its potential stem, and no record longer.
// Plain-dictionary records stay in front.
// An ichidan verb whose stem a noun spells (面倒見 돌보기, 面倒見る 돌보다) is the verb before an
// ending a noun does not take: る, た, て, ない, なさい, よう, ろ, ます, られ (面倒見てた 돌보고 있었다,
// 面倒見る 돌본다, 面倒見なさい 돌봐), while 面倒見が, 面倒見も, 面倒見だ stay with the noun. Only a kanji key of two characters or more, which the
// noun leads (spec GRM-3921).
Record* VerbBeforeTeForm(Record* records, std::string_view text) {
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr) return records;
    // The noun of the longest key: no record spells more of the text (改めて stays the adverb).
    const Record* noun = nullptr;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (noun == nullptr || r->source.size() > noun->source.size()) noun = r;
    }
    if (noun == nullptr || At(noun->partCode, 1) != '1' || !text.starts_with(noun->source)) return records;
    const std::string_view key = noun->source;
    if (text::RuneCount(key) < 2 || !HasKanji(key)) return records;
    const std::string_view after = text.substr(key.size());
    bool form = false;
    for (const std::string_view f : {"る", "た", "て", "ない", "なか", "なき", "なく", "なけ", "なさ", "よう", "ろ", "ます", "られ"}) {
        form = form || after.starts_with(f);
    }
    // Before るに the plain verb has no reading of its own yet (裏付けるに 증명하는에), so the noun stays.
    if (!form || after.starts_with("るに")) return records;
    bool verb = false;
    for (const Record* r = first; r != nullptr; r = r->child) {
        verb = verb || (r->source == key && r->partCode.size() == 4 && At(r->partCode, 1) == '4' && r->partCode[3] == '7');
    }
    if (!verb) return records;
    Record* verbs = nullptr;
    Record* rest = nullptr;
    Record** verbTail = &verbs;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (r->source == key && r->partCode.size() == 4 && At(r->partCode, 1) == '4' && r->partCode[3] == '7') {
            *verbTail = r;
            verbTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *verbTail = rest;
    if (owned == nullptr) return verbs;
    owned->child = verbs;
    return records;
}

Record* VerbBeforePolite(Record* records, std::string_view text) {
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr || At(first->partCode, 1) == '4' || !text.starts_with(first->source)) return records;
    if (dict::StartsWithMember(HDTRANS_CLASS("follower.polite-masu"), text.substr(first->source.size())) == 0) return records;
    bool spelled = false;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (r->source.size() > first->source.size()) return records;
        if (At(r->partCode, 1) != '4' || r->partCode.size() != 4 || !first->source.starts_with(r->source)) continue;
        const std::string_view form = first->source.substr(r->source.size());
        const int kind = r->partCode[3] - '0';
        spelled = spelled || (!form.empty() && (form == Conjugation(kind, 10) || form == Conjugation(kind, 4)));
    }
    if (!spelled) return records;
    Record* verbs = nullptr;
    Record* rest = nullptr;
    Record** verbTail = &verbs;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (At(r->partCode, 1) == '4') {
            *verbTail = r;
            verbTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *verbTail = rest;
    if (owned == nullptr) return verbs;
    owned->child = verbs;
    return records;
}

// A degree suffix after an adjective stem or a verb's continuative cannot take
// adnominal な from the verb's ない or なければ. Complete finite endings stay available.
bool DegreeNegativeAdnominal(const Item& parent, const Item& candidate, std::string_view source) {
    const Detail& detail = *parent.detail;
    const bool degreeStem = (detail.cls == kDetailKeiyo && detail.kind == 'x') ||
                            (detail.cls == kDetailDakeiyo && detail.kind == 'X') ||
                            (detail.cls == kDetailVerb && detail.kind == 10 && parent.DirectLength() == 0);
    if (!degreeStem || candidate.detail->cls != kDetailDakeiyo || candidate.group->entries.front()->user != nullptr ||
        candidate.DirectSurface() != "な" ||
        !dict::InClass(HDTRANS_CLASS("suffix.continuative"), candidate.group->surface)) {
        return false;
    }
    const std::string_view ending = source.substr(candidate.group->surface.size());
    if (!ending.starts_with("ない") && !ending.starts_with("なければ")) return false;
    bool adnominal = false;
    for (int i = 0; i < candidate.DirectCount(); i++) {
        if (!candidate.DirectEnabled(i)) continue;
        const std::string_view target = candidate.DirectText(i);
        if (target != "4" && !target.starts_with("4 ")) return false;
        adnominal = true;
    }
    if (!adnominal) return false;
    for (const Record* other = candidate.group->records; other != nullptr; other = other->child) {
        if (other->source == candidate.group->surface && At(other->partCode, 1) == '4') return true;
    }
    return false;
}

// Rejects some kind-1 records next to level-2 kanji and the characters after them in
// the code order (Shift-JIS lead byte 0xE0 or above); kinds 0, 1, 2, 3 and 9 are
// otherwise valid, others never.
bool ValidateCandidate(std::string_view source, int start, int token, const Record& record, const Item* previous) {
    const std::string_view surface = record.source;
    const std::string_view prefix = source.substr(0, static_cast<std::size_t>(start));
    if (surface.size() + static_cast<std::size_t>(start) > source.size()) return false;
    const std::string_view tail = From(source, start + static_cast<long>(surface.size()));
    switch (At(record.partCode, 1)) {
    case '0':
    case '2':
    case '3':
    case '9':
        return true;
    case '1':
        break;
    default:
        return false;
    }
    auto highLeadPair = [](std::string_view s) {
        const text::Rune r = text::DecodeRune(s);
        return r.size == s.size() && tokenizer::IsSjisHighLead(r.cp);
    };
    if (!highLeadPair(surface) || WidthOf(surface) != 2) return true;
    const Window l = Right(prefix, 2), r = Left(tail, 2);
    if (prefix.ends_with("□") || tail.starts_with("□")) return false;
    auto hanja = [&](const Window& s) { return !s.broken && WidthOf(s.text) == 2 && highLeadPair(s.text); };
    if (!hanja(l) && !hanja(r) && !(l == "○")) return true;
    if (previous != nullptr && previous->tokenIndex == token && previous->start + previous->length >= start) return true;
    return !l.broken && WidthOf(l.text) == 2 && Has(kNumeralOrder, l.text);
}

// The canonical form of r cut to two width units; a form shorter than that ends the
// filtered text after it (cut).
std::string_view CanonicalPair(char32_t r, std::string& scratch, bool& cut) {
    scratch.clear();
    if (const auto form = tokenizer::CanonicalForm(r)) scratch.assign(*form);
    else text::AppendRune(scratch, r);
    std::size_t i = 0;
    int w = 0;
    while (i < scratch.size()) {
        const text::Rune c = text::DecodeRune(std::string_view(scratch).substr(i));
        if (w + Width(c.cp) > 2) break;
        w += Width(c.cp);
        i += c.size;
    }
    scratch.resize(i);
    cut = WidthOf(scratch) < 2;
    return scratch;
}

}  // namespace

bool FractionAt(std::string_view text, std::size_t size) {
    if (size > text.size()) return false;
    const std::string_view after = text.substr(size);
    for (const std::string_view of : dict::ClassMembers(HDTRANS_CLASS("fraction.of"))) {
        if (!after.starts_with(of) || after.size() == of.size()) continue;
        const std::string_view rest = after.substr(of.size());
        if (IsDigit(rest[0]) || dict::InClass(HDTRANS_CLASS("fraction.numerator-lead"), FirstChar(rest))) return true;
    }
    return false;
}

namespace {

// Whether a unit that numbers name (counter.unit-abbrev) follows the number of size bytes at the
// start of text, after more kanji digits written on without 十 (二水戦 2수전, 三一駆 31구): the
// number is read as digits then, and those digits read by place value.
// Whether the kanji number of size bytes at the start of text, at cursor of full, is part of a
// date: a month before a day or after a year (七月七日, 二〇二〇年一月), or a day after a month
// (一月二日). Its digits are read then, not a word that spells it (七月 칠월, 七日 이레, 二日 이틀).
// A date before an honorific of honorific.candidate or a speaker label is a name (四月一日くん).
bool DateNumber(std::string_view full, int cursor, std::string_view text, std::size_t size) {
    constexpr std::string_view kMonth = "月", kDay = "日", kYear = "年", kTen = "十";
    // Only a kanji number: digits already part from a word the dictionary spells (9月23日).
    if (size > text.size() || !(IsNumeral(FirstChar(text)) || FirstChar(text) == kTen)) return false;
    const std::string_view head = full.substr(0, static_cast<std::size_t>(cursor));
    std::string_view rest = text.substr(size);
    const auto named = [](std::string_view after) {
        return dict::StartsWithMember(HDTRANS_CLASS("honorific.candidate"), after) > 0 || after.starts_with("：") ||
               after.starts_with(":") || after.starts_with("「");
    };
    if (rest.starts_with(kDay)) return head.ends_with(kMonth) && !named(rest.substr(kDay.size()));
    if (!rest.starts_with(kMonth)) return false;
    rest.remove_prefix(kMonth.size());
    std::size_t digits = 0;
    while (IsNumeral(FirstChar(rest)) || FirstChar(rest) == kTen) {
        rest.remove_prefix(FirstChar(rest).size());
        digits++;
    }
    const bool day = digits > 0 && rest.starts_with(kDay);
    if (day && named(rest.substr(kDay.size()))) return false;
    return day || head.ends_with(kYear);
}

bool UnitAfterNumber(std::string_view text, std::size_t size) {
    if (size > text.size()) return false;
    std::string_view rest = text.substr(size);
    while (IsNumeral(FirstChar(rest))) rest.remove_prefix(FirstChar(rest).size());
    return dict::StartsWithMember(HDTRANS_CLASS("counter.unit-abbrev"), rest) > 0;
}

}  // namespace

bool NounEndAt(std::string_view text) { return NounEnd(text); }

bool VerbOrNounKey(const dict::Snapshot& snapshot, std::string_view stem, std::string_view nominal) {
    for (const dict::Candidate& c : snapshot.RetrieveRecords(stem, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.codes->part.Tag() == 'M' && c.codes->part.ClassDigit() == '4') return true;
    }
    const std::string noun = std::string(stem) + std::string(nominal);
    for (const dict::Candidate& c : snapshot.RetrieveRecords(noun, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.codes->part.ClassDigit() == '1') return true;
    }
    return false;
}

// Rewrites a synthetic target: kanji become their Hangul reading (hanjaToHangul),
// hiragana, katakana and 、 are written in their canonical forms or dropped
// (deleteJapan; 、 and ・ are always written), other characters are copied.
std::string Worker::FilterKana(std::string_view text) const {
    std::string out, scratch;
    while (!text.empty()) {
        const text::Rune d = text::DecodeRune(text);
        text.remove_prefix(d.size);
        char32_t r = d.cp;
        if (r < 0x80) {
            out += static_cast<char>(r);
            continue;
        }
        const int typ = tokenizer::Class(r);
        if (typ == tokenizer::kTypeJapanese && settings.hanjaToHangul == 1) {
            if (const auto reading = dict::Written(r)) r = *reading;
            text::AppendRune(out, r);
        } else if (typ == tokenizer::kTypeJapanese ||
                   (typ != tokenizer::kClassHiragana && typ != tokenizer::kClassKatakana && r != U'、')) {
            text::AppendRune(out, r);
        } else if (settings.deleteJapan != 1 || r == U'・' || r == U'、') {
            bool cut = false;
            out += CanonicalPair(r, scratch, cut);
            if (cut) return out;
        }
    }
    return out;
}

bool Worker::Transliterate(std::string_view s, std::string& out) {
    if (!settings.text) return TransliterateKatakana(s, out);
    if (std::string kr = snapshot.PersonName(s); !kr.empty()) {
        out = std::move(kr);
        return true;
    }
    if (std::string kr = text::FromKana(s, text::Preset::Fan); !kr.empty() && s.ends_with("ッ") && text::RuneCount(s) <= 6) {
        out = std::move(kr);
        return true;
    }
    // A particle stretched after a word spelled in katakana (セツブンガー 세츠분가) is not -er.
    for (const std::string_view particle : {"ガー", "ハー", "ナー", "カー", "サー", "ダー"}) {
        if (!s.ends_with(particle)) continue;
        const std::string_view stem = s.substr(0, s.size() - particle.size());
        std::string hiragana;
        for (std::string_view rest = stem; !rest.empty();) {
            const text::Rune r = text::DecodeRune(rest);
            text::AppendRune(hiragana, r.cp >= U'ァ' && r.cp <= U'ヶ' ? r.cp - 0x60 : r.cp);
            rest.remove_prefix(r.size);
        }
        std::string tail;
        if (text::RuneCount(stem) >= 3 && snapshot.DictionaryWord(hiragana) && TransliterateName(stem, out) &&
            TransliterateName(particle, tail)) {
            out += tail;
            return true;
        }
        break;
    }
    return TransliterateName(s, out);
}

namespace {

// Whether text is one letter or number written as a symbol (①, Ⅳ, α): a word, not a mark.
// Han characters and the marks of Japanese writing (U+3000..U+30FF: ゝ, ヾ, 〆) are not.
bool SymbolWord(std::string_view s) {
    if (s.empty()) return false;
    const char32_t r = text::DecodeRune(s).cp;
    return text::DecodeRune(s).size == s.size() && r >= 0x80 && (r < 0x3000 || r > 0x30FF) && !text::IsKanji(r) &&
           !text::IsSymbol(r) && !text::IsPunct(r) && !text::IsSpace(r);
}

// Whether text is one sign that a number before it takes as its unit (℃, ㎏, °).
bool UnitSign(std::string_view s) {
    const text::Rune r = text::DecodeRune(s);
    return !s.empty() && r.size == s.size() && r.cp >= 0x80 && (text::IsSymbol(r.cp) || text::IsPunct(r.cp)) &&
           (dict::InClassRune(HDTRANS_CLASS("sign.final-vowel"), r.cp) ||
            dict::InClassRune(HDTRANS_CLASS("sign.final-consonant"), r.cp) ||
            dict::InClassRune(HDTRANS_CLASS("sign.final-rieul"), r.cp));
}

}  // namespace

// Whether token j is Latin letters or digits (a symbol written on to one is part of its word:
// 6461π, BoyzⅡMen).
bool Worker::LatinOrDigits(int j) const {
    if (j < 0 || j >= TokenCount()) return false;
    switch (Tok(j).type) {
    case tokenizer::kTypeNumber: case tokenizer::kTypeAlpha: case 15: case 16:
        return true;
    }
    return false;
}

// Whether text is one character the loaded pre-filter writes as a mark (Settings::filterMarks).
bool Worker::FilterMark(std::string_view s) const {
    const text::Rune r = text::DecodeRune(s);
    return !s.empty() && r.size == s.size() &&
           std::binary_search(settings.filterMarks.begin(), settings.filterMarks.end(), r.cp);
}

bool Worker::Dispatch(int i, int& last) {
    current = i;
    last = i;
    const std::string_view merged = tokens.Merged(static_cast<std::size_t>(i));
    const int type = tokens.TypeAfter(static_cast<std::size_t>(i));
    const std::string_view left = tokens.Canonical(static_cast<std::size_t>(i));
    switch (type) {
    case tokenizer::kTypeNumber: case 15: case tokenizer::kTypeAlpha: case 16:
        BuildTokenTransformText(i);
        break;
    case tokenizer::kTypeAsciiClose: case tokenizer::kTypeAsciiOpen: case tokenizer::kTypeAsciiSymbol:
        if (left == "(" || left == ")" || Has(left, "%") || tokenizer::SignedNumber(left)) BuildTokenTransformText(i);
        break;
    case tokenizer::kTypeJapanese:
        if (Classify(left) == 13 && WidthOf(left) == 2 && AnchorAround(i)) BuildTokenTransformText(i);
        else BuildSurfaceTransform(i, left);
        if (const Item* tail = chain.tail) {
            for (int e = 0; e < static_cast<int>(tail->group->entries.size()); e++) {
                if (Has(tail->GAttr(e), "Wa") && i + 1 < TokenCount() && Tok(i + 1).type == tokenizer::kTypeWideSpace) {
                    spaced[static_cast<std::size_t>(i + 1)] = 1;
                    last = i + 1;
                    break;
                }
            }
        }
        break;
    case 18: case 20: case 22:
        if (left == "(" || left == ")" || left == "?" || left == "%" ||
            (type == tokenizer::kTypeSymbol && SymbolWord(merged) && !FilterMark(merged) && !LatinOrDigits(i - 1) &&
             !LatinOrDigits(i + 1)) ||
            (type == tokenizer::kTypeSymbol && left == merged && UnitSign(merged) && i > 0 &&
             (Tok(i - 1).type == tokenizer::kTypeNumber || Tok(i - 1).type == 15))) {
            BuildTokenTransformText(i);
        }
        break;
    case tokenizer::kTypeProtected:
        Insert(ProtectedItem(i));
        break;
    case tokenizer::kTypeMarkup:
        return Span(i) == 0 && ((HasPrefixFold(merged, "<LI>") && merged.size() == 4) ||
                                (HasPrefixFold(merged, "<BR>") && merged.size() == 4));
    case 3: case 6: case 9: case 17: case 23: case 26: case 78:
        if (left == "(" || left == ")" || left == "?") BuildTokenTransformText(i);
        return true;
    }
    return false;
}

// Token i sits between "<A..." and "</A>" tokens (not spans).
bool Worker::AnchorAround(int i) const {
    return i - 1 >= 0 && Span(i - 1) == 0 && HasPrefixFold(Tok(i - 1).text, "<A") && TokenCount() > i + 1 &&
           Span(i + 1) == 0 && HasPrefixFold(Tok(i + 1).text, "</A>");
}

void Worker::Insert(Item* it) {
    if (it != nullptr) insertedLength += chain.Insert(it, true);
}

void Worker::BuildTokenTransformText(int i) {
    const std::string_view surface = Tok(i).text;
    Record* record = CreateSyntheticRecord(i, surface, false);
    Insert(InsertLateCandidate(surface, 0, i, record));
}

void Worker::BuildSyntheticTransform(std::string_view surface, int token, int offset, bool emptyTarget) {
    Record* record = CreateSyntheticRecord(token, surface, emptyTarget);
    Item* item = InsertLateCandidate(transformSurface, offset, token, record);
    if (item == nullptr) {
        // The validity rules reject a synthetic word when a longer dictionary word starts
        // here, even when that longer word did not fit the sentence. The span still needs
        // an item: without one the compose stage writes its source text as it is.
        item = NewItem(NewDetail(kDetailInfo, token), NewGroup(record, ' '), transformSurface, offset, token);
    }
    Insert(item);
}

namespace {

bool IsNounSymbol(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); i++) {
        const auto c = static_cast<unsigned char>(s[i]);
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
        // Roman numerals (Ⅰ, U+2160..218F) and unit squares (㎝, U+3380..33FF).
        if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x85) return true;
        if (c == 0xE3 && i + 2 < s.size() && (static_cast<unsigned char>(s[i + 1]) == 0x8E || static_cast<unsigned char>(s[i + 1]) == 0x8F)) return true;
        if (c == 0xEF && i + 2 < s.size()) {
            const auto b = static_cast<unsigned char>(s[i + 1]), d = static_cast<unsigned char>(s[i + 2]);
            if (b == 0xBC && ((d >= 0x90 && d <= 0x99) || (d >= 0xA1 && d <= 0xBA))) return true;
            if (b == 0xBD && d >= 0x81 && d <= 0x9A) return true;
        }
    }
    for (const std::string_view m : {"%", "％", "‰", "+", "＋", "-", "－", "−", "×", "÷", "=", "＝", "°", "℃", "$", "＄",
                                     "¥", "￥", "€", "£", "#", "＃", "&", "＆", "@", "＠", "*", "＊", "/", "／"}) {
        if (s == m) return true;
    }
    return false;
}

}  // namespace

// A record for text the dictionary does not know: kana and kanji get a transliterated
// target, everything else is its own target.
Record* Worker::CreateSyntheticRecord(int token, std::string_view source, bool emptyTarget) {
    Record* r = NewRecord();
    r->source = source;
    const int typ = Classify(source);
    bool symbol = false;
    if (typ != 14 && typ != 24 && typ != 13) {
        if (WidthOf(source) == 1 && !IsDigit(source[0])) symbol = !IsAlpha(source[0]);
        r->target.assign(source);
    } else if (!emptyTarget) {
        std::string target;
        if (typ == 14) {
            if (!Transliterate(source, target)) fault = true;
            if (target == source) target.clear();
            if (Classify(target) != 0) target = FilterKana(target);
        } else if (typ == 13 && WidthOf(source) == 2 && AnchorAround(token)) {
            target = TransliterateHiragana(source);
            if (target.empty()) target = FilterKana(source);
        } else {
            target = FilterKana(source);
        }
        r->target = std::move(target);
    }
    int category = 0;
    if (typ == 24 || typ == 14 || typ == 13) category = typ;
    else if (IsDigit(At(source, 0))) category = 1;
    else if (IsAlpha(At(source, 0))) category = 2;
    if (category != 0 && symbol) r->partCode = "N600";
    else if (category != 0) r->partCode = "N110";
    // Text with no kana, kanji, digit or letter at its start (?, ？, ♪, ≫) is a mark, never a noun;
    // unit and sign symbols (%, ＋, ℃, $) and text with a digit or letter in it (+2) still stand for a noun.
    else if (IsNounSymbol(source)) r->partCode = "N110";
    else r->partCode = "N600";
    r->attr = category == 1 ? "Nd" : "Nm";
    if (category == 24 && WidthOf(source) == 2) r->attr += "On";
    return r;
}

// The dictionary record whose source is exactly source (with the records after it), or
// a numeral or punctuation record for it.
Record* Worker::LookupFallbackRecord(int token, std::string_view source, int left, int right, bool digits) {
    if (const Lookup l = digits ? Lookup{} : DoLookup(source, kModeGeneral); l.count != 0) {
        for (Record* r = l.records; r != nullptr; r = r->child) {
            if (r->source == source) return r;
        }
    }
    std::string attrs = "N";
    char second = 0;
    switch (left) {
    case 12: case 24: second = 'j'; break;
    case 13: second = 'h'; break;
    case 14: second = 'k'; break;
    case 15: second = 'd'; break;
    case 50: second = 'p'; break;
    }
    if (second != 0) {
        attrs += second;
        if (right == 15 && left != 12) attrs += 'd';
    }
    Record* r = NewRecord();
    r->source = source;
    r->target.assign(source);
    if (left == 12 && right == 15) r->target = EncodeNumericPair(source);
    switch (tokens.TypeAfter(static_cast<std::size_t>(token))) {
    case tokenizer::kTypeNumber: case tokenizer::kTypeAlpha: case 15: case 16: case tokenizer::kTypeJapanese:
        r->partCode = source == "'" || left == 50 || right == 50 ? "N600" : "N110";
        break;
    default:
        r->partCode = "NA00";
    }
    r->attr = attrs;
    if ((left == 24 || right == 24) && WidthOf(source) == 2) r->attr += "On";
    return r;
}

// A numeral (kanji or ASCII digits) or an ASCII punctuation character at source and
// its record, or null.
Record* Worker::ProbeNumericPrefix(int token, std::string_view source, bool afterMyriad) {
    if (source.empty()) return nullptr;
    std::size_t prefix = 0;
    int left = 0, right = 0;
    // The character after n bytes when it is not ASCII.
    auto next = [&](std::size_t n) {
        const std::string_view rest = source.substr(n);
        return !rest.empty() && static_cast<unsigned char>(rest[0]) >= 0x80 ? FirstChar(rest) : std::string_view();
    };
    switch (Classify(source)) {
    case tokenizer::kTypeJapanese: {
        const std::string_view c = FirstChar(source);
        // A number of two or more kanji is read as its digits even where a dictionary word
        // spells it (五十 50, 百二十 120); 百 and 千 alone keep their words (千里).
        if (settings.improvements.kanjiNumbers && !afterMyriad) {
            if (const KanjiNumber n = ParseKanjiNumber(source); text::RuneCount(source.substr(0, n.size)) >= 2) {
                Record* r = LookupFallbackRecord(token, source.substr(0, n.size), 12, 15, true);
                r->target = n.digits;
                return r;
            }
        }
        if (IsNumeral(c)) {
            left = 12, right = 15;
            prefix = c.size();
            if (const std::string_view c2 = next(c.size()); !IsNumeral(c2) && c2 == "十") {
                prefix += c2.size();
                if (IsNumeral(next(prefix))) prefix += next(prefix).size();
            }
        } else if (c == "十") {
            left = 12, right = 15;
            prefix = c.size();
            if (const std::string_view c2 = next(c.size()); IsNumeral(c2)) prefix += c2.size();
        }
        if (left == 0 && right == 0) return nullptr;
        break;
    }
    case 14:
        return nullptr;
    default: {
        const char c = source[0];
        if (static_cast<unsigned char>(c) < 0x80 && IsDigit(c)) {
            left = 15;
            std::size_t n = 1;
            while (n < source.size() && static_cast<unsigned char>(source[n]) < 0x80 &&
                   (IsDigit(source[n]) || source[n] == ',' || source[n] == '.')) {
                n++;
            }
            prefix = n;
        } else {
            if (static_cast<unsigned char>(c) >= 0x80 || !IsPunct(c)) return nullptr;
            left = 50;
            prefix = 1;
        }
    }
    }
    return LookupFallbackRecord(token, source.substr(0, prefix), left, right);
}

namespace {

// A word the search placed without reading it: a hiragana or kanji character no dictionary
// word covers.
bool Unread(const Item* it) {
    if (it->group == nullptr || it->group->entries.size() != 1) return false;
    const Entry* e = it->group->entries.front();
    const int typ = Classify(it->group->surface);
    return e->field.empty() && Has(e->gattr, "Nm") && (typ == 13 || typ == 24);
}

}  // namespace

// Splits a Japanese token into dictionary words, inserting each into the chain;
// unknown characters become synthetic words. A token the search leaves partly unread is
// searched again with the shorter endings of verbs (ProcessCandidateRecord). The second
// reading replaces the first when it reads the whole token and a verb with a shorter ending
// reads a character the first left unread.
void Worker::BuildSurfaceTransform(int token, std::string_view surface) {
    transformSurface = surface;
    resumeCursor = -1;
    Item* const mark = chain.tail;
    CoverSurface(token, surface, 0, nullptr);
    if (!settings.text || !settings.improvements.shorterEndings) return;
    // The items of this token follow the items of the tokens before it.
    auto items = [&] { return mark != nullptr ? mark->next : chain.head; };
    std::vector<const Item*> unread;
    for (const Item* it = items(); it != nullptr; it = it->next) {
        if (Unread(it)) unread.push_back(it);
    }
    if (unread.empty()) return;
    Item* const firstHead = items();
    Item* const firstTail = chain.tail;
    auto cut = [&] {
        if (mark != nullptr) mark->next = nullptr;
        else chain.head = nullptr;
        chain.tail = mark;
    };
    cut();
    transformSurface = surface;
    resumeCursor = -1;
    shorterEndings = true;
    shortenedVerbs.clear();
    CoverSurface(token, surface, 0, nullptr);
    shorterEndings = false;
    bool reads = false, whole = true;
    for (const Item* it = items(); it != nullptr; it = it->next) {
        if (Unread(it)) whole = false;
        if (std::find(shortenedVerbs.begin(), shortenedVerbs.end(), it) == shortenedVerbs.end()) continue;
        // A verb in its dictionary form ends its clause or describes the noun after it; it goes on
        // to no verb, which only its て or continuative form does (食うとる is not 食う and 取る).
        const bool bare = it->detail->group == nullptr || it->detail->group->length <= 0;
        if (bare && it->next != nullptr && At(it->next->Code(0), 0) == '4') whole = false;
        // A shorter ending gives back kana the longer one took, so the second reading counts as reading
        // more only where a shortened verb covers a hiragana the first left unread; an unread kanji is a
        // word the dictionary lacks, which no ending reaches. Either kind left unread in the second
        // reading makes it no whole reading.
        for (const Item* u : unread) {
            reads = reads || (Classify(u->group->surface) == 13 && it->start < u->start + u->length && u->start < it->start + it->length);
        }
    }
    if (reads && whole) return;
    cut();
    if (firstHead == nullptr) return;
    firstHead->prev = mark;
    if (mark != nullptr) mark->next = firstHead;
    else chain.head = firstHead;
    chain.tail = firstTail;
}

void Worker::CoverSurface(int token, std::string_view full, int cursor, Item* parent) {
    const int size = static_cast<int>(full.size());
    // Where a search that reached kMaxWordDepth stopped: the search goes on there.
    int retries = 0;
    auto resumed = [&] {
        if (resumeCursor < 0) return false;
        cursor = resumeCursor;
        parent = resumeParent;
        retries = resumeRetries;
        resumeCursor = -1;
        return true;
    };
    // Each step moves the cursor forward by at least one byte or resumes a search that
    // stopped deeper in the token, so the steps stay below the budget.
    for (long budget = 4L * size + 16; cursor < size && budget > 0; budget--) {
        insertedLength = 0;
        candidateRetryCount = retries;
        const bool translated = TranslateCandidateAt(full, cursor, token, parent);
        parent = nullptr;
        retries = 0;
        if (resumed()) continue;
        cursor += insertedLength;
        if (translated && insertedLength != 0) continue;
        if (cursor >= size) break;
        const std::string_view rest = full.substr(static_cast<std::size_t>(cursor));
        if (Record* numeric = ProbeNumericPrefix(token, rest, AfterMyriad(full, cursor))) {
            if (const Lookup l = DoLookup(rest, kModeGeneral); l.count != 0) {
                Record* cur = l.records;
                while (cur != nullptr && cur->source.size() <= numeric->source.size()) cur = cur->child;
                if (cur != nullptr) {
                    ProcessCandidateRecord(full, cursor, token, cur, nullptr);
                    if (resumed()) continue;
                    return;
                }
            }
            if (Item* candidate = InsertLateCandidate(full, cursor, token, numeric)) {
                insertedLength = chain.Insert(candidate, true);
                cursor += insertedLength;
            } else {
                cursor += static_cast<int>(FirstChar(rest).size());
            }
            continue;
        }
        const char lead = rest[0];
        if (static_cast<unsigned char>(lead) >= 0x80) {
            const std::string_view c = FirstChar(rest);
            BuildSyntheticTransform(c, token, cursor, FindHonorificPrefix(c) != -1);
            cursor += static_cast<int>(c.size());
            continue;
        }
        const int start = cursor;
        if (IsAlpha(lead)) {
            while (cursor < size && (IsAlpha(full[static_cast<std::size_t>(cursor)]) || IsDigit(full[static_cast<std::size_t>(cursor)]))) {
                cursor++;
            }
        } else {
            cursor++;
        }
        // The offset is taken after the run.
        BuildSyntheticTransform(full.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(cursor - start)), token,
                                cursor, false);
    }
}

// Whether the dictionary words at a kanji number of two or more characters only spell the
// number, alone or with one counter read with its Sino-Korean syllable (五十 오십, 十五日
// 십오일, 十二月 12월): the number is read as digits then (50, 15일, 12월). A word read
// otherwise (四十九日 사십구일재, 二十歳 20살) or a longer word (五十肩, 八百屋, 二十四時間)
// keeps its reading, and so does a word that also has a sense of its own beside the
// spelled one, where no kanji goes on after it (十八番 십팔번 and 장기: 十八番を歌う, but
// 十八番艦 18번함), and a name before an honorific (九十九さん). text is the text the words
// start.
bool Worker::SpellsNumber(Record* records, const Record& number, std::string_view text) {
    const std::string_view source = number.source;
    if (!settings.improvements.kanjiNumbers || source.empty() || static_cast<unsigned char>(source[0]) < 0x80 ||
        number.target.empty() || !IsDigit(number.target[0]) || text::RuneCount(source) < 2) {
        return false;
    }
    // A name spelled like the number before an honorific (九十九さん) is the name.
    const std::string_view after = text.substr(std::min(source.size(), text.size()));
    if (dict::StartsWithMember(HDTRANS_CLASS("honorific.candidate"), after) > 0) {
        for (const Record* r = records; r != nullptr; r = r->child) {
            if (r->source == source && (r->partCode.starts_with("N13") || r->semantic.starts_with("QPN"))) return false;
        }
    }
    const std::string reading = SinoKoreanNumber(number.target);
    bool spelled = false, own = false;
    std::size_t length = 0;
    for (Record* r = records; r != nullptr; r = r->child) {
        if (r->source.size() <= source.size()) continue;
        if (!std::string_view(r->source).starts_with(source)) return false;
        const std::string_view rest = std::string_view(r->source).substr(source.size());
        if (FirstChar(rest).size() != rest.size()) return false;
        bool counter = false;
        if (const Lookup l = DoLookup(rest, kModeGeneral); l.count != 0) {
            for (Record* c = l.records; c != nullptr && !counter; c = c->child) counter = c->source == rest && Has(c->attr, "Ct");
        }
        if (!counter) return false;
        const std::optional<char32_t> syllable = dict::Written(text::DecodeRune(rest).cp);
        if (reading.empty() || !syllable) return false;
        std::string target;
        for (const char c : std::string_view(r->target)) {
            if (c != ' ') target += c;
        }
        // A word that does not read the number (a name read 쥬하치, 十八番 장기) is no other
        // reading of it.
        constexpr std::string_view kNativeTens[] = {"열", "스물", "스무", "서른", "마흔", "쉰", "예순", "일흔", "여든", "아흔"};
        if (!target.starts_with(reading) && !target.starts_with(number.target) &&
            std::none_of(std::begin(kNativeTens), std::end(kNativeTens),
                         [&](std::string_view w) { return target.starts_with(w); })) {
            const bool name = r->partCode.starts_with("N13") || r->semantic.starts_with("QPN") ||
                              r->semantic.starts_with("QCT") || (r->user != nullptr && r->user->nameLayer);
            // An adjectival noun (十二分 충분하다) is that word before its endings に and な,
            // or where no next word is seen; before any other word it is the count (十二分で
            // 着く 12분에, あと十二分だ).
            const std::string_view next = r->source.size() < text.size() ? text.substr(r->source.size()) : "";
            const bool adjectival = r->partCode.starts_with("G8") && !next.empty() && !next.starts_with("に") &&
                                    !next.starts_with("な");
            if (!name && !target.empty() && !adjectival) {
                own = true;
                length = std::max(length, r->source.size());
            }
            continue;
        }
        std::string unit;
        text::AppendRune(unit, *syllable);
        if (target != reading + unit && target != number.target + unit) return false;
        spelled = true;
        length = std::max(length, r->source.size());
    }
    if (spelled && own && length <= text.size() &&
        (length == text.size() || !text::IsKanji(text::DecodeRune(text.substr(length)).cp))) {
        return false;
    }
    return true;
}

// Whether the dictionary words at a number of one kanji only spell it with one counter read
// with its Sino-Korean syllable (四面 사면, 一面 일면) and a follower.count member comes after
// them or the times sign before them: the words count there (四面×四面 네 면×네 면, 一面ずつ
// 한 면씩). Elsewhere they keep their reading (四面を囲まれた 사면을 둘러싸인). text is the
// text the words start in token.
bool Worker::CountBeforeFollower(Record* records, const Record& number, std::string_view text, int token) {
    const std::string_view source = number.source;
    if (!settings.improvements.kanjiNumbers || text::RuneCount(source) != 1 || number.target.empty() ||
        !IsDigit(number.target[0])) {
        return false;
    }
    const std::string reading = SinoKoreanNumber(number.target);
    std::size_t length = 0;
    for (Record* r = records; r != nullptr; r = r->child) {
        if (r->source.size() <= source.size()) continue;
        if (!std::string_view(r->source).starts_with(source)) return false;
        const std::string_view rest = std::string_view(r->source).substr(source.size());
        if (FirstChar(rest).size() != rest.size()) return false;
        // A name spelled with the count (三面 미즈라) is no other reading of it.
        if (r->partCode.starts_with("N13") || r->semantic.starts_with("QPN") || r->semantic.starts_with("QCT")) continue;
        bool counter = false;
        if (const Lookup l = DoLookup(rest, kModeGeneral); l.count != 0) {
            for (Record* c = l.records; c != nullptr && !counter; c = c->child) counter = c->source == rest && Has(c->attr, "Ct");
        }
        const std::optional<char32_t> syllable = dict::Written(text::DecodeRune(rest).cp);
        if (!counter || reading.empty() || !syllable) return false;
        std::string unit;
        text::AppendRune(unit, *syllable);
        std::string target;
        for (const char c : std::string_view(r->target)) {
            if (c != ' ') target += c;
        }
        if (target != reading + unit) return false;
        length = r->source.size();
    }
    if (length == 0 || length > text.size()) return false;
    // The times sign is a token of its own; the count after it counts too (四面×四面).
    const auto follower = [](std::string_view s) {
        return dict::StartsWithMember(HDTRANS_CLASS("follower.count"), s) > 0;
    };
    std::string_view after = text.substr(length);
    if (after.empty() && token + 1 < TokenCount()) after = tokens.Canonical(static_cast<std::size_t>(token + 1));
    const std::string_view before = token > 0 ? tokens.Canonical(static_cast<std::size_t>(token - 1)) : std::string_view();
    const bool timesBefore = dict::InClass(HDTRANS_CLASS("mark.times"), before) &&
                             text.size() == tokens.Canonical(static_cast<std::size_t>(token)).size();
    return follower(after) || timesBefore;
}

// Whether the counter of a count word that is the longest word at the number (1回 of 1回復した,
// but not of 一回転 일회전) starts a verbal noun of the dictionary that goes on past it (回復)
// and is followed by its する, a particle, a mark or nothing: the number counts the action of
// that noun (HPが1回復 1 회복), and the count word is no word there. text is the text the
// words start; a noun that goes on with a kanji or a verb stem after the count keeps the count
// (1回復活, 3回避けた).
bool Worker::VerbalNounAtCounter(Record* records, const Record& number, std::string_view text) {
    std::size_t count = 0, longest = 0;
    for (Record* r = records; r != nullptr; r = r->child) {
        longest = std::max(longest, r->source.size());
        if (r->source.size() > number.source.size() && std::string_view(r->source).starts_with(number.source) &&
            Has(r->attr, "Ct")) {
            count = std::max(count, r->source.size());
        }
    }
    if (count == 0 || count != longest ||
        count != number.source.size() + FirstChar(text.substr(number.source.size())).size()) {
        return false;
    }
    const std::string_view rest = text.substr(number.source.size());
    const Lookup l = DoLookup(rest, kModeGeneral);
    for (Record* r = l.records; r != nullptr; r = r->child) {
        if (r->source.size() + number.source.size() <= count || !Has(r->attr, "Hd") || r->partCode.empty() ||
            r->partCode[0] != 'N' || !rest.starts_with(r->source)) {
            continue;
        }
        const std::string_view after = rest.substr(r->source.size());
        const char32_t next = after.empty() ? 0 : text::DecodeRune(after).cp;
        const bool mark = next != 0 && !text::IsKanji(next) && (next < U'\u3041' || next > U'\u30FF');
        if (after.empty() || mark ||
            dict::StartsWithMember(HDTRANS_CLASS("follower.verbal-noun"), after) > 0) {
            return true;
        }
    }
    return false;
}

namespace {

// A kanji, kana or ー: a character a word unit may hold.
bool UnitRune(char32_t r) { return tokenizer::Class(r) != 0; }

// The length of the mark at the start of s that may stand between two copies (pause.copy), or 0.
std::size_t CopyPauseAt(std::string_view s) { return dict::StartsWithMember(HDTRANS_CLASS("pause.copy"), s); }

// The copies at the start of text of a unit of two to ten characters said again straight
// after it or after a pause.copy mark (いやだいやだ, 待って、待って): the bytes of the unit,
// the bytes of the mark after the first copy, the number of copies and the end of the last;
// count 0 where none is. The shortest unit wins; a unit of one character repeated (ああ) is none.
struct Copies {
    std::size_t unit = 0;
    std::size_t separator = 0;
    int count = 0;
    std::size_t end = 0;  // the end of the last copy
};

// Whether s is non-empty and only hiragana.
bool AllHiragana(std::string_view s) {
    for (std::string_view t = s; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (tokenizer::Class(r.cp) != tokenizer::kClassHiragana) return false;
        t.remove_prefix(r.size);
    }
    return !s.empty();
}

bool HasKanji(std::string_view s) {
    for (std::string_view t = s; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (text::IsKanji(r.cp)) return true;
        t.remove_prefix(r.size);
    }
    return false;
}

// Whether s is unit written two or more times (はいはい of はい).
bool SpellsCopies(std::string_view s, std::string_view unit) {
    if (unit.empty() || s.size() < 2 * unit.size() || s.size() % unit.size() != 0) return false;
    for (std::size_t at = 0; at < s.size(); at += unit.size()) {
        if (s.substr(at, unit.size()) != unit) return false;
    }
    return true;
}

// Whether text before a word ends in a copy of unit, with or without a pause.copy mark.
bool AfterCopy(std::string_view before, std::string_view unit) {
    if (before.ends_with(unit)) return true;
    for (const std::string_view p : dict::ClassMembers(HDTRANS_CLASS("pause.copy"))) {
        if (before.ends_with(p) && before.substr(0, before.size() - p.size()).ends_with(unit)) return true;
    }
    return false;
}

Copies CopiesAt(std::string_view text) {
    constexpr int kMaxUnit = 10;
    std::size_t unit = 0;
    char32_t first = 0;
    bool mixed = false;
    for (int k = 1; k <= kMaxUnit && unit < text.size(); k++) {
        const text::Rune r = text::DecodeRune(text.substr(unit));
        if (!UnitRune(r.cp)) break;
        if (k == 1) first = r.cp;
        mixed = mixed || r.cp != first;
        unit += r.size;
        if (k < 2 || !mixed) continue;
        const std::string_view u = text.substr(0, unit);
        const std::size_t separator = CopyPauseAt(text.substr(unit));
        if (!text.substr(unit + separator).starts_with(u)) continue;
        Copies c{unit, separator, 1, unit};
        for (std::size_t at = unit;;) {
            const std::size_t s = CopyPauseAt(text.substr(at));
            if (!text.substr(at + s).starts_with(u)) break;
            c.count++;
            at += s + unit;
            c.end = at;
        }
        return c;
    }
    return {};
}

struct DepthScope {
    explicit DepthScope(int& d) : depth(d) { ++depth; }
    ~DepthScope() { --depth; }
    DepthScope(const DepthScope&) = delete;
    DepthScope& operator=(const DepthScope&) = delete;
    int& depth;
};

}  // namespace

// The end of the first copy of a unit said two or more times at the start of text
// (CopiesAt), or 0: there each copy is a word or words of its own, so that an ending or
// a word of one copy does not take the start of the next (いやだ|いやだ, not いや+だい).
// A dictionary word that spells all the copies keeps them (いろいろ, はいはい); one that
// spells only some of them does not (はい|はい|はい).
std::size_t Worker::CopyEnd(std::string_view text, std::string_view before) {
    Copies c = CopiesAt(text);
    // Copies written apart are the pieces between the source spaces (もしもし もしもし, not もし four times).
    if (const std::size_t unit = CopyUnit(text); unit > c.unit && unit <= text.size()) {
        c = {unit, 0, 1, unit};
        while (text.substr(c.end).starts_with(text.substr(0, unit))) {
            c.count++;
            c.end += unit;
        }
    }
    if (c.count < 2) return 0;
    const std::string_view unit = text.substr(0, c.unit);
    const bool kanji = HasKanji(unit);
    // A unit in katakana alone said again is a sound word as a rule (テレテレ, ゴロゴロ).
    // A unit in hiragana alone starts a phrase or repeats a copy before it: inside a run of
    // words it is as often the end of one word and the start of the next (見ていないな,
    // 他にいないなんて: い+ない+な, not いな twice).
    if (!kanji) {
        if (!AllHiragana(unit)) return 0;
        if (!before.empty() && UnitRune(DecodeLastRune(before).cp) && !AfterCopy(before, unit)) return 0;
    }
    // The unit is one word: a word of the dictionary that is neither an ending nor a bare
    // stem nor a name in kana (はい, だめ; not the stem きよ of 清い), or the stem of an
    // inflected word with kana after it (いや+だ, 待+って, い+た) that is no particle (not
    // の+も of のものも); in kana alone, with one kana after the stem (not そう+やろ). A kana noun that also reads as such a stem and ending is either
    // (きた 북 or 왔다), and no unit.
    bool word = false, noun = false, bareStem = false;
    std::size_t longest = 0;
    std::vector<std::string_view> stems, particles;
    if (const Lookup l = DoLookup(text, kModeGeneral); l.count != 0) {
        for (const Record* r = l.records; r != nullptr; r = r->child) {
            longest = std::max(longest, r->source.size());
            const char part = At(r->partCode, 0);
            if (part == 'P') particles.push_back(r->source);
            if (r->source == unit) {
                const bool name = part == 'N' && At(r->partCode, 2) == '3';
                word = word || (part != 'S' && part != 'T' && part != 'M' && part != 'H' && (kanji || !name));
                noun = noun || part == 'N';
                bareStem = bareStem || part == 'M' || part == 'H';
            } else if (unit.starts_with(r->source) && !r->source.empty() && (part == 'M' || part == 'H' || part == 'G') &&
                       AllHiragana(unit.substr(r->source.size())) &&
                       (kanji || text::RuneCount(unit.substr(r->source.size())) == 1)) {
                stems.push_back(r->source);
            }
        }
    }
    bool inflected = false;
    for (const std::string_view stem : stems) inflected = inflected || std::ranges::find(particles, stem) == particles.end();
    if (!kanji && noun && inflected) return 0;
    // A unit that is also a bare stem, with kana after the last copy, is the stem of a verb
    // form that goes on there (流れ流れて, 照れ照れに), which the copy alone would misread.
    if (bareStem && tokenizer::Class(text::DecodeRune(text.substr(c.end)).cp) == tokenizer::kClassHiragana) return 0;
    if (!word && !inflected) return 0;
    // A dictionary word that spells fewer copies than are said straight (好き好き of 好き好き好き,
    // also where a copy stands right before it) is no reading of the run: each copy is read on its own
    // when the unit is a word; a unit that is only a stem (そろ of そろそろ) leaves the dictionary
    // word the first copies.
    if (longest >= 2 * c.unit + c.separator) {
        const bool fewer = longest / c.unit < static_cast<std::size_t>(c.count);
        if (c.separator != 0 || !SpellsCopies(text.substr(0, longest), unit) || !(fewer || before.ends_with(unit))) {
            return 0;
        }
        if (!word) return fewer ? longest : 0;
    }
    // The next copy is no start of a longer word that reaches past the last copy (抜け、抜け際;
    // not the stem 好き好 of 好き好む inside 好き好き好き). The separator stays with the copy before it.
    const std::size_t next = c.unit + c.separator;
    if (const Lookup l = DoLookup(text.substr(next), kModeGeneral); l.count != 0) {
        for (const Record* r = l.records; r != nullptr; r = r->child) {
            if (r->source.size() > c.unit && next + r->source.size() > c.end && !SpellsCopies(r->source, unit)) return 0;
        }
    }
    return next;
}

// Looks up the words at full[cursor:] and processes them; parent is the word before,
// whose candidates may be reordered.
bool Worker::TranslateCandidateAt(std::string_view full, int cursor, int token, Item* parent) {
    const std::string_view text = From(full, cursor);
    if (text.empty()) return true;
    // Each word of the token adds a level; a very long token takes its words in parts so
    // that the depth, and the stack, stays bounded.
    if (wordDepth >= kMaxWordDepth) {
        resumeCursor = cursor;
        resumeParent = parent;
        resumeRetries = candidateRetryCount;
        return true;
    }
    const DepthScope scope(wordDepth);
    if (const std::size_t end = settings.text ? CopyEnd(text, full.substr(0, static_cast<std::size_t>(cursor))) : 0; end != 0) {
        // Each copy is read on its own, as if the text stopped after it: the words behind
        // first, as the recursion inserts a word after the words behind it. At the depth
        // limit the copies start again from the top, where the search of a copy has room.
        if (wordDepth >= kMaxWordDepth) {
            resumeCursor = cursor;
            resumeParent = parent;
            resumeRetries = candidateRetryCount;
            return true;
        }
        const int next = cursor + static_cast<int>(end);
        if (!TranslateCandidateAt(full, next, token, nullptr)) return false;
        const std::string_view whole = transformSurface;
        const int inserted = insertedLength, retries = candidateRetryCount, resume = resumeCursor;
        Item* const resumeAfter = resumeParent;
        const int resumeAfterRetries = resumeRetries;
        transformSurface = full.substr(0, static_cast<std::size_t>(next));
        resumeCursor = -1;
        CoverSurface(token, transformSurface, cursor, parent);
        transformSurface = whole;
        insertedLength = inserted + (next - cursor);
        candidateRetryCount = retries;
        resumeCursor = resume;
        resumeParent = resumeAfter;
        resumeRetries = resumeAfterRetries;
        return true;
    }
    if (Item* hon = HonorificItem(full, cursor, token, parent)) {
        TranslateCandidateAt(full, cursor + hon->length, token, hon);
        Insert(hon);
        return true;
    }
    Lookup found;
    // 第 before a number of two or more kanji is a word of its own unless the dictionary
    // spells them together (第五十 제50, not 第五 제5 and 十).
    if (constexpr std::string_view kOrdinal = "第"; settings.improvements.kanjiNumbers && text.starts_with(kOrdinal)) {
        const std::string_view rest = text.substr(kOrdinal.size());
        if (const KanjiNumber n = ParseKanjiNumber(rest); text::RuneCount(rest.substr(0, n.size)) >= 2) {
            const std::string_view whole = text.substr(0, kOrdinal.size() + n.size);
            bool spelled = false;
            if (const Lookup words = DoLookup(text, kModeGeneral); words.count != 0) {
                for (Record* r = words.records; r != nullptr && !spelled; r = r->child) spelled = r->source == whole;
            }
            // A longer word at the number (a name) keeps the dictionary reading too.
            if (Record* number = ProbeNumericPrefix(token, rest); number != nullptr && !spelled) {
                const Lookup after = DoLookup(rest, kModeGeneral);
                spelled = after.count != 0 && !SpellsNumber(after.records, *number, rest);
            }
            if (const Lookup l = DoLookup(kOrdinal, kModeGeneral); !spelled && l.count != 0) {
                return ProcessCandidateRecord(full, cursor, token, l.records, parent);
            }
        }
    }
    if (Record* numeric = ProbeNumericPrefix(token, text, AfterMyriad(full, cursor))) {
        // Before a unit name the kanji number is its digits, not a word that starts with it or
        // the Sino-Korean reading of 一 (一航戦 1항전, 二水戦 2수전).
        if (UnitAfterNumber(text, numeric->source.size()) || DateNumber(full, cursor, text, numeric->source.size())) {
            if (text::RuneCount(numeric->source) == 1 && static_cast<unsigned char>(numeric->source[0]) >= 0x80) {
                numeric = LookupFallbackRecord(token, numeric->source, 12, 15, true);
            }
            return ProcessCandidateRecord(full, cursor, token, numeric, parent);
        }
        const Lookup dictionary = DoLookup(text, kModeGeneral);
        if (dictionary.count == 0 || SpellsNumber(dictionary.records, *numeric, text) ||
            VerbalNounAtCounter(dictionary.records, *numeric, text) || FractionAt(text, numeric->source.size()) ||
            CountBeforeFollower(dictionary.records, *numeric, text, token)) {
            return ProcessCandidateRecord(full, cursor, token, numeric, parent);
        }
        found = dictionary;
    } else {
        found = DoLookup(text, kModeGeneral);
    }
    if (found.count != 0 && settings.text) {
        found.records = DropStraySuffixes(found.records, full, cursor, token, parent);
        if (found.records == nullptr) found.count = 0;
    }
    if (found.count != 0) return ProcessCandidateRecord(full, cursor, token, found.records, parent);
    std::string_view surface = FirstChar(text);
    const bool emptyTarget = FindHonorificPrefix(surface) != -1;
    if (!emptyTarget) {
        if (const int typ = Classify(text); typ != 24 && typ != 14) {
            const char c = text[0];
            if (static_cast<unsigned char>(c) < 0x80 && (IsDigit(c) || IsAlpha(c))) surface = AsciiRun(text);
            else if (static_cast<unsigned char>(c) < 0x80) surface = text.substr(0, 1);
            else return false;
        }
    }
    return ProcessCandidateRecord(full, cursor, token, CreateSyntheticRecord(token, surface, emptyTarget), parent);
}

Record* Worker::NounBeforeCompound(Record* records, std::string_view text) {
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr) return records;
    std::string_view word;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (At(r->partCode, 1) == '1' && text.starts_with(r->source) && r->source.size() > word.size()) word = r->source;
    }
    if (word.empty()) return records;
    const text::Rune last = DecodeLastRune(word);
    const std::string_view name = word.substr(0, word.size() - last.size);
    const std::string_view after = text.substr(word.size());
    const std::string_view compound = text.substr(name.size(), last.size + text::DecodeRune(after).size);
    const bool province = !dict::InClass(HDTRANS_CLASS("noun.realm-country"), name) && dict::InClass(HDTRANS_CLASS("noun.realm-province-compound"), compound);
    if (!dict::InClass(HDTRANS_CLASS("noun.realm-name"), name) || !(province || dict::InClass(HDTRANS_CLASS("noun.realm-compound"), compound))) return records;
    for (const Record* r = DoLookup(after, kModeGeneral).records; r != nullptr; r = r->child) {
        if (after.starts_with(r->source) && text::RuneCount(r->source) > 1) return records;
    }
    // The compound as a common noun (国境 국경), not a name (国中 쿠니나카).
    bool common = false;
    for (const Record* r = DoLookup(compound, kModeGeneral).records; r != nullptr && !common; r = r->child) {
        common = r->source == compound && At(r->partCode, 1) == '1' && At(r->partCode, 2) == '1';
    }
    if (!common) return records;
    // The reading of the name that the name with 国 starts with (越中 엣추 of 越中国 엣추국), not
    // another word written the same (甲斐 보람).
    const auto realm = [&](const Record* n) {
        if (n->source != name || At(n->partCode, 1) != '1' || n->target.empty()) return false;
        for (const Record* r = first; r != nullptr; r = r->child) {
            if (r->source == word && std::string_view(r->target).starts_with(n->target)) return true;
        }
        return false;
    };
    Record* front = nullptr;
    Record* back = nullptr;
    Record** frontTail = &front;
    Record** backTail = &back;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (realm(r)) {
            *frontTail = r;
            frontTail = &r->child;
        } else {
            *backTail = r;
            backTail = &r->child;
        }
        r = next;
    }
    if (front == nullptr) {
        if (owned == nullptr) return back;
        owned->child = back;
        return records;
    }
    *frontTail = back;
    if (owned == nullptr) return front;
    owned->child = front;
    return records;
}

// Whether a small kana starts full[cursor:]: the ending ends inside a contraction (ときゃ), not
// before a word.
bool SmallKanaAt(std::string_view full, int cursor) {
    const std::string_view rest = From(full, cursor);
    return !rest.empty() && Has("ぁぃぅぇぉっゃゅょゎ", FirstChar(rest));
}

bool Worker::StandsAlone(std::string_view full, int cursor, int token, Record* records, const Item& shorter) {
    // The stem form of the first reading was checked against the ending and the text after it; the
    // shorter reading is checked the same way against what is left of it, as the text ending after
    // it: the form it reads there must be the same (傾げる before わたし is the dictionary form there
    // too; 隠せ of 隠せぬ is the negative stem only before the ぬ, and no word without it).
    const std::string_view cut = full.substr(0, static_cast<std::size_t>(cursor + shorter.length));
    const Item* alone = InsertLateCandidate(cut, cursor, token, records);
    auto ending = [](const Item& it) {
        return it.detail->group == nullptr || it.detail->group->length <= 0 ? std::string_view() : std::string_view(it.detail->group->surface);
    };
    return alone != nullptr && alone->detail->cls == kDetailVerb && alone->length == shorter.length &&
           alone->detail->kind == shorter.detail->kind && alone->detail->patternKind == shorter.detail->patternKind &&
           ending(*alone) == ending(shorter) && alone->Code(0) == shorter.Code(0);
}

bool Worker::WordAt(std::string_view full, int cursor, int token) {
    const Lookup l = DoLookup(From(full, cursor), kModeGeneral);
    return l.count != 0 && InsertLateCandidate(full, cursor, token, l.records) != nullptr;
}

// A long reading must not leave one hiragana behind it that only a one-kana noun or suffix row reads
// (本当ですか + ら + ね, にな + ら + か, だて + め + ぇ, にそい + つ + を). When a shorter reading lets the
// next word take that kana (本当 + ですからね, に + なめらか, だ + てめぇ, に + そいつ), the shorter
// reading goes first.
Record* Worker::OverStrayKana(std::string_view full, int cursor, int token, const Item& c) {
    const int next = cursor + c.length;
    const std::string_view rest = From(full, next);
    const text::Rune kana = text::DecodeRune(rest);
    // Small kana, vowels that draw a sound out (これじゃ + あ), ん, the お and ご of a polite word, the
    // ば of a condition and the た and て of a verb are not stray.
    if (kana.cp < U'ぁ' || kana.cp > U'ゖ' || Has("ぁぃぅぇぉっゃゅょゎゕゖあいうえおんごばたて", FirstChar(rest))) return nullptr;
    // The long reading ends in kana and the kana left behind runs on into a word: a kana before a
    // mark or the end is a final particle (なのよ + さ」), and a kanji reading before kana is
    // the stem of its own okurigana (生成 + り).
    const std::string_view source = From(full, cursor).substr(0, static_cast<std::size_t>(c.length));
    if (NameClassOf(LastChar(source)) != kClassHiragana) return nullptr;
    if (const int on = NameClassOf(FirstChar(rest.substr(kana.size))); on != kClassHiragana && on != kClassKatakana && on != kClassKanji) {
        return nullptr;
    }
    // An ending that the reading did not take is not a stray kana (といい + ます).
    if (KeyLengths(snapshot, rest, dict::LookupMode::VerbEnding, 'S') >> 2 != 0) return nullptr;
    auto after = [](Record* r) {
        while (r != nullptr && r->source.empty()) r = r->child;
        return r;
    };
    // The longest reading of a word at at, 0 for none; each record is tried, as the first that fits
    // is not the longest (つ before ついた).
    auto reach = [&](int at) {
        const Lookup l = DoLookup(From(full, at), kModeGeneral);
        int longest = 0;
        Record* from = l.count == 0 ? nullptr : after(l.records);
        for (int tries = 0; from != nullptr && tries < 64; tries++, from = after(from->child)) {
            const Item* w = InsertLateCandidate(full, at, token, from);
            if (w != nullptr) longest = std::max(longest, w->length);
        }
        return longest;
    };
    // The kana is no word there: its only reading is a one-kana noun or suffix after a reading that is
    // not a noun (a suffix after a noun is its own word: あいつ + ら). A kana that no row reads is left
    // as it is (調べて + き + とう).
    const Lookup words = DoLookup(rest, kModeGeneral);
    const Item* word = words.count == 0 ? nullptr : InsertLateCandidate(full, next, token, words.records);
    if (word == nullptr || word->length != static_cast<int>(kana.size) || Has(word->GAttr(0), "Nm")) return nullptr;
    auto tag = [](const Item& it) {
        const Entry* e = it.group == nullptr ? nullptr : it.group->At(0);
        return e == nullptr ? '\0' : e->cls;
    };
    const char host = tag(c);
    if (tag(*word) != 'N' && tag(*word) != 'Q') return nullptr;
    if (host == 'I' || host == 'K' || host == 'N' || host == 'Q') return nullptr;
    // わ before ね or よ is the final particle (でしたわね).
    if (kana.cp == U'わ' && (rest.substr(kana.size).starts_with("ね") || rest.substr(kana.size).starts_with("よ"))) return nullptr;
    if (reach(next) > static_cast<int>(kana.size)) return nullptr;
    // A longer reading at cursor that takes the kana is left to the readings after this one (verb
    // ついた after the adverb つい, さわられて after さわら).
    if (cursor + reach(cursor) >= next + static_cast<int>(kana.size)) return nullptr;
    // The longest shorter reading whose next word takes the kana wins. It is a word that ends before
    // the kana, not a verb whose ending runs on (こう of 乞う in こうしたら, 逸れ + と in それともう).
    Record* best = nullptr;
    int bestLength = 0;
    uint64_t tried = 0;  // the lengths whose next word was looked at
    Record* shorter = after(c.group->records);
    for (int tries = 0; shorter != nullptr && tries < 64; tries++, shorter = after(shorter->child)) {
        const Item* head = InsertLateCandidate(full, cursor, token, shorter);
        if (head == nullptr || head->detail->cls == kDetailVerb || head->length >= c.length || head->length <= bestLength ||
            head->length >= 64 || (tried >> head->length & 1) != 0) {
            continue;
        }
        tried |= uint64_t{1} << head->length;
        if (cursor + head->length + reach(cursor + head->length) >= next + static_cast<int>(kana.size)) {
            best = shorter;
            bestLength = head->length;
        }
    }
    return best;
}

// A reading that starts with a particle and runs past it (からか, とかで) yields to the
// particle alone when the particle and the word after it cover more text than the reading and the
// word after it (から + かかって, not からか + かって).
Record* Worker::OverFusedParticle(std::string_view full, int cursor, int token, const Item& c) {
    const std::string_view source = From(full, cursor).substr(0, static_cast<std::size_t>(c.length));
    static constexpr std::string_view kParticles[] = {"から", "まで", "とか", "には", "では", "とは", "にも", "でも", "に", "と", "で", "が", "を", "の", "は", "も", "へ"};
    std::string_view particle;
    for (const std::string_view p : kParticles) {
        if (source.starts_with(p) && source.size() > p.size()) { particle = p; break; }
    }
    if (particle.empty() || c.detail == nullptr || c.detail->cls == kDetailVerb) return nullptr;
    // Only a particle reading that runs on (からか, とかで), not a particle of its own (とか, でさえ) nor a
    // reading of another class (に比べ, もし, といい).
    const Entry* first = c.group == nullptr ? nullptr : c.group->At(0);
    if (first == nullptr || first->cls != 'P') return nullptr;
    // という and のこと are grammar of their own (メアリーといいます, 我が部のこと).
    // The copula in で (でした, でしょう, です) is no particle either (仰る通りでしたわね).
    if (source.starts_with("とい") || source.starts_with("のこと") || source.starts_with("もし") || source.starts_with("でし") ||
        source.starts_with("です")) {
        return nullptr;
    }
    for (const std::string_view whole : {"とか", "でさえ", "さえ", "でも", "には", "では", "とは", "にも", "ても", "とも", "から", "まで", "より", "しか", "こそ", "など", "ばかり", "だけ", "って", "とかって", "とかは", "とかも", "との", "での", "への", "からの", "までの", "にて", "にの"}) {
        if (source == whole) return nullptr;
    }
    auto after = [](Record* r) {
        while (r != nullptr && r->source.empty()) r = r->child;
        return r;
    };
    auto reach = [&](int at) {
        const Lookup l = DoLookup(From(full, at), kModeGeneral);
        int longest = 0;
        Record* from = l.count == 0 ? nullptr : after(l.records);
        for (int tries = 0; from != nullptr && tries < 64; tries++, from = after(from->child)) {
            const Item* w = InsertLateCandidate(full, at, token, from);
            if (w != nullptr) longest = std::max(longest, w->length);
        }
        return longest;
    };
    const int longCover = c.length + reach(cursor + c.length);
    Record* shorter = after(c.group->records);
    for (int tries = 0; shorter != nullptr && tries < 64; tries++, shorter = after(shorter->child)) {
        if (shorter->source != particle) continue;
        const Item* head = InsertLateCandidate(full, cursor, token, shorter);
        if (head == nullptr || head->length != static_cast<int>(particle.size())) continue;
        if (head->length + reach(cursor + head->length) > longCover) return shorter;
        return nullptr;
    }
    return nullptr;
}

// LEX-4981: a phrase reading that cuts a common noun. Where the longest reading at a point is a phrase X の Y that
// starts with kanji or katakana and whose Y is a noun of its own (次の日 of 次の日曜日, バッグの中 of バッグの中身, 家の後
// of 家の後ろ盾), and a noun that ends in kanji or katakana and holds no particle starts at its Y and runs past its end,
// reaching at least as far as the reading and the word after it, the shorter reading that ends where that noun starts
// (or whose next word does) wins. A noun whose part past the phrase is a suffix (日の出 + 前, 男の子 + 達) keeps it.
Record* Worker::OverCutWord(std::string_view full, int cursor, int token, const Item& c) {
    const std::string_view source = From(full, cursor).substr(0, static_cast<std::size_t>(c.length));
    if (c.group == nullptr || c.detail == nullptr || c.detail->cls == kDetailVerb || source.empty()) return nullptr;
    auto letter = [](char32_t r) { return text::IsKanji(r) || (r >= U'ァ' && r <= U'ヺ'); };
    const std::size_t no = source.find("の");
    const std::size_t tail = no == std::string_view::npos ? 0 : no + std::string_view("の").size();
    if (no == std::string_view::npos || no == 0 || tail >= source.size() || !letter(text::DecodeRune(source).cp)) return nullptr;
    auto after = [](Record* r) {
        while (r != nullptr && r->source.empty()) r = r->child;
        return r;
    };
    // The longest reading at at, 0 for none; noun only counts common nouns (N, I; no N13x name) that end in kanji or
    // katakana and hold no particle (中身, 日曜日, 後ろ盾; not 中で, 前の席, the name reading of 人魚姫).
    auto reach = [&](int at, bool noun) {
        const Lookup l = DoLookup(From(full, at), kModeGeneral);
        int longest = 0;
        Record* from = l.count == 0 ? nullptr : after(l.records);
        for (int tries = 0; from != nullptr && tries < 64; tries++, from = after(from->child)) {
            const Item* w = InsertLateCandidate(full, at, token, from);
            if (w == nullptr || w->length <= longest) continue;
            if (noun) {
                const std::string_view ws = From(full, at).substr(0, static_cast<std::size_t>(w->length));
                const Entry* e = w->group == nullptr ? nullptr : w->group->At(0);
                if (e == nullptr || (e->cls != 'N' && e->cls != 'I') || e->code.starts_with("13") || w->detail == nullptr || w->detail->cls != kDetailInfo ||
                    !letter(text::DecodeRune(LastChar(ws)).cp) || ws.find("の") != std::string_view::npos ||
                    ws.find("に") != std::string_view::npos || ws.find("が") != std::string_view::npos) {
                    continue;
                }
            }
            longest = w->length;
        }
        return longest;
    };
    // The Y of a phrase is a noun of its own (中, 日, 後; not the 出 of 日の出, a verb first).
    {
        const int at = cursor + static_cast<int>(tail);
        const int y = c.length - static_cast<int>(tail);
        const Lookup l = DoLookup(From(full, at), kModeGeneral);
        bool noun = false;
        Record* from = l.count == 0 ? nullptr : after(l.records);
        for (int tries = 0; from != nullptr && tries < 64 && !noun; tries++, from = after(from->child)) {
            const Item* w = InsertLateCandidate(full, at, token, from);
            if (w == nullptr || w->length != y) continue;
            const Entry* e = w->group == nullptr ? nullptr : w->group->At(0);
            if (e == nullptr) continue;
            if (e->cls != 'N' && e->cls != 'I') return nullptr;
            noun = true;
        }
        if (!noun) return nullptr;
    }
    const int end = cursor + c.length;
    const int longCover = end + reach(end, false);
    for (std::size_t off = tail; off < source.size(); off += text::DecodeRune(source.substr(off)).size) {
        const int k = cursor + static_cast<int>(off);
        const int w = reach(k, true);
        // The reading keeps a noun whose part past it is one kanji of a suffix (日の出 + 前, 男の子 + 達, 海の神 + 様; not the
        // 身 of 中身).
        const std::string_view past = k + w > end ? From(full, end).substr(0, static_cast<std::size_t>(k + w - end)) : std::string_view();
        const bool suffix = text::DecodeRune(past).size == past.size() && !past.empty() &&
                            RuneIn(text::DecodeRune(past).cp, "前後中達様君殿氏家内外上下用的性者人等方側々");
        if (k + w <= end || k + w < longCover || suffix) return nullptr;
        Record* shorter = after(c.group->records);
        for (int tries = 0; shorter != nullptr && tries < 64; tries++, shorter = after(shorter->child)) {
            const Item* head = InsertLateCandidate(full, cursor, token, shorter);
            if (head == nullptr || head->length >= c.length || cursor + head->length > k) continue;
            const int stop = cursor + head->length;
            if (stop == k || stop + reach(stop, false) == k) return shorter;
        }
        return nullptr;
    }
    return nullptr;
}

// The first base record of records that reads at full[cursor:] when a record ahead of it does
// not and a longer noun row that it starts follows it; null otherwise (the first base record leads).
const Record* Worker::ReadingLead(std::string_view full, int cursor, int token, Record* records) {
    Record* first = records;
    while (first != nullptr && first->user != nullptr) first = first->child;
    if (first == nullptr || NounRow(*first)) return nullptr;
    const std::string_view text = From(full, cursor);
    bool longerNoun = false;
    for (const Record* r = first->child; r != nullptr && !longerNoun; r = r->child) {
        longerNoun = r->user == nullptr && NounRow(*r) && r->source.size() > first->source.size() && text.starts_with(r->source);
    }
    if (!longerNoun) return nullptr;
    const Item* read = InsertLateCandidate(full, cursor, token, first);
    if (read == nullptr || read->group == nullptr) return nullptr;
    const Entry* e = read->group->At(0);
    for (const Record* r = first; r != nullptr && r != read->group->records; r = r->child) {
        if (r->source == read->group->surface && e != nullptr && At(r->partCode, 0) == e->cls && From(r->partCode, 1) == e->code) {
            return r == first ? nullptr : r;
        }
    }
    return nullptr;
}

// Whether s starts with two hiragana.
static bool TwoKanaLead(std::string_view s) {
    const std::size_t one = FirstChar(s).size();
    return one != 0 && one < s.size() && AllHiragana(s.substr(0, one + FirstChar(s.substr(one)).size()));
}

// LEX-5115: a word written in hiragana is read whole where it starts a word, not as shorter readings and the kana
// after them (しわを 주름을, not し + わ + を; かまいたちが, not か + まい + たち; たしかめて 확인해, not たしか + め +
// て). The word is a noun row that ends a word (at a particle, a mark, a copula, a character other than hiragana or
// the end), or a verb whose form reads at full[cursor:]; it starts with two hiragana and is longer than the first
// record, a verb, an adjective, an adjectival noun, an adverb or a particle. It goes first when it starts a word (the
// start of the token, after a character other than hiragana or kanji, after a particle, or after a noun of two
// characters or more written in kanji) and when the reading of the first record and the words read after it one by
// one end where the word ends, in three words or more, or in two of which one is a single kana or the first a verb's
// bare continuative, or, for a noun, no word reads on before it ends. A reading that reads as far, words that run past the end of the word, two longer words (いいこと:
// いい + こと, not いいこ + と), after a particle fewer than two content words (から + だ, not からだ), or a longer
// record (しれいかんさま) keep their place. Plain-dictionary records stay in front.
Record* Worker::KanaWordFirst(std::string_view full, int cursor, int token, Record* records, const Item* parent) {
    Record* first = records;
    while (first != nullptr && first->user != nullptr) first = first->child;
    if (first == nullptr || NounRow(*first)) return records;
    const std::string_view text = From(full, cursor);
    if (NameClassOf(FirstChar(text)) != kClassHiragana) return records;
    // One kanji before is mostly the stem of a verb or an adjective (分かんねん, 良かったんだ).
    if (const int before = cursor > 0 ? NameClassOf(LastChar(full.substr(0, static_cast<std::size_t>(cursor)))) : kClassOther;
        before == kClassHiragana || before == kClassKanji) {
        if (parent == nullptr || parent->detail == nullptr || parent->detail->cls == kDetailVerb) return records;
        if (before == kClassHiragana && !dict::InClass(HDTRANS_CLASS("particle.verb-boundary"), parent->group->surface)) {
            return records;
        }
        if (before == kClassKanji && (At(parent->Code(0), 0) != '1' || text::RuneCount(parent->group->surface) < 2)) return records;
    }
    const char lead = At(first->partCode, 1);
    if (lead != '2' && lead != '3' && lead != '4' && lead != '7' && lead != '8') return records;
    const Item* read = InsertLateCandidate(full, cursor, token, first);
    if (read == nullptr || read->detail == nullptr) return records;
    // The word: the longest noun row that ends a word, or after an adverb or adjectival noun the longest verb form.
    // A reading of that adverb or adjectival noun that runs into the verb's stem gives way to the verb (たしか + め of
    // たしかめて), unless it took an ending of its own there (にぎやかに + なって 떠들썩해져).
    const std::size_t leadSize = first->source.size();
    const bool runsOn = read->length > static_cast<int>(leadSize);
    // The same holds for an ending that follows the reading (たっぷり + と + した 듬뿍 담긴, not たっぷりとした).
    if (lead == '3' || lead == '8') {
        const std::string_view after = text.substr(runsOn ? leadSize : static_cast<std::size_t>(read->length));
        for (const std::string_view e : {"に", "な", "で", "だ", "と", "の"}) {
            if (after.starts_with(e)) return records;
        }
    }
    std::string_view word;
    char kind = 0;
    int stop = 0;
    for (Record* r = first->child; r != nullptr; r = r->child) {
        if (r->user != nullptr || !text.starts_with(r->source) || !TwoKanaLead(r->source)) continue;
        int end = 0;
        if (lead == '3' || lead == '8') {
            if (At(r->partCode, 1) != '4' || read->length > static_cast<int>(r->source.size())) continue;
            const Item* verb = InsertLateCandidate(full, cursor, token, r);
            // The verb with an ending of its own (たしかめて), not its bare stem (あらため、).
            if (verb == nullptr || verb->detail == nullptr || verb->detail->cls != kDetailVerb || verb->group->surface != r->source ||
                verb->length <= static_cast<int>(r->source.size()) ||
                NameClassOf(FirstChar(text.substr(r->source.size()))) != kClassHiragana) {
                continue;
            }
            end = cursor + verb->length;
        } else if (NounRow(*r)) {
            const std::string_view after = text.substr(r->source.size());
            if (NameClassOf(FirstChar(after)) == kClassHiragana && !NounEnd(after) && !NounGoesOn(snapshot, after)) continue;
            end = cursor + static_cast<int>(r->source.size());
        } else {
            continue;
        }
        if (end > stop) {
            word = r->source;
            kind = At(r->partCode, 1);
            stop = end;
        }
    }
    const bool verbWord = kind == '4' && runsOn;
    if (word.empty() || cursor + read->length >= stop) return records;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (r->source.size() > word.size() && text.starts_with(r->source) && cursor + static_cast<int>(r->source.size()) > stop) return records;
    }
    auto oneKana = [&](int at, int length) {
        const std::string_view piece = From(full, at).substr(0, static_cast<std::size_t>(length));
        return text::RuneCount(piece) == 1 && AllHiragana(piece);
    };
    int at = cursor + read->length;
    int words = 1;
    int content = 0;
    bool broken = false;
    bool single = lead != '2' && oneKana(cursor, read->length);
    while (at < stop) {
        if (words >= 6) return records;
        const Lookup l = DoLookup(From(full, at), kModeGeneral);
        const Item* next = l.count == 0 ? nullptr : InsertLateCandidate(full, at, token, l.records);
        if (next == nullptr || next->length <= 0) {
            broken = true;
            break;
        }
        const bool contentWord = !AnyOf("256", At(next->Code(0), 0));
        single = single || (contentWord && oneKana(at, next->length) && !From(full, at).starts_with("ん"));
        content += contentWord;
        at += next->length;
        words++;
    }
    // A verb in its bare continuative takes no noun after it (かまい + たち of かまいたち).
    const bool stem = read->detail->cls == kDetailVerb && read->detail->kind == 10 &&
                      (read->detail->group == nullptr || read->detail->group->length <= 0) && !oneKana(cursor, read->length);
    // Words that run past the end of the word by as much as the word and the word after it do are a tie, which a
    // single kana breaks for the word (しわが: し + わが, not before しわ + が).
    if (at > stop && single && !broken) {
        const Lookup l = DoLookup(From(full, stop), kModeGeneral);
        const Item* next = l.count == 0 ? nullptr : InsertLateCandidate(full, stop, token, l.records);
        if (next != nullptr && stop + next->length == at) at = stop;
    }
    if (!verbWord && !(broken && kind != '4') && (at > stop || (words < 3 && !single && !stem) || (lead == '2' && content < 2))) return records;
    Record* owned = nullptr;
    for (Record* r = records; r != first; r = r->child) owned = r;
    Record* front = nullptr;
    Record* rest = nullptr;
    Record** frontTail = &front;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (r->source == word && At(r->partCode, 1) == kind) {
            *frontTail = r;
            frontTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *frontTail = rest;
    if (owned == nullptr) return front;
    owned->child = front;
    return records;
}

// LEX-4984: at the end of the retry budget, where a word starts at cursor (no reading from before it runs past it) and
// the reading that failed is no verb, the longest reading among the records left at cursor after c's group that is
// longer than c and ends at the end of the token or where a word of two or more characters, or one that starts with
// kanji or katakana, starts; null for none.
Item* Worker::LongerAtExhaustion(std::string_view full, int cursor, int token, const Item& c) {
    // A verb that failed keeps its place: its longer forms are its own endings (なくなって, not なくなってしまうで).
    if (c.detail == nullptr || c.detail->cls == kDetailVerb) return nullptr;
    // Only where a word starts: no reading from up to four characters back runs past cursor (the て of 知って, the
    // かった of 良かった stay the ending of the word before).
    int back = cursor;
    for (int k = 0; k < 4 && back > 0; k++) {
        back -= static_cast<int>(DecodeLastRune(full.substr(0, static_cast<std::size_t>(back))).size);
        const Lookup l = DoLookup(From(full, back), kModeGeneral);
        Record* rec = l.count == 0 ? nullptr : l.records;
        for (int tries = 0; rec != nullptr && tries < 64; tries++) {
            while (rec != nullptr && rec->source.empty()) rec = rec->child;
            if (rec == nullptr) break;
            const Item* it = InsertLateCandidate(full, back, token, rec);
            if (it == nullptr || it->group == nullptr) break;
            if (back + it->length > cursor) return nullptr;
            rec = it->group->records;
        }
    }
    Item* best = nullptr;
    Record* rec = c.group == nullptr ? nullptr : c.group->records;
    for (int tries = 0; rec != nullptr && tries < 64; tries++) {
        while (rec != nullptr && rec->source.empty()) rec = rec->child;
        if (rec == nullptr) break;
        Item* it = InsertLateCandidate(full, cursor, token, rec);
        if (it == nullptr || it->group == nullptr) break;
        if (it->length > c.length && (best == nullptr || it->length > best->length)) {
            const int end = cursor + it->length;
            bool boundary = end >= static_cast<int>(full.size());
            if (!boundary) {
                const Lookup l = DoLookup(From(full, end), kModeGeneral);
                const Item* next = l.count == 0 ? nullptr : InsertLateCandidate(full, end, token, l.records);
                const char32_t lead = text::DecodeRune(From(full, end)).cp;
                boundary = next != nullptr && (text::RuneCount(From(full, end).substr(0, static_cast<std::size_t>(next->length))) >= 2 ||
                                               text::IsKanji(lead) || (lead >= U'ァ' && lead <= U'ヺ'));
            }
            if (boundary) best = it;
        }
        rec = it->group->records;
    }
    return best;
}

// Makes an item from records at full[cursor:], then translates the rest of the token
// after it; when that fails it retries with the shorter candidates.
bool Worker::ProcessCandidateRecord(std::string_view full, int cursor, int token, Record* records, Item* parent) {
    if (settings.text) records = LongerNounFirst(snapshot, records, From(full, cursor), ReadingLead(full, cursor, token, records));
    if (settings.text) records = VerdictsInPlace(records, full.substr(0, static_cast<std::size_t>(cursor)), From(full, cursor));
    if (settings.text) records = CopulaPredicateFirst(records, From(full, cursor));
    if (settings.text) records = AdjectiveOverKanaNoun(records, full.substr(0, static_cast<std::size_t>(cursor)), From(full, cursor));
    if (settings.text) records = NounBeforeCompound(records, From(full, cursor));
    if (settings.text) records = VerbBeforePolite(records, From(full, cursor));
    if (settings.text) records = VerbBeforeTeForm(records, From(full, cursor));
    if (settings.text) records = NounBeforeCaseParticle(records, full.substr(0, static_cast<std::size_t>(cursor)), From(full, cursor));
    if (settings.text) records = AdverbOverKanaVerb(snapshot, records, full, cursor);
    if (parent != nullptr) {
        records = ReorderAfterNoun(*parent, records, snapshot, From(full, cursor));
        if (settings.text) records = NounAfterModifier(*parent, records, From(full, cursor));
        if (settings.text) records = KanaVerbAfterParticle(*parent, records, From(full, cursor));
        if (settings.text) records = SuffixAfterNoun(*parent, records);
        if (settings.text) records = HonorificAfterNoun(*parent, records, snapshot);
    }
    if (settings.text) records = KanaWordFirst(full, cursor, token, records, parent);
    if (settings.text) records = KatakanaCoverFirst(*this, records, full, static_cast<std::size_t>(cursor));
    if (settings.text) records = KanjiCoverFirst(*this, records, full, static_cast<std::size_t>(cursor), token);
    const std::string_view text = From(full, cursor);
    Item* candidate = InsertLateCandidate(full, cursor, token, records);
    Record* from = records;
    while (settings.text && parent != nullptr && candidate != nullptr && DegreeNegativeAdnominal(*parent, *candidate, text)) {
        Record* remaining = candidate->group->records;
        candidate->group->records = nullptr;
        from = remaining;
        candidate = InsertLateCandidate(full, cursor, token, remaining);
    }
    if (settings.text && candidate != nullptr) {
        if (Record* shorter = OverStrayKana(full, cursor, token, *candidate)) {
            candidate->group->records = nullptr;
            return ProcessCandidateRecord(full, cursor, token, shorter, parent);
        }
        if (Record* shorter = OverFusedParticle(full, cursor, token, *candidate)) {
            candidate->group->records = nullptr;
            return ProcessCandidateRecord(full, cursor, token, shorter, parent);
        }
        if (Record* shorter = OverCutWord(full, cursor, token, *candidate)) {
            candidate->group->records = nullptr;
            return ProcessCandidateRecord(full, cursor, token, shorter, parent);
        }
    }
    if (candidate == nullptr) {
        const std::string_view pair = PairText(text);
        if (FindHonorificPrefix(pair) != -1) {
            BuildSyntheticTransform(pair, token, cursor, true);
            return true;
        }
        const int typ = Classify(text);
        if (typ == 24 || typ == 14) {
            BuildSyntheticTransform(pair, token, cursor, false);
            return true;
        }
        if (const char c = text[0]; static_cast<unsigned char>(c) < 0x80 && (IsDigit(c) || IsAlpha(c)) && typ == 0) {
            BuildSyntheticTransform(AsciiRun(text), token, cursor, false);
            return true;
        }
        if (candidateRetryCount >= 3) {
            BuildSyntheticTransform(pair, token, cursor, false);
            return true;
        }
        return false;
    }
    const int savedRetries = candidateRetryCount;
    if (settings.improvements.perLevelRetries) candidateRetryCount = 0;
    bool translated = TranslateCandidateAt(full, cursor + candidate->length, token, candidate);
    // A verb whose ending leaves text that no word reads takes the next shorter ending of
    // the same stem before another stem is tried (読むのですぞ: 読む, の, ですぞ; not 読むのです
    // and ぞ, then 読 as a noun).
    while (!translated && shorterEndings && candidate->detail->cls == kDetailVerb && candidate->detail->group != nullptr &&
           candidate->detail->group->length > 0 && !SmallKanaAt(full, cursor + candidate->length) &&
           !WordAt(full, cursor + candidate->length, token)) {
        const std::string_view longer = longerEnding;
        longerEnding = candidate->detail->group->surface;
        Item* shorter = InsertLateCandidate(full, cursor, token, from);
        longerEnding = longer;
        // Without an ending row the verb is kept only in its dictionary form, which ends a word before
        // the next one (傾げる before わたし). A bare past or て form left before kana is mostly the head
        // of a contracted ending (骨折れてんだぞ: てる and んだ, not 折れて and ん) or of another word
        // (ウェーブがかった is 掛かった, not 買った).
        const bool bare = shorter != nullptr && (shorter->detail->group == nullptr || shorter->detail->group->length <= 0);
        // A stem of one hiragana with an ending of one kana is two kana that the words around them spell
        // as well (いんだけど after a line cut in 忙し|い is not いる and ん); with a longer ending the
        // shorter reading still shows the verb (いますよ, みましょうか).
        auto twoKana = [&] {
            const std::string_view form = From(full, cursor).substr(0, static_cast<std::size_t>(shorter->length));
            const std::size_t ending = bare ? 0 : std::min(form.size(), shorter->detail->group->surface.size());
            const std::string_view stem = form.substr(0, form.size() - ending);
            return text::RuneCount(stem) == 1 && Classify(stem) == 13 && text::RuneCount(form) <= 2;
        };
        if (shorter == nullptr || twoKana() ||
            shorter->detail->cls != kDetailVerb || shorter->group->surface != candidate->group->surface ||
            shorter->EntryText(0) != candidate->EntryText(0) || shorter->Code(0) != candidate->Code(0) ||
            shorter->length >= candidate->length || (bare && shorter->detail->kind != 0) ||
            !StandsAlone(full, cursor, token, from, *shorter)) {
            break;
        }
        candidate = shorter;
        if (settings.improvements.perLevelRetries) candidateRetryCount = 0;
        translated = TranslateCandidateAt(full, cursor + candidate->length, token, candidate);
        if (translated) shortenedVerbs.push_back(candidate);
    }
    if (settings.improvements.perLevelRetries) candidateRetryCount = savedRetries;
    if (translated) {
        Insert(candidate);
        return true;
    }
    const std::string_view source = candidate->group->surface;
    const int typ = Classify(source);
    const char part = At(candidate->Code(0), 0);
    if (part == '6' || ((typ == 24 || typ == 14) && part != '4' && part != '7' && part != '8') ||
        IsDigit(At(source, 0)) || IsAlpha(At(source, 0)) || StartsWithSymbol(source)) {
        Insert(candidate);
        return true;
    }
    if (candidateRetryCount >= 3) {
        // The retries of the token are spent, so the reading that failed stands. A longer reading still left at the
        // same point that reaches the end of the token, or a point where a word of two or more characters or one not
        // in hiragana starts, takes its place (ためらう: ためら + う, not た + め + ら + う after ため and た failed).
        if (Item* longer = settings.text ? LongerAtExhaustion(full, cursor, token, *candidate) : nullptr) {
            Insert(longer);
            return true;
        }
        Insert(candidate);
        return true;
    }
    if (Record* remaining = candidate->group->records) {
        candidateRetryCount++;
        candidate->group->records = nullptr;
        while (remaining != nullptr && remaining->source.empty()) remaining = remaining->child;
        if (remaining != nullptr) return ProcessCandidateRecord(full, cursor, token, remaining, candidate);
        return false;
    }
    if (Has(candidate->GAttr(0), "Nm")) {
        Insert(candidate);
        return true;
    }
    candidateRetryCount++;
    return false;
}

// The item of a word span token: one fixed noun entry (code 110, no attributes, no
// local rule) whose target is the span's stand-in.
Item* Worker::ProtectedItem(int token) {
    const std::string_view text = tokens.Canonical(static_cast<std::size_t>(token));
    Entry* e = NewEntry();
    e->dst.assign(output::StandIn(text));
    e->code = "110";
    e->cls = 'N';
    e->ruleId = -1;
    e->fixed = true;
    Group* g = NewGroup(nullptr, ' ');
    g->surface.assign(text);
    g->length = static_cast<int>(text.size());
    g->entries.push_back(e);
    return NewItem(NewDetail(kDetailInfo, token), g, text, 0, token);
}

// The item of an honorific at full[cursor:] when the word before it, parent or (at the
// start of a token) the last item of the chain, is a name item that ends at the cursor;
// otherwise null. The item is a fixed noun (code 110) with the grammar code Tu
// (attached) or Ha (spaced).
namespace {

struct Honorific {
    std::string_view jp, kr;
    bool space;
};
constexpr Honorific kHonorifics[] = {
    {"さん", "씨", false}, {"ちゃん", "짱", false}, {"くん", "군", false},     {"君", "군", false},
    {"様", "님", false},   {"さま", "님", false},   {"氏", "씨", false},       {"殿", "님", false},
    {"たん", "짱", false}, {"先輩", "선배", true},  {"先生", "선생님", true},
};

// The nickname suffix すけ after a name (里奈すけ 리나스케) when a particle, the copula, a
// break or the end follows it: not 許すけど (許す), 透ける or すけべ.
bool NicknameSuke(std::string_view text) {
    constexpr std::string_view kSuke = "すけ";
    if (!text.starts_with(kSuke)) return false;
    const std::string_view after = text.substr(kSuke.size());
    if (after.empty()) return true;
    const char32_t next = text::DecodeRune(after).cp;
    if (next < U'ぁ' || next > U'ゖ') return next != U'ー' && !text::IsKanji(next);
    for (const std::string_view p : {"は", "が", "を", "の", "に", "へ", "と", "も", "で", "だ", "か", "よ", "ね", "って"}) {
        if (after.starts_with(p)) return true;
    }
    return false;
}

}  // namespace

bool Worker::StartsWithHonorific(std::string_view text) {
    return std::any_of(std::begin(kHonorifics), std::end(kHonorifics), [&](const Honorific& h) { return text.starts_with(h.jp); });
}

Item* Worker::WordBefore(int cursor, int token, Item* parent) const {
    if (parent != nullptr) return parent;
    Item* tail = chain.tail;
    if (tail != nullptr && ((tail->tokenIndex == token && tail->start + tail->length == cursor) ||
                            (cursor == 0 && tail->tokenIndex == token - 1 &&
                             tokens[static_cast<std::size_t>(tail->tokenIndex)].type == tokenizer::kTypeProtected))) {
        return tail;
    }
    return nullptr;
}

Item* Worker::HonorificItem(std::string_view full, int cursor, int token, Item* parent) {
    Item* name = WordBefore(cursor, token, parent);
    if (name == nullptr) return nullptr;
    const std::string_view text = From(full, cursor);
    // The nickname すけ also follows a person name of the base dictionary (里奈すけ).
    const bool person = !name->group->entries.empty() && std::string_view(name->group->entries[0]->attr).starts_with("QPN");
    // A katakana name of the name layer takes ちゃん as the honorific before と too (アリスちゃんと
    // 앨리스짱과, not the adverb ちゃんと), unless a dictionary gives it a common sense other than a
    // person or its sound as names are written (マスクちゃんと 마스크 제대로).
    const auto katakanaPerson = [&] {
        const std::string_view surface = name->group->surface;
        if (!text.starts_with("ちゃん") || NameClassOf(LastChar(surface)) != kClassKatakana) return false;
        if (!snapshot.NamePerson(surface)) return false;
        for (const dict::Candidate& c : snapshot.RetrieveRecords(surface, dict::LookupMode::General)) {
            if (c.codes == nullptr || c.source != surface || c.user != nullptr) continue;
            if (c.codes->sem.HasCode("QPN") || c.codes->sem.HasCode("VHn") || c.codes->part.ProperNoun()) continue;
            if (!text::SpellsSound(surface, c.target, false)) return false;
        }
        return true;
    };
    if (!name->IsNameItem() && !(person && NicknameSuke(text)) && !katakanaPerson()) return nullptr;
    const bool titleWord = dict::StartsWithMember(HDTRANS_CLASS("noun.title-word"), text) > 0;
    std::string_view key, kr;
    bool space = false;
    for (const Honorific& h : kHonorifics) {
        if (h.jp.empty() || h.jp.size() <= key.size() || !text.starts_with(h.jp) || titleWord) continue;
        key = h.jp, kr = h.kr, space = h.space;
    }
    if (key.empty() && !titleWord && NicknameSuke(text)) key = "すけ", kr = "스케";
    if (lookupContext.plain != nullptr) {
        if (const dict::UserEntry* e = lookupContext.plain->Honorific(text); e != nullptr && e->key.size() >= key.size()) {
            key = e->key, kr = e->target, space = e->codes->gram.Contains("Ha");
        }
    }
    if (key.empty()) return nullptr;
    const std::string_view nameText = name->group->entries[0]->dst;
    if (dict::EndsWithMember(HDTRANS_CLASS("ko.title-end"), nameText) > 0) kr = {};
    Entry* e = NewEntry();
    e->dst.assign(kr);
    e->code = "110";
    e->attr = "VHn";
    e->gattr = space ? "Ha" : "Tu";
    e->cls = 'N';
    e->ruleId = -1;
    e->fixed = true;
    Group* g = NewGroup(nullptr, ' ');
    g->surface.assign(key);
    g->length = static_cast<int>(key.size());
    g->entries.push_back(e);
    return NewItem(NewDetail(kDetailInfo, token), g, full, cursor, token);
}

// Whether the godan e-row form that record matched before 、 also spells an ichidan stem of its
// last kanji (粉々に砕け、 is 砕ける, 懐に入れ、 is 入れる), which the shorter reading takes.
bool Worker::IchidanBeforeComma(const Detail& d, std::string_view text, int offset, const Record* record) const {
    if (record == nullptr || (d.kind != 4 && d.kind != 11) || d.commaEForm == 0) return false;
    const std::string_view entry = Conjugation(d.patternKind, d.kind);
    const std::string_view tail = From(text, offset + static_cast<long>(record->source.size()));
    if (!tail.starts_with(entry)) return false;
    const std::string_view after = tail.substr(entry.size());
    if (!after.starts_with(kJaFullComma) && !after.starts_with(kJaComma)) return false;
    const std::string stem = std::string(LastChar(record->source)) + std::string(entry);
    for (const dict::Candidate& c : snapshot.RetrieveRecords(stem, dict::LookupMode::General)) {
        const std::string_view pc = c.codes->partText;
        if (pc.size() == 4 && pc[0] == 'M' && pc[1] == '4' && pc[3] == '7') return true;
    }
    return false;
}

// An item from the first usable record group of records at text[offset:], running the
// conjugation checks of its kind: verbs ('4'), adjectives ('7'), adjectival nouns
// ('8'), words without ending ('6') and the rest. Null when no record group passes.
Item* Worker::InsertLateCandidate(std::string_view text, int offset, int token, Record* records) {
    Record* rec = records;
    while (rec != nullptr) {
        if (rec->source.empty() || rec->partCode.size() != 4 || rec->partCode[1] == '5') {
            rec = rec->child;
            continue;
        }
        const std::size_t length = rec->source.size();
        const char kind = rec->partCode[1];
        auto sameGroup = [&](const Record* r) { return r->source.size() == length && At(r->partCode, 1) == kind; };
        switch (kind) {
        case '4': {
            Detail* word = NewDetail(kDetailVerb, token);
            const int pattern = rec->partCode[3] - '0';
            word->patternKind = pattern;
            word->dialect = settings.text && settings.improvements.dialectForms;
            Record* stop = nullptr;
            const bool ok = MatchConjugation(*word, text, offset, rec, stop) && !IchidanBeforeComma(*word, text, offset, stop);
            rec = stop;
            if (!ok) {
                while (rec != nullptr && sameGroup(rec) && At(rec->partCode, 3) - '0' == pattern) {
                    rec = rec->child;
                }
            } else {
                Group* group = NewGroup(rec, static_cast<char>(word->kind + '0'));
                Item* candidate = NewItem(word, group, text, offset, token);
                bool accepted = candidate->AcceptVerbContinuation(group->records);
                if (accepted && candidate->group->surface == kJaShi && candidate->EntryText(0) == "&HT@" &&
                    candidate->DirectSurface() == PrefixGate(14) &&
                    !From(text, offset + candidate->length).starts_with(PrefixGate(14))) {
                    accepted = false;
                }
                if (accepted) {
                    switch (word->record) {
                    case 1: candidate->AppendRuleCandidateMarker("Pi"); break;
                    case 2: candidate->AppendRuleCandidateMarker("Sa"); break;
                    case 4: candidate->AppendRuleCandidateMarker("Ka"); break;
                    }
                    return candidate;
                }
                // A く verb of the same stem reads the て or た that the stem in い of a わ-row
                // verb cannot take (彷徨いて: 彷徨く in て, not 彷徨う + て).
                Record* other = stop->child;
                while (other != nullptr && other->source == stop->source && At(other->partCode, 1) == '4' &&
                       At(other->partCode, 3) == At(stop->partCode, 3)) {
                    other = other->child;
                }
                const std::string_view afterStem = From(text, offset + static_cast<long>(stop->source.size()));
                if (word->patternKind == 0 && word->kind == 10 && afterStem.starts_with("い") && other != nullptr &&
                    other->source == stop->source && At(other->partCode, 1) == '4' &&
                    TeAfterWaStem(stop->source, afterStem.substr(std::string_view("い").size()))) {
                    rec = other;
                    group->records = nullptr;
                    continue;
                }
                rec = group->records;
                group->records = nullptr;
            }
            if (rec == nullptr || At(rec->partCode, 1) != '4') {
                if (Item* candidate = QueryPrefixTable(text, offset, token)) {
                    candidate->group->records = rec;
                    return candidate;
                }
            }
            break;
        }
        case '6': {
            Detail* word = NewDetail(kDetailInfo, token);
            return NewItem(word, NewGroup(rec, ' '), text, offset, token);
        }
        case '7':
        case '8': {
            Detail* word = NewDetail(kind == '8' ? kDetailDakeiyo : kDetailKeiyo, token);
            const std::string_view suffix = From(text, offset + static_cast<long>(length));
            word->kind = '0';
            if (suffix.starts_with(kJaSugiKanji) || suffix.starts_with(kJaSugi) || suffix.starts_with(kJaSaSugiKanji) ||
                suffix.starts_with(kJaSaSugi)) {
                word->kind = kind == '8' ? 'X' : 'x';
            }
            Group* group = NewGroup(rec, ' ');
            Item* candidate = NewItem(word, group, text, offset, token);
            if ((kind == '7' && candidate->RunKeiyoTrieQuery()) || (kind == '8' && candidate->RunDakeiyoCase8Query())) {
                return candidate;
            }
            rec = group->records;
            group->records = nullptr;
            break;
        }
        default: {
            Detail* word = NewDetail(kDetailInfo, token);
            if (!ValidateCandidate(text, offset, token, *rec, chain.tail)) {
                while (rec != nullptr && sameGroup(rec)) rec = rec->child;
                continue;
            }
            Group* group = NewGroup(rec, ' ');
            Item* candidate = NewItem(word, group, text, offset, token);
            if (candidate->RunDefaultTrieQuery()) return candidate;
            rec = group->records;
            group->records = nullptr;
        }
        }
    }
    return nullptr;
}

namespace {

// Rejects some prefix-table matches by what follows them; it may move index to branch
// 8 or 10.
bool CheckPrefixTableGate(int group, int& index, std::string_view token, std::string_view sourceTail) {
    auto g = [](int i) { return PrefixGate(i); };
    bool rejected = false;
    if (group == 1 && index == 6 && WidthOf(sourceTail) >= 4) {
        if (const Window last = Right(sourceTail, 4); last == g(11) || last == g(12) || last == g(13)) rejected = true;
    }
    const Window two = Cut(token, 2, 2);
    if (!two.Empty()) {
        if (rejected) return true;
        if ((group == 1 || group == 2) && index == 8) {
            const Window four = Cut(token, 2, 4), six = Cut(token, 2, 6), eight = Cut(token, 2, 8);
            if (!(two == g(15))) {
                if (two == g(14)) {
                    index = 8;
                    return false;
                }
                if (two == g(16) || two == g(17) || six == g(18) || six == g(19) || six == g(20) || four == g(21) ||
                    four == g(22) || two == g(23) || four == g(24) || two == g(25) || four == g(26) || four == g(27) ||
                    eight == g(28) || four == g(29) || four == g(30) || eight == g(31) || four == g(32)) {
                    index = 10;
                    return false;
                }
                if (!(Left(token, 2) == kJaShi)) rejected = true;
            }
        }
    }
    if (!rejected && (group == 1 || group == 2) && index == 10 && !(two == ",") && !(two == g(33)) && !(two == g(16)) &&
        !(two == g(17))) {
        rejected = true;
    }
    return rejected;
}

}  // namespace

// A conjugated する, くる or 来る at text[offset:] as a verb item.
Item* Worker::QueryPrefixTable(std::string_view text, int offset, int token) {
    if (offset > static_cast<int>(text.size())) return nullptr;
    const std::string_view tail = From(text, offset);
    // The n width units after byte off of tail, "" when fewer are left.
    auto tailAt = [&](std::size_t off, int n) {
        const std::string_view rest = tail.substr(off);
        return WidthOf(rest) >= n ? Left(rest, n) : Window{};
    };
    static constexpr int kKinds[] = {0, 9, 11, 13};
    // The と form of する, くる or 来る before おる (切ってきとる, 勉強しとった), as of other verbs (MatchKind):
    // their て form with と for its て.
    for (int tableGroup = 1; settings.text && settings.improvements.dialectForms && tableGroup < 4; tableGroup++) {
        const std::string_view te = PrefixForm(50 * tableGroup + 7);
        const std::string to = std::string(te.substr(0, te.size() - kJaTe.size())) + std::string(kJaTo);
        if (!tail.starts_with(to)) continue;
        const Lookup found = DoLookup(PrefixForm(50 * tableGroup), kModePrefixVerb);
        if (found.count == 0) continue;
        Detail* verb = NewDetail(kDetailVerb, token);
        verb->patternKind = kKinds[tableGroup];
        verb->patternMatched = 1;
        verb->dialect = true;
        verb->patternLength = static_cast<int>(to.size());
        Group* group = NewGroup(found.records, static_cast<char>(kBranchTo + '0'));
        Item* candidate = NewItem(verb, group, text, offset, token);
        verb->kind = kBranchTo;
        if (candidate->AcceptVerbContinuation(group->records)) return candidate;
    }
    for (int tableGroup = 1; tableGroup < 4; tableGroup++) {
        for (int entry = 0; entry < kConjugationBranches; entry++) {
            const std::string_view prefix = PrefixForm(50 * tableGroup + entry);
            const bool contractedConditional = tableGroup == 1 && entry == 3 && tail.starts_with("すりゃ");
            if (prefix.empty() || (!tail.starts_with(prefix) && !contractedConditional)) continue;
            const std::size_t pl = prefix.size();
            // The く of くらい or くせに after the past (したくらい, 来たくせに) is not the desire.
            const std::string_view afterPrefix = tail.substr(pl);
            const bool kuWord = afterPrefix.starts_with(kJaKurai) || afterPrefix.starts_with(kJaKuseni);
            if (tableGroup == 1) {
                if (entry == 6) {
                    const Window two = tailAt(pl, 2), six = tailAt(pl, 6);
                    if (two == PrefixGate(0) || six == PrefixGate(1) || (two == PrefixGate(2) && !kuWord) ||
                        six == PrefixGate(3) || two == PrefixGate(4) || six == kJaOwaru) {
                        continue;
                    }
                } else if (entry == 8 || entry == 10) {
                    if (tailAt(0, 6) == PrefixForm(67)) entry = 17;
                    else if (tailAt(0, 4) == PrefixForm(61)) entry = 11;
                }
            } else if (tableGroup == 3 && entry == 6) {
                // As for する: 来た before い, かった, く, ければ or き is the 来たい family, not the past.
                const Window two = tailAt(pl, 2), six = tailAt(pl, 6);
                if (two == PrefixGate(0) || six == PrefixGate(1) || (two == PrefixGate(2) && !kuWord) ||
                    six == PrefixGate(3) || two == PrefixGate(4)) {
                    continue;
                }
            } else if (tableGroup == 3 && (entry == 8 || entry == 10) && tail.starts_with(PrefixForm(50 * 3 + 17))) {
                entry = 17;  // as for する: 来ちゃ is one form, not 来 and ちゃ
            } else if (tableGroup == 3 && (entry == 8 || entry == 10)) {
                const Window two = tailAt(pl, 2), four = tailAt(pl, 4), eight = tailAt(pl, 8), ten = tailAt(pl, 10);
                if (two == PrefixGate(0) || two == PrefixGate(5) || four == PrefixGate(6) || four == PrefixGate(7) ||
                    eight == PrefixGate(8) || eight == PrefixGate(9) || ten == PrefixGate(10)) {
                    continue;
                }
            }
            int index = entry;
            if (CheckPrefixTableGate(tableGroup, index, tail, text.substr(0, static_cast<std::size_t>(offset)))) continue;
            const Lookup found = DoLookup(PrefixForm(50 * tableGroup), kModePrefixVerb);
            if (found.count == 0) continue;
            Detail* verb = NewDetail(kDetailVerb, token);
            verb->patternKind = kKinds[tableGroup];
            verb->patternMatched = 1;
            verb->dialect = settings.text && settings.improvements.dialectForms;
            verb->patternLength = static_cast<int>(PrefixForm(50 * tableGroup + entry).size());
            Group* group = NewGroup(found.records, static_cast<char>(verb->kind + '0'));
            Item* candidate = NewItem(verb, group, text, offset, token);
            verb->kind = index;
            if (candidate->AcceptVerbContinuation(group->records)) return candidate;
        }
    }
    return nullptr;
}

}  // namespace hdTrans::word
