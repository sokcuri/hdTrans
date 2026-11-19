// The segmentation lattice (LEX-6720..6722, spec/words.md).
//
// A token read again as one best path over a lattice: an edge for every dictionary candidate
// Worker::WordInfo returns at every character, conjugated verbs and adjectives spanning their ending
// (InsertLateCandidate on the record alone), a run of Latin letters as one word, and fallback edges
// for unknown text. Every edge costs a sum of named, hand-set terms from the dictionary file of the
// layer seg-costs (dict/seg_costs.h); the cheapest path wins, and a tie keeps the order of the search.
// The lattice reads, and decides only one thing (LatticeKatakana): at the start of a katakana run
// where every reading by words has a stray piece, the run read whole by its sound goes first when
// the lattice reads it so more cheaply.
//
// Also:
// - SegReport with --why: per token the path the search took with the cost terms of each edge, and
//   the best path with another split.
// - SegReport with HDTRANS_SEG_SHADOW=<file>: writes <file>.<pid>, one row per window where the
//   lattice splits more cheaply than the search, with the family of the window.
//
// The work is linear in the length of the token: the edges at a character are the lookup's, the
// sound edge of a katakana run reads its end and length from a memo of the run, and labels are
// formatted only for the paths printed.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/dict/char_readings.h"
#include "core/dict/classes.h"
#include "core/text/unicode.h"
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <process.h>
#else
#include <unistd.h>
#endif
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
    bool joined = false;                // a katakana word with the single kana that ends the run (ジャーニー|ズ)
    bool stem = false;                  // a verb or adjective stem with no ending matched
    // For --why and the shadow only: the start of the span, the code, layer and gloss of the edge,
    // and its cost terms, formatted by EdgeLabel; label is set where it is formatted already.
    int from = 0;
    std::string code, gloss, terms, label;
    char layer = 0;
};

// The katakana run SegEdgesAt read last: text, the byte offsets [start, end) from where it was
// read to its end, and per byte offset from start the kana from there to end.
struct SegRunMemo {
    std::string_view text;
    int start = 0, end = 0;
    std::vector<int> chars;
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

int SoundRunCost(const SegCosts& t, int chars, std::string* terms) {
    Cost c(t, terms);
    c.Add(dict::kSegSoundRun);
    c.Add(dict::kSegSoundChar, chars);
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
    if (AllOf(span, KataRune)) return SoundRunCost(t, Chars(span), terms);
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
    // The gloss up to 24 bytes, cut at a character.
    std::size_t cut = 0;
    while (cut < gloss.size()) {
        const std::size_t n = std::max<std::size_t>(1, FirstChar(gloss.substr(cut)).size());
        if (cut + n > 24) break;
        cut += n;
    }
    s += gloss.substr(0, cut);
    s += " (";
    s += terms;
    s += ')';
    for (char& ch : s) {
        if (ch == '\t' || ch == '\n') ch = ' ';
    }
    return s;
}

std::string EdgeLabel(std::string_view s, const SegEdge& e) {
    if (!e.label.empty()) return e.label;
    return Label(s.substr(static_cast<std::size_t>(e.from), static_cast<std::size_t>(e.end - e.from)), e.code, e.layer, e.gloss, e.terms);
}

// Adds a cost term to the label of e.
void AddTerm(SegEdge& e, std::string_view term) {
    if (!e.label.empty()) {
        e.label.insert(e.label.size() - 1, std::string(" ") + std::string(term));
    } else {
        if (!e.terms.empty()) e.terms += ' ';
        e.terms += term;
    }
}

// The fields of the label of e.
void SetLabel(SegEdge& e, int from, std::string_view code, char layer, std::string_view gloss, std::string terms) {
    e.from = from;
    e.code = code;
    e.layer = layer;
    e.gloss = gloss.substr(0, std::min<std::size_t>(gloss.size(), 32));
    e.terms = std::move(terms);
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
        if (detail) AddTerm(*x, "particle-seam " + std::to_string(pieces - x->cost));
        x->cost = pieces;
    }
}

