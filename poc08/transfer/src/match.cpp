// Construction matching and resolution by specificity (no pass order).
#include <algorithm>

#include "ir/json.h"
#include "transfer.h"

namespace hd08::xfer {

namespace {

constexpr int kMaxGap = 14;
// Specificity weights: a matched token counts more than a slot form; a satisfied when atom about as much.
constexpr int kLiteral = 4, kWhen = 2, kClass = 2, kForm = 1;
constexpr int kMaxUnit = 10;
// The anchor atom is at most this many tokens after the start of a match (slots, groups and gaps before it).
constexpr int kMaxReach = 2 * kMaxUnit + kMaxGap + 4;

bool Has(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

bool StartsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

struct Ctx {
  const Table& t;
  const LineIR& ir;
  int n;

  const Tok& T(int i) const { return ir.toks[static_cast<size_t>(i)]; }

  bool InClass(int i, const std::string& cls) const {
    if (i < 0 || i >= n) return false;
    auto it = t.fclass.find(cls);
    if (it == t.fclass.end()) return false;
    const Tok& k = T(i);
    for (const auto& f : it->second) {
      if (!f.pos.empty() && f.pos != "*" && f.pos != k.pos) continue;
      if (!f.udPrefix.empty() && !StartsWith(k.ud, f.udPrefix)) continue;
      if (!f.cform.empty() && !StartsWith(k.cform, f.cform)) continue;
      if (!f.ctype.empty() && !StartsWith(k.ctype, f.ctype)) continue;
      if (Has(f.lemmas, "*") || Has(f.lemmas, k.lemma)) return true;
    }
    return false;
  }

  std::string Form(int i) const {
    const std::string& cf = T(i).cform;
    for (const auto& [prefix, form] : t.forms)
      if (!prefix.empty() && StartsWith(cf, prefix)) return form;
    return {};
  }

  bool IsVerb(int i) const {
    const std::string& p = T(i).pos;
    return p == "v1" || p == "v5" || p == "vs" || p == "vk" || p == "vz";
  }
  bool IsPredStart(int i, char slot) const {
    const std::string& p = T(i).pos;
    if (slot == 'A') return p == "adj";
    if (IsVerb(i) || p == "adj" || p == "na") return true;
    if (p == "n" || p == "pn" || p == "num") {
      if (slot == 'X') return true;
      return i + 1 < n && InClass(i + 1, "suru");
    }
    return false;
  }
  bool IsNoun(int i) const {
    const std::string& p = T(i).pos;
    return p == "n" || p == "pn" || p == "num";
  }
  // a token that continues a predicate unit: auxiliaries, て, non-independent verbs/adjectives, する after a noun
  bool UnitCont(int j) const { return T(j).pos == "aux" || InClass(j, "unit"); }

  int ClauseOfTok(int i) const {
    if (i < 0 || i >= n) return -1;
    int b = T(i).bun;
    if (b < 0) return -1;
    return ir.buns[static_cast<size_t>(b)].clause;
  }
  int ClauseOfPred(int i) const {
    for (size_t c = 0; c < ir.clauses.size(); ++c)
      if (ir.clauses[c].pred == i) return static_cast<int>(c);
    return ClauseOfTok(i);
  }
  // the clause a span ending at token i attaches to (its main clause)
  int MainOf(int i) const {
    if (i < 0 || i >= n) return -1;
    int b = T(i).bun;
    if (b < 0) return -1;
    const Bun& bun = ir.buns[static_cast<size_t>(b)];
    int c = bun.clause;
    if (bun.type == 'P' && c >= 0 && ir.clauses[static_cast<size_t>(c)].lastBun == b)
      return ir.clauses[static_cast<size_t>(c)].parent;
    int h = bun.head, guard = 0;
    while (h >= 0 && static_cast<size_t>(h) < ir.buns.size() && ir.buns[static_cast<size_t>(h)].type != 'P' && guard++ < 64)
      h = ir.buns[static_cast<size_t>(h)].head;
    if (h < 0 || static_cast<size_t>(h) >= ir.buns.size()) return -1;
    return ir.buns[static_cast<size_t>(h)].clause;
  }
};

struct St {
  size_t ai = 0;
  int ti = 0;
  std::vector<int> lits;
  int vTok = -1, vEnd = -1, nTok = -1, score = 0, begin = 0;
};

bool UnitForms(const Ctx& c, const Atom& a, int i, int j) {
  const Tok& head = c.T(i);
  for (const auto& f : a.forms) {
    bool ok = true;
    if (f == "dict") {
      for (int k = i + 1; k <= j && ok; ++k) ok = !(c.InClass(k, "past") && c.Form(k) != "katei");
      std::string fm = c.Form(j);
      ok = ok && (fm == "shushi" || fm == "rentai");
    } else if (f == "ta") {
      ok = j > i && c.InClass(j, "past") && c.Form(j) != "katei";
    } else if (f == "te") {
      ok = j > i && c.InClass(j, "te");
    } else if (f == "neg" || f == "pos") {
      int negs = 0;
      for (int k = i + 1; k <= j; ++k) negs += c.InClass(k, "neg") ? 1 : 0;
      ok = (f == "neg") == (negs % 2 == 1);
    } else if (f == "renyou") {
      ok = j == i && c.Form(i) == "renyou";
    } else if (f == "mizen") {
      ok = c.Form(j) == "mizen";
    } else if (f == "katei") {
      ok = c.Form(j) == "katei";
    } else if (f == "adn") {
      std::string fm = c.Form(j);
      ok = fm == "rentai" || fm == "shushi";
    } else if (f == "vol") {
      ok = c.InClass(j, "vol");
    } else if (f == "pass" || f == "caus" || f == "polite") {
      ok = false;
      for (int k = i + 1; k <= j; ++k) ok = ok || c.InClass(k, f);
    } else if (f == "nopolite") {
      for (int k = i + 1; k <= j && ok; ++k) ok = !c.InClass(k, "polite");
    } else if (f == "bare") {
      ok = j == i;
    } else if (f == "verb") {
      ok = c.IsVerb(i);
    } else if (f == "vadj") {
      ok = c.IsVerb(i) || head.pos == "adj";
    } else if (f == "adj") {
      ok = head.pos == "adj" || head.pos == "na";
    } else if (f == "noun") {
      ok = c.IsNoun(i);
    } else if (f == "pot") {
      int cl = c.ClauseOfPred(i);
      ok = cl >= 0 && c.ir.clauses[static_cast<size_t>(cl)].voice == "potential";
    } else {
      ok = false;  // unknown form never matches (the loader lists the known ones)
    }
    if (!ok) return false;
  }
  return true;
}

bool LemmaAtom(const Ctx& c, const Atom& a, int i) {
  const Tok& k = c.T(i);
  bool hit = Has(a.lemmas, k.lemma);
  if (hit && !a.pos.empty()) {
    bool posOk = false;
    for (const auto& p : a.pos) posOk = posOk || p == k.pos || StartsWith(k.ud, p);
    hit = posOk;
  }
  for (const auto& cls : a.classes) hit = hit || c.InClass(i, cls);
  return hit;
}

// Index of the GroupClose atom of the group opened at ai.
size_t GroupEnd(const Row& r, size_t ai) {
  for (size_t k = ai + 1; k < r.atoms.size(); ++k)
    if (r.atoms[k].kind == Atom::Kind::GroupClose && r.atoms[k].group == r.atoms[ai].group) return k;
  return r.atoms.size() - 1;
}

void Rec(const Ctx& c, const Row& r, St st, std::vector<St>& out, int depth) {
  if (depth > 64 || out.size() > 64) return;
  if (st.ai >= r.atoms.size()) {
    out.push_back(std::move(st));
    return;
  }
  const Atom& a = r.atoms[st.ai];
  switch (a.kind) {
    case Atom::Kind::GroupOpen: {
      St skip = st;
      skip.ai = GroupEnd(r, st.ai) + 1;
      St in = st;
      in.ai += 1;
      Rec(c, r, std::move(in), out, depth + 1);
      Rec(c, r, std::move(skip), out, depth + 1);
      return;
    }
    case Atom::Kind::GroupClose:
      st.ai += 1;
      Rec(c, r, std::move(st), out, depth + 1);
      return;
    case Atom::Kind::Gap: {
      for (int k = 0; k <= kMaxGap && st.ti + k <= c.n; ++k) {
        if (k > 0 && c.InClass(st.ti + k - 1, "sentend")) break;
        St g = st;
        g.ai += 1;
        g.ti += k;
        Rec(c, r, std::move(g), out, depth + 1);
      }
      return;
    }
    case Atom::Kind::NotLemma: {
      if (st.ti < c.n && LemmaAtom(c, a, st.ti)) return;
      st.ai += 1;
      Rec(c, r, std::move(st), out, depth + 1);
      return;
    }
    case Atom::Kind::Lemma: {
      if (st.ti >= c.n || !LemmaAtom(c, a, st.ti)) return;
      st.lits.push_back(st.ti);
      st.ti += 1;
      st.ai += 1;
      st.score += kLiteral;
      Rec(c, r, std::move(st), out, depth + 1);
      return;
    }
    case Atom::Kind::FClass: {
      if (a.fclass == "end") {
        if (st.ti < c.n && !c.InClass(st.ti, "sentend")) return;
        st.ai += 1;
        Rec(c, r, std::move(st), out, depth + 1);
        return;
      }
      if (a.fclass == "fin") {
        int k = st.ti;
        while (k < c.n && c.InClass(k, "final")) ++k;
        if (k < c.n && !c.InClass(k, "sentend")) return;
        st.ai += 1;
        st.ti = k;
        Rec(c, r, std::move(st), out, depth + 1);
        return;
      }
      if (st.ti >= c.n || !c.InClass(st.ti, a.fclass)) return;
      if (a.fclass == "past" && c.Form(st.ti) == "katei") return;
      st.lits.push_back(st.ti);
      st.ti += 1;
      st.ai += 1;
      st.score += kLiteral;
      Rec(c, r, std::move(st), out, depth + 1);
      return;
    }
    case Atom::Kind::Slot: {
      int i = st.ti;
      if (i >= c.n) return;
      if (a.slot == 'N') {
        if (!c.IsNoun(i)) return;
        int j = i;
        std::vector<int> ends{i};
        while (j + 1 < c.n && (c.IsNoun(j + 1) || c.T(j + 1).pos == "suf") && !c.T(j + 1).spaceBefore) {
          ++j;
          ends.push_back(j);
        }
        for (auto e = ends.rbegin(); e != ends.rend(); ++e) {
          if (!a.cls.empty() && !StartsWith(c.T(*e).cls, a.cls)) continue;
          St g = st;
          g.nTok = *e;
          g.ti = *e + 1;
          g.ai += 1;
          g.score += a.cls.empty() ? 0 : kClass;
          Rec(c, r, std::move(g), out, depth + 1);
        }
        return;
      }
      if (!c.IsPredStart(i, a.slot)) return;
      if (a.slot == 'X' && c.IsNoun(i)) {
        // noun predicate: the noun (compound) and its copula auxiliaries
        int j = i;
        while (j + 1 < c.n && (c.IsNoun(j + 1) || c.T(j + 1).pos == "suf")) ++j;
        int nEnd = j;
        for (int e = nEnd; e < c.n && e <= nEnd + kMaxUnit; ++e) {
          if (e > nEnd && !(c.T(e).pos == "aux")) break;
          if (!UnitForms(c, a, i, e) && !(e == nEnd && a.forms.empty())) continue;
          St g = st;
          g.vTok = nEnd;
          g.vEnd = e;
          g.ti = e + 1;
          g.ai += 1;
          g.score += kForm * static_cast<int>(a.forms.size());
          Rec(c, r, std::move(g), out, depth + 1);
        }
        return;
      }
      int j = i;
      std::vector<int> ends{i};
      while (j + 1 < c.n && j + 1 <= i + kMaxUnit && c.UnitCont(j + 1)) {
        ++j;
        ends.push_back(j);
      }
      for (auto e = ends.rbegin(); e != ends.rend(); ++e) {
        if (!UnitForms(c, a, i, *e)) continue;
        St g = st;
        g.vTok = i;
        g.vEnd = *e;
        g.ti = *e + 1;
        g.ai += 1;
        g.score += kForm * static_cast<int>(a.forms.size());
        Rec(c, r, std::move(g), out, depth + 1);
      }
      return;
    }
  }
}

// ---- when atoms ---------------------------------------------------------------------------------

std::string Vol(const Ctx& c, int cl) {
  if (cl < 0) return "-";
  const Clause& k = c.ir.clauses[static_cast<size_t>(cl)];
  if (k.pred < 0) return "-";
  if (k.voice == "potential" || k.voice == "passive" || k.voice == "rare" || k.voice == "spontaneous") return "-";
  if (Has(k.modality, "volitional") || k.force == "imp" || Has(k.modality, "want")) return "+";
  const Tok& p = c.T(k.pred);
  bool verbalNoun = c.IsNoun(k.pred) && c.InClass(k.pred + 1, "suru");
  if (!c.IsVerb(k.pred) && !verbalNoun) return "-";
  auto it = c.t.pred.find(p.lemma);
  if (it != c.t.pred.end() && !it->second.vol.empty()) return it->second.vol;
  return "+";
}

std::string Asp(const Ctx& c, int cl) {
  if (cl < 0) return "";
  const Clause& k = c.ir.clauses[static_cast<size_t>(cl)];
  if (k.pred < 0) return "";
  const Tok& p = c.T(k.pred);
  bool verbalNoun = c.IsNoun(k.pred) && c.InClass(k.pred + 1, "suru");
  if (!c.IsVerb(k.pred) && !verbalNoun) return "state";
  auto it = c.t.pred.find(p.lemma);
  if (it != c.t.pred.end() && !it->second.asp.empty()) return it->second.asp;
  return "activity";
}

// Values of a clause path (one clause feature, a lexicon feature or an argument field).
std::vector<std::string> ClauseVals(const Ctx& c, int cl, const std::string& f) {
  if (cl < 0) return {};
  const Clause& k = c.ir.clauses[static_cast<size_t>(cl)];
  if (f == "tense") return {k.tense};
  if (f == "aspect") return {k.aspect};
  if (f == "pol") return {k.pol};
  if (f == "negFn") return {k.negFn};
  if (f == "voice") return {k.voice};
  if (f == "bene") return {k.bene};
  if (f == "link") return {k.link};
  if (f == "force") return {k.force};
  if (f == "polite") return {k.polite};
  if (f == "modality") return k.modality.empty() ? std::vector<std::string>{"-"} : k.modality;
  if (f == "vol") return {Vol(c, cl)};
  if (f == "asp") return {Asp(c, cl)};
  if (f == "pclass") {
    if (k.pred < 0) return {"-"};
    auto it = c.t.pred.find(c.T(k.pred).lemma);
    return {it == c.t.pred.end() || it->second.pclass.empty() ? std::string("-") : it->second.pclass};
  }
  if (f == "lemma") return k.pred >= 0 ? std::vector<std::string>{c.T(k.pred).lemma} : std::vector<std::string>{};
  if (f == "pos") return k.pred >= 0 ? std::vector<std::string>{c.T(k.pred).pos} : std::vector<std::string>{};
  if (f == "exists") return {"+"};
  if (!f.empty() && f[0] == '@') return {k.pred >= 0 && c.InClass(k.pred, f.substr(1)) ? "+" : "-"};
  return {};
}

std::vector<std::string> ArgVals(const Ctx& c, int cl, const std::string& role, const std::string& f) {
  std::vector<std::string> out;
  if (cl < 0) return out;
  for (const auto& a : c.ir.clauses[static_cast<size_t>(cl)].args) {
    if (a.role != role) continue;
    if (f.empty()) out.push_back("+");
    else if (f == "lemma") out.push_back(a.lemma);
    else if (f == "cls") {
      // a class value matches its sub-classes: @person.higher is also @person
      std::string v = a.cls;
      out.push_back(v.empty() ? "-" : v);
      size_t dot = v.rfind('.');
      while (dot != std::string::npos && dot > 0) {
        v = v.substr(0, dot);
        out.push_back(v);
        dot = v.rfind('.');
      }
    } else if (f == "how") out.push_back(a.how);
  }
  return out;
}

bool CondHolds(const Ctx& c, const St& m, const Cond& cond, std::string& fact, bool& vacuous) {
  vacuous = false;
  const std::string& path = cond.path;
  size_t dot = path.find('.');
  std::string head = path.substr(0, dot);
  std::string rest = dot == std::string::npos ? std::string() : path.substr(dot + 1);
  int last = m.lits.empty() ? (m.vEnd >= 0 ? m.vEnd : m.ti - 1) : m.lits.back();
  int vCl = m.vTok >= 0 ? c.ClauseOfPred(m.vTok) : c.ClauseOfTok(last);
  std::vector<std::string> vals;
  bool absentOk = false;
  if (head == "V") {
    vals = ClauseVals(c, vCl, rest);
  } else if (head == "host") {
    vals = ClauseVals(c, c.ClauseOfTok(last), rest);
  } else if (head == "main") {
    int mc = c.MainOf(m.ti - 1 >= 0 ? m.ti - 1 : last);
    if (rest == "exists") vals = {mc >= 0 ? "+" : "-"};
    else vals = ClauseVals(c, mc, rest);
    absentOk = mc < 0;
  } else if (head == "subj" || head == "obj" || head == "dat" || head == "topic" || head == "loc" || head == "from" ||
             head == "with" || head == "head" || head == "to" || head == "cmp") {
    vals = ArgVals(c, vCl, head, rest);
    if (vals.empty()) vals = {"-"};
  } else if (head == "next" || head == "prev") {
    int i = head == "next" ? m.ti : m.begin - 1;
    if (i < 0 || i >= c.n) vals = {"-"};
    else if (rest == "lemma") vals = {c.T(i).lemma};
    else if (rest == "pos") vals = {c.T(i).pos};
    else if (rest == "form") vals = {c.Form(i)};
    else if (rest == "cls") vals = {c.T(i).cls.empty() ? "-" : c.T(i).cls};
    else if (!rest.empty() && rest[0] == '@') vals = {c.InClass(i, rest.substr(1)) ? "+" : "-"};
  } else if (head == "inq") {
    // inside a quote: an opening bracket before the span that no closing bracket matched (stand-in for S's utterance)
    int depth = 0;
    for (int k = 0; k < m.begin && k < c.n; ++k) {
      if (c.InClass(k, "qopen")) ++depth;
      else if (c.InClass(k, "qclose") && depth > 0) --depth;
    }
    vals = {depth > 0 ? "+" : "-"};
  } else if (head == "clause") {
    // a lemma anywhere in the clause of V (adverbs and cue words: 毎日, 何, どうして)
    int cl = vCl;
    bool any = false;
    if (cl >= 0) {
      const Clause& k = c.ir.clauses[static_cast<size_t>(cl)];
      int b0 = k.firstBun, b1 = k.lastBun;
      for (int t = 0; t < c.n; ++t) {
        int b = c.T(t).bun;
        if (b < b0 || b > b1) continue;
        if (!rest.empty() && rest[0] == '@') any = any || c.InClass(t, rest.substr(1));
      }
    }
    vals = {any ? "+" : "-"};
  } else if (head == "sent") {
    // a word of a class anywhere in the sentence of the span (politeness of the utterance: です, ます)
    int b = m.begin, e = m.ti;
    while (b > 0 && !c.InClass(b - 1, "sentend")) --b;
    while (e < c.n && !c.InClass(e, "sentend")) ++e;
    bool any = false;
    for (int k = b; k < e && !rest.empty() && rest[0] == '@'; ++k) any = any || c.InClass(k, rest.substr(1));
    vals = {any ? "+" : "-"};
  } else if (head == "final") {
    int k = m.ti;
    while (k < c.n && c.InClass(k, "final")) ++k;
    vals = {(k >= c.n || c.InClass(k, "sentend")) ? "+" : "-"};
  } else if (head == "N") {
    if (rest == "cls") vals = {m.nTok >= 0 && !c.T(m.nTok).cls.empty() ? c.T(m.nTok).cls : "-"};
    else if (rest == "lemma") vals = {m.nTok >= 0 ? c.T(m.nTok).lemma : "-"};
    if (!vals.empty() && vals[0].size() > 1) {
      std::string v = vals[0];
      size_t d = v.rfind('.');
      while (d != std::string::npos && d > 0) {
        v = v.substr(0, d);
        vals.push_back(v);
        d = v.rfind('.');
      }
    }
  } else if (head == "Vtok") {
    if (m.vTok >= 0 && rest == "vol") {
      // the verb's own volition (lexicon), whatever voice its clause has
      auto it = c.t.pred.find(c.T(m.vTok).lemma);
      bool verbal = c.IsVerb(m.vTok) || (c.IsNoun(m.vTok) && c.InClass(m.vTok + 1, "suru"));
      vals = {!verbal ? "-" : (it != c.t.pred.end() && !it->second.vol.empty() ? it->second.vol : "+")};
    } else if (m.vTok >= 0 && rest == "lemma") vals = {c.T(m.vTok).lemma};
    else if (m.vTok >= 0 && rest == "pos") vals = {c.T(m.vTok).pos};
    else if (m.vTok >= 0 && !rest.empty() && rest[0] == '@') {
      bool any = false;
      for (int k = m.vTok; k <= m.vEnd; ++k) any = any || c.InClass(k, rest.substr(1));
      vals = {any ? "+" : "-"};
    }
  }
  bool hit = false;
  for (const auto& want : cond.values)
    for (const auto& v : vals) hit = hit || v == want;
  if (vals.empty() && absentOk) {
    hit = Has(cond.values, "-");
    vacuous = true;
  }
  bool ok = cond.negate ? !hit : hit;
  if (ok) {
    fact = path + (cond.negate ? "!=" : "=");
    fact += vals.empty() ? std::string("-") : vals[0];
  }
  return ok;
}

}  // namespace

Result Transfer(const Table& t, const LineIR& ir) {
  Ctx c{t, ir, static_cast<int>(ir.toks.size())};
  Result res;
  struct Cand {
    Match m;
    bool when = false;
  };
  std::vector<Cand> cands;
  std::vector<char> starts(static_cast<size_t>(c.n), 0);
  for (size_t ri = 0; ri < t.rows.size(); ++ri) {
    const Row& r = t.rows[ri];
    // candidate start positions: up to kMaxReach tokens before an occurrence of the row's anchor atom
    std::fill(starts.begin(), starts.end(), static_cast<char>(r.req < 0 ? 1 : 0));
    if (r.req >= 0) {
      const Atom& a = r.atoms[static_cast<size_t>(r.req)];
      bool any = false;
      for (int p = 0; p < c.n; ++p) {
        bool hit = a.kind == Atom::Kind::Lemma ? LemmaAtom(c, a, p) : c.InClass(p, a.fclass);
        if (!hit) continue;
        any = true;
        for (int s = p - kMaxReach < 0 ? 0 : p - kMaxReach; s <= p; ++s) starts[static_cast<size_t>(s)] = 1;
      }
      if (!any) continue;
    }
    for (int s = 0; s < c.n; ++s) {
      if (!starts[static_cast<size_t>(s)]) continue;
      std::vector<St> sols;
      St st;
      st.ti = s;
      st.begin = s;
      Rec(c, r, st, sols, 0);
      if (sols.empty()) continue;
      // best completion: most score, then longest
      auto best = std::max_element(sols.begin(), sols.end(), [](const St& a, const St& b) {
        return a.score != b.score ? a.score < b.score : a.ti < b.ti;
      });
      if (best->lits.empty() && best->vTok < 0 && best->nTok < 0) continue;
      Cand cd;
      cd.m.row = ri;
      cd.m.begin = s;
      cd.m.end = best->ti;
      cd.m.lits = best->lits;
      cd.m.vTok = best->vTok;
      cd.m.vEnd = best->vEnd;
      cd.m.nTok = best->nTok;
      cd.m.score = best->score;
      cd.when = true;
      for (const auto& cond : r.conds) {
        std::string fact;
        bool vacuous = false;
        if (!CondHolds(c, *best, cond, fact, vacuous)) {
          cd.when = false;
          break;
        }
        cd.m.because.push_back(fact);
        if (!vacuous) cd.m.whenSat += 1;
      }
      cd.m.score += kWhen * cd.m.whenSat;
      cands.push_back(std::move(cd));
    }
  }
  // resolution: most specific first, then prio, then file order; winners may not share literal tokens
  std::vector<size_t> order;
  for (size_t i = 0; i < cands.size(); ++i)
    if (cands[i].when) order.push_back(i);
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const Match& x = cands[a].m;
    const Match& y = cands[b].m;
    if (x.score != y.score) return x.score > y.score;
    const Row& rx = t.rows[x.row];
    const Row& ry = t.rows[y.row];
    if (rx.prio != ry.prio) return rx.prio > ry.prio;
    if (rx.order != ry.order) return rx.order < ry.order;
    return x.begin < y.begin;
  });
  std::vector<char> used(static_cast<size_t>(c.n), 0);
  std::vector<size_t> won;
  for (size_t oi : order) {
    const Match& m = cands[oi].m;
    bool clash = false;
    for (int k : m.lits) clash = clash || used[static_cast<size_t>(k)] != 0;
    // a slot-only row (no literal) may not stack on the same predicate with another row of its facet
    if (m.lits.empty())
      for (size_t w : won) {
        const Match& o = cands[w].m;
        if (t.rows[o.row].facet == t.rows[m.row].facet && o.vTok == m.vTok && m.vTok >= 0) clash = true;
      }
    if (clash) continue;
    for (int k : m.lits) used[static_cast<size_t>(k)] = 1;
    won.push_back(oi);
  }
  std::sort(won.begin(), won.end(), [&](size_t a, size_t b) { return cands[a].m.begin < cands[b].m.begin; });
  for (size_t wi : won) {
    const Match& m = cands[wi].m;
    const Row& r = t.rows[m.row];
    std::string family = r.id.substr(0, r.id.rfind('.'));
    Decision d;
    d.rule = "cx:" + r.id;
    d.score = m.score;
    d.because = m.because;
    int first = m.begin, last = m.end - 1;
    if (first >= 0 && first < c.n && last >= first) {
      d.src.begin = c.T(first).src.begin;
      d.src.end = c.T(last).src.end;
    }
    // alternatives: rows of the same family whose pattern matched over the same tokens
    int bestOther = -1;
    std::string other;
    int alts = 1;
    for (size_t k = 0; k < cands.size(); ++k) {
      if (k == wi) continue;
      const Match& o = cands[k].m;
      const Row& orow = t.rows[o.row];
      if (orow.id.substr(0, orow.id.rfind('.')) != family) continue;
      bool overlap = false;
      for (int x : o.lits) overlap = overlap || std::find(m.lits.begin(), m.lits.end(), x) != m.lits.end();
      if (!overlap && !(o.vTok == m.vTok && m.vTok >= 0)) continue;
      ++alts;
      if (cands[k].when && o.score > bestOther) {
        bestOther = o.score;
        other = "cx:" + orow.id;
      }
    }
    d.nAlt = alts;
    d.runnerUp = other;
    d.margin = bestOther < 0 ? 0 : m.score - bestOther;
    d.kind = !r.conds.empty() ? "cued" : (alts > 1 ? "default" : "cued");
    res.chosen.push_back(m);
    res.decisions.push_back(std::move(d));
  }
  for (const auto& cd : cands)
    if (cd.when) res.candidates.push_back(cd.m);
  return res;
}

