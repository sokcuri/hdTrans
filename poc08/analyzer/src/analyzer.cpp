#include <algorithm>
#include <cstdio>

#include "hd8/analyzer.h"
#include "lattice.h"
#include "repair.h"
#include "utf8.h"

namespace hd8::ana {

using internal::AppendUtf8;
using internal::IsHiragana;
using internal::IsKatakana;
using internal::ToKatakana;

void WorkspaceDeleter::operator()(Workspace* ws) const { delete ws; }
WorkspacePtr NewWorkspace() { return WorkspacePtr(new Workspace); }

Analyzer::Analyzer() = default;
Analyzer::~Analyzer() = default;

bool Analyzer::Load(std::string_view snapshot, std::string* err) {
  auto m = std::make_unique<Model>();
  if (!Deserialize(snapshot, m.get(), err)) return false;
  model_ = std::move(m);
  return true;
}

bool Analyzer::LoadRepair(std::string_view tsv, std::string* err) {
  auto r = std::make_unique<RepairRules>();
  if (!ParseRepairTsv(tsv, r.get(), err)) return false;
  repair_ = std::move(r);
  return true;
}

bool Analyzer::SetRepairCore(std::string_view keys) {
  if (!repair_ || !model_) return false;
  SetCoreKeys(*model_, keys, repair_.get());
  return true;
}

bool Analyzer::LoadOverlay(std::string_view overlay, std::string* err) {
  if (!model_) {
    *err = "no snapshot loaded";
    return false;
  }
  return DeserializeLongs(overlay, model_.get(), err);
}

std::string_view Analyzer::UdName(uint16_t ud) const {
  return model_ && ud < model_->udNames.size() ? std::string_view(model_->udNames[ud]) : std::string_view();
}
std::string_view Analyzer::CtypeName(uint8_t c) const {
  return model_ && c < model_->ctypeNames.size() ? std::string_view(model_->ctypeNames[c]) : std::string_view();
}
std::string_view Analyzer::CformName(uint8_t c) const {
  return model_ && c < model_->cformNames.size() ? std::string_view(model_->cformNames[c]) : std::string_view();
}

std::string_view Analyzer::PosName(Pos p) {
  static constexpr std::string_view kNames[] = {"n", "pn", "num", "suf", "pref", "v1", "v5", "vs", "vk", "vz",
                                                "adj", "na", "adv", "adn", "conj", "intj", "aux", "p_case",
                                                "p_topic", "p_conj", "p_final", "p_adv", "sym", "unk"};
  auto i = static_cast<size_t>(p);
  return i < std::size(kNames) ? kNames[i] : "unk";
}

std::string_view Analyzer::FlagName(uint16_t bit) {
  switch (bit) {
    case Unknown: return "unknown";
    case Fallback: return "fallback";
    case LongUnit: return "long-unit";
    case UserArc: return "user-arc";
    case NameCand: return "name-cand";
    case Spoken: return "spoken";
    case Elongated: return "elongated";
    case SpaceBefore: return "space-before";
    case SinoReading: return "sino-reading";
    case Repaired: return "repaired";
    default: return "";
  }
}

std::string_view Analyzer::EditName(EditKind k) {
  switch (k) {
    case EditKind::width: return "width";
    case EditKind::join: return "join";
    case EditKind::ruby: return "ruby";
  }
  return "";
}

namespace {

bool AllKana(std::u32string_view s) {
  for (char32_t c : s)
    if (!IsHiragana(c) && !IsKatakana(c)) return false;
  return !s.empty();
}

std::string Katakana(std::u32string_view s) {
  std::string out;
  for (char32_t c : s) AppendUtf8(out, ToKatakana(c));
  return out;
}

struct Emitter {
  const Model& m;
  const Workspace& ws;
  Analysis* out;

  std::u32string_view Core(uint32_t b, uint32_t e) const {
    return std::u32string_view(ws.core.data() + b, e - b);
  }

  Span SrcOf(uint32_t b, uint32_t e) const {
    return {ws.nchars[ws.coreN[b]].sb, ws.nchars[ws.coreN[e - 1]].se};
  }