struct Sink {
    std::FILE* f = nullptr;
    std::atomic<long> tokens{0}, differ{0}, ties{0}, differTokens{0}, kataDiffer{0}, kataTies{0};
    Sink() {
        const char* path = std::getenv("HDTRANS_SEG_SHADOW");
        if (path == nullptr || *path == '\0') return;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
        const std::string p = std::string(path) + "." + std::to_string(_getpid());
#else
        const std::string p = std::string(path) + "." + std::to_string(::getpid());
#endif
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
        e.stem = stem;
        e.particle = part[0] == 'P' && AllHiragana(s.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(end - p)));
        if (detail) SetLabel(e, p, part, nameRow ? 'n' : kanaRow ? 'k' : 0, c.target, terms);
        // A single kana that ends the run after a katakana word belongs to that word (ジャーニー|ズ,
        // LEX-95): the word with it costs what the word costs.
        const bool joins = end < size && KataRune(RuneAt(s, end)) && AllOf(s.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(end - p)), KataRune) &&
                           !KataRune(RuneAt(s, end + CharLen(s, end)));
        if (joins) {
            SegEdge j = e;
            j.end = end + CharLen(s, end);
            j.joined = true;
            if (detail) AddTerm(j, "+kana");
            out.push_back(std::move(j));
        }
        out.push_back(std::move(e));
    }
    const int one = CharLen(s, p);
    const std::string_view ch = rest.substr(0, static_cast<std::size_t>(one));
    terms.clear();
    {
        SegEdge e(p + one, UnknownCost(t, ch, tp));
        if (detail) SetLabel(e, p, "unk", 0, "", terms);
        out.push_back(std::move(e));
    }
    const char32_t r = RuneAt(s, p);
    const char32_t before = p > 0 ? RuneAt(s, RuneBefore(s, p)) : 0;
    if (LatinRune(r) && !LatinRune(before)) {
        int q = p;
        while (q < size && LatinRune(RuneAt(s, q))) q += CharLen(s, q);
        terms.clear();
        SegEdge e(q, UnreadCost(t, rest.substr(0, static_cast<std::size_t>(q - p)), tp));
        if (detail) SetLabel(e, p, "lat", 0, "", terms);
        out.push_back(std::move(e));
    }
    // From the start of the run, or from a kana inside it that can start a word (no ー, ン or small
    // kana): the rest of a run after a word is read whole by its sound (ドクター|ゴフマン).
    if (KataRune(r) && (!KataRune(before) || !BoundKana(r))) {
        // The end of the run and the kana from p to it, from the memo of the run.
        if (segRun_ == nullptr) segRun_ = std::make_shared<SegRunMemo>();
        SegRunMemo& m = *segRun_;
        if (m.text.data() != s.data() || m.text.size() != s.size() || p < m.start || p >= m.end) {
            m.text = s;
            m.start = p;
            m.end = p;
            while (m.end < size && KataRune(RuneAt(s, m.end))) m.end += CharLen(s, m.end);
            m.chars.assign(static_cast<std::size_t>(m.end - p) + 1, 0);
            for (int at = m.end; at-- > p;) {
                const bool lead = (static_cast<unsigned char>(s[static_cast<std::size_t>(at)]) & 0xC0) != 0x80;
                m.chars[static_cast<std::size_t>(at - p)] = m.chars[static_cast<std::size_t>(at - p) + 1] + (lead ? 1 : 0);
            }
        }
        const int q = m.end;
        if (q - p > one) {
            terms.clear();
            SegEdge e(q, SoundRunCost(t, m.chars[static_cast<std::size_t>(p - m.start)], tp));
            if (detail) SetLabel(e, p, "snd", 0, "", terms);
            out.push_back(std::move(e));
        }
    }
}

// The lattice of a katakana run: edges from every character of the run, and from the end of the run
// up to the furthest end of those edges (keeping the edges that end there), with the cost of the best
// path from each character to that end. Built at each visit of a run start (the search may come back to it); its size is that of the run and
// the words that leave it.
struct SegWindow {
    int start = 0, end = 0, limit = 0;
    std::vector<std::vector<SegEdge>> edges;  // by offset from start
    std::vector<int> suffix;               // by offset from start; kInf where no path reaches limit
};

