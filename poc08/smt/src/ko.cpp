#include "hd8/smt_ko.h"

#include <map>
#include <mutex>
#include <unordered_map>

#include "hd8gen/analyze.h"
#include "hd8gen/generator.h"

namespace hd8::smt {

using hd8gen::Bundle;
using hd8gen::CType;
using hd8gen::Level;
using hd8gen::Mood;

namespace {

const char* kMoods[] = {"decl", "q", "imp", "prop", "excl"};

bool ParseMoodName(const std::string& s, Mood* m) {
  for (int i = 0; i < 5; ++i)
    if (s == kMoods[i]) {
      *m = static_cast<Mood>(i);
      return true;
    }
  return false;
}

char CtypeLetter(CType c) {
  switch (c) {
    case CType::final_: return 'F';
    case CType::conn: return 'C';
    case CType::adn: return 'A';
    case CType::nom: return 'N';
    case CType::quote: return 'Q';
    case CType::stem: return 'S';
  }
  return 'F';
}

bool CtypeFromLetter(char l, CType* c) {
  switch (l) {
    case 'F': *c = CType::final_; return true;
    case 'C': *c = CType::conn; return true;
    case 'A': *c = CType::adn; return true;
    case 'N': *c = CType::nom; return true;
    case 'Q': *c = CType::quote; return true;
    case 'S': *c = CType::stem; return true;
    default: return false;
  }
}

std::vector<std::string> SplitBar(const std::string& s) {
  std::vector<std::string> out(1);
  for (char ch : s) {
    if (ch == '|') out.emplace_back();
    else out.back() += ch;
  }
  return out;
}

// E|c|eomi|rel|mood|flags → bundle (lemma not set); level from @suffix
bool ParseBundle(const std::string& body, Bundle* b, std::string* level, bool* unc) {
  std::string s = body;
  level->clear();
  size_t at = s.rfind('@');
  if (at != std::string::npos) {
    *level = s.substr(at + 1);
    s = s.substr(0, at);
  }
  auto f = SplitBar(s);
  if (f.size() != 6 || f[0] != "E" || f[1].size() != 1) return false;
  hd8gen::Node& v = b->node;
  v.kind = hd8gen::Kind::V;
  if (!CtypeFromLetter(f[1][0], &v.ctype)) return false;
  v.eomi = f[2];
  v.rel = f[3];
  if (!f[4].empty() && !ParseMoodName(f[4], &v.mood)) return false;
  for (char ch : f[5]) {
    if (ch == 'p') v.tense = hd8gen::Tense::past;
    else if (ch == 'g') v.modal = "gess";
    else if (ch == 'h') b->hon = true;
    else if (ch == 'u') *unc = true;
  }
  return true;
}

std::string BundleBody(const Bundle& b, bool unc) {
  const hd8gen::Node& v = b.node;
  std::string flags;
  if (v.tense == hd8gen::Tense::past) flags += 'p';
  if (v.modal == "gess") flags += 'g';
  if (b.hon) flags += 'h';
  if (unc) flags += 'u';
  std::string mood = v.ctype == CType::final_ || v.ctype == CType::quote ? kMoods[static_cast<int>(v.mood)] : "";
  return std::string("E|") + CtypeLetter(v.ctype) + "|" + v.eomi + "|" + v.rel + "|" + mood + "|" + flags;
}

std::string Unescape(const std::string& t) { return !t.empty() && t[0] == '\\' ? t.substr(1) : t; }

// trailing run of Hangul syllables
size_t HangulTailStart(const std::string& s) {
  size_t i = s.size();
  while (i >= 3) {
    const unsigned char a = static_cast<unsigned char>(s[i - 3]);
    const unsigned char b0 = static_cast<unsigned char>(s[i - 2]);
    // U+AC00..U+D7A3 are EA B0 80 .. ED 9E A3
    if (!((a == 0xEA && b0 >= 0xB0) || a == 0xEB || a == 0xEC || (a == 0xED && b0 <= 0x9E))) break;
    i -= 3;
  }
  return i;
}

std::string LastChar(const std::string& s) {
  if (s.empty()) return s;
  size_t i = s.size() - 1;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  return s.substr(i);
}

// hd8gen::Realize with generator options (uncontracted 되어/하여 forms)
std::string RealizeOpt(const hd8gen::Tables& t, const Bundle& b, const std::string& host, bool uncontracted) {
  if (!uncontracted) return hd8gen::Realize(t, b, host);
  std::vector<hd8gen::Node> nodes;
  if (!host.empty()) {
    hd8gen::Node h;
    h.kind = hd8gen::Kind::Raw;
    h.lemma = host;
    h.id = h.origin = 1;
    nodes.push_back(h);
  }
  hd8gen::Node v = b.node;
  v.id = v.origin = 2;
  v.glue = false;
  nodes.push_back(v);
  hd8gen::Reg r;
  r.node = 2;
  r.level = b.level == Level::none ? Level::haera : b.level;
  r.hon = b.hon;
  hd8gen::UttLevel u;
  u.level = r.level;
  u.narration = true;
  hd8gen::GenOptions go;
  go.uncontracted = true;
  std::string out = hd8gen::Generate(t, nodes, {r}, {u}, go).out;
  if (!host.empty() && out.rfind(host, 0) == 0) out = out.substr(host.size());
  if (!out.empty() && out[0] == ' ') out = out.substr(1);
  return out;
}

struct Pending {
  std::string lemma, cls, host, pos;
  bool active = false;
};

void ParseLemma(const std::string& lem, Pending* p) {
  std::string l = lem;
  size_t c = l.find('^');
  if (c != std::string::npos) {
    p->pos = l.substr(c + 1);
    l = l.substr(0, c);
  }
  size_t tl = l.find('~');
  p->lemma = tl == std::string::npos ? l : l.substr(0, tl);
  p->cls = tl == std::string::npos ? "" : l.substr(tl + 1);
}

}  // namespace

std::vector<std::string> SplitTokens(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : line) {
    if (ch == ' ') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += ch;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool KoText::Load(const std::string& dir, std::string* err) {
  std::string e;
  tables_ = hd8gen::LoadTables(dir, e);
  if (!tables_) {
    *err = e;
    return false;
  }
  return true;
}

std::string KoText::Join(const std::vector<std::string>& toks, const std::string& level,
                         const std::string& quoteLevel) const {
  const hd8gen::Tables& t = *tables_;
  std::string out, text;
  int depth = 0;
  auto track = [&](const std::string& lit) {
    for (const char* o : {"「", "『", "“"})
      for (size_t at = lit.find(o); at != std::string::npos; at = lit.find(o, at + 1)) ++depth;
    for (const char* c : {"」", "』", "”"})
      for (size_t at = lit.find(c); at != std::string::npos; at = lit.find(c, at + 1)) depth = depth > 0 ? depth - 1 : 0;
  };
  Pending p;
  bool any = false;
  auto flushWord = [&]() {
    if (p.active) text += p.host.empty() ? p.lemma : p.lemma;  // a lemma with no ending: dictionary form
    p = Pending{};
    if (text.empty()) return;
    // no space after an opening bracket (「 『 “ ( [ ‘)
    static const char* kOpen[] = {"「", "『", "“", "(", "（", "[", "‘", "〈", "《"};
    bool afterOpen = false;
    for (const char* o : kOpen) {
      const size_t n = std::char_traits<char>::length(o);
      if (out.size() >= n && out.compare(out.size() - n, n, o) == 0) afterOpen = true;
    }
    if (any && !afterOpen) out += ' ';
    out += text;
    any = true;
    text.clear();
  };
  for (const std::string& tok : toks) {
    if (tok.empty()) continue;
    if (tok[0] != '+') {
      flushWord();
      if (tok.size() > 2 && tok[0] == 'V' && tok[1] == ':') {
        p.active = true;
        ParseLemma(tok.substr(2), &p);
      } else {
        text = Unescape(tok);
        track(text);
      }
      continue;
    }
    const std::string body = tok.substr(1);
    if (body.rfind("X:", 0) == 0) {
      if (p.active) {
        text += p.lemma;
        p = Pending{};
      }
      size_t h = HangulTailStart(text);
      p.active = true;
      ParseLemma(body.substr(2), &p);
      p.lemma = text.substr(h) + p.lemma;
      text = text.substr(0, h);
    } else if (body.rfind("L:", 0) == 0) {
      if (p.active) {
        text += p.lemma;
        p = Pending{};
      }
      p.active = true;
      ParseLemma(body.substr(2), &p);
    } else if (body.rfind("V:", 0) == 0) {
      if (p.active) {
        text += p.lemma;
        p = Pending{};
      }
      p.active = true;
      ParseLemma(body.substr(2), &p);
      p.host = text;
      if (p.host.empty()) p.host = LastChar(out);
    } else if (body.rfind("E|", 0) == 0) {
      Bundle b;
      std::string lv;
      bool unc = false;
      if (!p.active || !ParseBundle(body, &b, &lv, &unc)) continue;  // an ending without a lemma is dropped
      b.node.lemma = p.lemma;
      b.node.kcls = p.cls;
      b.node.vpos = p.pos;
      if (b.node.ctype == CType::final_) {
        Level L = Level::haera;
        if (!hd8gen::ParseLevel(lv.empty() ? (depth > 0 ? quoteLevel : level) : lv, L)) L = Level::haera;
        b.level = L;
      }
      text += RealizeOpt(t, b, p.host, unc);
      p = Pending{};
    } else if (body.rfind("J:", 0) == 0) {
      if (p.active) {
        text += p.lemma;
        p = Pending{};
      }
      const std::string prev = text.empty() ? LastChar(out) : text;
      text += hd8gen::Particle(t, prev, body.substr(2));
    } else {
      if (p.active) {
        text += p.lemma;
        p = Pending{};
      }
      text += Unescape(body);
      track(Unescape(body));
    }
  }
  flushWord();
  return out;
}

namespace {
struct UB {
  Bundle b;
  bool unc;
};
struct ParaCache {
  std::mutex mu;
  std::unordered_map<std::string, std::unordered_map<std::string, std::vector<UB>>> map;
};
ParaCache& Cache() {
  static ParaCache c;
  return c;
}
}  // namespace

std::string KoText::QueryPredicate(const std::string& lemma, const std::string& cls, const std::string& pos,
                                   const std::string& host, const std::string& surface, const std::string& want,
                                   const std::string& mood, std::string* level) const {
  const std::string hk = LastChar(host);
  const std::string key = lemma + "\t" + cls + "\t" + pos + "\t" + hk;
  std::vector<UB> ubs;
  {
    ParaCache& c = Cache();
    std::lock_guard<std::mutex> lk(c.mu);
    auto it = c.map.find(key);
    if (it == c.map.end()) {
      std::unordered_map<std::string, std::vector<UB>> m;
      for (auto& [s, b] : hd8gen::Paradigm(*tables_, lemma, pos, cls, hk)) {
        m[s].push_back({b, false});
        std::string u = RealizeOpt(*tables_, b, hk, true);
        if (u != s) m[u].push_back({b, true});
      }
      it = c.map.emplace(key, std::move(m)).first;
    }
    auto f = it->second.find(surface);
    if (f == it->second.end()) return "";
    ubs = f->second;
  }
  // contracted readings first; the hints rank within each group
  std::vector<Bundle> bs;
  bool unc = true;
  for (auto& x : ubs)
    if (!x.unc) unc = false;
  for (auto& x : ubs)
    if (x.unc == unc) bs.push_back(x.b);
  hd8gen::AnalyzeHints h;
  if (want == "final") h.want = hd8gen::Want::final_;
  else if (want == "conn") h.want = hd8gen::Want::conn;
  else if (want == "adn") h.want = hd8gen::Want::adn;
  else if (want == "nom") h.want = hd8gen::Want::nom;
  Mood m;
  if (ParseMoodName(mood, &m)) h.mood = m;
  h.host = hk;
  hd8gen::RankBundles(bs, h);
  const Bundle& b = bs.front();
  *level = b.node.ctype == CType::final_ ? hd8gen::Name(b.level) : "";
  // the bundle must realize the same surface at its own level (Realize defaults non-finals to haera)
  return BundleBody(b, unc);
}

bool KoText::CheckParticle(const std::string& prev, const std::string& notation, const std::string& surface) const {
  return hd8gen::Particle(*tables_, prev, notation) == surface;
}

}  // namespace hd8::smt
