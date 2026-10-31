#include "repair.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

#include "tsv.h"
#include "utf8.h"

namespace hd8::ana {

using internal::AppendUtf8;
using internal::IsHiragana;
using internal::IsKanji;
using internal::IsKatakana;
using internal::ToU32;
using internal::ToUtf8;

namespace {

// ---- table ------------------------------------------------------------------------------------------
enum Field : uint8_t { fUd, fCtype, fCform, fLemma, fSurf, fPos, fArc, fDerive, fScript, fLemmaScript, fLen,
                       fElong, fPrevCc, fNextCc, fSpace, fTriple, fSokuon, fFold, fFirstKata, fLastKata, fWordRun,
                       fLead, fRepeat, fTier, fSame, fCount };
constexpr std::string_view kFields[] = {"ud",  "ctype", "cform",  "lemma",       "surf", "pos",
                                        "arc", "derive", "script", "lemmascript", "len",  "elong",
                                        "prevcc", "nextcc", "space", "triple", "sokuon", "fold", "firstkata", "lastkata", "wordrun", "lead", "repeat", "tier",
                                        "same"};
constexpr std::string_view kOps[] = {"kata-fold", "small-big", "elong", "stutter", "space", "dakuten", "swap", "edit1"};

bool ParseAtom(std::string x, bool pairRow, RepairRules::Atom* a, std::string* err) {
  x = Trim(x);
  if (!x.empty() && x[0] == '!') {
    a->neg = true;
    x = Trim(x.substr(1));
  }
  if (x == "same=surf") {
    a->field = fSame;
    return true;
  }
  size_t dot = x.find('.'), eq = x.find('=');
  if (dot == std::string::npos || eq == std::string::npos || eq < dot) {
    *err = "bad atom '" + x + "'";
    return false;
  }
  std::string side = x.substr(0, dot);
  if (pairRow ? (side != "l" && side != "r") : side != "t") {
    *err = "bad side in '" + x + "'";
    return false;
  }
  a->side = static_cast<uint8_t>(side == "r" ? 1 : 0);
  a->prefix = eq > 0 && x[eq - 1] == '^';
  a->suffix = eq > 0 && x[eq - 1] == '$';
  std::string field = x.substr(dot + 1, (a->prefix || a->suffix ? eq - 1 : eq) - dot - 1);
  a->field = fCount;
  for (uint8_t k = 0; k < fSame; ++k)
    if (kFields[k] == field) a->field = k;
  if (a->field == fCount) {
    *err = "unknown field '" + field + "'";
    return false;
  }
  a->vals = SplitOn(x.substr(eq + 1), '|');
  return true;
}

bool ParseConj(const std::string& s, bool pairRow, RepairRules::Conj* out, std::string* err) {
  out->clear();
  if (s.find("||") != std::string::npos) {
    *err = "|| only in require: '" + s + "'";
    return false;
  }
  if (Trim(s) == "-" || Trim(s).empty()) return true;
  for (auto& part : SplitOn(s, '&')) {
    RepairRules::Atom a;
    if (!ParseAtom(part, pairRow, &a, err)) return false;
    out->push_back(std::move(a));
  }
  return true;
}

// ---- character classes ------------------------------------------------------------------------------
bool IsKana(char32_t c) { return IsHiragana(c) || IsKatakana(c); }
bool IsLongMark(char32_t c) { return c == U'ー' || c == U'〜' || c == U'～'; }
bool IsSokuon(char32_t c) { return c == U'っ' || c == U'ッ'; }
bool IsKataLetter(char32_t c) { return c >= 0x30A1 && c <= 0x30F6; }
bool IsSmall(char32_t c);
char32_t Fold(char32_t c) { return IsKataLetter(c) ? c - 0x60 : c; }

// Vowel of a kana (1..5), 0 if none.
int Vowel(char32_t c) {
  static constexpr std::u32string_view kRows[5] = {
      U"あぁかがさざただなはばぱまやゃらわゎ", U"いぃきぎしじちぢにひびぴみり", U"うぅくぐすずつづぬふぶぷむゆゅるゔ",
      U"えぇけげせぜてでねへべぺめれ", U"おぉこごそぞとどのほぼぽもよょろを"};
  char32_t h = Fold(c);
  for (int v = 0; v < 5; ++v)
    if (kRows[v].find(h) != std::u32string_view::npos) return v + 1;
  return 0;
}
bool IsVowelLetter(char32_t c) {
  char32_t h = Fold(c);
  return h == U'あ' || h == U'い' || h == U'う' || h == U'え' || h == U'お' || h == U'ぁ' || h == U'ぃ' ||
         h == U'ぅ' || h == U'ぇ' || h == U'ぉ';
}

// Small ↔ big kana.
char32_t SmallBig(char32_t c) {
  static constexpr std::u32string_view kSmall = U"ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮ";
  static constexpr std::u32string_view kBig = U"あいうえおつやゆよわアイウエオツヤユヨワ";
  size_t i = kSmall.find(c);
  if (i != std::u32string_view::npos) return kBig[i];
  i = kBig.find(c);
  if (i != std::u32string_view::npos) return kSmall[i];
  return 0;
}

bool IsSmall(char32_t c) {
  static constexpr std::u32string_view kSmall = U"ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮ";
  return kSmall.find(c) != std::u32string_view::npos;
}

// Voiced and half-voiced forms of an unvoiced kana (0 when none).
char32_t Voiced(char32_t c) {
  bool hira = IsHiragana(c);
  char32_t k = hira ? c + 0x60 : c;
  char32_t v = 0;
  if ((k >= U'カ' && k <= U'ヂ' && (k - U'カ') % 2 == 0) || k == U'ツ' || k == U'テ' || k == U'ト') v = k + 1;
  else if (k >= U'ハ' && k <= U'ホ' && (k - U'ハ') % 3 == 0) v = k + 1;
  else if (k == U'ウ') v = U'ヴ';
  if (!v) return 0;
  return hira ? (v == U'ヴ' ? U'ゔ' : v - 0x60) : v;
}
char32_t HalfVoiced(char32_t c) {
  bool hira = IsHiragana(c);
  char32_t k = hira ? c + 0x60 : c;
  if (k >= U'ハ' && k <= U'ホ' && (k - U'ハ') % 3 == 0) return hira ? k + 2 - 0x60 : k + 2;
  return 0;
}

std::string_view CcName(char32_t c) {
  if (c == 0x30FB) return "sym";  // ・
  if (IsLongMark(c) || IsSokuon(c)) return "mark";
  if (IsHiragana(c)) return "hira";
  if (IsKatakana(c)) return "kata";
  if (IsKanji(c)) return "kanji";
  if ((c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= 0xFF10 && c <= 0xFF5A))
    return "other";
  return "sym";
}

// ---- token facts for the checks ---------------------------------------------------------------------
struct Info {
  std::string_view ud, ctype, cform, lemma, derive, pos;
  std::string surf;
  std::string_view arc = "edge", script = "sym", lemmaScript = "other", elong = "none", prevCc = "edge",
                   nextCc = "edge";
  bool space = false;
  bool triple = false;   // keeps a letter written three times in running text (実実実力, だだだよ)
  bool sokuon = false;   // keeps a っ before a letter that never follows it (じっゃ, 奪いっに, うっい)
  std::string_view tier = "core";  // core, fallback (a word outside the core lexicon) or unknown
  bool repeat = false;   // keeps a repeated comma or an unpartnered repeated bracket (、、、, 「「「なんで…」)
  std::string_view fold = "none";  // katakana letters of the span read as hiragana: none, part, all
  bool firstKata = false, lastKata = false;  // the span's first and last letters are katakana as written
  bool lead = false;     // two letters come right before the span (or before its run of marks)
  bool wordRun = false;  // the span is a whole katakana run that the dictionary has as written (バカ, ヘン)
  int len = 0;
  bool content = false;  // not a function word, for the swap and fold conditions
};

std::string_view ScriptOf(std::u32string_view s) {
  bool hira = false, kata = false, kanji = false, sym = false, other = false, mixed = false, marks = !s.empty();
  for (char32_t c : s) {
    if (!IsLongMark(c) && !IsSokuon(c)) marks = false;
    if (IsHiragana(c)) hira = true;
    else if (c == U'ー') {
    } else if (c == 0x30FB) {
      sym = true;
    } else if (IsKatakana(c)) {
      // a big katakana letter after hiragana: れル, すグ (あッ, がぁッ are spoken marks)
      if (hira && !IsSmall(c)) mixed = true;
      kata = true;
    } else if (IsKanji(c)) kanji = true;
    else if (CcName(c) == "other") other = true;
    else sym = true;
  }
  if (marks) return "mark";
  if (mixed) return "mixed";
  if (kanji && (hira || kata) && !other && !sym) return "jp";  // kanji with kana: 考える, 目をやる
  int kinds = hira + kata + kanji + other + sym;
  if (kinds == 0) return s.empty() ? "sym" : "kata";
  if (kinds > 1) return "other";
  return hira ? "hira" : kata ? "kata" : kanji ? "kanji" : sym ? "sym" : "other";
}

bool HasKatakana(std::string_view s) {
  // U+30A1..U+30F6 in UTF-8: E3 82 A1..BF, E3 83 80..B6
  for (size_t i = 0; i + 2 < s.size(); ++i) {
    auto b0 = static_cast<unsigned char>(s[i]), b1 = static_cast<unsigned char>(s[i + 1]),
         b2 = static_cast<unsigned char>(s[i + 2]);
    if (b0 == 0xE3 && ((b1 == 0x82 && b2 >= 0xA1) || (b1 == 0x83 && b2 <= 0xB6))) return true;
  }
  return false;
}

// Where the lengthening of a derived word sits: end (its last letters), infl (before at most two
// hiragana letters), mid (elsewhere).
std::string_view ElongOf(std::u32string_view s) {
  size_t p = std::u32string_view::npos, q = 0;
  for (size_t i = 0; i < s.size(); ++i)
    if (IsLongMark(s[i])) {
      p = i;
      break;
    }
  if (p == std::u32string_view::npos) {
    for (size_t i = 1; i + 1 < s.size(); ++i)
      if (IsVowelLetter(s[i]) && Vowel(s[i]) == Vowel(s[i - 1]) && Vowel(s[i + 1]) == Vowel(s[i])) {
        p = i;
        break;
      }
  }
  if (p == std::u32string_view::npos) return "end";
  q = p;
  while (q < s.size() && (IsLongMark(s[q]) || (IsVowelLetter(s[q]) && Vowel(s[q]) == Vowel(s[p - (p > 0)]))))
    ++q;
  if (q >= s.size()) return "end";
  size_t rest = s.size() - q;
  bool hira = true;
  for (size_t i = q; i < s.size(); ++i) hira = hira && IsHiragana(s[i]);
  return rest <= 2 && hira ? "infl" : "mid";
}

// A spoken lengthening: ー, 〜 or ～ after a hiragana letter, or three of one hiragana vowel.
bool Lengthened(std::u32string_view s) {
  for (size_t i = 1; i < s.size(); ++i) {
    if (IsLongMark(s[i]) && IsHiragana(s[i - 1])) return true;
    if (i + 1 < s.size() && IsHiragana(s[i]) && IsVowelLetter(s[i]) && Vowel(s[i - 1]) == Vowel(s[i]) &&
        Vowel(s[i + 1]) == Vowel(s[i]))
      return true;
  }
  return false;
}

bool HasPrefix(const std::vector<std::string>& list, std::string_view ud) {
  for (auto& p : list)
    if (ud.substr(0, p.size()) == p) return true;
  return false;
}

struct Facts {
  const Model& m;
  const RepairRules& R;
  const Workspace& ws;
  const std::vector<char32_t>& core;
  bool layout = false;
  const std::vector<uint8_t>* single = nullptr;  // a single space before core k
  const std::vector<uint32_t>* wordRun = nullptr;  // begin << 16 | end of the katakana word run over core k
  const std::vector<uint8_t>* triple = nullptr;  // 1: the third letter of a run of one letter in running text;
                                                 // 2: a っ before a letter that never follows it