const SegWindow& Worker::SegWindowFor(std::string_view s, int start, int end, int token) {
    if (segWindow_ == nullptr) segWindow_ = std::make_shared<SegWindow>();
    SegWindow& w = *segWindow_;
    if (segRun_ != nullptr) segRun_->text = {};
    w.start = start;
    w.end = end;
    w.edges.assign(static_cast<std::size_t>(end - start) + 1, {});
    int limit = end;
    std::vector<SegEdge> out;
    for (int p = start; p < limit; p += CharLen(s, p)) {
        out.clear();
        SegEdgesAt(s, p, token, false, out);
        if (p >= end) std::erase_if(out, [&](const SegEdge& e) { return e.end > limit; });
        for (const SegEdge& e : out) limit = std::max(limit, e.end);
        if (w.edges.size() < static_cast<std::size_t>(limit - start) + 1) w.edges.resize(static_cast<std::size_t>(limit - start) + 1);
        w.edges[static_cast<std::size_t>(p - start)] = std::move(out);
        out = {};
    }
    w.limit = limit;
    w.edges.resize(static_cast<std::size_t>(limit - start) + 1);
    AddSeams(s, w.edges, start, start, limit, false);
    w.suffix.assign(static_cast<std::size_t>(limit - start) + 1, kInf);
    w.suffix.back() = 0;
    for (int p = limit; p-- > start;) {
        if ((static_cast<unsigned char>(s[static_cast<std::size_t>(p)]) & 0xC0) == 0x80) continue;
        int best = kInf;
        for (const SegEdge& e : w.edges[static_cast<std::size_t>(p - start)]) {
            const int rest = w.suffix[static_cast<std::size_t>(e.end - start)];
            if (rest < kInf) best = std::min(best, e.cost + rest);
        }
        w.suffix[static_cast<std::size_t>(p - start)] = best;
    }
    return w;
}

