// The static checks. Every finding is a warning with a flag; they hold for the normalized
// tree and use the dictionaries for codes, rule references and the keys of a rule's rows.
#include <algorithm>
#include <map>

#include "core/rules/match.h"
#include "rulec/rulec.h"

namespace hdTrans::rulec {

namespace {

using rules::NodeKind;
using rules::Op;

std::string DeltaText(int d) { return (d < 0 ? "-" : "+") + std::to_string(d < 0 ? -d : d); }

std::string Show(const Expr& e) {
    const OpInfo& o = Info(e.op);
    std::string s = std::string(1, o.field) + DeltaText(e.delta) + o.flat + " ";
    return s + (o.word ? "@" + DeltaText(e.rightDelta) : "\"" + e.literal + "\"");
}

std::string Key(const Expr& e) {
    return std::to_string(static_cast<int>(e.op)) + ":" + std::to_string(e.delta) + ":" +
           (Info(e.op).word ? std::to_string(e.rightDelta) : e.literal);
}

bool Assigns(const Expr& e) {
    if (e.kind == NodeKind::Atom) return Info(e.op).assigns;
    for (const Expr& c : e.children) {
        if (Assigns(c)) return true;
    }
    return false;
}

bool ReadsTarget(const Expr& e) { return e.op == Op::DstEq || e.op == Op::DstNe || e.op == Op::GobiDstEq; }

Op Negation(Op op, bool& has) {
    has = true;
    switch (op) {
    case Op::SrcEq: return Op::SrcNe;
    case Op::DstEq: return Op::DstNe;
    case Op::PumsaEq: return Op::PumsaNe;
    case Op::AttrEq: return Op::AttrNe;
    case Op::GramEq: return Op::GramNe;
    case Op::GobiSrcEq: return Op::GobiSrcNe;
    case Op::GobiGramEq: return Op::GobiGramNe;
    default: has = false; return op;
    }
}

// The atoms a statement needs: itself, or the atom children of an And or AndAll.
std::vector<const Expr*> Conjuncts(const Expr& e) {
    std::vector<const Expr*> out;
    if (e.kind == NodeKind::Atom) {
        out.push_back(&e);
    } else if (e.kind == NodeKind::And || e.kind == NodeKind::AndAll) {
        for (const Expr& c : e.children) {
            if (c.kind == NodeKind::Atom) out.push_back(&c);
        }
    }
    return out;
}

// Whether e is an atom or a list of atoms, none assigning.
bool PureConjunction(const Expr& e) {
    if (e.kind == NodeKind::Atom) return !Info(e.op).assigns;
    if (e.kind != NodeKind::And && e.kind != NodeKind::AndAll) return false;
    for (const Expr& c : e.children) {
        if (c.kind != NodeKind::Atom || Info(c.op).assigns) return false;
    }
    return !e.children.empty();
}

// Whether some surface satisfies both positive surface patterns.
bool SurfacesMeet(std::string_view a, std::string_view b) {
    std::string_view va, vb;
    Match ma = SplitSurface(a, va), mb = SplitSurface(b, vb);
    if (static_cast<int>(ma) > static_cast<int>(mb)) std::swap(ma, mb), std::swap(va, vb);
    switch (ma) {
    case Match::Exact:
        if (mb == Match::Exact) return va == vb;
        if (mb == Match::Prefix) return va.starts_with(vb);
        if (mb == Match::Suffix) return va.ends_with(vb);
        return vb.find(va) != std::string_view::npos;
    case Match::Prefix:
        if (mb == Match::Prefix) return va.starts_with(vb) || vb.starts_with(va);
        if (mb == Match::Suffix) return true;
        return vb.find(va) != std::string_view::npos;
    case Match::Suffix:
        if (mb == Match::Suffix) return va.ends_with(vb) || vb.ends_with(va);
        return vb.find(va) != std::string_view::npos;
    case Match::Inside:
        for (std::size_t i = 0; i < va.size(); i++) {
            if ((static_cast<unsigned char>(va[i]) & 0xc0) == 0x80) continue;
            std::size_t n = 1;
            while (i + n < va.size() && (static_cast<unsigned char>(va[i + n]) & 0xc0) == 0x80) n++;
            if (vb.find(va.substr(i, n)) != std::string_view::npos) return true;
        }
        return false;
    }
    return true;
}

bool CodesMeet(std::string_view a, std::string_view b, const Inventory& inv) {
    if (!inv.loaded) return true;
    for (const std::string& c : inv.partCodes) {
        if (rules::MatchRuleCode(c, a) && rules::MatchRuleCode(c, b)) return true;
    }
    return false;
}

bool LeadsSynthetic(std::string_view pattern) {
    for (std::size_t i = 0; i < pattern.size(); i += 3) {
        if (pattern[i] == '1' || pattern[i] == '2' || pattern[i] == '3') return true;
    }
    return false;
}

class Checker {
public:
    Checker(const File& file, const Inventory& inv, const CheckOptions& options, Diagnostics& diags)
        : file_(file), inv_(inv), options_(options), diags_(diags) {
        for (const auto& f : rules::internal::AttributeFamilies()) families_.insert(std::string(f.name, 3));
    }