  void Around(Info* x, uint32_t b, uint32_t e) const {
    size_t n = core.size();
    bool marks = e > b;
    for (uint32_t k = b; k < e; ++k) marks = marks && (IsLongMark(core[k]) || IsSokuon(core[k]));
    if (marks) {
      // a token of marks only (ー, 〜, っ) is judged by the letters around its whole run
      while (b > 0 && !ws.spaceBefore[b] && (IsLongMark(core[b - 1]) || IsSokuon(core[b - 1]))) --b;
      while (e < n && !ws.spaceBefore[e] && (IsLongMark(core[e]) || IsSokuon(core[e]))) ++e;
    }
    auto letter = [&](uint32_t k) { return IsHiragana(core[k]) || IsKatakana(core[k]) || IsKanji(core[k]); };
    x->lead = b >= 2 && !ws.spaceBefore[b] && !ws.spaceBefore[b - 1] && letter(b - 1) && letter(b - 2);
    x->prevCc = b == 0 || ws.spaceBefore[b] ? std::string_view("edge") : CcName(core[b - 1]);
    x->nextCc = e >= n || ws.spaceBefore[e] ? std::string_view("edge") : CcName(core[e]);
    x->space = b > 0 && ws.spaceBefore[b] && !layout && (!single || (*single)[b]);
  }

  void FromClass(Info* x, uint16_t cls) const {
    const auto& c = m.classes[cls];
    x->ud = m.udNames[c.ud];
    x->ctype = m.ctypeNames[c.ctype];
    x->cform = m.cformNames[c.cform];
    x->pos = Analyzer::PosName(m.classPos[cls]);
  }

  Info Of(const Arc& a, const RepairArc* ra) const {
    Info x;
    std::u32string_view text = ra ? std::u32string_view(ra->text) : std::u32string_view(core.data() + a.b, a.e - a.b);
    x.surf = ToUtf8(text);
    x.len = static_cast<int>(text.size());
    x.script = ScriptOf(text);
    Around(&x, a.b, a.e);
    if (ra && x.space)
      for (auto& e : ra->edits)
        if (e.spaceOnly && e.pos == a.b) x.space = false;
    if (wordRun && (*wordRun)[a.b] != UINT32_MAX)
      x.wordRun = ((*wordRun)[a.b] >> 16) == a.b && ((*wordRun)[a.b] & 0xFFFF) == a.e;
    x.firstKata = IsKatakana(core[a.b]) && core[a.b] != U'ー';
    x.lastKata = IsKatakana(core[a.e - 1]);
    if (ra) {
      int kata = 0, folded = 0;
      for (uint32_t k = a.b; k < a.e; ++k) kata += IsKataLetter(core[k]);
      for (auto& e : ra->edits)
        if (!e.spaceOnly && e.len == 1 && !e.to.empty() && IsHiragana(e.to[0]) && IsKataLetter(core[e.pos])) ++folded;
      x.fold = folded == 0 ? "none" : folded == kata ? "all" : "part";
    }
    if (triple)
      for (uint32_t k = a.b; k < a.e; ++k) {
        if (!(*triple)[k]) continue;
        bool dropped = false;
        if (ra)
          for (auto& e : ra->edits) dropped = dropped || (!e.spaceOnly && e.pos <= k && k < e.pos + e.len);
        if (dropped) continue;
        uint8_t kind = (*triple)[k];
        (kind == 1 ? x.triple : kind == 2 ? x.sokuon : x.repeat) = true;
      }
    switch (a.kind) {
      case ArcKind::lex: {
        x.arc = "lex";
        FromClass(&x, m.pforms[a.form].cls);
        x.lemma = m.S(m.groups[a.ref].lemma);
        break;
      }
      case ArcKind::repair: {
        x.arc = "repair";
        FromClass(&x, m.pforms[ra->form].cls);
        x.lemma = m.S(m.groups[ra->group].lemma);
        break;
      }
      case ArcKind::unk:
        x.arc = "unk";
        FromClass(&x, a.lcls);
        x.lemma = {};
        break;
      case ArcKind::longUnit: {
        x.arc = "long";
        const auto& l = m.longs[a.ref];
        x.lemma = m.S(l.lemma);
        if (l.paradigm != UINT32_MAX) {
          FromClass(&x, a.rcls);
        } else {
          x.ud = m.udNames[l.ud];
          x.ctype = m.ctypeNames[l.ctype];
          x.cform = m.cformNames[l.cform];
          x.pos = Analyzer::PosName(l.pos);
        }
        break;
      }
      case ArcKind::stutter:
        x.arc = "stutter";
        FromClass(&x, a.lcls);
        break;
    }
    if (a.rule >= 0) x.derive = m.rules[static_cast<size_t>(a.rule)].id;
    if (a.kind != ArcKind::repair && Lengthened(text)) x.elong = ElongOf(text);
    x.lemmaScript = HasKatakana(x.lemma) ? "kata" : "other";
    if (a.kind == ArcKind::unk) x.tier = "unknown";
    else if (!R.fallbackGroup.empty()) {
      uint32_t g = a.kind == ArcKind::lex ? a.ref : a.kind == ArcKind::repair ? ra->group : UINT32_MAX;
      if (g != UINT32_MAX && g < R.fallbackGroup.size() && R.fallbackGroup[g]) x.tier = "fallback";
    }
    x.content = !HasPrefix(R.functionUd, x.ud);
    return x;
  }
};

std::string_view FieldOf(const Info& x, uint8_t f, std::string& buf) {
  switch (f) {
    case fUd: return x.ud;
    case fCtype: return x.ctype;
    case fCform: return x.cform;
    case fLemma: return x.lemma;
    case fSurf: return x.surf;
    case fPos: return x.pos;
    case fArc: return x.arc;
    case fDerive: return x.derive;
    case fScript: return x.script;
    case fLemmaScript: return x.lemmaScript;
    case fLen: buf = std::to_string(x.len); return buf;
    case fElong: return x.elong;
    case fPrevCc: return x.prevCc;
    case fNextCc: return x.nextCc;
    case fSpace: return x.space ? "1" : "0";
    case fTriple: return x.triple ? "1" : "0";
    case fSokuon: return x.sokuon ? "1" : "0";
    case fFold: return x.fold;
    case fFirstKata: return x.firstKata ? "1" : "0";
    case fLastKata: return x.lastKata ? "1" : "0";
    case fWordRun: return x.wordRun ? "1" : "0";
    case fLead: return x.lead ? "1" : "0";
    case fRepeat: return x.repeat ? "1" : "0";
    case fTier: return x.tier;
    default: return {};
  }
}

bool Holds(const RepairRules::Atom& a, const Info& l, const Info* r) {
  bool v = false;
  if (a.field == fSame) {
    v = r && l.surf == r->surf;
  } else {
    const Info* x = a.side == 1 ? r : &l;
    if (!x) return a.neg;
    std::string buf;
    std::string_view s = FieldOf(*x, a.field, buf);
    for (auto& val : a.vals)
      if (a.prefix   ? s.substr(0, val.size()) == val
          : a.suffix ? s.size() >= val.size() && s.substr(s.size() - val.size()) == val
                     : s == val) {
        v = true;
        break;
      }
  }
  return a.neg ? !v : v;
}

bool Fires(const RepairRules::Check& c, const Info& l, const Info* r) {
  for (auto& a : c.when)
    if (!Holds(a, l, r)) return false;
  for (auto& conj : c.require) {
    bool all = true;
    for (auto& a : conj) all = all && Holds(a, l, r);
    if (all) return false;
  }
  return true;
}

int TokBad(const RepairRules& R, const Info& t, std::string* why, bool triggersOnly = false) {
  int n = 0;
  for (auto& c : R.tok)
    if ((!triggersOnly || c.trigger) && Fires(c, t, nullptr)) {
      ++n;
      if (why) *why += " " + c.id + "(" + t.surf + ")";
    }
  return n;
}

int PairBad(const RepairRules& R, const Info& l, const Info& r, std::string* why, bool triggersOnly = false) {
  int n = 0;
  for (auto& c : R.pair)
    if ((!triggersOnly || c.trigger) && (c.space || !r.space) && Fires(c, l, &r)) {
      ++n;
      if (why) *why += " " + c.id + "(" + l.surf + "|" + r.surf + ")";
    }
  return n;
}

// ---- candidate words ---------------------------------------------------------------------------------
struct LArc {
  Arc arc;
  int32_t ws = -1;     // index in ws.arcs, or -1 for a repaired word
  int32_t rarc = -1;   // index in Window::rarcs
  int32_t rep = 0;
  Info info;
  int bad = 0;
};

struct Gen {
  Gen(const Model& mm, const RepairRules& rr, const Workspace& w, const std::vector<char32_t>& c, uint32_t h, uint32_t tb,
      uint32_t te, std::vector<RepairArc>* ra, std::vector<LArc>* la)
      : m(mm), R(rr), ws(w), core(c), hi(h), touchB(tb), touchE(te), rarcs(ra), arcs(la) {}
  const Model& m;
  const RepairRules& R;
  const Workspace& ws;
  const std::vector<char32_t>& core;
  uint32_t hi;                        // words end at or before hi
  uint32_t touchB = 0, touchE = 0;    // with a swap: words over [touchB, touchE) are kept even at cost 0
  std::vector<RepairArc>* rarcs;
  std::vector<LArc>* arcs;
  std::unordered_map<std::string, size_t> seen;
  bool layout = false;  // spaces separate words in this line: none is joined
  const std::vector<uint8_t>* single = nullptr;  // a single space before core k (only those are joined)
  const std::vector<uint32_t>* wordRun = nullptr; // begin << 16 | end of the katakana word run over core k
  const std::vector<uint8_t>* longKata = nullptr; // letters of a katakana run of three letters or more
  const std::vector<uint8_t>* unknown = nullptr;  // letters of an unknown word of the current reading
  const std::vector<uint8_t>* dup = nullptr;      // repeated letters inside a katakana word (may drop)
  bool Unknown(uint32_t j) const { return unknown && j < unknown->size() && (*unknown)[j]; }

