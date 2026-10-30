// Learned deciders: decision points, named features, the weight tables and the gated decision.
#include "decide.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace hd08::xfer {

namespace {

struct V {
  const Table& t;
  const LineIR& v;
  int n;
  const Tok& T(int i) const { return v.toks[static_cast<size_t>(i)]; }
  bool In(int i, const char* cls) const {
    if (i < 0 || i >= n) return false;
    auto it = t.fclass.find(cls);
    if (it == t.fclass.end()) return false;
    const Tok& k = T(i);
    for (const auto& f : it->second) {
      if (!f.pos.empty() && f.pos != "*" && f.pos != k.pos) continue;
      if (!f.udPrefix.empty() && k.ud.compare(0, f.udPrefix.size(), f.udPrefix) != 0) continue;
      if (!f.cform.empty() && k.cform.compare(0, f.cform.size(), f.cform) != 0) continue;
      if (!f.ctype.empty() && k.ctype.compare(0, f.ctype.size(), f.ctype) != 0) continue;
      if (std::find(f.lemmas.begin(), f.lemmas.end(), "*") != f.lemmas.end() ||
          std::find(f.lemmas.begin(), f.lemmas.end(), k.lemma) != f.lemmas.end())
        return true;
    }
    return false;
  }
  std::string Form(int i) const {
    const std::string& cf = T(i).cform;
    for (const auto& [prefix, form] : t.forms)
      if (!prefix.empty() && cf.compare(0, prefix.size(), prefix) == 0) return form;
    return "-";
  }
  bool Verb(int i) const {
    const std::string& p = T(i).pos;
    return p == "v1" || p == "v5" || p == "vs" || p == "vk" || p == "vz";
  }
  int ClauseOf(int i) const {
    if (i < 0 || i >= n || T(i).bun < 0) return -1;
    return v.buns[static_cast<size_t>(T(i).bun)].clause;
  }
  // the clause a span ending at token i attaches to
  int MainOf(int i) const {
    if (i < 0 || i >= n || T(i).bun < 0) return -1;
    int b = T(i).bun;
    const Bun& bun = v.buns[static_cast<size_t>(b)];
    int c = bun.clause;
    if (bun.type == 'P' && c >= 0 && v.clauses[static_cast<size_t>(c)].lastBun == b)
      return v.clauses[static_cast<size_t>(c)].parent;
    int h = bun.head, guard = 0;
    while (h >= 0 && static_cast<size_t>(h) < v.buns.size() && v.buns[static_cast<size_t>(h)].type != 'P' && guard++ < 64)
      h = v.buns[static_cast<size_t>(h)].head;
    if (h < 0 || static_cast<size_t>(h) >= v.buns.size()) return -1;
    return v.buns[static_cast<size_t>(h)].clause;
  }
  std::string Vol(int i) const {
    auto it = t.pred.find(T(i).lemma);
    return it != t.pred.end() && !it->second.vol.empty() ? it->second.vol : (Verb(i) ? "+" : "-");
  }
};

void AddClass(std::vector<std::string>& f, const std::string& prefix, const std::string& cls) {
  if (cls.empty()) return;
  std::string c = cls;
  f.push_back(prefix + c);
  size_t dot = c.rfind('.');
  while (dot != std::string::npos && dot > 0) {
    c = c.substr(0, dot);
    f.push_back(prefix + c);
    dot = c.rfind('.');
  }
}

void ArgFeatures(const V& x, int cl, std::vector<std::string>& f) {
  static const char* kRoles[] = {"subj", "obj", "dat", "loc", "topic", "from", "with"};
  std::set<std::string> seen;
  if (cl >= 0) {
    for (const auto& a : x.v.clauses[static_cast<size_t>(cl)].args) {
      seen.insert(a.role);
      f.push_back(a.role + "=" + a.lemma);
      AddClass(f, a.role, a.cls);
    }
  }
  for (const char* r : kRoles)
    if (!seen.count(r)) f.push_back(std::string("no_") + r);
}

void MainFeatures(const V& x, int mc, std::vector<std::string>& f) {
  if (mc < 0) {
    f.push_back("main_none");
    return;
  }
  const Clause& c = x.v.clauses[static_cast<size_t>(mc)];
  if (c.pred >= 0 && c.pred < x.n) {
    f.push_back("main_lem=" + x.T(c.pred).lemma);
    f.push_back("main_pos=" + x.T(c.pred).pos);
    f.push_back("main_vol=" + x.Vol(c.pred));
  }
  if (c.tense == "past") f.push_back("main_past");
  if (c.pol == "neg") f.push_back("main_neg");
  if (c.force != "decl") f.push_back("main_force=" + c.force);
  if (c.aspect != "none") f.push_back("main_asp=" + c.aspect);
  if (c.voice != "active") f.push_back("main_voice=" + c.voice);
  for (const auto& m : c.modality) f.push_back("main_mod=" + m);
  if (c.link == "final") f.push_back("main_final");
}

void Window(const V& x, int i, int from, int to, std::vector<std::string>& f) {
  for (int j = from; j <= to; ++j) {
    if (j == 0) continue;
    int k = i + j;
    std::string p = (j < 0 ? "p" : "n") + std::to_string(j < 0 ? -j : j);
    if (k < 0 || k >= x.n) {
      f.push_back(p + "=#");
      continue;
    }
    f.push_back(p + "=" + x.T(k).lemma);
    f.push_back(p + "pos=" + x.T(k).pos);
  }
}

bool InQuote(const V& x, int i) {
  int depth = 0;
  for (int k = 0; k < i && k < x.n; ++k) {
    if (x.In(k, "qopen")) ++depth;
    else if (x.In(k, "qclose") && depth > 0) --depth;
  }
  return depth > 0;
}

bool SentenceHas(const V& x, int i, const char* cls) {
  int b = i, e = i;
  while (b > 0 && !x.In(b - 1, "sentend")) --b;
  while (e < x.n && !x.In(e, "sentend")) ++e;
  for (int k = b; k < e; ++k)
    if (x.In(k, cls)) return true;
  return false;
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  std::istringstream in(s);
  while (std::getline(in, cur, sep)) out.push_back(cur);
  return out;
}

}  // namespace