  Token Make(const Arc& a) const {
    Token t;
    t.src = SrcOf(a.b, a.e);
    auto surf = Core(a.b, a.e);
    for (char32_t c : surf) AppendUtf8(t.surf, c);
    t.flags = a.flags;
    t.cost = static_cast<float>(a.cost) / 100.0f;
    if (a.rule >= 0) t.derive = m.rules[static_cast<size_t>(a.rule)].id;
    if (a.removedBy >= 0) t.removedBy = m.constraints[static_cast<size_t>(a.removedBy)].id;
    switch (a.kind) {
      case ArcKind::lex: {
        const auto& g = m.groups[a.ref];
        const auto& pf = m.pforms[a.form];
        t.cls = pf.cls;
        t.lemma = std::string(m.S(g.lemma));
        if (a.rule >= 0 && AllKana(surf)) {
          t.reading = Katakana(surf);
        } else if (g.readKind == 0) {
          t.reading = Katakana(m.C(g.stem, g.stemLen));
          t.reading += m.S(pf.read);
        } else if (g.readKind == 1) {
          t.reading = std::string(m.S(g.read));
          t.reading += m.S(pf.read);
        }
        break;
      }
      case ArcKind::unk:
        t.cls = a.lcls;
        t.lemma = t.surf;
        break;
      case ArcKind::longUnit: {
        const auto& l = m.longs[a.ref];
        t.cls = a.rcls;
        t.lemma = std::string(m.S(l.lemma));
        t.reading = std::string(m.S(l.read));
        if (t.reading.empty()) {
          std::vector<uint32_t> parts;
          SubPath(m, ws, a.b, a.e, 0, 0, &parts);
          for (uint32_t pi : parts)
            if (ws.arcs[pi].kind != ArcKind::longUnit) t.reading += Make(ws.arcs[pi]).reading;
        }
        if (l.paradigm != UINT32_MAX) {
          const auto& c = m.classes[a.rcls];
          t.ud = c.ud;
          t.ctype = c.ctype;
          t.cform = c.cform;
          t.pos = m.classPos[a.rcls];
          return t;
        }
        t.ud = l.ud;
        t.ctype = l.ctype;
        t.cform = l.cform;
        t.pos = l.pos;
        return t;
      }
      case ArcKind::stutter:
        t.cls = a.lcls;
        t.lemma = t.surf;
        t.reading = Katakana(surf);
        break;
      case ArcKind::repair: {
        const auto& ra = ws.rarcs[a.ref];
        const auto& g = m.groups[ra.group];
        const auto& pf = m.pforms[ra.form];
        t.surf.clear();
        for (char32_t c : ra.text) AppendUtf8(t.surf, c);
        t.cls = pf.cls;
        t.lemma = std::string(m.S(g.lemma));
        if (g.readKind == 0) {
          t.reading = Katakana(m.C(g.stem, g.stemLen));
          t.reading += m.S(pf.read);
        } else if (g.readKind == 1) {
          t.reading = std::string(m.S(g.read));
          t.reading += m.S(pf.read);
        }
        break;
      }
    }
    const auto& c = m.classes[t.cls];
    t.ud = c.ud;
    t.ctype = c.ctype;
    t.cform = c.cform;
    t.pos = m.classPos[t.cls];
    return t;
  }

