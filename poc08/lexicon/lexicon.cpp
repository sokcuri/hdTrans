#include "lexicon.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hd8::lex {
namespace {

constexpr char kMagic[8] = {'H', 'D', '8', 'L', 'E', 'X', 0, 0};
constexpr uint32_t kVersion = 1;
constexpr uint32_t kTables = 9;

static_assert(sizeof(RawEntry) == 24);
static_assert(sizeof(RawSense) == 24);
static_assert(sizeof(RawAtom) == 8);
static_assert(sizeof(RawAlt) == 8);
static_assert(sizeof(RawPhrase) == 28);
static_assert(sizeof(RawName) == 20);
static_assert(sizeof(RawClass) == 8);

constexpr std::string_view kPosNames[] = {"n", "pn", "num", "suf", "pref", "v1", "v5", "vs", "vk", "vz",
                                          "adj", "na", "adv", "adn", "conj", "intj"};
constexpr std::string_view kSlotNames[] = {"subj", "obj", "dat", "loc", "with", "from", "to", "arg",
                                           "head", "mod", "pred", "after", "next", "form", "domain",
                                           "register", "main.vol", "ctx", "subj-of", "obj-of", "dat-of", "verb"};
constexpr std::string_view kClassNames[] = {"", "REG", "HA", "D", "B", "BWA", "S", "H",
                                            "REU", "REO", "U", "EU", "L", "COP", "EXIST"};

bool IsVerb(uint8_t p) {
    return p >= static_cast<uint8_t>(Pos::v1) && p <= static_cast<uint8_t>(Pos::vz);
}

template <class T>
bool Table(const std::vector<uint32_t>& data, size_t bytes, uint32_t off, uint32_t count, std::span<const T>& out) {
    if (off % 4 != 0 || off > bytes || (bytes - off) / sizeof(T) < count) return false;
    out = std::span<const T>(reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(data.data()) + off), count);
    return true;
}

}  // namespace

bool Lexicon::Load(const std::string& path, std::string& err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        err = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> bytes;
    uint8_t buf[1 << 16];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    return LoadBytes(std::move(bytes), err);
}

bool Lexicon::LoadBytes(std::vector<uint8_t> bytes, std::string& err) {
    const size_t size = bytes.size();
    data_.assign((size + 3) / 4, 0);
    if (size > 0) std::memcpy(data_.data(), bytes.data(), size);
    const auto* p = reinterpret_cast<const uint8_t*>(data_.data());
    if (size < 16 || std::memcmp(p, kMagic, 8) != 0) {
        err = "not a lexicon file";
        return false;
    }
    uint32_t version = 0, tables = 0;
    std::memcpy(&version, p + 8, 4);
    std::memcpy(&tables, p + 12, 4);
    if (version != kVersion || tables != kTables || size < 16 + tables * 8) {
        err = "unsupported lexicon version";
        return false;
    }
    uint32_t off[kTables], cnt[kTables];
    for (uint32_t i = 0; i < kTables; ++i) {
        std::memcpy(&off[i], p + 16 + i * 8, 4);
        std::memcpy(&cnt[i], p + 20 + i * 8, 4);
    }
    std::span<const char> str;
    bool ok = Table(data_, size, off[0], cnt[0], entries_) && Table(data_, size, off[1], cnt[1], senses_) &&
              Table(data_, size, off[2], cnt[2], atoms_) && Table(data_, size, off[3], cnt[3], alts_) &&
              Table(data_, size, off[4], cnt[4], phrases_) && Table(data_, size, off[5], cnt[5], names_) &&
              Table(data_, size, off[6], cnt[6], classes_) && Table(data_, size, off[7], cnt[7], byJa_) &&
              Table(data_, size, off[8], cnt[8], str);
    if (!ok || str.empty() || str.back() != '\0') {
        err = "corrupt lexicon tables";
        return false;
    }
    strings_ = std::string_view(str.data(), str.size());
    for (const RawEntry& e : entries_) {
        if (e.firstSense + e.nSense > senses_.size() || e.pos >= static_cast<uint8_t>(Pos::count) ||
            e.ja >= strings_.size() || e.yomi >= strings_.size()) {
            err = "corrupt lexicon entry";
            return false;
        }
    }
    for (const RawSense& s : senses_) {
        if (s.firstAtom + s.nAtom > atoms_.size() || s.ko >= strings_.size()) {
            err = "corrupt lexicon sense";
            return false;
        }
    }
    for (const RawAtom& a : atoms_) {
        if (a.firstAlt + a.nAlt > alts_.size()) {
            err = "corrupt lexicon cue";
            return false;
        }
    }
    for (uint32_t i : byJa_) {
        if (i >= entries_.size()) {
            err = "corrupt lexicon index";
            return false;
        }
    }
    return true;
}

std::string_view Lexicon::Str(uint32_t off) const {
    if (off >= strings_.size()) return {};
    return std::string_view(strings_.data() + off);
}

