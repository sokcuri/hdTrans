#include "core/word/kana_tables.h"

#include <cstdint>
#include <string>

#include "fnv.h"
#include "hdtest.h"

using namespace hdTrans::word;

namespace {

uint64_t PairFingerprint(std::span<const KanaPair> rows) {
    hdtest::Fnv h;
    h.U32(static_cast<uint32_t>(rows.size()));
    for (const auto& r : rows) {
        h.Str(r.kana);
        h.Str(r.hangul);
    }
    return h.h;
}

}  // namespace

// The expected values are the hashes of the tables the data was generated from, cell
// by cell and row by row in table order.
HD_TEST(kana_tables_fingerprints) {
    HD_CHECK_EQ(PairFingerprint(KanaDirect()), uint64_t{0x61af42a220cf1a02ull});
    HD_CHECK_EQ(PairFingerprint(HiraganaRows()), uint64_t{0x28dfd139d3b063b4ull});

    hdtest::Fnv matrix;
    matrix.U32(kKanaMatrixRows * 5);
    for (int i = 0; i < kKanaMatrixRows * 5; i++) matrix.Str(KanaMatrixKatakana(i));
    HD_CHECK_EQ(matrix.h, uint64_t{0xd144dabc8d0349d0ull});

    hdtest::Fnv cells;
    cells.U32(kKanaMatrixRows);
    cells.U32(kKanaCells);
    for (int row = 0; row < kKanaMatrixRows; row++) {
        for (int n = 0; n < kKanaCells; n++) cells.Str(KanaCell(row, n));
    }
    HD_CHECK_EQ(cells.h, uint64_t{0xda3b498487605c3full});

    // The row each row's own hiragana selects (row 89 repeats row 88 and is never selected).
    hdtest::Fnv matches;
    const auto rows = HiraganaRows();
    matches.U32(static_cast<uint32_t>(rows.size()));
    for (const auto& r : rows) matches.U32(static_cast<uint32_t>(MatchHiragana(r.kana)));
    HD_CHECK_EQ(matches.h, uint64_t{0x0f21b0007a1a1c48ull});

    HD_CHECK_EQ(KanaDirect().size(), size_t{64});
    HD_CHECK_EQ(rows.size(), size_t{474});
}

HD_TEST(kana_tables_lookups) {
    HD_CHECK_EQ(KanaMatrixIndex("ア"), 0);
    HD_CHECK_EQ(KanaMatrixIndex("ル"), 62);
    HD_CHECK_EQ(KanaMatrixIndex("ン"), -1);
    HD_CHECK_EQ(KanaMatrixIndex("@$"), 66);
    HD_CHECK_EQ(KanaMatrixIndex(""), -1);
    HD_CHECK_EQ(std::string(KanaMatrixKatakana(74)), std::string("@$"));
    HD_CHECK(KanaMatrixKatakana(75).empty());
    HD_CHECK_EQ(std::string(KanaCell(1, 0)), std::string("카"));
    HD_CHECK_EQ(std::string(KanaCell(1, 17)), std::string("쿤"));  // カ row, ウ column, final ㄴ
    HD_CHECK_EQ(std::string(KanaCell(12, 8)), std::string("르"));
    HD_CHECK_EQ(std::string(KanaCell(0, 6)), std::string("  "));
    HD_CHECK(KanaCell(15, 0).empty());
    HD_CHECK(KanaCell(0, 45).empty());
    HD_CHECK_EQ(FindKanaCell(1, "킨"), 16);
    HD_CHECK_EQ(FindKanaCell(0, "  "), 6);
    HD_CHECK_EQ(FindKanaCell(1, "가"), -1);
    HD_CHECK_EQ(std::string(KanaDirect()[0].kana), std::string("ヴァ"));
    HD_CHECK_EQ(MatchHiragana("じょうず"), 88);
    HD_CHECK_EQ(MatchHiragana("あぁっと"), 0);
    HD_CHECK_EQ(MatchHiragana("abc"), -1);
    HD_CHECK_EQ(MatchHiragana(""), -1);
}