  // Path → tokens with spaces; joined arcs record a join edit.
  // tokPath (optional) receives the path index of each token.
  void Emit(const std::vector<uint32_t>& path, std::vector<Token>* toks, bool edits,
            const Options& opt, Workspace* mws, std::vector<uint32_t>* tokPath = nullptr) const {
    toks->clear();
    if (tokPath) tokPath->clear();
    for (size_t pi = 0; pi < path.size(); ++pi) {
      const Arc& a = ws.arcs[path[pi]];
      bool spaced = a.b > 0 && ws.spaceBefore[a.b];
      if (a.kind == ArcKind::longUnit && opt.splitLongUnits) {
        uint16_t sc = pi == 0 ? 0 : ws.arcs[path[pi - 1]].rcls;
        uint16_t ec = pi + 1 < path.size() ? ws.arcs[path[pi + 1]].lcls : 0;
        std::vector<uint32_t> parts;
        SubPath(m, *mws, a.b, a.e, sc, ec, &parts);
        for (size_t k = 0; k < parts.size(); ++k) {
          const Arc& p = ws.arcs[parts[k]];
          Token t = Make(p);
          if (k == 0 && spaced) t.flags |= SpaceBefore;
          toks->push_back(std::move(t));
          if (tokPath) tokPath->push_back(static_cast<uint32_t>(pi));
        }
      } else {
        Token t = Make(a);
        if (spaced) t.flags |= SpaceBefore;
        if (a.joined && edits) {
          NormEdit e;
          e.src = t.src;
          for (uint32_t x = ws.coreN[a.b]; x <= ws.coreN[a.e - 1]; ++x) AppendUtf8(e.from, ws.nchars[x].c);
          e.to = t.surf;
          e.kind = EditKind::join;
          out->edits.push_back(std::move(e));
        }
        toks->push_back(std::move(t));
        if (tokPath) tokPath->push_back(static_cast<uint32_t>(pi));
      }
    }
  }
};

// Analysis::repairs and ::repaired from the repaired words of the path; tokens get the Repaired flag and
// their edits.
void RecordRepairs(const Model& m, const RepairRules& R, const Workspace& ws, std::string_view src,
                   const std::vector<uint32_t>& tokPath, Analysis* out) {
  (void)m;
  auto byteB = [&](uint32_t k) { return ws.nchars[ws.coreN[k]].sb; };
  auto byteE = [&](uint32_t k) { return ws.nchars[ws.coreN[k]].se; };
  for (size_t ti = 0; ti < out->toks.size(); ++ti) {
    const Arc& a = ws.arcs[ws.path[tokPath[ti]]];
    if (a.kind != ArcKind::repair) continue;
    if (ti > 0 && tokPath[ti - 1] == tokPath[ti]) continue;
    const auto& ra = ws.rarcs[a.ref];
    Token& t = out->toks[ti];
    t.flags |= Repaired;
    bool foldCharged = false, swapCharged = false;
    for (size_t k = 0; k < ra.edits.size(); ++k) {
      const RepairEdit& e = ra.edits[k];
      Repair r;
      if (e.spaceOnly) {
        // only the space characters: a ruby reading the normaliser took out stays in the text
        uint32_t first = ws.coreN[e.pos - 1u] + 1, last = ws.coreN[e.pos];
        while (first < last && !internal::IsSpace(ws.nchars[first].c)) ++first;
        uint32_t end = first;
        while (end < last && internal::IsSpace(ws.nchars[end].c)) ++end;
        r.src = first < end ? Span{ws.nchars[first].sb, ws.nchars[end - 1].se} : Span{byteB(e.pos), byteB(e.pos)};
      } else if (e.len == 0) {
        uint32_t at = e.pos > 0 ? byteE(e.pos - 1u) : byteB(e.pos);  // an insertion sits right after the letter before
        r.src = {at, at};
      } else {
        r.src = {byteB(e.pos), byteE(e.pos + e.len - 1u)};
      }
      // the cost charged: per letter, a run of drops once, a katakana fold and a swap once per word
      int32_t cost = R.ops[static_cast<size_t>(e.op)].cost;
      if (e.op == RepairOp::kataFold) {
        cost = foldCharged ? 0 : cost;
        foldCharged = true;
      } else if (e.op == RepairOp::swap) {
        cost = swapCharged ? 0 : cost;
        swapCharged = true;
      }
      // a run of one operation over adjacent letters is one repair
      if (!out->repairs.empty() && out->repairs.back().tok == static_cast<uint16_t>(ti) &&
          out->repairs.back().src.end == r.src.begin && out->repairs.back().op == RepairOpId(R, e.op) && !e.spaceOnly) {
        auto& b = out->repairs.back();
        b.src.end = r.src.end;
        b.from = std::string(src.substr(b.src.begin, b.src.end - b.src.begin));
        for (char32_t c : e.to) AppendUtf8(b.to, c);
        if (e.op == RepairOp::dakuten || e.op == RepairOp::smallBig || e.op == RepairOp::edit1)
          b.cost = static_cast<uint16_t>(b.cost + cost);
        continue;
      }
      r.from = std::string(src.substr(r.src.begin, r.src.end - r.src.begin));
      for (char32_t c : e.to) AppendUtf8(r.to, c);
      r.op = std::string(RepairOpId(R, e.op));
      r.cost = static_cast<uint16_t>(cost);
      r.tok = static_cast<uint16_t>(ti);
      out->repairs.push_back(std::move(r));
      if (e.spaceOnly && e.pos == a.b) t.flags = static_cast<uint16_t>(t.flags & ~SpaceBefore);  // it took the space in
    }
  }
  std::stable_sort(out->repairs.begin(), out->repairs.end(), [](const Repair& x, const Repair& y) {
    return x.src.begin != y.src.begin ? x.src.begin < y.src.begin : x.src.end < y.src.end;
  });
  size_t at = 0;
  for (const Repair& r : out->repairs) {
    if (r.src.begin < at) continue;
    out->repaired.append(src.substr(at, r.src.begin - at));
    out->repaired += r.to;
    at = r.src.end;
  }
  out->repaired.append(src.substr(at));
  for (const Repair& r : out->repairs) {
    Token& t = out->toks[r.tok];
    if (!t.repair.empty()) t.repair += "; ";
    t.repair += r.op + " " + (r.from.empty() ? std::string("∅") : r.from) + "→" + (r.to.empty() ? std::string("∅") : r.to);
  }
}

}  // namespace

void Analyzer::Analyze(std::string_view src, const Options& opt, Analysis* out, Workspace* wsIn) const {
  WorkspacePtr own;
  Workspace* ws = wsIn;
  if (!ws) {
    own = NewWorkspace();
    ws = own.get();
  }
  out->norm.clear();
  out->map.clear();
  out->toks.clear();
  out->alts.clear();
  out->alt.clear();
  out->altMargin = 0;
  out->repairs.clear();
  out->repaired.clear();
  out->checks.clear();
  if (!model_) return;
  const Model& m = *model_;
  internal::Normalize(src, &ws->nchars, &ws->edits);
  out->edits = ws->edits;
  for (auto& nc : ws->nchars) {
    size_t before = out->norm.size();
    AppendUtf8(out->norm, nc.c);
    for (size_t k = before; k < out->norm.size(); ++k) out->map.push_back(nc.sb);
  }
  out->map.push_back(static_cast<uint32_t>(src.size()));

  BuildLattice(m, ws, opt);
  RunViterbi(m, ws, opt.margins);
  ws->rarcs.clear();
  out->repairs.clear();
  out->repaired.clear();
  out->checks.clear();
  bool repaired = opt.repair && repair_ && RepairPath(m, *repair_, ws, opt, opt.why ? &out->checks : nullptr);
  Emitter em{m, *ws, out};
  std::vector<uint32_t> tokPath;
  em.Emit(ws->path, &out->toks, true, opt, ws, &tokPath);
  if (repaired) {
    RecordRepairs(m, *repair_, *ws, src, tokPath, out);
    return;  // margins and path 1 belong to the unrepaired lattice
  }
  if (!opt.margins || ws->path.empty()) return;

  // margins, same-span alternatives and path 1
  const auto& A = ws->arcs;
  int32_t tau = static_cast<int32_t>(opt.altTau * 100.0f);
  int32_t minMargin = kInf;
  int32_t minArc = -1;
  size_t ti = 0;
  for (size_t pi = 0; pi < ws->path.size(); ++pi) {
    const Arc& x = A[ws->path[pi]];
    int32_t margin = kInf, arg = -1;
    while (ti < tokPath.size() && tokPath[ti] < pi) ++ti;
    Token* tok = ti < tokPath.size() && tokPath[ti] == pi ? &out->toks[ti] : nullptr;
    uint16_t altFirst = static_cast<uint16_t>(out->alts.size());
    for (uint32_t yi = 0; yi < A.size(); ++yi) {
      const Arc& y = A[yi];
      if (yi == ws->path[pi] || y.b >= x.e || y.e <= x.b) continue;
      if (y.removedBy >= 0) {
        if (y.b == x.b && y.e == x.e && tok) {
          Token t = em.Make(y);
          t.margin = -1;
          out->alts.push_back(std::move(t));
        }
        continue;
      }
      if (y.fwd >= kInf || y.bwd >= kInf) continue;
      int32_t pc = y.fwd + y.bwd - ws->best;
      if (pc < margin) {
        margin = pc;
        arg = static_cast<int32_t>(yi);
      }
      if (y.b == x.b && y.e == x.e && pc < tau && tok) {
        Token t = em.Make(y);
        t.margin = static_cast<float>(pc) / 100.0f;
        out->alts.push_back(std::move(t));
      }
    }
    if (tok) {
      tok->alt = altFirst;
      tok->nAlt = static_cast<uint16_t>(out->alts.size() - altFirst);
      for (size_t k = ti; k < tokPath.size() && tokPath[k] == pi; ++k)
        out->toks[k].margin = margin >= kInf ? 99.0f : static_cast<float>(margin) / 100.0f;
    }
    if (margin < minMargin) {
      minMargin = margin;
      minArc = arg;
    }
  }
  if (minArc >= 0 && minMargin < tau) {
    std::vector<uint32_t> p1;
    for (int32_t a = minArc; a >= 0; a = A[static_cast<size_t>(a)].bp) p1.push_back(static_cast<uint32_t>(a));
    std::reverse(p1.begin(), p1.end());
    for (int32_t a = A[static_cast<size_t>(minArc)].np; a >= 0; a = A[static_cast<size_t>(a)].np)
      p1.push_back(static_cast<uint32_t>(a));
    em.Emit(p1, &out->alt, false, opt, ws);
    out->altMargin = static_cast<float>(minMargin) / 100.0f;
  }
}

// ---- segmentation hints ----------------------------------------------------------------------------
namespace {

uint8_t ClassCode(const Model& m, uint16_t cls) {
  if (m.udNames[m.classes[cls].ud].find("助動詞語幹") != std::string::npos) return 'x';
  switch (m.classPos[cls]) {
    case Pos::n: case Pos::pn: case Pos::num: return 'n';
    case Pos::v1: case Pos::v5: case Pos::vs: case Pos::vk: case Pos::vz: return 'v';
    case Pos::adj: return 'a';
    case Pos::na: return 'k';
    case Pos::adv: case Pos::conj: case Pos::intj: return 'd';
    case Pos::adn: return 'm';
    case Pos::aux: return 'x';
    case Pos::p_case: case Pos::p_topic: case Pos::p_conj: case Pos::p_final: case Pos::p_adv: return 'p';
    case Pos::pref: return 'f';
    case Pos::suf: return 'z';
    case Pos::sym: return 's';
    default: return 'u';
  }
}

}  // namespace

void Analyzer::Hints(std::string_view text, SegHints* out, Workspace* wsIn, float slackNats) const {
  WorkspacePtr own;
  Workspace* ws = wsIn;
  if (!ws) {
    own = NewWorkspace();
    ws = own.get();
  }
  out->ends.assign(text.size() + 1, 0);
  out->rejected.clear();
  out->respell.clear();
  out->ends[0] = 'u';
  out->ends[text.size()] = 'e';
  if (!model_) return;
  const Model& m = *model_;
  internal::Normalize(text, &ws->nchars, &ws->edits);
  Options opt;
  opt.margins = false;
  BuildLattice(m, ws, opt);
  RunViterbi(m, ws, true);
  const int32_t slack = static_cast<int32_t>(slackNats * 100.0f);
  auto through = [&](const Arc& a) {
    return a.fwd >= kInf || a.bwd >= kInf ? kInf : a.fwd + a.bwd - ws->best;
  };
  // short-unit path: long units are replaced by their parts
  std::vector<uint32_t> path, parts;
  for (size_t pi = 0; pi < ws->path.size(); ++pi) {
    const Arc& a = ws->arcs[ws->path[pi]];
    if (a.kind != ArcKind::longUnit) {
      path.push_back(ws->path[pi]);
      continue;
    }
    uint16_t sc = pi == 0 ? 0 : ws->arcs[ws->path[pi - 1]].rcls;
    uint16_t ec = pi + 1 < ws->path.size() ? ws->arcs[ws->path[pi + 1]].lcls : 0;
    SubPath(m, *ws, a.b, a.e, sc, ec, &parts);
    path.insert(path.end(), parts.begin(), parts.end());
  }
  size_t n = ws->core.size();
  auto byteB = [&](uint32_t b) { return ws->nchars[ws->coreN[b]].sb; };
  auto byteE = [&](uint32_t e) { return ws->nchars[ws->coreN[e - 1]].se; };
  std::vector<uint8_t> boundary(n + 1, 0);
  std::vector<uint32_t> tokenAt(n + 1, UINT32_MAX);
  for (uint32_t ai : path) {
    const Arc& a = ws->arcs[ai];
    boundary[a.b] = boundary[a.e] = 1;
    tokenAt[a.b] = ai;
    if (byteE(a.e) <= text.size() && out->ends[byteE(a.e)] == 0) out->ends[byteE(a.e)] = 'e';
  }
  for (uint32_t ai : path) {
    const Arc& a = ws->arcs[ai];
    uint16_t cls = a.kind == ArcKind::lex ? m.pforms[a.form].cls : a.lcls;
    if (byteB(a.b) <= text.size()) out->ends[byteB(a.b)] = ClassCode(m, cls);
  }
  for (uint32_t ai : path) {
    const Arc& x = ws->arcs[ai];
    if (x.kind != ArcKind::lex || ClassCode(m, m.pforms[x.form].cls) != 'v' || x.e - x.b < 4) continue;
    // a compound verb: a continuative verb form of two letters or more, then a verb to the end
    for (uint32_t k = ws->beginStart[x.b]; k < ws->beginStart[x.b + 1]; ++k) {
      const Arc& y = ws->arcs[ws->byBegin[k]];
      if (y.kind != ArcKind::lex || y.e >= x.e || y.e - y.b < 2 || y.rule >= 0) continue;
      const auto& yc = m.classes[m.pforms[y.form].cls];
      if (ClassCode(m, m.pforms[y.form].cls) != 'v' || m.cformNames[yc.cform].rfind("連用形", 0) != 0) continue;
      bool rest = false;
      for (uint32_t j = ws->beginStart[y.e]; j < ws->beginStart[y.e + 1] && !rest; ++j) {
        const Arc& z = ws->arcs[ws->byBegin[j]];
        rest = z.kind == ArcKind::lex && z.e == x.e && z.rule < 0 && ClassCode(m, m.pforms[z.form].cls) == 'v';
      }
      if (rest && out->ends[byteB(y.e)] == 0) out->ends[byteB(y.e)] = 'i';
    }
  }
  // derived spoken words may be cut anywhere inside (か|ぁ, た|ぁ～)
  for (uint32_t ai : path) {
    const Arc& x = ws->arcs[ai];
    if (x.rule < 0) continue;
    for (uint32_t k = x.b + 1; k < x.e; ++k)
      if (out->ends[byteB(k)] == 0) out->ends[byteB(k)] = 'i';
  }
  // cuts made by a path close to the best one
  for (const Arc& a : ws->arcs) {
    if (a.removedBy >= 0 || a.e >= n || boundary[a.e]) continue;
    if (through(a) < slack && out->ends[byteB(a.e)] == 0) out->ends[byteB(a.e)] = 'i';
  }
  // kana verb and adjective forms of a kanji lemma, spelled in kanji when the analyzer is sure of the word
  for (uint32_t ai : path) {
    const Arc& x = ws->arcs[ai];
    if (x.kind != ArcKind::lex || x.rule >= 0) continue;
    uint8_t c = ClassCode(m, m.pforms[x.form].cls);
    if (c != 'v' && c != 'a') continue;
    std::string_view xud = m.udNames[m.groups[x.ref].ud];
    if (xud.size() < 6 || xud.substr(xud.size() - 6) != "一般") continue;  // not する, いる, なる
    bool kana = true;
    for (uint32_t k = x.b; k < x.e; ++k) kana = kana && internal::IsHiragana(ws->core[k]);
    if (!kana) continue;
    // only a kana spelling of several words (たつ: 立つ, 経つ, 断つ), where the analyzer's word wins clearly
    bool sure = true, homograph = false;
    for (uint32_t k = ws->beginStart[x.b]; k < ws->beginStart[x.b + 1] && sure; ++k) {
      const Arc& y = ws->arcs[ws->byBegin[k]];
      if (&y == &x || y.e != x.e || y.kind != ArcKind::lex) continue;
      uint8_t yc = ClassCode(m, m.pforms[y.form].cls);
      if ((yc != 'v' && yc != 'a') || m.S(m.groups[y.ref].lemma) == m.S(m.groups[x.ref].lemma)) continue;
      homograph = true;
      if (through(y) < slack) sure = false;
    }
    if (!sure || !homograph) continue;
    auto it = m.kanjiGroups.find(m.S(m.groups[x.ref].lemma));
    if (it == m.kanjiGroups.end()) continue;
    uint16_t cls = m.pforms[x.form].cls;
    std::string best;
    int32_t bestCost = kInf;
    for (uint32_t g : it->second) {
      const auto& gr = m.groups[g];
      if (gr.ud != m.groups[x.ref].ud || gr.ctype != m.groups[x.ref].ctype) continue;
      const auto& p = m.paradigms[gr.paradigm];
      for (uint32_t f = p.first; f < p.first + p.count; ++f) {
        if (m.pforms[f].cls != cls || gr.cost >= bestCost) continue;
        best.clear();
        for (char32_t ch : m.C(gr.stem, gr.stemLen)) AppendUtf8(best, ch);
        for (char32_t ch : m.C(m.pforms[f].suf, m.pforms[f].sufLen)) AppendUtf8(best, ch);
        bestCost = gr.cost;
      }
    }
    if (!best.empty()) out->respell.push_back({{byteB(x.b), byteE(x.e)}, best});
  }
  for (const Arc& a : ws->arcs) {
    if (a.kind != ArcKind::lex || a.rule >= 0 || a.removedBy >= 0 || !boundary[a.b] || !boundary[a.e]) continue;
    if (tokenAt[a.b] != UINT32_MAX && ws->arcs[tokenAt[a.b]].e >= a.e) continue;  // one analyzer word or less
    uint8_t c = ClassCode(m, m.pforms[a.form].cls);
    if (c == 'p' || c == 'x' || c == 's' || c == 'u') continue;
    // only a word that loses clearly and would swallow a particle the analyzer reads (しか + し)
    if (through(a) < slack) continue;
    bool swallows = false;
    for (uint32_t k = a.b; k < a.e && !swallows; ++k) {
      if (tokenAt[k] == UINT32_MAX) continue;
      const Arc& t = ws->arcs[tokenAt[k]];
      uint8_t tc = ClassCode(m, t.kind == ArcKind::lex ? m.pforms[t.form].cls : t.lcls);
      swallows = tc == 'p';
    }
    if (swallows) out->rejected.push_back({{byteB(a.b), byteE(a.e)}, c});
  }
}

// ---- JSON ---------------------------------------------------------------------------------------
namespace {

void Esc(std::string& o, std::string_view s) {
  o += '"';
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += c;
        }
    }
  }
  o += '"';
}

