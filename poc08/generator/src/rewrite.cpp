#include "hd8gen/rewrite.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <unordered_map>

#include "kir_text.h"
#include "tables.h"

namespace hd8gen {

namespace {

struct FeatTest {
    std::string name;
    bool neg = false;
    std::vector<std::string> values;
};
struct Matcher {
    enum class T { Node, Gap, Start, End } t = T::Node;
    std::string kind;                 // N P V Adv Det Intj Punct Raw or *
    std::vector<std::string> alts;    // lemma (P: kase or lexical lemma; Punct: punct kind)
    std::vector<FeatTest> feats;
    std::string name;
    bool optional = false;
};
struct Atom {
    bool neg = false;
    std::string lhs, op;  // lhs: x.f, x.prev.f, x.next.f, utt.level, utt.narration; op: = != <= >= or "" (boolean)
    std::vector<std::string> values;
};
struct Action {
    std::string op;      // set del replace copy insert-after
    std::string target;  // node name
    std::string feat, value, srcName, srcFeat;
    std::vector<Node> nodes;  // replace / insert-after
};

}  // namespace

struct RuleRow {
    std::string id, phase, scope, pattern, condition, action, note;
    std::vector<Matcher> m;
    std::vector<Atom> cond;
    std::vector<Action> act;
};
struct CaseRow {
    std::string rule, kind, level, input, expected;
};
struct RuleSet {
    std::vector<RuleRow> rules;
    std::vector<CaseRow> cases;
};

namespace {

std::vector<std::string> Tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '[' || c == '{') ++depth;
        if (c == ']' || c == '}') --depth;
        if (c == ' ' && depth == 0) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool ParseFeats(const std::string& f, std::vector<FeatTest>& out, std::string& err) {
    for (const auto& kv : Split(f, ',')) {
        if (kv.empty()) continue;
        FeatTest t;
        size_t eq = kv.find("!=");
        if (eq != std::string::npos) {
            t.neg = true;
            t.name = kv.substr(0, eq);
            t.values = Split(kv.substr(eq + 2), '|');
        } else if ((eq = kv.find('=')) != std::string::npos) {
            t.name = kv.substr(0, eq);
            t.values = Split(kv.substr(eq + 1), '|');
        } else {
            t.name = kv;  // a flag: dep, aux
            t.values = {"1"};
        }
        if (t.name.empty()) {
            err = "bad feature test " + kv;
            return false;
        }
        out.push_back(t);
    }
    return true;
}

bool ParsePattern(const std::string& p, std::vector<Matcher>& out, std::string& err) {
    for (const auto& tok : Tokens(p)) {
        Matcher m;
        if (tok == "...") m.t = Matcher::T::Gap;
        else if (tok == "^") m.t = Matcher::T::Start;
        else if (tok == "$") m.t = Matcher::T::End;
        else {
            if (tok.empty() || tok[0] != '[') {
                err = "bad matcher " + tok;
                return false;
            }
            const size_t close = tok.rfind(']');
            std::string inner = tok.substr(1, close - 1);
            std::string tail = tok.substr(close + 1);
            if (!tail.empty() && tail.back() == '?') {
                m.optional = true;
                tail.pop_back();
            }
            if (!tail.empty() && tail[0] == ':') m.name = tail.substr(1);
            const size_t brace = inner.find('{');
            if (brace != std::string::npos) {
                const size_t bend = inner.rfind('}');
                if (!ParseFeats(inner.substr(brace + 1, bend - brace - 1), m.feats, err)) return false;
                inner = inner.substr(0, brace);
            }
            std::vector<std::string> parts;
            for (const auto& x : Split(inner, ' '))
                if (!x.empty()) parts.push_back(x);
            if (parts.empty()) {
                err = "empty matcher " + tok;
                return false;
            }
            m.kind = parts[0];
            if (parts.size() > 1) m.alts = Split(parts[1], '|');
        }
        out.push_back(m);
    }
    return true;
}

bool ParseCond(const std::string& c, std::vector<Atom>& out, std::string& err) {
    if (c == "-" || c.empty()) return true;
    for (auto a : Split(c, '&')) {
        while (!a.empty() && a.front() == ' ') a.erase(a.begin());
        while (!a.empty() && a.back() == ' ') a.pop_back();
        Atom t;
        if (!a.empty() && a[0] == '!') {
            t.neg = true;
            a = a.substr(1);
        }
        for (const char* op : {"!=", "<=", ">=", "="}) {
            const size_t p = a.find(op);
            if (p != std::string::npos) {
                t.lhs = a.substr(0, p);
                t.op = op;
                t.values = Split(a.substr(p + std::string(op).size()), '|');
                break;
            }
        }
        if (t.op.empty()) t.lhs = a;
        if (t.lhs.empty()) {
            err = "bad condition " + a;
            return false;
        }
        // @y.feat names another node's own feature (no prev./next. hop), and stands alone
        for (const auto& v : t.values)
            if (!v.empty() && v[0] == '@' && (std::count(v.begin(), v.end(), '.') != 1 || t.values.size() != 1)) {
                err = "bad node reference " + v + " (write @y.feat, alone)";
                return false;
            }
        out.push_back(t);
    }
    return true;
}

bool ParseNodeSpec(const std::string& spec, std::vector<Node>& out, std::string& err) {
    // [K lemma {feats}] → kir text K:lemma{feats}
    if (spec.size() < 2 || spec.front() != '[' || spec.back() != ']') {
        err = "bad node " + spec;
        return false;
    }
    std::string inner = spec.substr(1, spec.size() - 2);
    std::string feats;
    const size_t brace = inner.find('{');
    if (brace != std::string::npos) {
        feats = inner.substr(brace);
        inner = inner.substr(0, brace);
    }
    while (!inner.empty() && inner.back() == ' ') inner.pop_back();
    const size_t sp = inner.find(' ');
    const std::string text = (sp == std::string::npos ? inner + ":" : inner.substr(0, sp) + ":" + inner.substr(sp + 1)) + feats;
    return ParseKirText(text, out, err);
}

bool ParseActions(const std::string& a, std::vector<Action>& out, std::string& err) {
    for (auto s : Split(a, ';')) {
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        while (!s.empty() && s.back() == ' ') s.pop_back();
        if (s.empty()) continue;
        Action x;
        const size_t sp = s.find(' ');
        x.op = s.substr(0, sp);
        const std::string rest = sp == std::string::npos ? "" : s.substr(sp + 1);
        if (x.op == "del") {
            x.target = rest;
        } else if (x.op == "set" || x.op == "copy") {
            const size_t dot = rest.find('.'), eq = rest.find('=');
            if (dot == std::string::npos || eq == std::string::npos) {
                err = "bad action " + s;
                return false;
            }
            x.target = rest.substr(0, dot);
            x.feat = rest.substr(dot + 1, eq - dot - 1);
            x.value = rest.substr(eq + 1);
            if (x.op == "copy") {
                const size_t d2 = x.value.find('.');
                if (d2 == std::string::npos) {
                    err = "bad copy " + s;
                    return false;
                }
                x.srcName = x.value.substr(0, d2);
                x.srcFeat = x.value.substr(d2 + 1);
            }
        } else if (x.op == "replace" || x.op == "insert-after") {
            const size_t sp2 = rest.find(' ');
            x.target = rest.substr(0, sp2);
            const std::string specs = sp2 == std::string::npos ? "" : rest.substr(sp2 + 1);
            for (const auto& spec : Tokens(specs)) {
                std::vector<Node> ns;
                if (!ParseNodeSpec(spec, ns, err)) return false;
                for (auto& n : ns) x.nodes.push_back(n);
            }
        } else {
            err = "unknown action " + x.op;
            return false;
        }
        out.push_back(x);
    }
    return true;
}

// ---------------------------------------------------------------- matching

struct Ctx {
    const Tables& gen;
    std::vector<Node>& nodes;
    std::vector<bool>& dead;
    std::unordered_map<uint32_t, Reg>& reg;
    const std::unordered_map<uint16_t, UttLevel>& utt;
};

bool Boundary(const Node& n) {
    return n.kind == Kind::Punct && (n.punct == Punct::end || n.punct == Punct::q || n.punct == Punct::excl ||
                                     n.punct == Punct::qopen || n.punct == Punct::qclose);
}

std::string Feature(const Ctx& c, size_t i, const std::string& name) {
    const Node& n = c.nodes[i];
    if (name == "level" || name == "hon" || name == "persona") {
        auto r = c.reg.find(n.id);
        if (name == "hon") return r != c.reg.end() && r->second.hon ? "1" : "0";
        Level l = Level::none;
        if (r != c.reg.end()) l = r->second.level;
        if (l == Level::none) {
            auto u = c.utt.find(n.utt);
            if (u != c.utt.end()) l = u->second.level;
        }
        return Name(l);
    }
    if (name == "class") return c.gen.ClassesOf(n.lemma);
    if (name == "cls") return n.kcls.empty() ? LookupPredicate(c.gen, n.lemma).cls : n.kcls;
    if (name == "pos") return n.vpos.empty() ? LookupPredicate(c.gen, n.lemma).pos : n.vpos;
    return GetFeature(n, name);
}

bool FeatOk(const Ctx& c, size_t i, const FeatTest& f) {
    const std::string v = Feature(c, i, f.name);
    bool any = false;
    // sem and class hold several values joined by |: a test holds when one of them is wanted
    if (f.name == "sem" || f.name == "class") {
        for (const auto& want : f.values)
            for (const auto& have : Split(v, '|'))
                if (have == want) any = true;
    } else {
        for (const auto& want : f.values)
            if (v == want) any = true;
    }
    return f.neg ? !any : any;
}

bool NodeOk(const Ctx& c, size_t i, const Matcher& m) {
    const Node& n = c.nodes[i];
    if (m.kind != "*" && GetFeature(n, "kind") != m.kind) return false;
    if (!m.alts.empty()) {
        bool ok = false;
        for (const auto& a : m.alts) {
            if (n.kind == Kind::P && (n.kase == a || (n.kase == "lex" && n.lemma == a))) ok = true;
            else if (n.kind == Kind::Punct && GetFeature(n, "punct") == a) ok = true;
            else if (n.lemma == a) ok = true;
        }
        if (!ok) return false;
    }
    for (const auto& f : m.feats)
        if (!FeatOk(c, i, f)) return false;
    return true;
}

// next live index after i (or size)
size_t Next(const Ctx& c, size_t i) {
    ++i;
    while (i < c.nodes.size() && c.dead[i]) ++i;
    return i;
}
size_t Prev(const Ctx& c, size_t i) {  // returns size() when none
    while (i > 0) {
        --i;
        if (!c.dead[i]) return i;
    }
    return c.nodes.size();
}

bool MatchFrom(const Ctx& c, const std::vector<Matcher>& ms, size_t mi, size_t pos, std::map<std::string, size_t>& b,
               size_t& end) {
    if (mi == ms.size()) {
        end = pos;
        return true;
    }
    const Matcher& m = ms[mi];
    switch (m.t) {
        case Matcher::T::Start: {
            const size_t p = Prev(c, pos);
            if (p == c.nodes.size() || Boundary(c.nodes[p])) return MatchFrom(c, ms, mi + 1, pos, b, end);
            return false;
        }
        case Matcher::T::End:
            if (pos >= c.nodes.size() || Boundary(c.nodes[pos])) return MatchFrom(c, ms, mi + 1, pos, b, end);
            return false;
        case Matcher::T::Gap: {
            size_t p = pos;
            for (;;) {
                if (MatchFrom(c, ms, mi + 1, p, b, end)) return true;
                if (p >= c.nodes.size() || Boundary(c.nodes[p])) return false;
                p = Next(c, p);
            }
        }
        case Matcher::T::Node:
            if (pos < c.nodes.size() && NodeOk(c, pos, m)) {
                if (!m.name.empty()) b[m.name] = pos;
                if (MatchFrom(c, ms, mi + 1, Next(c, pos), b, end)) return true;
                if (!m.name.empty()) b.erase(m.name);
            }
            if (m.optional) return MatchFrom(c, ms, mi + 1, pos, b, end);
            return false;
    }
    return false;
}

size_t ClauseLen(const Ctx& c, size_t i) {
    // content words from the previous boundary (punctuation or a connective/final predicate) to i
    size_t n = 0;
    size_t k = i;
    for (;;) {
        const Node& x = c.nodes[k];
        if (!c.dead[k]) {
            if (k != i && (x.kind == Kind::Punct || (x.kind == Kind::V && x.ctype != CType::adn && !x.aux))) break;
            if (x.kind == Kind::N || x.kind == Kind::V || x.kind == Kind::Adv || x.kind == Kind::Det || x.kind == Kind::Raw ||
                x.kind == Kind::Intj)
                ++n;
        }
        if (k == 0) break;
        --k;
    }
    return n;
}

bool CondOk(const Ctx& c, const std::vector<Atom>& cond, const std::map<std::string, size_t>& b, size_t first) {
    for (const auto& a : cond) {
        bool r = false;
        if (a.lhs.rfind("utt.", 0) == 0) {
            auto u = c.utt.find(c.nodes[first].utt);
            const std::string f = a.lhs.substr(4);
            if (f == "narration") r = u != c.utt.end() && u->second.narration;
            else if (f == "quote") r = !(u != c.utt.end() && u->second.narration);
            else if (f == "level") {
                const std::string v = u != c.utt.end() ? Name(u->second.level) : "none";
                for (const auto& w : a.values)
                    if (v == w) r = true;
                if (a.op == "!=") r = !r;
            }
        } else {
            const size_t dot = a.lhs.find('.');
            const std::string name = a.lhs.substr(0, dot);
            std::string f = a.lhs.substr(dot + 1);
            auto it = b.find(name);
            if (it == b.end()) {
                r = false;  // an unbound optional node: atoms about it are false
            } else {
                size_t i = it->second;
                bool ok = true;
                if (f.rfind("prev.", 0) == 0) {
                    i = Prev(c, i);
                    f = f.substr(5);
                    ok = i < c.nodes.size();
                } else if (f.rfind("next.", 0) == 0) {
                    i = Next(c, i);
                    f = f.substr(5);
                    ok = i < c.nodes.size();
                }
                if (!ok) {
                    r = a.op == "!=";
                } else if (f == "clauselen") {
                    const long len = static_cast<long>(ClauseLen(c, i));
                    const long lim = std::atol(a.values[0].c_str());
                    r = a.op == "<=" ? len <= lim : a.op == ">=" ? len >= lim : len == lim;
                } else if (a.op.empty()) {
                    r = Feature(c, i, f) == "1";
                } else {
                    // a value @y.feat is the feature of another bound node; - is the empty value
                    std::vector<std::string> vals = a.values;
                    for (auto& v : vals) {
                        if (v.size() > 1 && v[0] == '@') {
                            const size_t d2 = v.find('.');
                            auto o = d2 == std::string::npos ? b.end() : b.find(v.substr(1, d2 - 1));
                            v = o == b.end() ? std::string("\x01") : Feature(c, o->second, v.substr(d2 + 1));
                        } else if (v == "-") {
                            v.clear();
                        }
                    }
                    FeatTest t{f, a.op == "!=", vals};
                    r = FeatOk(c, i, t);
                }
            }
        }
        if (a.neg) r = !r;
        if (!r) return false;
    }
    return true;
}

uint32_t MaxId(const std::vector<Node>& ns) {
    uint32_t m = 0;
    for (const auto& n : ns) m = std::max(m, n.id);
    return m;
}

struct Edit {
    uint32_t origin;
    std::string feat, value;  // feat "del" for deletion
};

void Apply(Ctx& c, const RuleRow& r, const std::map<std::string, size_t>& b, std::vector<std::pair<size_t, std::vector<Node>>>& inserts,
           std::vector<Edit>* edits) {
    for (const auto& a : r.act) {
        auto it = b.find(a.target);
        if (it == b.end()) continue;
        Node& n = c.nodes[it->second];
        if (a.op == "del") {
            c.dead[it->second] = true;
            if (edits) edits->push_back({n.origin, "del", ""});
        } else if (a.op == "set" || a.op == "copy") {
            std::string v = a.value;
            if (a.op == "copy") {
                auto s = b.find(a.srcName);
                if (s == b.end()) continue;
                v = Feature(c, s->second, a.srcFeat);
            }
            if (edits) edits->push_back({n.origin, a.feat, v});
            if (a.feat == "level" || a.feat == "hon" || a.feat == "persona") {
                Reg& g = c.reg[n.id];
                g.node = n.id;
                if (g.level == Level::none) {
                    auto u = c.utt.find(n.utt);
                    if (u != c.utt.end()) g.level = u->second.level;
                }
                if (a.feat == "level") ParseLevel(v, g.level);
                else if (a.feat == "persona") ParsePersona(v, g.persona);
                else g.hon = v == "1";
            } else {
                SetFeature(n, a.feat, v);
            }
        } else if (a.op == "replace" && !a.nodes.empty()) {
            Node keep = n;
            Node rep = a.nodes[0];
            rep.id = keep.id;
            rep.origin = keep.origin;
            rep.utt = keep.utt;
            rep.clause = keep.clause;
            rep.src = keep.src;
            n = rep;
            if (edits) edits->push_back({keep.origin, "replace", KirText({rep})});
            if (a.nodes.size() > 1) {
                std::vector<Node> more(a.nodes.begin() + 1, a.nodes.end());
                for (auto& m : more) {
                    m.origin = keep.origin;
                    m.utt = keep.utt;
                    m.clause = keep.clause;
                }
                inserts.push_back({it->second, more});
            }
        } else if (a.op == "insert-after") {
            std::vector<Node> more = a.nodes;
            for (auto& m : more) {
                m.origin = n.origin;
                m.utt = n.utt;
                m.clause = n.clause;
            }
            inserts.push_back({it->second, more});
            if (edits) edits->push_back({n.origin, "insert", KirText(more)});
        }
    }
}

bool InScope(const Ctx& c, const RuleRow& r, size_t i) {
    if (r.scope == "any") return true;
    auto u = c.utt.find(c.nodes[i].utt);
    const bool narr = u != c.utt.end() && u->second.narration;
    if (r.scope == "quote") return !narr;
    if (r.scope == "narr") return narr;
    return true;
}

RewriteResult Run(const RuleSet& rs, const Tables& t, const std::vector<Node>& kir, const std::vector<Reg>& reg,
                  const std::vector<UttLevel>& lev, const std::vector<std::string>& only, const std::vector<std::string>& skip,
                  std::map<std::string, std::vector<Edit>>* edits) {
    RewriteResult res;
    std::vector<Node> nodes = kir;
    std::unordered_map<uint32_t, Reg> regs;
    for (const auto& r : reg) regs[r.node] = r;
    std::unordered_map<uint16_t, UttLevel> utt;
    for (const auto& u : lev) utt[u.utt] = u;
    std::vector<const RuleRow*> order;
    for (const char* phase : {"disc", "ko"})
        for (const auto& r : rs.rules) {
            if (r.phase != phase) continue;
            bool on = only.empty();
            for (const auto& o : only)
                if (r.id.rfind(o, 0) == 0) on = true;
            for (const auto& s : skip)
                if (r.id.rfind(s, 0) == 0) on = false;
            if (on) order.push_back(&r);
        }
    std::stable_sort(order.begin(), order.end(), [](const RuleRow* a, const RuleRow* b) {
        if (a->phase != b->phase) return a->phase == "disc";
        return a->id < b->id;
    });
    for (const RuleRow* r : order) {
        std::vector<bool> dead(nodes.size(), false);
        Ctx c{t, nodes, dead, regs, utt};
        std::vector<std::pair<size_t, std::vector<Node>>> inserts;
        size_t i = 0;
        while (i < nodes.size()) {
            if (dead[i] || !InScope(c, *r, i)) {
                ++i;
                continue;
            }
            std::map<std::string, size_t> b;
            size_t end = i;
            if (MatchFrom(c, r->m, 0, i, b, end) && end > i && CondOk(c, r->cond, b, i)) {
                res.fired.push_back({r->id, nodes[i].origin});
                Apply(c, *r, b, inserts, edits ? &(*edits)[r->id] : nullptr);
                i = end;
            } else {
                ++i;
            }
        }
        // compact: drop dead nodes, add inserts
        std::vector<Node> next;
        uint32_t id = MaxId(nodes);
        std::sort(inserts.begin(), inserts.end(), [](const auto& a, const auto& b2) { return a.first < b2.first; });
        size_t ins = 0;
        bool clearGlue = false;
        for (size_t k = 0; k < nodes.size(); ++k) {
            if (dead[k]) {
                // the word after a deleted mark was attached to the mark, not to the word before it
                if (nodes[k].kind == Kind::Punct) clearGlue = true;
            } else {
                Node keep = nodes[k];
                if (clearGlue && keep.kind != Kind::P && keep.kind != Kind::Punct) keep.glue = false;
                clearGlue = false;
                next.push_back(keep);
            }
            while (ins < inserts.size() && inserts[ins].first == k) {
                for (auto n : inserts[ins].second) {
                    n.id = ++id;
                    next.push_back(n);
                }
                ++ins;
            }
        }
        nodes.swap(next);
    }
    res.kir2 = nodes;
    for (const auto& n : nodes) {
        auto r = regs.find(n.id);
        if (r != regs.end()) res.reg.push_back(r->second);
    }
    return res;
}

}  // namespace

