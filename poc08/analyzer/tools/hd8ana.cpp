// hd8ana: build, run and evaluate the 0.8 analyzer.
//   hd8ana pack DATA_DIR OUT.bin
//   hd8ana pack-long CORE.bin LONG.tsv OUT.bin
//   hd8ana run CORE.bin [-l LONG.bin] [--units] [--format json|wakati|tsv]   (stdin → stdout; --units keeps long units whole)
//   hd8ana eval CORE.bin [-l LONG.bin] [--units] [--no-derive] [--diff F] [--explain F] GOLD...  (tag.py format)
//   hd8ana bench CORE.bin FILE [-l LONG.bin] [--no-margins]
//   hd8ana longunits CORE.bin KEYS.tsv OUT.tsv
//   hd8ana cases CORE.bin|DATA_DIR CASES.tsv   (analyzer fix and keep cases)
//   hd8ana norm < in > out          (layout-normalized lines, the teacher's input for fair gold)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hd8/analyzer.h"
#include "lattice.h"
#include "model.h"
#include "normalize.h"
#include "tsv.h"
#include "utf8.h"

using namespace hd8::ana;
namespace fs = std::filesystem;

namespace {

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

bool WriteFile(const std::string& path, const std::string& data) {
  std::ofstream o(path, std::ios::binary);
  o << data;
  return static_cast<bool>(o);
}

int Die(const std::string& msg) {
  std::fprintf(stderr, "hd8ana: %s\n", msg.c_str());
  return 1;
}

struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::string> opt;
  bool Has(const std::string& k) const { return opt.count(k) != 0; }
  std::string Get(const std::string& k, const std::string& d = "") const {
    auto it = opt.find(k);
    return it == opt.end() ? d : it->second;
  }
};

Args Parse(int argc, char** argv, std::set<std::string> valued) {
  Args a;
  for (int i = 2; i < argc; ++i) {
    std::string s = argv[i];
    if (s.size() > 1 && s[0] == '-') {
      if (valued.count(s) && i + 1 < argc) a.opt[s] = argv[++i];
      else a.opt[s] = "1";
    } else {
      a.pos.push_back(s);
    }
  }
  return a;
}

bool LoadAnalyzer(Analyzer* an, const std::string& core, const std::string& longPath, double* ms) {
  std::string bytes, err;
  if (!ReadFile(core, &bytes)) return Die("cannot read " + core), false;
  auto t0 = std::chrono::steady_clock::now();
  if (!an->Load(bytes, &err)) return Die(err), false;
  if (!longPath.empty()) {
    std::string lb;
    if (!ReadFile(longPath, &lb)) return Die("cannot read " + longPath), false;
    if (!an->LoadOverlay(lb, &err)) return Die(err), false;
  }
  if (ms) *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return true;
}

// ---- gold (tag.py format) -------------------------------------------------------------------------
struct GoldTok {
  uint32_t b, e;  // src bytes
  std::string surf, ud, ctype, cform, lemma, kana;
  bool unk;
};
struct GoldLine {
  std::string src;
  std::vector<GoldTok> toks;
};

std::string UdName(const std::vector<std::string>& f) {
  std::string s;
  for (int i = 2; i <= 5; ++i) {
    if (f[static_cast<size_t>(i)] == "*") continue;
    if (!s.empty()) s += '-';
    s += f[static_cast<size_t>(i)];
  }
  return s;
}

bool ReadGold(const std::string& path, std::vector<GoldLine>* out, size_t limit = SIZE_MAX) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string line;
  while (out->size() < limit && std::getline(in, line)) {
    GoldLine g;
    for (auto& t : SplitOn(line, '\x1f')) {
      auto f = SplitOn(t, '\x1e');
      if (f.size() < 12) continue;
      g.src += f[0];
      auto b = static_cast<uint32_t>(g.src.size());
      g.src += f[1];
      if (f[2] == "空白") continue;
      g.toks.push_back({b, static_cast<uint32_t>(g.src.size()), f[1], UdName(f), f[6] == "*" ? "" : f[6],
                        f[7] == "*" ? "" : f[7], f[9], f[10] == "*" ? "" : f[10], !f[11].empty()});
    }
    out->push_back(std::move(g));
  }
  return true;
}

