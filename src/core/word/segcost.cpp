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
//
// The work is linear in the length of the token: the edges at a character are the lookup's, the
// sound edge of a katakana run reads its end and length from a memo of the run, and labels are
// formatted only for the paths printed.

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/dict/char_readings.h"
#include "core/dict/classes.h"
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
    bool joined = false;                // a katakana word with the single kana that ends the run (ジャーニー|ズ)
    bool stem = false;                  // a verb or adjective stem with no ending matched
    bool conj = false;                  // a verb or adjective with its ending matched
    bool aux = false;                   // a verb that can follow a verb in て (aux.te-start)
    int from = 0;                       // the start of the span
    // For --why only: the code, layer and gloss of the edge, and its cost terms, formatted by
    // EdgeLabel; label is set where it is formatted already.
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
    // Saturates at kInf (a run long enough to reach it is no path), so no length overflows.
    void Add(SegCost c, int times = 1) {
        const int v = static_cast<int>(std::min<long long>(static_cast<long long>(table_[c]) * times, kInf));
        total_ = std::min(total_ + v, kInf);
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

}  // namespace

// The edges at p of s: every dictionary candidate there, one unknown character, a run of Latin
// letters from its start, and a katakana run from its start read whole by its sound.
void Worker::SegEdgesAt(std::string_view s, int p, int token, bool detail, std::vector<SegEdge>& out) {
    const SegCosts& t = snapshot.SegCostTable();
    const int size = static_cast<int>(s.size());
    const std::string_view rest = s.substr(static_cast<std::size_t>(p));
    std::string terms;
    std::string* const tp = detail ? &terms : nullptr;
    dict::Candidates found = WordInfo(rest, kModeGeneral);
    // The name-layer rows of found.list point into found.names; the edges and records keep those
    // pointers, so the names live in the arena as DoLookup keeps them.
    if (!found.names.empty()) {
        if (arena.namesUsed == arena.names.size()) arena.names.emplace_back();
        arena.names[arena.namesUsed++].swap(found.names);
    }
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
        e.from = p;
        e.conj = (part[1] == '4' || part[1] == '7') && !stem;
        e.aux = e.conj && part[0] == 'M' && dict::StartsWithMember(HDTRANS_CLASS("aux.te-start"), rest) > 0;
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
        e.from = p;
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
        e.from = p;
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
            e.from = p;
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
    // SegEdgesAt's edges by position for one token's text, kept while the windows move along it (the search visits
    // each position of a hiragana stretch, and every window reads up to 12 of them).
    std::string_view memoText;
    int memoToken = -1;
    std::unordered_map<int, std::vector<SegEdge>> memo;
    // LatticeKana: the rows at a position that do not conjugate, by their end and kind, and whether each is a noun or
    // adjectival noun of three or more hiragana.
    struct Row {
        int end = 0;
        char kind = 0;
        bool kanaNoun = false;
        bool particle = false;   // a particle row in hiragana
        bool shortNoun = false;  // a noun of one or two hiragana
        bool pword = false;      // a particle word in hiragana (SegEdge::particle)
    };
    std::unordered_map<int, std::vector<Row>> rows;
};

// The memo of the windows for the token's text s.
SegWindow& SegMemo(std::shared_ptr<SegWindow>& window, std::string_view s, int token) {
    if (window == nullptr) window = std::make_shared<SegWindow>();
    SegWindow& w = *window;
    if (w.memoText.data() != s.data() || w.memoText.size() != s.size() || w.memoToken != token) {
        w.memo.clear();
        w.rows.clear();
        w.memoText = s;
        w.memoToken = token;
    }
    return w;
}

