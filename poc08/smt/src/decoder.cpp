#include "hd8/smt_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include "hd8/analyzer.h"
#include "hd8/smt_ko.h"
#include "lm.h"
#include "ptable.h"

namespace hd8::smt {

const char* FeatureName(int f) {
  static const char* names[] = {"lm", "pef", "pfe", "lef", "lfe", "phrase", "word", "oov", "lat", "gloss"};
  return f >= 0 && f < kNumFeatures ? names[f] : "?";
}

struct Gloss {
  std::string ja, ko;
};

struct DecoderImpl {
  Lm lm;
  PhraseTable pt;
  ana::Analyzer analyzer;
  int64_t w[kNumFeatures] = {};
  std::vector<Gloss> gloss;
};

Decoder::Decoder() : impl_(std::make_unique<DecoderImpl>()) {}
Decoder::~Decoder() = default;

namespace {

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

bool IsSpaceAt(const std::string& s, size_t p, size_t* len) {
  if (p >= s.size()) return false;
  const unsigned char c = static_cast<unsigned char>(s[p]);
  if (c == ' ' || c == '\t') {
    *len = 1;
    return true;
  }
  if (c == 0xE3 && p + 2 < s.size() && static_cast<unsigned char>(s[p + 1]) == 0x80 &&
      static_cast<unsigned char>(s[p + 2]) == 0x80) {
    *len = 3;
    return true;
  }
  return false;
}

size_t Canon(const std::string& s, size_t p) {
  size_t l = 0;
  while (IsSpaceAt(s, p, &l)) p += l;
  return p;
}

bool AllSpace(const std::string& s) {
  size_t p = 0, l = 0;
  while (IsSpaceAt(s, p, &l)) p += l;
  return p >= s.size();
}

std::string SpaceToIdeo(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == ' ') o += "\xE3\x80\x80";
    else o += c;
  }
  return o;
}

// Japanese text (kana or CJK) that a passthrough leaves untranslated
bool HasJapanese(const std::string& s) {
  for (size_t i = 0; i + 2 < s.size() + 0 && i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xE3 && i + 1 < s.size()) {
      const unsigned char d = static_cast<unsigned char>(s[i + 1]);
      if (d >= 0x81 && d <= 0x83) return true;  // U+3040..U+30FF
    }
    if (c >= 0xE4 && c <= 0xE9) return true;  // U+4E00..U+9FFF
  }
  return false;
}

std::string Escape(const std::string& t) { return !t.empty() && (t[0] == '+' || t[0] == '\\') ? "\\" + t : t; }

int64_t ToFixedWeight(double v) { return static_cast<int64_t>(std::llround(v * 1000.0)); }

struct Edge {
  size_t b, e;
  std::string surf;
  int32_t lat;  // -margin × 10^4
  bool best;
  ana::Pos pos;
};

bool NounLike(ana::Pos p) {
  return p == ana::Pos::n || p == ana::Pos::pn || p == ana::Pos::unk || p == ana::Pos::na || p == ana::Pos::num ||
         p == ana::Pos::suf || p == ana::Pos::pref;
}

struct Option {
  size_t start, end;  // node indices
  const PhraseOpt* p;
  int32_t lat;
  bool gloss, oov;
  std::string src;
  bool rule = false;
};

struct Hyp {
  int64_t score;
  int64_t f[kNumFeatures];
  LmState st;
  int32_t back, opt;
};

struct StateHash {
  size_t operator()(const LmState& s) const { return static_cast<size_t>(NgramKey(s.ids, s.len)); }
};

}  // namespace