// ---- pack ------------------------------------------------------------------------------------------
int CmdPack(const Args& a) {
  if (a.pos.size() < 2) return Die("pack DATA_DIR OUT.bin");
  Model m;
  std::string err;
  if (!LoadTsvDir(a.pos[0], &m, &err)) return Die(err);
  Finalize(&m);
  std::string bin = Serialize(m);
  if (!WriteFile(a.pos[1], bin)) return Die("cannot write " + a.pos[1]);
  std::fprintf(stderr, "classes %zu groups %zu paradigms %zu connection rules %zu rules %zu constraints %zu: %zu bytes\n",
               m.classes.size(), m.groups.size(), m.paradigms.size(), m.connRules.size(), m.rules.size(),
               m.constraints.size(), bin.size());
  return 0;
}

int CmdPackLong(const Args& a) {
  if (a.pos.size() < 3) return Die("pack-long CORE.bin LONG.tsv OUT.bin");
  std::string bytes, err;
  if (!ReadFile(a.pos[0], &bytes)) return Die("cannot read core");
  Model m;
  if (!Deserialize(bytes, &m, &err)) return Die(err);
  if (!LoadLongTsv(a.pos[1], &m, &err)) return Die(err);
  std::string bin = SerializeLongs(m);
  if (!WriteFile(a.pos[2], bin)) return Die("cannot write " + a.pos[2]);
  std::fprintf(stderr, "long units %zu: %zu bytes\n", m.longs.size(), bin.size());
  return 0;
}

Options OptsFrom(const Args& a) {
  Options o;
  o.splitLongUnits = !a.Has("--units");
  o.margins = !a.Has("--no-margins");
  o.longUnits = !a.Has("--no-long");
  return o;
}

// ---- run -------------------------------------------------------------------------------------------
int CmdRun(const Args& a) {
  if (a.pos.empty()) return Die("run CORE.bin");
  Analyzer an;
  if (!LoadAnalyzer(&an, a.pos[0], a.Get("-l"), nullptr)) return 1;
  Options opt = OptsFrom(a);
  if (a.Has("--no-derive")) {
    an.mutableModel()->rules.clear();
    an.mutableModel()->stutterRule = -1;
  }
  std::string fmt = a.Get("--format", "json");
  auto ws = NewWorkspace();
  std::string line;
  Analysis r;
  int n = 0;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    an.Analyze(line, opt, &r, ws.get());
    ++n;
    if (fmt == "json") {
      std::cout << ToJson(an, line, r, a.Get("--doc"), n) << '\n';
    } else if (fmt == "wakati") {
      for (size_t i = 0; i < r.toks.size(); ++i) std::cout << (i ? " " : "") << r.toks[i].surf;
      std::cout << '\n';
    } else {
      for (auto& t : r.toks)
        std::cout << t.surf << '\t' << t.lemma << '\t' << t.reading << '\t' << Analyzer::PosName(t.pos) << '\t'
                  << an.UdName(t.ud) << '\t' << an.CtypeName(t.ctype) << '\t' << an.CformName(t.cform) << '\t'
                  << t.margin << (t.derive.empty() ? "" : "\t") << t.derive << '\n';
      std::cout << "EOS\n";
    }
  }
  return 0;
}

// ---- eval ------------------------------------------------------------------------------------------
// Keys of the translation dictionaries (one per line): boundaries strictly inside a span that one key covers,
// with the key's ends on reference boundaries, do not count in the key-boundary score.
struct KeySet {
  std::unordered_set<std::string> keys;
  size_t maxChars = 0;
};

