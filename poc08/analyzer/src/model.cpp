#include "model.h"

#include <algorithm>
#include <cstring>

#include "utf8.h"

namespace hd8::ana {

using internal::AppendUtf8;
using internal::DecodeUtf8;

Str Model::Intern(std::string_view s) {
  auto it = internMap.find(std::string(s));
  if (it != internMap.end()) return it->second;
  Str r{static_cast<uint32_t>(pool.size()), static_cast<uint32_t>(s.size())};
  pool.append(s);
  internMap.emplace(std::string(s), r);
  return r;
}

uint32_t Model::Trie::Child(uint32_t n, char32_t c) const {
  if (n == 0 && c < 0x10000 && !rootDirect.empty()) return rootDirect[c];
  const Node& node = nodes[n];
  auto first = edgeChar.begin() + node.child;
  auto last = first + node.nChild;
  auto it = std::lower_bound(first, last, c);
  if (it == last || *it != c) return 0;
  return edgeNode[static_cast<size_t>(it - edgeChar.begin())];
}

uint8_t Model::Primary(char32_t c) const {
  if (c < 0x10000) return bmpPrimary[c];
  for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
    if (c >= it->lo && c <= it->hi) return it->primary;
  return 0;
}

uint32_t Model::Mask(char32_t c) const {
  if (c < 0x10000) return bmpMask[c];
  for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
    if (c >= it->lo && c <= it->hi) return it->mask;
  return 1;
}

uint32_t CharClassOf(char32_t c) {
  if (internal::IsHiragana(c) && c != 0x309B && c != 0x309C) return ccHira;
  if (internal::IsKatakana(c)) return ccKata;
  if (internal::IsKanji(c)) return ccKanji;
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xFF21 && c <= 0xFF3A) ||
      (c >= 0xFF41 && c <= 0xFF5A) || (c >= 0xC0 && c <= 0x24F))
    return ccAlpha;
  if ((c >= '0' && c <= '9') || (c >= 0xFF10 && c <= 0xFF19)) return ccDigit;
  if (c < 0x80 || (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFF65) || (c >= 0x2000 && c <= 0x2BFF))
    return ccSym;
  return ccOther;
}

int FindClass(const Model& m, std::string_view ud, std::string_view ctype, std::string_view cform,
              std::string_view lemma) {
  for (size_t i = 0; i < m.classes.size(); ++i) {
    const auto& c = m.classes[i];
    if (m.udNames[c.ud] == ud && m.ctypeNames[c.ctype] == ctype && m.cformNames[c.cform] == cform &&
        m.S(c.lemma) == lemma)
      return static_cast<int>(i);
  }
  return -1;
}

