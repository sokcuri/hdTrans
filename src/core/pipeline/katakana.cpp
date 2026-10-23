#include <algorithm>
#include <string>
#include <vector>

#include "core/pipeline/protect.h"
#include "core/pipeline/runes.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"
#include "core/word/conjugation.h"

namespace hdTrans::pipeline::internal {

namespace {

constexpr dict::LookupMode kAllModes[] = {dict::LookupMode::General, dict::LookupMode::AdjectivalNounEnding,
                                          dict::LookupMode::AdjectiveEnding, dict::LookupMode::VerbEnding,
                                          dict::LookupMode::PrefixVerb};

// Calls f for each plain entry stored under exactly key, kana spellings left out. The
// modes partition the tags, so every entry comes once.
template <class F>
void ForUserEntries(std::string_view key, const LineContext& ctx, F f) {
    if (ctx.lookup.plain == nullptr) return;
    for (const dict::LookupMode mode : kAllModes) {
        for (const dict::UserEntry* e : ctx.lookup.plain->Search(key, mode)) {
            if (e->key.size() == key.size() && !e->kana) f(*e);
        }
    }
}

bool HasUserEntries(std::string_view key, const LineContext& ctx) {
    bool found = false;
    ForUserEntries(key, ctx, [&](const dict::UserEntry&) { found = true; });
    return found;
}

std::u32string Runes(std::string_view s) {
    std::u32string out;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        out.push_back(r.cp);
        s.remove_prefix(r.size);
    }
    return out;
}

std::string Utf8(std::u32string_view rs) {
    std::string out;
    for (const char32_t r : rs) text::AppendRune(out, r);
    return out;
}

template <class Pred>
int CountRunes(std::string_view s, Pred f) {
    int n = 0;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (f(r.cp)) n++;
        s.remove_prefix(r.size);
    }
    return n;
}

bool IsKatakanaLetter(char32_t r) { return r >= U'ァ' && r <= U'ヶ'; }

// Katakana ァ..ヶ in hiragana.
std::string ToHiragana(std::string_view s) {
    std::string out;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.size == 1 && r.cp == text::kReplacement) {
            out += s[0];
        } else {
            text::AppendRune(out, IsKatakanaLetter(r.cp) ? r.cp - 0x60 : r.cp);
        }
        s.remove_prefix(r.size);
    }
    return out;
}

// ---- katakana speech ----

// A part of a run's reading: kana[from:to] of the run.
enum class PieceKind : char {
    Word = 'w',  // a dictionary word in hiragana
    Func = 'f',  // an entry of kFuncWords
    Conj = 'c',  // one kana that conjugates the verb or adjective stem before it
    Kata = 'k',  // a katakana dictionary word, kept in katakana
};

struct KataPiece {
    size_t from = 0;
    size_t to = 0;
    PieceKind kind = PieceKind::Word;
    int morae = 0;     // the length of its reading, small ゃゅょ not counted
    std::string tags;  // of a Word, see KeyTags
};

// A run of katakana in a line.
struct KataRun {
    size_t start = 0;  // byte offsets in the line
    size_t end = 0;
    std::u32string kana;
    // The run read as Japanese words; empty when it does not, or when hiragana or kanji
    // touch the run.
    std::vector<KataPiece> pieces;
    // The other lines of the document write the run in their prose.
    bool prose = false;
};

// Particles and endings with what each follows: kana of the vowel rows a, i, u, e, o
// or the kana given; "" for anything.
struct FuncWord {
    std::u32string_view word;
    std::u32string_view after;
};

constexpr FuncWord kFuncWords[] = {
    {U"は", U""}, {U"が", U""}, {U"を", U""}, {U"に", U""}, {U"の", U""}, {U"で", U""}, {U"と", U""},
    {U"も", U""}, {U"へ", U""}, {U"や", U""}, {U"か", U""}, {U"よ", U""}, {U"ね", U""}, {U"ぞ", U""},
    {U"ぜ", U""}, {U"さ", U""}, {U"な", U""}, {U"わ", U""}, {U"から", U""}, {U"まで", U""}, {U"より", U""},
    {U"って", U""}, {U"けど", U""}, {U"のに", U""}, {U"ので", U""}, {U"ても", U""}, {U"でも", U""},
    {U"だけ", U""}, {U"しか", U""}, {U"ほど", U""}, {U"なら", U""}, {U"よね", U""}, {U"かな", U""},
    {U"かしら", U""}, {U"こそ", U""}, {U"たち", U""}, {U"ども", U""}, {U"です", U""}, {U"ですか", U""},
    {U"でした", U""}, {U"でしょう", U""}, {U"でしょうか", U""}, {U"でしょ", U""}, {U"だ", U""},
    {U"だった", U""}, {U"だろう", U""}, {U"だろ", U""}, {U"じゃ", U""}, {U"じゃう", U""}, {U"だら", U""},
    {U"だり", U""}, {U"ん", U""}, {U"し", U""}, {U"する", U""}, {U"して", U""}, {U"した", U""},
    {U"しない", U""}, {U"しよう", U""}, {U"ござい", U""}, {U"ございます", U""}, {U"ございません", U""},
    {U"ください", U""}, {U"います", U""}, {U"いました", U""}, {U"いません", U""}, {U"いる", U""},
    {U"いた", U""}, {U"いない", U""}, {U"あります", U""}, {U"ありません", U""}, {U"します", U""},
    {U"しました", U""}, {U"しません", U""}, {U"じゃない", U""}, {U"ではない", U""},
    {U"ます", U"ie"}, {U"ますか", U"ie"}, {U"ました", U"ie"}, {U"ません", U"ie"}, {U"ましょう", U"ie"},
    {U"ませ", U"ie"}, {U"まし", U"ie"}, {U"たい", U"ie"}, {U"たく", U"ie"}, {U"なさい", U"ie"}, {U"ろ", U"ie"},
    {U"ない", U"aie"}, {U"なかった", U"aie"}, {U"なく", U"aie"}, {U"なきゃ", U"aie"},
    {U"た", U"ieっ"}, {U"たら", U"ieっ"}, {U"たり", U"ieっ"}, {U"ちゃ", U"ieっ"}, {U"ちゃう", U"ieっ"},
    {U"ちゃった", U"ieっ"},
    {U"て", U"ieっく"}, {U"てる", U"ieっく"}, {U"てた", U"ieっく"}, {U"ている", U"ieっく"}, {U"ていた", U"ieっく"},
    {U"ば", U"e"},
};