  uint32_t start = 0;
  std::u32string prod;
  std::vector<RepairEdit> edits;
  int32_t cost = 0;
  int folds = 0, foldsStem = 0;
  bool edited = false, inStem = true;

  bool On(RepairOp op) const { return R.ops[static_cast<size_t>(op)].on; }
  int32_t Cost(RepairOp op) const { return R.ops[static_cast<size_t>(op)].cost; }

  // Pushes an edit (merging a deletion run of the same operation, which then costs nothing more).
  // Returns the cost added, or -1 when over budget.
  int32_t Push(RepairEdit e) {
    int32_t add = Cost(e.op);
    if (e.op == RepairOp::kataFold) add = 0;  // charged once per word at the end
    if (!e.spaceOnly && e.to.empty() && !edits.empty()) {
      auto& b = edits.back();
      if (b.op == e.op && !b.spaceOnly && b.to.empty() && b.pos + b.len == e.pos &&
          (e.op == RepairOp::elong || e.op == RepairOp::stutter))
        add = 0;
    }
    if (cost + add > R.arcBudget) return -1;
    cost += add;
    edits.push_back(std::move(e));
    return add;
  }
  void Pop(int32_t add) {
    cost -= add;
    edits.pop_back();
  }

  // Crossing the space before core position j inside the word.
  template <class F>
  void AtSpace(uint32_t j, const F& f) {
    if (j > start && ws.spaceBefore[j] && !ws.joinable[j]) {
      if (!On(RepairOp::space) || layout || (single && !(*single)[j])) return;
      RepairEdit e;
      e.pos = static_cast<uint16_t>(j);
      e.op = RepairOp::space;
      e.spaceOnly = true;
      int32_t add = Push(e);
      if (add < 0) return;
      f();
      Pop(add);
      return;
    }
    f();
  }

  // Every way to drop the letter at j: a lengthening mark, an inserted っ, a repeated vowel or letter.
  template <class F>
  void Drop(uint32_t j, const F& f) {
    if (prod.empty() || j >= hi || ws.spaceBefore[j]) return;
    char32_t c = core[j];
    auto try1 = [&](RepairOp op) {
      if (!On(op)) return;
      AtSpace(j, [&] {
        RepairEdit e;
        e.pos = static_cast<uint16_t>(j);
        e.len = 1;
        e.op = op;
        int32_t add = Push(e);
        if (add < 0) return;
        f(j + 1);
        Pop(add);
      });
    };
    bool inLong = longKata && (*longKata)[j];
    bool doubled = (j > 0 && IsLongMark(core[j - 1])) || (j + 1 < core.size() && IsLongMark(core[j + 1]));
    if (IsLongMark(c)) {
      if (!inLong || doubled) try1(RepairOp::elong);
    } else if (inLong) {
    } else if (IsSokuon(c) && j + 1 < core.size()) {
      try1(RepairOp::elong);
    } else if (IsHiragana(c) && IsVowelLetter(c) && Vowel(c) == Vowel(prod.back()) && c != core[j - 1]) {
      try1(RepairOp::elong);  // a vowel repeated as a letter (ええ) is a stutter below, never both
    }
    bool dropOk = !inLong || (dup && (*dup)[j]);
    // a repeat drops only as the tail of its run: the letter after it is not kept once one is dropped
    if (j > 0 && c == core[j - 1] && dropOk && !IsLongMark(c) && !IsSokuon(c) &&
        (IsKana(c) || IsKanji(c) || R.repeatMarks.find(c) != std::u32string::npos))
      try1(RepairOp::stutter);
  }

  // Every way to read the letter at j as one letter c (fn(c, j + 1)).
  template <class F>
  void Read(uint32_t j, const F& fn) {
    if (j >= hi) return;
    AtSpace(j, [&] {
      char32_t c = core[j];
      fn(c, j + 1);
      if (longKata && (*longKata)[j]) return;
      bool digraph = IsKatakana(c) && ((j + 1 < core.size() && R.digraphs.find(core[j + 1]) != std::u32string::npos) ||
                                       R.digraphs.find(c) != std::u32string::npos);
      auto variants = [&](char32_t base, bool folded) {
        if (On(RepairOp::smallBig))  // big read as small (だつた → だった); a small kana is never read big
          if (char32_t s = SmallBig(base); s && IsSmall(s)) Alt(j, s, RepairOp::smallBig, folded, fn);
        bool foreign = j + 1 < core.size() && R.softVowels.find(core[j + 1]) != std::u32string::npos;  // ふぉ, てぃ
        if (On(RepairOp::dakuten) && !foreign) {
          if (char32_t v = Voiced(base)) Alt(j, v, RepairOp::dakuten, folded, fn);
          if (char32_t v = HalfVoiced(base)) Alt(j, v, RepairOp::dakuten, folded, fn);
        }
      };
      variants(c, false);
      if (On(RepairOp::kataFold) && IsKataLetter(c) && !digraph) {
        Alt(j, Fold(c), RepairOp::kataFold, true, fn);
        variants(Fold(c), true);
      }
    });
  }

  template <class F>
  void Alt(uint32_t j, char32_t to, RepairOp op, bool folded, const F& fn) {
    RepairEdit e;
    e.pos = static_cast<uint16_t>(j);
    e.len = 1;
    e.to = std::u32string(1, to);
    e.op = op;
    int32_t add = Push(e);
    if (add < 0) return;
    if (folded) {
      ++folds;
      if (inStem) ++foldsStem;
    }
    fn(to, j + 1);
    if (folded) {
      --folds;
      if (inStem) --foldsStem;
    }
    Pop(add);
  }

  // One letter: drops first, then the letter read. fn(c, nextJ).
  template <class F>
  void Letter(uint32_t j, const F& fn) {
    if (j > start + 32) return;
    Drop(j, [&](uint32_t j2) { Letter(j2, fn); });
    Read(j, fn);
  }

  // ---- the lexicon walk ----
  void Stem(uint32_t j, uint32_t node) {
    if (node) {
      const auto& nd = m.trie.nodes[node];
      for (uint32_t g = nd.first; g < nd.first + nd.count; ++g) {
        const auto& p = m.paradigms[m.groups[g].paradigm];
        for (uint32_t f = p.first; f < p.first + p.count; ++f) {
          inStem = false;
          Suffix(g, f, 0, j);
          inStem = true;
        }
      }
    }
    Letter(j, [&](char32_t c, uint32_t j2) {
      uint32_t ch = m.trie.Child(node, c);
      if (!ch) return;
      prod.push_back(c);
      Stem(j2, ch);
      prod.pop_back();
    });
    // one edit in a kana word: substitute, insert or drop a kana
    if (On(RepairOp::edit1) && !edited && node && !prod.empty() && Unknown(j)) {
      const auto& nd = m.trie.nodes[node];
      for (uint32_t k = nd.child; k < nd.child + nd.nChild; ++k) {
        char32_t x = m.trie.edgeChar[k];
        if (!IsKana(x)) continue;
        uint32_t ch = m.trie.edgeNode[k];
        if (j < hi && IsKana(core[j]) && x != core[j]) EditStep(j, x, 1, [&](uint32_t j2) { Stem(j2, ch); });
        EditStep(j, x, 0, [&](uint32_t j2) { Stem(j2, ch); });
      }
      if (j < hi && IsKana(core[j])) EditStep(j, 0, 1, [&](uint32_t j2) { Stem(j2, node); });
    }
  }

