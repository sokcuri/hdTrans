// hd8ana: build, run, evaluate and train the 0.8 analyzer.
//   hd8ana pack DATA_DIR OUT.bin
//   hd8ana pack-long CORE.bin LONG.tsv OUT.bin
//   hd8ana run CORE.bin [-l LONG.bin] [--units] [--format json|wakati|tsv|text] [--repair [--repair-tsv FILE]] [--why]
//              (stdin → stdout; --units keeps long units whole; --repair reads noisy spans with ja/repair.tsv,
//              --format text writes each line with its repairs applied, --why lists checks and repairs on stderr)
//   hd8ana eval CORE.bin [-l LONG.bin] [--units] [--no-derive] GOLD...        (tag.py format)
//   hd8ana bench CORE.bin FILE [-l LONG.bin] [--no-margins]
//   hd8ana train DATA_DIR SILVER OUT_DATA_DIR [--epochs N] [--step S] [--limit L]
//   hd8ana longunits CORE.bin KEYS.tsv OUT.tsv [--bonus NATS]
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
  std::fprintf(stderr, "classes %zu groups %zu paradigms %zu pairs %zu rules %zu constraints %zu: %zu bytes\n",
               m.classes.size(), m.groups.size(), m.paradigms.size(), m.pairs.size(), m.rules.size(),
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
  o.repair = a.Has("--repair");
  o.why = a.Has("--why");
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
  if (opt.repair) {
    std::string tsv, err;
    std::string path = a.Get("--repair-tsv", "poc08/analyzer/data/ja/repair.tsv");
    if (!ReadFile(path, &tsv)) return Die("cannot read " + path);
    if (!an.LoadRepair(tsv, &err)) return Die(err);
  }
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
    if (opt.why && (!r.checks.empty() || !r.repairs.empty())) {
      std::cerr << "line " << n << ": " << line << '\n' << r.checks;
      for (auto& rp : r.repairs)
        std::cerr << "  repair " << rp.op << ' ' << (rp.from.empty() ? "∅" : rp.from) << "→" << (rp.to.empty() ? "∅" : rp.to)
                  << " on " << r.toks[rp.tok].surf << '\n';
    }
    if (fmt == "json") {
      std::cout << ToJson(an, line, r, a.Get("--doc"), n) << '\n';
    } else if (fmt == "text") {
      std::cout << (r.repairs.empty() ? line : r.repaired) << '\n';
    } else if (fmt == "wakati") {
      for (size_t i = 0; i < r.toks.size(); ++i) std::cout << (i ? " " : "") << r.toks[i].surf;
      std::cout << '\n';
    } else {
      for (auto& t : r.toks)
        std::cout << t.surf << '\t' << t.lemma << '\t' << t.reading << '\t' << Analyzer::PosName(t.pos) << '\t'
                  << an.UdName(t.ud) << '\t' << an.CtypeName(t.ctype) << '\t' << an.CformName(t.cform) << '\t'
                  << t.margin << (t.derive.empty() ? "" : "\t") << t.derive << (t.repair.empty() ? "" : "\t")
                  << t.repair << '\n';
      std::cout << "EOS\n";
    }
  }
  return 0;
}

// ---- eval ------------------------------------------------------------------------------------------
struct Score {
  size_t gold = 0, pred = 0, bound = 0, lemma = 0, full = 0, read = 0, lines = 0, exact = 0;
  void Add(const Score& o) {
    gold += o.gold; pred += o.pred; bound += o.bound; lemma += o.lemma; full += o.full; read += o.read;
    lines += o.lines; exact += o.exact;
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
  }
};