namespace {

template <class KeyFn>
void BuildTrie(Model::Trie* t, size_t n, KeyFn key) {
  struct TNode {
    std::vector<std::pair<char32_t, uint32_t>> kids;
    uint32_t first = 0, count = 0;
  };
  std::vector<TNode> tn(1);
  for (size_t i = 0; i < n; ++i) {
    std::u32string_view k = key(i);
    uint32_t cur = 0;
    for (char32_t c : k) {
      auto& kids = tn[cur].kids;
      if (!kids.empty() && kids.back().first == c) {
        cur = kids.back().second;
        continue;
      }
      uint32_t nx = static_cast<uint32_t>(tn.size());
      kids.emplace_back(c, nx);  // keys arrive sorted, so children stay sorted
      tn.emplace_back();
      cur = nx;
    }
    if (tn[cur].count == 0) tn[cur].first = static_cast<uint32_t>(i);
    ++tn[cur].count;
  }
  t->nodes.assign(tn.size(), {});
  t->edgeChar.clear();
  t->edgeNode.clear();
  for (size_t i = 0; i < tn.size(); ++i) {
    auto& node = t->nodes[i];
    node.child = static_cast<uint32_t>(t->edgeChar.size());
    node.nChild = static_cast<uint32_t>(tn[i].kids.size());
    node.first = tn[i].first;
    node.count = tn[i].count;
    for (auto& kv : tn[i].kids) {
      t->edgeChar.push_back(kv.first);
      t->edgeNode.push_back(kv.second);
    }
  }
  t->rootDirect.assign(0x10000, 0);
  for (auto& kv : tn[0].kids)
    if (kv.first < 0x10000) t->rootDirect[kv.first] = kv.second;
}

// ---- varint writer / reader ----------------------------------------------------------------------
struct Writer {
  std::string b;
  void U(uint64_t v) {
    while (v >= 0x80) {
      b += static_cast<char>((v & 0x7F) | 0x80);
      v >>= 7;
    }
    b += static_cast<char>(v);
  }
  void I(int64_t v) { U((static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63)); }
  void Bytes(std::string_view s) {
    U(s.size());
    b.append(s);
  }
  void U32(std::u32string_view s) {
    std::string t;
    for (char32_t c : s) AppendUtf8(t, c);
    Bytes(t);
  }
};

struct Reader {
  std::string_view d;
  size_t p = 0;
  bool ok = true;
  uint64_t U() {
    uint64_t v = 0;
    int sh = 0;
    while (true) {
      if (p >= d.size() || sh > 63) {
        ok = false;
        return 0;
      }
      auto x = static_cast<unsigned char>(d[p++]);
      v |= static_cast<uint64_t>(x & 0x7F) << sh;
      if (!(x & 0x80)) break;
      sh += 7;
    }
    return v;
  }
  int64_t I() {
    uint64_t v = U();
    return static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1);
  }
  std::string_view Bytes() {
    uint64_t n = U();
    if (!ok || n > d.size() - p) {
      ok = false;
      return {};
    }
    auto s = d.substr(p, n);
    p += n;
    return s;
  }
};

Str Append(Model* m, std::string_view s) {
  Str r{static_cast<uint32_t>(m->pool.size()), static_cast<uint32_t>(s.size())};
  m->pool.append(s);
  return r;
}

uint32_t AppendCps(Model* m, std::string_view utf8) {
  auto off = static_cast<uint32_t>(m->cps.size());
  size_t i = 0;
  while (i < utf8.size()) m->cps += DecodeUtf8(utf8, i);
  return off;
}

constexpr char kMagic[] = "HD8A";
constexpr char kMagicLong[] = "HD8L";
constexpr uint64_t kVersion = 1;

void WriteNames(Writer& w, const std::vector<std::string>& v) {
  w.U(v.size());
  for (auto& s : v) w.Bytes(s);
}

bool ReadNames(Reader& r, std::vector<std::string>* v) {
  uint64_t n = r.U();
  if (!r.ok || n > r.d.size()) return false;
  v->clear();
  for (uint64_t i = 0; i < n && r.ok; ++i) v->emplace_back(r.Bytes());
  return r.ok;
}

}  // namespace

void Finalize(Model* m) {
  m->nClass = static_cast<uint32_t>(m->classes.size());
  size_t n = m->nClass;
  m->conn.assign(n * n, 0);
  for (size_t a = 0; a < n; ++a)
    for (size_t b = 0; b < n; ++b) {
      int v = m->boLeft[a] + m->uniRight[b];
      m->conn[a * n + b] = static_cast<int16_t>(std::clamp(v, -32000, 32000));
    }
  for (auto& p : m->pairs) m->conn[static_cast<size_t>(p.l) * n + p.r] = static_cast<int16_t>(std::clamp(p.cost, -32000, 32000));
  BuildTrie(&m->trie, m->groups.size(), [m](size_t i) { return m->C(m->groups[i].stem, m->groups[i].stemLen); });
  m->bmpPrimary.assign(0x10000, 0);
  m->bmpMask.assign(0x10000, 1);
  for (auto& r : m->ranges) {
    char32_t hi = std::min<char32_t>(r.hi, 0xFFFF);
    for (char32_t c = r.lo; c <= hi && c < 0x10000; ++c) {
      m->bmpPrimary[c] = r.primary;
      m->bmpMask[c] = r.mask;
    }
  }
  m->classPos.assign(n, Pos::unk);
  for (size_t i = 0; i < n; ++i) {
    const auto& c = m->classes[i];
    std::string_view ud = m->udNames[c.ud], ct = m->ctypeNames[c.ctype];
    for (auto& r : m->posRules)
      if (ud.substr(0, r.ud.size()) == r.ud && ct.substr(0, r.ctype.size()) == r.ctype) {
        m->classPos[i] = r.pos;
        break;
      }
  }
  m->stutterRule = -1;
  for (size_t i = 0; i < m->rules.size(); ++i)
    if (m->rules[i].kind == 2) m->stutterRule = static_cast<int16_t>(i);
  FinalizeLongs(m);
}