// LEX-6723: at the start of a katakana run of 3 to 24 kana that no record reads whole, the run
// read whole by its sound goes first where the lattice reads it so more cheaply than any path from
// the search's first record, and only where every reading of the run by dictionary words has a stray
// piece: one kana (not ー) read alone, a word with the single kana that ends the run, or a kana no
// word covers. A kana after a word that the dictionary reads in hiragana as a particle is no stray
// piece, at the end of the run too (ボク|ハ|マリオ, コレ|ハ: LEX-6724). The lattice does not choose between readings by words: their order is the
// search's. records holds the records of the lookup in its order.
Record* Worker::LatticeKatakana(Record* records, std::string_view full, int cursor, int token) {
    const SegCosts& t = snapshot.SegCostTable();
    if (!t.loaded || records == nullptr || (records->user != nullptr && records->user->force)) return records;
    const int size = static_cast<int>(full.size());
    if (cursor < 0 || cursor >= size || !KataRune(RuneAt(full, cursor))) return records;
    if (cursor > 0 && KataRune(RuneAt(full, RuneBefore(full, cursor)))) return records;  // not the start of a run
    const int start = cursor;
    int end = start, kana = 0;
    while (end < size && KataRune(RuneAt(full, end)) && kana <= static_cast<int>(kKatakanaRunMax)) {
        end += CharLen(full, end);
        kana++;
    }
    if (kana < 3 || kana > static_cast<int>(kKatakanaRunMax)) return records;
    for (const Record* r = records; r != nullptr; r = r->child) {
        if (r->source.size() >= static_cast<std::size_t>(end - start)) return records;
    }
    const SegWindow& w = SegWindowFor(full, start, end, token);
    auto rest = [&](int at) { return at >= start && at <= w.limit ? w.suffix[static_cast<std::size_t>(at - start)] : kInf; };
    // Whether the edge e from at is a stray piece of a reading of the run by words.
    auto stray = [&](const SegEdge& e, int at) {
        const bool one = e.end - at == CharLen(full, at);
        const char32_t r = RuneAt(full, at);
        if (one && r == U'ー') return false;  // a mark, read or not
        if (!e.word || e.joined) return true;
        if (!one || !KataRune(r)) return false;
        if (at == start) return true;
        const char32_t next = RuneAt(full, e.end);
        if (BoundKana(next) || r < U'ァ' || r > U'ヶ') return true;
        std::string hira;
        text::AppendRune(hira, static_cast<char32_t>(r - 0x60));
        for (const dict::Candidate& c : WordInfo(hira, kModeGeneral).list) {
            const dict::Codes* codes = c.codes != nullptr ? c.codes : c.user != nullptr ? c.user->codes : nullptr;
            if (c.source.size() == hira.size() && codes != nullptr && codes->partText.size() == 4 && codes->partText[0] == 'P' &&
                codes->partText[1] == '2') {
                return false;
            }
        }
        return true;
    };
    // clean[i]: a reading by words with no stray piece runs from start + i to the end of the run.
    std::vector<char> clean(static_cast<std::size_t>(end - start) + 1, 0);
    clean.back() = 1;
    for (int at = end; at-- > start;) {
        if ((static_cast<unsigned char>(full[static_cast<std::size_t>(at)]) & 0xC0) == 0x80) continue;
        for (const SegEdge& e : w.edges[static_cast<std::size_t>(at - start)]) {
            if (!stray(e, at) && (e.end >= end || clean[static_cast<std::size_t>(e.end - start)] != 0)) {
                clean[static_cast<std::size_t>(at - start)] = 1;
                break;
            }
        }
    }
    if (clean.front() != 0) return records;
    // kanaAt[i]: the kana from start to start + i. A stray prefix of two or more kana holding a word before a dictionary word
    // that takes two or more kana of the run and ends at its end or crosses it is a name before a word (ヨンホ|コーヒー,
    // ハリホ|パン, ホロ|スクリーン, トラジャ|モー娘): the run is not one sound. Nor is a run whose reading by words starts
    // with words that end at a kana the dictionary reads in hiragana as a particle (ボク|ハ|ヨンホ): it is speech.
    std::vector<int> kanaAt(static_cast<std::size_t>(end - start) + 1, 0);
    for (int at = start, k = 0; at <= end; at += at < end ? CharLen(full, at) : 1) kanaAt[static_cast<std::size_t>(at - start)] = k++;
    // The word at the end is the longest dictionary word that takes two or more kana of the run and ends at its end
    // or crosses it (ハピ|ライン, not ハピラ|イン). Not a proper noun of two kana inside the run: the costs read it as a
    // piece cut by chance (short-kata-piece). The prefix before it holds a name only where a dictionary word of two
    // or more kana lies inside it (ヨン|ホ, ハリ|ホ); lone strays (ハ|ピ) are sound.
    int last = end;
    for (int at = start; at < end && last == end; at += CharLen(full, at)) {
        const std::size_t i = static_cast<std::size_t>(at - start);
        if (kana - kanaAt[i] < 2) break;
        for (const SegEdge& e : w.edges[i]) {
            const bool piece = e.part == "N130" && e.end <= end && kanaAt[static_cast<std::size_t>(e.end - start)] - kanaAt[i] == 2;
            if (e.word && !e.joined && e.end >= end && !stray(e, at) && !piece) last = at;
        }
    }
    if (last < end && kanaAt[static_cast<std::size_t>(last - start)] >= 2) {
        for (int at = start; at < last; at += CharLen(full, at)) {
            for (const SegEdge& e : w.edges[static_cast<std::size_t>(at - start)]) {
                if (e.word && !e.joined && e.end <= last &&
                    kanaAt[static_cast<std::size_t>(e.end - start)] - kanaAt[static_cast<std::size_t>(at - start)] >= 2) {
                    return records;
                }
            }
        }
    }
    std::vector<char> words(static_cast<std::size_t>(end - start) + 1, 0);  // a reading by words with no stray piece runs from start to start + i
    words.front() = 1;
    for (int at = start; at < end; at += CharLen(full, at)) {
        const std::size_t i = static_cast<std::size_t>(at - start);
        if (words[i] == 0) continue;
        for (const SegEdge& e : w.edges[i]) {
            if (stray(e, at) || e.end > end) continue;
            words[static_cast<std::size_t>(e.end - start)] = 1;
            const bool particle = at > start && e.end - at == CharLen(full, at) && e.end < end;
            if (particle && kanaAt[static_cast<std::size_t>(e.end - start)] >= 2) return records;
        }
    }
    // The cheapest path from the search's first record; a record the lattice cannot price stands.
    const int key = static_cast<int>(records->source.size());
    int first = kInf;
    for (const SegEdge& e : w.edges[0]) {
        if (e.word && !e.joined && e.key == key && e.part == records->partCode && e.user == records->user && rest(e.end) < kInf) {
            first = std::min(first, e.cost + rest(e.end));
        }
    }
    if (first >= kInf) return records;
    const int sound = SoundRunCost(t, kana, nullptr) + rest(end);
    if (sound >= first) return records;
    segDecided_.emplace_back(start, end);
    Record* read = CreateSyntheticRecord(token, full.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)), false);
    read->child = records;
    return read;
}