Score EvalOne(const Analyzer& an, const GoldLine& g, const Options& opt, Analysis* r, Workspace* ws,
              std::FILE* diff) {
  Score s;
  an.Analyze(g.src, opt, r, ws);
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
  if (opt.repair) {
    std::string tsv, err;
    std::string path = a.Get("--repair-tsv", "poc08/analyzer/data/ja/repair.tsv");
    if (!ReadFile(path, &tsv)) return Die("cannot read " + path);
    if (!an.LoadRepair(tsv, &err)) return Die(err);
  }
  if (a.Has("--no-derive")) {
    an.mutableModel()->rules.clear();
    an.mutableModel()->stutterRule = -1;
  }
  opt.margins = false;
  std::FILE* diff = nullptr;
  if (a.Has("--diff")) diff = std::fopen(a.Get("--diff").c_str(), "w");
  auto ws = NewWorkspace();
  Analysis r;
  Score total;
  for (size_t i = 1; i < a.pos.size(); ++i) {
    std::vector<GoldLine> gold;
    if (!ReadGold(a.pos[i], &gold)) return Die("cannot read " + a.pos[i]);
    Score s;
    for (auto& g : gold) s.Add(EvalOne(an, g, opt, &r, ws.get(), diff));
    s.Print(fs::path(a.pos[i]).filename().string().c_str());
    total.Add(s);
  }
  if (a.pos.size() > 2) total.Print("pooled");
  if (diff) std::fclose(diff);
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

// ---- train (averaged structured perceptron) ---------------------------------------------------------
struct TrainSent {
  std::string src;
  std::vector<GoldTok> toks;
  std::vector<int> cls;
};

int CmdTrain(const Args& a) {
  if (a.pos.size() < 3) return Die("train DATA_DIR SILVER OUT_DATA_DIR");
  std::string dataDir = a.pos[0], outDir = a.pos[2];
  Model m;
  std::string err;
  if (!LoadTsvDir(dataDir, &m, &err)) return Die(err);
  Finalize(&m);
  m.rules.clear();        // derived arcs are hand-set, not learned from the teacher
  m.stutterRule = -1;
  m.constraints.clear();
  size_t limit = static_cast<size_t>(std::atoll(a.Get("--limit", "100000000").c_str()));
  int epochs = std::atoi(a.Get("--epochs", "3").c_str());
  int step = std::atoi(a.Get("--step", "30").c_str());
  std::unordered_map<std::string, int> classKey;
  for (size_t i = 0; i < m.classes.size(); ++i) {
    const auto& c = m.classes[i];
    classKey[m.udNames[c.ud] + '\t' + m.ctypeNames[c.ctype] + '\t' + m.cformNames[c.cform] + '\t' +
             std::string(m.S(c.lemma))] = static_cast<int>(i);
  }
  auto findCls = [&](const GoldTok& t) {
    auto it = classKey.find(t.ud + '\t' + t.ctype + '\t' + t.cform + '\t' + t.lemma);
    if (it != classKey.end()) return it->second;
    it = classKey.find(t.ud + '\t' + t.ctype + '\t' + t.cform + '\t');
    return it == classKey.end() ? -1 : it->second;
  };
  std::vector<GoldLine> gold;
  if (!ReadGold(a.pos[1], &gold, limit)) return Die("cannot read silver");
  std::vector<TrainSent> sents;
  size_t noClass = 0;
  for (auto& g : gold) {
    TrainSent s{std::move(g.src), std::move(g.toks), {}};
    bool ok = !s.toks.empty();
    for (auto& t : s.toks) {
      int c = findCls(t);
      if (c < 0) ok = false;
      s.cls.push_back(c);
    }
    if (ok) sents.push_back(std::move(s));
    else ++noClass;
  }
  gold.clear();
  std::fprintf(stderr, "sentences %zu (skipped %zu without a class)\n", sents.size(), noClass);

  size_t n = m.nClass;
  // parameters: conn cells, group costs, unknown-word costs; averaged with the lazy trick
  std::vector<double> connU(n * n, 0.0), groupU(m.groups.size(), 0.0);
  std::vector<std::vector<double>> unkU(m.cats.size()), unkPU(m.cats.size());
  for (size_t c = 0; c < m.cats.size(); ++c) {
    unkU[c].assign(m.cats[c].arcs.size(), 0.0);
    unkPU[c].assign(m.cats[c].arcs.size(), 0.0);
  }
  std::vector<uint8_t> touched(n * n, 0);
  std::vector<int32_t> connW(m.conn.begin(), m.conn.end());
  double t = 1;
  Options opt;
  opt.longUnits = false;
  opt.margins = false;
  Workspace ws;
  std::mt19937 rng(7);
  std::vector<size_t> order(sents.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;

  auto updConn = [&](uint16_t p, uint16_t q, int d) {
    size_t k = static_cast<size_t>(p) * n + q;
    connW[k] += d;
    m.conn[k] = static_cast<int16_t>(std::clamp(connW[k], -32000, 32000));
    connU[k] += t * d;
    touched[k] = 1;
  };
  auto updArc = [&](const Arc& x, int d) {
    if (x.kind == ArcKind::lex) {
      m.groups[x.ref].cost += d;
      groupU[x.ref] += t * d;
    } else if (x.kind == ArcKind::unk) {
      size_t c = x.ref >> 8, k = x.ref & 0xFF;
      m.cats[c].arcs[k].cost += d;
      unkU[c][k] += t * d;
      int dl = d * (x.e - x.b);
      m.cats[c].arcs[k].perChar += dl;
      unkPU[c][k] += t * dl;
    }
  };
  std::vector<uint32_t> goldPath;
  std::vector<int32_t> byteToCore;
  for (int ep = 0; ep < epochs; ++ep) {
    std::shuffle(order.begin(), order.end(), rng);
    size_t wrong = 0, skipped = 0, done = 0;
    for (size_t oi : order) {
      const TrainSent& s = sents[oi];
      internal::Normalize(s.src, &ws.nchars, &ws.edits);
      BuildLattice(m, &ws, opt);
      // gold arcs
      byteToCore.assign(s.src.size() + 1, -1);
      std::vector<int32_t> endToCore(s.src.size() + 1, -1);
      for (size_t k = 0; k < ws.core.size(); ++k) {
        byteToCore[ws.nchars[ws.coreN[k]].sb] = static_cast<int32_t>(k);
        endToCore[ws.nchars[ws.coreN[k]].se] = static_cast<int32_t>(k + 1);
      }
      goldPath.clear();
      bool ok = true;
      for (size_t ti = 0; ti < s.toks.size() && ok; ++ti) {
        const auto& gt = s.toks[ti];
        int32_t b = byteToCore[gt.b], e = endToCore[gt.e];
        if (b < 0 || e <= b) {
          ok = false;
          break;
        }
        int32_t found = -1;
        for (uint32_t k = ws.beginStart[static_cast<size_t>(b)]; k < ws.beginStart[static_cast<size_t>(b) + 1]; ++k) {
          const Arc& x = ws.arcs[ws.byBegin[k]];
          if (x.e != e) continue;
          if (gt.unk) {
            if (x.kind == ArcKind::unk && x.lcls == s.cls[ti]) found = static_cast<int32_t>(ws.byBegin[k]);
          } else if (x.kind == ArcKind::lex && m.pforms[x.form].cls == s.cls[ti] &&
                     m.S(m.groups[x.ref].lemma) == gt.lemma) {
            if (found < 0) found = static_cast<int32_t>(ws.byBegin[k]);
            const auto& g = m.groups[x.ref];
            std::string rd;
            if (g.readKind == 0) {
              for (char32_t c : m.C(g.stem, g.stemLen)) internal::AppendUtf8(rd, internal::ToKatakana(c));
            } else if (g.readKind == 1) {
              rd = std::string(m.S(g.read));
            }
            rd += m.S(m.pforms[x.form].read);
            if (rd == gt.kana) found = static_cast<int32_t>(ws.byBegin[k]);
          }
          if (found >= 0 && gt.unk) break;
        }
        if (found < 0) ok = false;
        else goldPath.push_back(static_cast<uint32_t>(found));
      }
      if (!ok) {
        ++skipped;
        continue;
      }
      RunViterbi(m, &ws, false);
      ++done;
      if (ws.path != goldPath) {
        ++wrong;
        uint16_t prev = 0;
        for (uint32_t ai : goldPath) {
          updConn(prev, ws.arcs[ai].lcls, -step);
          updArc(ws.arcs[ai], -step);
          prev = ws.arcs[ai].rcls;
        }
        updConn(prev, 0, -step);
        prev = 0;
        for (uint32_t ai : ws.path) {
          updConn(prev, ws.arcs[ai].lcls, step);
          updArc(ws.arcs[ai], step);
          prev = ws.arcs[ai].rcls;
        }
        updConn(prev, 0, step);
      }
      t += 1;
    }
    std::fprintf(stderr, "epoch %d: %zu sentences, %zu wrong (%.2f%%), %zu skipped\n", ep + 1, done, wrong,
                 done ? 100.0 * double(wrong) / double(done) : 0.0, skipped);
  }
  // averaged weights
  auto avg = [&](double w, double u) { return static_cast<int32_t>(std::lround(w - u / t)); };
  for (size_t g = 0; g < m.groups.size(); ++g) m.groups[g].cost = avg(m.groups[g].cost, groupU[g]);
  for (size_t c = 0; c < m.cats.size(); ++c)
    for (size_t k = 0; k < m.cats[c].arcs.size(); ++k) {
      m.cats[c].arcs[k].cost = avg(m.cats[c].arcs[k].cost, unkU[c][k]);
      m.cats[c].arcs[k].perChar = avg(m.cats[c].arcs[k].perChar, unkPU[c][k]);
    }
  // write the output data dir: copy everything, then rewrite the learned columns
  fs::create_directories(outDir + "/model");
  fs::create_directories(outDir + "/ja");
  for (auto& f : fs::recursive_directory_iterator(dataDir)) {
    if (!f.is_regular_file()) continue;
    auto rel = fs::relative(f.path(), dataDir);
    fs::create_directories((fs::path(outDir) / rel).parent_path());
    fs::copy_file(f.path(), fs::path(outDir) / rel, fs::copy_options::overwrite_existing);
  }
  {
    std::set<std::pair<uint16_t, uint16_t>> observed;
    for (auto& p : m.pairs) observed.insert({p.l, p.r});
    std::ofstream o(outDir + "/model/conn.tsv", std::ios::binary);
    o << "#! analyzer-conn\nleft\tright\tcost\n";
    size_t written = 0;
    for (size_t p = 0; p < n; ++p)
      for (size_t q = 0; q < n; ++q) {
        size_t k = p * n + q;
        bool obs = observed.count({static_cast<uint16_t>(p), static_cast<uint16_t>(q)}) != 0;
        if (!obs && !touched[k]) continue;
        int32_t v = std::clamp(avg(connW[k], connU[k]), -32000, 32000);
        if (!obs && v == m.boLeft[p] + m.uniRight[q]) continue;
        o << p << '\t' << q << '\t' << v << '\n';
        ++written;
      }
    std::fprintf(stderr, "conn pairs written %zu (observed %zu)\n", written, observed.size());
  }
  {
    std::ifstream in(dataDir + "/model/lexicon.tsv", std::ios::binary);
    std::ofstream o(outDir + "/model/lexicon.tsv", std::ios::binary);
    std::string line;
    size_t row = 0;
    bool header = false;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#' || !header) {
        if (!line.empty() && line[0] != '#') header = true;
        o << line << '\n';
        continue;
      }
      auto f = SplitOn(line, '\t');
      if (f.size() >= 7 && row < m.groups.size()) f[6] = std::to_string(m.groups[row].cost);
      ++row;
      for (size_t i = 0; i < f.size(); ++i) o << (i ? "\t" : "") << f[i];
      o << '\n';
    }
  }
  {
    std::ofstream o(outDir + "/model/unk.tsv", std::ios::binary);
    o << "#! analyzer-unk\ncategory\tclass\tcost\tperchar\n";
    for (auto& c : m.cats)
      for (auto& u : c.arcs) o << c.name << '\t' << u.cls << '\t' << u.cost << '\t' << u.perChar << '\n';
  }
  return 0;
}

