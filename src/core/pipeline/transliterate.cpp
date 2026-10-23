// Katakana transliteration by the loanword spellings and the katakana matrix of
// word/kana_tables.h. A run is edited as cells: one katakana, one Hangul syllable or the
// two spaces of an empty matrix cell.
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline/restore.h"
#include "core/text/utf8.h"
#include "core/word/kana_tables.h"

namespace hdTrans::pipeline::internal {

namespace {

constexpr std::string_view kInitial = "쿠";
constexpr std::string_view kSource = "이";
constexpr std::string_view kLongVowel = "ー";
constexpr std::string_view kDirectSet = "ファフィフュフェフォ";
constexpr std::string_view kEmptyCell = "  ";

// The kana the context rules test.
constexpr std::string_view kRu = "ル";
constexpr std::string_view kSmallYa = "ャ";
constexpr std::string_view kSmallYu = "ュ";
constexpr std::string_view kSmallYo = "ョ";
constexpr std::string_view kN = "ン";
constexpr std::string_view kGu = "グ";
constexpr std::string_view kSmallTsu = "ッ";
constexpr std::string_view kKRow = "カキクケコ";
constexpr std::string_view kKusu = "クス";
constexpr std::string_view kKu = "ク";
constexpr std::string_view kBPRows = "バビブベボパピプペポ";
constexpr std::string_view kPu = "プ";
constexpr std::string_view kMu = "ム";
constexpr std::string_view kSmallVowels = "ァィゥェォャュョ";

using Cells = std::vector<std::string>;

bool IsKatakana(char32_t r) { return (r >= U'ァ' && r <= U'ヺ') || r == U'ー'; }

bool KatakanaCell(std::string_view c) { return !c.empty() && IsKatakana(text::DecodeRune(c).cp); }

// The runes of s, an invalid byte as U+FFFD.
std::vector<char32_t> Runes(std::string_view s) {
    std::vector<char32_t> rs;
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        rs.push_back(r.cp);
        i += r.size;
    }
    return rs;
}

Cells CellsOf(std::string_view s) {
    if (s == kEmptyCell) return {std::string(s)};
    Cells out;
    for (const char32_t r : Runes(s)) {
        std::string c;
        text::AppendRune(c, r);
        out.push_back(std::move(c));
    }
    return out;
}

std::string CellAt(const Cells& run, long i) {
    if (i < 0 || i >= static_cast<long>(run.size())) return {};
    return run[static_cast<size_t>(i)];
}

// Whether the cells of text start at run[i].
bool HasCells(const Cells& run, long i, std::string_view text) {
    if (i < 0) return false;
    const long n = static_cast<long>(run.size());
    if (text == kEmptyCell) return i < n && run[static_cast<size_t>(i)] == text;
    for (const char32_t r : Runes(text)) {
        if (i >= n) return false;
        const std::string& c = run[static_cast<size_t>(i)];
        const auto d = text::DecodeRune(c);
        if (d.cp != r || d.size != c.size()) return false;
        i++;
    }
    return true;
}

struct Run {
    Cells cells;
    bool fault = false;

    // Replaces the first occurrence of the cells of search at or after cells[i] with
    // the cells of replacement; a fault when there is none.
    void Replace(long i, std::string_view search, std::string_view replacement) {
        if (fault) return;
        const long n = static_cast<long>(cells.size());
        for (long j = i < 0 ? 0 : i; j <= n; j++) {
            if (!HasCells(cells, j, search)) continue;
            const auto at = cells.begin() + j;
            const auto removed = static_cast<long>(CellsOf(search).size());
            const Cells added = CellsOf(replacement);
            cells.insert(cells.erase(at, at + removed), added.begin(), added.end());
            return;
        }
        fault = true;
    }

