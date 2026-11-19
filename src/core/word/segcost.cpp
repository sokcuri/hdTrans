// The segmentation lattice (LEX-6720..6739, spec/words.md).
//
// A token read as one best path over a lattice: an edge for every dictionary candidate the word
// search itself sees at every character (Worker::WordInfo, the same name and kana gates), conjugated
// verbs and adjectives spanning their ending (InsertLateCandidate on the record alone), a run of
// Latin letters as one word, and fallback edges for unknown text. Every edge costs a sum of named,
// hand-set terms from the dictionary file of the layer seg-costs (dict/seg_costs.h); the cheapest path
// wins, and a tie keeps the order of the word search.
//
// Three uses:
// - LatticeKatakana: at a cursor inside a katakana run of 3 to 24 kana, the lattice of the run picks
//   the span the search reads first (in place of the order KatakanaRunWhole and KatakanaCoverFirst
//   give); elsewhere the search decides alone.
// - SplitNote (--why): per token the path the search took with the cost terms of each edge, and the
//   best path with another split.
// - ShadowSegment (HDTRANS_SEG_SHADOW=<file>): writes <file>.<pid>, one row per window where the
//   lattice splits more cheaply than the search, with the family of the window; reads only.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "core/text/unicode.h"
#include "core/word/strings.h"
#include "core/word/util.h"
#include "core/word/worker.h"