    void Run() {
        for (const Rule& r : file_.local) RuleChecks(r);
        for (const Rule& r : file_.global) RuleChecks(r);
        DuplicateRules();
        GlobalShadowing();
        References();
    }

private:
    void Warn(const Rule& r, int statement, const std::string& flag, uint32_t offset, uint32_t length, std::string message) {
        diags_.Warning(flag, offset, length, std::move(message));
        Tag(r, statement);
    }
    void Note(const Rule& r, int statement, uint32_t offset, uint32_t length, std::string message) {
        diags_.Note(offset, length, std::move(message));
        Tag(r, statement);
    }
    void Tag(const Rule& r, int statement) {
        Diagnostic& d = diags_.list.back();
        d.kind = r.kind;
        d.index = r.index;
        d.statement = statement;
    }
    std::string Name(const Rule& r) const { return std::string(1, r.kind) + " " + std::to_string(r.index); }

    void RuleChecks(const Rule& r) {
        if (r.kind == 'G' && r.ugan && !((r.number > 0 && r.number < 1000) || (r.number > 1000 && r.number < 5000)))
            Warn(r, 0, "unreachable-global", r.offset, r.length,
                 "stem rule number " + std::to_string(r.number) + " never runs: stem rules run for 0 < n < 1000 and 1000 < n < 5000");
        for (std::size_t s = 0; s < r.body.children.size(); s++) Walk(r, static_cast<int>(s) + 1, r.body.children[s]);
        OrList(r, 0, r.body);
        KeyMismatch(r);
    }

    void Walk(const Rule& r, int statement, const Expr& e) {
        if (e.kind == NodeKind::Atom) return AtomChecks(r, statement, e);
        for (const Expr& c : e.children) Walk(r, statement, c);
        if (e.kind == NodeKind::Or) OrList(r, statement, e);
        else Conjunction(r, statement, e);
    }