namespace {

void Feats(std::string& out, const std::string& body) {
  // "ctype=nom:gi,rel=purpose" or "dep" flags → JSON members
  std::string cur;
  auto flush = [&](const std::string& kv) {
    if (kv.empty()) return;
    size_t eq = kv.find('=');
    out += ',';
    if (eq == std::string::npos) {
      AppendJsonString(out, kv);
      out += ":1";
    } else {
      AppendJsonString(out, kv.substr(0, eq));
      out += ':';
      AppendJsonString(out, kv.substr(eq + 1));
    }
  };
  for (char ch : body) {
    if (ch == ',' || ch == ' ') {
      flush(cur);
      cur.clear();
    } else {
      cur += ch;
    }
  }
  flush(cur);
}

// Node templates of the ko column: {V feats}, {N feats}, KIND:lemma[flags]^, P:case
std::string Nodes(const Ctx& c, const Match& m, const std::string& ko) {
  std::string out = "[";
  bool first = true;
  size_t i = 0;
  while (i < ko.size()) {
    while (i < ko.size() && ko[i] == ' ') ++i;
    if (i >= ko.size()) break;
    if (!first) out += ',';
    first = false;
    if (ko[i] == '{') {
      size_t e = ko.find('}', i);
      if (e == std::string::npos) e = ko.size();
      std::string in = ko.substr(i + 1, e - i - 1);
      std::string slot = in.substr(0, in.find(' '));
      int tok = slot == "N" ? m.nTok : m.vTok;
      out += "{\"slot\":";
      AppendJsonString(out, slot);
      if (tok >= 0) {
        out += ",\"ja\":";
        AppendJsonString(out, c.T(tok).lemma);
      }
      if (in.size() > slot.size()) Feats(out, in.substr(slot.size() + 1));
      out += '}';
      i = e + 1;
      continue;
    }
    size_t e = i;
    while (e < ko.size() && ko[e] != ' ') ++e;
    std::string w = ko.substr(i, e - i);
    i = e;
    bool inherit = !w.empty() && w.back() == '^';
    if (inherit) w.pop_back();
    std::string flags;
    size_t lb = w.find('[');
    if (lb != std::string::npos) {
      flags = w.substr(lb + 1, w.size() - lb - 2);
      w = w.substr(0, lb);
    }
    size_t colon = w.find(':');
    std::string kind = w.substr(0, colon);
    std::string val = colon == std::string::npos ? std::string() : w.substr(colon + 1);
    out += "{\"kind\":";
    AppendJsonString(out, kind);
    if (kind == "P") {
      out += ",\"kase\":";
      AppendJsonString(out, val);
    } else if (!val.empty()) {
      out += ",\"lemma\":";
      AppendJsonString(out, val);
    }
    if (!flags.empty()) Feats(out, flags);
    if (inherit) out += ",\"inherit\":1";
    out += '}';
  }
  out += ']';
  return out;
}

}  // namespace