std::vector<DecisionPoint> FindPoints(const Table& t, const LineIR& v) {
  V x{t, v, static_cast<int>(v.toks.size())};
  std::vector<DecisionPoint> out;
  for (int i = 0; i < x.n; ++i) {
    if (x.In(i, "pass") && i > 0 && x.Verb(i - 1)) out.push_back({i, "voice"});
    else if (x.In(i, "dp.tame")) out.push_back({i, "tame"});
    else if (x.In(i, "dp.youni") && x.In(i + 1, "ni.cop")) out.push_back({i, "youni"});
    else if (x.In(i, "dp.tokoro") && x.In(i + 1, "dp.de") && !(i + 2 < x.n && x.Verb(i + 2)))
      out.push_back({i, "tokorode"});
  }
  return out;
}

std::vector<std::string> Features(const Table& t, const LineIR& v, const DecisionPoint& p) {
  V x{t, v, static_cast<int>(v.toks.size())};
  std::vector<std::string> f{"bias"};
  int i = p.tok;
  if (i < 0 || i >= x.n) return f;
  if (InQuote(x, i)) f.push_back("inq");
  if (SentenceHas(x, i, "polite")) f.push_back("polite");
  if (p.family == "voice") {
    int vb = i - 1;
    f.push_back("aux=" + x.T(i).lemma);
    f.push_back("vlem=" + x.T(vb).lemma);
    f.push_back("vpos=" + x.T(vb).pos);
    if (x.In(vb, "suru") && vb > 0) f.push_back("vnoun=" + x.T(vb - 1).lemma);
    if ((vb > 0 && x.In(vb - 1, "hon.prefix")) || (vb > 1 && x.In(vb - 2, "hon.prefix"))) f.push_back("o_prefix");
    auto it = t.pred.find(x.T(vb).lemma);
    if (it != t.pred.end() && !it->second.pclass.empty()) f.push_back("vclass=" + it->second.pclass);
    Window(x, i, 1, 2, f);
    if (x.In(i + 1, "neg") || x.In(i + 2, "neg")) f.push_back("neg");
    if (x.In(i + 1, "q") || x.In(i + 2, "q") || x.In(i + 3, "q")) f.push_back("q");
    int cl = x.ClauseOf(vb);
    ArgFeatures(x, cl, f);
    if (cl >= 0) {
      const Clause& c = v.clauses[static_cast<size_t>(cl)];
      if (c.link == "adn") f.push_back("adn");
      for (const auto& a : c.args)
        if (a.role == "head") {
          f.push_back("head=" + a.lemma);
          AddClass(f, "head", a.cls);
        }
      for (int k = 0; k < x.n; ++k)
        if (x.T(k).pos == "adv" && x.ClauseOf(k) == cl) f.push_back("mod=" + x.T(k).lemma);
    }
    return f;
  }
  // connectives: the construction ends at i (ため), i+1 (ように, ところで) or the に after ため
  int last = i;
  if (p.family == "youni" || p.family == "tokorode") last = i + 1;
  if (p.family == "tame" && x.In(i + 1, "dp.ni")) last = i + 1;
  Window(x, i, -2, 3, f);
  if (i > 0) {
    f.push_back("p1form=" + x.Form(i - 1));
    if (x.Verb(i - 1)) f.push_back("p1vol=" + x.Vol(i - 1));
    if (x.In(i - 1, "past")) f.push_back("p1past");
    if (x.In(i - 1, "neg")) f.push_back("p1neg");
    if (x.In(i - 1, "pass")) f.push_back("p1pass");
  }
  if (i == 0 || x.T(i - 1).pos == "sym") f.push_back("initial");
  if (last + 1 < x.n && x.T(last + 1).pos == "sym") f.push_back("then_sym");
  MainFeatures(x, x.MainOf(last), f);
  int own = i > 0 ? x.ClauseOf(i - 1) : -1;
  if (own >= 0) {
    const Clause& c = v.clauses[static_cast<size_t>(own)];
    if (c.voice != "active") f.push_back("own_voice=" + c.voice);
    if (c.pred >= 0) f.push_back("own_vol=" + x.Vol(c.pred));
  }
  return f;
}

