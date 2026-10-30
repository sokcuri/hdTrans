#pragma once
// Back-off n-gram LM over Korean tokens (interpolated modified Kneser-Ney, estimated by tools/lm.py).
// Scores are fixed-point integers: log10 probability × 10^4. Each order is a sorted array of
// (64-bit key, prob, backoff); the key is FNV-1a over the token ids, so lookups need no strings.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hd8::smt {

constexpr int kMaxOrder = 6;

struct LmState {
  uint32_t ids[kMaxOrder - 1] = {};
  uint8_t len = 0;  // most recent token last
  bool operator==(const LmState& o) const {
    if (len != o.len) return false;
    for (int i = 0; i < len; ++i)
      if (ids[i] != o.ids[i]) return false;
    return true;
  }
};

uint64_t NgramKey(const uint32_t* ids, int n);

class Lm {
 public:
  bool Load(const std::string& path, std::string* err);
  int order() const { return order_; }
  uint32_t Id(const std::string& tok) const;  // 0 = <unk>
  static constexpr uint32_t kUnk = 0, kBos = 1, kEos = 2;
  LmState Begin() const;
  // log10 p(w | state) × 10^4, and the successor state (shortened to the longest suffix the model knows)
  int32_t Score(const LmState& in, uint32_t w, LmState* out) const;
  size_t entries() const;
  size_t bytes() const { return bytes_; }

 private:
  struct Entry {
    uint64_t key;
    int32_t prob, bow;
  };
  const Entry* Find(int n, const uint32_t* ids) const;
  int order_ = 0;
  std::vector<std::vector<Entry>> tables_;  // [n-1]
  std::unordered_map<std::string, uint32_t> vocab_;
  int32_t unkProb_ = -100000;
  size_t bytes_ = 0;
};

}  // namespace hd8::smt