  // An edit1 step at j: produce x (0 = nothing) after consuming `consume` letters.
  template <class F>
  void EditStep(uint32_t j, char32_t x, uint32_t consume, const F& f) {
    auto go = [&] {
      RepairEdit e;
      e.pos = static_cast<uint16_t>(j);
      e.len = static_cast<uint16_t>(consume);
      if (x) e.to = std::u32string(1, x);
      e.op = RepairOp::edit1;
      int32_t add = Push(e);
      if (add < 0) return;
      edited = true;
      if (x) prod.push_back(x);
      f(j + consume);
      if (x) prod.pop_back();
      edited = false;
      Pop(add);
    };
    if (consume) AtSpace(j, go);
    else go();
  }

  void Suffix(uint32_t g, uint32_t f, uint32_t k, uint32_t j) {
    const auto& pf = m.pforms[f];
    if (k == pf.sufLen) {
      Trailing(g, f, j);
      return;
    }
    char32_t t = m.cps[pf.suf + k];
    Letter(j, [&](char32_t c, uint32_t j2) {
      if (c != t) return;
      prod.push_back(c);
      Suffix(g, f, k + 1, j2);
      prod.pop_back();
    });
    if (On(RepairOp::edit1) && !edited && !prod.empty() && IsKana(t) && Unknown(j)) {
      if (j < hi && IsKana(core[j]) && core[j] != t) EditStep(j, t, 1, [&](uint32_t j2) { Suffix(g, f, k + 1, j2); });
      EditStep(j, t, 0, [&](uint32_t j2) { Suffix(g, f, k + 1, j2); });
      if (j < hi && IsKana(core[j])) EditStep(j, 0, 1, [&](uint32_t j2) { Suffix(g, f, k, j2); });
    }
  }

  // The word ends at j; it may also swallow the drops that follow it (部屋|屋屋, 幼なじみ|ー).
  void Trailing(uint32_t g, uint32_t f, uint32_t j) {
    Emit(g, f, j);
    Drop(j, [&](uint32_t j2) { Trailing(g, f, j2); });
  }

  // A hiragana letter before a katakana letter inside the span (すグ, れル); katakana then hiragana is a
  // katakana stem with its inflection (バカな, ヤバい).
  bool SpanMixed(uint32_t b, uint32_t e) const {
    bool hira = false;
    for (uint32_t k = b; k < e; ++k) {
      if (IsHiragana(core[k])) hira = true;
      else if (hira && IsKataLetter(core[k])) return true;
    }
    return false;
  }

  // The katakana run around the span touches hiragana or kanji: a katakana word inside running text
  // (音ガした, すグニ), not a katakana stretch of its own (ンッ ンッ).
  bool InText(uint32_t b, uint32_t e) const {
    auto txt = [&](char32_t c) { return IsHiragana(c) || IsKanji(c); };
    uint32_t l = b, r = e;
    while (l > 0 && !ws.spaceBefore[l] && IsKatakana(core[l - 1])) --l;
    while (r < core.size() && !ws.spaceBefore[r] && IsKatakana(core[r])) ++r;
    bool before = l > 0 && !ws.spaceBefore[l] && txt(core[l - 1]);
    bool after = r < core.size() && !ws.spaceBefore[r] && txt(core[r]);
    return before || after || (l < b && txt(core[b])) || SpanHasText(b, e);
  }
  bool SpanHasText(uint32_t b, uint32_t e) const {
    for (uint32_t k = b; k < e; ++k)
      if (IsHiragana(core[k]) || IsKanji(core[k])) return true;
    return false;
  }

  // every folded letter is a katakana run of one letter (音ガした, 親ノ過剰): longer runs read as words
  bool OneLetterRuns() const {
    for (auto& e : edits) {
      if (e.spaceOnly || e.len != 1 || !IsKataLetter(core[e.pos])) continue;
      bool before = e.pos > 0 && !ws.spaceBefore[e.pos] && IsKatakana(core[e.pos - 1]);
      bool after = e.pos + 1u < core.size() && !ws.spaceBefore[e.pos + 1] && IsKatakana(core[e.pos + 1]);
      if (before || after) return false;
    }
    return true;
  }

  bool AllHiragana(uint32_t b, uint32_t e) const {
    for (uint32_t k = b; k < e; ++k)
      if (!IsHiragana(core[k])) return false;
    return e > b;
  }

  // The fold of a katakana run counts once: a word whose folds start at its first letter, right after a
  // katakana letter of the same run, continues the fold of the word before it.
  bool FoldContinues() const {
    for (auto& e : edits)
      if (e.op == RepairOp::kataFold || (e.len == 1 && !e.to.empty() && IsHiragana(e.to[0]) && IsKataLetter(core[e.pos])))
        return e.pos == start && start > 0 && !ws.spaceBefore[start] && IsKataLetter(core[start - 1]);
    return false;
  }

  void Emit(uint32_t g, uint32_t f, uint32_t j) {
    int32_t total = cost + (folds && !FoldContinues() ? Cost(RepairOp::kataFold) : 0);
    bool joined = false, other = folds > 0;
    for (auto& e : edits) (e.spaceOnly ? joined : other) = true;
    if (joined && other) return;  // a space is joined only inside an otherwise exact word
    // a restored dakuten makes a one-letter word only of the voiced function words (か → が, た → だ);
    // は stays は
    if (prod.size() == 1)
      for (auto& e : edits)
        if (e.op == RepairOp::dakuten && R.voicedWords.find(prod[0]) == std::u32string::npos) return;
    bool touches = touchE > touchB && start < touchE && j > touchB;
    if (edits.empty() && !touches) return;  // the plain word is in the lattice already
    if (total > R.arcBudget || j <= start) return;
    if (g < R.fallbackGroup.size() && R.fallbackGroup[g]) return;  // a repaired reading uses core words only
    const auto& grp = m.groups[g];
    const auto& pf = m.pforms[f];
    std::string_view ud = m.udNames[m.classes[pf.cls].ud];
    if (folds) {
      // katakana read as hiragana: function words, inflections, or a word spelt in both scripts
      // a one-letter katakana particle or auxiliary right after a kanji (音ガした, 親ノ過剰, 部屋デ), or the
      // katakana inflection of a word with a kanji stem (考エて)
      bool afterWord = start > 0 && !ws.spaceBefore[start] && IsKanji(core[start - 1]);
      bool ok = (HasPrefix(R.functionUd, ud) && afterWord && OneLetterRuns()) || (foldsStem == 0 && IsKanji(core[start]));
      // a katakana ending before punctuation is a spelling (見ロ」, だヨ。), not noise
      if (ok && IsKataLetter(core[j - 1]) && (j >= core.size() || ws.spaceBefore[j] || CcName(core[j]) == "sym")) ok = false;
      if (!ok) return;
      // a katakana run that is a word as written is read as hiragana only as one particle or auxiliary
      // (カラ → から), never split (ダメ is not だ + め) nor as another word (サル is not さる)
      if (wordRun)
        for (auto& e : edits) {
          uint32_t wr = (*wordRun)[e.pos];
          if (wr == UINT32_MAX || e.spaceOnly || !IsKataLetter(core[e.pos])) continue;
          bool whole = (wr >> 16) == start && (wr & 0xFFFF) == j;
          if (!whole || !HasPrefix(R.foldWordUd, ud)) return;
        }
    }
    if (edited) {
      int minLen = R.ops[static_cast<size_t>(RepairOp::edit1)].minLen;
      if (static_cast<int>(prod.size()) < minLen || HasPrefix(R.noEditUd, ud)) return;
      if (!AllHiragana(start, j)) return;  // never on katakana words: names and loanwords
      for (uint32_t k = start; k < j; ++k)
        if (IsSmall(core[k])) return;  // nor on sound words (びゅるっ)
      for (char32_t c : prod)
        if (!IsKana(c)) return;
    }
    std::string key = std::to_string(start) + "," + std::to_string(j) + "," + std::to_string(g) + "," +
                      std::to_string(f) + "|" + ToUtf8(prod);
    auto it = seen.find(key);
    if (it != seen.end()) {
      LArc& old = (*arcs)[it->second];
      if (old.rep <= total) return;
      (*rarcs)[static_cast<size_t>(old.rarc)].edits = edits;
      (*rarcs)[static_cast<size_t>(old.rarc)].cost = total;
      old.rep = total;
      return;
    }
    RepairArc ra;
    ra.group = g;
    ra.form = f;
    ra.text = prod;
    ra.edits = edits;
    ra.cost = total;
    LArc la;
    la.arc.b = static_cast<uint16_t>(start);
    la.arc.e = static_cast<uint16_t>(j);
    la.arc.lcls = la.arc.rcls = pf.cls;
    la.arc.cost = grp.cost;
    la.arc.kind = ArcKind::repair;
    la.arc.flags = Repaired;
    la.rarc = static_cast<int32_t>(rarcs->size());
    la.rep = total;
    rarcs->push_back(std::move(ra));
    seen.emplace(std::move(key), arcs->size());
    arcs->push_back(std::move(la));
  }

