#include "ptable.h"

#include <cstdlib>
#include <fstream>

#include "hd8/smt_ko.h"
#include "lm.h"

namespace hd8::smt {

namespace {
std::vector<std::string_view> Fields(std::string_view s) {
  std::vector<std::string_view> out;
  size_t b = 0;
  for (size_t i = 0; i <= s.size(); ++i)
    if (i == s.size() || s[i] == '\t') {
      out.push_back(s.substr(b, i - b));
      b = i + 1;
    }
  return out;
}
int32_t ToInt(std::string_view s) { return static_cast<int32_t>(std::strtol(std::string(s).c_str(), nullptr, 10)); }
}  // namespace

bool PhraseTable::Load(const std::string& path, const Lm& lm, std::string* err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    *err = "cannot open " + path;
    return false;
  }
  std::string line;
  std::unordered_map<std::string, uint16_t> provIds;
  size_t n = 0;
  while (std::getline(in, line)) {
    ++n;
    if (line.empty() || line[0] == '#') continue;
    auto f = Fields(line);
    if (f.size() < 8) {
      *err = path + ":" + std::to_string(n) + ": need 8 columns";
      return false;
    }
    PhraseOpt o;
    o.tgt = std::string(f[1]);
    for (const std::string& t : SplitTokens(o.tgt)) o.ids.push_back(lm.Id(t));
    o.ntok = static_cast<uint16_t>(o.ids.size());
    o.pef = ToInt(f[2]);
    o.pfe = ToInt(f[3]);
    o.lef = ToInt(f[4]);
    o.lfe = ToInt(f[5]);
    o.count = static_cast<uint32_t>(ToInt(f[6]));
    std::string prov(f[7]);
    auto it = provIds.find(prov);
    if (it == provIds.end()) {
      it = provIds.emplace(prov, static_cast<uint16_t>(provs_.size())).first;
      provs_.push_back(prov);
    }
    o.prov = it->second;
    std::string src(f[0]);
    int len = 1;
    for (char c : src) len += c == ' ';
    if (len > maxLen_) maxLen_ = len;
    for (size_t sp = src.find(' '); sp != std::string::npos; sp = src.find(' ', sp + 1)) prefixes_.insert(src.substr(0, sp));
    map_[src].push_back(std::move(o));
    ++rows_;
  }
  return true;
}

const std::vector<PhraseOpt>* PhraseTable::Find(const std::string& src) const {
  auto it = map_.find(src);
  return it == map_.end() ? nullptr : &it->second;
}

}  // namespace hd8::smt
