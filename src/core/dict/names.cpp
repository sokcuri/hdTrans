// The name layer holds the senses of names*.tsv files: person, work, place and
// organisation names and terms. A name sense never replaces a base sense on its
// own: it takes part in a lookup like a plain-dictionary entry only when its key is
// confirmed for the text (LookupContext::confirmed; not a place sense, since
// confirmed keys are persons), when the key is not a word of the base dictionary
// (StrongName), when a katakana person name stands before an honorific
// (AddressedName), or, for person senses, when a familiar honorific follows it
// (PersonCue). A plain-dictionary entry with the same key removes it. A place sense
// with the flag replace respells the key's base senses instead (Load).
#include <algorithm>
#include <array>

#include "core/dict/names.h"
#include "core/dict/runes.h"
#include "core/dict/snapshot.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"

namespace hdTrans::dict {

namespace {

using internal::RuneCountOf;

text::Rune DecodeRune(std::string_view s) { return internal::DecodeRuneAt(s); }
std::size_t RuneCount(std::string_view s) { return RuneCountOf(s); }

bool IsHiragana(char32_t r) { return r >= U'ぁ' && r <= U'ゖ'; }
bool IsKatakana(char32_t r) { return (r >= U'ァ' && r <= U'ヺ') || r == U'ー'; }

// The last code point of s and its size; an invalid end is U+FFFD of size 1.
text::Rune DecodeLastRune(std::string_view s) {
    const std::size_t end = s.size();
    if (end == 0) return {text::kReplacement, 0};
    const auto lastByte = static_cast<unsigned char>(s[end - 1]);
    if (lastByte < 0x80) return {lastByte, 1};
    const std::size_t lim = end >= 4 ? end - 4 : 0;
    std::size_t start = end - 1;
    while (start > lim && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) start--;
    const auto r = DecodeRune(s.substr(start));
    if (start + r.size != end) return {text::kReplacement, 1};
    return r;
}

char32_t FirstRune(std::string_view s) { return s.empty() ? text::kReplacement : DecodeRune(s).cp; }

bool ContainsRune(std::string_view s, char32_t r) {
    for (std::size_t i = 0; i < s.size();) {
        const auto d = DecodeRune(s.substr(i));
        if (d.cp == r) return true;
        i += d.size;
    }
    return false;
}

template <class F>
bool AnyRune(std::string_view s, F&& f) {
    for (std::size_t i = 0; i < s.size();) {
        const auto d = DecodeRune(s.substr(i));
        if (f(d.cp)) return true;
        i += d.size;
    }
    return false;
}

template <std::size_t N>
bool HasPrefixIn(std::string_view s, const std::array<std::string_view, N>& list) {
    return std::any_of(list.begin(), list.end(), [s](std::string_view p) { return s.starts_with(p); });
}

// Honorifics that follow a person's name.
constexpr std::array<std::string_view, 11> kHonorifics = {"さん", "ちゃん", "くん", "君", "様", "さま",
                                                           "先輩", "先生", "殿", "氏", "たん"};
// The particles and honorifics that make a word followed by them an inflected key.
constexpr std::array<std::string_view, 31> kParticleTails = {
    "の", "は", "が", "を", "に", "へ", "と", "も", "で", "や", "って", "とは", "には", "では", "から", "まで",
    "より", "だけ", "への", "との", "での", "にも", "とも", "でも", "だ", "です", "さん", "ちゃん", "くん", "さま", "たん",
};
// The particles and honorifics that follow a name.
constexpr std::array<std::string_view, 20> kNameFollowers = {"は", "が", "を", "の", "に", "へ", "と", "も", "や", "で",
                                                              "か", "だ", "って", "から", "まで", "より", "さん", "ちゃん", "くん", "さま"};
// Endings of a sentence, not of a name (陽炎型十番艦の時津風だよ).
constexpr std::array<std::string_view, 12> kSentenceTails = {"だよ", "だね", "だぞ", "です", "ですか", "ます",
                                                              "ますか", "ました", "ません", "でした", "だもん", "ないもん"};
// Familiar honorifics after a key that make it a person's name (梨花ちゃん, 圭一くん).
constexpr std::array<std::string_view, 5> kPersonCues = {"ちゃん", "くん", "君", "たん", "っち"};
// Particles that join the words of a phrase key.
constexpr std::string_view kPhraseParticles = "のとがをにへや";
constexpr std::string_view kNumerals = "〇一二三四五六七八九十百千万億兆0123456789０１２３４５６７８９";
constexpr std::string_view kCounters = "本人個回日年月時分秒歳枚匹頭台冊杯度番号階件通名羽隻機発門両着足軒";

bool PersonCue(std::string_view after) { return HasPrefixIn(after, kPersonCues); }

// Whether key is a number, alone or with a counter (二, 十七, 四本, 0時).
bool NumeralKey(std::string_view key) {
    std::vector<char32_t> r;
    for (std::size_t i = 0; i < key.size();) {
        const auto d = DecodeRune(key.substr(i));
        r.push_back(d.cp);
        i += d.size;
    }
    if (r.size() > 1 && ContainsRune(kCounters, r.back())) r.pop_back();
    return std::all_of(r.begin(), r.end(), [](char32_t c) { return ContainsRune(kNumerals, c); });
}

// key with its katakana written in hiragana.
std::string HiraganaOf(std::string_view key) {
    std::string out;
    out.reserve(key.size());
    for (std::size_t i = 0; i < key.size();) {
        const auto d = DecodeRune(key.substr(i));
        text::AppendRune(out, d.cp >= U'ァ' && d.cp <= U'ヶ' ? d.cp - U'ァ' + U'ぁ' : d.cp);
        i += d.size;
    }
    return out;
}

// Whether s holds 、 or 。.
bool HasCommaOrPeriod(std::string_view s) {
    for (std::size_t i = 0; i + 2 < s.size(); i++) {
        if (s[i] == '\xE3' && s[i + 1] == '\x80' && (s[i + 2] == '\x81' || s[i + 2] == '\x82')) return true;
    }
    return false;
}

int Syllables(std::string_view s) {
    int n = 0;
    for (std::size_t i = 0; i < s.size();) {
        const auto d = DecodeRune(s.substr(i));
        if (d.cp >= U'가' && d.cp <= U'힣') n++;
        i += d.size;
    }
    return n;
}

// A key is an inflected word when it ends in っ (なっ) or in a sentence ending, or
// when it is a stem of base and a hiragana tail and the stem is a verb or adjective
// stem (食べる, 嬉しいです), a noun that takes する with a form of する (心配しないで),
// or a word followed by a particle or an honorific (ここの, 俺は, 姉ちゃん). A stem of
// one hiragana does not count.
bool InflectedKey(std::string_view key, const Dict& base) {
    if (key.ends_with("っ")) return true;
    for (const std::string_view t : kSentenceTails) {
        if (key.ends_with(t) && RuneCount(key) >= 4) return true;
    }
    for (std::size_t i = key.size(); i > 0;) {
        const auto r = DecodeLastRune(key.substr(0, i));
        if (!IsHiragana(r.cp)) return false;
        i -= r.size;
        const std::string_view stem = key.substr(0, i);
        if (stem.empty() || (RuneCount(stem) == 1 && IsHiragana(FirstRune(stem)))) return false;
        const std::string_view tail = key.substr(i);
        const std::string_view stripped = key.substr(i, r.size);
        const bool particle = std::find(kParticleTails.begin(), kParticleTails.end(), tail) != kParticleTails.end();
        const int k = base.Find(stem);
        if (k < 0) continue;
        bool inflected = false;
        base.EachRecord(k, [&](const Record& rec) {
            if (rec.codes->part.TagIn("MHG") ||
                (rec.codes->gram.Contains("Hd") && (stripped == "し" || stripped == "さ" || stripped == "す" || stripped == "せ"))) {
                inflected = true;
                return false;
            }
            if (particle && (RuneCount(stem) >= 2 || !IsHiragana(FirstRune(stem)))) {
                inflected = true;
                return false;
            }
            return true;
        });
        if (inflected) return true;
    }
    return false;
}

}  // namespace

namespace internal {

bool PlaceSense(const SemCodes& sem) { return sem.Contains("QCT") || sem.Contains("QNN"); }

NameKeyCounts DropNameKeys(Row* rows, std::size_t count, const Dict& base) {
    NameKeyCounts n;
    for (std::size_t i = 0; i < count; i++) {
        Row& r = rows[i];
        if (HasCommaOrPeriod(r.key)) {
            n.punct++;
        } else if (InflectedKey(r.key, base)) {
            n.inflected++;
        } else if (Syllables(r.target) > 2 * static_cast<int>(RuneCount(r.key)) + 4) {
            n.longKorean++;
        } else {
            continue;
        }
        r.code = Layer::kDeleted;
    }
    return n;
}

}  // namespace internal

const Snapshot::Respelling* Snapshot::Place(std::string_view key) const {
    if (places_.empty()) return nullptr;
    const auto it = places_.find(key);
    return it == places_.end() ? nullptr : &it->second;
}

// The Korean of the record r of key, sp being the respelling of key (null for
// none): the person spelling when key is read as a person and r has its sound, else
// that of the respelling when it applies to r, else r.target.
std::string_view Snapshot::Respelled(const Respelling* sp, const KeySet* persons, std::string_view key,
                                     const Record& r) const {
    if (!sp) return r.target;
    if (persons && persons->find(key) != persons->end() && !sp->person.empty() && text::SameSound(r.target, sp->person)) {
        return sp->person;
    }
    const bool place = internal::PlaceSense(r.codes->sem);
    const bool noun = r.codes->part.TagIn("NIK");
    if ((sp->mode == Respelling::Mode::SameSound && place && text::SameSound(r.target, sp->ko)) ||
        (sp->mode == Respelling::Mode::AllPlaces && place) || (sp->mode == Respelling::Mode::AllNouns && noun)) {
        return sp->ko;
    }
    return r.target;
}

std::vector<Record> Snapshot::NameRecords(std::string_view key) const { return names_.Records(key); }

std::vector<std::string> Snapshot::PersonPlaceKeys(std::string_view text) const {
    std::vector<std::string> keys;
    if (names_.Len() == 0 || places_.empty()) return keys;
    for (std::size_t i = 0; i < text.size();) {
        names_.Prefixes(text.substr(i), [&](int, std::size_t n) {
            const std::string_view k = text.substr(i, n);
            const Respelling* sp = Place(k);
            if (sp && !sp->person.empty() && std::find(keys.begin(), keys.end(), k) == keys.end()) keys.emplace_back(k);
        });
        i += DecodeRune(text.substr(i)).size;
    }
    return keys;
}

std::string Snapshot::NameReading(std::string_view key) const {
    if (names_.Len() == 0) return {};
    std::string fan = text::FromKana(key, text::Preset::Fan);
    if (fan.empty()) return {};
    for (const Record& r : names_.Records(key)) {
        if (r.codes->sem.HasPrefix("QPN") && text::SameSound(r.target, fan)) return fan;
    }
    return {};
}

std::string Snapshot::PersonName(std::string_view key) const {
    const std::string fan = NameReading(key);
    if (fan.empty()) return {};
    const std::vector<Record> base = base_.Records(key);
    if (base.empty() ||
        std::any_of(base.begin(), base.end(), [&](const Record& b) { return text::SameSound(b.target, fan); })) {
        return fan;
    }
    return {};
}

bool Snapshot::PersonCueKey(std::string_view key) const {
    if (!CuedKey(key)) return false;
    for (const Record& r : NameRecords(key)) {
        if (r.codes->sem.HasPrefix("QPN")) return true;
    }
    return false;
}

// Whether a cue may confirm key: two or more characters with a kanji, or one kanji no
// base word has, and no katakana, and no base sense of a person (先生, 部長 take
// honorifics as roles) or a name (the base reading wins).
bool Snapshot::CuedKey(std::string_view key) const {
    // One kanji only where the base dictionary has no word of it (筧, 凪).
    if ((RuneCount(key) < 2 && !base_.Records(key).empty()) || RuneCount(key) < 1 ||
        AnyRune(key, [](char32_t r) { return r >= U'ァ' && r <= U'ヺ'; }) ||
        !AnyRune(key, [](char32_t r) { return r >= 0x4E00 && r <= 0x9FFF; })) {
        return false;
    }
    for (const Record& r : base_.Records(key)) {
        if (r.codes->sem.Contains("VH") || r.codes->sem.HasPrefix("Q")) return false;
    }
    return true;
}

bool Snapshot::BaseWord(std::string_view key) const { return base_.Find(key) >= 0 || user_.Find(key) >= 0; }

// Whether key is base words joined by particles between two characters other than
// hiragana (北の海, 剣と魔法の世界, 我が祖国), a phrase rather than a name.
bool Snapshot::PhraseKey(std::string_view key) const {
    for (std::size_t i = 0; i < key.size();) {
        const auto r = DecodeRune(key.substr(i));
        const std::size_t at = i;
        i += r.size;
        if (at == 0 || !ContainsRune(kPhraseParticles, r.cp)) continue;
        const std::string_view left = key.substr(0, at);
        const std::string_view right = key.substr(at + r.size);
        if (!right.empty() && !IsHiragana(DecodeLastRune(left).cp) && !IsHiragana(FirstRune(right)) &&
            (BaseWord(left) || PhraseKey(left)) && (BaseWord(right) || PhraseKey(right))) {
            return true;
        }
    }
    return false;
}

// Whether the name key s[:n] is not a word of the base dictionary: the base and user
// dictionaries have no sense with it nor, for katakana, with its hiragana spelling
// (アレ, イク); it is not a single character, a number, hiragana only or a phrase; no
// base word other than a verb starts inside it and ends after it (宇宙機 in
// 宇宙機関); and no verb or adjective stem starts inside it, ends with it and is
// followed by hiragana other than a name follower (子来 in 子来た, 家近 in 家近い,
// but not 翔太 in 翔太くん).
bool Snapshot::StrongName(std::string_view s, std::size_t n) const {
    const std::string_view key = s.substr(0, n);
    if (RuneCount(key) == 1 || NumeralKey(key) || !AnyRune(key, [](char32_t r) { return !IsHiragana(r); }) ||
        BaseWord(key) || BaseWord(HiraganaOf(key)) || PhraseKey(key)) {
        return false;
    }
    const std::string_view after = s.substr(n);
    const bool inflected = IsHiragana(FirstRune(after)) && !HasPrefixIn(after, kNameFollowers);
    for (std::size_t i = 0; i < key.size();) {
        const std::size_t at = i;
        i += DecodeRune(key.substr(i)).size;
        if (at == 0) continue;
        bool crosses = false;
        base_.Prefixes(s.substr(at), [&](int k, std::size_t l) {
            base_.EachRecord(k, [&](const Record& r) {
                const PartCode p = r.codes->part;
                if (p.Empty()) return true;
                crosses = crosses || (at + l > n && !p.TagIn("MSTUV")) || (at + l == n && p.TagIn("MHG") && inflected);
                return true;
            });
        });
        if (crosses) return false;
    }
    return true;
}

// Whether the name key s[:n], katakana of two or more characters, is followed by an
// honorific and is a person name (PersonName).
bool Snapshot::AddressedName(std::string_view s, std::size_t n) const {
    const std::string_view key = s.substr(0, n);
    return RuneCount(key) >= 2 && !AnyRune(key, [](char32_t r) { return !IsKatakana(r); }) &&
           HasPrefixIn(s.substr(n), kHonorifics) && !PersonName(key).empty();
}

// The name senses that take part in a lookup of text and whose key prefixes text,
// longer keys first, as entries marked nameLayer. owned are the plain entries of
// text.
void Snapshot::NameEntries(std::string_view text, LookupMode mode, const LookupContext& ctx,
                           const std::vector<const UserEntry*>& owned, std::vector<UserEntry>& out) const {
    if (names_.Len() == 0 || !AcceptsTag(mode, 'N')) return;
    struct Match {
        int k;
        std::size_t n;
    };
    std::vector<Match> matches;
    names_.Prefixes(text, [&](int k, std::size_t n) { matches.push_back({k, n}); });
    for (auto m = matches.rbegin(); m != matches.rend(); ++m) {
        const std::string_view key = text.substr(0, m->n);
        const Respelling* sp = Place(key);
        // A key that takes part only as confirmed (by the registry or a cue in the
        // line) is a person's name: its place senses stay out.
        bool person = false, cued = false;
        if (!StrongName(text, m->n) && !AddressedName(text, m->n)) {
            person = true;
            if (!ctx.confirmed || ctx.confirmed->find(key) == ctx.confirmed->end()) {
                cued = PersonCue(text.substr(m->n)) && CuedKey(key);
                if (!cued) continue;
            }
        }
        if (std::any_of(owned.begin(), owned.end(), [key](const UserEntry* e) { return e->key == key; })) continue;
        names_.EachRecord(m->k, [&](const Record& r) {
            const bool isPerson = r.codes->sem.HasPrefix("QPN");
            const PartCode p = r.codes->part;
            if (p.Empty() || !AcceptsTag(mode, p.Tag()) || (cued && !isPerson) ||
                (person && !isPerson && internal::PlaceSense(r.codes->sem))) {
                return true;
            }
            UserEntry& e = out.emplace_back();
            e.key = key;
            e.target = Respelled(sp, ctx.persons, key, r);
            e.codes = r.codes->asPlain;
            e.file = -2;
            e.line = r.id;
            e.name = isPerson;
            e.nameLayer = true;
            return true;
        });
    }
}

}  // namespace hdTrans::dict