namespace {

bool KanjiRune(char32_t r) { return text::IsKanji(r) || r == U'々'; }

// Whether gloss reads span by the sounds of its kanji, one syllable each, spaces aside: 分岐 분기,
// 米国 미국, 年度 연도 (an initial ㄴ, ㄹ or ㅇ may stand for another of them).
bool ReadsBySound(std::string_view span, std::string_view gloss) {
    std::u32string sound;
    for (std::string_view s = span; !s.empty();) {
        const text::Rune r = text::DecodeRune(s);
        s.remove_prefix(r.size);
        const std::optional<char32_t> w = r.cp == U'々' && !sound.empty() ? std::optional<char32_t>(sound.back()) : dict::Written(r.cp);
        if (!w || *w < 0xAC00 || *w > 0xD7A3) return false;
        sound.push_back(*w);
    }
    auto liquid = [](int initial) { return initial == 2 || initial == 5 || initial == 11; };
    std::size_t k = 0;
    for (std::string_view s = gloss; !s.empty();) {
        const text::Rune r = text::DecodeRune(s);
        s.remove_prefix(r.size);
        if (r.cp == U' ') continue;
        if (k >= sound.size() || r.cp < 0xAC00 || r.cp > 0xD7A3) return false;
        const int a = static_cast<int>(r.cp) - 0xAC00, b = static_cast<int>(sound[k++]) - 0xAC00;
        if (a != b && (a % 588 != b % 588 || !liquid(a / 588) || !liquid(b / 588))) return false;
    }
    return k == sound.size();
}

}  // namespace