const FuncWord* FindFuncWord(std::u32string_view w) {
    for (const FuncWord& f : kFuncWords) {
        if (f.word == w) return &f;
    }
    return nullptr;
}

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
        if (RuneIn(r, kVowelRows[v])) return v;
    }
    return -1;
}

// Whether hiragana prev may come before an ending that follows after.
bool Follows(char32_t prev, std::u32string_view after) {
    if (after.empty() || RuneIn(prev, after)) return true;
    const int v = VowelOf(prev + 0x60);
    return v >= 0 && RuneIn(static_cast<char32_t>("aiueo"[v]), after);
}

// The kana that conjugate a verb or adjective stem.
constexpr std::u32string_view kConjKana =
    U"かきくけこがぎぐげごさしすせそたちつてとなにぬねのばびぶべぼまみむめもらりるれろわいうえおっん";

// Piece costs of SegmentRun: kCostWord by length in morae (1..4 or more), kCostFunc by
// length (1, 2 or more).
constexpr int kCostWord[4] = {12, 11, 10, 9};
constexpr int kCostFunc[2] = {10, 8};
constexpr int kCostConj = 5;
constexpr int kCostKata = 17;

int Morae(std::u32string_view h) {
    int n = 0;
    for (const char32_t r : h) {
        if (!RuneIn(r, U"ゃゅょ")) n++;
    }
    return n;
}

// Whether more than half of the tags are in set.
bool MostlyTags(std::string_view tags, std::string_view set) {
    size_t n = 0;
    for (const char t : tags) {
        if (set.find(t) != std::string_view::npos) n++;
    }
    return 2 * n > tags.size();
}

bool HasTag(std::string_view tags, std::string_view any) { return tags.find_first_of(any) != std::string_view::npos; }

// The run with its pieces other than Kata in hiragana.
std::string RunHiragana(const KataRun& r) {
    std::u32string out;
    for (const KataPiece& p : r.pieces) {
        for (size_t i = p.from; i < p.to; i++) {
            char32_t c = r.kana[i];
            if (p.kind != PieceKind::Kata && IsKatakanaLetter(c)) c -= 0x60;
            out.push_back(c);
        }
    }
    return Utf8(out);
}

// The weight of a piece that is a particle, an ending or a conjugation; 0 for others.
int Grammar(const KataPiece& p) {
    if (p.kind == PieceKind::Conj || (p.kind == PieceKind::Func && p.morae == 1)) return 1;
    if (p.kind == PieceKind::Func || (p.kind == PieceKind::Word && p.morae >= 2 && MostlyTags(p.tags, "PSTU"))) return 2;
    return 0;
}

// The grammar evidence of a run's reading: the particles, endings and conjugations of
// a run that ends in one, each after a word or after counted grammar (but not a
// one-mora particle after another), two for one of two or more morae and one for a
// shorter one. A run that ends in a one-mora ン weighs nothing (ゴロン). A run of one
// piece weighs two if it is a func word or a greeting (デス, ゴザイマス, アリガトウ) and
// one if it is a word that is mostly a particle or an ending.
int Evidence(const KataRun& r) {
    if (r.pieces.size() == 1) {
        const KataPiece& p = r.pieces[0];
        if ((p.kind == PieceKind::Func && p.morae >= 2) || (p.kind == PieceKind::Word && HasTag(p.tags, "A"))) return 2;
        if (p.kind == PieceKind::Word && MostlyTags(p.tags, "PSTU")) return 1;
        return 0;
    }
    switch (Grammar(r.pieces.back())) {
        case 0:
            return 0;
        case 1: {
            std::u32string_view k = r.kana;
            while (!k.empty() && RuneIn(k.back(), U"ーッァィゥェォ")) k.remove_suffix(1);
            if (!k.empty() && k.back() == U'ン') return 0;
            break;
        }
        default:
            break;
    }
    int n = 0;
    bool open = false;         // grammar may follow the piece before
    bool afterSingle = false;  // the piece before is a one-mora particle
    for (size_t i = 0; i < r.pieces.size(); i++) {
        const KataPiece& p = r.pieces[i];
        const int g = Grammar(p);
        const bool single = p.kind == PieceKind::Func && p.morae == 1;
        const bool counted = i > 0 && g > 0 && open && !(single && afterSingle);
        if (counted) n += g;
        open = g == 0 || (i == 0 && !single) || counted;
        afterSingle = single;
    }
    return n;
}

bool OnlyLengthening(std::u32string_view kana) {
    for (const char32_t r : kana) {
        if (!RuneIn(r, U"ーッァィゥェォ")) return false;
    }
    return true;
}

// The hiragana of kana without the marks that only lengthen a sound (ー, a small vowel
// repeating the vowel before it, ッ before the end), and for each character of the
// reading its index in kana. Empty for kana with a small vowel of a loanword (ティ,
// ファ) or a kana that has no hiragana (ヴ, ヵ, ヶ).
void Reading(std::u32string_view kana, std::u32string& h, std::vector<size_t>& at) {
    h.clear();
    at.clear();
    for (size_t i = 0; i < kana.size(); i++) {
        const char32_t r = kana[i];
        if (r == U'ー') continue;
        if (r == U'ッ' && OnlyLengthening(kana.substr(i + 1))) continue;
        if (RuneIn(r, U"ァィゥェォ")) {
            if (i == 0 || VowelOf(kana[i - 1]) == VowelOf(r) || kana[i - 1] == U'ー') continue;
            h.clear();
            at.clear();
            return;
        }
        if (r >= U'ヴ') {
            h.clear();
            at.clear();
            return;
        }
        h.push_back(r - 0x60);
        at.push_back(i);
    }
}

// Whether h is three or more of あいうえお and はひふへほ (laughter).
bool Laughter(std::u32string_view h) {
    if (h.size() < 3) return false;
    for (const char32_t r : h) {
        if (!RuneIn(r, U"あいうえおはひふへほ")) return false;
    }
    return true;
}

// Whether h without っ is a part of two or more kana written twice.
bool Doubled(std::u32string_view h) {
    std::u32string s;
    for (const char32_t r : h) {
        if (r != U'っ') s.push_back(r);
    }
    const size_t n = s.size() / 2;
    return n >= 2 && s.size() == 2 * n && s.compare(0, n, s, n, n) == 0;
}