void FinalizeLongs(Model* m) {
  BuildTrie(&m->longTrie, m->longs.size(), [m](size_t i) { return m->C(m->longs[i].key, m->longs[i].keyLen); });
  m->maxLongLen = 0;
  for (auto& l : m->longs) m->maxLongLen = std::max<uint32_t>(m->maxLongLen, l.keyLen);
}

std::string Serialize(const Model& m) {
  Writer w;
  w.b.append(kMagic, 4);
  w.U(kVersion);
  WriteNames(w, m.udNames);
  WriteNames(w, m.ctypeNames);
  WriteNames(w, m.cformNames);
  w.U(m.posRules.size());
  for (auto& r : m.posRules) {
    w.Bytes(r.ud);
    w.Bytes(r.ctype);
    w.U(static_cast<uint8_t>(r.pos));
  }
  w.U(m.classes.size());
  for (auto& c : m.classes) {
    w.U(c.ud);
    w.U(c.ctype);
    w.U(c.cform);
    w.Bytes(m.S(c.lemma));
  }
  w.U(m.paradigms.size());
  for (auto& p : m.paradigms) {
    w.U(p.count);
    for (uint32_t k = 0; k < p.count; ++k) {
      const auto& f = m.pforms[p.first + k];
      w.U32(m.C(f.suf, f.sufLen));
      w.U(f.cls);
      w.Bytes(m.S(f.read));
    }
  }
  // groups, column by column
  const auto& g = m.groups;
  w.U(g.size());
  std::u32string_view prev;
  for (auto& x : g) {
    auto stem = m.C(x.stem, x.stemLen);
    size_t common = 0;
    while (common < prev.size() && common < stem.size() && prev[common] == stem[common]) ++common;
    w.U(common);
    w.U32(stem.substr(common));
    prev = stem;
  }
  for (auto& x : g) w.U(x.ud);
  for (auto& x : g) w.U(x.ctype);
  for (auto& x : g) w.U(x.paradigm);
  for (auto& x : g) {
    w.U(x.readKind);
    if (x.readKind == 1) w.Bytes(m.S(x.read));
  }
  for (auto& x : g) w.Bytes(m.S(x.lemma));
  for (auto& x : g) w.I(x.cost);
  w.U(m.pairs.size());
  uint32_t pl = 0, pr = 0;
  for (auto& p : m.pairs) {
    w.U(p.l - pl);
    w.U(p.l == pl ? p.r - pr : p.r);
    pl = p.l;
    pr = p.r;
  }
  for (auto& p : m.pairs) w.I(p.cost);
  for (auto v : m.boLeft) w.I(v);
  for (auto v : m.uniRight) w.I(v);
  w.U(m.cats.size());
  for (auto& c : m.cats) {
    w.Bytes(c.name);
    w.U(c.invoke);
    w.U(c.group);
    w.U(c.length);
    w.U(c.arcs.size());
    for (auto& a : c.arcs) {
      w.U(a.cls);
      w.I(a.cost);
      w.I(a.perChar);
    }
  }
  w.U(m.ranges.size());
  for (auto& r : m.ranges) {
    w.U(r.lo);
    w.U(r.hi);
    w.U(r.mask);
    w.U(r.primary);
  }
  auto writeConds = [&w](const std::vector<Model::Cond>& cs) {
    w.U(cs.size());
    for (auto& c : cs) {
      w.U(c.what);
      w.U(c.neg);
      w.U(c.mask);
      w.U(c.alts.size());
      for (auto& a : c.alts) w.U32(a);
    }
  };
  w.U(m.rules.size());
  for (auto& r : m.rules) {
    w.U(r.kind);
    w.U32(r.from);
    w.U32(r.to);
    w.I(r.cost);
    writeConds(r.cond);
    w.Bytes(r.id);
  }
  w.U(m.constraints.size());
  for (auto& c : m.constraints) {
    w.Bytes(c.id);
    w.Bytes(c.lemma);
    w.Bytes(c.surf);
    w.Bytes(c.cform);
    w.Bytes(c.ud);
    w.I(c.pos);
    writeConds(c.when);
    writeConds(c.unless);
  }
  w.I(m.joinCost);
  w.U(m.stutterCls);
  return w.b;
}