// LEX-6725: inside a run of 3 to 16 kanji, where the first record is a noun of two kanji
// that ends inside the run and reads them otherwise than by their sounds (全店 가게 전체, 学研
// 가켄, 明大 메이지대학), the first word of the lattice's path goes first where that path is cheaper
// than any path from the record, cuts the record's end inside one of its words (全|店舗, 学|研究,
// 明|大統領), and reads every kanji of the run by a dictionary word that is no proper noun, no reading
// of the name layer and no bare stem. A record read by the sounds of its kanji stays: a kanji read by
// its sound after it is a Sino-Korean compound (分岐|路 분기로, 米国|内 미국 내). records holds the records
// of the lookup in the order the reorders left.
Record* Worker::LatticeKanji(Record* records, std::string_view full, int cursor, int token) {
    constexpr int kMaxRun = 16;
    const SegCosts& t = snapshot.SegCostTable();
    if (!t.loaded || records == nullptr || (records->user != nullptr && records->user->force)) return records;
    const int size = static_cast<int>(full.size());
    if (cursor < 0 || cursor >= size || !text::IsKanji(RuneAt(full, cursor))) return records;
    int end = cursor, n = 0;
    while (end < size && KanjiRune(RuneAt(full, end)) && n <= kMaxRun) {
        end += CharLen(full, end);
        n++;
    }
    // A noun of the lookup that could lead: two kanji ending inside the run, not read by their sounds. Two kanji is
    // the length a chance overlap takes (明大 of 李在明大統領); a longer noun is a compound by its length (想定問答|集).
    auto leads = [&](const Record& r) {
        return !r.partCode.empty() && r.partCode[0] == 'N' && static_cast<int>(r.source.size()) < end - cursor && Chars(r.source) == 2 &&
               AllOf(r.source, KanjiRune) && !ReadsBySound(r.source, r.target);
    };
    if (n < 3 || n > kMaxRun) return records;
    bool any = false;
    for (const Record* r = records; r != nullptr && !any; r = r->child) any = leads(*r);
    if (!any) return records;
    const SegWindow& w = SegWindowFor(full, cursor, end, token);
    auto rest = [&](int at) { return at >= cursor && at <= w.limit ? w.suffix[static_cast<std::size_t>(at - cursor)] : kInf; };
    auto edgeOf = [&](const Record& r) -> const SegEdge* {
        const int k = static_cast<int>(r.source.size());
        for (const SegEdge& e : w.edges[0]) {
            if (e.word && e.key == k && e.part == r.partCode && e.user == r.user) return &e;
        }
        return nullptr;
    };
    // The record the search takes: the first one, past verbs and adjectives whose ending does not match here
    // (学 of 学ぶ, 重 of 重い before a kanji).
    const Record* lead = records;
    while (lead != nullptr && (lead->partCode.starts_with("M") || lead->partCode.starts_with("H"))) {
        const SegEdge* e = edgeOf(*lead);
        if (e == nullptr || !e->stem) break;
        lead = lead->child;
    }
    if (lead == nullptr || !leads(*lead)) return records;
    const int key = static_cast<int>(lead->source.size());
    // A kanji the dictionary reads as a suffix binds to the word before it (福良|港, 中地|区, 冠水|路), as in KanjiCoverFirst.
    const std::string_view after = full.substr(static_cast<std::size_t>(cursor + key), static_cast<std::size_t>(CharLen(full, cursor + key)));
    // The class holds kanji with no row of their own too (冠水|路).
    if (dict::InClass(HDTRANS_CLASS("suffix.kanji-noun"), after)) return records;
    for (const dict::Candidate& c : WordInfo(full.substr(static_cast<std::size_t>(cursor + key)), kModeGeneral).list) {
        if (c.source == after && c.codes != nullptr && c.codes->gram.HasCode("Z7")) return records;
    }
    // A proper noun that starts the run stands at its own boundary (津地|検); inside the run it may overlap the
    // next word by chance (李在|明大|統領).
    if (lead->partCode == "N130" && (cursor == 0 || !KanjiRune(RuneAt(full, RuneBefore(full, cursor))))) return records;
    int first = kInf;
    for (const SegEdge& e : w.edges[0]) {
        if (e.word && e.end == cursor + key && e.key == key && e.part == lead->partCode && e.user == lead->user && rest(e.end) < kInf) {
            first = std::min(first, e.cost + rest(e.end));
        }
    }
    if (first >= kInf || rest(cursor) >= first) return records;
    // The lattice's path through the run; a tie keeps the edge the lookup lists first.
    const SegEdge* head = nullptr;
    bool cuts = false;
    for (int p = cursor; p < end;) {
        const SegEdge* pick = nullptr;
        for (const SegEdge& e : w.edges[static_cast<std::size_t>(p - cursor)]) {
            if (rest(e.end) < kInf && e.cost + rest(e.end) == rest(p)) {
                pick = &e;
                break;
            }
        }
        // Every word of the path lies inside the run: a word that goes on into kana (人並|に) is grammar, not a kanji cover.
        if (pick == nullptr || !pick->word || pick->stem || pick->part == "N130" || (pick->user != nullptr && pick->user->nameLayer) ||
            pick->end > end) {
            return records;
        }
        cuts = cuts || (p < cursor + key && pick->end > cursor + key);
        if (head == nullptr) head = pick;
        p = pick->end;
    }
    if (head == nullptr || !cuts || head->end >= cursor + key) return records;
    const std::size_t chosen = static_cast<std::size_t>(head->end - cursor);
    // The record gives way only to a first word read by its kanji sound (全|店舗 전, 李在|明|大統領 명), never to
    // the meaning of its first kanji (桑|山村 뽕, 首|相兼 목): a name read by sound is still the name.
    for (const Record* r = records; r != nullptr; r = r->child) {
        if (r->source.size() == chosen && r->partCode == head->part && r->user == head->user) {
            if (!ReadsBySound(r->source, r->target)) return records;
            break;
        }
    }
    // The record of the lattice's first edge, then the other records of its length, then the rest.
    Record* exact = nullptr;
    Record* front = nullptr;
    Record* back = nullptr;
    Record** frontTail = &front;
    Record** backTail = &back;
    for (Record* r = records; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (exact == nullptr && r->source.size() == chosen && r->partCode == head->part && r->user == head->user) {
            exact = r;
        } else {
            Record**& tail = r->source.size() == chosen ? frontTail : backTail;
            *tail = r;
            tail = &r->child;
        }
        r = next;
    }
    if (exact != nullptr) {
        exact->child = front;
        if (front == nullptr) frontTail = &exact->child;
        front = exact;
    }
    if (front == nullptr) {
        if (static_cast<int>(chosen) != CharLen(full, cursor)) return back;
        front = CreateSyntheticRecord(token, full.substr(static_cast<std::size_t>(cursor), chosen), false);
        frontTail = &front->child;
    }
    *frontTail = back;
    segDecided_.emplace_back(cursor, cursor + static_cast<int>(chosen));
    return front;
}