    void AtomChecks(const Rule& r, int s, const Expr& e) {
        const OpInfo& o = Info(e.op);
        if (std::abs(e.delta) > options_.maxDelta || (o.word && std::abs(e.rightDelta) > options_.maxDelta))
            Warn(r, s, "delta-range", e.offset, e.length,
                 "'" + Show(e) + "' looks " + std::to_string(std::max(std::abs(e.delta), std::abs(e.rightDelta))) +
                     " words away; the rules use at most " + std::to_string(options_.maxDelta));
        if (o.word && e.rightDelta == e.delta && !o.assigns)
            Warn(r, s, "self-compare", e.offset, e.length, "'" + Show(e) + "' compares a word with itself");
        if (e.op == Op::DstSetW)
            Warn(r, s, "always-false", e.offset, e.length,
                 "'" + Show(e) + "' (D= @d) is not implemented by the engine and is always false");
        if (o.word) return;
        const std::string& lit = e.literal;
        if (o.assigns ? lit.size() >= 2 && lit.ends_with("  ") : !lit.empty() && (lit.front() == ' ' || lit.back() == ' '))
            Warn(r, s, "literal-space", e.offset, e.length,
                 "'" + Show(e) + "' " + (o.assigns ? "pads its text with trailing spaces" : "tests a value with a space at an end"));
        if (o.operand == Operand::Surface) {
            std::string_view v;
            const Match m = SplitSurface(lit, v);
            const bool source = o.field == 'S' || o.field == 'U';
            if (v.empty() && (m != Match::Exact || source))
                Warn(r, s, "empty-pattern", e.offset, e.length,
                     "'" + Show(e) + "' " + (m == Match::Exact ? "needs an empty source surface" : "matches every surface"));
            return;
        }
        if (o.operand == Operand::Text || !inv_.loaded) return;
        const std::size_t width = o.operand == Operand::Code3 ? 3 : 2;
        if (lit.empty()) {
            Warn(r, s, "empty-pattern", e.offset, e.length, "'" + Show(e) + "' has no code");
            return;
        }
        for (std::size_t i = 0; i < lit.size(); i += width) {
            const std::string c = lit.substr(i, width);
            if (!KnownCode(o, c))
                Warn(r, s, "unknown-code", e.offset, e.length,
                     "'" + c + "' in '" + Show(e) + "' " +
                         (o.operand == Operand::Code3 && (o.field == 'P' || o.field == 'M')
                              ? "matches no part code of the dictionaries"
                              : "is no code of the dictionaries or the attribute families"));
        }
    }

    bool KnownCode(const OpInfo& o, const std::string& c) const {
        if (o.field == 'P' || o.field == 'M') {
            for (const std::string& code : inv_.partCodes) {
                if (rules::MatchRuleCode(code, c)) return true;
            }
            return false;
        }
        if (o.operand == Operand::Code2) {
            // Position conditions of CheckGAttr, and the markers the word stage adds to rule
            // candidates.
            for (const char* engine : {"Ow", "Hz", "Td", "Pi", "Sa", "Ka"}) {
                if (c == engine) return true;
            }
            if (c == "Nq") return true;  // a count, Item::ReadsCount
            return inv_.gramCodes.count(c) > 0;
        }
        const bool familyTest = o.flat == std::string_view("-=") || o.field == 'T';
        if (familyTest && c.find_first_of("*X") != std::string::npos) return true;
        return inv_.semCodes.count(c) > 0 || families_.count(c) > 0;
    }

    void Conjunction(const Rule& r, int s, const Expr& e) {
        std::map<std::string, const Expr*> seen;
        for (std::size_t i = 0; i < e.children.size(); i++) {
            const Expr& c = e.children[i];
            if (c.kind != NodeKind::Atom) continue;
            const auto [it, fresh] = seen.emplace(Key(c), &c);
            if (!fresh && !Info(c.op).assigns)
                Warn(r, s, "duplicate-test", c.offset, c.length, "'" + Show(c) + "' is tested twice in one conjunction");
            if (e.kind == NodeKind::And && Info(c.op).assigns) {
                bool testAfter = false;
                for (std::size_t k = i + 1; k < e.children.size(); k++) {
                    const Expr& n = e.children[k];
                    testAfter = testAfter || n.kind != NodeKind::Atom || !Info(n.op).assigns;
                }
                if (testAfter)
                    Warn(r, s, "assign-then-test", c.offset, c.length,
                         "'" + Show(c) + "' assigns before later tests; the assignment stays when they fail");
            }
            if (c.op == Op::DstSetW && e.kind == NodeKind::And && i + 1 < e.children.size())
                Warn(r, s, "unreachable-test", e.children[i + 1].offset, e.children[i + 1].length,
                     "the tests after the always-false '" + Show(c) + "' never run");
        }
        // Pairs of atoms at one word that cannot hold together.
        bool assignedTarget = false;
        for (std::size_t i = 0; i < e.children.size(); i++) {
            const Expr& a = e.children[i];
            if (a.kind != NodeKind::Atom) {
                assignedTarget = assignedTarget || Assigns(a);
                continue;
            }
            if (Info(a.op).assigns) assignedTarget = true;
            for (std::size_t k = i + 1; k < e.children.size(); k++) {
                const Expr& b = e.children[k];
                if (b.kind != NodeKind::Atom || b.delta != a.delta || Info(a.op).word || Info(b.op).word) continue;
                if (ReadsTarget(a) || ReadsTarget(b)) {
                    bool between = false;
                    for (std::size_t m = i; m <= k; m++) between = between || Assigns(e.children[m]);
                    if (between) continue;
                }
                Pair(r, s, a, b);
            }
        }
    }

