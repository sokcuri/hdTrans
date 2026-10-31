// data/ TSV files → Model. Used by the packer and the trainer, not by the runtime.
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "model.h"
#include "tsv.h"
#include "utf8.h"

namespace hd8::ana {

namespace {

using internal::ToU32;

std::string Star(const std::string& s) { return s == "*" ? std::string() : s; }

int Index(const std::vector<std::string>& v, const std::string& s) {
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i] == s) return static_cast<int>(i);
  return -1;
}

bool ParsePos(std::string_view s, Pos* p) {
  for (int i = 0; i <= static_cast<int>(Pos::unk); ++i)
    if (Analyzer::PosName(static_cast<Pos>(i)) == s) {
      *p = static_cast<Pos>(i);
      return true;
    }
  return false;
}

uint32_t ParseMask(const std::string& s) {
  if (s.empty() || s == "*") return 0;
  uint32_t m = 0;
  for (auto& t : SplitOn(s, '|')) {
    if (t == "hira") m |= ccHira;
    else if (t == "kata") m |= ccKata;
    else if (t == "kanji") m |= ccKanji;
    else if (t == "alpha") m |= ccAlpha;
    else if (t == "digit") m |= ccDigit;
    else if (t == "sym") m |= ccSym;
    else if (t == "other") m |= ccOther;
    else if (t == "end") m |= ccEnd;
    else if (t == "vowelrun") m |= ccVowelRun;
  }
  return m;
}

bool ParseConds(const std::string& s, std::vector<Model::Cond>* out, std::string* err) {
  if (s.empty() || s == "-") return true;
  for (auto t : SplitOn(s, '&')) {
    t = Trim(t);
    Model::Cond c;
    if (!t.empty() && t[0] == '!') {
      c.neg = true;
      t = t.substr(1);
    }
    auto eq = t.find('=');
    if (eq == std::string::npos) {
      *err = "condition without '=': " + t;
      return false;
    }
    std::string key = t.substr(0, eq), val = t.substr(eq + 1);
    if (key == "prev") c.what = 0;
    else if (key == "next") c.what = 1;
    else if (key == "prev.cc") c.what = 2;
    else if (key == "next.cc") c.what = 3;
    else {
      *err = "unknown condition key: " + key;
      return false;
    }
    if (c.what >= 2) c.mask = ParseMask(val);
    else
      for (auto& a : SplitOn(val, '|')) c.alts.push_back(ToU32(a));
    out->push_back(std::move(c));
  }
  return true;
}

}  // namespace