// ---- long units -------------------------------------------------------------------------------------
// KEYS.tsv: key, kind (n pn adn adv na phrase v adj). Keys the core reads as one token are dropped. The rest
// become long-unit arcs entered through the class of their first token and costing their own best path minus
// the bonus. A v/adj key is a stem: it takes the paradigm of the verb or adjective whose stem ends it.
int CmdLongUnits(const Args& a) {
  if (a.pos.size() < 3) return Die("longunits CORE.bin KEYS.tsv OUT.tsv");
  Analyzer an;
  if (!LoadAnalyzer(&an, a.pos[0], "", nullptr)) return 1;
  const Model& m = *an.model();
  int32_t bonus = static_cast<int32_t>(std::lround(std::atof(a.Get("--bonus", "1.0").c_str()) * 100));
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
  auto analyze = [&](const std::string& s, int32_t* cost) {
    internal::Normalize(s, &ws.nchars, &ws.edits);
    BuildLattice(m, &ws, opt);
    RunViterbi(m, &ws, false);
    int32_t inner = 0;
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
          int32_t bestG = -1, bestCost = 0;
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
              int32_t c = 0;
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
            int32_t ignored = 0;
            if (analyze(prefix, &ignored) == 0 || function(ws.arcs[ws.path.front()])) {
              ++edgeFunction;
              return;
            }
          }
          const auto& g = m.groups[static_cast<size_t>(bestG)];
          const auto& p = m.paradigms[g.paradigm];
          ++multi[kind];
          o << key << '\t' << Analyzer::PosName(m.classPos[m.pforms[p.first].cls]) << '\t' << firstCls << '\t'
            << m.pforms[p.first].cls << '\t' << (bestCost - bonus) << '\t' << m.udNames[g.ud] << "\t*\t*\t" << prefix
            << m.S(g.lemma) << "\t*\t" << g.paradigm;
        } else {
          int32_t inner = 0;
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
          o << key << '\t' << pos << '\t' << first.lcls << '\t' << last.rcls << '\t' << (inner - bonus) << '\t' << ud
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
  if (argc < 2) return Die("usage: hd8ana pack|pack-long|run|eval|bench|train|longunits ...");
  std::string cmd = argv[1];
  std::set<std::string> valued{"--slack", "-l", "--format", "--epochs", "--step", "--limit", "--bonus", "--reps", "--doc", "--diff", "--repair-tsv"};
  Args a = Parse(argc, argv, valued);
  if (cmd == "pack") return CmdPack(a);
  if (cmd == "pack-long") return CmdPackLong(a);
  if (cmd == "run") return CmdRun(a);
  if (cmd == "eval") return CmdEval(a);
  if (cmd == "bench") return CmdBench(a);
  if (cmd == "train") return CmdTrain(a);
  if (cmd == "longunits") return CmdLongUnits(a);
  if (cmd == "norm") return CmdNorm();
  if (cmd == "hints") return CmdHints(a);
  return Die("unknown command " + cmd);
}
