// Loading of the transfer tables: cx rows, their cases and the support tables (alias, fclass, forms, pred).
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "transfer.h"

namespace hd08::xfer {

namespace {
// A path as UTF-8 (path::string() aborts on characters the narrow code page lacks).
[[maybe_unused]] std::string Utf8Of(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}
}  // namespace

namespace {

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(cur);
  return out;
}

std::vector<std::string> Words(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string w;
  while (in >> w) out.push_back(w);
  return out;
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \r");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \r");
  return s.substr(a, b - a + 1);
}

// Reads a table file: skips the `#!` line, comments and blank lines; the first other line is the header.
bool ReadTsv(const std::filesystem::path& p, std::vector<std::string>& header,
             std::vector<std::pair<int, std::vector<std::string>>>& rows) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    ++n;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    auto cols = Split(line, '\t');
    if (header.empty()) {
      header = cols;
      continue;
    }
    rows.emplace_back(n, std::move(cols));
  }
  return true;
}

std::string Col(const std::vector<std::string>& header, const std::vector<std::string>& row, const char* name) {
  for (size_t i = 0; i < header.size(); ++i)
    if (header[i] == name) return i < row.size() ? Trim(row[i]) : std::string();
  return {};
}

std::vector<std::string> Expand(const Table& t, const std::vector<std::string>& alts) {
  std::vector<std::string> out;
  for (const auto& a : alts) {
    out.push_back(a);
    auto it = t.alias.find(a);
    if (it != t.alias.end())
      for (const auto& l : it->second) out.push_back(l);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

bool ParseAtom(const Table& t, std::string w, Atom& a, std::string& err) {
  if (w == "...") {
    a.kind = Atom::Kind::Gap;
    return true;
  }
  if (w.size() >= 2 && w.front() == '{' && w.back() == '}') {
    a.kind = Atom::Kind::Slot;
    std::string in = w.substr(1, w.size() - 2);
    size_t colon = in.find(':');
    std::string name = in.substr(0, colon);
    if (name != "V" && name != "N" && name != "X" && name != "A") {
      err = "unknown slot " + name;
      return false;
    }
    a.slot = name[0];
    if (colon != std::string::npos) {
      for (const auto& f : Split(in.substr(colon + 1), ',')) {
        if (f.empty()) continue;
        if (f[0] == '@') a.cls = f;
        else a.forms.push_back(f);
      }
    }
    return true;
  }
  if (w[0] == '@' && w.find('|') == std::string::npos) {
    a.kind = Atom::Kind::FClass;
    a.fclass = w.substr(1);
    if (a.fclass != "end" && a.fclass != "fin" && t.fclass.find(a.fclass) == t.fclass.end()) {
      err = "unknown function class @" + a.fclass;
      return false;
    }
    return true;
  }
  if (w[0] == '!') {
    a.kind = Atom::Kind::NotLemma;
    w = w.substr(1);
  } else {
    a.kind = Atom::Kind::Lemma;
  }
  size_t slash = w.find('/');
  if (slash != std::string::npos) {
    a.pos = Split(w.substr(slash + 1), '|');
    w = w.substr(0, slash);
  }
  std::vector<std::string> words;
  for (const auto& alt : Split(w, '|')) {
    if (!alt.empty() && alt[0] == '@') {
      if (t.fclass.find(alt.substr(1)) == t.fclass.end()) {
        err = "unknown function class " + alt;
        return false;
      }
      a.classes.push_back(alt.substr(1));
    } else {
      words.push_back(alt);
    }
  }
  a.lemmas = Expand(t, words);
  return true;
}

bool ParseMatch(const Table& t, Row& r, std::string& err) {
  int group = -1, groups = 0;
  for (auto w : Words(r.match)) {
    bool open = false, close = false;
    if (!w.empty() && w.front() == '[') {
      open = true;
      w = w.substr(1);
    }
    if (!w.empty() && w.back() == ']') {
      close = true;
      w.pop_back();
    }
    if (open) {
      if (group >= 0) {
        err = "nested [ ]";
        return false;
      }
      group = groups++;
      Atom g;
      g.kind = Atom::Kind::GroupOpen;
      g.group = group;
      r.atoms.push_back(g);
    }
    if (!w.empty()) {
      Atom a;
      if (!ParseAtom(t, w, a, err)) return false;
      a.optional = group >= 0;
      a.group = group;
      if (a.kind == Atom::Kind::Lemma || (a.kind == Atom::Kind::FClass && a.fclass != "end" && a.fclass != "fin"))
        ++r.literals;
      if (a.kind == Atom::Kind::Slot) ++r.slots;
      r.atoms.push_back(std::move(a));
    }
    if (close) {
      if (group < 0) {
        err = "] without [";
        return false;
      }
      Atom g;
      g.kind = Atom::Kind::GroupClose;
      g.group = group;
      r.atoms.push_back(g);
      group = -1;
    }
  }
  if (group >= 0) {
    err = "[ without ]";
    return false;
  }
  if (r.atoms.empty()) {
    err = "empty match";
    return false;
  }
  for (size_t i = 0; i < r.atoms.size() && r.req < 0; ++i) {
    const Atom& a = r.atoms[i];
    if (a.optional) continue;
    if (a.kind == Atom::Kind::Lemma || (a.kind == Atom::Kind::FClass && a.fclass != "end" && a.fclass != "fin"))
      r.req = static_cast<int>(i);
  }
  return true;
}

bool ParseWhen(const Table& t, Row& r, std::string& err) {
  if (r.when.empty() || r.when == "-") return true;
  for (const auto& w : Words(r.when)) {
    Cond c;
    size_t ne = w.find("!=");
    size_t eq = w.find('=');
    if (ne != std::string::npos) {
      c.path = w.substr(0, ne);
      c.negate = true;
      c.values = Split(w.substr(ne + 2), '|');
    } else if (eq != std::string::npos) {
      c.path = w.substr(0, eq);
      c.values = Split(w.substr(eq + 1), '|');
    } else {
      err = "bad when atom " + w;
      return false;
    }
    if (c.path.size() > 6 && c.path.compare(c.path.size() - 6, 6, ".lemma") == 0) c.values = Expand(t, c.values);
    if (false) {
      err = "bad when atom " + w;
      return false;
    }
    r.conds.push_back(std::move(c));
  }
  return true;
}

void LoadSupport(const std::filesystem::path& dir, Table& t) {
  std::vector<std::string> h;
  std::vector<std::pair<int, std::vector<std::string>>> rows;
  if (ReadTsv(dir / "alias.tsv", h, rows))
    for (const auto& nr : rows) {
      const auto& r = nr.second;
      std::string from = Col(h, r, "form"), to = Col(h, r, "lemma");
      if (!from.empty() && !to.empty())
        for (const auto& l : Split(to, '|')) t.alias[from].push_back(l);
    }
  h.clear();
  rows.clear();
  if (ReadTsv(dir / "fclass.tsv", h, rows))
    for (const auto& nr : rows) {
      const auto& r = nr.second;
      FuncLine f;
      f.pos = Col(h, r, "pos");
      f.udPrefix = Col(h, r, "ud");
      if (f.udPrefix == "-") f.udPrefix.clear();
      f.cform = Col(h, r, "cform");
      if (f.cform == "-") f.cform.clear();
      f.ctype = Col(h, r, "ctype");
      if (f.ctype == "-") f.ctype.clear();
      f.lemmas = Split(Col(h, r, "lemmas"), '|');
      t.fclass[Col(h, r, "class")].push_back(std::move(f));
    }
  h.clear();
  rows.clear();
  if (ReadTsv(dir / "forms.tsv", h, rows))
    for (const auto& nr : rows) t.forms.emplace_back(Col(h, nr.second, "cform"), Col(h, nr.second, "form"));
  h.clear();
  rows.clear();
  if (ReadTsv(dir / "classes.tsv", h, rows))
    for (const auto& nr : rows) {
      std::string cls = Col(h, nr.second, "class");
      for (const auto& w : Split(Col(h, nr.second, "words"), '|'))
        if (!w.empty()) t.nounClass.emplace(w, cls);
    }
  h.clear();
  rows.clear();
  if (ReadTsv(dir / "pred.tsv", h, rows))
    for (const auto& nr : rows) {
      const auto& r = nr.second;
      PredFeat p;
      p.vol = Col(h, r, "vol");
      p.asp = Col(h, r, "asp");
      p.pclass = Col(h, r, "pclass");
      if (p.vol == "*") p.vol.clear();
      if (p.asp == "*") p.asp.clear();
      if (p.pclass == "*") p.pclass.clear();
      t.pred[Col(h, r, "lemma")] = p;
    }
}

}  // namespace

std::string Table::ClassOf(const std::string& lemma, const std::string& surf, const std::string& ud) const {
  auto it = fclass.find("cls.person.name");
  if (it != fclass.end())
    for (const auto& f : it->second)
      if (!f.udPrefix.empty() && ud.compare(0, f.udPrefix.size(), f.udPrefix) == 0) return "@person";
  it = fclass.find("cls.place.name");
  if (it != fclass.end())
    for (const auto& f : it->second)
      if (!f.udPrefix.empty() && ud.compare(0, f.udPrefix.size(), f.udPrefix) == 0) return "@place";
  auto c = nounClass.find(lemma);
  if (c == nounClass.end()) c = nounClass.find(surf);
  return c == nounClass.end() ? std::string() : c->second;
}

bool LoadTable(const std::string& dirName, Table& t) {
  namespace fs = std::filesystem;
  fs::path dir(std::u8string(dirName.begin(), dirName.end()));  // UTF-8 path
  LoadSupport(dir, t);
  std::error_code ec;
  std::vector<fs::path> files;
  for (fs::directory_iterator it(dir / "cx", ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".tsv") files.push_back(it->path());
  if (ec) {
    t.errors.push_back("cannot list " + Utf8Of(dir / "cx"));
    return false;
  }
  std::sort(files.begin(), files.end());
  for (const auto& p : files) {
    std::string name = Utf8Of(p.filename());
    bool cases = name.size() > 10 && name.compare(name.size() - 10, 10, ".cases.tsv") == 0;
    std::vector<std::string> h;
    std::vector<std::pair<int, std::vector<std::string>>> rows;
    if (!ReadTsv(p, h, rows)) {
      t.errors.push_back("cannot read " + Utf8Of(p));
      continue;
    }
    for (const auto& [n, r] : rows) {
      std::string where = name + ":" + std::to_string(n);
      if (cases) {
        Case c{Col(h, r, "id"), Col(h, r, "kind"), Col(h, r, "ja"), Col(h, r, "ko"), where};
        if (c.id.empty() || (c.kind != "fix" && c.kind != "keep") || c.ja.empty()) {
          t.errors.push_back(where + ": bad case row");
          continue;
        }
        t.cases.push_back(std::move(c));
        continue;
      }
      Row row;
      row.id = Col(h, r, "id");
      row.match = Col(h, r, "match");
      row.when = Col(h, r, "when");
      row.set = Col(h, r, "set");
      row.ko = Col(h, r, "ko");
      row.pol = Col(h, r, "pol");
      row.spec = Col(h, r, "spec");
      row.note = Col(h, r, "note");
      std::string prio = Col(h, r, "prio");
      row.prio = prio.empty() || prio == "-" ? 0 : std::atoi(prio.c_str());
      row.file = where;
      row.order = t.rows.size();
      row.facet = row.id.substr(0, row.id.find('.'));
      std::string err;
      if (row.id.empty() || !ParseMatch(t, row, err) || !ParseWhen(t, row, err)) {
        t.errors.push_back(where + ": " + row.id + ": " + err);
        continue;
      }
      if (row.pol.empty()) row.pol = "keep";
      t.rows.push_back(std::move(row));
    }
  }
  // every row needs a fix and a keep case (DESIGN §3)
  for (const auto& row : t.rows) {
    bool fix = false, keep = false;
    for (const auto& c : t.cases)
      if (c.id == row.id) (c.kind == "fix" ? fix : keep) = true;
    if (!fix || !keep) t.errors.push_back("lint: " + row.id + " lacks a " + (fix ? "keep" : "fix") + " case");
  }
  for (const auto& c : t.cases) {
    bool found = false;
    for (const auto& row : t.rows) found = found || row.id == c.id;
    if (!found) t.errors.push_back(c.file + ": case for unknown row " + c.id);
  }
  return true;
}

}  // namespace hd08::xfer