struct Score {
  size_t gold = 0, pred = 0, bound = 0, lemma = 0, full = 0, read = 0, lines = 0, exact = 0;
  size_t kGold = 0, kPred = 0, kHit = 0;  // internal boundaries outside key spans
  void Add(const Score& o) {
    gold += o.gold; pred += o.pred; bound += o.bound; lemma += o.lemma; full += o.full; read += o.read;
    lines += o.lines; exact += o.exact;
    kGold += o.kGold; kPred += o.kPred; kHit += o.kHit;
  }
  void Print(const char* name) const {
    double p = pred ? static_cast<double>(bound) / static_cast<double>(pred) : 0;
    double r = gold ? static_cast<double>(bound) / static_cast<double>(gold) : 0;
    double f = p + r > 0 ? 2 * p * r / (p + r) : 0;
    std::printf("%-28s lines %6zu tokens %7zu  boundaryF1 %.4f (P %.4f R %.4f)  +lemma %.4f  +pos %.4f  "
                "+reading %.4f  exact-lines %.4f\n",
                name, lines, gold, f, p, r, gold ? double(lemma) / double(gold) : 0,
                gold ? double(full) / double(gold) : 0, gold ? double(read) / double(gold) : 0,
                lines ? double(exact) / double(lines) : 0);
    if (kGold) {
      double kp = kPred ? double(kHit) / double(kPred) : 0, kr = double(kHit) / double(kGold);
      std::printf("%-28s key-boundary F1 %.4f (P %.4f R %.4f) over %zu reference boundaries\n", name,
                  kp + kr > 0 ? 2 * kp * kr / (kp + kr) : 0, kp, kr, kGold);
    }
  }
};

// For a line the analyzer gets wrong: the gold path's cost against the best path, region by region.
// Output rows: line, kind (missing | cost), gold cost - best cost, gold tokens | predicted tokens of each
// differing region. A missing gold arc means the lexicon, the derive rules or a constraint lacks it.
void Explain(const Model& m, const GoldLine& g, const Workspace& ws, size_t lineNo,
             std::FILE* out) {
  // src byte → core index
  std::map<uint32_t, uint32_t> atB, atE;
  for (uint32_t k = 0; k < ws.coreN.size(); ++k) {
    atB[ws.nchars[ws.coreN[k]].sb] = k;
    atE[ws.nchars[ws.coreN[k]].se] = k + 1;
  }
  auto label = [&](const Arc& a) {
    uint16_t cls = a.kind == ArcKind::lex ? m.pforms[a.form].cls : a.lcls;
    const auto& c = m.classes[cls];
    std::string lemma = a.kind == ArcKind::lex ? std::string(m.S(m.groups[a.ref].lemma)) : std::string("*");
    return lemma + "/" + m.udNames[c.ud] + "/" + m.cformNames[c.cform];
  };
  std::vector<int64_t> goldArcs;
  std::string missing;
  for (auto& gt : g.toks) {
    auto ib = atB.find(gt.b), ie = atE.find(gt.e);
    if (ib == atB.end() || ie == atE.end()) {
      missing += " " + gt.surf + "[span]";
      continue;
    }
    int best = -1, bestScore = -1;
    for (uint32_t k = ws.beginStart[ib->second]; k < ws.beginStart[ib->second + 1]; ++k) {
      uint32_t ai = ws.byBegin[k];
      const Arc& a = ws.arcs[ai];
      if (a.e != ie->second || a.kind == ArcKind::longUnit) continue;
      uint16_t cls = a.kind == ArcKind::lex ? m.pforms[a.form].cls : a.lcls;
      const auto& c = m.classes[cls];
      int sc = 0;
      if (gt.unk && a.kind == ArcKind::unk) sc += 8;
      if (a.kind == ArcKind::lex && m.S(m.groups[a.ref].lemma) == gt.lemma) sc += 4;
      if (m.udNames[c.ud] == gt.ud) sc += 2;
      if (m.cformNames[c.cform] == gt.cform) sc += 1;
      if (sc > bestScore || (sc == bestScore && best >= 0 && a.cost < ws.arcs[static_cast<size_t>(best)].cost)) {
        bestScore = sc;
        best = static_cast<int>(ai);
      }
    }
    if (best < 0) missing += " " + gt.surf + "[" + gt.lemma + "/" + gt.ud + "/" + gt.cform + "]";
    else goldArcs.push_back(best);
  }
  if (!missing.empty()) {
    std::fprintf(out, "%zu\tmissing\t0\t%s\n", lineNo, missing.c_str() + 1);
    return;
  }
  int64_t cost = 0;
  uint16_t prev = 0;
  for (int64_t ai : goldArcs) {
    const Arc& a = ws.arcs[static_cast<size_t>(ai)];
    cost += m.Conn(prev, a.lcls) + a.cost;
    prev = a.rcls;
  }
  cost += m.Conn(prev, 0);
  // differing regions between the gold arcs and the best path
  std::vector<uint32_t> pred(ws.path.begin(), ws.path.end());
  size_t i = 0, j = 0;
  std::string regions;
  while (i < goldArcs.size() && j < pred.size()) {
    const Arc& ga = ws.arcs[static_cast<size_t>(goldArcs[i])];
    const Arc& pa = ws.arcs[pred[j]];
    if (ga.b == pa.b && ga.e == pa.e && label(ga) == label(pa)) {
      ++i;
      ++j;
      continue;
    }
    std::string gs, ps;
    uint16_t end = 0;
    do {
      const Arc& x = ws.arcs[static_cast<size_t>(goldArcs[i])];
      const Arc& y = ws.arcs[pred[j]];
      if (x.e <= y.e) {
        gs += " " + internal::ToUtf8(std::u32string(ws.core.begin() + x.b, ws.core.begin() + x.e)) + "[" + label(x) + "]";
        end = x.e;
        ++i;
        if (x.e == y.e) {
          ps += " " + internal::ToUtf8(std::u32string(ws.core.begin() + y.b, ws.core.begin() + y.e)) + "[" + label(y) + "]";
          ++j;
        }
      } else {
        ps += " " + internal::ToUtf8(std::u32string(ws.core.begin() + y.b, ws.core.begin() + y.e)) + "[" + label(y) + "]";
        end = y.e;
        ++j;
      }
    } while (i < goldArcs.size() && j < pred.size() &&
             ws.arcs[static_cast<size_t>(goldArcs[i])].b != ws.arcs[pred[j]].b);
    (void)end;
    regions += "\t" + gs.substr(1) + " |" + ps;
  }
  std::fprintf(out, "%zu\tcost\t%lld%s\n", lineNo, static_cast<long long>(cost - ws.best), regions.c_str());
}

