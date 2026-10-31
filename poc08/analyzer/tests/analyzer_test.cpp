// Analyzer tests: normalizer tables and, on the shipped data, short real-shaped inputs.
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "hd8/analyzer.h"
#include "hdtest.h"
#include "model.h"
#include "normalize.h"
#include "utf8.h"

using namespace hd8::ana;

namespace {

std::string NormText(std::string_view src, std::vector<NormEdit>* edits) {
  std::vector<internal::NChar> nc;
  internal::Normalize(src, &nc, edits);
  std::string out;
  for (auto& c : nc) internal::AppendUtf8(out, c.c);
  return out;
}

bool ReadText(const std::string& path, std::string* out) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[65536];
  size_t k;
  while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, k);
  std::fclose(f);
  return true;
}

const Analyzer* Shared() {
  static Analyzer* a = [] {
    auto* an = new Analyzer;
    Model m;
    std::string err;
    if (!LoadTsvDir(HD08_ANALYZER_DATA, &m, &err)) {
      std::fprintf(stderr, "analyzer data: %s\n", err.c_str());
      return an;
    }
    Finalize(&m);
    if (!an->Load(Serialize(m), &err)) std::fprintf(stderr, "snapshot: %s\n", err.c_str());
    return an;
  }();
  return a->model() ? a : nullptr;
}

std::string Seg(std::string_view src) {
  const Analyzer* a = Shared();
  if (!a) return "";
  Analysis r;
  a->Analyze(src, Options{}, &r);
  std::string out;
  for (auto& t : r.toks) {
    if (!out.empty()) out += ' ';
    out += t.surf;
  }
  return out;
}

const Token* Find(const Analysis& r, std::string_view surf) {
  for (auto& t : r.toks)
    if (t.surf == surf) return &t;
  return nullptr;
}

}  // namespace

HD_TEST(normalize_width) {
  std::vector<NormEdit> e;
  HD_CHECK_EQ(NormText("ｶﾞｲﾄﾞﾌﾞｯｸ｡", &e), std::string("ガイドブック。"));
  HD_REQUIRE(e.size() == 1);
  HD_CHECK(e[0].kind == EditKind::width);
  HD_CHECK_EQ(e[0].from, std::string("ｶﾞｲﾄﾞﾌﾞｯｸ｡"));
}

HD_TEST(normalize_ruby) {
  std::vector<NormEdit> e;
  HD_CHECK_EQ(NormText("｜魔導師《ウィザード》が来た", &e), std::string("魔導師が来た"));
  HD_CHECK_EQ(NormText("彼女（かのじょ）は笑った", &e), std::string("彼女は笑った"));
  HD_REQUIRE(e.size() == 1);
  HD_CHECK(e[0].kind == EditKind::ruby);
  // a speaker label is not a reading
  HD_CHECK_EQ(NormText("(城島)楽しみ", &e), std::string("(城島)楽しみ"));
  HD_CHECK(e.empty());
}

HD_TEST(normalize_keeps_spaces) {
  std::vector<NormEdit> e;
  HD_CHECK_EQ(NormText("通 知 表", &e), std::string("通 知 表"));
  HD_CHECK(e.empty());
}

HD_TEST(analyze_offsets) {
  const Analyzer* a = Shared();
  if (!a) HD_SKIP("no analyzer data");
  std::string src = "ｶﾞｲﾄﾞを見た。";
  Analysis r;
  a->Analyze(src, Options{}, &r);
  HD_REQUIRE(!r.toks.empty());
  HD_CHECK_EQ(r.toks[0].surf, std::string("ガイド"));
  HD_CHECK_EQ(r.toks[0].src.begin, 0u);
  HD_CHECK_EQ(r.toks[0].src.end, static_cast<uint32_t>(std::string("ｶﾞｲﾄﾞ").size()));
  HD_CHECK_EQ(r.map.size(), r.norm.size() + 1);
  HD_CHECK_EQ(r.toks.back().src.end, static_cast<uint32_t>(src.size()));
}

HD_TEST(analyze_katakana_unknown_is_noun) {
  const Analyzer* a = Shared();
  if (!a) HD_SKIP("no analyzer data");
  Analysis r;
  a->Analyze("ヴァーゲルディアス家としても賛成します", Options{}, &r);
  HD_REQUIRE(!r.toks.empty());
  HD_CHECK_EQ(r.toks[0].surf, std::string("ヴァーゲルディアス"));
  HD_CHECK(r.toks[0].pos == Pos::n || r.toks[0].pos == Pos::pn);
}

HD_TEST(analyze_space_is_boundary) {
  if (!Shared()) HD_SKIP("no analyzer data");
  // a space between words is never crossed
  HD_CHECK_EQ(Seg("この 本を"), std::string("この 本 を"));
  // single kanji separated by single spaces join (通 知 表)
  Analysis r;
  Shared()->Analyze("通 知 表をもらった", Options{}, &r);
  bool join = false;
  for (auto& e : r.edits) join = join || e.kind == EditKind::join;
  HD_CHECK(join);
  HD_REQUIRE(!r.toks.empty());
  HD_CHECK(r.toks[0].surf != "通");
}