void Num(std::string& o, float v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.2f", static_cast<double>(v));
  o += b;
}

void TokJson(std::string& o, const Analyzer& a, const Token& t, const Analysis* an) {
  o += "{\"src\":[" + std::to_string(t.src.begin) + "," + std::to_string(t.src.end) + "],\"surf\":";
  Esc(o, t.surf);
  o += ",\"lemma\":";
  Esc(o, t.lemma);
  o += ",\"reading\":";
  Esc(o, t.reading);
  o += ",\"pos\":";
  Esc(o, Analyzer::PosName(t.pos));
  o += ",\"ud\":";
  Esc(o, a.UdName(t.ud));
  o += ",\"ctype\":";
  Esc(o, a.CtypeName(t.ctype));
  o += ",\"cform\":";
  Esc(o, a.CformName(t.cform));
  o += ",\"flags\":[";
  bool first = true;
  for (uint16_t bit = 1; bit != 0 && bit <= Repaired; bit = static_cast<uint16_t>(bit << 1))
    if (t.flags & bit) {
      if (!first) o += ',';
      first = false;
      Esc(o, Analyzer::FlagName(bit));
    }
  o += "],\"cost\":";
  Num(o, t.cost);
  o += ",\"margin\":";
  Num(o, t.margin);
  if (!t.derive.empty()) {
    o += ",\"derive\":";
    Esc(o, t.derive);
  }
  if (!t.removedBy.empty()) {
    o += ",\"removedBy\":";
    Esc(o, t.removedBy);
  }
  if (!t.repair.empty()) {
    o += ",\"repair\":";
    Esc(o, t.repair);
  }
  if (an && t.nAlt) {
    o += ",\"alts\":[";
    for (uint16_t k = 0; k < t.nAlt; ++k) {
      const Token& x = an->alts[t.alt + k];
      if (k) o += ',';
      o += "{\"lemma\":";
      Esc(o, x.lemma);
      o += ",\"ud\":";
      Esc(o, a.UdName(x.ud));
      o += ",\"cform\":";
      Esc(o, a.CformName(x.cform));
      o += ",\"cost\":";
      Num(o, x.margin);
      if (!x.removedBy.empty()) {
        o += ",\"removedBy\":";
        Esc(o, x.removedBy);
      }
      o += '}';
    }
    o += ']';
  }
  o += '}';
}

}  // namespace

