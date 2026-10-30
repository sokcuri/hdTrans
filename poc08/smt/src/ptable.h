#pragma once
// Phrase table: Japanese token sequence (analyzer surfaces joined by ' ') → Korean token options.
// File: TSV rows `src tgt pef pfe lef lfe count prov`, scores are log10 × 10^4 integers, rows of one
// source phrase are contiguous and already pruned and ordered by the builder (tools/phrases.py).
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hd8::smt {

struct PhraseOpt {
  std::string tgt;            // Korean tokens, space separated
  std::vector<uint32_t> ids;  // LM ids of the tokens
  int32_t pef = 0, pfe = 0, lef = 0, lfe = 0;
  uint32_t count = 0;
  uint16_t prov = 0;
  uint16_t ntok = 0;
};

class Lm;

class PhraseTable {
 public:
  bool Load(const std::string& path, const Lm& lm, std::string* err);
  // Options of one source phrase, or null.
  const std::vector<PhraseOpt>* Find(const std::string& src) const;
  const std::string& Prov(uint16_t id) const { return provs_[id]; }
  size_t sources() const { return map_.size(); }
  size_t rows() const { return rows_; }
  int maxLen() const { return maxLen_; }

 private:
  std::unordered_map<std::string, std::vector<PhraseOpt>> map_;
  std::vector<std::string> provs_;
  size_t rows_ = 0;
  int maxLen_ = 1;
};

}  // namespace hd8::smt