void KeyScore(const KeySet& ks, const GoldLine& g, const Analysis& r, Score* s) {
  std::set<uint32_t> gb, pb;
  for (auto& t : g.toks) {
    gb.insert(t.b);
    gb.insert(t.e);
  }
  for (auto& t : r.toks) {
    if (t.surf.empty()) continue;
    pb.insert(t.src.begin);
    pb.insert(t.src.end);
  }
  std::set<uint32_t> skip;  // line ends and positions inside key spans
  skip.insert(0);
  skip.insert(static_cast<uint32_t>(g.src.size()));
  std::vector<uint32_t> charAt;  // byte offsets of character starts
  for (uint32_t i = 0; i < g.src.size(); ++i)
    if ((static_cast<unsigned char>(g.src[i]) & 0xC0) != 0x80) charAt.push_back(i);
  charAt.push_back(static_cast<uint32_t>(g.src.size()));
  for (size_t a = 0; a + 1 < charAt.size(); ++a) {
    if (!gb.count(charAt[a])) continue;
    for (size_t b = a + 2; b < charAt.size() && b - a <= ks.maxChars; ++b) {
      if (!gb.count(charAt[b])) continue;
      if (!ks.keys.count(g.src.substr(charAt[a], charAt[b] - charAt[a]))) continue;
      for (size_t k = a + 1; k < b; ++k) skip.insert(charAt[k]);
    }
  }
  for (uint32_t x : gb)
    if (!skip.count(x)) {
      ++s->kGold;
      if (pb.count(x)) ++s->kHit;
    }
  for (uint32_t x : pb)
    if (!skip.count(x)) ++s->kPred;
}