  void From(uint32_t i) {
    start = i;
    prod.clear();
    edits.clear();
    cost = 0;
    folds = foldsStem = 0;
    edited = false;
    inStem = true;
    Stem(i, 0);
    // a word right after a stray space may take the space in (並ん で → 並んで, ある 。 → ある。)
    if (i > 0 && ws.spaceBefore[i] && On(RepairOp::space) && !layout && (!single || (*single)[i])) {
      RepairEdit e;
      e.pos = static_cast<uint16_t>(i);
      e.op = RepairOp::space;
      e.spaceOnly = true;
      int32_t add = Push(e);
      if (add >= 0) {
        Stem(i, 0);
        Pop(add);
      }
    }
  }
};

// ---- a span read again ---------------------------------------------------------------------------------
struct Key {
  int32_t bad = 0, rep = 0;
  int64_t cost = 0;
  bool operator<(const Key& o) const {
    if (bad != o.bad) return bad < o.bad;
    if (rep != o.rep) return rep < o.rep;
    return cost < o.cost;
  }
};
constexpr Key kNoKey{1 << 28, 1 << 28, 0};

struct Window {
  uint32_t b = 0, e = 0;   // core range
  int32_t left = -1, right = -1;  // path arcs around it (ws.arcs index), -1 = edge
};

struct Best {
  Key key = kNoKey;
  std::vector<LArc> arcs;
  std::vector<RepairArc> rarcs;
  std::vector<uint32_t> path;  // into arcs
  int32_t swapAt = -1;
  std::vector<char32_t> core;
};

class Reader {
 public:
  Reader(const Model& m, const RepairRules& R, Workspace* ws, const Options& opt) : m_(m), R_(R), ws_(ws), opt_(opt) {
    // a line with two or more spaces between Japanese letters is laid out with spaces (俺 の 前 に): its
    // spaces are separators, not strays
    int inner = 0;
    const auto& core = ws_->core;
    auto jp = [](char32_t c) { return IsHiragana(c) || IsKatakana(c) || IsKanji(c); };
    for (size_t k = 1; k < core.size(); ++k)
      if (ws_->spaceBefore[k] && jp(core[k - 1]) && jp(core[k])) ++inner;
    layout_ = inner >= 2;
    // katakana runs that the dictionary has as written (ダメ, カラ): read as hiragana only as a whole
    wordRun_.assign(core.size() + 1, UINT32_MAX);
    for (uint32_t l = 0; l < core.size();) {
      if (!IsKataLetter(core[l]) || (l > 0 && !ws_->spaceBefore[l] && IsKatakana(core[l - 1]))) {
        ++l;
        continue;
      }
      uint32_t r = l + 1;
      while (r < core.size() && !ws_->spaceBefore[r] && IsKatakana(core[r])) ++r;
      bool word = false;
      for (uint32_t k = ws_->beginStart[l]; k < ws_->beginStart[l + 1] && !word; ++k) {
        const Arc& a = ws_->arcs[ws_->byBegin[k]];
        word = a.e == r && a.removedBy < 0 && (a.kind == ArcKind::lex || a.kind == ArcKind::longUnit);
      }
      if (word && r - l >= 2)
        for (uint32_t x = l; x < r; ++x) wordRun_[x] = l << 16 | r;
      l = r;
    }
    // katakana runs of three letters or more (ー not counted) are names and loanwords: no letter of
    // theirs is read otherwise (タナトス, フィーナ, パタパタ); only a doubled ー may drop (ドーーア)
    longKata_.assign(core.size() + 1, 0);
    for (uint32_t l = 0; l < core.size();) {
      if (!IsKatakana(core[l])) {
        ++l;
        continue;
      }
      uint32_t r = l, letters = 0;
      while (r < core.size() && (r == l || !ws_->spaceBefore[r]) && IsKatakana(core[r])) letters += core[r++] != U'ー';
      bool small = false;
      for (uint32_t x = l; x < r; ++x) small = small || IsSmall(core[x]);
      bool spoken = IsLongMark(core[l]) || IsSokuon(core[l]);
      bool censored = (l > 0 && R_.censor.find(core[l - 1]) != std::u32string::npos) ||
                      (r < core.size() && R_.censor.find(core[r]) != std::u32string::npos);
      if (letters >= 3 || small || spoken || censored)
        for (uint32_t x = l; x < r; ++x) longKata_[x] = 1;
      l = r;
    }
    // unknown words of the current reading; a katakana run read as one unknown word is a name or a loanword
    unknown_.assign(core.size() + 1, 0);
    for (uint32_t ai : ws_->path) {
      const Arc& a = ws_->arcs[ai];
      if (a.kind != ArcKind::unk) continue;
      bool kata = a.e - a.b >= 2;
      for (uint32_t x = a.b; x < a.e; ++x) {
        unknown_[x] = 1;
        kata = kata && IsKatakana(core[x]);
      }
      if (kata)
        for (uint32_t x = a.b; x < a.e; ++x) longKata_[x] = 1;
    }
    // stray spaces are single: a run of two or more is layout (＊　＊ 　＊)
    single_.assign(core.size() + 1, 0);
    for (size_t k = 1; k < core.size(); ++k)
      if (ws_->spaceBefore[k]) single_[k] = ws_->coreN[k] - ws_->coreN[k - 1] == 2;
    // runs of one letter three times or more inside running text: kanji, and hiragana but vowels, ん and
    // っ (ああああ, んんっ) and laughs after a vowel or at the start (あははは, ふふふ); katakana runs are
    // sound words (ドドド)
    triple_.assign(core.size() + 1, 0);
    dup_.assign(core.size() + 1, 0);
    for (size_t p = 0; p + 2 < core.size(); ++p) {
      char32_t c = core[p];
      if (core[p + 1] != c || core[p + 2] != c || ws_->spaceBefore[p + 1] || ws_->spaceBefore[p + 2]) continue;
      if (p > 0 && core[p - 1] == c) continue;  // counted from the run's first letter
      bool kana = IsHiragana(c);
      // a katakana letter tripled inside a katakana word (インターホホホン); a run that opens the word is a
      // sound or a laugh (ドドド, フフフ)
      bool kataIn = IsKataLetter(c) && !IsSmall(c) && p > 0 && !ws_->spaceBefore[p] && IsKatakana(core[p - 1]) &&
                    core[p - 1] != U'ー';
      if (!kana && !IsKanji(c) && !kataIn) continue;
      if ((kana || kataIn) && R_.tripleFree.find(Fold(c)) != std::u32string::npos) continue;
      bool laugh = R_.laughs.find(Fold(c)) != std::u32string::npos;
      if (laugh && p > 0 && IsKana(core[p - 1])) continue;  // あははは, キャハハハ, ぶっははは
      bool atStart = p == 0 || ws_->spaceBefore[p] || !(IsHiragana(core[p - 1]) || IsKanji(core[p - 1]) || IsKatakana(core[p - 1]));
      if (laugh && (atStart || IsVowelLetter(core[p - 1]) || IsSokuon(core[p - 1]))) continue;  // ぶっははは
      if (atStart && kana) continue;  // す、すすすみません; ななな なんで; だだだ、駄目: a stutter that opens a phrase
      size_t q = p;
      while (q < core.size() && core[q] == c) ++q;
      // before a cry mark or the end it is a cry (いたたたた！, うぬぬぬ……); before 、 or 。 it is noise
      if (q >= core.size() || ws_->spaceBefore[q] || R_.cryMarks.find(core[q]) != std::u32string::npos) continue;
      // a cry that ends in っ or ー (いたたたたっ！) or a sound word before the quoting と (とてててと)
      size_t r = q;
      while (r < core.size() && (IsSokuon(core[r]) || IsLongMark(core[r]))) ++r;
      if (r > q && (r >= core.size() || ws_->spaceBefore[r] || R_.cryMarks.find(core[r]) != std::u32string::npos)) continue;
      if (kana && R_.soundEnd.find(core[q]) != std::u32string::npos) continue;
      if (kataIn)
        for (size_t x = p + 1; x < q; ++x) dup_[x] = 1;
      triple_[p + 2] = 1;
    }
    // a comma written twice or more (、、、), and brackets repeated past their partners (「「「なんで…」)
    for (size_t p = 1; p < core.size(); ++p)
      if (core[p] == core[p - 1] && R_.repeatMarks.find(core[p]) != std::u32string::npos && !ws_->spaceBefore[p] &&
          R_.brackets.find(core[p]) == std::u32string::npos)
        triple_[p] = 3;
    for (size_t b = 0; b + 1 < R_.brackets.size(); b += 2) {
      char32_t open = R_.brackets[b], close = R_.brackets[b + 1];
      const auto opens = std::count(core.begin(), core.end(), open), closes = std::count(core.begin(), core.end(), close);
      char32_t extra = opens > closes ? open : close;
      auto excess = opens > closes ? opens - closes : closes - opens;
      for (size_t p = 1; p < core.size() && excess > 0; ++p)
        if (core[p] == extra && core[p - 1] == extra && !ws_->spaceBefore[p]) {
          triple_[p] = 3;
          --excess;
        }
    }
    // っ before a vowel, a small kana, ん, ー, っ or a letter of the な, ま, や, ら and わ rows: no word is
    // spelt so (じっゃ, うっい, 奪いっに, わかっった)
    for (size_t p = 1; p + 1 < core.size(); ++p) {
      if (!IsSokuon(core[p]) || ws_->spaceBefore[p + 1] || !IsKana(core[p - 1]) || ws_->spaceBefore[p]) continue;
      if (IsSokuon(core[p - 1])) continue;  // a run of っ is judged from its first
      size_t q = p;
      while (q < core.size() && IsSokuon(core[q]) && (q == p || !ws_->spaceBefore[q])) ++q;
      if (q >= core.size() || ws_->spaceBefore[q] || !IsKana(core[q])) continue;  // っっ before a mark: a cry
      if (q - p >= 2) continue;  // おっっっはよ: a run of っ is emphasis
      if (R_.notAfterSokuon.find(core[p + 1]) != std::u32string::npos && !triple_[p]) triple_[p] = 2;
    }
    FindCrossable();
  }