namespace hdTrans::word {

using dict::SegCost;
using dict::SegCosts;

struct SegEdge {
    SegEdge(int to, int price) : end(to), cost(price) {}
    int end;
    int cost;
    int key = 0;                        // the length of the dictionary key (a verb's stem), 0 for none
    std::string_view part;              // the part code of a dictionary edge
    const dict::UserEntry* user = nullptr;
    bool word = false;                  // a dictionary word (not unknown text read by sound)
    bool particle = false;              // a particle spelled in kana
    std::string label;                  // span/code=gloss with the cost terms, only for --why and the shadow
};

// The lattice of a whole token and the search's path through it (SegTokenLattice).
struct SegTokenPaths {
    std::vector<std::vector<SegEdge>> edges;
    std::vector<int> bounds;                 // the search's path: 0 first, the size last
    std::vector<int> costs;
    std::vector<std::string> labels;
    std::vector<std::pair<int, int>> refs;   // where each item of the search sits in edges ({-1, -1} for a gap)
};

namespace {

constexpr int kInf = std::numeric_limits<int>::max() / 4;
constexpr int kMaxRun = 24;  // the longest katakana run the lattice decides, as the katakana reorders

int CharLen(std::string_view s, int p) {
    const int n = static_cast<int>(FirstChar(s.substr(static_cast<std::size_t>(p))).size());
    return n > 0 ? n : 1;
}

int Chars(std::string_view s) {
    int n = 0;
    for (int p = 0; p < static_cast<int>(s.size()); p += CharLen(s, p)) n++;
    return n;
}

char32_t RuneAt(std::string_view s, int p) {
    return p >= 0 && p < static_cast<int>(s.size()) ? text::DecodeRune(s.substr(static_cast<std::size_t>(p))).cp : 0;
}

// The start of the rune that ends at p.
int RuneBefore(std::string_view s, int p) {
    int q = p - 1;
    while (q > 0 && (static_cast<unsigned char>(s[static_cast<std::size_t>(q)]) & 0xC0) == 0x80) q--;
    return q;
}

bool KataRune(char32_t r) { return (r >= U'ァ' && r <= U'ヶ') || r == U'ー'; }

bool LatinRune(char32_t r) {
    return (r >= U'A' && r <= U'Z') || (r >= U'a' && r <= U'z') || (r >= U'Ａ' && r <= U'Ｚ') || (r >= U'ａ' && r <= U'ｚ');
}

bool AllOf(std::string_view span, bool (*pred)(char32_t)) {
    for (int p = 0; p < static_cast<int>(span.size()); p += CharLen(span, p)) {
        if (!pred(RuneAt(span, p))) return false;
    }
    return !span.empty();
}

bool AllHiragana(std::string_view span) {
    for (int p = 0; p < static_cast<int>(span.size()); p += CharLen(span, p)) {
        if (NameClassOf(span.substr(static_cast<std::size_t>(p))) != kClassHiragana) return false;
    }
    return !span.empty();
}

// The katakana run of s around p: [start, end).
std::pair<int, int> KataRunAt(std::string_view s, int p) {
    int a = p, b = p;
    while (a > 0 && KataRune(RuneAt(s, RuneBefore(s, a)))) a = RuneBefore(s, a);
    while (b < static_cast<int>(s.size()) && KataRune(RuneAt(s, b))) b += CharLen(s, b);
    return {a, b};
}

// Whether the katakana at p is a ー that starts or ends its run: a mark, not a kana of a word.
bool EdgeMark(std::string_view s, int p) {
    if (RuneAt(s, p) != U'ー') return false;
    const int next = p + CharLen(s, p);
    return p == 0 || !KataRune(RuneAt(s, RuneBefore(s, p))) || !KataRune(RuneAt(s, next));
}

// Whether the word at [p, end) of s has katakana of its run right before or after it (a ー that
// starts or ends the run does not count).
bool InKataRun(std::string_view s, int p, int end) {
    if (end < static_cast<int>(s.size()) && KataRune(RuneAt(s, end)) && !EdgeMark(s, end)) return true;
    if (p > 0) {
        const int q = RuneBefore(s, p);
        if (KataRune(RuneAt(s, q)) && !EdgeMark(s, q)) return true;
    }
    return false;
}

class Cost {
public:
    Cost(const SegCosts& table, std::string* terms) : table_(table), terms_(terms) {}
    void Add(SegCost c, int times = 1) {
        const int v = table_[c] * times;
        total_ += v;
        if (terms_ == nullptr) return;
        if (!terms_->empty()) *terms_ += ' ';
        *terms_ += dict::kSegCostNames[static_cast<std::size_t>(c)];
        if (times != 1) *terms_ += "x" + std::to_string(times);
        *terms_ += ' ';
        *terms_ += std::to_string(v);
    }
    int Total() const { return total_; }

private:
    const SegCosts& table_;
    std::string* terms_;
    int total_ = 0;
};

// The cost of the word at [p, end) of s; tag is the major part tag.
int WordCost(const SegCosts& t, std::string_view s, int p, int end, char tag, bool nameRow, bool kanaRow, bool proper,
             std::string* terms) {
    Cost c(t, terms);
    const std::string_view span = s.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(end - p));
    if (AllOf(span, [](char32_t r) { return r == U'ー'; }) && EdgeMark(s, p)) {
        c.Add(dict::kSegLongMark);
        return c.Total();
    }
    const bool kata = AllOf(span, KataRune);
    const int n = Chars(span);
    if (kata && n >= 3) c.Add(dict::kSegKataLoan);
    else c.Add(dict::kSegWord);
    // A stray piece of a longer run: one kana, or a proper noun of two (LEX-95, LEX-7100).
    if (kata && (n == 1 || (n == 2 && proper)) && InKataRun(s, p, end)) c.Add(dict::kSegShortKataPiece);
    if (proper && !nameRow && !kata) c.Add(dict::kSegProper);
    if (tag == 'P' && AllHiragana(span)) c.Add(dict::kSegParticle);
    if (n == 1) {
        const int cls = NameClassOf(span);
        if (cls == kClassKanji) c.Add(dict::kSegLoneKanji);
        static constexpr std::string_view kContent = "NGEKCI";
        if (cls == kClassHiragana && kContent.find(tag) != std::string_view::npos) c.Add(dict::kSegLoneKana);
    }
    if (nameRow) c.Add(dict::kSegNameRow);
    if (kanaRow) c.Add(dict::kSegKanaRow);
    return c.Total();
}

int SoundRunCost(const SegCosts& t, std::string_view span, std::string* terms) {
    Cost c(t, terms);
    c.Add(dict::kSegSoundRun);
    c.Add(dict::kSegSoundChar, Chars(span));
    return c.Total();
}

int UnknownCost(const SegCosts& t, std::string_view ch, std::string* terms) {
    Cost c(t, terms);
    c.Add(KataRune(RuneAt(ch, 0)) ? dict::kSegKataSound : dict::kSegUnknownChar);
    return c.Total();
}

// The cost of text no dictionary word read (an unknown item of the search).
int UnreadCost(const SegCosts& t, std::string_view span, std::string* terms) {
    if (AllOf(span, LatinRune)) {
        Cost c(t, terms);
        c.Add(dict::kSegLatinRun);
        return c.Total();
    }
    if (Chars(span) == 1) return UnknownCost(t, span, terms);
    if (AllOf(span, KataRune)) return SoundRunCost(t, span, terms);
    Cost c(t, terms);
    c.Add(dict::kSegUnknownChar, Chars(span));
    return c.Total();
}

std::string Label(std::string_view span, std::string_view code, char layer, std::string_view gloss, std::string_view terms) {
    std::string s(span);
    s += '/';
    s += code;
    if (layer != 0) s += layer;
    s += '=';
    s += gloss.substr(0, 24);
    s += " (";
    s += terms;
    s += ')';
    for (char& ch : s) {
        if (ch == '\t' || ch == '\n') ch = ' ';
    }
    return s;
}

// A word whose span the lattice also reads as word + kana particle (+ word), or particle + word,
// costs no less than the cheapest reading of its span by those pieces: a row does not save the cost
// of the particle it holds, so on its own it ties with its pieces and the search's order stands.
void AddSeams(std::string_view s, std::vector<std::vector<SegEdge>>& edges, int base, int from, int to, bool detail) {
    auto at = [&](int p) -> std::vector<SegEdge>& { return edges[static_cast<std::size_t>(p - base)]; };
    auto wordTo = [&](int a, int b) {
        if (a == b) return false;
        for (const SegEdge& x : at(a)) {
            if (x.word && !x.particle && x.end == b) return true;
        }
        return false;
    };
    std::vector<std::pair<SegEdge*, int>> seams;  // the edge and the cost of its pieces
    for (int p = from; p < to; p++) {
        for (SegEdge& x : at(p)) {
            if (!x.word || Chars(s.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(x.end - p))) < 2) continue;
            bool seam = false;
            for (int m = p; m < x.end && !seam && m < to; m++) {
                const bool before = m > p && wordTo(p, m);
                if (m > p && !before) continue;
                for (const SegEdge& y : at(m)) {
                    if (!y.particle || y.end > x.end || (m == p && y.end == x.end) || &y == &x) continue;
                    if (y.end != x.end && !wordTo(y.end, x.end)) continue;
                    if (before || y.end != x.end) {
                        seam = true;
                        break;
                    }
                }
            }
            if (!seam) continue;
            // The cheapest reading of [p, x.end) by edges inside it that are shorter than the span.
            std::vector<int> best(static_cast<std::size_t>(x.end - p) + 1, kInf);
            best[0] = 0;
            for (int q = p; q < x.end; q++) {
                if (best[static_cast<std::size_t>(q - p)] >= kInf) continue;
                for (const SegEdge& y : at(q)) {
                    if (y.end > x.end || (q == p && y.end == x.end)) continue;
                    int& slot = best[static_cast<std::size_t>(y.end - p)];
                    slot = std::min(slot, best[static_cast<std::size_t>(q - p)] + y.cost);
                }
            }
            seams.emplace_back(&x, best.back());
        }
    }
    for (auto& [x, pieces] : seams) {
        if (pieces >= kInf || pieces <= x->cost) continue;
        if (detail) x->label.insert(x->label.size() - 1, " particle-seam " + std::to_string(pieces - x->cost));
        x->cost = pieces;
    }
}