Score EvalOne(const Analyzer& an, const GoldLine& g, const Options& opt, Analysis* r, Workspace* ws,
              std::FILE* diff, std::FILE* explain = nullptr, size_t lineNo = 0, const KeySet* keys = nullptr) {
  Score s;
  an.Analyze(g.src, opt, r, ws);
  if (keys) KeyScore(*keys, g, *r, &s);
  s.lines = 1;
  s.gold = g.toks.size();
  std::map<std::pair<uint32_t, uint32_t>, const Token*> pred;
  for (auto& t : r->toks) {
    if (t.surf.empty()) continue;
    pred[{t.src.begin, t.src.end}] = &t;
  }
  s.pred = pred.size();
  bool all = pred.size() == g.toks.size();
  for (auto& gt : g.toks) {
    auto it = pred.find({gt.b, gt.e});
    if (it == pred.end()) {
      all = false;
      continue;
    }
    ++s.bound;
    const Token& t = *it->second;
    bool lemmaOk = t.lemma == gt.lemma || (gt.unk && (t.flags & Unknown));
    if (lemmaOk) ++s.lemma;
    if (lemmaOk && an.UdName(t.ud) == gt.ud && an.CtypeName(t.ctype) == gt.ctype && an.CformName(t.cform) == gt.cform)
      ++s.full;
    else
      all = false;
    if (t.reading == gt.kana || (gt.unk && t.reading.empty())) ++s.read;
  }
  if (all) s.exact = 1;
  if (explain && !all) Explain(*an.model(), g, *ws, lineNo, explain);
  if (diff && !all) {
    std::fprintf(diff, "%s\n  G:", g.src.c_str());
    for (auto& gt : g.toks) std::fprintf(diff, " %s[%s/%s/%s]", gt.surf.c_str(), gt.lemma.c_str(), gt.ud.c_str(), gt.cform.c_str());
    std::fprintf(diff, "\n  P:");
    for (auto& t : r->toks)
      std::fprintf(diff, " %s[%s/%s/%s]%s", t.surf.c_str(), t.lemma.c_str(), std::string(an.UdName(t.ud)).c_str(),
                   std::string(an.CformName(t.cform)).c_str(), t.derive.empty() ? "" : ("{" + t.derive + "}").c_str());
    std::fprintf(diff, "\n");
  }
  return s;
}

int CmdEval(const Args& a) {
  if (a.pos.size() < 2) return Die("eval CORE.bin GOLD...");
  Analyzer an;
  if (!LoadAnalyzer(&an, a.pos[0], a.Get("-l"), nullptr)) return 1;
  Options opt = OptsFrom(a);
  if (a.Has("--no-derive")) {
    an.mutableModel()->rules.clear();
    an.mutableModel()->stutterRule = -1;
  }
  opt.margins = false;
  std::FILE* diff = nullptr;
  if (a.Has("--diff")) diff = std::fopen(a.Get("--diff").c_str(), "w");
  std::FILE* explain = nullptr;
  if (a.Has("--explain")) explain = std::fopen(a.Get("--explain").c_str(), "w");
  size_t lineNo = 0;
  KeySet keySet;
  if (a.Has("--keys")) {
    std::ifstream in(a.Get("--keys"), std::ios::binary);
    std::string k;
    while (std::getline(in, k)) {
      if (k.empty()) continue;
      size_t chars = 0;
      for (char c : k) chars += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
      keySet.maxChars = std::max(keySet.maxChars, chars);
      keySet.keys.insert(k);
    }
  }
  auto ws = NewWorkspace();
  Analysis r;
  Score total;
  for (size_t i = 1; i < a.pos.size(); ++i) {
    std::vector<GoldLine> gold;
    if (!ReadGold(a.pos[i], &gold)) return Die("cannot read " + a.pos[i]);
    Score s;
    for (auto& g : gold)
      s.Add(EvalOne(an, g, opt, &r, ws.get(), diff, explain, ++lineNo, keySet.keys.empty() ? nullptr : &keySet));
    s.Print(fs::path(a.pos[i]).filename().string().c_str());
    total.Add(s);
  }
  if (a.pos.size() > 2) total.Print("pooled");
  if (diff) std::fclose(diff);
  if (explain) std::fclose(explain);
  return 0;
}

// ---- bench -----------------------------------------------------------------------------------------
int CmdBench(const Args& a) {
  if (a.pos.size() < 2) return Die("bench CORE.bin FILE");
  Analyzer an;
  double loadMs = 0;
  if (!LoadAnalyzer(&an, a.pos[0], a.Get("-l"), &loadMs)) return 1;
  std::ifstream in(a.pos[1], std::ios::binary);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) lines.push_back(line);
  Options opt = OptsFrom(a);
  auto ws = NewWorkspace();
  Analysis r;
  size_t toks = 0;
  int reps = std::atoi(a.Get("--reps", "3").c_str());
  auto t0 = std::chrono::steady_clock::now();
  for (int k = 0; k < reps; ++k)
    for (auto& l : lines) {
      an.Analyze(l, opt, &r, ws.get());
      toks += r.toks.size();
    }
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("load %.1f ms; %zu lines x %d in %.3f s: %.0f lines/s, %.0f tokens/s\n", loadMs, lines.size(), reps, s,
              double(lines.size()) * reps / s, double(toks) / s);
  return 0;
}