bool Deserialize(std::string_view bytes, Model* m, std::string* err) {
  Reader r{bytes};
  if (bytes.size() < 4 || std::memcmp(bytes.data(), kMagic, 4) != 0) {
    *err = "not an analyzer snapshot";
    return false;
  }
  r.p = 4;
  if (r.U() != kVersion) {
    *err = "unsupported snapshot version";
    return false;
  }
  if (!ReadNames(r, &m->udNames) || !ReadNames(r, &m->ctypeNames) || !ReadNames(r, &m->cformNames)) {
    *err = "bad name tables";
    return false;
  }
  uint64_t n = r.U();
  for (uint64_t i = 0; i < n && r.ok; ++i) {
    Model::PosRule pr;
    pr.ud = std::string(r.Bytes());
    pr.ctype = std::string(r.Bytes());
    pr.pos = static_cast<Pos>(r.U());
    m->posRules.push_back(pr);
  }
  n = r.U();
  m->classes.resize(r.ok ? n : 0);
  for (auto& c : m->classes) {
    c.ud = static_cast<uint16_t>(r.U());
    c.ctype = static_cast<uint8_t>(r.U());
    c.cform = static_cast<uint8_t>(r.U());
    c.lemma = Append(m, r.Bytes());
  }
  n = r.U();
  m->paradigms.resize(r.ok ? n : 0);
  for (auto& p : m->paradigms) {
    p.first = static_cast<uint32_t>(m->pforms.size());
    p.count = static_cast<uint32_t>(r.U());
    for (uint32_t k = 0; k < p.count && r.ok; ++k) {
      Model::PForm f;
      auto s = r.Bytes();
      f.suf = AppendCps(m, s);
      f.sufLen = static_cast<uint16_t>(m->cps.size() - f.suf);
      f.cls = static_cast<uint16_t>(r.U());
      f.read = Append(m, r.Bytes());
      m->pforms.push_back(f);
    }
  }
  n = r.U();
  if (!r.ok || n > bytes.size()) {
    *err = "bad group count";
    return false;
  }
  m->groups.resize(n);
  std::u32string prev, cur;
  for (auto& x : m->groups) {
    size_t common = r.U();
    auto rest = r.Bytes();
    if (!r.ok || common > prev.size()) break;
    cur.assign(prev, 0, common);
    size_t i = 0;
    while (i < rest.size()) cur += DecodeUtf8(rest, i);
    x.stem = static_cast<uint32_t>(m->cps.size());
    x.stemLen = static_cast<uint16_t>(cur.size());
    m->cps += cur;
    prev.swap(cur);
  }
  for (auto& x : m->groups) x.ud = static_cast<uint16_t>(r.U());
  for (auto& x : m->groups) x.ctype = static_cast<uint8_t>(r.U());
  for (auto& x : m->groups) x.paradigm = static_cast<uint32_t>(r.U());
  for (auto& x : m->groups) {
    x.readKind = static_cast<uint8_t>(r.U());
    if (x.readKind == 1) x.read = Append(m, r.Bytes());
  }
  for (auto& x : m->groups) x.lemma = Append(m, r.Bytes());
  for (auto& x : m->groups) x.cost = static_cast<int32_t>(r.I());
  n = r.U();
  if (!r.ok || n > bytes.size()) {
    *err = "bad pair count";
    return false;
  }
  m->pairs.resize(n);
  uint32_t pl = 0, pr = 0;
  for (auto& p : m->pairs) {
    uint32_t dl = static_cast<uint32_t>(r.U());
    uint32_t v = static_cast<uint32_t>(r.U());
    p.l = static_cast<uint16_t>(pl + dl);
    p.r = static_cast<uint16_t>(dl == 0 ? pr + v : v);
    pl = p.l;
    pr = p.r;
  }
  for (auto& p : m->pairs) p.cost = static_cast<int32_t>(r.I());
  m->boLeft.resize(m->classes.size());
  m->uniRight.resize(m->classes.size());
  for (auto& v : m->boLeft) v = static_cast<int32_t>(r.I());
  for (auto& v : m->uniRight) v = static_cast<int32_t>(r.I());
  n = r.U();
  m->cats.resize(r.ok && n < 256 ? n : 0);
  for (auto& c : m->cats) {
    c.name = std::string(r.Bytes());
    c.invoke = r.U() != 0;
    c.group = r.U() != 0;
    c.length = static_cast<uint8_t>(r.U());
    uint64_t k = r.U();
    for (uint64_t i = 0; i < k && r.ok; ++i) {
      Model::UnkArc a;
      a.cls = static_cast<uint16_t>(r.U());
      a.cost = static_cast<int32_t>(r.I());
      a.perChar = static_cast<int32_t>(r.I());
      c.arcs.push_back(a);
    }
  }
  n = r.U();
  for (uint64_t i = 0; i < n && r.ok; ++i) {
    Model::CharRange cr;
    cr.lo = static_cast<char32_t>(r.U());
    cr.hi = static_cast<char32_t>(r.U());
    cr.mask = static_cast<uint32_t>(r.U());
    cr.primary = static_cast<uint8_t>(r.U());
    m->ranges.push_back(cr);
  }
  auto readConds = [&r](std::vector<Model::Cond>* cs) {
    uint64_t k = r.U();
    for (uint64_t i = 0; i < k && r.ok; ++i) {
      Model::Cond c;
      c.what = static_cast<uint8_t>(r.U());
      c.neg = r.U() != 0;
      c.mask = static_cast<uint32_t>(r.U());
      uint64_t na = r.U();
      for (uint64_t j = 0; j < na && r.ok; ++j) c.alts.push_back(internal::ToU32(r.Bytes()));
      cs->push_back(std::move(c));
    }
  };
  n = r.U();
  for (uint64_t i = 0; i < n && r.ok; ++i) {
    Model::Rule ru;
    ru.kind = static_cast<uint8_t>(r.U());
    ru.from = internal::ToU32(r.Bytes());
    ru.to = internal::ToU32(r.Bytes());
    ru.cost = static_cast<int32_t>(r.I());
    readConds(&ru.cond);
    ru.id = std::string(r.Bytes());
    m->rules.push_back(ru);
  }
  n = r.U();
  for (uint64_t i = 0; i < n && r.ok; ++i) {
    Model::Constraint c;
    c.id = std::string(r.Bytes());
    c.lemma = std::string(r.Bytes());
    c.surf = std::string(r.Bytes());
    c.cform = std::string(r.Bytes());
    c.ud = std::string(r.Bytes());
    c.pos = static_cast<int>(r.I());
    readConds(&c.when);
    readConds(&c.unless);
    m->constraints.push_back(std::move(c));
  }
  m->joinCost = static_cast<int32_t>(r.I());
  m->stutterCls = static_cast<uint16_t>(r.U());
  if (!r.ok) {
    *err = "truncated snapshot";
    return false;
  }
  // bounds checks on references
  for (auto& c : m->classes)
    if (c.ud >= m->udNames.size() || c.ctype >= m->ctypeNames.size() || c.cform >= m->cformNames.size()) {
      *err = "class out of range";
      return false;
    }
  for (auto& f : m->pforms)
    if (f.cls >= m->classes.size()) {
      *err = "form class out of range";
      return false;
    }
  for (auto& x : m->groups)
    if (x.paradigm >= m->paradigms.size() || x.ud >= m->udNames.size()) {
      *err = "group out of range";
      return false;
    }
  for (auto& p : m->pairs)
    if (p.l >= m->classes.size() || p.r >= m->classes.size()) {
      *err = "pair out of range";
      return false;
    }
  for (auto& c : m->cats)
    for (auto& a : c.arcs)
      if (a.cls >= m->classes.size()) {
        *err = "unknown-word class out of range";
        return false;
      }
  for (auto& cr : m->ranges)
    if (cr.primary >= m->cats.size()) {
      *err = "char range out of range";
      return false;
    }
  if (m->stutterCls >= m->classes.size() || m->cats.empty() || m->classes.empty()) {
    *err = "bad parameters";
    return false;
  }
  Finalize(m);
  return true;
}

