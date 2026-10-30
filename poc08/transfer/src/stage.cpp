// Stage T entry points (ir/stages.h). The construction matcher runs on every line and records one
// Decision per winning construction (rule cx:<id>, the facts it read, the runner-up and margin).
// Until the sense chart and the lexicon (W3) are wired in, content tokens are still emitted as Raw
// nodes with fallback evidence, as the driver stub did; the constructions' Korean node templates are
// in the decisions' because facts (ko=…) and in hd8xfer's JSON output.
#include <fstream>
#include <sstream>

#include "decide.h"
#include "ir/stages.h"
#include "transfer.h"

namespace hd08 {

// R1: how 0.7.x's own decision at a point is read as a decider class, from the facts the 0.7.x bridge
// records on its lifted decisions (t075.voice=…, t075.ko=<target>|<ending>). Rows of
// data/decide/base075.tsv, first match per family wins.
struct BaseRow {
  std::string family, source, pattern, cls;  // source: voice, marker, jlem (equal); ko, real (substring); * any
};

struct TransferData {
  xfer::Table table;
  xfer::DecideData deciders;
  std::vector<BaseRow> base;
};

namespace {

void LoadBase(const std::string& path, std::vector<BaseRow>& out) {
  std::ifstream in(path);
  std::string l;
  while (std::getline(in, l)) {
    if (l.empty() || l[0] == '#') continue;
    std::istringstream s(l);
    BaseRow r;
    std::getline(s, r.family, '\t');
    std::getline(s, r.source, '\t');
    std::getline(s, r.pattern, '\t');
    std::getline(s, r.cls, '\t');
    if (!r.cls.empty()) out.push_back(r);
  }
}

// 0.7.x's class at a point: the facts of the T decisions whose span overlaps the point's token.
// jlem is the lemma of the token before the point (the verb of a voice point).
std::string Base075(const TransferData& t, const Doc& doc, const Line& line, const std::string& family, Span at,
                    std::string_view jlem) {
  std::vector<std::string_view> voices, kos, reals, markers;
  for (const Decision& d : line.decisions) {
    if (d.stage != 'T' || !Overlaps(d.src, at)) continue;
    for (uint32_t f : d.because) {
      const std::string_view s = doc.S(f);
      if (s.starts_with("t075.voice=")) voices.push_back(s.substr(11));
      else if (s.starts_with("t075.ko=")) kos.push_back(s.substr(8));
      else if (s.starts_with("t075.real=")) reals.push_back(s.substr(10));
      else if (s.starts_with("t075.marker=")) markers.push_back(s.substr(12));
    }
  }
  if (voices.empty() && kos.empty()) return "";
  for (const BaseRow& r : t.base) {
    if (r.family != family) continue;
    if (r.source == "*") return r.cls;
    if (r.source == "jlem") {
      if (jlem == r.pattern) return r.cls;
      continue;
    }
    if (r.source == "real") {
      for (std::string_view k : reals)
        if (k.find(r.pattern) != std::string_view::npos) return r.cls;
    } else if (r.source == "marker") {
      for (std::string_view k : markers)
        if (k == r.pattern) return r.cls;
    } else if (r.source == "voice") {
      for (std::string_view v : voices)
        if (v == r.pattern) return r.cls;
    } else if (r.source == "ko") {
      for (std::string_view k : kos)
        if (r.pattern == "*" || k.find(r.pattern) != std::string_view::npos) return r.cls;
    }
  }
  return "";
}

// Writes a decider's class into the slots of the nodes over the point.
void Apply(const std::string& family, const std::string& cls, Line& line, Span at) {
  for (KNode& n : line.kir) {
    if (!Overlaps(n.src, at)) continue;
    if (family == "voice") {
      if (n.kind != KK::V) continue;
      n.passive = cls == "PASSIVE";
      n.potential = cls == "POTENTIAL";
      n.spont = cls == "SPONTANEOUS";
      if (cls == "HONORIFIC") n.ja |= jafact::sonkei;
      else n.ja &= static_cast<uint16_t>(~jafact::sonkei);
    } else if (family == "tame") {
      if (cls == "PURPOSE") n.rel = rel::purpose;
      else if (cls == "CAUSE") n.rel = rel::cause;
    } else if (family == "youni") {
      if (cls == "LIKE") n.rel = rel::like;
      else if (cls == "PURPOSE") n.rel = rel::sothat;
    } else if (family == "tokorode") {
      if (cls == "CONCESSIVE") n.rel = rel::even_try;
    }
  }
}

}  // namespace

TransferData* LoadTransfer(const Config& cfg, std::string& err) {
  auto* d = new TransferData;
  xfer::LoadTable(cfg.data + "/transfer/data", d->table);
  xfer::LoadDeciders(cfg.data + "/transfer/data", d->deciders);
  LoadBase(cfg.data + "/transfer/data/decide/base075.tsv", d->base);
  if (d->table.rows.empty()) {
    err = "transfer: no construction rows under " + cfg.data + "/transfer/data/cx";
    delete d;
    return nullptr;
  }
  return d;
}

void FreeTransfer(TransferData* data) { delete data; }

void StageT(const TransferData& t, const Config&, Doc& doc, Line& line) {
  line.kir.clear();
  xfer::LineIR view = xfer::MakeView(doc, line, t.table);
  xfer::Result r = xfer::Transfer(t.table, view);
  for (size_t i = 0; i < r.chosen.size(); ++i) {
    const xfer::Match& m = r.chosen[i];
    const xfer::Row& row = t.table.rows[m.row];
    const xfer::Decision& x = r.decisions[i];
    Decision d;
    d.stage = 'T';
    d.kind = x.kind == "default" ? Ev::Default : Ev::Cued;
    d.rule = doc.I(x.rule);
    d.src.begin = x.src.begin;
    d.src.end = x.src.end;
    d.score = static_cast<float>(x.score);
    d.margin = static_cast<float>(x.margin);
    d.nAlt = static_cast<uint8_t>(x.nAlt > 255 ? 255 : x.nAlt);
    if (!x.runnerUp.empty()) d.runnerUp = doc.I(x.runnerUp);
    for (const auto& b : x.because) d.because.push_back(doc.I(b));
    d.because.push_back(doc.I("ko=" + row.ko));
    if (row.pol != "keep") d.because.push_back(doc.I("pol=" + row.pol));
    if (x.nAlt > 1) d.contrib.emplace_back(doc.I("specificity"), static_cast<float>(x.score));
    line.Decide(std::move(d));
  }
  const uint32_t rule = doc.I("code:stub-transfer");
  for (const Token& tok : line.toks) {
    Decision d;
    d.stage = 'T';
    d.kind = Ev::Fallback;
    d.rule = rule;
    d.src = tok.src;
    KNode n;
    n.id = static_cast<uint32_t>(line.kir.size() + 1);
    n.kind = KK::Raw;
    n.lemma = doc.I(line.Src(tok.src));
    n.src = tok.src;
    n.decision = line.Decide(d);
    line.kir.push_back(n);
  }
}

// D: the deciders over the lifted kir (stages.h). One Decision per point: rule dec:<family>@<version>,
// Cued when it overrode 0.7.x, Default when it kept it; runnerUp t075:<0.7.x class>.
void StageD(const TransferData& t, const Config&, Doc& doc, Line& line) {
  if (t.deciders.byFamily.empty()) return;
  const xfer::LineIR view = xfer::MakeView(doc, line, t.table);
  for (const xfer::DecisionPoint& p : xfer::FindPoints(t.table, view)) {
    const auto dec = t.deciders.byFamily.find(p.family);
    if (dec == t.deciders.byFamily.end()) continue;
    const xfer::Tok& tok = view.toks[static_cast<size_t>(p.tok)];
    const Span at{tok.src.begin, tok.src.end};
    // A point no 0.7.x word of this line covers belongs to another sentence of it.
    bool covered = false;
    for (const Decision& x : line.decisions)
      if (x.stage == 'T' && Overlaps(x.src, at) && !doc.S(x.rule).starts_with("dec:")) covered = true;
    if (!covered) continue;
    const std::string base =
        Base075(t, doc, line, p.family, at, p.tok > 0 ? std::string_view(view.toks[static_cast<size_t>(p.tok - 1)].lemma) : "");
    const xfer::DeciderResult r = xfer::Decide(dec->second, xfer::Features(t.table, view, p), base);
    Decision d;
    d.stage = 'T';
    d.kind = r.overridden ? Ev::Cued : Ev::Default;
    d.rule = doc.I("dec:" + dec->second.id);
    d.src = at;
    d.margin = r.margin;
    d.score = r.pTop;
    d.nAlt = static_cast<uint8_t>(std::min<size_t>(dec->second.classes.size(), 255));
    d.runnerUp = doc.I("t075:" + (base.empty() ? std::string("NONE") : base));
    d.because.push_back(doc.I("class=" + r.chosen));
    if (r.overridden) d.because.push_back(doc.I("overrode=" + (base.empty() ? std::string("NONE") : base)));
    for (const xfer::Contribution& c : r.because) d.contrib.emplace_back(doc.I(c.feature), c.weight);
    line.Decide(std::move(d));
    if (r.overridden) Apply(p.family, r.chosen, line, at);
  }
}

}  // namespace hd08