struct Sink {
    std::FILE* f = nullptr;
    std::atomic<long> tokens{0}, differ{0}, ties{0}, differTokens{0}, kataDiffer{0}, kataTies{0};
    Sink() {
        const char* path = std::getenv("HDTRANS_SEG_SHADOW");
        if (path == nullptr || *path == '\0') return;
        const std::string p = std::string(path) + "." + std::to_string(::getpid());
        f = std::fopen(p.c_str(), "w");
    }
    ~Sink() {
        if (f == nullptr) return;
        std::fprintf(f, "#TOTAL\ttokens\t%ld\tdiffer\t%ld\tabstain\t%ld\tdiffer-tokens\t%ld\tkata-differ\t%ld\tkata-abstain\t%ld\n",
                     tokens.load(), differ.load(), ties.load(), differTokens.load(), kataDiffer.load(), kataTies.load());
        std::fclose(f);
    }
};

Sink& TheSink() {
    static Sink sink;
    return sink;
}

}  // namespace

bool SegShadowOn() { return TheSink().f != nullptr; }

// The edges at p of s: every dictionary candidate there, one unknown character, a run of Latin
// letters from its start, and a katakana run from its start read whole by its sound.
void Worker::SegEdgesAt(std::string_view s, int p, int token, bool detail, std::vector<SegEdge>& out) {
    const SegCosts& t = snapshot.SegCostTable();
    const int size = static_cast<int>(s.size());
    const std::string_view rest = s.substr(static_cast<std::size_t>(p));
    std::string terms;
    std::string* const tp = detail ? &terms : nullptr;
    const dict::Candidates found = WordInfo(rest, kModeGeneral);
    for (const dict::Candidate& c : found.list) {
        if (c.source.empty() || c.codes == nullptr || !rest.starts_with(c.source)) continue;
        const std::string_view part = c.codes->partText;
        if (part.size() != 4 || part[1] == '5') continue;
        const bool nameRow = c.user != nullptr && c.user->nameLayer;
        const bool kanaRow = c.user != nullptr && c.user->kana;
        int end = p + static_cast<int>(c.source.size());
        bool stem = false;
        if (part[1] == '4' || part[1] == '7') {
            Record* r = NewRecord();
            r->source = c.source;
            r->target.assign(c.target);
            r->partCode = part;
            r->semantic = c.codes->sem.Text();
            r->attr.assign(c.codes->gram.Text());
            r->dictCode = c.codes->dict.Text();
            r->ruleNo = c.rule;
            r->id = c.id;
            r->user = c.user;
            if (const Item* v = InsertLateCandidate(s, p, token, r); v != nullptr && v->start == p && v->length > 0) {
                end = p + v->length;
            } else {
                stem = true;
            }
        }
        if (end > size) continue;
        terms.clear();
        int cost = WordCost(t, s, p, end, part[0], nameRow, kanaRow, part == "N130", tp);
        if (stem) {
            Cost extra(t, tp);
            extra.Add(dict::kSegStemOnly);
            cost += extra.Total();
        }
        SegEdge e(end, cost);
        e.key = static_cast<int>(c.source.size());
        e.part = part;
        e.user = c.user;
        e.word = true;
        e.particle = part[0] == 'P' && AllHiragana(s.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(end - p)));
        if (detail) e.label = Label(s.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(end - p)), part, nameRow ? 'n' : kanaRow ? 'k' : 0, c.target, terms);
        // A single kana that ends the run after a katakana word belongs to that word (ジャーニー|ズ,
        // LEX-95): the word with it costs what the word costs.
        const bool joins = end < size && KataRune(RuneAt(s, end)) && AllOf(s.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(end - p)), KataRune) &&
                           !KataRune(RuneAt(s, end + CharLen(s, end)));
        if (joins) {
            SegEdge j = e;
            j.end = end + CharLen(s, end);
            if (detail) j.label.insert(j.label.size() - 1, " +kana");
            out.push_back(std::move(j));
        }
        out.push_back(std::move(e));
    }
    const int one = CharLen(s, p);
    const std::string_view ch = rest.substr(0, static_cast<std::size_t>(one));
    terms.clear();
    {
        SegEdge e(p + one, UnknownCost(t, ch, tp));
        if (detail) e.label = Label(ch, "unk", 0, "", terms);
        out.push_back(std::move(e));
    }
    const char32_t r = RuneAt(s, p);
    const char32_t before = p > 0 ? RuneAt(s, RuneBefore(s, p)) : 0;
    if (LatinRune(r) && !LatinRune(before)) {
        int q = p;
        while (q < size && LatinRune(RuneAt(s, q))) q += CharLen(s, q);
        terms.clear();
        SegEdge e(q, UnreadCost(t, rest.substr(0, static_cast<std::size_t>(q - p)), tp));
        if (detail) e.label = Label(rest.substr(0, static_cast<std::size_t>(q - p)), "lat", 0, "", terms);
        out.push_back(std::move(e));
    }
    // From the start of the run, or from a kana inside it that can start a word (no ー, ン or small
    // kana): the rest of a run after a word is read whole by its sound (ドクター|ゴフマン).
    static constexpr std::u32string_view kBound = U"ーンァィゥェォッャュョヮヵヶ";
    if (KataRune(r) && (!KataRune(before) || kBound.find(r) == std::u32string_view::npos)) {
        int q = p;
        while (q < size && KataRune(RuneAt(s, q))) q += CharLen(s, q);
        if (q - p > one) {
            terms.clear();
            SegEdge e(q, SoundRunCost(t, rest.substr(0, static_cast<std::size_t>(q - p)), tp));
            if (detail) e.label = Label(rest.substr(0, static_cast<std::size_t>(q - p)), "snd", 0, "", terms);
            out.push_back(std::move(e));
        }
    }
}