// ---- long units -------------------------------------------------------------------------------------
// KEYS.tsv: key, kind (n pn adn adv na phrase v adj). Keys the core reads as one token are dropped. The rest
// become long-unit arcs entered through the class of their first token, costing one unit (pf.units). A v/adj key is a stem: it takes the paradigm of the verb or adjective whose stem ends it.
int CmdLongUnits(const Args& a) {
  if (a.pos.size() < 3) return Die("longunits CORE.bin KEYS.tsv OUT.tsv");
  Analyzer an;
  if (!LoadAnalyzer(&an, a.pos[0], "", nullptr)) return 1;
  const Model& m = *an.model();
  struct Row {
    std::u32string key;
    std::string line;
  };
  std::vector<Row> rows;
  std::string err;
  Workspace ws;
  Options opt;
  opt.longUnits = false;
  opt.margins = false;
  std::map<std::string, size_t> total, single, multi, bad;
  std::set<std::u32string> seen;
  // stem → inflecting groups (verbs, adjectives)
  std::unordered_map<std::u32string, std::vector<uint32_t>> stems;
  for (uint32_t g = 0; g < m.groups.size(); ++g) {
    const auto& gr = m.groups[g];
    std::string_view ud = m.udNames[gr.ud];
    if (gr.ctype == 0 || !(ud.substr(0, 6) == "動詞" || ud.substr(0, 9) == "形容詞")) continue;
    stems[std::u32string(m.C(gr.stem, gr.stemLen))].push_back(g);
  }
  auto analyze = [&](const std::string& s, int64_t* cost) {
    internal::Normalize(s, &ws.nchars, &ws.edits);
    BuildLattice(m, &ws, opt);
    RunViterbi(m, &ws, false);
    int64_t inner = 0;
    for (size_t k = 0; k < ws.path.size(); ++k) {
      const Arc& x = ws.arcs[ws.path[k]];
      inner += x.cost;
      if (k) inner += m.Conn(ws.arcs[ws.path[k - 1]].rcls, x.lcls);
    }
    *cost = inner;
    return ws.path.size();
  };
  // A unit starts and ends with a content word: keys that open or close on a particle, an auxiliary
  // or a symbol (の死に, に困ら, 早口に, あるから) are contextual rows of the translation dictionary.
  auto function = [&](const Arc& x) {
    if (x.kind != ArcKind::lex && x.kind != ArcKind::unk) return false;
    Pos p = m.classPos[x.lcls];
    return p == Pos::aux || p == Pos::sym || p == Pos::p_case || p == Pos::p_topic || p == Pos::p_conj ||
           p == Pos::p_final || p == Pos::p_adv;
  };
  size_t edgeFunction = 0;
  auto udOf = [](const std::string& kind) -> std::string {
    if (kind == "n") return "名詞-普通名詞-一般";
    if (kind == "pn") return "名詞-固有名詞-一般";
    if (kind == "adn") return "連体詞";
    if (kind == "adv") return "副詞";
    if (kind == "na") return "形状詞-一般";
    return "";
  };
  if (!ReadTsv(a.pos[1], [&](const std::vector<std::string>& f) {
        if (f.size() < 2) return;
        const std::string& key = f[0];
        const std::string& kind = f[1];
        ++total[kind];
        std::u32string k32 = internal::ToU32(key);
        if (k32.empty() || !seen.insert(k32).second) return;
        std::ostringstream o;
        if (kind == "v" || kind == "adj") {
          // stems of inflecting groups that end the key; the one whose dictionary form reads best wins
          int32_t bestG = -1;
          int64_t bestCost = 0;
          size_t stemLen = 0;
          uint16_t firstCls = 0;
          for (size_t len = k32.size(); len >= 1; --len) {
            auto it = stems.find(k32.substr(k32.size() - len));
            if (it == stems.end()) continue;
            for (uint32_t g : it->second) {
              std::string_view ud = m.udNames[m.groups[g].ud];
              if ((kind == "v") != (ud.substr(0, 6) == "動詞")) continue;
              const auto& p = m.paradigms[m.groups[g].paradigm];
              const Model::PForm* dict = nullptr;
              for (uint32_t fi = p.first; fi < p.first + p.count; ++fi)
                if (m.cformNames[m.classes[m.pforms[fi].cls].cform] == "終止形-一般") dict = &m.pforms[fi];
              if (!dict) continue;
              std::string full = key + internal::ToUtf8(m.C(dict->suf, dict->sufLen));
              int64_t c = 0;
              if (analyze(full, &c) == 0) continue;
              const Arc& last = ws.arcs[ws.path.back()];
              if (last.kind != ArcKind::lex || last.ref != g) continue;  // the form must read as this verb
              if (bestG < 0 || c < bestCost) {
                bestG = static_cast<int32_t>(g);
                bestCost = c;
                stemLen = len;
                firstCls = ws.arcs[ws.path.front()].lcls;
              }
            }
          }
          if (bestG < 0) {
            ++bad[kind];
            return;
          }
          if (stemLen == k32.size()) {
            ++single[kind];
            return;
          }
          std::string prefix = internal::ToUtf8(k32.substr(0, k32.size() - stemLen));
          {
            int64_t ignored = 0;
            if (analyze(prefix, &ignored) == 0 || function(ws.arcs[ws.path.front()])) {
              ++edgeFunction;
              return;
            }
          }
          const auto& g = m.groups[static_cast<size_t>(bestG)];
          const auto& p = m.paradigms[g.paradigm];
          ++multi[kind];
          o << key << '\t' << Analyzer::PosName(m.classPos[m.pforms[p.first].cls]) << '\t' << firstCls << '\t'
            << m.pforms[p.first].cls << '\t' << m.unitCost << '\t' << m.udNames[g.ud] << "\t*\t*\t" << prefix
            << m.S(g.lemma) << "\t*\t" << g.paradigm;
        } else {
          int64_t inner = 0;
          size_t ntok = analyze(key, &inner);
          if (ntok == 0 || ws.core.size() != k32.size()) {
            ++bad[kind];
            return;
          }
          if (ntok == 1) {
            ++single[kind];
            return;
          }
          const Arc& first = ws.arcs[ws.path.front()];
          const Arc& last = ws.arcs[ws.path.back()];
          if (function(first) || function(last)) {
            ++edgeFunction;
            return;
          }
          ++multi[kind];
          const auto& lc = m.classes[last.rcls];
          std::string ud = udOf(kind), ct = "*", cf = "*";
          std::string pos = kind == "phrase" ? std::string(Analyzer::PosName(m.classPos[last.rcls])) : kind;
          if (ud.empty()) {
            ud = m.udNames[lc.ud];
            ct = m.ctypeNames[lc.ctype].empty() ? "*" : m.ctypeNames[lc.ctype];
            cf = m.cformNames[lc.cform].empty() ? "*" : m.cformNames[lc.cform];
          }
          o << key << '\t' << pos << '\t' << first.lcls << '\t' << last.rcls << '\t' << m.unitCost << '\t' << ud
            << '\t' << ct << '\t' << cf << "\t*\t*\t*";
        }
        rows.push_back({k32, o.str()});
      }, &err))
    return Die(err);
  std::sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) { return x.key < y.key; });
  std::ofstream o(a.pos[2], std::ios::binary);
  o << "#! analyzer-long\nkey\tpos\tlcls\trcls\tcost\tud\tctype\tcform\tlemma\treading\tparadigm\n";
  for (auto& r : rows) o << r.line << '\n';
  std::fprintf(stderr, "dropped: open or close on a function word %zu\n", edgeFunction);
  for (auto& [kind, n] : total)
    std::fprintf(stderr, "%-6s keys %7zu: one token %7zu, long units %7zu, unusable %6zu\n", kind.c_str(), n,
                 single[kind], multi[kind], bad[kind]);
  return 0;
}