  // Spaces between two Japanese letters inside a word: with the space dropped, the analyzer's own best
  // reading has a word over it (だったら しい: らしい; 実 力不足: 実力; ど うですか: どう). A reading that
  // keeps such a space splits a word (rp.bad.space-in-word).
  void FindCrossable() {
    size_t n = ws_->core.size();
    cross_.assign(n + 1, 0);  // 0 not judged yet, 1 no, 2 a word spans the space
    crossOn_ = !R_.spaceInWord.empty() && R_.ops[static_cast<size_t>(RepairOp::space)].on && !layout_;
    if (!crossOn_) return;
    after_.assign(n + 1, 0);
    afterFunction_.assign(n + 1, 0);
    beforeContent_.assign(n + 1, 0);
    for (uint32_t ai : ws_->path) {
      const Arc& a = ws_->arcs[ai];
      uint16_t cls = a.kind == ArcKind::lex ? m_.pforms[a.form].cls : a.lcls;
      if (a.kind == ArcKind::longUnit) continue;
      std::string_view ud = m_.udNames[m_.classes[cls].ud];
      if (HasPrefix(R_.pauseUd, ud) && a.e - a.b >= 2) after_[a.e] = 1;
      if (HasPrefix(R_.phraseEndUd, ud)) afterFunction_[a.e] = 1;
      else if (!HasPrefix(R_.functionUd, ud)) beforeContent_[a.b] = 1;
    }
  }

  // Whether a dictionary word spans the space before core position k (judged once, when first asked).
  bool Cross(uint32_t k) const {
    if (!crossOn_ || k == 0 || k >= cross_.size()) return false;
    if (cross_[k]) return cross_[k] == 2;
    cross_[k] = 1;
    size_t n = ws_->core.size();
    const auto& core = ws_->core;
    auto jp = [](char32_t c) { return IsHiragana(c) || IsKatakana(c) || IsKanji(c); };
    if (!ws_->spaceBefore[k] || !single_[k] || ws_->joinable[k] || !jp(core[k - 1]) || !jp(core[k])) return false;
    if (after_[k]) return false;  // ああ あっし, よ そして: a pause after an interjection or a final particle
    if (afterFunction_[k] && beforeContent_[k]) return false;  // そばに いて: a space between a particle and a content word parts phrases
    // the analyzer's reading of the text around the space (twelve letters each side) with the space dropped
    const uint32_t lo = k > 12 ? k - 12 : 0, hi = std::min<uint32_t>(static_cast<uint32_t>(n), k + 12);
    Workspace tmp;
    for (uint32_t x = ws_->coreN[lo]; x <= ws_->coreN[hi - 1]; ++x)
      if (x <= ws_->coreN[k - 1] || x >= ws_->coreN[k]) tmp.nchars.push_back(ws_->nchars[x]);
    BuildLattice(m_, &tmp, opt_);
    RunViterbi(m_, &tmp, false);
    // a spaced repeat or stutter (はい はい, ハァ ハァ, ど どうした) keeps its space
    for (uint32_t ai : tmp.path) {
      const Arc& t = tmp.arcs[ai];
      uint32_t b = t.b + lo, e = t.e + lo;
      if (b >= k || e <= k || t.kind != ArcKind::lex) continue;  // a long unit is words (もう ちょっと)
      if (k - b <= e - k && std::equal(core.begin() + b, core.begin() + k, core.begin() + k)) continue;
      bool one = true;  // one letter repeated (あああ)
      for (uint32_t x = b + 1; x < e; ++x) one = one && core[x] == core[b];
      if (one) continue;
      cross_[k] = 2;
    }
    return cross_[k] == 2;
  }

  bool LongKata(uint32_t p) const { return p < longKata_.size() && longKata_[p]; }

  // The analyzer's reading of the span has a word outside the core lexicon (fallback or unknown, in Japanese
  // letters): it is compared with repaired readings only (prefs: fewer fallback words, then repair cost).
  bool LexicalTrigger(const std::vector<uint32_t>& pathArcs) const {
    if (R_.fallbackGroup.empty()) return false;
    Facts facts{m_, R_, *ws_, ws_->core, layout_, &single_, &wordRun_, &triple_};
    for (uint32_t ai : pathArcs) {
      const Arc& a = ws_->arcs[ai];
      const RepairArc* ra = a.kind == ArcKind::repair ? &ws_->rarcs[a.ref] : nullptr;
      Info x = facts.Of(a, ra);
      if (x.elong != "none" || !x.derive.empty()) continue;  // spoken forms (すごーい) are not lexical noise
      if (x.tier == "fallback" || (x.tier == "unknown" && x.script != "sym" && x.script != "other")) return true;
    }
    return false;
  }

  // A kept space a dictionary word spans, before core position p (not at the edge of the span read).
  int SpaceBad(uint32_t p, const RepairArc* next) const {
    if (!Cross(p)) return 0;
    if (next)
      for (auto& e : next->edits)
        if (e.spaceOnly && e.pos == p) return 0;
    return 1;
  }

  // The best reading of window w over `core` (the line's text, or with one swap at swapAt).
  void Read(const Window& w, const std::vector<char32_t>& core, int32_t swapAt, Best* best, std::string* trace = nullptr,
            bool mustRepair = false) {
    std::vector<LArc> arcs;
    std::vector<RepairArc> rarcs;
    Facts facts{m_, R_, *ws_, core, layout_, &single_, &wordRun_, &triple_};
    const auto& A = ws_->arcs;
    uint32_t tb = swapAt >= 0 ? static_cast<uint32_t>(swapAt) : 0, te = swapAt >= 0 ? tb + 2 : 0;
    for (uint32_t pos = w.b; pos < w.e; ++pos)
      for (uint32_t k = ws_->beginStart[pos]; k < ws_->beginStart[pos + 1]; ++k) {
        uint32_t ai = ws_->byBegin[k];
        const Arc& a = A[ai];
        if (a.e > w.e || a.kind == ArcKind::repair) continue;
        if (te > tb && a.b < te && a.e > tb) continue;
        LArc la;
        la.arc = a;
        la.ws = static_cast<int32_t>(ai);
        arcs.push_back(std::move(la));
      }
    Gen gen(m_, R_, *ws_, core, w.e, tb, te, &rarcs, &arcs);
    gen.layout = layout_;
    gen.single = &single_;
    gen.wordRun = &wordRun_;
    gen.longKata = &longKata_;
    gen.unknown = &unknown_;
    gen.dup = &dup_;
    for (uint32_t i = w.b; i < w.e; ++i) gen.From(i);
    for (auto& la : arcs) {
      la.info = facts.Of(la.arc, la.rarc >= 0 ? &rarcs[static_cast<size_t>(la.rarc)] : nullptr);
      la.bad = TokBad(R_, la.info, nullptr);
    }
    Info leftInfo = Edge(w.left, facts), rightInfo = Edge(w.right, facts);
    uint16_t leftCls = w.left >= 0 ? A[static_cast<size_t>(w.left)].rcls : 0;
    uint16_t rightCls = w.right >= 0 ? A[static_cast<size_t>(w.right)].lcls : 0;
    int32_t swapCost = swapAt >= 0 ? R_.ops[static_cast<size_t>(RepairOp::swap)].cost : 0;

    // dynamic programme over the candidates in start order; state = arc * 2 + (a repaired word or the swap on
    // the path). With mustRepair the reading is compared with the analyzer's own reading only, so another
    // unrepaired reading never stands in for it (ご|さ|い|ます for ごさいます).
    std::vector<uint32_t> order(arcs.size());
    for (uint32_t k = 0; k < order.size(); ++k) order[k] = k;
    std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) { return arcs[x].arc.b < arcs[y].arc.b; });
    std::vector<std::vector<uint32_t>> endAt(w.e + 1);
    std::vector<Key> f(arcs.size() * 2, kNoKey);
    std::vector<int32_t> bp(arcs.size() * 2, -1);
    const uint32_t startU = swapAt >= 0 ? 1 : 0;
    for (uint32_t x : order) {
      LArc& a = arcs[x];
      // a repaired word must itself be a valid reading
      if (a.rarc >= 0 && a.bad) continue;
      const uint32_t own = a.rarc >= 0 ? 1 : 0;
      Key bk[2] = {kNoKey, kNoKey};
      int32_t arg[2] = {-1, -1};
      auto consider = [&](const Info& li, uint16_t lcls, Key base, int32_t from, bool repairedLeft, uint32_t u) {
        if (base.bad >= kNoKey.bad) return;
        int pb = PairBad(R_, li, a.info, nullptr);
        if (pb && (a.rarc >= 0 || repairedLeft)) return;
        Key k = base;
        k.bad += pb + a.bad;
        k.rep += a.rep;
        k.cost += m_.Conn(lcls, a.arc.lcls) + a.arc.cost;
        uint32_t v = u | own;
        if (k < bk[v]) {
          bk[v] = k;
          arg[v] = from;
        }
      };
      if (a.arc.b == w.b) consider(leftInfo, leftCls, Key{0, swapCost, 0}, -1, false, startU);
      int sb = a.arc.b > w.b ? SpaceBad(a.arc.b, a.rarc >= 0 ? &rarcs[static_cast<size_t>(a.rarc)] : nullptr) : 0;
      for (uint32_t ys : endAt[a.arc.b]) {
        uint32_t y = ys / 2;
        Key base = f[ys];
        if (base.bad < kNoKey.bad) base.bad += sb;
        consider(arcs[y].info, arcs[y].arc.rcls, base, static_cast<int32_t>(ys), arcs[y].rarc >= 0, ys % 2);
      }
      for (uint32_t v = 0; v < 2; ++v) {
        f[x * 2 + v] = bk[v];
        bp[x * 2 + v] = arg[v];
        if (bk[v].bad < kNoKey.bad) endAt[a.arc.e].push_back(x * 2 + v);
      }
    }
    Key fin = kNoKey;
    int32_t last = -1;
    for (uint32_t ys : endAt[w.e]) {
      uint32_t y = ys / 2;
      if (mustRepair && ys % 2 == 0) continue;
      int pb = PairBad(R_, arcs[y].info, rightInfo, nullptr);
      if (pb && arcs[y].rarc >= 0) continue;
      Key k = f[ys];
      k.bad += pb;
      k.cost += m_.Conn(arcs[y].arc.rcls, rightCls);
      if (k < fin) {
        fin = k;
        last = static_cast<int32_t>(ys);
      }
    }
    if (trace) {
      // --why at level 2: the best reading of this variant and the repaired words tried
      *trace += "    read" + (swapAt >= 0 ? " swap@" + std::to_string(swapAt) : std::string()) + ": bad " +
                std::to_string(fin.bad) + " cost " + std::to_string(fin.rep) + " |";
      for (int32_t x = last; x >= 0; x = bp[static_cast<size_t>(x)]) {
        const Info& in = arcs[static_cast<size_t>(x) / 2].info;
        *trace += " " + in.surf + "[" + std::string(in.ud) + (in.cform.empty() ? "" : " " + std::string(in.cform)) + " " +
                  std::string(in.lemma) + "]";
      }
      *trace += " (right to left)\n";
      size_t shown = 0;
      for (auto& la : arcs)
        if (la.rarc >= 0 && shown++ < 40)
          *trace += "      try " + std::to_string(la.arc.b) + "-" + std::to_string(la.arc.e) + " " + la.info.surf + " [" +
                    std::string(la.info.ud) + "] cost " + std::to_string(la.rep) + " bad " + std::to_string(la.bad) + "\n";
    }
    if (last < 0 || !(fin < best->key)) return;
    if (swapAt >= 0 && fin.bad > 0) return;  // a transposition must make the span a valid reading
    std::vector<uint32_t> path;
    for (int32_t x = last; x >= 0; x = bp[static_cast<size_t>(x)]) path.push_back(static_cast<uint32_t>(x) / 2);
    std::reverse(path.begin(), path.end());
    if (swapAt >= 0 && !SwapOk(arcs, rarcs, path, static_cast<uint32_t>(swapAt))) return;
    best->key = fin;
    best->arcs = std::move(arcs);
    best->rarcs = std::move(rarcs);
    best->path = std::move(path);
    best->swapAt = swapAt;
    best->core = core;
  }

