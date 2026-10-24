// A word with a hiragana ending stays out of the transliterated katakana run next to it.
#include <cstdlib>
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

namespace {

std::string Translate(const hdTrans::dict::Snapshot& snap, const std::string& input) {
    const auto generated = hdTrans::internal::NewGenerated(snap);
    hdTrans::internal::Memory memory;
    const auto out = hdTrans::internal::TranslateDocument(snap, *generated, {&input, 1}, memory, {}, {});
    return out.size() == 1 ? out[0] : std::string();
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
