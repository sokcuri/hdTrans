#include "ir_view.h"

#include <algorithm>

#include "transfer.h"

namespace hd08::xfer {

namespace {

std::string En(F f, unsigned v) { return std::string(Name(f, v)); }

std::vector<std::string> BitNames(F f, uint32_t bits) {
  std::vector<std::string> out;
  for (unsigned i = 0; i < Count(f); ++i)
    if (bits & (1u << i)) out.emplace_back(Name(f, i));
  return out;
}

}  // namespace

LineIR MakeView(const hd08::Doc& doc, const hd08::Line& line, const Table& t) {
  LineIR v;
  v.key = line.name.empty() ? std::to_string(line.id) : line.name;
  v.src = line.src;
  for (const Token& k : line.toks) {
    Tok x;
    x.src.begin = k.src.begin;
    x.src.end = k.src.end;
    x.surf = std::string(doc.S(k.surf));
    x.lemma = std::string(doc.S(k.lemma));
    x.reading = std::string(doc.S(k.reading));
    x.pos = En(F::pos, static_cast<unsigned>(k.pos));
    x.ud = std::string(doc.ud.Str(k.ud));
    x.ctype = std::string(doc.ctypes.Str(k.ctype));
    x.cform = std::string(doc.cforms.Str(k.cform));
    x.spaceBefore = (k.flags & tokflag::SpaceBefore) != 0;
    if (x.pos == "n" || x.pos == "pn" || x.pos == "num") x.cls = t.ClassOf(x.lemma, x.surf, x.ud);
    v.toks.push_back(std::move(x));
  }
  // a noun before an honorific suffix (王妃様, 田中さん) is a (higher) person
  for (size_t i = 0; i + 1 < v.toks.size(); ++i) {
    Tok& k = v.toks[i];
    if (k.pos != "n" && k.pos != "pn") continue;
    const Tok& s = v.toks[i + 1];
    auto in = [&](const char* cls) {
      auto it = t.fclass.find(cls);
      if (it == t.fclass.end()) return false;
      for (const auto& f : it->second)
        if ((f.pos.empty() || f.pos == "*" || f.pos == s.pos) && std::find(f.lemmas.begin(), f.lemmas.end(), s.lemma) != f.lemmas.end())
          return true;
      return false;
    };
    if (in("hon.suffix.higher")) k.cls = "@person.higher";
    else if (in("hon.suffix") && k.cls.empty()) k.cls = "@person";
  }
  for (const Bunsetsu& b : line.buns) {
    Bun x;
    x.first = b.first;
    x.last = b.last;
    x.content = b.content;
    x.head = b.head;
    x.head2 = b.head2;
    x.headMargin = b.headMargin;
    std::string type = En(F::buntype, b.type);
    x.type = type.empty() ? 'N' : type[0];
    x.kase = En(F::kase_ja, static_cast<unsigned>(b.kase));
    x.topic = (b.flags & bunflag::topic) != 0;
    x.commaAfter = (b.flags & bunflag::commaAfter) != 0;
    x.quoteClose = (b.flags & bunflag::quoteClose) != 0;
    v.buns.push_back(std::move(x));
  }
  for (const hd08::Clause& c : line.clauses) {
    Clause x;
    x.pred = c.pred;
    x.firstBun = c.firstBun;
    x.lastBun = c.lastBun;
    x.parent = c.parent;
    x.link = En(F::link, c.link);
    x.tense = En(F::tense, c.tense);
    x.aspect = En(F::jaspect, c.aspect);
    x.pol = En(F::pol, c.pol);
    x.negFn = En(F::negfn, c.negFn);
    x.voice = En(F::voice, c.voice);
    x.bene = En(F::bene, c.bene);
    x.force = En(F::force, c.force);
    x.polite = En(F::polite, c.polite);
    x.modality = BitNames(F::modality, c.modality);
    x.jreg = BitNames(F::jreg, c.jreg);
    for (const hd08::Arg& a : c.args) {
      xfer::Arg g;
      g.role = En(F::role, static_cast<unsigned>(a.role));
      g.lemma = std::string(doc.S(a.lemma));
      g.cls = std::string(doc.S(a.cls));
      g.how = En(F::how, a.how);
      g.bun = a.bun;
      if (g.cls.empty() && a.bun < line.buns.size()) {
        size_t ct = line.buns[a.bun].content;
        if (ct < v.toks.size()) g.cls = v.toks[ct].cls;
      }
      x.args.push_back(std::move(g));
    }
    for (uint16_t k : c.consumed) x.consumed.push_back(k);
    for (uint16_t k : c.unfolded) x.unfolded.push_back(k);
    v.clauses.push_back(std::move(x));
  }
  for (size_t b = 0; b < v.buns.size(); ++b)
    for (int k = v.buns[b].first; k <= v.buns[b].last && k >= 0 && static_cast<size_t>(k) < v.toks.size(); ++k)
      v.toks[static_cast<size_t>(k)].bun = static_cast<int>(b);
  for (size_t c = 0; c < v.clauses.size(); ++c)
    for (int b = v.clauses[c].firstBun; b <= v.clauses[c].lastBun && b >= 0 && static_cast<size_t>(b) < v.buns.size(); ++b)
      v.buns[static_cast<size_t>(b)].clause = static_cast<int>(c);
  return v;
}

}  // namespace hd08::xfer