bool Decoder::Load(const std::string& model, const std::string& analyzer, const std::string& weights,
                   const std::string& glossary, std::string* err) {
  DecoderImpl& m = *impl_;
  if (!m.lm.Load(model + "/lm.bin", err)) return false;
  if (!m.pt.Load(model + "/phrases.tsv", m.lm, err)) return false;
  std::string snap;
  const std::string ap = analyzer.empty() ? model + "/analyzer.bin" : analyzer;
  if (!ReadFile(ap, &snap)) {
    *err = "cannot read analyzer " + ap;
    return false;
  }
  if (!m.analyzer.Load(snap, err)) return false;
  // defaults, overridden by weights.tsv (name value)
  const double def[kNumFeatures] = {1.0, 0.2, 0.2, 0.2, 0.2, -0.5, 0.0, -10.0, 0.5, 5.0};
  for (int i = 0; i < kNumFeatures; ++i) m.w[i] = ToFixedWeight(def[i]);
  const std::string wp = weights.empty() ? model + "/weights.tsv" : weights;
  std::ifstream wi(wp);
  std::string line;
  while (wi && std::getline(wi, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string name;
    double v = 0;
    if (!(ls >> name >> v)) continue;
    for (int i = 0; i < kNumFeatures; ++i)
      if (name == FeatureName(i)) m.w[i] = ToFixedWeight(v);
  }
  if (!glossary.empty()) {
    std::ifstream gi(glossary);
    if (!gi) {
      *err = "cannot open glossary " + glossary;
      return false;
    }
    while (std::getline(gi, line)) {
      if (line.empty() || line[0] == '#') continue;
      size_t t = line.find('\t');
      if (t == std::string::npos) continue;
      size_t t2 = line.find('\t', t + 1);
      m.gloss.push_back({line.substr(0, t), line.substr(t + 1, t2 == std::string::npos ? std::string::npos : t2 - t - 1)});
    }
    std::stable_sort(m.gloss.begin(), m.gloss.end(),
                     [](const Gloss& a, const Gloss& b) { return a.ja.size() > b.ja.size(); });
  }
  return true;
}

std::string Decoder::Translate(const std::string& line, const KoText& ko, const DecodeOptions& opt) const {
  const DecoderImpl& m = *impl_;
  if (AllSpace(line)) return opt.why ? std::string(line) : line;

  // ---- lattice
  ana::Options ao;
  ao.margins = opt.lattice;
  ao.altTau = opt.latTau;
  ao.longUnits = false;
  ana::Analysis an;
  auto ws = ana::NewWorkspace();
  m.analyzer.Analyze(line, ao, &an, ws.get());
  std::vector<Edge> edges;
  size_t prevEnd = Canon(line, 0);
  auto addEdge = [&](size_t b, size_t e, const std::string& surf, int32_t lat, bool best, ana::Pos pos) {
    if (surf.empty() || AllSpace(surf)) return;
    b = Canon(line, b);
    e = Canon(line, e);
    if (b >= e) return;
    for (const Edge& x : edges)
      if (x.b == b && x.e == e && x.surf == surf) return;
    edges.push_back({b, e, SpaceToIdeo(surf), lat, best, pos});
  };
  for (const auto& t : an.toks) {
    const size_t b = Canon(line, t.src.begin);
    if (b > prevEnd) addEdge(prevEnd, b, line.substr(prevEnd, b - prevEnd), 0, true, ana::Pos::unk);  // text the analyzer skipped
    addEdge(t.src.begin, t.src.end, t.surf, 0, true, t.pos);
    if (Canon(line, t.src.end) > prevEnd) prevEnd = Canon(line, t.src.end);
  }
  if (prevEnd < line.size()) addEdge(prevEnd, line.size(), line.substr(prevEnd), 0, true, ana::Pos::unk);
  if (opt.lattice)
    for (const auto& t : an.alts) {
      const float mg = std::max(0.0f, t.margin);
      addEdge(t.src.begin, t.src.end, t.surf, -static_cast<int32_t>(std::lround(mg * 1000.0f)) * 10, false, t.pos);
    }
  std::vector<size_t> pos;
  pos.push_back(Canon(line, 0));
  for (const Edge& e : edges) {
    pos.push_back(e.b);
    pos.push_back(e.e);
  }
  std::sort(pos.begin(), pos.end());
  pos.erase(std::unique(pos.begin(), pos.end()), pos.end());
  auto nodeOf = [&](size_t p) { return static_cast<size_t>(std::lower_bound(pos.begin(), pos.end(), p) - pos.begin()); };
  const size_t nNodes = pos.size();
  const size_t endNode = nodeOf(Canon(line, line.size()));
  std::vector<std::vector<size_t>> out(nNodes);
  for (size_t i = 0; i < edges.size(); ++i) out[nodeOf(edges[i].b)].push_back(i);

  // ---- forced glossary spans
  std::vector<std::pair<size_t, size_t>> forced;  // node spans
  std::deque<PhraseOpt> owned;
  std::vector<Option> opts;
  std::vector<std::string> notes;
  for (const Gloss& g : m.gloss) {
    for (size_t at = line.find(g.ja); at != std::string::npos; at = line.find(g.ja, at + 1)) {
      const size_t b = Canon(line, at), e = Canon(line, at + g.ja.size());
      if (!std::binary_search(pos.begin(), pos.end(), b) || !std::binary_search(pos.begin(), pos.end(), e)) {
        notes.push_back("gloss-misaligned " + g.ja);
        continue;
      }
      const size_t nb = nodeOf(b), ne = nodeOf(e);
      bool clash = false;
      for (auto& f : forced) clash |= nb < f.second && ne > f.first;
      if (clash) continue;
      forced.push_back({nb, ne});
      PhraseOpt p;
      for (const std::string& t : SplitTokens(g.ko)) {
        p.tgt += (p.tgt.empty() ? "" : " ") + Escape(t);
        p.ids.push_back(m.lm.Id(Escape(t)));
      }
      p.ntok = static_cast<uint16_t>(p.ids.size());
      p.count = 0;
      owned.push_back(std::move(p));
      opts.push_back({nb, ne, &owned.back(), 0, true, false, g.ja});
    }
  }
  auto blocked = [&](size_t s, size_t e) {
    for (auto& f : forced)
      if (s < f.second && e > f.first && !(s == f.first && e == f.second)) return true;
    return false;
  };

  // ---- phrase options: paths of up to maxLen edges from every node
  std::map<std::pair<size_t, size_t>, int> perSpan;
  struct Frame {
    size_t node;
    std::string key, src;
    int len;
    int32_t lat;
  };
  for (size_t s = 0; s < nNodes; ++s) {
    std::vector<Frame> stack{{s, "", "", 0, 0}};
    while (!stack.empty()) {
      Frame fr = stack.back();
      stack.pop_back();
      if (fr.len >= m.pt.maxLen()) continue;
      for (size_t ei : out[fr.node]) {
        const Edge& e = edges[ei];
        Frame nx{nodeOf(e.e), fr.key.empty() ? e.surf : fr.key + " " + e.surf, fr.src + e.surf, fr.len + 1, fr.lat + e.lat};
        const auto* found = m.pt.Find(nx.key);
        if (found && !blocked(s, nx.node)) {
          int& cnt = perSpan[{s, nx.node}];
          for (const PhraseOpt& p : *found) {
            if (cnt >= opt.maxOpts) break;
            opts.push_back({s, nx.node, &p, nx.lat, false, false, nx.src});
            ++cnt;
          }
        }
        if (found || nx.len == 1) stack.push_back(nx);  // keep extending (prefixes of longer phrases)
        else if (nx.len < m.pt.maxLen()) stack.push_back(nx);
      }
    }
  }
  // passthrough for edges without a one-edge option
  for (const Edge& e : edges) {
    const size_t s = nodeOf(e.b), t = nodeOf(e.e);
    if (blocked(s, t) || m.pt.Find(e.surf)) continue;
    PhraseOpt p;
    bool rule = false;
    if (opt.oov && HasJapanese(e.surf) && NounLike(e.pos)) {
      const std::string kw = opt.oov(e.surf);
      if (!kw.empty() && !HasJapanese(kw)) {
        for (const std::string& t2 : SplitTokens(kw)) {
          p.tgt += (p.tgt.empty() ? "" : " ") + Escape(t2);
          p.ids.push_back(m.lm.Id(Escape(t2)));
        }
        rule = !p.ids.empty();
      }
    }
    if (!rule) {
      p.tgt = Escape(e.surf);
      p.ids.assign(1, m.lm.Id(p.tgt));
    }
    p.ntok = static_cast<uint16_t>(p.ids.size());
    owned.push_back(std::move(p));
    opts.push_back({s, t, &owned.back(), e.lat, false, !rule && HasJapanese(e.surf), e.surf, rule});
  }
  std::vector<std::vector<size_t>> optAt(nNodes);
  for (size_t i = 0; i < opts.size(); ++i) optAt[opts[i].start].push_back(i);

  auto staticFeat = [&](const Option& o, int64_t* f) {
    for (int i = 0; i < kNumFeatures; ++i) f[i] = 0;
    f[kPef] = o.p->pef;
    f[kPfe] = o.p->pfe;
    f[kLef] = o.p->lef;
    f[kLfe] = o.p->lfe;
    f[kPhrase] = -10000;
    f[kWord] = -10000 * static_cast<int64_t>(o.p->ntok);
    f[kOov] = o.oov ? -10000 : 0;
    f[kLat] = o.lat;
    f[kGloss] = o.gloss ? 10000 : 0;
  };

  // ---- search
  std::vector<Hyp> arena;
  arena.reserve(4096);
  std::vector<std::vector<int32_t>> stacks(nNodes);
  std::vector<std::unordered_map<LmState, int32_t, StateHash>> recomb(nNodes);
  {
    Hyp h{};
    h.st = m.lm.Begin();
    h.back = -1;
    h.opt = -1;
    arena.push_back(h);
    stacks[nodeOf(Canon(line, 0))].push_back(0);
  }
  auto better = [&](int32_t a, int32_t b) {
    return arena[a].score != arena[b].score ? arena[a].score > arena[b].score : a < b;
  };
  for (size_t n = 0; n < nNodes; ++n) {
    auto& st = stacks[n];
    if (st.empty() || n == endNode) continue;
    std::sort(st.begin(), st.end(), better);
    if (st.size() > static_cast<size_t>(opt.beam)) st.resize(opt.beam);
    for (int32_t hi : st) {
      for (size_t oi : optAt[n]) {
        const Option& o = opts[oi];
        Hyp nh;
        int64_t sf[kNumFeatures];
        staticFeat(o, sf);
        for (int i = 0; i < kNumFeatures; ++i) nh.f[i] = arena[hi].f[i] + sf[i];
        LmState s = arena[hi].st;
        int64_t lmd = 0;
        for (uint32_t id : o.p->ids) {
          LmState ns;
          lmd += m.lm.Score(s, id, &ns);
          s = ns;
        }
        if (o.end == endNode) {
          LmState ns;
          lmd += m.lm.Score(s, Lm::kEos, &ns);
          s = ns;
        }
        nh.f[kLm] += lmd;
        int64_t score = 0;
        for (int i = 0; i < kNumFeatures; ++i) score += m.w[i] * nh.f[i];
        nh.score = score;
        nh.st = s;
        nh.back = hi;
        nh.opt = static_cast<int32_t>(oi);
        auto& rc = recomb[o.end];
        auto it = rc.find(s);
        if (it != rc.end()) {
          if (score > arena[it->second].score) arena[it->second] = nh;
          continue;
        }
        arena.push_back(nh);
        const int32_t idx = static_cast<int32_t>(arena.size() - 1);
        rc.emplace(s, idx);
        stacks[o.end].push_back(idx);
      }
    }
  }
  auto& fin = stacks[endNode];
  if (fin.empty()) return line;  // unreachable end: should not happen (passthrough covers every edge)
  std::sort(fin.begin(), fin.end(), better);

  auto backtrack = [&](int32_t h) {
    std::vector<int32_t> path;
    for (int32_t x = h; x > 0 && arena[x].opt >= 0; x = arena[x].back) path.push_back(x);
    std::reverse(path.begin(), path.end());
    return path;
  };
  auto tokensOf = [&](int32_t h) {
    std::string t;
    for (int32_t x : backtrack(h)) {
      const std::string& g = opts[arena[x].opt].p->tgt;
      if (g.empty()) continue;
      t += (t.empty() ? "" : " ") + g;
    }
    return t;
  };
  const std::string toks = tokensOf(fin[0]);
  std::string res = opt.tokens || !opt.join ? toks : ko.Join(SplitTokens(toks), opt.level, opt.quoteLevel.empty() ? opt.level : opt.quoteLevel);
  if (opt.why) {
    std::ostringstream w;
    w << "\n# score " << arena[fin[0]].score;
    for (int i = 0; i < kNumFeatures; ++i) w << ' ' << FeatureName(i) << '=' << arena[fin[0]].f[i];
    for (int32_t x : backtrack(fin[0])) {
      const Option& o = opts[arena[x].opt];
      w << "\n#   " << o.src << " => " << o.p->tgt << "  [pef " << o.p->pef << " pfe " << o.p->pfe << " lef "
        << o.p->lef << " lfe " << o.p->lfe << " n " << o.p->count << " lat " << o.lat << "] "
        << (o.gloss ? "user:glossary" : o.rule ? "rule:oov" : o.oov ? "passthrough:oov" : o.p->count == 0 ? "passthrough" : m.pt.Prov(o.p->prov));
    }
    for (const auto& n : notes) w << "\n# note " << n;
    res += w.str();
  }
  if (opt.nbest > 0) {
    std::ostringstream w;
    int k = 0;
    std::vector<std::string> seen;
    for (int32_t h : fin) {
      if (k >= opt.nbest) break;
      std::string t = tokensOf(h);
      if (std::find(seen.begin(), seen.end(), t) != seen.end()) continue;
      seen.push_back(t);
      w << "\n" << k++ << " ||| " << t << " |||";
      for (int i = 0; i < kNumFeatures; ++i) w << ' ' << arena[h].f[i];
      w << " ||| " << ko.Join(SplitTokens(t), opt.level, opt.quoteLevel.empty() ? opt.level : opt.quoteLevel);
    }
    w << "\n<<<";
    res += w.str();
  }
  return res;
}

}  // namespace hd8::smt