  // The current reading of the window, scored the same way.
  Key Score(const Window& w, const std::vector<uint32_t>& pathArcs, std::string* why) const {
    Facts facts{m_, R_, *ws_, ws_->core, layout_, &single_, &wordRun_, &triple_};
    const auto& A = ws_->arcs;
    Info prev = Edge(w.left, facts);
    Key k;
    for (uint32_t ai : pathArcs) {
      const RepairArc* ra = A[ai].kind == ArcKind::repair ? &ws_->rarcs[A[ai].ref] : nullptr;
      Info x = facts.Of(A[ai], ra);
      k.bad += TokBad(R_, x, why) + PairBad(R_, prev, x, why);
      if (A[ai].b > w.b && SpaceBad(A[ai].b, ra)) {
        ++k.bad;
        if (why) *why += " " + R_.spaceInWord + "(" + prev.surf + "␣" + x.surf + ")";
      }
      prev = std::move(x);
    }
    k.bad += PairBad(R_, prev, Edge(w.right, facts), why);
    return k;
  }

  // Which path arcs a check fires on (tokens and transitions, the right edge included).
  void Marks(std::vector<uint8_t>* mark) const {
    Facts facts{m_, R_, *ws_, ws_->core, layout_, &single_, &wordRun_, &triple_};
    const auto& A = ws_->arcs;
    const auto& P = ws_->path;
    mark->assign(P.size(), 0);
    Info prev;
    for (size_t i = 0; i < P.size(); ++i) {
      Info x = facts.Of(A[P[i]], nullptr);
      if (TokBad(R_, x, nullptr, true)) (*mark)[i] = 1;
      if (PairBad(R_, prev, x, nullptr, true) || (i > 0 && SpaceBad(A[P[i]].b, nullptr))) {
        (*mark)[i] = 1;
        if (i > 0) (*mark)[i - 1] = 1;
      }
      prev = std::move(x);
    }
    if (!P.empty() && PairBad(R_, prev, Info{}, nullptr, true)) mark->back() = 1;
  }

 private:
  Info Edge(int32_t ai, const Facts& facts) const {
    if (ai < 0) return Info{};
    const Arc& a = ws_->arcs[static_cast<size_t>(ai)];
    const RepairArc* ra = a.kind == ArcKind::repair ? &ws_->rarcs[a.ref] : nullptr;
    return facts.Of(a, ra);
  }

  // A swap holds inside one word of minlen letters or more, or inside function words and inflections.
  bool SwapOk(const std::vector<LArc>& arcs, const std::vector<RepairArc>& rarcs, const std::vector<uint32_t>& path,
              uint32_t p) const {
    int minLen = R_.ops[static_cast<size_t>(RepairOp::swap)].minLen;
    const LArc* cover[2] = {nullptr, nullptr};
    for (uint32_t x : path)
      for (uint32_t k = 0; k < 2; ++k)
        if (arcs[x].arc.b <= p + k && p + k < arcs[x].arc.e) cover[k] = &arcs[x];
    if (!cover[0] || !cover[1]) return false;
    if (cover[0] == cover[1] && cover[0]->info.len >= minLen) return true;
    for (uint32_t k = 0; k < 2; ++k) {
      const LArc& a = *cover[k];
      if (!a.info.content) continue;
      // inside the inflection of a verb or adjective: after its stem
      uint32_t stem = 0;
      if (a.arc.kind == ArcKind::lex) stem = m_.groups[a.arc.ref].stemLen;
      else if (a.rarc >= 0) stem = m_.groups[rarcs[static_cast<size_t>(a.rarc)].group].stemLen;
      else return false;
      bool infl = std::find(R_.inflecting.begin(), R_.inflecting.end(), a.info.pos) != R_.inflecting.end();
      if (!infl || p + k < a.arc.b + stem) return false;
    }
    return true;
  }

  const Model& m_;
  const RepairRules& R_;
  Workspace* ws_;
  Options opt_;
  mutable std::vector<uint8_t> cross_;
  std::vector<uint8_t> single_, triple_, dup_, after_, afterFunction_, beforeContent_;
  bool crossOn_ = false;
  std::vector<uint32_t> wordRun_;
  std::vector<uint8_t> longKata_, unknown_;
  bool layout_ = false;
};

bool HardSymbol(const Model& m, const RepairRules& R, const Arc& a) {
  uint16_t cls = a.kind == ArcKind::lex ? m.pforms[a.form].cls : a.lcls;
  if (a.kind == ArcKind::longUnit) return false;
  return HasPrefix(R.stopUd, m.udNames[m.classes[cls].ud]);
}

}  // namespace

bool ParseRepairTsv(std::string_view text, RepairRules* out, std::string* err) {
  *out = RepairRules{};
  for (size_t k = 0; k < static_cast<size_t>(RepairOp::count); ++k) out->ops[k].id.clear();
  bool header = false;
  size_t lineNo = 0;
  for (auto& raw : SplitOn(std::string(text), '\n')) {
    ++lineNo;
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (!header) {
      header = true;
      continue;
    }
    auto f = SplitOn(line, '\t');
    if (f.size() < 6) {
      *err = "repair.tsv line " + std::to_string(lineNo) + ": too few columns";
      return false;
    }
    const std::string &id = f[0], &kind = f[2];
    if (kind == "param") {
      std::string name = id.rfind("rp.param.", 0) == 0 ? id.substr(9) : id;
      auto list = SplitOn(Trim(f[4]), '|');
      std::u32string letters = ToU32(Trim(f[4]));
      if (name == "function-ud") out->functionUd = list;
      else if (name == "fold-word-ud") out->foldWordUd = list;
      else if (name == "no-edit-ud") out->noEditUd = list;
      else if (name == "stop-ud") out->stopUd = list;
      else if (name == "pause-ud") out->pauseUd = list;
      else if (name == "phrase-end-ud") out->phraseEndUd = list;
      else if (name == "censor-marks") out->censor = letters;
      else if (name == "core-ud") out->coreUd = list;
      else if (name == "digraphs") out->digraphs = letters;
      else if (name == "inflecting") out->inflecting = list;
      else if (name == "sound-end") out->soundEnd = letters;
      else if (name == "voiced-words") out->voicedWords = letters;
      else if (name == "brackets") out->brackets = letters;
      else if (name == "cry-marks") out->cryMarks = letters;
      else if (name == "soft-vowels") out->softVowels = letters;
      else if (name == "repeat-marks") out->repeatMarks = letters;
      else if (name == "triple-free") out->tripleFree = letters;
      else if (name == "laughs") out->laughs = letters;
      else if (name == "not-after-sokuon") out->notAfterSokuon = letters;
      else {
        *err = "repair.tsv " + id + ": unknown param";
        return false;
      }
      continue;
    }
    if (kind == "bad.space") {
      out->spaceInWord = id;
      continue;
    }
    if (kind == "bad.tok" || kind == "bad.pair" || kind == "valid.tok" || kind == "valid.pair") {
      RepairRules::Check c;
      c.id = id;
      c.pair = kind == "bad.pair" || kind == "valid.pair";
      c.trigger = kind[0] == 'b';
      std::string ce;
      if (!ParseConj(f[4], c.pair, &c.when, &ce)) {
        *err = "repair.tsv " + id + ": " + ce;
        return false;
      }
      std::string req = f[5];
      if (Trim(req) != "-" && !Trim(req).empty()) {
        size_t s = 0;
        while (true) {
          size_t p = req.find("||", s);
          RepairRules::Conj conj;
          if (!ParseConj(req.substr(s, p == std::string::npos ? std::string::npos : p - s), c.pair, &conj, &ce)) {
            *err = "repair.tsv " + id + ": " + ce;
            return false;
          }
          c.require.push_back(std::move(conj));
          if (p == std::string::npos) break;
          s = p + 2;
        }
      }
      for (auto& a : c.when) c.space = c.space || a.field == fSpace;
      for (auto& conj : c.require)
        for (auto& a : conj) c.space = c.space || a.field == fSpace;
      (c.pair ? out->pair : out->tok).push_back(std::move(c));
      continue;
    }
    size_t op = std::size(kOps);
    for (size_t k = 0; k < std::size(kOps); ++k)
      if (kOps[k] == kind) op = k;
    if (op == std::size(kOps)) {
      *err = "repair.tsv " + id + ": unknown kind " + kind;
      return false;
    }
    auto& o = out->ops[op];
    o.id = id;
    o.on = true;
    o.cost = std::atoi(f[3].c_str());
    for (auto& kv : SplitOn(f[4], '&')) {
      std::string x = Trim(kv);
      if (x.rfind("minlen=", 0) == 0) o.minLen = std::atoi(x.c_str() + 7);
    }
  }
  if (!header) {
    *err = "repair.tsv: no header";
    return false;
  }
  return true;
}