    void Pair(const Rule& r, int s, const Expr& a, const Expr& b) {
        bool has = false;
        const Op na = Negation(a.op, has);
        if (has && na == b.op && a.literal == b.literal) {
            return Contradiction(r, s, a, b, "'" + Show(b) + "' negates '" + Show(a) + "'");
        }
        bool hb = false;
        if (Negation(b.op, hb) == a.op && hb && a.literal == b.literal)
            return Contradiction(r, s, a, b, "'" + Show(b) + "' negates '" + Show(a) + "'");
        if (a.op != b.op) return;
        if (a.op == Op::SrcEq || a.op == Op::GobiSrcEq) {
            if (!SurfacesMeet(a.literal, b.literal))
                Contradiction(r, s, a, b, "no surface is both '" + Show(a) + "' and '" + Show(b) + "'");
        } else if ((a.op == Op::PumsaEq || a.op == Op::GobiPumsaEq) && !CodesMeet(a.literal, b.literal, inv_)) {
            const bool synthetic = a.op == Op::PumsaEq && LeadsSynthetic(a.literal) != LeadsSynthetic(b.literal);
            std::string why = "no part code matches both '" + Show(a) + "' and '" + Show(b) + "'";
            if (synthetic) why += " (a candidate with grammar code Pn, Pe or Pj can still pass both)";
            if (a.delta == 0) {
                Contradiction(r, s, a, b, why);
            } else {
                Warn(r, s, "neighbour-contradiction", b.offset, b.length,
                     why + "; both hold only while word " + DeltaText(a.delta) +
                         " has no chosen candidate and has candidates of both kinds");
                Note(r, s, a.offset, a.length, "the other test");
            }
        }
    }

    void Contradiction(const Rule& r, int s, const Expr& a, const Expr& b, const std::string& why) {
        Warn(r, s, "contradiction", b.offset, b.length, why + "; this conjunction is never true");
        Note(r, s, a.offset, a.length, "the other test");
    }