std::string ResultJson(const Table& t, const LineIR& ir, const Result& r, bool withCandidates) {
  Ctx c{t, ir, static_cast<int>(ir.toks.size())};
  std::string out = "{\"key\":";
  AppendJsonString(out, ir.key);
  out += ",\"src\":";
  AppendJsonString(out, ir.src);
  out += ",\"cx\":[";
  for (size_t i = 0; i < r.chosen.size(); ++i) {
    const Match& m = r.chosen[i];
    const Row& row = t.rows[m.row];
    const Decision& d = r.decisions[i];
    if (i) out += ',';
    out += "{\"id\":";
    AppendJsonString(out, row.id);
    out += ",\"src\":[" + std::to_string(d.src.begin) + "," + std::to_string(d.src.end) + "]";
    out += ",\"toks\":[" + std::to_string(m.begin) + "," + std::to_string(m.end) + "]";
    std::string ja;
    for (int k = m.begin; k < m.end; ++k) ja += c.T(k).surf;
    out += ",\"ja\":";
    AppendJsonString(out, ja);
    out += ",\"ev\":";
    AppendJsonString(out, d.kind);
    out += ",\"score\":" + std::to_string(d.score) + ",\"margin\":" + std::to_string(d.margin) +
           ",\"nAlt\":" + std::to_string(d.nAlt);
    if (!d.runnerUp.empty()) {
      out += ",\"runnerUp\":";
      AppendJsonString(out, d.runnerUp);
    }
    out += ",\"because\":[";
    for (size_t k = 0; k < d.because.size(); ++k) {
      if (k) out += ',';
      AppendJsonString(out, d.because[k]);
    }
    out += "],\"set\":";
    AppendJsonString(out, row.set);
    out += ",\"pol\":";
    AppendJsonString(out, row.pol);
    out += ",\"spec\":";
    AppendJsonString(out, row.spec);
    out += ",\"kir\":" + Nodes(c, m, row.ko) + "}";
  }
  out += "]";
  if (withCandidates) {
    out += ",\"candidates\":[";
    for (size_t i = 0; i < r.candidates.size(); ++i) {
      if (i) out += ',';
      out += "{\"id\":";
      AppendJsonString(out, t.rows[r.candidates[i].row].id);
      out += ",\"toks\":[" + std::to_string(r.candidates[i].begin) + "," + std::to_string(r.candidates[i].end) +
             "],\"score\":" + std::to_string(r.candidates[i].score) + "}";
    }
    out += "]";
  }
  out += "}";
  return out;
}

}  // namespace hd08::xfer