// The edges of the whole token for --why and the shadow, the search's own items among them, and
// the search's path through them; false where the search left the token in pieces outside it.
bool Worker::SegTokenLattice(int token, std::string_view surface, const Item* mark, SegTokenPaths& out) {
    const SegCosts& t = snapshot.SegCostTable();
    const int size = static_cast<int>(surface.size());
    auto& edges = out.edges;
    edges.assign(static_cast<std::size_t>(size) + 1, {});
    out.bounds = {0};
    out.costs.clear();
    out.labels.clear();
    out.refs.clear();
    int at = 0;
    std::string terms;
    for (const Item* it = mark != nullptr ? mark->next : chain.head; it != nullptr; it = it->next) {
        if (it->tokenIndex != token || it->length <= 0 || it->text.size() != surface.size()) continue;
        if (it->start < at || it->start + it->length > size) continue;
        if (it->start > at) {
            terms.clear();
            const int gap = UnreadCost(t, surface.substr(static_cast<std::size_t>(at), static_cast<std::size_t>(it->start - at)), &terms);
            out.costs.push_back(gap);
            out.refs.emplace_back(-1, -1);
            out.bounds.push_back(it->start);
            out.labels.push_back(Label(surface.substr(static_cast<std::size_t>(at), static_cast<std::size_t>(it->start - at)), "gap", 0, "", terms));
        }
        const std::string_view span = surface.substr(static_cast<std::size_t>(it->start), static_cast<std::size_t>(it->length));
        const Entry* e = it->group != nullptr && !it->group->entries.empty() ? it->group->entries.front() : nullptr;
        terms.clear();
        int cost;
        std::string code;
        char layer = 0;
        bool word = false, particle = false;
        if (e == nullptr) {
            Cost c(t, &terms);
            c.Add(dict::kSegWord);
            cost = c.Total();
        } else if (e->field.empty() && Has(e->gattr, "Nm") && e->user == nullptr) {
            cost = UnreadCost(t, span, &terms);
            code = "snd";
        } else {
            const char tag = e->cls;
            const bool nameRow = e->user != nullptr && e->user->nameLayer;
            const bool kanaRow = e->user != nullptr && e->user->kana;
            cost = WordCost(t, surface, it->start, it->start + it->length, tag, nameRow, kanaRow, tag == 'N' && e->code == "130", &terms);
            code = std::string(1, tag ? tag : '?') + std::string(e->code);
            layer = nameRow ? 'n' : kanaRow ? 'k' : 0;
            word = true;
            particle = tag == 'P' && AllHiragana(span);
        }
        out.costs.push_back(cost);
        at = it->start + it->length;
        out.bounds.push_back(at);
        out.labels.push_back(Label(span, code, layer, e != nullptr ? std::string_view(e->dst) : "", terms));
        out.refs.emplace_back(it->start, static_cast<int>(edges[static_cast<std::size_t>(it->start)].size()));
        SegEdge x(at, cost);
        x.word = word;
        x.particle = particle;
        x.label = out.labels.back();
        edges[static_cast<std::size_t>(it->start)].push_back(std::move(x));
    }
    if (at != size) return false;
    for (int p = 0; p < size; p += CharLen(surface, p)) SegEdgesAt(surface, p, token, true, edges[static_cast<std::size_t>(p)]);
    AddSeams(surface, edges, 0, 0, size, true);
    for (std::size_t k = 0; k < out.refs.size(); k++) {
        if (out.refs[k].first < 0) continue;
        const SegEdge& x = edges[static_cast<std::size_t>(out.refs[k].first)][static_cast<std::size_t>(out.refs[k].second)];
        out.costs[k] = x.cost;
        out.labels[k] = x.label;
    }
    return true;
}