HD_TEST(analyze_spoken_derived) {
  const Analyzer* a = Shared();
  if (!a) HD_SKIP("no analyzer data");
  Analysis r;
  a->Analyze("静かに飲みてえってときに", Options{}, &r);
  const Token* t = Find(r, "てえ");
  HD_REQUIRE(t != nullptr);
  HD_CHECK_EQ(t->lemma, std::string("たい"));
  HD_CHECK(t->flags & Spoken);
  HD_CHECK(!t->derive.empty());
}

// Every case of ja/repair.cases.tsv: the repaired text of a fix case, the unchanged text of a keep case.
HD_TEST(repair_cases) {
  static Analyzer* a = [] {
    auto* an = new Analyzer;
    Model m;
    std::string err;
    if (!LoadTsvDir(HD08_ANALYZER_DATA, &m, &err)) return an;
    Finalize(&m);
    if (!an->Load(Serialize(m), &err)) return an;
    std::string tsv;
    if (!ReadText(std::string(HD08_ANALYZER_DATA) + "/ja/repair.tsv", &tsv) || !an->LoadRepair(tsv, &err))
      std::fprintf(stderr, "repair.tsv: %s\n", err.c_str());
    return an;
  }();
  if (!a->model() || !a->HasRepair()) HD_SKIP("no analyzer or repair data");
  std::string cases;
  HD_REQUIRE(ReadText(std::string(HD08_ANALYZER_DATA) + "/ja/repair.cases.tsv", &cases));
  Options opt;
  opt.margins = false;
  opt.repair = true;
  int n = 0, bad = 0;
  bool header = false;
  size_t at = 0;
  while (at < cases.size()) {
    size_t nl = cases.find('\n', at);
    std::string line = cases.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    at = nl == std::string::npos ? cases.size() : nl + 1;
    if (line.empty() || line[0] == '#') continue;
    if (!header) {
      header = true;
      continue;
    }
    std::vector<std::string> f;
    for (size_t s = 0, t; ; s = t + 1) {
      t = line.find('\t', s);
      f.push_back(line.substr(s, t == std::string::npos ? std::string::npos : t - s));
      if (t == std::string::npos) break;
    }
    HD_REQUIRE(f.size() >= 4);
    Analysis r;
    a->Analyze(f[2], opt, &r);
    std::string got = r.repairs.empty() ? f[2] : r.repaired;
    ++n;
    if (got != f[3]) {
      ++bad;
      std::fprintf(stderr, "  %s %s: %s -> %s (want %s)\n", f[0].c_str(), f[1].c_str(), f[2].c_str(), got.c_str(),
                   f[3].c_str());
    }
  }
  std::printf("repair cases: %d, wrong %d\n", n, bad);
  HD_CHECK(n > 0);
  HD_CHECK_EQ(bad, 0);
}

// With a core lexicon, a fallback word that a repair turns into core words is read again: ごさ is outside the
// core (here the closed classes alone), so ありがとうごさいます is read as ございます.
HD_TEST(repair_core_fallback) {
  Analyzer an;
  Model m;
  std::string err, tsv;
  if (!LoadTsvDir(HD08_ANALYZER_DATA, &m, &err)) HD_SKIP("no analyzer data");
  Finalize(&m);
  HD_REQUIRE(an.Load(Serialize(m), &err));
  HD_REQUIRE(ReadText(std::string(HD08_ANALYZER_DATA) + "/ja/repair.tsv", &tsv));
  HD_REQUIRE(an.LoadRepair(tsv, &err));
  Options opt;
  opt.margins = false;
  opt.repair = true;
  Analysis r;
  an.Analyze("ありがとうごさいます", opt, &r);
  HD_CHECK(r.repairs.empty());  // without a core lexicon every word is core
  HD_REQUIRE(an.SetRepairCore("-\n"));
  an.Analyze("ありがとうごさいます", opt, &r);
  HD_CHECK_EQ(r.repaired, std::string("ありがとうございます"));
  an.Analyze("ありがとうございます", opt, &r);
  HD_CHECK(r.repairs.empty());
  // spoken lengthening never changes, even of a word outside the core
  for (const char* keep : {"「すごーい！」", "「ちがーう！」", "「だーれだ！」", "「いいなー？」"}) {
    an.Analyze(keep, opt, &r);
    HD_CHECK(r.repairs.empty());
  }
}

int main(int argc, char** argv) {
  const std::string_view filter = argc > 1 ? argv[1] : "";
  int failed = 0, run = 0;
  for (const hdtest::Test& t : hdtest::Registry()) {
    if (!filter.empty() && std::string_view(t.name).find(filter) == std::string_view::npos) continue;
    hdtest::Cur() = {};
    t.fn();
    run++;
    if (hdtest::Cur().failures) {
      failed++;
      std::fprintf(stderr, "FAIL %s\n", t.name);
    }
  }
  std::printf("%d tests, %d failed\n", run, failed);
  return failed ? 1 : 0;
}