// The cheapest cover of h, the reading of kana (at maps it back), by pieces: a
// dictionary word of two or more morae that is not a func word, a func word after what
// it follows, a conjugating kana after a verb or adjective stem (with the stem when it
// has one mora), or a part of kana of two or more kana that is a katakana dictionary
// word. Only katakana pieces hold a long mark ー that is not at the end of the run.
// Empty when nothing covers h.
std::vector<KataPiece> SegmentRun(std::u32string_view kana, std::u32string_view h, const std::vector<size_t>& at,
                               const LineContext& ctx, const dict::Snapshot& snap) {
    constexpr size_t kMaxKey = 10;
    struct State {
        int cost = -1;
        size_t from = 0;  // the previous state
        PieceKind kind = PieceKind::Word;
        std::string tags;
    };
    // State 2i reaches h[:i], state 2i+1 reaches h[:i] after a stem.
    const size_t n = h.size();
    std::vector<State> best(2 * (n + 1));
    best[0].cost = 0;
    auto relax = [&](size_t to, size_t from, int cost, PieceKind kind, std::string_view tags) {
        if (best[to].cost < 0 || cost < best[to].cost) best[to] = {cost, from, kind, std::string(tags)};
    };
    // The kana index of h[i], with the marks before it.
    auto start = [&](size_t i) { return i == 0 ? size_t{0} : at[i]; };
    // The kana index after h[:j], with its marks.
    auto end = [&](size_t j) { return j == n ? kana.size() : at[j]; };
    // No ー in the kana of h[i:j] but at the end of the run.
    auto plain = [&](size_t i, size_t j) {
        const size_t e = j == n ? at[j - 1] : end(j);
        const size_t s = start(i);
        return s >= e || kana.substr(s, e - s).find(U'ー') == std::u32string_view::npos;
    };
    for (size_t i = 0; i < n; i++) {
        for (size_t s = 2 * i; s <= 2 * i + 1; s++) {
            const int c = best[s].cost;
            if (c < 0) continue;
            if (s == 2 * i + 1 && RuneIn(h[i], kConjKana) && plain(i, i + 1)) {
                relax(2 * (i + 1), s, c + kCostConj, PieceKind::Conj, "");
            }
            for (size_t j = i + 1; j <= n && j - i <= kMaxKey; j++) {
                if (j - i >= 2 && (i > 0 || j < n) &&
                    !KeyTags(Utf8(kana.substr(start(i), end(j) - start(i))), ctx, snap).empty()) {
                    relax(2 * j, s, c + kCostKata, PieceKind::Kata, "");
                }
                if (!plain(i, j)) continue;
                const std::u32string_view w = h.substr(i, j - i);
                const std::string word = Utf8(w);
                const int m = Morae(w);
                const FuncWord* f = FindFuncWord(w);
                if (f != nullptr && (i == 0 || Follows(h[i - 1], f->after))) {
                    relax(2 * j, s, c + kCostFunc[std::clamp(m, 1, 2) - 1], PieceKind::Func, "");
                }
                const std::string tags = KeyTags(word, ctx, snap);
                if (m == 1) {
                    // A one-mora verb stem is a piece with its conjugation.
                    if (j < n && HasTag(tags, "M") && RuneIn(h[j], kConjKana) && plain(i, j + 1)) {
                        relax(2 * (j + 1), s, c + kCostWord[0] + kCostConj, PieceKind::Conj, "");
                    }
                } else if (!tags.empty() && f == nullptr) {
                    const int cost = c + kCostWord[std::clamp(m, 1, 4) - 1];
                    relax(2 * j, s, cost, PieceKind::Word, tags);
                    if (HasTag(tags, "MH")) relax(2 * j + 1, s, cost, PieceKind::Word, tags);
                }
            }
        }
    }
    size_t last = 2 * n;
    if (best[last].cost < 0 || (best[last + 1].cost >= 0 && best[last + 1].cost < best[last].cost)) last++;
    if (best[last].cost < 0) return {};
    std::vector<KataPiece> pieces;
    for (size_t s = last; s != 0; s = best[s].from) {
        const size_t i = best[s].from / 2, j = s / 2;
        pieces.push_back({start(i), end(j), best[s].kind, Morae(h.substr(i, j - i)), best[s].tags});
    }
    std::reverse(pieces.begin(), pieces.end());
    return pieces;
}

// The length of the katakana name that kana ends in an honorific after (ヨウタサマ: 3), or
// 0: a name of the name layer only or no dictionary word, not after オ (オジサン) nor a
// run the document shows to be a name whole, before
// サマ サン クン チャン or ドノ, the whole run no dictionary word in katakana or hiragana.
size_t KataNameBeforeHonorific(std::u32string_view kana, const LineContext& ctx, const dict::Snapshot& snap) {
    for (const std::u32string_view h : {std::u32string_view(U"サマ"), std::u32string_view(U"サン"),
                                        std::u32string_view(U"クン"), std::u32string_view(U"チャン"),
                                        std::u32string_view(U"ドノ")}) {
        if (kana.size() < h.size() + 2 || !kana.ends_with(h)) continue;
        const std::u32string_view name = kana.substr(0, kana.size() - h.size());
        if (name[0] == U'オ' || name.back() == U'ー' || name.back() == U'ッ') return 0;
        // A name the document shows whole (サーラサン beside サーライチ and サーラニ).
        if (ctx.named != nullptr && ctx.named->contains(Utf8(kana))) return 0;
        // A word whole, in katakana or hiragana (タクサン たくさん, ネエサン ねえさん).
        if (!KeyTags(Utf8(kana), ctx, snap).empty() || !KeyTags(ToHiragana(Utf8(kana)), ctx, snap).empty()) return 0;
        // A name of the name layer only, or no word (タン of タンサン is a word).
        for (const dict::Candidate& c : snap.RetrieveRecords(Utf8(name), dict::LookupMode::General)) {
            if (c.codes != nullptr && !c.codes->sem.HasPrefix("QPN")) return 0;
        }
        return name.size();
    }
    return 0;
}

