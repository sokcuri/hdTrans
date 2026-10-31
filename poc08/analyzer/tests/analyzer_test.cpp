// Analyzer tests: normalizer tables and, on the shipped data, short real-shaped inputs.
#include <cstdio>
#include <string>
#include <string_view>

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
  // a lengthened word is a derived arc: the run collapses onto the dictionary word
  a->Analyze("いいいい加減にして", Options{}, &r);
  const Token* t = Find(r, "いいいい");
  HD_REQUIRE(t != nullptr);
  HD_CHECK_EQ(t->lemma, std::string("良い"));
  HD_CHECK(t->flags & Elongated);
  HD_CHECK(!t->derive.empty());
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