std::shared_ptr<const RuleSet> LoadRules(const std::string& dir, std::string& err) {
    auto rs = std::make_shared<RuleSet>();
    TsvTable t;
    const std::string p = dir + "/rules.tsv";
    if (!ReadTsv(p, t, err)) return nullptr;
    const int ci = t.Col("id"), cp = t.Col("phase"), cs = t.Col("scope"), cpat = t.Col("pattern"), cc = t.Col("condition"),
              ca = t.Col("action"), cn = t.Col("note"), csp = t.Col("spec");
    if (ci < 0 || cp < 0 || cs < 0 || cpat < 0 || cc < 0 || ca < 0) {
        err = p + ": missing column";
        return nullptr;
    }
    std::set<std::string> ids;
    for (size_t k = 0; k < t.rows.size(); ++k) {
        const auto& r = t.rows[k];
        RuleRow row{r[ci], r[cp], r[cs], r[cpat], r[cc], r[ca], cn >= 0 ? r[cn] : "", {}, {}, {}};
        std::string e;
        if (!ids.insert(row.id).second) e = "duplicate id";
        else if (csp >= 0 && r[csp].rfind("RW-", 0) != 0) e = "spec must be a rule id RW-n of spec/rewrite.md";
        else if (row.phase != "disc" && row.phase != "ko") e = "phase must be disc or ko";
        else if (row.scope != "any" && row.scope != "quote" && row.scope != "narr") e = "scope must be any, quote or narr";
        else if (!ParsePattern(row.pattern, row.m, e) || !ParseCond(row.condition, row.cond, e) || !ParseActions(row.action, row.act, e)) {
        }
        if (!e.empty()) {
            err = p + ":" + std::to_string(t.lines[k]) + ": " + row.id + ": " + e;
            return nullptr;
        }
        rs->rules.push_back(std::move(row));
    }
    TsvTable cs2;
    const std::string pc = dir + "/rules.cases.tsv";
    if (!ReadTsv(pc, cs2, err)) return nullptr;
    const int ki = cs2.Col("id"), kk = cs2.Col("kind"), kl = cs2.Col("level"), kin = cs2.Col("input"), ke = cs2.Col("expected");
    if (ki < 0 || kk < 0 || kl < 0 || kin < 0 || ke < 0) {
        err = pc + ": missing column";
        return nullptr;
    }
    for (const auto& r : cs2.rows) rs->cases.push_back({r[ki], r[kk], r[kl], r[kin], r[ke]});
    return rs;
}