std::string SerializeLongs(const Model& m) {
  Writer w;
  w.b.append(kMagicLong, 4);
  w.U(kVersion);
  w.U(m.classes.size());
  w.U(m.paradigms.size());
  w.U(m.longs.size());
  std::u32string_view prev;
  for (auto& x : m.longs) {
    auto k = m.C(x.key, x.keyLen);
    size_t common = 0;
    while (common < prev.size() && common < k.size() && prev[common] == k[common]) ++common;
    w.U(common);
    w.U32(k.substr(common));
    prev = k;
  }
  for (auto& x : m.longs) w.U(x.lcls);
  for (auto& x : m.longs) w.U(x.rcls);
  for (auto& x : m.longs) w.U(x.ud);
  for (auto& x : m.longs) w.U(x.ctype);
  for (auto& x : m.longs) w.U(x.cform);
  for (auto& x : m.longs) w.U(static_cast<uint8_t>(x.pos));
  for (auto& x : m.longs) w.I(x.cost);
  for (auto& x : m.longs) w.U(x.paradigm == UINT32_MAX ? 0 : x.paradigm + 1);
  for (auto& x : m.longs) w.Bytes(m.S(x.lemma));
  for (auto& x : m.longs) w.Bytes(m.S(x.read));
  return w.b;
}