namespace {

struct Path {
    int cost = kInf;
    std::vector<int> bounds;  // 0 first, the size last
    std::vector<const SegEdge*> edges;
};

// The cheapest path and the cheapest one that splits otherwise (each span read by its cheapest edge).
std::pair<Path, Path> TwoBest(std::string_view s, const std::vector<std::vector<SegEdge>>& edges) {
    const int size = static_cast<int>(s.size());
    struct Back {
        int cost = kInf;
        int from = -1, rank = 0;
        const SegEdge* edge = nullptr;
    };
    std::vector<std::array<Back, 2>> best(static_cast<std::size_t>(size) + 1);
    best[0][0].cost = 0;
    for (int p = 0; p < size; p++) {
        // The cheapest edge of each end from p.
        std::vector<const SegEdge*> cheapest(static_cast<std::size_t>(size) + 1, nullptr);
        for (const SegEdge& e : edges[static_cast<std::size_t>(p)]) {
            const SegEdge*& c = cheapest[static_cast<std::size_t>(e.end)];
            if (c == nullptr || e.cost < c->cost) c = &e;
        }
        for (int e = p + 1; e <= size; e++) {
            const SegEdge* x = cheapest[static_cast<std::size_t>(e)];
            if (x == nullptr) continue;
            for (int k = 0; k < 2; k++) {
                const Back& b = best[static_cast<std::size_t>(p)][static_cast<std::size_t>(k)];
                if (b.cost >= kInf) continue;
                const Back n{b.cost + x->cost, p, k, x};
                auto& slot = best[static_cast<std::size_t>(e)];
                if (n.cost < slot[0].cost) {
                    slot[1] = slot[0];
                    slot[0] = n;
                } else if (n.cost < slot[1].cost) {
                    slot[1] = n;
                }
            }
        }
    }
    auto trace = [&](int rank) {
        Path path;
        const Back& last = best[static_cast<std::size_t>(size)][static_cast<std::size_t>(rank)];
        if (last.cost >= kInf) return path;
        path.cost = last.cost;
        int e = size, k = rank;
        while (e > 0) {
            const Back& b = best[static_cast<std::size_t>(e)][static_cast<std::size_t>(k)];
            path.bounds.push_back(e);
            path.edges.push_back(b.edge);
            e = b.from;
            k = b.rank;
        }
        path.bounds.push_back(0);
        std::reverse(path.bounds.begin(), path.bounds.end());
        std::reverse(path.edges.begin(), path.edges.end());
        return path;
    };
    return {trace(0), trace(1)};
}

std::string Joined(const std::vector<std::string>& v, std::size_t a, std::size_t b) {
    std::string out;
    for (std::size_t k = a; k < b; k++) out += (out.empty() ? "" : " | ") + v[k];
    return out;
}

}  // namespace