// kana read as Japanese words; empty when it does not. A katakana word of the base
// dictionary reads as words only when its hiragana is a word too or reads as a stem and
// its conjugation (コロス); one of a plain dictionary or a name registry never does,
// nor do laughter (アハハ, ウフフッ), doubled sounds (ネバネバ, バシッバシッ) and runs
// that end in ー, as loanwords do (スカイライナー). A run that starts with a katakana
// word reads as words only when its other words have three or more morae and do not
// conjugate (アリスデス, not ウルトラマンネクサス). One kana reads only as a particle
// (ハ, ヲ).
std::vector<KataPiece> ReadRun(std::u32string_view kana, const LineContext& ctx, const dict::Snapshot& snap) {
    std::u32string h;
    std::vector<size_t> at;
    Reading(kana, h, at);
    if (h.size() == 1) {
        if (FindFuncWord(h) != nullptr) return {{0, kana.size(), PieceKind::Func, 1, ""}};
        return {};
    }
    if (h.empty() || Laughter(h) || Doubled(h) || kana.back() == U'ー') return {};
    if (const size_t n = KataNameBeforeHonorific(kana, ctx, snap); n > 0) {
        // A name kept in katakana and its honorific in hiragana (ヨウタサマ ヨウタさま).
        return {{0, n, PieceKind::Kata, 0, ""}, {n, kana.size(), PieceKind::Word, 2, "N"}};
    }
    const std::string key = Utf8(kana);
    if (HasUserEntries(key, ctx)) return {};
    if (KeyTags(key, ctx, snap).empty()) {
        std::vector<KataPiece> p = SegmentRun(kana, h, at, ctx, snap);
        if (!p.empty() && p[0].kind == PieceKind::Kata &&
            std::any_of(p.begin(), p.end(), [](const KataPiece& x) {
                return (x.kind == PieceKind::Word && x.morae < 3) || x.kind == PieceKind::Conj;
            })) {
            return {};
        }
        return p;
    }
    if (std::string tags = KeyTags(Utf8(h), ctx, snap); !tags.empty()) {
        return {{0, kana.size(), PieceKind::Word, Morae(h), std::move(tags)}};
    }
    std::vector<KataPiece> p = SegmentRun(kana, h, at, ctx, snap);
    if (p.size() == 2 && p[0].kind == PieceKind::Word && p[1].kind == PieceKind::Conj) return p;
    return {};
}

}  // namespace