bool LoadTsvDir(const std::string& dir, Model* m, std::string* err) {
  std::string e;
  auto fail = [err](const std::string& what) {
    *err = what;
    return false;
  };
  // names
  if (!ReadTsv(dir + "/udpos.tsv", [m](const std::vector<std::string>& f) {
        if (f.size() >= 2) m->udNames.push_back(f[1]);
      }, &e))
    return fail(e);
  m->ctypeNames = {""};
  m->cformNames = {""};
  if (!ReadTsv(dir + "/conj.tsv", [m](const std::vector<std::string>& f) {
        if (f.size() < 3 || f[1] == "0") return;
        (f[0] == "ctype" ? m->ctypeNames : m->cformNames).push_back(f[2]);
      }, &e))
    return fail(e);
  bool posOk = true;
  if (!ReadTsv(dir + "/pos-map.tsv", [m, &posOk](const std::vector<std::string>& f) {
        if (f.size() < 3) return;
        Model::PosRule r;
        r.ud = Star(f[0]);
        r.ctype = Star(f[1]);
        posOk = posOk && ParsePos(f[2], &r.pos);
        m->posRules.push_back(r);
      }, &e))
    return fail(e);
  if (!posOk) return fail("pos-map.tsv: unknown Pos name");
  auto udId = [m](const std::string& s) { return Index(m->udNames, s); };
  auto ctId = [m](const std::string& s) { return s == "*" ? 0 : Index(m->ctypeNames, s); };
  auto cfId = [m](const std::string& s) { return s == "*" ? 0 : Index(m->cformNames, s); };

  // classes
  std::unordered_map<std::string, int> classKey;
  bool ok = true;
  std::string bad;
  if (!ReadTsv(dir + "/model/classes.tsv", [&](const std::vector<std::string>& f) {
        if (f.size() < 5) return;
        Model::Class c;
        int ud = udId(f[1]), ct = ctId(f[2]), cf = cfId(f[3]);
        if (ud < 0 || ct < 0 || cf < 0) {
          ok = false;
          bad = f[1] + " " + f[2] + " " + f[3];
          return;
        }
        if (std::atoi(f[0].c_str()) != static_cast<int>(m->classes.size())) {
          ok = false;
          bad = "class ids must be 0..n-1 in order";
          return;
        }
        c.ud = static_cast<uint16_t>(ud);
        c.ctype = static_cast<uint8_t>(ct);
        c.cform = static_cast<uint8_t>(cf);
        std::string lemma = Star(f[4]);
        c.lemma = m->Intern(lemma);
        classKey[f[1] + '\t' + Star(f[2]) + '\t' + Star(f[3]) + '\t' + lemma] = static_cast<int>(m->classes.size());
        m->classes.push_back(c);
      }, &e))
    return fail(e);
  if (!ok) return fail("classes.tsv: " + bad);
  size_t n = m->classes.size();

  // templates
  struct TForm {
    std::string cform, suf, read;
  };
  std::vector<std::vector<TForm>> templates;
  if (!ReadTsv(dir + "/model/templates.tsv", [&](const std::vector<std::string>& f) {
        std::vector<TForm> forms;
        if (f.size() >= 2)
          for (auto& t : SplitOn(f[1], ' ')) {
            if (t.empty()) continue;
            auto parts = SplitOn(t, ',');
            if (parts.size() != 3) {
              ok = false;
              bad = t;
              continue;
            }
            forms.push_back({parts[0], parts[1], parts[2]});
          }
        templates.push_back(std::move(forms));
      }, &e))
    return fail(e);
  if (!ok) return fail("templates.tsv: " + bad);

  // lexicon
  std::map<std::vector<uint32_t>, uint32_t> paradigmIds;
  auto findClass = [&](const std::string& ud, const std::string& ct, const std::string& cf,
                       const std::string& lemma) {
    auto it = classKey.find(ud + '\t' + Star(ct) + '\t' + Star(cf) + '\t' + lemma);
    if (it != classKey.end()) return it->second;
    it = classKey.find(ud + '\t' + Star(ct) + '\t' + Star(cf) + '\t');
    return it == classKey.end() ? -1 : it->second;
  };
  std::u32string prevStem;
  if (!ReadTsv(dir + "/model/lexicon.tsv", [&](const std::vector<std::string>& f) {
        if (!ok || f.size() < 7) return;
        Model::Group g;
        std::u32string stem = ToU32(f[0]);
        if (stem.empty() || stem < prevStem) {
          ok = false;
          bad = "stems must be non-empty and sorted: " + f[0];
          return;
        }
        prevStem = stem;
        g.stem = static_cast<uint32_t>(m->cps.size());
        g.stemLen = static_cast<uint16_t>(stem.size());
        m->cps += stem;
        int ud = udId(f[3]), ct = ctId(f[4]);
        size_t tid = static_cast<size_t>(std::atoi(f[5].c_str()));
        if (ud < 0 || ct < 0 || tid >= templates.size()) {
          ok = false;
          bad = "bad row: " + f[0];
          return;
        }
        g.ud = static_cast<uint16_t>(ud);
        g.ctype = static_cast<uint8_t>(ct);
        g.lemma = m->Intern(f[2]);
        if (f[1] == "*") {
          g.readKind = 2;
        } else {
          std::u32string kat;
          for (char32_t c : stem) kat += internal::ToKatakana(c);
          if (ToU32(f[1]) == kat) {
            g.readKind = 0;
          } else {
            g.readKind = 1;
            g.read = m->Intern(f[1]);
          }
        }
        g.cost = std::strtoll(f[6].c_str(), nullptr, 10);
        // paradigm instance: template forms with their classes
        std::vector<uint32_t> key{static_cast<uint32_t>(tid)};
        std::vector<Model::PForm> forms;
        const auto& tf = templates[tid];
        if (tf.empty()) {
          int c = findClass(f[3], f[4], "*", f[2]);
          if (c < 0) {
            ok = false;
            bad = "no class for " + f[0] + " " + f[3];
            return;
          }
          key.push_back(static_cast<uint32_t>(c));
          forms.push_back({0, 0, static_cast<uint16_t>(c), {}});
        } else {
          for (auto& x : tf) {
            int c = findClass(f[3], f[4], x.cform, f[2]);
            if (c < 0) {
              ok = false;
              bad = "no class for " + f[0] + " " + f[3] + " " + x.cform;
              return;
            }
            key.push_back(static_cast<uint32_t>(c));
            Model::PForm pf;
            std::u32string suf = ToU32(x.suf);
            pf.suf = 0;
            pf.sufLen = static_cast<uint16_t>(suf.size());
            pf.cls = static_cast<uint16_t>(c);
            pf.read = m->Intern(x.read);
            forms.push_back(pf);
          }
        }
        auto it = paradigmIds.find(key);
        if (it == paradigmIds.end()) {
          Model::Paradigm p;
          p.first = static_cast<uint32_t>(m->pforms.size());
          p.count = static_cast<uint32_t>(forms.size());
          for (size_t k = 0; k < forms.size(); ++k) {
            auto pf = forms[k];
            std::u32string suf = tf.empty() ? std::u32string() : ToU32(tf[k].suf);
            pf.suf = static_cast<uint32_t>(m->cps.size());
            m->cps += suf;
            m->pforms.push_back(pf);
          }
          it = paradigmIds.emplace(key, static_cast<uint32_t>(m->paradigms.size())).first;
          m->paradigms.push_back(p);
        }
        g.paradigm = it->second;
        m->groups.push_back(g);
      }, &e))
    return fail(e);
  if (!ok) return fail("lexicon.tsv: " + bad);

  // connection
  if (!ReadTsv(dir + "/model/connrules.tsv", [&](const std::vector<std::string>& f) {
        if (!ok || f.size() < 3) return;
        Model::ConnRule c;
        c.left = f[0];
        c.right = f[1];
        c.cost = std::strtoll(f[2].c_str(), nullptr, 10);
        if (!ParseSel(c.left, &c.l) || !ParseSel(c.right, &c.r)) {
          ok = false;
          bad = f[0] + " -> " + f[1];
          return;
        }
        m->connRules.push_back(std::move(c));
      }, &e))
    return fail(e);
  if (!ok) return fail("connrules.tsv: bad selector " + bad);

  // characters and unknown words
  std::vector<std::vector<std::string>> rangeRows;
  if (!ReadTsv(dir + "/model/chardef.tsv", [&](const std::vector<std::string>& f) {
        if (f.size() >= 5 && f[0] == "cat") {
          Model::Category c;
          c.name = f[1];
          c.invoke = f[2] == "1";
          c.group = f[3] == "1";
          c.length = static_cast<uint8_t>(std::atoi(f[4].c_str()));
          m->cats.push_back(c);
        } else if (f.size() >= 4 && f[0] == "range") {
          rangeRows.push_back(f);
        }
      }, &e, false))
    return fail(e);
  for (auto& f : rangeRows) {
    Model::CharRange r;
    r.lo = static_cast<char32_t>(std::strtoul(f[1].c_str(), nullptr, 16));
    r.hi = static_cast<char32_t>(std::strtoul(f[2].c_str(), nullptr, 16));
    bool first = true;
    for (auto& name : SplitOn(f[3], ' ')) {
      std::vector<std::string> names;
      for (auto& c : m->cats) names.push_back(c.name);
      int ci = Index(names, name);
      if (ci < 0) continue;
      if (first) r.primary = static_cast<uint8_t>(ci);
      first = false;
      r.mask |= 1u << ci;
    }
    m->ranges.push_back(r);
  }
  if (!ReadTsv(dir + "/model/unk.tsv", [&](const std::vector<std::string>& f) {
        if (f.size() < 4) return;
        for (auto& c : m->cats)
          if (c.name == f[0]) {
            Model::UnkArc a;
            a.cls = static_cast<uint16_t>(std::atoi(f[1].c_str()));
            a.cost = std::strtoll(f[2].c_str(), nullptr, 10);
            a.perChar = std::strtoll(f[3].c_str(), nullptr, 10);
            if (a.cls < n) c.arcs.push_back(a);
          }
      }, &e))
    return fail(e);

  // ja/: derived arcs and constraints
  if (!ReadTsv(dir + "/ja/derive.tsv", [&](const std::vector<std::string>& f) {
        if (!ok || f.size() < 6) return;
        Model::Rule r;
        r.id = f[0];
        r.kind = f[1] == "elong" ? 1 : f[1] == "stutter" ? 2 : 0;
        r.from = ToU32(f[2] == "-" ? "" : f[2]);
        r.to = ToU32(f[3] == "-" ? "" : f[3]);
        std::string ce;
        if (!ParseConds(f[5], &r.cond, &ce)) {
          ok = false;
          bad = r.id + ": " + ce;
          return;
        }
        m->rules.push_back(r);
      }, &e))
    return fail(e);
  if (!ok) return fail("derive.tsv: " + bad);
  if (!ReadTsv(dir + "/ja/constraints.tsv", [&](const std::vector<std::string>& f) {
        if (!ok || f.size() < 4) return;
        Model::Constraint c;
        c.id = f[0];
        std::string sel = f[1];
        auto lb = sel.find('[');
        c.lemma = sel.substr(0, lb);
        if (c.lemma == "*") c.lemma.clear();
        if (lb != std::string::npos) {
          auto rb = sel.rfind(']');
          for (auto& kv : SplitOn(sel.substr(lb + 1, rb - lb - 1), ',')) {
            auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            std::string k = Trim(kv.substr(0, eq)), v = Trim(kv.substr(eq + 1));
            if (k == "pos") {
              Pos p;
              if (!ParsePos(v, &p)) {
                ok = false;
                bad = "bad pos in " + c.id;
                return;
              }
              c.pos = static_cast<int>(p);
            } else if (k == "cform") c.cform = v;
            else if (k == "ud") c.ud = v;
            else if (k == "surf") c.surf = v;
          }
        }
        std::string ce;
        if (!ParseConds(f[2], &c.when, &ce) || !ParseConds(f[3], &c.unless, &ce)) {
          ok = false;
          bad = c.id + ": " + ce;
          return;
        }
        m->constraints.push_back(std::move(c));
      }, &e))
    return fail(e);
  if (!ok) return fail("constraints.tsv: " + bad);

  // parameters
  if (!ReadTsv(dir + "/model/params.tsv", [&](const std::vector<std::string>& f) {
        if (f.size() < 2) return;
        if (f[0] == "cut") {  // value: LEFT RIGHT COST (char classes joined by |)
          auto parts = SplitOn(f[1], ' ');
          if (parts.size() == 3) m->cuts.push_back({ParseMask(parts[0]), ParseMask(parts[1]),
                                                    std::strtoll(parts[2].c_str(), nullptr, 10)});
          return;
        }
        int64_t v = std::strtoll(f[1].c_str(), nullptr, 10);
        if (f[0] == "join_cost") m->joinCost = v;
        else if (f[0] == "derive_cost") m->deriveCost = v;
        else if (f[0] == "forbid_cost") m->forbidCost = v;
        else if (f[0] == "unit_cost") m->unitCost = v > 0 ? v : 1;
        else if (f[0] == "stutter_ud") {
          int c = findClass(f[1], "*", "*", "");
          if (c >= 0) m->stutterCls = static_cast<uint16_t>(c);
        }
      }, &e))
    return fail(e);
  return true;
}