const SegWindow& Worker::SegWindowFor(std::string_view s, int start, int end, int token) {
    SegWindow& w = SegMemo(segWindow_, s, token);
    if (segRun_ != nullptr) segRun_->text = {};
    w.start = start;
    w.end = end;
    w.edges.assign(static_cast<std::size_t>(end - start) + 1, {});
    int limit = end;
    std::vector<SegEdge> out;
    for (int p = start; p < limit; p += CharLen(s, p)) {
        auto it = w.memo.find(p);
        if (it == w.memo.end()) {
            out.clear();
            SegEdgesAt(s, p, token, false, out);
            it = w.memo.emplace(p, std::move(out)).first;
        }
        out = it->second;
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

namespace {

bool HiraRune(char32_t r) { return r >= U'ぁ' && r <= U'ゖ'; }

// The transition vetoes of the lattice (SEG-DESIGN section 2): the cost of the edge r after the edge l, kInf where
// the pair is forbidden. edgesAt(p) lists the edges from the byte offset p.
//   CutVerb: l ends in a kanji before a hiragana where a verb or adjective from the start of l runs on with its ending
//     (目指|していた, 欲|しくなって, 持|ってる): the hiragana is that ending, no word of its own.
//   CutKanaWord: l is one hiragana, no particle, before a hiragana, and a word from the start of l, no particle and no
//     conjugated form, ends in kana inside r, no particle (わ|けさ for わけ|さ, し|かいない, そ|の中に): r starts inside
//     the word l cut. After a particle the same pair is a reading of its own (は|なす); a longer l is a word of its own.
//   te-chain: a verb in て and an auxiliary verb after it (aux.te-start) are one word: the auxiliary costs te-chain in
//     place of word.
template <class EdgesAt>
int Step(const SegCosts& t, std::string_view s, EdgesAt&& edgesAt, const SegEdge& l, const SegEdge& r) {
    const int p = r.from;
    if (l.end != p || p <= 0 || p >= static_cast<int>(s.size())) return r.cost;
    const char32_t last = RuneAt(s, RuneBefore(s, p));
    const char32_t next = RuneAt(s, p);
    if (HiraRune(next) && text::IsKanji(last)) {
        for (const SegEdge& v : edgesAt(l.from)) {
            if (v.word && v.conj && v.end > p) return kInf;
        }
    }
    if (HiraRune(next) && HiraRune(last) && !l.particle && !r.particle && l.end - l.from == CharLen(s, l.from)) {
        for (const SegEdge& v : edgesAt(l.from)) {
            if (!v.word || v.particle || v.conj || v.end <= p || v.end >= r.end) continue;
            if (AllHiragana(s.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(v.end - p)))) return kInf;
        }
    }
    int cost = r.cost;
    if (r.aux && l.conj) {
        const std::string_view lt = s.substr(static_cast<std::size_t>(l.from), static_cast<std::size_t>(l.end - l.from));
        if (lt.ends_with("て") || lt.ends_with("で")) cost += t[dict::kSegTeChain] - t[dict::kSegWord];
    }
    return cost;
}

// A piece of a reading of hiragana that the costs name as a defect: hiragana no word reads, a bare stem, or one
// hiragana read as a content word before another hiragana.
bool KanaDefect(std::string_view s, const SegEdge& e) {
    const std::string_view span = s.substr(static_cast<std::size_t>(e.from), static_cast<std::size_t>(e.end - e.from));
    if (!e.word) return true;
    if (e.stem) return true;
    if (!AllHiragana(span) || Chars(span) != 1 || e.particle) return false;
    if (e.part.empty() || std::string_view("NGEKCI").find(e.part[0]) == std::string_view::npos) return false;
    return e.end < static_cast<int>(s.size()) && HiraRune(RuneAt(s, e.end));
}

bool NameEdge(const SegEdge& e) { return (e.part.size() == 4 && e.part[2] == '3') || (e.user != nullptr && e.user->nameLayer); }

}  // namespace

// LEX-7900: inside hiragana, where the lattice reads the stretch from the cursor more cheaply than any reading that
// starts with the search's pick, and that pick's reading cuts a word W of the lattice's path: a noun, adjective or
// phrase row of three or more hiragana (たい|こ for たいこ, かい|じゅう for かいじゅう), or a word with a kanji that a
// boundary row starting in kana takes the head of (その上|着 for その|上着). W must be followed by a particle (the copula
// among them), punctuation or the end of the token, and every word of the path up to W is a dictionary word with no
// defect and no name. The pairs of Step apply to both readings. Then the record of the path's first word goes first;
// else the search's order stands. records holds the records of the lookup in the order the reorders left.
Record* Worker::LatticeKana(Record* records, std::string_view full, int cursor, int token, const Item* parent) {
    constexpr int kMaxChars = 12;
    const SegCosts& t = snapshot.SegCostTable();
    if (!t.loaded || records == nullptr || records->child == nullptr || (records->user != nullptr && records->user->force)) return records;
    const int size = static_cast<int>(full.size());
    if (cursor < 0 || cursor >= size || !HiraRune(RuneAt(full, cursor))) return records;
    int end = cursor;
    for (int n = 0; end < size && n < kMaxChars; n++) {
        const char32_t r = RuneAt(full, end);
        if (!HiraRune(r) && !text::IsKanji(r) && r != U'々') break;
        end += CharLen(full, end);
    }
    // W is the first or the second word of the lattice's path and starts inside the window. The first word does not
    // conjugate: it is W, or it stands before W (below), and the boundary row that takes the head of the second word
    // starts where the path does, so it is the pick. So only a cursor where the pick is a boundary row, or where a noun
    // or adjectival noun of three or more hiragana starts at the cursor or after a row there, can decide. The rows
    // tell that without the lattice, which is built only then.
    SegWindow& memo = SegMemo(segWindow_, full, token);
    auto rowsAt = [&](int p) -> const std::vector<SegWindow::Row>& {
        auto [at, fresh] = memo.rows.try_emplace(p);
        if (fresh) {
            const std::string_view rest = full.substr(static_cast<std::size_t>(p));
            // The search's own lookup of rest, where it read what this one would: same text, token and surface, and
            // no word before that KanaAfterWord reads here.
            const GeneralLookup& g = lastGeneral_;
            const bool same = g.data == rest.data() && g.size == rest.size() && g.current == current &&
                              g.surface.data() == transformSurface.data() && g.surface.size() == transformSurface.size() &&
                              (kanaAfter == nullptr || kanaAfterAt != rest.data());
            const dict::Candidates found = same ? dict::Candidates{} : WordInfo(rest, kModeGeneral);
            for (const dict::Candidate& c : same ? g.list : found.list) {
                if (c.source.empty() || c.codes == nullptr || !rest.starts_with(c.source)) continue;
                const std::string_view part = c.codes->partText;
                const std::string_view src = c.source;
                if (part.size() != 4 || part[1] == '5') continue;
                if (part[1] == '4' || part[1] == '7') {
                    // A verb or an adjective: its end is its ending's, so a row of kind 7 counts only where it starts; one of
                    // kind v (a verb) or a (an adjective) keeps the end of its key, where its ending starts (LEX-7920).
                    if (part[1] == '7') at->second.push_back({p, '7'});
                    at->second.push_back({p + static_cast<int>(src.size()), part[1] == '7' ? 'a' : 'v'});
                    continue;
                }
                const bool kana = AllHiragana(src);
                at->second.push_back({p + static_cast<int>(src.size()), part[1], (part[1] == '1' || part[1] == '8') && kana && Chars(src) >= 3,
                                      part[1] == '2' && kana, part[1] == '1' && kana && Chars(src) <= 2, part[0] == 'P' && kana});
            }
        }
        return at->second;
    };
    // What follows W: the end of the token, punctuation, or a particle; or one noun of one or two hiragana (たち, さん)
    // and then one of those.
    auto stops = [&](int at) {
        if (at >= size) return true;
        const char32_t r = RuneAt(full, at);
        return !HiraRune(r) && !KataRune(r) && !text::IsKanji(r) && r != U'々' && !LatinRune(r) && !(r >= U'0' && r <= U'9') &&
               !(r >= U'０' && r <= U'９');
    };
    auto fitsAt = [&](int at) {
        if (stops(at)) return true;
        for (const SegWindow::Row& x : rowsAt(at)) {
            if (x.particle || (x.shortNoun && stops(x.end))) return true;
            if (!x.shortNoun) continue;
            for (const SegWindow::Row& y : rowsAt(x.end)) {
                if (y.particle) return true;
            }
        }
        return false;
    };
    // Whether the text before at ends in a form of a verb (cls v) or an adjective (cls a) that can stand before a noun
    // (LEX-7920): a kana noun after it is a word of its own (ある|いみ, いい|しなやか), not after a stem or a て form.
    // A form of two hiragana is also the stem of a verb (いい of 言い|たい), so it is no such form.
    auto attributive = [&](int at, char cls, int from) {
        const std::string_view form = full.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(at - from));
        if (AllHiragana(form) && Chars(form) <= 2) return false;
        return at > 0 && dict::EndsWithMember(cls == 'a' ? HDTRANS_CLASS("ending.attributive-adjective") : HDTRANS_CLASS("ending.attributive"),
                                              full.substr(0, static_cast<std::size_t>(at))) > 0;
    };
    // A noun W, as for maybe below, after the ending of the verb or adjective row r: one to three kana after its key.
    auto nounAfterForm = [&](const SegWindow::Row& r) {
        int q = r.end;
        for (int n = 0; n < 3 && q < end; n++) {
            q += CharLen(full, q);
            if (q >= end || !attributive(q, r.kind, cursor)) continue;
            for (const SegWindow::Row& x : rowsAt(q)) {
                if (x.kanaNoun && fitsAt(x.end)) return true;
            }
        }
        return false;
    };
    // W right at the cursor does not follow a verb or an adjective (below), unless in a form that stands before a noun.
    const bool afterVerb = parent != nullptr && cursor > 0 && parent->start + parent->length == cursor && parent->detail != nullptr &&
                           (parent->detail->cls == kDetailVerb || parent->detail->cls == kDetailKeiyo) &&
                           !attributive(cursor, parent->detail->cls == kDetailKeiyo ? 'a' : 'v', parent->start);
    // A boundary row or such a noun is there before the pick is read.
    bool maybe = false;
    for (const SegWindow::Row& r : rowsAt(cursor)) {
        if (maybe) break;
        if (r.kind == '7') continue;
        if (r.kind == 'v' || r.kind == 'a') {
            maybe = nounAfterForm(r);
            continue;
        }
        maybe = r.kind == '3' || (r.kanaNoun && !afterVerb && fitsAt(r.end));
        if (!maybe && r.end < end) {
            for (const SegWindow::Row& n : rowsAt(r.end)) maybe = maybe || (n.kanaNoun && fitsAt(n.end));
        }
    }
    if (!maybe) return records;
    const Item* pick = InsertLateCandidate(full, cursor, token, records);
    if (pick == nullptr || pick->start != cursor || pick->length <= 0) return records;
    const int e0 = cursor + pick->length;
    if (e0 >= size || end <= e0) return records;
    // The pick is a boundary row whose head a word of two or more characters before it leaves to a word with a kanji
    // or an adjective.
    bool pickBoundary = false;
    for (const SegWindow::Row& r : rowsAt(cursor)) pickBoundary = pickBoundary || (r.kind == '3' && r.end == e0);
    bool open = false;
    for (const SegWindow::Row& r : rowsAt(cursor)) {
        if (open) break;
        if (r.kind == '7') continue;
        if (r.kind == 'v' || r.kind == 'a') {
            open = nounAfterForm(r);
            continue;
        }
        if (r.end == e0) continue;
        if (pickBoundary && r.end < e0 && !r.pword && Chars(full.substr(static_cast<std::size_t>(cursor), static_cast<std::size_t>(r.end - cursor))) >= 2) {
            const char32_t h = RuneAt(full, r.end);
            open = text::IsKanji(h) || h == U'々';
            for (const SegWindow::Row& n : rowsAt(r.end)) open = open || n.kind == '7';
        }
        open = open || (r.kanaNoun && !afterVerb && fitsAt(r.end));
        if (!open && r.end < end) {
            for (const SegWindow::Row& n : rowsAt(r.end)) open = open || (n.kanaNoun && fitsAt(n.end));
        }
    }
    if (!open) return records;
    const SegWindow& w = SegWindowFor(full, cursor, end, token);
    const int start = cursor;
    auto edgesAt = [&](int p) -> const std::vector<SegEdge>& { return w.edges[static_cast<std::size_t>(p - start)]; };
    // rest[i][k], clean[i][k]: the cheapest steps after the edge k from start + i to w.limit, by any edges or by edges
    // with no defect and no name; kInf for none.
    std::vector<std::vector<int>> rest(w.edges.size()), clean(w.edges.size());
    for (int p = w.limit; p-- > start;) {
        if ((static_cast<unsigned char>(full[static_cast<std::size_t>(p)]) & 0xC0) == 0x80) continue;
        const std::size_t i = static_cast<std::size_t>(p - start);
        const auto& from = w.edges[i];
        rest[i].assign(from.size(), kInf);
        clean[i].assign(from.size(), kInf);
        for (std::size_t k = 0; k < from.size(); k++) {
            const SegEdge& e = from[k];
            if (e.end > w.limit || e.end <= p) continue;
            if (e.end == w.limit) {
                rest[i][k] = clean[i][k] = 0;
                continue;
            }
            const std::size_t j = static_cast<std::size_t>(e.end - start);
            for (std::size_t m = 0; m < w.edges[j].size() && m < rest[j].size(); m++) {
                if (rest[j][m] >= kInf) continue;
                const SegEdge& r = w.edges[j][m];
                const int step = Step(t, full, edgesAt, e, r);
                if (step >= kInf) continue;
                rest[i][k] = std::min(rest[i][k], step + rest[j][m]);
                if (clean[j][m] < kInf && !KanaDefect(full, r) && !NameEdge(r)) clean[i][k] = std::min(clean[i][k], step + clean[j][m]);
            }
        }
    }
    const auto& first = w.edges[0];
    if (rest[0].size() != first.size()) return records;
    int best = kInf, record = kInf, tidy = kInf;
    for (std::size_t k = 0; k < first.size(); k++) {
        if (rest[0][k] >= kInf) continue;
        best = std::min(best, first[k].cost + rest[0][k]);
        if (clean[0][k] < kInf && !KanaDefect(full, first[k]) && !NameEdge(first[k])) tidy = std::min(tidy, first[k].cost + clean[0][k]);
        if (first[k].word && !first[k].joined && first[k].end == e0) record = std::min(record, first[k].cost + rest[0][k]);
    }
    if (record >= kInf || best >= record) return records;
    if (tidy != best) return records;
    // A path of the window from the first edge k0 of cost total; a tie keeps the edge the lookup lists first.
    auto walk = [&](std::size_t k0, const std::vector<std::vector<int>>& table, bool tidyOnly) {
        std::vector<const SegEdge*> path{&first[k0]};
        for (;;) {
            const SegEdge& e = *path.back();
            if (e.end >= w.limit) break;
            const std::size_t i = static_cast<std::size_t>(e.from - start);
            const int want = table[i][static_cast<std::size_t>(&e - w.edges[i].data())];
            const std::size_t j = static_cast<std::size_t>(e.end - start);
            const SegEdge* next = nullptr;
            for (std::size_t m = 0; m < w.edges[j].size() && next == nullptr; m++) {
                if (table[j][m] >= kInf) continue;
                if (tidyOnly && (KanaDefect(full, w.edges[j][m]) || NameEdge(w.edges[j][m]))) continue;
                const int step = Step(t, full, edgesAt, e, w.edges[j][m]);
                if (step < kInf && step + table[j][m] == want) next = &w.edges[j][m];
            }
            if (next == nullptr) break;
            path.push_back(next);
        }
        return path;
    };
    std::size_t kl = first.size(), kr = first.size();
    for (std::size_t k = 0; k < first.size(); k++) {
        if (rest[0][k] >= kInf) continue;
        if (kl == first.size() && clean[0][k] < kInf && !KanaDefect(full, first[k]) && !NameEdge(first[k]) && first[k].cost + clean[0][k] == best) kl = k;
        if (kr == first.size() && first[k].word && !first[k].joined && first[k].end == e0 && first[k].cost + rest[0][k] == record) kr = k;
    }
    if (kl == first.size() || kr == first.size() || first[kl].end == e0) return records;
    const std::vector<const SegEdge*> lp = walk(kl, clean, true), rp = walk(kr, rest, false);
    // The first word W of the lattice's path that the pick's reading cuts and that is a noun, adjective or phrase row
    // of three or more hiragana, or a word with a kanji whose head a boundary row starting in kana takes.
    auto cutter = [&](const SegEdge& e) -> const SegEdge* {
        for (const SegEdge* r : rp) {
            if (r->from < e.end && r->end > e.from && (r->from > e.from || r->end < e.end)) return r;
        }
        return nullptr;
    };
    // A boundary row: an adverb or conjunction row starting in kana (その上, それでは, いくら) that may take the head of
    // the next word.
    auto boundary = [&](const SegEdge& by) { return by.part.size() == 4 && by.part[1] == '3' && HiraRune(RuneAt(full, by.from)); };
    auto qualifies = [&](const SegEdge& e, const SegEdge& by) {
        if (e.joined || e.part.size() != 4) return false;
        // The head of a boundary row: the row starts before W and ends inside it, and the lattice keeps a word of two
        // or more characters of it before W (その|上着, それ|ではない; not も|うさ for もう|さ).
        bool head = by.from < e.from && by.end < e.end && boundary(by);
        if (head) {
            const SegEdge* kept = nullptr;
            for (const SegEdge* l : lp) {
                if (l->end == e.from) kept = l;
            }
            head = kept != nullptr && kept->from >= by.from && kept->word && !kept->particle && Chars(full.substr(static_cast<std::size_t>(kept->from), static_cast<std::size_t>(kept->end - kept->from))) >= 2;
            // An adjective W keeps a pronoun before it, and the search reads the adjective after the row too: the row
            // only moves a particle (それ|ではない for それでは|ない; not まる|でない for まるで|ない, どう|でもよく).
            if (head && e.part[1] == '7') {
                bool adjective = false;
                for (const SegEdge* r : rp) adjective = adjective || (r->from == by.end && r->part.size() == 4 && r->part[1] == '7');
                head = adjective && kept->part.size() == 4 && kept->part[2] == '2';
            }
        }
        // A row the search reads as one kana particle and a word, or as a word and a particle, is a phrase across a
        // particle (の|ところ, と|だな, なか|でも, など|では); one whose last piece the search reads as an adverb, a verb,
        // an adjective or an adjectival noun of two or more kana (さん|いつ, なのかも|しれない, もあり|そう), or that holds a piece of two or more
        // kana read only as a verb (そうな|ん), is a phrase over that word. Neither is a word the search breaks.
        for (const SegEdge* r : rp) {
            if (head) break;
            // The last piece may run on past W (なのかも|しれないが, から|ちゃんと, もう|ふとん).
            if (r->from > e.from && r->from < e.end && r->end > e.end && r->part.size() == 4) {
                const std::string_view on = full.substr(static_cast<std::size_t>(r->from), static_cast<std::size_t>(r->end - r->from));
                if ((r->part[1] == '4' || r->part[1] == '7' || r->part[1] == '8') && Chars(on) >= 2) return false;
                // A kana noun the search reads past W that is as long as W: each reading cuts a kana word of the same
                // size, and the search's stands (もう|ふとん for もうふ|と; not もう|ろうと for もうろう|と).
                if (r->part[1] == '1' && AllHiragana(on) && Chars(on) >= 3 &&
                    Chars(on) >= Chars(full.substr(static_cast<std::size_t>(e.from), static_cast<std::size_t>(e.end - e.from)))) {
                    return false;
                }
            }
            if (r->from < e.from || r->end > e.end) continue;
            if (r->particle && (r->end == e.end || (r->from == e.from && r->end - r->from == CharLen(full, r->from)))) return false;
            if (r->part.size() != 4 || Chars(full.substr(static_cast<std::size_t>(r->from), static_cast<std::size_t>(r->end - r->from))) < 2) continue;
            if (r->end == e.end && (r->part[1] == '3' || r->part[1] == '4' || r->part[1] == '7' || r->part[1] == '8')) return false;
            if (r->part[1] == '4') {
                bool other = false;
                for (const SegEdge& x : edgesAt(r->from)) {
                    other = other || (x.word && x.end == r->end && x.part.size() == 4 && x.part[1] != '4');
                }
                if (!other) return false;
            }
        }
        const std::string_view sp = full.substr(static_cast<std::size_t>(e.from), static_cast<std::size_t>(e.end - e.from));
        if (e.part[1] == '7') return head && Chars(sp) >= 3;
        if (e.part[1] != '1' && e.part[1] != '8') return false;
        if (AllHiragana(sp)) {
            // The search's word that runs into W from before it is a prenominal, an adverb, a conjunction, a pronoun, a
            // particle, a name or a word with its ending (みんなの|ところ, いつか|って, まるで|なかった, あったら|この,
            // ったら|この, のこと|しか, ふたば|あちゃん), or W starts with a particle of two or more kana that ends inside it
            // (から|だ): the search's boundary is the one in the text.
            for (const SegEdge* r : rp) {
                if (r->part.size() != 4) continue;
                if (r->from < e.from && r->end > e.from &&
                    (r->part[1] == '0' || r->part[1] == '3' || r->part[2] == '2' || r->part[1] == '4' || r->part[1] == '7' || r->particle || NameEdge(*r))) {
                    return false;
                }
                // A prenominal of two or more kana starts W (こんな|ん, どんな|とこ).
                if (r->from == e.from && r->end < e.end && r->part[1] == '0' && r->end - r->from > CharLen(full, r->from)) return false;
                if (r->from == e.from && r->end < e.end && r->end - r->from > CharLen(full, r->from)) {
                    // The search's piece that starts W is a particle of two or more kana, or a stem with no ending
                    // that reads as one (から|だ, から|かぶさる with から as an adjective stem; not かい|じゅう).
                    bool particle = r->particle;
                    for (const SegEdge& x : edgesAt(r->from)) particle = particle || (r->stem && x.particle && x.end == r->end);
                    // Not where the search breaks W into three pieces or more, the last running on past W: the particle
                    // is then a piece of W (とも|だ|ちと for ともだち|と; but から|だ, から|ちゃんと: LEX-7920).
                    bool inner = false, closes = false;
                    for (const SegEdge* x : rp) {
                        inner = inner || (x->from == r->end && x->end < e.end);
                        closes = closes || x->end == e.end;
                    }
                    if (particle && (closes || !inner)) return false;
                }
            }
            // W is an adjective with its ending and a noun of two or more kana (いい|こと for いいこと): a phrase.
            for (const SegEdge& x : edgesAt(e.from)) {
                if (!x.word || x.stem || x.part.size() != 4 || x.part[1] != '7' || x.end >= e.end || x.end - x.from <= CharLen(full, x.from)) continue;
                for (const SegEdge& y : edgesAt(x.end)) {
                    if (y.word && y.part.size() == 4 && y.part[1] == '1' && y.end == e.end && y.end - y.from > CharLen(full, y.from)) return false;
                }
            }
            return Chars(sp) >= 3;
        }
        return Chars(sp) >= 2 && head && (text::IsKanji(RuneAt(full, e.from)) || RuneAt(full, e.from) == U'々');
    };
    const SegEdge* cutW = nullptr;
    std::size_t wi = 0;
    for (; wi < lp.size() && wi < 2 && cutW == nullptr && lp[wi]->from < end; wi++) {
        const SegEdge* by = cutter(*lp[wi]);
        if (by != nullptr && qualifies(*lp[wi], *by)) cutW = lp[wi];
    }
    if (cutW == nullptr) return records;
    const SegEdge& W = *cutW;
    // W does not follow a verb or an adjective: kana after a conjugated word is mostly its ending or an auxiliary
    // (聞きたい|こと, not たいこ; 続かない|よう, not いよう).
    // In a form that stands before a noun, a verb or adjective is followed by a noun W (ある|いみ, いい|しなやか: LEX-7920).
    if (W.from == cursor) {
        if (afterVerb) return records;
    } else if (wi >= 2) {
        const SegEdge& before = *lp[wi - 2];
        if (before.part.size() == 4 && (before.part[1] == '4' || before.part[1] == '7') &&
            (!before.conj || !attributive(before.end, before.part[1] == '7' ? 'a' : 'v', before.from))) {
            return records;
        }
    }
    auto particleAt = [&](const SegEdge& e) {
        return e.word && e.part.size() == 4 && e.part[1] == '2' &&
               AllHiragana(full.substr(static_cast<std::size_t>(e.from), static_cast<std::size_t>(e.end - e.from)));
    };
    auto edgesFrom = [&](int at) -> const std::vector<SegEdge>* {
        return at >= start && at < w.limit ? &w.edges[static_cast<std::size_t>(at - start)] : nullptr;
    };
    bool fits = stops(W.end) || (wi < lp.size() && particleAt(*lp[wi]));
    if (!fits && edgesFrom(W.end) != nullptr) {
        for (const SegEdge& x : *edgesFrom(W.end)) {
            const std::string_view sp = full.substr(static_cast<std::size_t>(x.from), static_cast<std::size_t>(x.end - x.from));
            if (!x.word || NameEdge(x) || x.part.size() != 4 || x.part[1] != '1' || !AllHiragana(sp) || Chars(sp) > 2) continue;
            if (stops(x.end)) fits = true;
            if (const auto* after = edgesFrom(x.end); after != nullptr) {
                for (const SegEdge& y : *after) fits = fits || particleAt(y);
            }
        }
    }
    if (!fits) return records;
    // The records of the length of the path's first word go first, in the order the reorders left them (that order
    // chooses among their senses), where the search reads the first of them as the lattice does.
    const SegEdge& lead = first[kl];
    Record* front = nullptr;
    Record* back = nullptr;
    Record** frontTail = &front;
    Record** backTail = &back;
    Record* copies = nullptr;
    Record** copyTail = &copies;
    for (Record* r = records; r != nullptr; r = r->child) {
        if (static_cast<int>(r->source.size()) != lead.key) continue;
        Record* c = NewRecord();
        *c = *r;
        c->child = nullptr;
        *copyTail = c;
        copyTail = &c->child;
    }
    if (copies == nullptr) return records;
    if (const Item* v = InsertLateCandidate(full, cursor, token, copies); v == nullptr || v->start != cursor || cursor + v->length != lead.end) {
        return records;
    }
    for (Record* r = records; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        Record**& tail = static_cast<int>(r->source.size()) == lead.key ? frontTail : backTail;
        *tail = r;
        tail = &r->child;
        r = next;
    }
    *frontTail = back;
    segDecided_.emplace_back(cursor, lead.end);
    return front;
}

// The edges of the whole token for --why, the search's own items among them, and
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
    if (!t.loaded || surface.empty() || !settings.splitWhy) return;
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

}  // namespace hdTrans::word