namespace {

// The katakana runs of line.
std::vector<KataRun> KataRuns(std::string_view line, const LineContext& ctx, const dict::Snapshot& snap) {
    std::vector<KataRun> runs;
    char32_t prev = 0;
    for (size_t i = 0; i < line.size();) {
        const text::Rune r = text::DecodeRune(line.substr(i));
        if (!IsKatakanaRune(r.cp)) {
            prev = r.cp;
            i += r.size;
            continue;
        }
        const size_t start = i;
        i += RunOf(line.substr(i), IsKatakanaRune);
        const char32_t next = text::DecodeRune(line.substr(i)).cp;
        KataRun run;
        run.start = start;
        run.end = i;
        run.kana = Runes(line.substr(start, i - start));
        if (ctx.prose != nullptr) {
            const std::string_view t = line.substr(start, i - start);
            const auto found = ctx.prose->find(t);
            const auto own = ctx.lineProse == nullptr ? 0 : std::count(ctx.lineProse->begin(), ctx.lineProse->end(), t);
            run.prose = found != ctx.prose->end() && found->second > own;
        }
        if (!IsJapaneseRune(prev) && !IsJapaneseRune(next)) run.pieces = ReadRun(run.kana, ctx, snap);
        runs.push_back(std::move(run));
    }
    return runs;
}

// Whether a line with japanese Japanese characters is speech written in katakana: the
// runs that read as words hold at least half of its Japanese characters and weigh at
// least two in grammar.
bool IsKataSpeech(int japanese, const std::vector<const KataRun*>& runs) {
    int speech = 0, evidence = 0;
    for (const KataRun* r : runs) {
        if (r->pieces.empty()) continue;
        speech += static_cast<int>(r->kana.size());
        evidence += Evidence(*r);
    }
    return evidence >= 2 && 2 * speech >= japanese;
}

// Whether a line written only in katakana and with the punctuation of speech (…、！？。)
// is speech when IsKataSpeech says no: two or more of its runs, of two or more kana,
// read as words, or one with grammar evidence next to a sound or phrase already written
// in Hangul (コレデ…シズメェッ！). A run of a name of the name layer or one joined to
// another by ・ or ＝ (トワ・エ・モワ) does not count, nor does one of katakana words
// only (ラブライブ).
bool IsPlainKataSpeech(const output::Pieces& line, const std::vector<const KataRun*>& runs,
                       const dict::Snapshot& snap) {
    std::string text;
    int sounds = 0;
    for (const output::Piece& p : line) {
        text += TextOf(p);
        if (p.span != output::SpanText) {
            bool syllable = false;
            for (std::string_view s = p.text; !s.empty() && !syllable;) {
                const text::Rune r = text::DecodeRune(s);
                syllable = r.cp >= U'가' && r.cp <= U'힣';
                s.remove_prefix(r.size);
            }
            if (syllable) sounds++;
        }
    }
    bool punctuated = false, joined = false;
    for (std::string_view s = text; !s.empty();) {
        const text::Rune r = text::DecodeRune(s);
        punctuated = punctuated || RuneIn(r.cp, U"…‥、！？。!?");
        joined = joined || RuneIn(r.cp, U"・＝");
        s.remove_prefix(r.size);
    }
    if (!punctuated || joined) return false;
    int words = 0, evidence = 0;
    for (const KataRun* r : runs) {
        if (r->pieces.empty() || r->kana.size() < 2 || !snap.NameRecords(Utf8(r->kana)).empty()) continue;
        if (std::none_of(r->pieces.begin(), r->pieces.end(), [](const KataPiece& p) { return p.kind != PieceKind::Kata; })) {
            continue;
        }
        words++;
        evidence += Evidence(*r);
    }
    return words >= 2 || (words == 1 && sounds > 0 && evidence > 0);
}

// The particles that speech in katakana writes after a kanji word (依頼者ハ, 工場カラ).
constexpr std::u32string_view kParticleRuns[] = {U"ハ",   U"ガ",   U"ヲ",   U"ニ",   U"ノ",   U"デ",   U"ト",
                                                 U"モ",   U"ヘ",   U"カラ", U"ヨリ", U"マデ", U"ニハ", U"デハ",
                                                 U"トハ", U"ニモ", U"デモ", U"ヘノ", U"カラハ"};

// Whether a katakana run after a kanji is its okurigana or particle: a particle of
// kParticleRuns, or a run without a dictionary sense that starts with a kana that
// may follow a stem (来マシタ, 困ッタ).
bool Okurigana(const KataRun& r, const LineContext& ctx, const dict::Snapshot& snap) {
    if (std::find(std::begin(kParticleRuns), std::end(kParticleRuns), r.kana) != std::end(kParticleRuns)) return true;
    if (!KeyTags(Utf8(r.kana), ctx, snap).empty()) return false;
    return RuneIn(r.kana[0] - 0x60, kConjKana) || r.kana[0] == U'ッ' || r.kana[0] == U'ヲ';
}

// Whether a line without hiragana writes its okurigana and particles in katakana
// (痛イ……沈マナイ, 目ヲ開ケテ): two or more katakana runs follow a kanji, start with a
// kana that may follow a stem or is a particle, and are not katakana words.
bool IsOkuriganaSpeech(const output::Pieces& line, const std::vector<std::vector<KataRun>>& runs,
                       const LineContext& ctx, const dict::Snapshot& snap) {
    int n = 0;
    for (size_t k = 0; k < line.size(); k++) {
        const std::string_view text = TextOf(line[k]);
        for (const KataRun& r : runs[k]) {
            if (IsKanjiRune(LastRune(text.substr(0, r.start)).cp) && Okurigana(r, ctx, snap)) n++;
        }
    }
    return n >= 2;
}

// ---- katakana words ----

// Pronouns, demonstratives and adjectives that fiction writes in katakana for
// emphasis; adjective marks the adjectives. Each is also a possible character name.
// オレ and オマエ are not here: the base dictionary reads them as 나 and 너, and おれ
// also reads as 折れ.
// A verb is written in its kanji form (as), which keeps it apart from the kana around
// it (出さないとコロス 出さないと殺す), and reads as a predicate like an adjective.
struct KataWord {
    std::string_view word;
    bool adjective;
    std::string_view as = {};
};

constexpr KataWord kKataWords[] = {
    {"アレ", false}, {"ソレ", false}, {"コレ", false}, {"ドレ", false}, {"ナニ", false},
    {"ダレ", false}, {"ココ", false}, {"ソコ", false}, {"ドコ", false}, {"アタシ", false},
    {"アンタ", false}, {"ダメ", true}, {"ヤバイ", true}, {"スゴイ", true}, {"カワイイ", true},
    {"ウルサイ", true}, {"ヒドイ", true}, {"コワイ", true}, {"ウザイ", true},
    {"コロス", true, "殺す"}, {"ブチコロス", true, "ぶっ殺す"}, {"シネ", true, "死ね"}, {"キエロ", true, "消えろ"},
    {"ユルサナイ", true, "許さない"}, {"ヤメテ", true, "やめて"}, {"タスケテ", true, "助けて"},
};

// The particles and predicates after which a word of kKataWords reads as the word; an
// adjective also reads as one before the end of a sentence. あれ？ is also the
// interjection "huh?", so アレ does not take ？.
constexpr std::string_view kPronounNext[] = {"は", "が", "を", "も", "じゃ", "だ", "です", "って", "か", "？", "?"};
constexpr std::string_view kAdjectiveNext[] = {"だ", "じゃ", "って", "か"};
// The sentence-final particles after which an i-adjective of kKataWords reads as one.
constexpr std::string_view kAdjectiveFinal[] = {"ぞ", "よ", "な", "ね", "わ"};
// What else follows a verb of kKataWords (コロスんだ, コロスから).
constexpr std::string_view kVerbNext[] = {"ん", "の", "から", "けど", "し", "ぜ"};

constexpr std::u32string_view kSentenceEnd = U"。！？!?…‥」』";

// The stem of 行く written in katakana with its conjugation in hiragana, as fiction writes
// the verb for climax (イっちゃう, イッた, イクの, イきそう, イかせて): the katakana run and the
// hiragana it must be followed by, and its kanji spelling. イケ is left out (イケる, イケてる).
struct KataStem {
    std::string_view run;
    std::string_view as;
    std::string_view next[12];
};

constexpr KataStem kKataStems[] = {
    {"イ", "行", {"った", "って", "っち", "っし", "く", "き", "か", "こ"}},
    {"イッ", "行っ", {"た", "て", "ち", "し"}},
    {"イク", "行く", {"の", "ん", "っ", "ぅ", "よ", "ね", "わ", "ぞ", "から", "まで", "時", "とき"}},
    {"イキ", "行き", {"そう", "そ", "ま", "た", "す", "っぱ", "続"}},
    {"イカ", "行か", {"せ", "され", "な"}},
    {"イコ", "行こ", {"う"}},
};

// The element of list that starts s.
template <size_t N>
bool PrefixIn(std::string_view s, const std::string_view (&list)[N], std::string_view& found) {
    for (const std::string_view p : list) {
        if (s.starts_with(p)) {
            found = p;
            return true;
        }
    }
    found = {};
    return false;
}

// The length of the longest key a lookup of s matches.
size_t Longest(std::string_view s, const LineContext& ctx, const dict::Snapshot& snap) {
    size_t n = 0;
    for (const dict::Candidate& c : snap.GetWordInfo(s, dict::LookupMode::General, ctx.lookup).list) {
        n = std::max(n, c.source.size());
    }
    return n;
}

// Whether the hiragana word w, with its follower, would join the text around it into
// a dictionary word: one that starts in the hiragana before it and reaches into w
// (からだめ, じゃあだめ), or one that starts with w and ends after its follower (あれだけ).
bool JoinsWord(std::string_view before, const std::string& w, std::string_view after, const LineContext& ctx,
               const dict::Snapshot& snap) {
    std::string_view tail = after.substr(0, 12);
    while (!tail.empty() && !ValidUtf8(tail)) tail.remove_suffix(1);
    const std::string word = w + std::string(tail);
    if (Longest(word, ctx, snap) > w.size()) return true;
    size_t k = before.size();
    for (int i = 0; i < 3; i++) {
        const text::Rune r = LastRune(before.substr(0, k));
        if (!IsHiraganaRune(r.cp) || r.size == 0 || r.size > k) break;
        k -= r.size;
        if (Longest(std::string(before.substr(k)) + word, ctx, snap) > before.size() - k) return true;
    }
    return false;
}

// Whether a plain dictionary (a name registry included) or the name layer gives the key
// s a sense in a lookup.
bool HasUserSense(std::string_view s, const LineContext& ctx, const dict::Snapshot& snap) {
    for (const dict::Candidate& c : snap.GetWordInfo(s, dict::LookupMode::General, ctx.lookup).list) {
        if (c.user != nullptr && c.user->key.size() == s.size()) return true;
    }
    return false;
}

// Whether a plain dictionary other than the name layer gives the key s a sense.
bool HasPlainSense(std::string_view s, const LineContext& ctx, const dict::Snapshot& snap) {
    for (const dict::Candidate& c : snap.GetWordInfo(s, dict::LookupMode::General, ctx.lookup).list) {
        if (c.user != nullptr && !c.user->nameLayer && c.user->key.size() == s.size()) return true;
    }
    return false;
}

// What may follow a noun: particles, the copula and honorifics (イカなんて, イカを).
constexpr std::string_view kNounNext[] = {"は", "が", "を", "に", "の", "で", "と", "へ", "も", "や", "か", "だ", "です",
                                          "じゃ", "なんて", "など", "なら", "なの", "なんか", "って", "より", "から",
                                          "まで", "さん", "ちゃん", "くん", "たち", "だけ", "しか", "ね", "よ"};

// How a dictionary word other than a name reads the katakana run word with rest, the text
// after it: a verb whose conjugation rest goes on with (イキった), or another word that rest
// leaves or continues with a particle (イカなんて). 0 for none, 1 for the text as written, 2
// when the first kana of rest is in katakana in the word (イかれてる: イカれ).
int KnownWord(std::string_view word, std::string_view rest, const LineContext& ctx, const dict::Snapshot& snap) {
    std::string spellings[2] = {std::string(word) + std::string(rest), {}};
    const text::Rune first = text::DecodeRune(rest);
    if (IsHiraganaRune(first.cp)) {
        spellings[1] = std::string(word);
        text::AppendRune(spellings[1], first.cp + 0x60);
        spellings[1] += rest.substr(first.size);
    }
    for (int k = 0; k < 2; k++) {
        const std::string& text = spellings[k];
        if (text.empty()) continue;
        const size_t least = k == 0 ? word.size() : word.size() + 3;  // a katakana is three bytes
        for (const dict::Candidate& c : snap.GetWordInfo(text, dict::LookupMode::General, ctx.lookup).list) {
            if (c.codes == nullptr || c.source.size() < least || (c.user != nullptr && c.user->nameLayer) ||
                c.codes->sem.HasPrefix("QPN")) {
                continue;
            }
            const std::string_view tail = std::string_view(text).substr(c.source.size());
            const std::string_view part = c.codes->partText;
            if (part.size() == 4 && part[0] == 'M') {
                const int kind = part[3] - '0';
                for (int branch = 0; branch < word::kConjugationBranches; branch++) {
                    const std::string_view conj = word::Conjugation(kind, branch);
                    if (!conj.empty() && conj.find_first_of("(X") == std::string_view::npos && tail.starts_with(conj)) {
                        return k + 1;
                    }
                }
                continue;
            }
            const text::Rune next = text::DecodeRune(tail);
            if (tail.empty() || !IsHiraganaRune(next.cp)) return k + 1;
            for (const std::string_view n : kNounNext) {
                if (tail.starts_with(n)) return k + 1;
            }
        }
    }
    return 0;
}

// The word a katakana run that ends in ッ before punctuation or the end clips (テメェッ！
// テメェ, バカッ！ バカ): the run without ッ when it is a dictionary word other than a name;
// empty otherwise.
std::string_view ClippedWord(std::string_view run, std::string_view rest, const LineContext& ctx,
                             const dict::Snapshot& snap) {
    constexpr std::string_view kTsu = "ッ";
    if (!run.ends_with(kTsu) || text::RuneCount(run) < 3) return {};
    const char32_t next = text::DecodeRune(rest).cp;
    if (next != 0 && !RuneIn(next, U"！？!?…‥。、」』）)～― 　")) return {};
    const std::string_view bare = run.substr(0, run.size() - kTsu.size());
    if (ctx.named != nullptr && ctx.named->contains(std::string(run))) return {};
    for (const dict::Candidate& c : snap.RetrieveRecords(bare, dict::LookupMode::General)) {
        if (c.codes != nullptr && !c.codes->sem.HasPrefix("QPN")) return bare;
    }
    return {};
}

// KataWordsToHiragana on one text piece; spanBefore and spanAfter report the spans
// around it.
std::string KataWordsIn(std::string_view text, bool spanBefore, bool spanAfter, const LineContext& ctx,
                        const dict::Snapshot& snap) {
    std::string b;
    size_t at = 0;
    for (size_t i = 0; i < text.size();) {
        const text::Rune c = text::DecodeRune(text.substr(i));
        if (!IsKatakanaRune(c.cp)) {
            i += c.size;
            continue;
        }
        const size_t start = i;
        i += RunOf(text.substr(i), IsKatakanaRune);
        const std::string_view word = text.substr(start, i - start);
        const char32_t prev = LastRune(text.substr(0, start)).cp;
        if (const std::string_view bare = ClippedWord(word, text.substr(i), ctx, snap); !bare.empty()) {
            b.append(text.substr(at, start - at));
            // A clipped adjective of kKataWords ends its sentence and reads as the word
            // (ダメッ！ だめ！).
            const KataWord* kw = nullptr;
            for (const KataWord& x : kKataWords) {
                if (x.word == bare && x.adjective) kw = &x;
            }
            if (kw != nullptr && prev != U'・' && prev != U'＝' && prev != U'の' && !HasUserSense(bare, ctx, snap) &&
                (ctx.named == nullptr || ctx.named->find(std::string(bare)) == ctx.named->end())) {
                b += kw->as.empty() ? ToHiragana(bare) : std::string(kw->as);
            } else {
                b += bare;
            }
            at = i;
            continue;
        }
        const KataStem* stem = nullptr;
        for (const KataStem& x : kKataStems) {
            if (x.run != word) continue;
            for (const std::string_view n : x.next) {
                if (!n.empty() && text.substr(i).starts_with(n)) stem = &x;
            }
        }
        const int known = stem != nullptr ? KnownWord(word, text.substr(i), ctx, snap) : 0;
        if (known == 2) {
            const text::Rune first = text::DecodeRune(text.substr(i));
            b.append(text.substr(at, i - at));
            text::AppendRune(b, first.cp + 0x60);
            i += first.size;
            at = i;
            continue;
        }
        if (stem != nullptr && known == 0 && prev != U'・' && prev != U'＝' && !(start == 0 && spanBefore) &&
            !HasPlainSense(word, ctx, snap) && (ctx.named == nullptr || ctx.named->find(word) == ctx.named->end())) {
            b.append(text.substr(at, start - at));
            b += stem->as;
            at = i;
            continue;
        }
        const std::string_view rest = text.substr(i);
        const char32_t next = text::DecodeRune(rest).cp;
        // A small ッ closing the run is the sound of the word, not part of it (ウルサイッ！).
        const bool tsu = word.size() > std::string_view("ッ").size() && word.ends_with("ッ");
        const std::string_view base = tsu ? word.substr(0, word.size() - std::string_view("ッ").size()) : word;
        const KataWord* kw = nullptr;
        for (const KataWord& x : kKataWords) {
            if (x.word == base && (!tsu || x.adjective)) kw = &x;
        }
        if (kw == nullptr) continue;
        std::string_view follower;
        bool ok;
        if (kw->adjective) {
            ok = PrefixIn(rest, kAdjectiveNext, follower) ||
                 ((word.ends_with("イ") || !kw->as.empty()) && PrefixIn(rest, kAdjectiveFinal, follower)) ||
                 (!kw->as.empty() && PrefixIn(rest, kVerbNext, follower));
            ok = ok || (rest.empty() && !spanAfter) || next == U'\n' || next == U'\r' || RuneIn(next, kSentenceEnd);
        } else {
            ok = PrefixIn(rest, kPronounNext, follower);
            ok = ok && !(word == "アレ" && RuneIn(next, U"？?"));
        }
        if (!ok || prev == U'・' || prev == U'＝' || prev == U'の' || (start == 0 && spanBefore)) continue;
        if (HasUserSense(word, ctx, snap) || HasUserSense(base, ctx, snap)) continue;
        if (ctx.named != nullptr &&
            (ctx.named->find(std::string(word)) != ctx.named->end() || ctx.named->find(std::string(base)) != ctx.named->end())) {
            continue;
        }
        const std::string hiragana = (kw->as.empty() ? ToHiragana(base) : std::string(kw->as)) + (tsu ? "っ" : "");
        const bool kanji = !kw->as.empty() && !IsHiraganaRune(text::DecodeRune(kw->as).cp);
        if (!kanji &&
            JoinsWord(text.substr(0, start), hiragana + std::string(follower), rest.substr(follower.size()), ctx, snap)) {
            continue;
        }
        b.append(text.substr(at, start - at));
        b += hiragana;
        at = i;
    }
    if (at == 0) return std::string(text);
    b.append(text.substr(at));
    return b;
}

}  // namespace

