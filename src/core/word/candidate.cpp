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

// Whether every particle among records is a single character.
bool OneKanaParticles(const Record* records) {
    for (const Record* r = records; r != nullptr; r = r->child) {
        if (At(r->partCode, 1) == '2' && text::RuneCount(r->source) > 1) return false;
    }
    return true;
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
            // of the adjective as a word (水着かわいい, not か + わ + いい; but それはかなわない).
            if (k == '7' && text::RuneCount(p->source) >= 3 && AllHiragana(p->source) && OneKanaParticles(first)) {
                const std::string_view rest = text.substr(text::DecodeRune(text).size);
                if (text::RuneCount(rest.substr(0, snapshot.WordAt(rest))) < 2) break;
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

// Text that starts with a particle or punctuation, or is empty.
bool NounEnd(std::string_view text) {
    if (text.empty()) return true;
    const std::string_view c = FirstChar(text);
    return text::DecodeRune(c).cp != text::kReplacement && Has("にがをはもでとへの、。，．！？」』）…", c);
}

// Whether before, the word ahead of a suffix, is a name or a noun of the dictionary with a sense
// of persons or animals among its candidates, those of its group or the other records of its surface
// (モモ 복숭아 beside the name モモ); not itself a suffix written attached (ら, たち).
bool SuffixHost(const Item* before) {
    if (before == nullptr) return false;
    if (before->IsNameItem()) return true;
    auto person = [](std::string_view code, std::string_view sem, std::string_view gram) {
        return code.starts_with('1') && (Has(sem, "VHn") || Has(sem, "QPN") || Has(sem, "QAA")) && !Has(gram, "Tu");
    };
    for (const Entry* e : before->group->entries) {
        if (person(e->code, e->attr, e->gattr)) return true;
    }
    for (const Record* r = before->group->records; r != nullptr; r = r->child) {
        if (r->source == before->group->surface && person(From(r->partCode, 1), r->semantic, r->attr)) {
            return true;
        }
    }
    return false;
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
    const bool hosted = !covered && SuffixHost(before);
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
// Plain-dictionary records stay in front.
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

Record* LongerNounFirst(const dict::Snapshot& snapshot, Record* records, std::string_view text) {
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr) return records;
    const bool afterNoun = NounRow(*first);
    if (!afterNoun && (At(first->partCode, 1) != '0' || !first->source.ends_with("の") ||
                       !text::IsKanji(text::DecodeRune(first->source).cp))) {
        return records;
    }
    std::string_view noun;
    for (const Record* r = first; r != nullptr; r = r->child) {
        if (r->user == nullptr && r->source.size() > first->source.size() && r->source.size() > noun.size() &&
            r->source.starts_with(first->source) && text.starts_with(r->source) &&
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
// spell, the stem and one kana of the i or e row (争い, not 争 + い), when no record is
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
    if (first == nullptr || At(first->partCode, 1) != '4') return records;
    std::string_view noun;
    for (Record* r = first; r != nullptr; r = r->child) {
        if (!r->source.starts_with(first->source)) continue;
        const std::string_view tail = r->source.substr(first->source.size());
        if (At(r->partCode, 0) == 'N' && text::RuneCount(tail) == 1 &&
            Has("いきしちにひみりぎじびぴえけせてねへめれげぜでべぺ", tail) && text.starts_with(r->source) &&
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

// Text path: the verb records of records move ahead of a longer record that is no verb but
// spells a form of the verb before a polite ます (喜び + ます: 喜ぶ, not the noun 喜び; 急ぎます,
// 頑張れます): the verb's stem and its continuative or its potential stem, and no record longer.
// Plain-dictionary records stay in front.
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
            (type == tokenizer::kTypeSymbol && SymbolWord(merged) && !LatinOrDigits(i - 1) && !LatinOrDigits(i + 1)) ||
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
    else if (source == "(" || source == ")" || source == "（" || source == "）") r->partCode = "N600";
    else r->partCode = "N110";
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

// Splits a Japanese token into dictionary words, inserting each into the chain;
// unknown characters become synthetic words.
void Worker::BuildSurfaceTransform(int token, std::string_view surface) {
    transformSurface = surface;
    resumeCursor = -1;
    CoverSurface(token, surface, 0, nullptr);
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
    const Copies c = CopiesAt(text);
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
        if (UnitAfterNumber(text, numeric->source.size())) {
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

// Makes an item from records at full[cursor:], then translates the rest of the token
// after it; when that fails it retries with the shorter candidates.
bool Worker::ProcessCandidateRecord(std::string_view full, int cursor, int token, Record* records, Item* parent) {
    if (settings.text) records = LongerNounFirst(snapshot, records, From(full, cursor));
    if (settings.text) records = CopulaPredicateFirst(records, From(full, cursor));
    if (settings.text) records = NounBeforeCompound(records, From(full, cursor));
    if (settings.text) records = VerbBeforePolite(records, From(full, cursor));
    if (parent != nullptr) {
        records = ReorderAfterNoun(*parent, records, snapshot, From(full, cursor));
        if (settings.text) records = NounAfterModifier(*parent, records, From(full, cursor));
        if (settings.text) records = SuffixAfterNoun(*parent, records);
    }
    const std::string_view text = From(full, cursor);
    Item* candidate = InsertLateCandidate(full, cursor, token, records);
    while (settings.text && parent != nullptr && candidate != nullptr && DegreeNegativeAdnominal(*parent, *candidate, text)) {
        Record* remaining = candidate->group->records;
        candidate->group->records = nullptr;
        candidate = InsertLateCandidate(full, cursor, token, remaining);
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
    const bool translated = TranslateCandidateAt(full, cursor + candidate->length, token, candidate);
    if (settings.improvements.perLevelRetries) candidateRetryCount = savedRetries;
    if (translated) {
        Insert(candidate);
        return true;
    }
    const std::string_view source = candidate->group->surface;
    const int typ = Classify(source);
    const char part = At(candidate->Code(0), 0);
    if (part == '6' || ((typ == 24 || typ == 14) && part != '4' && part != '7' && part != '8') ||
        IsDigit(At(source, 0)) || IsAlpha(At(source, 0)) || StartsWithSymbol(source) || candidateRetryCount >= 3) {
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
    if (!name->IsNameItem() && !(person && NicknameSuke(text))) return nullptr;
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
            Record* stop = nullptr;
            const bool ok = MatchConjugation(*word, text, offset, rec, stop);
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
            static constexpr int kKinds[] = {0, 9, 11, 13};
            verb->patternKind = kKinds[tableGroup];
            verb->patternMatched = 1;
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
