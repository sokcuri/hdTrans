#include "invariants.h"

#include "file.h"
#include "utf8.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string_view>

namespace hd08 {

namespace {

uint32_t Next(std::string_view s, size_t& i) { return NextCodePoint(s, i); }

// Full-width ASCII folded to ASCII; 0 when not a letter or digit.
char Alnum(uint32_t cp) {
    if (cp >= 0xFF01 && cp <= 0xFF5E) cp -= 0xFEE0;
    if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return static_cast<char>(cp);
    return 0;
}

struct Item { std::string text; Span at; };

// Maximal runs of Latin letters or of digits, width-folded.
std::vector<Item> Carried(std::string_view s) {
    std::vector<Item> items;
    size_t i = 0;
    while (i < s.size()) {
        const size_t at = i;
        const char c = Alnum(Next(s, i));
        if (!c) continue;
        // Letters and digits are separate items: G may space 'HP100' as 'HP 100'.
        const auto digit = [](char x) { return x >= '0' && x <= '9'; };
        if (!items.empty() && items.back().at.end == at && digit(items.back().text.back()) == digit(c)) {
            items.back().text += c;
            items.back().at.end = static_cast<uint32_t>(i);
        } else {
            items.push_back({std::string(1, c), {static_cast<uint32_t>(at), static_cast<uint32_t>(i)}});
        }
    }
    return items;
}

bool IsResidue(uint32_t cp) {
    return (cp >= 0x3040 && cp <= 0x30FA) || cp == 0x30FC || (cp >= 0x30FD && cp <= 0x30FF) ||
           (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0xFF66 && cp <= 0xFF9F) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x3005 && cp <= 0x3007);
}

bool IsContent(Pos p) {
    switch (p) {
    case Pos::n: case Pos::pn: case Pos::num: case Pos::v1: case Pos::v5: case Pos::vs: case Pos::vk:
    case Pos::vz: case Pos::adj: case Pos::na: case Pos::adv:
        return true;
    default:
        return false;
    }
}

bool HasFact(const Doc& doc, const Decision& d, std::string_view prefix) {
    for (uint32_t f : d.because)
        if (doc.S(f).starts_with(prefix)) return true;
    return false;
}

Span ClauseSpan(const ConstPathRef& line, const Clause& c) {
    Span s{UINT32_MAX, 0};
    for (uint32_t b = c.firstBun; b <= c.lastBun && b < line.buns.size(); b++) {
        const Bunsetsu& bun = line.buns[b];
        for (uint32_t t = bun.first; t <= bun.last && t < line.toks.size(); t++) {
            s.begin = std::min(s.begin, line.toks[t].src.begin);
            s.end = std::max(s.end, line.toks[t].src.end);
        }
    }
    if (s.begin == UINT32_MAX) s = {};
    return s;
}

// A T decision inside the clause that declares the feature realized by a construction.
bool Declared(const Doc& doc, const Line& line, Span clause, std::string_view prefix) {
    for (const Decision& d : line.decisions)
        if (d.stage == 'T' && Overlaps(d.src, clause) && HasFact(doc, d, prefix)) return true;
    return false;
}

Span NodeSrc(const Line& line, const KNode& n) {
    if (n.src.end > n.src.begin) return n.src;
    if (const KNode* o = FindNode(line.kir, n.origin)) return o->src;
    return {};
}

uint32_t NodeAt(const Line& line, uint32_t outByte) {
    for (const OutSpan& s : line.spans)
        if (s.begin <= outByte && outByte < s.end) return s.node;
    return 0;
}

void Add(std::vector<Flag>& out, uint8_t kind, uint8_t inv, Span src, uint32_t node) {
    Flag f;
    f.kind = kind;
    f.inv = inv;
    f.src = src;
    f.node = node;
    out.push_back(f);
}

}  // namespace

bool LoadInvariantData(const std::string& dataRoot, InvariantData& out, std::string& err) {
    const std::string path = dataRoot + "/ir/data/invariants.tsv";
    std::string text;
    if (!ReadWholeFile(path, text)) return true;
    size_t start = 0;
    int lineNo = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string_view row(text.data() + start, end - start);
        start = end + 1;
        lineNo++;
        if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        if (row.empty() || row[0] == '#') continue;
        const size_t tab = row.find('\t');
        if (tab == std::string_view::npos) {
            err = path + ":" + std::to_string(lineNo) + ": expected kind<TAB>value";
            return false;
        }
        const std::string_view kind = row.substr(0, tab);
        const std::string_view value = row.substr(tab + 1, row.find('\t', tab + 1) - tab - 1);
        if (kind == "neg") out.lexicalNegatives.emplace_back(value);
        else {
            err = path + ":" + std::to_string(lineNo) + ": unknown kind " + std::string(kind);
            return false;
        }
    }
    return true;
}