namespace {

// text with the katakana honorific that ends a katakana run after a name, written in
// hiragana (ヨウタサマ ヨウタさま).
std::string KataHonorificsIn(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    std::string b;
    size_t at = 0;
    for (size_t i = 0; i < text.size();) {
        const text::Rune r = text::DecodeRune(text.substr(i));
        if (!IsKatakanaRune(r.cp)) {
            i += std::max<size_t>(1, r.size);
            continue;
        }
        const size_t start = i;
        i += RunOf(text.substr(i), IsKatakanaRune);
        const std::u32string kana = Runes(text.substr(start, i - start));
        const size_t n = KataNameBeforeHonorific(kana, ctx, snap);
        if (n == 0) continue;
        b.append(text.substr(at, start - at));
        b += Utf8(std::u32string_view(kana).substr(0, n));
        b += ToHiragana(Utf8(std::u32string_view(kana).substr(n)));
        at = i;
    }
    if (at == 0) return std::string(text);
    b.append(text.substr(at));
    return b;
}

}  // namespace

output::Pieces KataHonorifics(const output::Pieces& text, const LineContext& ctx, const dict::Snapshot& snap) {
    output::Pieces out = text;
    for (output::Piece& p : out) {
        if (p.span == output::SpanText) p.text = KataHonorificsIn(p.text, ctx, snap);
    }
    return out;
}

