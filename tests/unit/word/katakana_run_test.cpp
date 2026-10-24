// A word with a hiragana ending stays out of the transliterated katakana run next to it, and a
// long run costs time in proportion to its length.
#include <algorithm>
#include <ctime>
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
    hdTrans::internal::Memory memory;
    const auto out = hdTrans::internal::TranslateDocument(snap, generated, {&input, 1}, memory, {}, {});
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

// The processor time of the fastest of three translations of input, with the translation.
double FastestSeconds(const hdTrans::dict::Snapshot& snap, hdTrans::internal::Generated& generated,
                      const std::string& input, std::string& out) {
    double best = 0;
    for (int i = 0; i < 3; i++) {
        const std::clock_t start = std::clock();
        out = Translate(snap, generated, input);
        const double seconds = static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
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

// A run of one katakana that is no word joins into one word a neighbour at a time. Only the word
// left at the end is transliterated, so four times the run takes about four times as long; a
// transliteration at every step makes it about sixteen times.
HD_TEST(word_katakana_run_time_grows_with_its_length) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = hdTrans::dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    std::string shortOut, longOut;
    const double shortRun = FastestSeconds(*loaded.snapshot, *generated, Repeat("メ", 400), shortOut);
    const double longRun = FastestSeconds(*loaded.snapshot, *generated, Repeat("メ", 1600), longOut);
    HD_CHECK_EQ(shortOut, Repeat("메", 400));
    HD_CHECK_EQ(longOut, Repeat("메", 1600));
    HD_CHECK(longRun < 10 * shortRun);
}