int CmdHints(const Args& a) {
  if (a.pos.empty()) return Die("hints CORE.bin");
  Analyzer an;
  if (!LoadAnalyzer(&an, a.pos[0], a.Get("-l"), nullptr)) return 1;
  std::string line;
  SegHints h;
  while (std::getline(std::cin, line)) {
    an.Hints(line, &h, nullptr, static_cast<float>(std::atof(a.Get("--slack", "2.0").c_str())));
    std::string o;
    for (size_t i = 0; i < line.size(); ++i) {
      if (h.ends[i]) o += std::string("|") + static_cast<char>(h.ends[i]);
      o += line[i];
    }
    o += "|e";
    std::cout << o;
    for (auto& r : h.rejected)
      std::cout << "  rej:" << line.substr(r.span.begin, r.span.end - r.span.begin) << '/' << static_cast<char>(r.cls);
    for (auto& r : h.respell) std::cout << "  respell:" << line.substr(r.span.begin, r.span.end - r.span.begin) << "→" << r.kanji;
    std::cout << '\n';
  }
  return 0;
}

// ---- cases ---------------------------------------------------------------------------------------
// CASES.tsv rows: id, kind (fix|keep|todo), input, expect, note. A todo case is reported, not failed. expect is a run of tokens, each surf or surf[lemma],
// separated by spaces; the case passes when the analysis of input contains that run.
int CmdCases(const Args& a) {
  if (a.pos.size() < 2) return Die("cases CORE.bin|DATA_DIR CASES.tsv");
  Analyzer an;
  std::string err;
  if (fs::is_directory(a.pos[0])) {
    Model m;
    if (!LoadTsvDir(a.pos[0], &m, &err)) return Die(err);
    Finalize(&m);
    if (!an.Load(Serialize(m), &err)) return Die(err);
  } else if (!LoadAnalyzer(&an, a.pos[0], a.Get("-l"), nullptr)) {
    return 1;
  }
  Options opt;
  opt.margins = false;
  auto ws = NewWorkspace();
  Analysis r;
  int n = 0, failed = 0, todo = 0;
  if (!ReadTsv(a.pos[1], [&](const std::vector<std::string>& f) {
        if (f.size() < 4) return;
        ++n;
        an.Analyze(f[2], opt, &r, ws.get());
        std::vector<std::string> want = SplitOn(f[3], ' ');
        std::vector<std::pair<std::string, std::string>> got;
        for (auto& t : r.toks) got.push_back({t.surf, t.lemma});
        bool ok = false;
        for (size_t i = 0; i + want.size() <= got.size() && !ok; ++i) {
          ok = true;
          for (size_t k = 0; k < want.size() && ok; ++k) {
            const std::string& w = want[k];
            auto lb = w.find('[');
            std::string surf = w.substr(0, lb), lemma = lb == std::string::npos ? "" : w.substr(lb + 1, w.size() - lb - 2);
            ok = got[i + k].first == surf && (lemma.empty() || got[i + k].second == lemma);
          }
        }
        if (!ok && f[1] == "todo") {
          ++todo;
          return;
        }
        if (!ok) {
          ++failed;
          std::string s;
          for (auto& g : got) s += " " + g.first + "[" + g.second + "]";
          std::fprintf(stderr, "FAIL %s (%s): want %s, got%s\n", f[0].c_str(), f[1].c_str(), f[3].c_str(), s.c_str());
        }
      }, &err))
    return Die(err);
  std::printf("%d cases, %d failed, %d todo not yet passing\n", n, failed, todo);
  return failed ? 1 : 0;
}

int CmdNorm() {
  std::string line, out;
  std::vector<internal::NChar> nc;
  std::vector<NormEdit> edits;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    internal::Normalize(line, &nc, &edits);
    out.clear();
    for (auto& c : nc) internal::AppendUtf8(out, c.c);
    std::cout << out << '\n';
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return Die("usage: hd8ana pack|pack-long|run|eval|bench|longunits|hints|norm ...");
  std::string cmd = argv[1];
  std::set<std::string> valued{"--slack", "-l", "--format", "--reps", "--doc", "--diff", "--explain", "--keys"};
  Args a = Parse(argc, argv, valued);
  if (cmd == "pack") return CmdPack(a);
  if (cmd == "pack-long") return CmdPackLong(a);
  if (cmd == "run") return CmdRun(a);
  if (cmd == "eval") return CmdEval(a);
  if (cmd == "bench") return CmdBench(a);
  if (cmd == "longunits") return CmdLongUnits(a);
  if (cmd == "norm") return CmdNorm();
  if (cmd == "cases") return CmdCases(a);
  if (cmd == "hints") return CmdHints(a);
  return Die("unknown command " + cmd);
}
