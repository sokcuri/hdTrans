// hd8decide: the deciders on ir.jsonl lines (feature dumps and decisions with --why records).
//
//   hd8decide --data DIR --dump < ir.jsonl
//       one line per decision point: case, token, family, src byte begin, features (space-separated)
//   hd8decide --data DIR --decide BASE.tsv < ir.jsonl
//       BASE.tsv: case<TAB>src-begin<TAB>0.7.5 class; one JSON line per decision point with the class,
//       the 0.7.5 class it kept or overrode, the margin and what decided it (rule:<row id> for a rule
//       table, each feature's contribution for a weight table)
// DIR is the transfer data folder. Exit: 0 ok, 2 usage or load error.
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

#include "decide.h"
#include "ir/ir_json.h"
#include "ir/json.h"

int main(int argc, char** argv) {
  std::string data, baseFile;
  bool dump = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) data = argv[++i];
    else if (a == "--dump") dump = true;
    else if (a == "--decide" && i + 1 < argc) baseFile = argv[++i];
    else {
      std::cerr << "usage: hd8decide --data DIR (--dump | --decide BASE.tsv) < ir.jsonl\n";
      return 2;
    }
  }
  if (data.empty() || (!dump && baseFile.empty())) {
    std::cerr << "usage: hd8decide --data DIR (--dump | --decide BASE.tsv) < ir.jsonl\n";
    return 2;
  }
  hd08::xfer::Table t;
  hd08::xfer::LoadTable(data, t);
  hd08::xfer::DecideData dd;
  if (!dump) {
    hd08::xfer::LoadDeciders(data, dd);
    for (const auto& e : dd.errors) std::cerr << e << "\n";
  }
  std::map<std::string, std::string> base;
  if (!baseFile.empty()) {
    std::ifstream in(baseFile);
    std::string l;
    while (std::getline(in, l)) {
      std::istringstream s(l);
      std::string c, b, k;
      std::getline(s, c, '\t');
      std::getline(s, b, '\t');
      std::getline(s, k, '\t');
      base[c + "@" + b] = k;
    }
  }
  std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
  std::vector<hd08::Doc> docs;
  std::string err;
  if (!hd08::ReadJsonl(text, docs, err)) {
    std::cerr << "hd8decide: " << err << "\n";
    return 2;
  }
  for (const auto& doc : docs)
    for (const auto& line : doc.lines) {
      auto v = hd08::xfer::MakeView(doc, line, t);
      for (const auto& p : hd08::xfer::FindPoints(t, v)) {
        auto f = hd08::xfer::Features(t, v, p);
        std::string begin = std::to_string(v.toks[static_cast<size_t>(p.tok)].src.begin);
        if (dump) {
          std::cout << v.key << "\t" << p.tok << "\t" << p.family << "\t" << begin << "\t";
          for (size_t k = 0; k < f.size(); ++k) std::cout << (k ? " " : "") << f[k];
          std::cout << "\n";
          continue;
        }
        auto it = dd.byFamily.find(p.family);
        if (it == dd.byFamily.end()) continue;
        auto bi = base.find(v.key + "@" + begin);
        auto r = hd08::xfer::Decide(it->second, f, bi == base.end() ? std::string() : bi->second);
        std::string o = "{\"case\":";
        hd08::AppendJsonString(o, v.key);
        o += ",\"src\":" + begin + ",\"family\":";
        hd08::AppendJsonString(o, p.family);
        o += ",\"by\":";
        hd08::AppendJsonString(o, "dec:" + it->second.id);
        o += ",\"class\":";
        hd08::AppendJsonString(o, r.chosen);
        o += ",\"t075\":";
        hd08::AppendJsonString(o, r.base);
        o += std::string(",\"overrode\":") + (r.overridden ? "true" : "false") + ",\"top\":";
        hd08::AppendJsonString(o, r.top);
        o += ",\"margin\":";
        hd08::AppendJsonNumber(o, r.margin);
        o += ",\"contrib\":[";
        for (size_t k = 0; k < r.because.size() && k < 8; ++k) {
          if (k) o += ",";
          o += "[";
          hd08::AppendJsonString(o, r.because[k].feature);
          o += ",";
          hd08::AppendJsonNumber(o, r.because[k].weight);
          o += "]";
        }
        o += "]}";
        std::cout << o << "\n";
      }
    }
  return 0;
}