    long Size() const { return static_cast<long>(cells.size()); }
};

// The ン, ッ, ム and ー rules for the syllable at run[i]: the cell n cells further in
// the matrix row than the syllable's cell replaces it, and the kana that selected it
// are removed.
void ApplyKanaFinal(Run& run, long i, int row) {
    auto variant = [&](int n, std::string& v) {
        const std::string current = CellAt(run.cells, i);
        int j = 0;
        if (!current.empty()) j = word::FindKanaCell(row, current);
        if (j < 0 || j + n >= word::kKanaCells) return false;
        v = std::string(word::KanaCell(row, j + n));
        return true;
    };
    std::string v;
    const std::string after = CellAt(run.cells, i + 1);
    if (after == kN) {
        if (CellAt(run.cells, i + 2) == kGu) {
            if (variant(40, v)) {
                run.Replace(i, CellAt(run.cells, i), v);
                run.Replace(i + 2, kGu, "");
                run.Replace(i + 1, kN, "");
            }
        } else if (variant(15, v)) {
            run.Replace(i, CellAt(run.cells, i), v);
            run.Replace(i + 1, kN, "");
        }
    } else if (after == kSmallTsu) {
        const std::string following = CellAt(run.cells, i + 2);
        int n = 35;
        if (!following.empty() && kKRow.find(following) != std::string_view::npos) {
            n = 10;
        } else if (!following.empty() && kBPRows.find(following) != std::string_view::npos) {
            n = 30;
        }
        if (variant(n, v)) {
            run.Replace(i, CellAt(run.cells, i), v);
            if (n == 30 && following == kPu) run.Replace(i + 2, kPu, "");
            run.Replace(i + 1, kSmallTsu, "");
            if (n == 10 && HasCells(run.cells, i + 1, kKusu)) run.Replace(i + 1, kKu, "");
        }
    }
    // ム closes the syllable (ゲーム 게임) unless ー or a small vowel makes it a syllable (ムーン 문).
    const std::string afterMu = CellAt(run.cells, i + 2);
    if (CellAt(run.cells, i + 1) == kMu && afterMu != kLongVowel &&
        (afterMu.empty() || kSmallVowels.find(afterMu) == std::string_view::npos) && variant(25, v)) {
        run.Replace(i, CellAt(run.cells, i), v);
        run.Replace(i + 1, kMu, "");
    }
    if (CellAt(run.cells, i + 1) == kLongVowel) run.Replace(i + 1, kLongVowel, "");
}

// The katakana at run[cursor] by the matrix, looking at the small kana, ル and ー after
// it.
void TransliterateMatrix(Run& run, long cursor) {
    const int index = word::KanaMatrixIndex(CellAt(run.cells, cursor));
    if (index < 0) return;
    const int row = index / 5, column = index % 5;
    const std::string search(word::KanaMatrixKatakana(index));
    const std::string next = CellAt(run.cells, cursor + 1);
    const bool longVowelNext = next == kLongVowel;
    auto cell = [&](int r, int n) { return word::KanaCell(r, n); };
    if (column != 3 && longVowelNext) {
        if (row == 12 && column == 2 && cursor == 0) {
            run.Replace(cursor, search, cell(12, 8));
            run.Replace(cursor + 1, kLongVowel, "");
            ApplyKanaFinal(run, cursor, 12);
        } else if (column == 0 && cursor >= run.Size() - 1) {
            run.Replace(cursor, search, cell(row, 6));
            run.Replace(cursor + 1, kLongVowel, "");
        } else {
            run.Replace(cursor, search, cell(row, column));
            run.Replace(cursor + 1, kLongVowel, "");
            ApplyKanaFinal(run, cursor, row);
        }
    } else if (cursor >= run.Size() - 2 && next == kRu) {
        run.Replace(cursor, search, cell(row, column + 20));
        run.Replace(cursor + 1, kRu, "");
    } else if (column == 1 && (next == kSmallYa || next == kSmallYu || next == kSmallYo)) {
        const int n = next == kSmallYa ? 5 : next == kSmallYu ? 7 : 9;
        run.Replace(cursor, search, cell(row, n));
        run.Replace(cursor + 1, next, "");
        ApplyKanaFinal(run, cursor, row);
    } else if (column == 3 && longVowelNext) {
        run.Replace(cursor, search, cell(row, 3));
        run.Replace(cursor + 1, kLongVowel, kSource);
        ApplyKanaFinal(run, cursor + 1, 0);
    } else if (row == 12 && column == 2 && cursor == 0) {
        run.Replace(cursor, search, cell(12, 8));
        ApplyKanaFinal(run, cursor, 12);
    } else if (row == 1 && column == 2 && cursor == 0) {
        run.Replace(cursor, search, kInitial);
    } else {
        run.Replace(cursor, search, cell(row, column));
        ApplyKanaFinal(run, cursor, row);
    }
}

void TransliterateRun(Run& run) {
    for (long cursor = 0; cursor < run.Size() && !run.fault; cursor++) {
        if (run.cells[static_cast<size_t>(cursor)] == kLongVowel) cursor++;
        bool direct = false;
        for (const auto& e : word::KanaDirect()) {
            const std::string_view search = e.kana, value = e.hangul;
            if (!HasCells(run.cells, cursor, search)) continue;
            run.Replace(cursor, search, value);
            if (text::RuneCount(search) == 2 && kDirectSet.find(search) != std::string_view::npos) {
                ApplyKanaFinal(run, cursor, 10);
            } else if (text::RuneCount(value) == 1 && CellAt(run.cells, cursor + 1) == kLongVowel) {
                ApplyKanaFinal(run, cursor, 10);
            }
            direct = true;
            break;
        }
        if (!direct) TransliterateMatrix(run, cursor);
    }
}

}  // namespace

bool TransliterateKatakana(std::string_view s, std::string& out) {
    Cells cells;
    for (const char32_t r : Runes(s)) {
        std::string c;
        text::AppendRune(c, r);
        cells.push_back(std::move(c));
    }
    for (size_t i = 0; i < cells.size();) {
        if (!KatakanaCell(cells[i])) {
            i++;
            continue;
        }
        size_t end = i;
        while (end < cells.size() && KatakanaCell(cells[end])) end++;
        Run run{Cells(cells.begin() + static_cast<long>(i), cells.begin() + static_cast<long>(end))};
        TransliterateRun(run);
        if (run.fault) return false;
        const auto at = cells.erase(cells.begin() + static_cast<long>(i), cells.begin() + static_cast<long>(end));
        cells.insert(at, run.cells.begin(), run.cells.end());
        i += run.cells.size();
    }
    out.clear();
    for (const auto& c : cells) {
        if (!KatakanaCell(c)) out += c;
    }
    return true;
}

}  // namespace hdTrans::pipeline::internal