bool DeserializeLongs(std::string_view bytes, Model* m, std::string* err) {
  Reader r{bytes};
  if (bytes.size() < 4 || std::memcmp(bytes.data(), kMagicLong, 4) != 0) {
    *err = "not a long-unit overlay";
    return false;
  }
  r.p = 4;
  if (r.U() != kVersion || r.U() != m->classes.size() || r.U() != m->paradigms.size()) {
    *err = "overlay does not match this snapshot";
    return false;
  }
  uint64_t n = r.U();
  if (!r.ok || n > bytes.size()) {
    *err = "bad overlay count";
    return false;
  }
  m->longs.resize(n);
  std::u32string prev, cur;
  for (auto& x : m->longs) {
    size_t common = r.U();
    auto rest = r.Bytes();
    if (!r.ok || common > prev.size()) break;
    cur.assign(prev, 0, common);
    size_t i = 0;
    while (i < rest.size()) cur += DecodeUtf8(rest, i);
    x.key = static_cast<uint32_t>(m->cps.size());
    x.keyLen = static_cast<uint16_t>(cur.size());
    m->cps += cur;
    prev.swap(cur);
  }
  for (auto& x : m->longs) x.lcls = static_cast<uint16_t>(r.U());
  for (auto& x : m->longs) x.rcls = static_cast<uint16_t>(r.U());
  for (auto& x : m->longs) x.ud = static_cast<uint16_t>(r.U());
  for (auto& x : m->longs) x.ctype = static_cast<uint8_t>(r.U());
  for (auto& x : m->longs) x.cform = static_cast<uint8_t>(r.U());
  for (auto& x : m->longs) x.pos = static_cast<Pos>(r.U());
  for (auto& x : m->longs) x.cost = static_cast<int32_t>(r.I());
  for (auto& x : m->longs) {
    uint64_t p = r.U();
    x.paradigm = p == 0 ? UINT32_MAX : static_cast<uint32_t>(p - 1);
  }
  for (auto& x : m->longs) x.lemma = Append(m, r.Bytes());
  for (auto& x : m->longs) x.read = Append(m, r.Bytes());
  if (!r.ok) {
    *err = "truncated overlay";
    return false;
  }
  for (auto& x : m->longs)
    if (x.lcls >= m->classes.size() || x.rcls >= m->classes.size() || x.ud >= m->udNames.size() ||
        x.ctype >= m->ctypeNames.size() || x.cform >= m->cformNames.size() || x.keyLen == 0 ||
        (x.paradigm != UINT32_MAX && x.paradigm >= m->paradigms.size())) {
      *err = "overlay entry out of range";
      return false;
    }
  FinalizeLongs(m);
  return true;
}

}  // namespace hd8::ana