void CheckInvariants(const Doc& doc, const Line& line, const InvariantData& data, std::vector<Flag>& out) {
    const ConstPathRef path = PathOf(line, line.chosen.path);  // the analysis T used
    auto isLexNeg = [&](uint32_t lemma) {
        const std::string_view s = doc.S(lemma);
        return std::find(data.lexicalNegatives.begin(), data.lexicalNegatives.end(), s) != data.lexicalNegatives.end();
    };

    // I1–I4 per clause.
    for (size_t ci = 0; ci < path.clauses.size(); ci++) {
        const Clause& c = path.clauses[ci];
        const Span span = ClauseSpan(path, c);
        int negs = 0, gramNegs = 0, verbs = 0, past = 0, q = 0, imp = 0, passive = 0, causative = 0;
        uint32_t firstV = 0;
        for (const KNode& n : line.kir2) {
            if (n.clause != ci) continue;
            if (n.neg != kneg::none) gramNegs++;
            if (n.neg != kneg::none || ((n.kind == KK::V || n.kind == KK::N) && isLexNeg(n.lemma))) negs++;
            if (n.kind != KK::V) continue;
            verbs++;
            if (!firstV) firstV = n.id;
            if (n.tense == tense::past) past++;
            if (n.mood == mood::q) q++;
            if (n.mood == mood::imp || n.neg == kneg::ji_ma) imp++;
            if (n.passive) passive++;
            if (n.causative) causative++;
        }
        if (verbs == 0 && line.kir2.empty()) continue;  // nothing generated; I8 reports the drops
        const bool declaredPol = Declared(doc, line, span, "pol=");
        const bool wantNeg = c.pol == pol::neg;
        const int wantCount = c.negFn == negfn::double_ ? 2 : 1;
        if (!declaredPol && ((wantNeg && negs != wantCount) || (!wantNeg && gramNegs > 0)))
            Add(out, flag::flip, 1, span, firstV);
        const bool finalClause = c.link == link::final && c.parent < 0;
        if (finalClause && c.tense == tense::past && past == 0 && verbs > 0 && !Declared(doc, line, span, "tense="))
            Add(out, flag::flip, 2, span, firstV);
        if (finalClause && !Declared(doc, line, span, "force=")) {
            const bool missing = (c.force == force::q && q == 0) ||
                                 ((c.force == force::imp || c.force == force::prohib || c.force == force::request) &&
                                  imp == 0);
            if (missing && verbs > 0) Add(out, flag::flip, 3, span, firstV);
        }
        if (!Declared(doc, line, span, "voice=")) {
            const bool wantPass = c.voice == voice::passive || c.voice == voice::caus_pass;
            const bool wantCaus = c.voice == voice::causative || c.voice == voice::caus_pass;
            if ((wantPass && passive == 0) || (wantCaus && causative == 0)) Add(out, flag::flip, 4, span, firstV);
        }
    }

    // I5 carried letters and digits.
    {
        std::map<std::string, int> balance;
        for (const Item& it : Carried(line.src)) balance[it.text]++;
        for (const Item& it : Carried(line.out)) balance[it.text]--;
        for (const Item& it : Carried(line.src)) {
            auto b = balance.find(it.text);
            if (b != balance.end() && b->second > 0) {
                Add(out, flag::carry, 5, it.at, 0);
                b->second--;
            }
        }
        for (const Item& it : Carried(line.out)) {
            auto b = balance.find(it.text);
            // Digits may come from numerals written in kanji; only Latin text counts as added.
            if (b != balance.end() && b->second < 0 && it.text.find_first_not_of("0123456789") != std::string::npos) {
                const uint32_t node = NodeAt(line, it.at.begin);
                const KNode* n = FindNode(line.kir2, node);
                Add(out, flag::added, 5, n ? NodeSrc(line, *n) : Span{}, node);
                b->second++;
            }
        }
    }

    // I6 accepted names appear.
    for (const Mention& m : line.mentions) {
        if (!m.entity) continue;
        bool found = false;
        for (const KNode& n : line.kir2)
            if (Overlaps(NodeSrc(line, n), m.src)) found = true;
        if (!found) Add(out, flag::dropped, 6, m.src, 0);
    }

    // I7 residue.
    for (size_t i = 0; i < line.out.size();) {
        const size_t at = i;
        if (IsResidue(Next(line.out, i))) {
            const uint32_t node = NodeAt(line, static_cast<uint32_t>(at));
            const KNode* n = FindNode(line.kir2, node);
            Add(out, flag::residue, 7, n ? NodeSrc(line, *n) : Span{}, node);
            while (i < line.out.size()) {  // one flag per run
                size_t j = i;
                if (!IsResidue(Next(line.out, j))) break;
                i = j;
            }
        }
    }

    // I8 consumption: unfolded grammatical tokens and content tokens without a Korean node.
    auto coveredBy = [&](Span s, const std::vector<KNode>& nodes) {
        for (const KNode& n : nodes)
            if (Overlaps(NodeSrc(line, n), s)) return true;
        return false;
    };
    for (const Clause& c : path.clauses) {
        for (uint16_t t : c.unfolded) {
            if (t >= path.toks.size()) continue;
            const Span s = path.toks[t].src;
            bool consumed = coveredBy(s, line.kir);
            for (const Decision& d : line.decisions)
                if (d.stage == 'T' && Overlaps(d.src, s) && d.kind != Ev::UnmatchedForm) consumed = true;
            if (!consumed) Add(out, flag::unmatched_form, 8, s, 0);
        }
    }
    std::vector<bool> folded(path.toks.size(), false);
    for (const Clause& c : path.clauses)
        for (uint16_t t : c.consumed)
            if (t < folded.size()) folded[t] = true;
    for (size_t t = 0; t < path.toks.size(); t++) {
        const Token& tok = path.toks[t];
        if (folded[t] || !IsContent(tok.pos)) continue;
        if (!coveredBy(tok.src, line.kir2)) Add(out, flag::dropped, 8, tok.src, 0);
    }
}

bool IsRiskFlag(uint8_t kind) {
    return kind == flag::fallback || kind == flag::dropped || kind == flag::unmatched_form || kind == flag::added ||
           kind == flag::flip || kind == flag::residue || kind == flag::carry || kind == flag::check;
}

bool IsRiskEvidence(Ev kind) {
    return kind == Ev::Fallback || kind == Ev::Dropped || kind == Ev::UnmatchedForm;
}

}  // namespace hd08