    // Alternatives of an Or that can never be the first true one, and, among the statements of
    // a rule, a lone code test after a conjunction: with prefix counts, a list one short makes
    // its last test a statement of its own.
    void OrList(const Rule& r, int statement, const Expr& e) {
        bool conjunction = false;
        for (std::size_t j = 0; statement == 0 && j < e.children.size(); j++) {
            const Expr& c = e.children[j];
            const bool code = c.kind == NodeKind::Atom && !Info(c.op).assigns &&
                              (Info(c.op).operand == Operand::Code2 || Info(c.op).operand == Operand::Code3);
            if (code && conjunction)
                Warn(r, static_cast<int>(j) + 1, "lone-test", c.offset, c.length,
                     "statement " + std::to_string(j + 1) + " is the single test '" + Show(c) +
                         "' and decides alone; check the counts of the lists before it");
            conjunction = conjunction || c.kind == NodeKind::And;
        }
        for (std::size_t j = 1; j < e.children.size(); j++) {
            const Expr& cj = e.children[j];
            const int sj = statement ? statement : static_cast<int>(j) + 1;
            bool done = false;
            for (std::size_t i = 0; i < j && !done; i++) {
                const Expr& ci = e.children[i];
                const int si = statement ? statement : static_cast<int>(i) + 1;
                if (SameTree(ci, cj)) {
                    Warn(r, sj, "duplicate-statement", cj.offset, cj.length,
                         statement ? "this alternative repeats an earlier one" : "statement " + std::to_string(sj) +
                                                                                    " repeats statement " + std::to_string(si));
                    Note(r, si, ci.offset, ci.length, "the earlier one");
                    done = true;
                    continue;
                }
                if (!PureConjunction(ci)) continue;
                const auto needs = Conjuncts(ci);
                const auto has = Conjuncts(cj);
                bool implied = !has.empty();
                bool readsTarget = false;
                for (const Expr* a : needs) {
                    readsTarget = readsTarget || ReadsTarget(*a);
                    bool found = false;
                    for (const Expr* b : has) found = found || Key(*a) == Key(*b);
                    implied = implied && found;
                }
                if (!implied) continue;
                if (readsTarget) {
                    bool assigns = false;
                    for (std::size_t m = i + 1; m <= j; m++) assigns = assigns || Assigns(e.children[m]);
                    if (assigns) continue;
                }
                const std::string what = statement ? "this alternative" : "statement " + std::to_string(sj);
                const std::string earlier = statement ? "an earlier alternative" : "statement " + std::to_string(si);
                if (Assigns(cj)) {
                    Warn(r, sj, "shadowed", cj.offset, cj.length,
                         what + " never succeeds: it needs every test of " + earlier +
                             ", which is tried first; only its assignments before a failing test take effect");
                } else {
                    Warn(r, sj, "shadowed", cj.offset, cj.length,
                         what + " is never true first: it needs every test of " + earlier + ", which is tried first");
                }
                Note(r, si, ci.offset, ci.length, "the earlier one");
                done = true;
            }
        }
    }

    // A statement of a local rule whose own-surface tests no key of the rule's rows passes:
    // S+0 for word rows, U+0 for ending rows (whose rules run on the word the ending
    // follows). Rules with rows of both kinds are skipped.
    void KeyMismatch(const Rule& r) {
        if (r.kind != 'L' || !inv_.loaded) return;
        const auto words = inv_.ruleKeys.find(r.index);
        const auto endings = inv_.ruleEndingKeys.find(r.index);
        const bool hasWords = words != inv_.ruleKeys.end(), hasEndings = endings != inv_.ruleEndingKeys.end();
        if (hasWords == hasEndings) return;
        const auto it = hasWords ? words : endings;
        const Op own = hasWords ? Op::SrcEq : Op::GobiSrcEq;
        int dead = 0;
        for (std::size_t s = 0; s < r.body.children.size(); s++) {
            const Expr& st = r.body.children[s];
            std::vector<std::vector<const Expr*>> needs;  // each: alternatives of which one must hold
            for (const Expr* a : Conjuncts(st)) {
                if (a->op == own && a->delta == 0) needs.push_back({a});
            }
            if (st.kind == NodeKind::And || st.kind == NodeKind::AndAll) {
                for (const Expr& c : st.children) {
                    if (c.kind != NodeKind::Or) continue;
                    std::vector<const Expr*> alts;
                    bool all = !c.children.empty();
                    for (const Expr& g : c.children) {
                        all = all && g.kind == NodeKind::Atom && g.op == own && g.delta == 0;
                        alts.push_back(&g);
                    }
                    if (all) needs.push_back(alts);
                }
            }
            if (needs.empty()) continue;
            bool someKey = false;
            for (const std::string& key : it->second) {
                bool ok = true;
                for (const auto& alts : needs) {
                    bool one = false;
                    for (const Expr* a : alts) one = one || rules::MatchRuleSurface(key, a->literal);
                    ok = ok && one;
                }
                someKey = someKey || ok;
            }
            if (someKey) continue;
            dead++;
            std::string keys;
            int n = 0;
            for (const std::string& k : it->second) {
                if (n++ == 4) {
                    keys += ", ...";
                    break;
                }
                keys += (keys.empty() ? "" : ", ") + k;
            }
            const Expr* first = needs[0][0];
            Warn(r, static_cast<int>(s) + 1, "key-mismatch", first->offset, first->length,
                 "statement " + std::to_string(s + 1) + " tests the " + (hasWords ? "word's" : "ending's") +
                     " own surface, but " + Name(r) + " belongs to " + (hasWords ? "word" : "ending") +
                     " rows with keys " + keys + "; it is never true");
        }
        if (dead > 0 && dead == static_cast<int>(r.body.children.size()) && Assigns(r.body) == false)
            Warn(r, 0, "never-fires", r.offset, r.length, Name(r) + " can never be true for the rows that name it");
    }