output::Pieces KataSpeech(const output::Pieces& text, const LineContext& ctx, const dict::Snapshot& snap) {
    std::vector<output::Pieces> byLine = Lines(text);
    for (output::Pieces& line : byLine) {
        int kata = 0, japanese = 0, hira = 0;
        for (const output::Piece& p : line) {
            const std::string_view t = TextOf(p);
            kata += CountRunes(t, IsKatakanaRune);
            japanese += CountRunes(t, IsJapaneseRune);
            hira += CountRunes(t, IsHiraganaRune);
        }
        // Not speech whatever the runs read as, unless kanji words take their particles and
        // okurigana in katakana (発信源ハ、物置ノ、内部).
        if (2 * kata < japanese && hira > 0) continue;
        std::vector<std::vector<KataRun>> runs(line.size());
        std::vector<const KataRun*> all;
        for (size_t k = 0; k < line.size(); k++) {
            runs[k] = KataRuns(TextOf(line[k]), ctx, snap);
            for (const KataRun& r : runs[k]) all.push_back(&r);
        }
        // Two or more runs that read as words, all of which the document's prose writes,
        // are the document's katakana words, a list of names (サーラ、ミナ、サーライチ).
        int words = 0;
        bool prose = true;
        for (const KataRun* r : all) {
            if (r->pieces.empty()) continue;
            words++;
            prose = prose && r->prose;
        }
        if (words >= 2 && prose) continue;
        bool okurigana = false;
        if (IsKataSpeech(japanese, all)) {
        } else if (kata == japanese && IsPlainKataSpeech(line, all, snap)) {
        } else if (hira == 0 && IsOkuriganaSpeech(line, runs, ctx, snap)) {
            okurigana = true;
        } else {
            continue;
        }
        for (size_t k = 0; k < line.size(); k++) {
            std::string b;
            size_t at = 0;
            for (const KataRun& r : runs[k]) {
                std::string h;
                const std::string_view piece = TextOf(line[k]);
                const bool afterKanji = IsKanjiRune(LastRune(piece.substr(0, r.start)).cp);
                const bool honorific = (r.kana == U"オ" || r.kana == U"ゴ") && IsKanjiRune(text::DecodeRune(piece.substr(r.end)).cp);
                if (!r.pieces.empty()) {
                    h = RunHiragana(r);
                } else if ((okurigana && KeyTags(Utf8(r.kana), ctx, snap).empty()) ||
                           (afterKanji && Okurigana(r, ctx, snap)) || honorific) {
                    h = ToHiragana(Utf8(r.kana));
                } else {
                    continue;
                }
                b.append(line[k].text, at, r.start - at);
                b += h;
                at = r.end;
            }
            if (at > 0) {
                b.append(line[k].text, at);
                line[k].text = std::move(b);
            }
        }
    }
    return JoinLines(byLine);
}

output::Pieces KataWordsToHiragana(const output::Pieces& text, const LineContext& ctx, const dict::Snapshot& snap) {
    output::Pieces out = text;
    for (size_t k = 0; k < out.size(); k++) {
        if (out[k].span == output::SpanText) out[k].text = KataWordsIn(text[k].text, k > 0, k + 1 < text.size(), ctx, snap);
    }
    return out;
}