bool LoadDeciders(const std::string& dirName, DecideData& d) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path dir = fs::path(dirName) / "decide";
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    if (e.path().extension() != ".tsv") continue;
    std::ifstream in(e.path(), std::ios::binary);
    Decider dec;
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) continue;
      if (line.rfind("#!", 0) == 0) {
        std::istringstream s(line.substr(2));
        std::string kv;
        while (s >> kv) {
          size_t eq = kv.find('=');
          if (eq == std::string::npos) continue;
          std::string k = kv.substr(0, eq), val = kv.substr(eq + 1);
          if (k == "family") dec.family = val;
          else if (k == "tau") dec.tau = std::strtof(val.c_str(), nullptr);
          else if (k == "id") dec.id = val;
        }
        continue;
      }
      if (line[0] == '#') continue;
      auto c = Split(line, '\t');
      if (c[0] == "classes") {
        dec.classes.assign(c.begin() + 1, c.end());
        dec.bias.assign(dec.classes.size(), 0.0f);
        continue;
      }
      if (c.size() < 3) continue;
      auto ci = std::find(dec.classes.begin(), dec.classes.end(), c[1]);
      if (ci == dec.classes.end()) {
        d.errors.push_back(e.path().filename().string() + ": unknown class " + c[1]);
        continue;
      }
      size_t k = static_cast<size_t>(ci - dec.classes.begin());
      float wv = std::strtof(c[2].c_str(), nullptr);
      if (c[0] == "bias") dec.bias[k] = wv;
      else {
        auto& row = dec.w[c[0]];
        if (row.empty()) row.assign(dec.classes.size(), 0.0f);
        row[k] = wv;
      }
    }
    if (dec.family.empty() || dec.classes.empty()) {
      d.errors.push_back(e.path().filename().string() + ": no family or classes");
      continue;
    }
    if (dec.id.empty()) dec.id = dec.family;
    d.byFamily[dec.family] = std::move(dec);
  }
  return !ec;
}

DeciderResult Decide(const Decider& d, const std::vector<std::string>& features, const std::string& base075) {
  DeciderResult r;
  size_t k = d.classes.size();
  std::vector<float> z(d.bias);
  std::vector<std::string> all(features);
  all.push_back("p075=" + (base075.empty() ? std::string("NONE") : base075));
  for (const auto& f : all) {
    auto it = d.w.find(f);
    if (it == d.w.end()) continue;
    for (size_t c = 0; c < k; ++c) z[c] += it->second[c];
  }
  float mx = *std::max_element(z.begin(), z.end());
  float sum = 0;
  std::vector<float> p(k);
  for (size_t c = 0; c < k; ++c) {
    p[c] = std::exp(z[c] - mx);
    sum += p[c];
  }
  size_t top = 0;
  for (size_t c = 0; c < k; ++c) {
    p[c] /= sum;
    if (p[c] > p[top]) top = c;
  }
  r.top = d.classes[top];
  r.pTop = p[top];
  r.base = base075.empty() ? "NONE" : base075;
  auto bi = std::find(d.classes.begin(), d.classes.end(), r.base);
  r.pBase = bi == d.classes.end() ? 0.0f : p[static_cast<size_t>(bi - d.classes.begin())];
  r.margin = r.pTop - r.pBase;
  r.overridden = r.top != r.base && r.margin > d.tau;
  r.chosen = r.overridden ? r.top : r.base;
  size_t ci = static_cast<size_t>(std::find(d.classes.begin(), d.classes.end(), r.chosen) - d.classes.begin());
  if (ci < k) {
    for (const auto& f : all) {
      auto it = d.w.find(f);
      if (it != d.w.end() && it->second[ci] != 0.0f) r.because.push_back({f, it->second[ci]});
    }
    std::sort(r.because.begin(), r.because.end(),
              [](const Contribution& a, const Contribution& b) { return std::fabs(a.weight) > std::fabs(b.weight); });
  }
  return r;
}

}  // namespace hd08::xfer
