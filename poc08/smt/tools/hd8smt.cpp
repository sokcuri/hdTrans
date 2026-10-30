// hd8smt: the phrase-based layer's command line.
//   hd8smt ko-query [--gen DIR]              build-time predicate/particle queries (stdin → stdout)
//       P <lemma> <cls> <pos> <host> <surface> <want> <mood>   → "<bundle>\t<level>" or "-"
//       K <prev> <notation> <surface>                           → 1 | 0
//   hd8smt ko-join [--gen DIR] [--level L]   token lines → Korean lines
//   hd8smt decode --model DIR [options]      Japanese lines → Korean lines (see Usage)
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "hd8/smt_decoder.h"
#include "hd8/smt_ko.h"
#include "hd8gen/register.h"

#ifndef HD8SMT_GEN_DATA
#define HD8SMT_GEN_DATA "data/ko"
#endif
#ifndef HD8SMT_REG_DATA
#define HD8SMT_REG_DATA "data/register"
#endif

namespace {

std::vector<std::string> SplitTab(const std::string& s) {
  std::vector<std::string> out(1);
  for (char c : s) {
    if (c == '\t') out.emplace_back();
    else out.back() += c;
  }
  return out;
}

int Usage() {
  std::fprintf(stderr,
               "usage: hd8smt ko-query|ko-join [--gen DIR] [--level L]\n"
               "       hd8smt decode --model DIR [--analyzer CORE.bin] [--gen DIR] [--weights W.tsv] [--level L]\n"
               "              [--beam N] [--why] [--nbest N] [--glossary G.tsv] [--tokens] [--no-lattice]\n"
               "              [--level-tab] [--lat-tau T] [--max-opts N] [--speech auto|strict]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return Usage();
  const std::string cmd = argv[1];
  std::string gen = HD8SMT_GEN_DATA, level = "haera";
  hd8::smt::DecodeOptions dopt;
  std::string model, analyzer, weights, glossary;
  bool levelTab = false;
  bool each = false;   // with --speech: every line is its own scene
  std::string speech;  // "" (use --level), auto, strict: decide levels with the W5 register model
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--gen") gen = val();
    else if (a == "--level") level = val();
    else if (a == "--model") model = val();
    else if (a == "--analyzer") analyzer = val();
    else if (a == "--weights") weights = val();
    else if (a == "--glossary") glossary = val();
    else if (a == "--beam") dopt.beam = std::atoi(val().c_str());
    else if (a == "--nbest") dopt.nbest = std::atoi(val().c_str());
    else if (a == "--why") dopt.why = true;
    else if (a == "--tokens") dopt.tokens = true;
    else if (a == "--no-lattice") dopt.lattice = false;
    else if (a == "--no-join") dopt.join = false;
    else if (a == "--level-tab") levelTab = true;
    else if (a == "--speech") speech = val();
    else if (a == "--each") each = true;
    else if (a == "--lat-tau") dopt.latTau = static_cast<float>(std::atof(val().c_str()));
    else if (a == "--max-opts") dopt.maxOpts = std::atoi(val().c_str());
    else return Usage();
  }
  hd8::smt::KoText ko;
  std::string err;
  if (!ko.Load(gen, &err)) {
    std::fprintf(stderr, "hd8smt: %s\n", err.c_str());
    return 1;
  }
  std::ios::sync_with_stdio(false);
  std::string line;
  if (cmd == "ko-query") {
    while (std::getline(std::cin, line)) {
      auto f = SplitTab(line);
      if (f[0] == "P" && f.size() == 8) {
        std::string lv;
        std::string b = ko.QueryPredicate(f[1], f[2], f[3], f[4], f[5], f[6], f[7], &lv);
        std::cout << (b.empty() ? "-" : b + "\t" + lv) << '\n';
      } else if (f[0] == "K" && f.size() == 4) {
        std::cout << (ko.CheckParticle(f[1], f[2], f[3]) ? "1" : "0") << '\n';
      } else {
        std::cout << "?\n";
      }
    }
    return 0;
  }
  if (cmd == "ko-join") {
    while (std::getline(std::cin, line)) std::cout << ko.Join(hd8::smt::SplitTokens(line), level) << '\n';
    return 0;
  }
  if (cmd == "decode") {
    hd8::smt::Decoder dec;
    if (!dec.Load(model, analyzer, weights, glossary, &err)) {
      std::fprintf(stderr, "hd8smt: %s\n", err.c_str());
      return 1;
    }
    dopt.level = level;
    if (!speech.empty()) {
      // document mode: the whole input is one document, blank lines cut scenes; R decides each line's level
      std::shared_ptr<const hd8gen::RegisterTables> reg = hd8gen::LoadRegisterTables(HD8SMT_REG_DATA, err);
      if (!reg) {
        std::fprintf(stderr, "hd8smt: %s\n", err.c_str());
        return 1;
      }
      std::vector<std::string> lines;
      while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
      }
      std::vector<hd8gen::RegLine> rl;
      uint32_t scene = 1;
      for (const std::string& l : lines) {
        if (each || l.find_first_not_of(" \t") == std::string::npos) ++scene;
        hd8gen::RegLine r;
        r.scene = scene;
        r.dialogue = l.find("「") != std::string::npos || l.find("『") != std::string::npos;
        r.ja = l;
        if (r.dialogue) {  // R reads the quoted speech, not the narration around it
          std::string q;
          for (size_t at = 0; at < l.size();) {
            size_t o1 = l.find("「", at), o2 = l.find("『", at);
            size_t o = std::min(o1, o2);
            if (o == std::string::npos) break;
            const char* close = o == o1 ? "」" : "』";
            size_t c = l.find(close, o + 3);
            q += l.substr(o, (c == std::string::npos ? l.size() : c + 3) - o);
            at = c == std::string::npos ? l.size() : c + 3;
          }
          if (!q.empty()) r.ja = q;
        }
        rl.push_back(r);
      }
      hd8gen::RegOptions ro;
      ro.profiles = speech == "auto";
      const std::vector<hd8gen::RegDecision> ds = hd8gen::DecideLevels(*reg, rl, ro);
      for (size_t i = 0; i < lines.size(); ++i) {
        hd8::smt::DecodeOptions o = dopt;
        hd8gen::RegDecision d = i < ds.size() ? ds[i] : hd8gen::RegDecision{};
        hd8gen::Level lv = d.level == hd8gen::Level::none ? hd8gen::Level::hae : d.level;
        if (speech == "strict") {
          const std::string& k = d.key;
          lv = (k == "pend" || k == "keigo-pend" || k == "set" || k == "keigo" || k == "polite-any") ? hd8gen::Level::hapsyo
                                                                                                     : hd8gen::Level::haera;
        }
        const std::string& l = lines[i];
        // quoted speech takes R's level; narration around it is 해라체; a line without brackets is speech
        // unless R reads it as narration
        o.quoteLevel = hd8gen::Name(lv);
        o.level = rl[i].dialogue || d.narration ? "haera" : hd8gen::Name(lv);
        std::cout << (l.find_first_not_of(" \t") == std::string::npos ? std::string() : dec.Translate(l, ko, o)) << '\n';
      }
      return 0;
    }
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      hd8::smt::DecodeOptions o = dopt;
      if (levelTab) {  // "LEVEL<TAB>line": the speech level of this line from the caller
        size_t t = line.find('\t');
        if (t != std::string::npos) {
          o.level = line.substr(0, t);
          line = line.substr(t + 1);
        }
      }
      std::cout << dec.Translate(line, ko, o) << '\n';
    }
    return 0;
  }
  return Usage();
}