std::string ToJson(const Analyzer& a, std::string_view src, const Analysis& an, std::string_view doc, int line) {
  std::string o = "{";
  if (!doc.empty()) {
    o += "\"doc\":";
    Esc(o, doc);
    o += ',';
  }
  if (line >= 0) o += "\"line\":" + std::to_string(line) + ",";
  o += "\"src\":";
  Esc(o, src);
  o += ",\"norm\":";
  Esc(o, an.norm);
  o += ",\"edits\":[";
  for (size_t i = 0; i < an.edits.size(); ++i) {
    const auto& e = an.edits[i];
    if (i) o += ',';
    o += "{\"src\":[" + std::to_string(e.src.begin) + "," + std::to_string(e.src.end) + "],\"from\":";
    Esc(o, e.from);
    o += ",\"to\":";
    Esc(o, e.to);
    o += ",\"kind\":";
    Esc(o, Analyzer::EditName(e.kind));
    o += '}';
  }
  o += "],\"toks\":[";
  for (size_t i = 0; i < an.toks.size(); ++i) {
    if (i) o += ',';
    TokJson(o, a, an.toks[i], &an);
  }
  o += ']';
  if (!an.repairs.empty()) {
    o += ",\"repairs\":[";
    for (size_t i = 0; i < an.repairs.size(); ++i) {
      const auto& r = an.repairs[i];
      if (i) o += ',';
      o += "{\"src\":[" + std::to_string(r.src.begin) + "," + std::to_string(r.src.end) + "],\"from\":";
      Esc(o, r.from);
      o += ",\"to\":";
      Esc(o, r.to);
      o += ",\"op\":";
      Esc(o, r.op);
      o += ",\"cost\":" + std::to_string(r.cost) + ",\"tok\":" + std::to_string(r.tok) + "}";
    }
    o += "],\"repaired\":";
    Esc(o, an.repaired);
  }
  if (!an.alt.empty()) {
    o += ",\"alt\":{\"margin\":";
    Num(o, an.altMargin);
    o += ",\"toks\":[";
    for (size_t i = 0; i < an.alt.size(); ++i) {
      if (i) o += ',';
      TokJson(o, a, an.alt[i], nullptr);
    }
    o += "]}";
  }
  o += '}';
  return o;
}

}  // namespace hd8::ana
