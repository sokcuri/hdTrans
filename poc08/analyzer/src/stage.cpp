// N and A stages of the 0.8 pipeline (ir/stages.h) on top of hd8::ana::Analyzer.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "hd8/analyzer.h"
#include "hd8/stage_repair.h"
#include "ir/stages.h"
#include "lattice.h"
#include "model.h"
#include "normalize.h"
#include "utf8.h"

namespace hd08 {

struct AnalyzerData {
  hd8::ana::Analyzer analyzer;
  bool repair = false;  // EnableRepair: StageA reads noisy spans again (ja/repair.tsv)
};

namespace {

bool ReadAll(const std::string& path, std::string* out) {
  // UTF-8 paths on every platform.
  std::ifstream in(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

uint8_t EditOf(hd8::ana::EditKind k) {
  switch (k) {
    case hd8::ana::EditKind::width: return edit::width;
    case hd8::ana::EditKind::join: return edit::join;
    case hd8::ana::EditKind::ruby: return edit::ruby;
  }
  return edit::width;
}

Token Convert(const hd8::ana::Analyzer& an, Doc& doc, const hd8::ana::Token& t) {
  Token o;
  o.src = {t.src.begin, t.src.end};
  o.surf = doc.I(t.surf);
  o.lemma = doc.I(t.lemma);
  o.reading = doc.I(t.reading);
  o.pos = static_cast<Pos>(t.pos);
  o.ud = static_cast<uint16_t>(doc.ud.Intern(an.UdName(t.ud)));
  o.ctype = static_cast<uint8_t>(doc.ctypes.Intern(an.CtypeName(t.ctype)));
  o.cform = static_cast<uint8_t>(doc.cforms.Intern(an.CformName(t.cform)));
  o.flags = t.flags;
  o.cost = t.cost;
  o.margin = t.margin;
  o.alt = t.alt;
  o.nAlt = t.nAlt;
  if (!t.derive.empty()) o.derive = doc.I(t.derive);
  if (!t.removedBy.empty()) o.removedBy = doc.I(t.removedBy);
  if (!t.repair.empty()) o.repair = doc.I(t.repair);  // a repaired reading: "rp.dakuten か→が"
  return o;
}

hd8::ana::Workspace* ThreadWorkspace() {
  thread_local hd8::ana::WorkspacePtr ws = hd8::ana::NewWorkspace();
  return ws.get();
}

}  // namespace

AnalyzerData* LoadAnalyzer(const Config& cfg, std::string& err) {
  auto* d = new AnalyzerData;
  std::string dir = cfg.data + "/analyzer/data";
  std::string bytes;
  if (ReadAll(dir + "/analyzer.bin", &bytes)) {
    if (!d->analyzer.Load(bytes, &err)) {
      delete d;
      return nullptr;
    }
  } else {
    hd8::ana::Model m;
    if (!hd8::ana::LoadTsvDir(dir, &m, &err)) {
      delete d;
      return nullptr;
    }
    hd8::ana::Finalize(&m);
    if (!d->analyzer.Load(hd8::ana::Serialize(m), &err)) {
      delete d;
      return nullptr;
    }
  }
  std::string lb;
  if (ReadAll(dir + "/long.bin", &lb) && !d->analyzer.LoadOverlay(lb, &err)) {
    delete d;
    return nullptr;
  }
  std::string rp;
  if (ReadAll(dir + "/ja/repair.tsv", &rp) && !d->analyzer.LoadRepair(rp, &err)) {
    err = "analyzer: " + err;
    delete d;
    return nullptr;
  }
  return d;
}

bool EnableRepair(AnalyzerData* data) {
  if (!data || !data->analyzer.HasRepair()) return false;
  data->repair = true;
  return true;
}

bool RepairEnabled(const AnalyzerData& data) { return data.repair; }

bool RepairText(const AnalyzerData& data, std::string_view src, std::string* out, std::string* why) {
  if (!data.repair) return false;
  hd8::ana::Analysis r;
  hd8::ana::Options opt;
  opt.margins = false;
  opt.repair = true;
  opt.why = why != nullptr;
  data.analyzer.Analyze(src, opt, &r, ThreadWorkspace());
  if (r.repairs.empty()) return false;
  *out = std::move(r.repaired);
  if (why) {
    *why += r.checks;
    for (auto& x : r.repairs)
      *why += "  repair " + x.op + " " + (x.from.empty() ? std::string("∅") : x.from) + "→" +
              (x.to.empty() ? std::string("∅") : x.to) + " on " + r.toks[x.tok].surf + "\n";
  }
  return true;
}

void FreeAnalyzer(AnalyzerData* data) { delete data; }

void StageN(const AnalyzerData&, const Config&, Doc& doc, Line& line) {
  std::vector<hd8::ana::internal::NChar> nchars;
  std::vector<hd8::ana::NormEdit> edits;
  hd8::ana::internal::Normalize(line.src, &nchars, &edits);
  line.norm.clear();
  line.map.clear();
  for (auto& nc : nchars) {
    size_t before = line.norm.size();
    hd8::ana::internal::AppendUtf8(line.norm, nc.c);
    for (size_t k = before; k < line.norm.size(); ++k) line.map.push_back(nc.sb);
  }
  line.map.push_back(static_cast<uint32_t>(line.src.size()));
  line.edits.clear();
  for (auto& e : edits) line.edits.push_back({{e.src.begin, e.src.end}, doc.I(e.from), doc.I(e.to), EditOf(e.kind)});
}

void StageA(const AnalyzerData& a, const Config&, Doc& doc, Line& line, const std::vector<ForcedSpan>&) {
  hd8::ana::Analysis r;
  hd8::ana::Options opt;
  opt.repair = a.repair;
  a.analyzer.Analyze(line.src, opt, &r, ThreadWorkspace());
  line.toks.clear();
  line.alts.clear();
  line.alt.toks.clear();
  for (auto& t : r.toks) {
    line.toks.push_back(Convert(a.analyzer, doc, t));
    // unknown kanji or hiragana words are guesses; katakana, Latin and digit runs are transliterated
    if (t.flags & hd8::ana::Unknown) {
      char32_t c = 0;
      size_t i = 0;
      if (!t.surf.empty()) c = hd8::ana::internal::DecodeUtf8(t.surf, i);
      if (hd8::ana::internal::IsKanji(c) || hd8::ana::internal::IsHiragana(c)) {
        line.toks.back().flags |= tokflag::Fallback;
        Flag f;
        f.kind = flag::fallback;
        f.src = line.toks.back().src;
        line.flags.push_back(f);
      }
    }
  }
  for (auto& t : r.alts) line.alts.push_back(Convert(a.analyzer, doc, t));
  for (auto& t : r.alt) line.alt.toks.push_back(Convert(a.analyzer, doc, t));
  // joins across spaces are decided here, so they are recorded with the layout edits
  for (auto& e : r.edits)
    if (e.kind == hd8::ana::EditKind::join)
      line.edits.push_back({{e.src.begin, e.src.end}, doc.I(e.from), doc.I(e.to), edit::join});
}

}  // namespace hd08