    void DuplicateRules() {
        std::map<std::string, int> bodies;
        for (const Rule& r : file_.local) {
            if (r.body.children.empty()) continue;
            const auto [it, fresh] = bodies.emplace(PrintFlatBody(r.body), r.index);
            if (!fresh)
                Warn(r, 0, "duplicate-rule", r.offset, r.length,
                     Name(r) + " has the body of L " + std::to_string(it->second));
        }
    }

    void GlobalShadowing() {
        const auto& g = file_.global;
        for (std::size_t j = 0; j < g.size(); j++) {
            for (std::size_t i = 0; i < j; i++) {
                if (g[i].ugan != g[j].ugan || g[i].pumsa != g[j].pumsa) continue;
                if (g[i].ugan && (g[i].number < 1000) != (g[j].number < 1000)) continue;
                bool all = !g[j].body.children.empty();
                for (const Expr& sj : g[j].body.children) {
                    bool covered = false;
                    for (const Expr& si : g[i].body.children) {
                        if (!PureConjunction(si)) continue;
                        bool implied = true;
                        for (const Expr* a : Conjuncts(si)) {
                            bool found = !ReadsTarget(*a);
                            if (found) {
                                found = false;
                                for (const Expr* b : Conjuncts(sj)) found = found || Key(*a) == Key(*b);
                            }
                            implied = implied && found;
                        }
                        covered = covered || implied;
                    }
                    all = all && covered;
                }
                if (!all) continue;
                Warn(g[j], 0, "shadowed", g[j].offset, g[j].length,
                     Name(g[j]) + " never fires: " + Name(g[i]) + " runs first for the same words and is true whenever it is");
                Note(g[i], 0, g[i].offset, g[i].length, "the earlier rule");
                break;
            }
        }
    }

    void References() {
        if (!inv_.loaded) return;
        for (const Rule& r : file_.local) {
            if (r.index != 0 && inv_.ruleRows.find(r.index) == inv_.ruleRows.end())
                Warn(r, 0, "unused-rule", r.offset, r.length, "no dictionary row names " + Name(r));
        }
        for (const auto& [rule, rows] : inv_.ruleRows) {
            if (rule >= static_cast<int>(file_.local.size())) {
                diags_.Warning("missing-rule", 0, 0,
                               std::to_string(rows) + " dictionary row(s) name local rule " + std::to_string(rule) +
                                   ", which does not exist; the rule is false for them");
                diags_.list.back().kind = 'L';
                diags_.list.back().index = rule;
            }
        }
    }

    const File& file_;
    const Inventory& inv_;
    const CheckOptions& options_;
    Diagnostics& diags_;
    std::set<std::string> families_;
};

}  // namespace

void Check(const File& file, const Inventory& inventory, const CheckOptions& options, Diagnostics& diags) {
    Checker(file, inventory, options, diags).Run();
}

}  // namespace hdTrans::rulec