void SetCoreKeys(const Model& m, std::string_view keys, RepairRules* r) {
  std::unordered_map<std::string, uint8_t> set;
  for (size_t at = 0; at < keys.size();) {
    size_t nl = keys.find('\n', at);
    std::string k(keys.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at));
    if (!k.empty() && k.back() == '\r') k.pop_back();
    if (!k.empty()) set.emplace(std::move(k), 1);
    at = nl == std::string_view::npos ? keys.size() : nl + 1;
  }
  r->fallbackGroup.assign(m.groups.size(), 1);
  for (size_t g = 0; g < m.groups.size(); ++g) {
    const auto& grp = m.groups[g];
    if (HasPrefix(r->coreUd, m.udNames[grp.ud])) {
      r->fallbackGroup[g] = 0;
      continue;
    }
    std::string stem = ToUtf8(m.C(grp.stem, grp.stemLen));
    bool core = set.count(stem) || set.count(std::string(m.S(grp.lemma)));
    const auto& p = m.paradigms[grp.paradigm];
    for (uint32_t f = p.first; f < p.first + p.count && !core; ++f)
      core = set.count(stem + ToUtf8(m.C(m.pforms[f].suf, m.pforms[f].sufLen))) != 0;
    r->fallbackGroup[g] = core ? 0 : 1;
  }
}

std::string_view RepairOpId(const RepairRules& rules, RepairOp op) { return rules.ops[static_cast<size_t>(op)].id; }

bool RepairPath(const Model& m, const RepairRules& R, Workspace* ws, const Options& opt, std::string* why) {
  auto& P = ws->path;
  if (P.empty() || ws->core.size() > 0xFFFF) return false;  // positions are 16-bit
  Reader reader(m, R, ws, opt);
  std::vector<uint8_t> mark;
  reader.Marks(&mark);
  if (std::find(mark.begin(), mark.end(), 1) == mark.end()) return false;

  // spans: two words either side of each marked word, not across punctuation, merged when they touch
  std::vector<std::pair<size_t, size_t>> spans;  // path index ranges [s, t)
  for (size_t i = 0; i < P.size(); ++i) {
    if (!mark[i]) continue;
    size_t s = i, t = i + 1;
    for (int k = 0; k < 2 && s > 0 && !HardSymbol(m, R, ws->arcs[P[s - 1]]); ++k) --s;
    for (int k = 0; k < 2 && t < P.size() && !HardSymbol(m, R, ws->arcs[P[t]]); ++k) ++t;
    // a repeated mark (、、、, 」」」) is read together with the marks next to it
    if (HardSymbol(m, R, ws->arcs[P[i]])) {
      if (s == i && s > 0) --s;
      if (t == i + 1 && t < P.size()) ++t;
    }
    if (!spans.empty() && spans.back().second >= s) spans.back().second = std::max(spans.back().second, t);
    else spans.push_back({s, t});
  }

  // letters of the words a check fired on: a transposition is tried only there
  std::vector<uint8_t> hot(ws->core.size() + 1, 0);
  for (size_t i = 0; i < P.size(); ++i)
    if (mark[i])
      for (uint32_t k = ws->arcs[P[i]].b; k < ws->arcs[P[i]].e; ++k) hot[k] = 1;

  bool changed = false;
  std::vector<uint32_t> newPath;
  size_t done = 0;
  for (auto [s, t] : spans) {
    for (; done < s; ++done) newPath.push_back(P[done]);
    Window w;
    w.b = ws->arcs[P[s]].b;
    w.e = ws->arcs[P[t - 1]].e;
    w.left = s > 0 ? static_cast<int32_t>(P[s - 1]) : -1;
    w.right = t < P.size() ? static_cast<int32_t>(P[t]) : -1;
    std::vector<uint32_t> cur(P.begin() + static_cast<long>(s), P.begin() + static_cast<long>(t));
    std::string checks;
    Key now = reader.Score(w, cur, why ? &checks : nullptr);
    const bool lexical = reader.LexicalTrigger(cur);
    Best best;
    best.key = now;
    best.key.rep = 0;
    std::string trace;
    std::string* tr = why && opt.whyLevel >= 2 ? &trace : nullptr;
    reader.Read(w, ws->core, -1, &best, tr, lexical);
    if (R.ops[static_cast<size_t>(RepairOp::swap)].on && best.key.bad > 0) {
      std::vector<char32_t> sw = ws->core;
      for (uint32_t p = w.b; p + 1 < w.e; ++p) {
        auto letter = [](char32_t c) { return IsKana(c) && !IsLongMark(c) && !IsSmall(c); };
        if (!letter(sw[p]) || !letter(sw[p + 1]) || sw[p] == sw[p + 1] || ws->spaceBefore[p + 1]) continue;
        if (IsHiragana(sw[p]) != IsHiragana(sw[p + 1]) || reader.LongKata(p) || reader.LongKata(p + 1)) continue;
        if (!hot[p] && !hot[p + 1]) continue;
        std::swap(sw[p], sw[p + 1]);
        reader.Read(w, sw, static_cast<int32_t>(p), &best, tr, lexical);
        std::swap(sw[p], sw[p + 1]);
      }
    }
    bool edited = best.swapAt >= 0;
    for (uint32_t x : best.path)
      if (best.arcs[x].rarc >= 0 && !best.rarcs[static_cast<size_t>(best.arcs[x].rarc)].edits.empty()) edited = true;
    bool take = !best.path.empty() && best.key.bad < now.bad && edited && best.key.rep <= R.windowBudget;
    if (why) {
      std::string text;
      for (uint32_t k = w.b; k < w.e; ++k) AppendUtf8(text, ws->core[k]);
      *why += "check " + text + ":" + checks + (take ? " -> repaired\n" : " -> kept\n") + trace;
    }
    if (!take) {
      for (; done < t; ++done) newPath.push_back(P[done]);
      continue;
    }
    changed = true;
    for (uint32_t x : best.path) {
      LArc& la = best.arcs[x];
      if (la.rarc < 0) {
        newPath.push_back(static_cast<uint32_t>(la.ws));
        continue;
      }
      RepairArc ra = std::move(best.rarcs[static_cast<size_t>(la.rarc)]);
      if (best.swapAt >= 0) {
        // the swap is an edit of the word holding its first letter; a word over the second letter only
        // records its half
        uint32_t p = static_cast<uint32_t>(best.swapAt);
        for (uint32_t k = 0; k < 2; ++k)
          if (la.arc.b <= p + k && p + k < la.arc.e) {
            RepairEdit e;
            e.pos = static_cast<uint16_t>(p + k);
            e.len = 1;
            e.to = std::u32string(1, best.core[p + k]);
            e.op = RepairOp::swap;
            bool dup = false;
            for (auto& x2 : ra.edits) dup = dup || (!x2.spaceOnly && x2.len > 0 && x2.pos <= e.pos && e.pos < x2.pos + x2.len);
            if (!dup) ra.edits.push_back(e);
          }
        std::sort(ra.edits.begin(), ra.edits.end(),
                  [](const RepairEdit& a, const RepairEdit& b) { return a.pos < b.pos; });
      }
      Arc a = la.arc;
      a.ref = static_cast<uint32_t>(ws->rarcs.size());
      a.fwd = a.bwd = kInf;
      a.bp = a.np = -1;
      ws->rarcs.push_back(std::move(ra));
      newPath.push_back(static_cast<uint32_t>(ws->arcs.size()));
      ws->arcs.push_back(a);
    }
    done = t;
  }
  if (!changed) return false;
  for (; done < P.size(); ++done) newPath.push_back(P[done]);
  P = std::move(newPath);
  return true;
}

}  // namespace hd8::ana
