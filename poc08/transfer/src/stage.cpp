// Stage T entry points (ir/stages.h). The construction matcher runs on every line and records one
// Decision per winning construction (rule cx:<id>, the facts it read, the runner-up and margin).
// Until the sense chart and the lexicon (W3) are wired in, content tokens are still emitted as Raw
// nodes with fallback evidence, as the driver stub did; the constructions' Korean node templates are
// in the decisions' because facts (ko=…) and in hd8xfer's JSON output.
#include "ir/stages.h"
#include "transfer.h"

namespace hd08 {

struct TransferData {
  xfer::Table table;
};

TransferData* LoadTransfer(const Config& cfg, std::string& err) {
  auto* d = new TransferData;
  xfer::LoadTable(cfg.data + "/transfer/data", d->table);
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

}  // namespace hd08