std::span<const RawSense> Lexicon::Senses(uint32_t entry) const {
    const RawEntry& e = entries_[entry];
    return senses_.subspan(e.firstSense, e.nSense);
}

int32_t Lexicon::Find(std::string_view ja, std::string_view yomi, Pos pos) const {
    const std::string y = Hiragana(yomi);
    // entries are sorted by ja, then yomi, then pos name, joined with 0x1F
    auto key = [&](const RawEntry& e) {
        std::string k(Str(e.ja));
        k += '\x1f';
        k += Str(e.yomi);
        return k;
    };
    std::string want(ja);
    want += '\x1f';
    want += y;
    auto lo = std::lower_bound(entries_.begin(), entries_.end(), want,
                               [&](const RawEntry& e, const std::string& w) { return key(e) < w; });
    int32_t family = -1;
    for (auto it = lo; it != entries_.end() && key(*it) == want; ++it) {
        if (it->pos == static_cast<uint8_t>(pos)) return static_cast<int32_t>(it - entries_.begin());
        if (family < 0 && IsVerb(it->pos) && IsVerb(static_cast<uint8_t>(pos)))
            family = static_cast<int32_t>(it - entries_.begin());
    }
    return family;
}

std::vector<uint32_t> Lexicon::FindSpelling(std::string_view ja) const {
    auto lo = std::lower_bound(byJa_.begin(), byJa_.end(), ja,
                               [&](uint32_t i, std::string_view w) { return Str(entries_[i].ja) < w; });
    std::vector<uint32_t> out;
    for (auto it = lo; it != byJa_.end() && Str(entries_[*it].ja) == ja; ++it) out.push_back(*it);
    return out;
}

std::span<const RawPhrase> Lexicon::PhrasesStartingWith(std::string_view lemma) const {
    auto lo = std::lower_bound(phrases_.begin(), phrases_.end(), lemma,
                               [&](const RawPhrase& p, std::string_view w) { return Str(p.head) < w; });
    auto hi = lo;
    while (hi != phrases_.end() && Str(hi->head) == lemma) ++hi;
    return phrases_.subspan(static_cast<size_t>(lo - phrases_.begin()), static_cast<size_t>(hi - lo));
}

std::span<const RawName> Lexicon::FindNames(std::string_view ja) const {
    auto lo = std::lower_bound(names_.begin(), names_.end(), ja,
                               [&](const RawName& n, std::string_view w) { return Str(n.ja) < w; });
    auto hi = lo;
    while (hi != names_.end() && Str(hi->ja) == ja) ++hi;
    return names_.subspan(static_cast<size_t>(lo - names_.begin()), static_cast<size_t>(hi - lo));
}

int32_t Lexicon::ClassIndex(std::string_view name) const {
    for (size_t i = 0; i < classes_.size(); ++i)
        if (Str(classes_[i].name) == name) return static_cast<int32_t>(i);
    return -1;
}

bool Lexicon::IsA(int32_t c, int32_t ancestor) const {
    for (size_t steps = 0; c >= 0 && steps <= classes_.size(); ++steps) {
        if (c == ancestor) return true;
        c = classes_[static_cast<size_t>(c)].parent;
    }
    return false;
}

std::string Lexicon::RowId(uint32_t entry, uint16_t sense) const {
    const RawEntry& e = entries_[entry];
    std::string id = "lex:";
    id += Str(e.ja);
    id += '/';
    id += Str(e.yomi);
    id += '/';
    id += PosName(static_cast<Pos>(e.pos));
    id += '#';
    id += std::to_string(sense);
    return id;
}

std::string_view PosName(Pos p) {
    const auto i = static_cast<size_t>(p);
    return i < std::size(kPosNames) ? kPosNames[i] : std::string_view("?");
}

bool PosFromName(std::string_view s, Pos& out) {
    for (size_t i = 0; i < std::size(kPosNames); ++i) {
        if (kPosNames[i] == s) {
            out = static_cast<Pos>(i);
            return true;
        }
    }
    return false;
}

std::string_view SlotName(Slot s) {
    const auto i = static_cast<size_t>(s);
    return i < std::size(kSlotNames) ? kSlotNames[i] : std::string_view("?");
}

std::string_view KClassName(KClass c) {
    const auto i = static_cast<size_t>(c);
    return i < std::size(kClassNames) ? kClassNames[i] : std::string_view("?");
}

std::string Hiragana(std::string_view s) {
    // katakana U+30A1..U+30F6 (E3 82 A1 .. E3 83 B6) becomes hiragana U+3041..U+3096
    std::string out(s);
    for (size_t i = 0; i + 2 < out.size(); ++i) {
        const auto b0 = static_cast<uint8_t>(out[i]);
        if (b0 != 0xE3) continue;
        const auto b1 = static_cast<uint8_t>(out[i + 1]), b2 = static_cast<uint8_t>(out[i + 2]);
        uint32_t cp = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
        if (cp >= 0x30A1 && cp <= 0x30F6) {
            cp -= 0x60;
            out[i + 1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[i + 2] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        i += 2;
    }
    return out;
}

}  // namespace hd8::lex
