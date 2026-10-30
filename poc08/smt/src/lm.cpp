#include "lm.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace hd8::smt {

uint64_t NgramKey(const uint32_t* ids, int n) {
  uint64_t h = 0xcbf29ce484222325ULL ^ static_cast<uint64_t>(n);
  for (int i = 0; i < n; ++i) {
    h ^= ids[i];
    h *= 0x100000001b3ULL;
    h ^= h >> 29;
  }
  return h;
}

namespace {
template <typename T>
bool ReadPod(const std::string& buf, size_t* pos, T* v) {
  if (*pos + sizeof(T) > buf.size()) return false;
  std::memcpy(v, buf.data() + *pos, sizeof(T));
  *pos += sizeof(T);
  return true;
}
}  // namespace

bool Lm::Load(const std::string& path, std::string* err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    *err = "cannot open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string buf = ss.str();
  bytes_ = buf.size();
  if (buf.size() < 8 || buf.compare(0, 6, "HD8LM1") != 0) {
    *err = path + ": not an HD8LM1 file";
    return false;
  }
  size_t pos = 8;
  uint32_t order = 0, nv = 0;
  int32_t unk = 0;
  if (!ReadPod(buf, &pos, &order) || !ReadPod(buf, &pos, &nv) || !ReadPod(buf, &pos, &unk) || order < 1 ||
      order > static_cast<uint32_t>(kMaxOrder)) {
    *err = path + ": bad header";
    return false;
  }
  order_ = static_cast<int>(order);
  unkProb_ = unk;
  vocab_.reserve(nv);
  for (uint32_t i = 0; i < nv; ++i) {
    uint32_t len = 0;
    if (!ReadPod(buf, &pos, &len) || pos + len > buf.size()) {
      *err = path + ": bad vocab";
      return false;
    }
    vocab_.emplace(buf.substr(pos, len), i);
    pos += len;
  }
  tables_.assign(order_, {});
  for (int n = 0; n < order_; ++n) {
    uint64_t cnt = 0;
    if (!ReadPod(buf, &pos, &cnt) || pos + cnt * 16 > buf.size()) {
      *err = path + ": bad table";
      return false;
    }
    auto& t = tables_[n];
    t.resize(cnt);
    for (uint64_t i = 0; i < cnt; ++i) {
      ReadPod(buf, &pos, &t[i].key);
      ReadPod(buf, &pos, &t[i].prob);
      ReadPod(buf, &pos, &t[i].bow);
    }
    for (uint64_t i = 1; i < cnt; ++i)
      if (t[i - 1].key >= t[i].key) {
        *err = path + ": table not sorted";
        return false;
      }
  }
  return true;
}

size_t Lm::entries() const {
  size_t n = 0;
  for (auto& t : tables_) n += t.size();
  return n;
}

uint32_t Lm::Id(const std::string& tok) const {
  auto it = vocab_.find(tok);
  return it == vocab_.end() ? kUnk : it->second;
}

LmState Lm::Begin() const {
  LmState s;
  s.ids[0] = kBos;
  s.len = 1;
  return s;
}

const Lm::Entry* Lm::Find(int n, const uint32_t* ids) const {
  const auto& t = tables_[n - 1];
  const uint64_t k = NgramKey(ids, n);
  auto it = std::lower_bound(t.begin(), t.end(), k, [](const Entry& e, uint64_t key) { return e.key < key; });
  return it != t.end() && it->key == k ? &*it : nullptr;
}

int32_t Lm::Score(const LmState& in, uint32_t w, LmState* out) const {
  uint32_t buf[kMaxOrder];
  const int h = std::min<int>(in.len, order_ - 1);
  for (int i = 0; i < h; ++i) buf[i] = in.ids[in.len - h + i];
  buf[h] = w;
  int32_t total = 0;
  int32_t prob = unkProb_;
  bool found = false;
  // longest context first: p(w | ctx) if (ctx, w) is known, else bow(ctx) + p(w | shorter ctx)
  for (int k = h; k >= 0; --k) {
    const uint32_t* start = buf + (h - k);
    if (const Entry* e = Find(k + 1, start)) {
      prob = e->prob;
      found = true;
      break;
    }
    if (k > 0)
      if (const Entry* c = Find(k, start)) total += c->bow;
  }
  if (!found) total = 0;  // unknown token: no back-off mass, the unk penalty alone
  total += prob;
  // successor state: longest suffix of (ctx, w) of length <= order-1 that the model knows as a context
  int keep = std::min(h + 1, order_ - 1);
  while (keep > 0) {
    const uint32_t* start = buf + (h + 1 - keep);
    if (Find(keep, start)) break;
    --keep;
  }
  out->len = static_cast<uint8_t>(keep);
  for (int i = 0; i < keep; ++i) out->ids[i] = buf[h + 1 - keep + i];
  return total;
}

}  // namespace hd8::smt