bool LoadLongTsv(const std::string& path, Model* m, std::string* err) {
  std::string e;
  bool ok = true;
  std::string bad;
  std::u32string prev;
  auto udId = [m](const std::string& s) { return Index(m->udNames, s); };
  m->longs.clear();
  if (!ReadTsv(path, [&](const std::vector<std::string>& f) {
        if (!ok || f.size() < 10) return;
        Model::Long l;
        std::u32string key = ToU32(f[0]);
        if (key.empty() || key < prev) {
          ok = false;
          bad = "keys must be sorted: " + f[0];
          return;
        }
        prev = key;
        l.key = static_cast<uint32_t>(m->cps.size());
        l.keyLen = static_cast<uint16_t>(key.size());
        m->cps += key;
        Pos p;
        if (!ParsePos(f[1], &p)) {
          ok = false;
          bad = "bad pos " + f[1];
          return;
        }
        l.pos = p;
        l.lcls = static_cast<uint16_t>(std::atoi(f[2].c_str()));
        l.rcls = static_cast<uint16_t>(std::atoi(f[3].c_str()));
        l.cost = std::strtoll(f[4].c_str(), nullptr, 10);
        int ud = udId(f[5]);
        int ct = f[6] == "*" ? 0 : Index(m->ctypeNames, f[6]);
        int cf = f[7] == "*" ? 0 : Index(m->cformNames, f[7]);
        if (ud < 0 || ct < 0 || cf < 0 || l.lcls >= m->classes.size() || l.rcls >= m->classes.size()) {
          ok = false;
          bad = "bad row " + f[0];
          return;
        }
        l.ud = static_cast<uint16_t>(ud);
        l.ctype = static_cast<uint8_t>(ct);
        l.cform = static_cast<uint8_t>(cf);
        l.lemma = m->Intern(f[8] == "*" ? f[0] : f[8]);
        l.read = m->Intern(f[9] == "*" ? "" : f[9]);
        if (f.size() > 10 && f[10] != "*") {
          l.paradigm = static_cast<uint32_t>(std::atoi(f[10].c_str()));
          if (l.paradigm >= m->paradigms.size()) {
            ok = false;
            bad = "bad paradigm " + f[0];
            return;
          }
        }
        m->longs.push_back(l);
      }, &e))
    return (*err = e, false);
  if (!ok) return (*err = path + ": " + bad, false);
  return true;
}

}  // namespace hd8::ana