std::vector<std::string> RuleIds(const RuleSet& rs) {
    std::vector<std::string> out;
    for (const auto& r : rs.rules) out.push_back(r.id);
    return out;
}

RewriteResult Rewrite(const RuleSet& rs, const Tables& t, const std::vector<Node>& kir, const std::vector<Reg>& reg,
                      const std::vector<UttLevel>& lev, const std::vector<std::string>& only, const std::vector<std::string>& skip) {
    return Run(rs, t, kir, reg, lev, only, skip, nullptr);
}

LintReport Lint(const RuleSet& rs, const Tables& t) {
    LintReport rep;
    // a class a rule names must exist in ko/classes.tsv
    auto checkClass = [&](const std::string& rule, const std::string& feat, const std::vector<std::string>& values) {
        if (feat != "class") return;
        for (const auto& v : values)
            if (!v.empty() && v[0] != '@' && !t.classNames.count(v)) rep.problems.push_back(rule + ": unknown class " + v);
    };
    for (const auto& r : rs.rules) {
        for (const auto& m : r.m)
            for (const auto& f : m.feats) checkClass(r.id, f.name, f.values);
        for (const auto& a : r.cond) {
            const size_t dot = a.lhs.rfind('.');
            if (dot != std::string::npos) checkClass(r.id, a.lhs.substr(dot + 1), a.values);
        }
    }
    std::map<std::string, std::pair<int, int>> counts;  // fix, keep
    for (const auto& r : rs.rules) counts[r.id] = {0, 0};
    for (const auto& cs : rs.cases) {
        CaseResult cr;
        cr.rule = cs.rule;
        cr.kind = cs.kind;
        cr.input = cs.input;
        cr.expected = cs.expected;
        // a case of rules that only work together names them joined by + (aspect.fold-1+prog.state-2): they run
        // together and the last one is the rule the case is about
        std::vector<std::string> on = Split(cs.rule, '+');
        const std::string own = on.back();
        bool unknown = false;
        for (const auto& id : on) unknown = unknown || !counts.count(id);
        if (unknown) {
            rep.problems.push_back("case for unknown rule " + cs.rule);
            continue;
        }
        if (cs.kind != "fix" && cs.kind != "keep") rep.problems.push_back(cs.rule + ": case kind must be fix or keep");
        else if (on.size() == 1) ++(cs.kind == "fix" ? counts[own].first : counts[own].second);
        std::vector<Node> nodes;
        std::string err;
        if (!ParseKirText(cs.input, nodes, err)) {
            rep.problems.push_back(cs.rule + ": " + err);
            continue;
        }
        UttLevel u;
        u.utt = 0;
        u.narration = cs.level == "narr";
        if (!u.narration && !ParseLevel(cs.level, u.level)) {
            rep.problems.push_back(cs.rule + ": bad level " + cs.level);
            continue;
        }
        if (u.narration) u.level = Level::haera;
        // a rule is gated alone, so its cases run with only that rule on; the overlap check below runs them with
        // every rule on
        const RewriteResult rr = Run(rs, t, nodes, {}, {u}, on, {}, nullptr);
        std::map<std::string, std::vector<Edit>> edits;
        // (a case of rules meant to run together is a composition, not an overlap)
        if (on.size() == 1) Run(rs, t, nodes, {}, {u}, {}, {}, &edits);
        for (const auto& f : rr.fired)
            if (f.rule == own) cr.fired = true;
        const GenResult g = Generate(t, rr.kir2, rr.reg, {u});
        cr.got = g.out;
        cr.pass = cr.got == cs.expected && (cs.kind == "fix" ? cr.fired : !cr.fired);
        if (!cr.pass)
            rep.problems.push_back(cs.rule + " " + cs.kind + ": " + (cs.kind == "fix" && !cr.fired ? "rule did not fire; " : "") +
                                   (cs.kind == "keep" && cr.fired ? "rule fired; " : "") + "got \"" + cr.got + "\" expected \"" +
                                   cs.expected + "\"");
        // overlap: two rules of one phase editing the same node feature differently, or one deleting what another edits
        std::map<std::pair<uint32_t, std::string>, std::pair<std::string, std::string>> seen;  // (origin, feat) → (rule, value)
        std::map<uint32_t, std::string> deleted;
        for (const auto& r : rs.rules) {
            auto it = edits.find(r.id);
            if (it == edits.end()) continue;
            for (const auto& e : it->second) {
                if (e.feat == "del") deleted[e.origin] = r.id;
            }
        }
        for (const auto& r : rs.rules) {
            auto it = edits.find(r.id);
            if (it == edits.end()) continue;
            for (const auto& e : it->second) {
                if (e.feat == "del") continue;
                auto d = deleted.find(e.origin);
                if (d != deleted.end() && d->second != r.id) {
                    const RuleRow* other = nullptr;
                    for (const auto& r2 : rs.rules)
                        if (r2.id == d->second) other = &r2;
                    if (other && other->phase == r.phase)
                        rep.problems.push_back("overlap: " + r.id + " edits a node that " + d->second + " deletes (case " + cs.rule + ")");
                }
                auto key = std::make_pair(e.origin, e.feat);
                auto s = seen.find(key);
                if (s != seen.end() && s->second.first != r.id && s->second.second != e.value) {
                    const RuleRow* other = nullptr;
                    for (const auto& r2 : rs.rules)
                        if (r2.id == s->second.first) other = &r2;
                    if (other && other->phase == r.phase)
                        rep.problems.push_back("overlap: " + r.id + " and " + s->second.first + " set " + e.feat +
                                               " differently (case " + cs.rule + ")");
                }
                seen[key] = {r.id, e.value};
            }
        }
        rep.cases.push_back(cr);
    }
    for (const auto& kv : counts) {
        if (kv.second.first == 0) rep.problems.push_back(kv.first + ": no fix case");
        if (kv.second.second == 0) rep.problems.push_back(kv.first + ": no keep case");
    }
    std::sort(rep.problems.begin(), rep.problems.end());
    rep.problems.erase(std::unique(rep.problems.begin(), rep.problems.end()), rep.problems.end());
    return rep;
}

}  // namespace hd8gen
