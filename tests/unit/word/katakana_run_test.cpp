// A word with a hiragana ending stays out of the transliterated katakana run next to it, and a
// long run costs time in proportion to its length.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

namespace {

std::string Translate(const hdTrans::dict::Snapshot& snap, hdTrans::internal::Generated& generated,
                      const std::string& input) {
    const auto out = hdTrans::internal::TranslateDocument(snap, generated, {&input, 1}, {}, {});
    return out.size() == 1 ? out[0] : std::string();
}

std::string Translate(const hdTrans::dict::Snapshot& snap, const std::string& input) {
    const auto generated = hdTrans::internal::NewGenerated(snap);
    return Translate(snap, *generated, input);
}

std::string Repeat(std::string_view s, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += s;
    return out;
}

// The time of the fastest of three translations of input, with the translation.
double FastestSeconds(const hdTrans::dict::Snapshot& snap, hdTrans::internal::Generated& generated,
                      const std::string& input, std::string& out) {
    double best = 0;
    for (int i = 0; i < 3; i++) {
        const auto start = std::chrono::steady_clock::now();
        out = Translate(snap, generated, input);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        best = i == 0 ? seconds : std::min(best, seconds);
    }
    return best;
}

}  // namespace

// The adjectival-noun analysis takes the dictionary's endings, so the test needs HDTRANS_DICTS.
HD_TEST(word_katakana_run_keeps_hiragana_ending_out) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = hdTrans::dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    // ティアーズ is no word: its pieces join into one transliterated run, リアルな does not.
    HD_CHECK_EQ(Translate(*loaded.snapshot, "リアルなティアーズ"), std::string("리얼한 티아즈"));
    HD_CHECK_EQ(Translate(*loaded.snapshot, "ティアーズ"), std::string("티아즈"));
}

// A run of one katakana that is no word joins into one word a neighbour at a time, and the
// word left at the end is transliterated once, so the time grows about in proportion to the
// run, also when hiragana follows it.
HD_TEST(word_katakana_run_time_grows_with_its_length) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = hdTrans::dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Shape {
        std::string_view head, unit, tail, readHead, readUnit;
    };
    const Shape shapes[] = {
        {"", "メ", "", "", "메"},
        {"ティ", "ア", "う少しで落ちるとこだった。", "티", "아"},
    };
    for (const Shape& shape : shapes) {
        auto input = [&](int n) { return std::string(shape.head) + Repeat(shape.unit, n) + std::string(shape.tail); };
        std::string shortOut, longOut;
        const double shortRun = FastestSeconds(*loaded.snapshot, *generated, input(200), shortOut);
        const double longRun = FastestSeconds(*loaded.snapshot, *generated, input(1600), longOut);
        HD_CHECK(shortOut.starts_with(std::string(shape.readHead) + Repeat(shape.readUnit, 200)));
        HD_CHECK(longOut.starts_with(std::string(shape.readHead) + Repeat(shape.readUnit, 1600)));
        HD_CHECK(longRun < 30 * shortRun);
    }
}