void Worker::SegReport(int token, std::string_view surface, const Item* mark) {
    const SegCosts& t = snapshot.SegCostTable();
    const bool decided = segDecided_;
    segDecided_ = false;
    Sink& sink = TheSink();
    const bool why = settings.splitWhy;
    if (!t.loaded || surface.empty() || (sink.f == nullptr && !why)) return;
    SegTokenPaths paths;
    if (!SegTokenLattice(token, surface, mark, paths)) return;
    const auto [first, second] = TwoBest(surface, paths.edges);
    int oldCost = 0;
    for (const int c : paths.costs) oldCost += c;
    if (why) {
        // The path the search took, and the best path that splits otherwise.
        const Path& other = first.bounds == paths.bounds ? second : first;
        std::string note(surface);
        note += decided ? "\tlattice " : "\tsearch ";
        note += std::to_string(oldCost) + ": " + Joined(paths.labels, 0, paths.labels.size());
        if (other.cost < kInf) {
            std::vector<std::string> labels;
            for (const SegEdge* e : other.edges) labels.push_back(e->label);
            note += other.cost < oldCost ? "\tcheaper " : "\trejected ";
            note += std::to_string(other.cost) + ": " + Joined(labels, 0, labels.size());
        }
        splitNotes.push_back(std::move(note));
    }
    if (sink.f == nullptr) return;
    sink.tokens++;
    if (first.cost >= oldCost) return;
    std::vector<int> newCosts;
    std::vector<std::string> newLabels;
    for (const SegEdge* e : first.edges) {
        newCosts.push_back(e->cost);
        newLabels.push_back(e->label);
    }
    const auto& oldBounds = paths.bounds;
    const auto& newBounds = first.bounds;
    // The windows between the bounds both paths share. The best path is no dearer than the old one
    // in any window (else the old edges would make it cheaper), so a window that splits otherwise
    // is either cheaper (a disagreement) or as dear (the costs do not decide: an abstention).
    std::string chunk;
    std::size_t i = 0, j = 0;
    bool any = false;
    while (i < paths.costs.size() && j < newCosts.size()) {
        const std::size_t i0 = i, j0 = j;
        int oc = paths.costs[i++], nc = newCosts[j++];
        while (oldBounds[i] != newBounds[j]) {
            if (oldBounds[i] < newBounds[j]) oc += paths.costs[i++];
            else nc += newCosts[j++];
        }
        if (i - i0 == 1 && j - j0 == 1) continue;  // the same span, perhaps another reading of it
        // The family: a window that touches a katakana run of 3 to 24 kana.
        bool kata = false;
        for (int p = oldBounds[i0]; p < oldBounds[i] && !kata; p += CharLen(surface, p)) {
            if (!KataRune(RuneAt(surface, p))) continue;
            const auto [a, b] = KataRunAt(surface, p);
            const int n = Chars(surface.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a)));
            kata = n >= 3 && n <= kMaxRun;
        }
        if (nc >= oc) {
            sink.ties++;
            if (kata) sink.kataTies++;
            continue;
        }
        sink.differ++;
        if (kata) sink.kataDiffer++;
        any = true;
        if (chunk.empty()) {
            for (int k = 0; k < TokenCount(); k++) chunk += Tok(k).text;
            for (char& ch : chunk) {
                if (ch == '\t' || ch == '\n') ch = ' ';
            }
        }
        const std::string_view window = surface.substr(static_cast<std::size_t>(oldBounds[i0]), static_cast<std::size_t>(oldBounds[i] - oldBounds[i0]));
        std::fprintf(sink.f, "%s\t%.*s\t%.*s\t%d\t%s\t%d\t%s\t%s\n", chunk.c_str(), static_cast<int>(surface.size()), surface.data(),
                     static_cast<int>(window.size()), window.data(), oc, Joined(paths.labels, i0, i).c_str(), nc,
                     Joined(newLabels, j0, j).c_str(), kata ? "kata" : "-");
    }
    if (any) sink.differTokens++;
}

}  // namespace hdTrans::word