// The edges of the whole token for --why and the shadow, the search's own items among them, and
// the search's path through them; false where the search left the token in pieces outside it.
bool Worker::SegTokenLattice(int token, std::string_view surface, const Item* mark, SegTokenPaths& out) {
    const SegCosts& t = snapshot.SegCostTable();
    const int size = static_cast<int>(surface.size());
    auto& edges = out.edges;
    edges.assign(static_cast<std::size_t>(size) + 1, {});
    if (segRun_ != nullptr) segRun_->text = {};
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
        out.labels[k] = EdgeLabel(surface, x);
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
    std::vector<const SegEdge*> from;
    for (int p = 0; p < size; p++) {
        // The cheapest edge of each end from p (the first of equal ones), by end.
        from.clear();
        for (const SegEdge& e : edges[static_cast<std::size_t>(p)]) from.push_back(&e);
        std::stable_sort(from.begin(), from.end(), [](const SegEdge* a, const SegEdge* b) { return a->end != b->end ? a->end < b->end : a->cost < b->cost; });
        for (std::size_t i = 0; i < from.size(); i++) {
            const SegEdge* x = from[i];
            if (i > 0 && from[i - 1]->end == x->end) continue;
            const int e = x->end;
            if (e <= p || e > size) continue;
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
    Sink& sink = TheSink();
    const bool why = settings.splitWhy;
    if (!t.loaded || surface.empty() || (sink.f == nullptr && !why)) return;
    SegTokenPaths paths;
    if (!SegTokenLattice(token, surface, mark, paths)) return;
    bool decided = false;
    for (std::size_t k = 0; k + 1 < paths.bounds.size() && !decided; k++) {
        const std::pair<int, int> span(paths.bounds[k], paths.bounds[k + 1]);
        decided = std::find(segDecided_.begin(), segDecided_.end(), span) != segDecided_.end();
    }
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
            for (const SegEdge* e : other.edges) labels.push_back(EdgeLabel(surface, *e));
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
        newLabels.push_back(EdgeLabel(surface, *e));
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
        for (int p = oldBounds[i0]; p < oldBounds[i] && !kata;) {
            if (!KataRune(RuneAt(surface, p))) {
                p += CharLen(surface, p);
                continue;
            }
            const auto [a, b] = KataRunAt(surface, p);
            const int n = Chars(surface.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a)));
            kata = n >= 3 && n <= static_cast<int>(kKatakanaRunMax);
            p = b;
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
        // The line and the token for context; past 512 bytes the window with 20 characters on each
        // side (the token's own), so a row stays short on a long line.
        std::string_view piece = surface;
        if (surface.size() > 512) {
            int a = oldBounds[i0], b = oldBounds[i];
            for (int k = 0; k < 20 && a > 0; k++) a = RuneBefore(surface, a);
            for (int k = 0; k < 20 && b < static_cast<int>(surface.size()); k++) b += CharLen(surface, b);
            piece = surface.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a));
        }
        const std::string_view context = chunk.size() > 512 ? piece : std::string_view(chunk);
        std::fprintf(sink.f, "%.*s\t%.*s\t%.*s\t%d\t%s\t%d\t%s\t%s\n", static_cast<int>(context.size()), context.data(), static_cast<int>(piece.size()), piece.data(),
                     static_cast<int>(window.size()), window.data(), oc, Joined(paths.labels, i0, i).c_str(), nc,
                     Joined(newLabels, j0, j).c_str(), kata ? "kata" : "-");
    }
    if (any) sink.differTokens++;
}

}  // namespace hdTrans::word