namespace {

constexpr std::string_view kNicknameHonorifics[] = {"ちゃん", "くん"};

// The kin, pet and other words before ちゃん the dictionary lacks in hiragana (いっちゃん 一番).
constexpr std::string_view kKinHeads[] = {"にい", "にー", "ねえ", "ねー", "かあ", "かー", "とう", "とー", "ばあ", "ばー",
                                          "じい", "じー", "ばっ", "じっ", "わん", "にゃん", "ぼっ", "じょう", "あか", "あん", "いっ"};

constexpr std::u32string_view kParticleRunes = U"はがをにともへでや";

bool NicknameRune(char32_t r) { return IsHiraganaRune(r) || r == U'ー'; }

// The start of the nickname that ends at end in text, or npos: two to four kana after a
// character other than kana or a kanji, or after a one-kana particle that follows a kanji,
// katakana or a span.
size_t NicknameStart(std::string_view text, size_t end, bool spanBefore) {
    size_t start = end;
    while (start > 0) {
        const text::Rune r = LastRune(text.substr(0, start));
        if (!NicknameRune(r.cp) || r.size == 0 || r.size > start) break;
        start -= r.size;
    }
    const auto runes = [&](size_t from) { return CountRunes(text.substr(from, end - from), [](char32_t) { return true; }); };
    const char32_t before = start == 0 ? 0 : LastRune(text.substr(0, start)).cp;
    const bool boundary = start == 0 || (!IsKanjiRune(before) && !IsKatakanaRune(before));
    if (start == end) return std::string_view::npos;
    if (boundary && runes(start) >= 2 && runes(start) <= 4) return start;
    const text::Rune first = text::DecodeRune(text.substr(start));
    const size_t after = start + first.size;
    if ((!boundary || (start == 0 && spanBefore)) && RuneIn(first.cp, kParticleRunes) && runes(after) >= 2 && runes(after) <= 4) {
        return after;
    }
    return std::string_view::npos;
}

void NicknamesIn(std::string_view text, bool spanBefore, const LineContext& ctx, const dict::Snapshot& snap, output::Pieces& out) {
    size_t at = 0;
    for (size_t i = 0; i < text.size();) {
        std::string_view honorific;
        for (const std::string_view h : kNicknameHonorifics) {
            if (text.substr(i).starts_with(h)) honorific = h;
        }
        if (honorific.empty()) {
            i += std::max<size_t>(1, text::DecodeRune(text.substr(i)).size);
            continue;
        }
        const size_t end = i;
        i += honorific.size();
        const size_t start = NicknameStart(text, end, spanBefore && at == 0);
        if (start == std::string_view::npos || start < at) continue;
        const std::string_view name = text.substr(start, end - start);
        const std::string_view rest = text.substr(i);
        if (name.starts_with("お") || name.starts_with("ご") || name.starts_with("ー")) continue;
        bool kin = false;
        for (const std::string_view k : kKinHeads) kin = kin || name == k;
        if (kin || (honorific == "ちゃん" && rest.starts_with("と"))) continue;  // ここにちゃんと
        // An animal called with the honorific is the animal (ねこちゃん 고양이).
        bool animal = false;
        for (const dict::Candidate& c : snap.RetrieveRecords(name, dict::LookupMode::General)) {
            animal = animal || (c.codes != nullptr && c.codes->sem.Text().find("QAA") != std::string_view::npos);
        }
        if (animal) continue;
        // A verb before んだ, んで or んな (叩くんじゃねえ, しとくんで, 狙ってくんな), not a name
        // in っ (あっくん).
        if (honorific == "くん") {
            const bool predicate = rest.starts_with("じゃ") || rest.starts_with("だ") || rest.starts_with("で") ||
                                   rest.starts_with("な") || rest.starts_with("の");
            const char32_t last = LastRune(name).cp;
            bool verb = predicate && RuneIn(last, U"てでとっ");
            // A verb stem of two kana or more at the end of the name (まだあるくんだ).
            for (size_t from = 0; predicate && !verb && from < name.size();
                 from += std::max<size_t>(1, text::DecodeRune(name.substr(from)).size)) {
                const std::string_view stem = name.substr(from);
                verb = CountRunes(stem, [](char32_t) { return true; }) >= 2 &&
                       KeyTags(stem, ctx, snap).find('M') != std::string::npos;
            }
            if (verb) continue;
        }
        // A dictionary word ending in the honorific, with up to four characters after it
        // (どんちゃん騒ぎ, 群がるおばちゃん).
        bool word = false;
        for (size_t from = 0; from < name.size() && !word; from += std::max<size_t>(1, text::DecodeRune(name.substr(from)).size)) {
            for (size_t n = 0, e = 0; n <= 4 && !word; n++) {
                const std::string key = std::string(name.substr(from)) + std::string(honorific) + std::string(rest.substr(0, e));
                word = !KeyTags(key, ctx, snap).empty();
                if (e >= rest.size()) break;
                e += std::max<size_t>(1, text::DecodeRune(rest.substr(e)).size);
            }
        }
        if (word) continue;
        // Long vowels are dropped as a Korean name drops them (ゆうくん 유군, みーちゃん 미짱).
        std::string kana(name);
        for (size_t p; (p = kana.find("ー")) != std::string::npos;) kana.erase(p, std::string_view("ー").size());
        const std::string hangul = text::FromKana(kana, text::Preset::FanShort);
        if (hangul.empty()) continue;
        if (start > at) output::Add(out, text.substr(at, start - at));
        out.push_back({hangul, output::SpanWord});
        at = end;
    }
    if (at < text.size()) output::Add(out, text.substr(at));
}

}  // namespace

output::Pieces NicknamesToHangul(const output::Pieces& text, const LineContext& ctx, const dict::Snapshot& snap) {
    output::Pieces out;
    for (size_t k = 0; k < text.size(); k++) {
        if (text[k].span == output::SpanText) NicknamesIn(text[k].text, k > 0, ctx, snap, out);
        else out.push_back(text[k]);
    }
    return out;
}

std::string KeyTags(std::string_view s, const LineContext& ctx, const dict::Snapshot& snap) {
    std::string tags;
    for (const dict::Candidate& c : snap.RetrieveRecords(s, dict::LookupMode::General)) {
        if (c.codes != nullptr && !c.codes->part.Empty()) tags += c.codes->part.Tag();
    }
    ForUserEntries(s, ctx, [&](const dict::UserEntry& e) { tags += e.Part().Tag(); });
    return tags;
}

}  // namespace hdTrans::pipeline::internal
