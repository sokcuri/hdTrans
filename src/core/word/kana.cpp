#include "core/word/kana.h"

#include <deque>
#include <string>
#include <vector>

#include "core/text/hangul.h"
#include "core/text/utf8.h"
#include "core/word/kana_tables.h"

namespace hdTrans::word {

namespace {

constexpr std::string_view kLongVowel = "ー";
constexpr std::string_view kInitialKu = "쿠";
constexpr std::string_view kLongRu = "루";
constexpr std::string_view kSourceI = "이";
constexpr std::string_view kDirectSet = "ファフィフュフェフォ";
constexpr std::string_view kEmptyCell = "  ";
constexpr std::string_view kReplacementChar = "\xEF\xBF\xBD";

// The markers the context rules look for.
constexpr std::string_view kMarkRu = "ル";
constexpr std::string_view kMarkYa = "ャ";
constexpr std::string_view kMarkYu = "ュ";
constexpr std::string_view kMarkYo = "ョ";
constexpr std::string_view kMarkN = "ン";
constexpr std::string_view kMarkGu = "グ";
constexpr std::string_view kMarkTsu = "ッ";
constexpr std::string_view kMarkKRow = "カキクケコ";
constexpr std::string_view kMarkKusu = "クス";
constexpr std::string_view kMarkKu = "ク";
constexpr std::string_view kMarkBPRows = "バビブベボパピプペポ";
constexpr std::string_view kMarkPu = "プ";
constexpr std::string_view kMarkMu = "ム";

// The characters of s as cells; an invalid byte is the cell U+FFFD.
void AppendCells(std::vector<std::string_view>& out, std::string_view s) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        out.push_back(r.cp == text::kReplacement && r.size == 1 ? kReplacementChar : s.substr(0, r.size));
        s.remove_prefix(r.size);
    }
}

bool KatakanaCell(std::string_view c) { return IsKatakana(text::DecodeRune(c).cp); }

// A katakana run edited as cells: one katakana, one Hangul syllable or the two spaces
// of an empty cell of the kana rows.
struct Run {
    std::vector<std::string_view> cells;
    std::vector<std::string_view> scratch;
    std::deque<std::string> composed;  // syllables made here, which cells may point to
    bool fault = false;

    long Size() const { return static_cast<long>(cells.size()); }
    std::string_view At(long i) const {
        return i < 0 || i >= Size() ? std::string_view() : cells[static_cast<std::size_t>(i)];
    }

    // Whether the cells of text start at cells[i].
    bool Has(long i, std::string_view text) const {
        if (i < 0) return false;
        if (text == kEmptyCell) return i < Size() && cells[static_cast<std::size_t>(i)] == text;
        while (!text.empty()) {
            if (i >= Size()) return false;
            const text::Rune want = text::DecodeRune(text);
            const std::string_view c = cells[static_cast<std::size_t>(i)];
            const text::Rune got = text::DecodeRune(c);
            if (got.cp != want.cp || got.size != c.size()) return false;
            text.remove_prefix(want.size);
            i++;
        }
        return true;
    }

    static std::size_t CellCount(std::string_view s) { return s == kEmptyCell ? 1 : text::RuneCount(s); }

    // Replaces the first occurrence of the cells of search at or after cells[i] with
    // replacement, a cell of the kana rows or a text of single characters.
    void Replace(long i, std::string_view search, std::string_view replacement) {
        if (fault) return;
        for (long j = i < 0 ? 0 : i; j <= Size(); j++) {
            if (!Has(j, search)) continue;
            scratch.assign(cells.begin(), cells.begin() + j);
            if (replacement == kEmptyCell) scratch.push_back(replacement);
            else AppendCells(scratch, replacement);
            scratch.insert(scratch.end(), cells.begin() + j + static_cast<long>(CellCount(search)), cells.end());
            cells.swap(scratch);
            return;
        }
        fault = true;
    }

    void ApplyFinal(long i, int row);
    void AddFinal(long i, int final, std::string_view mark);
    void AddFinalN(long i);
    void Matrix(long cursor);
    void Transliterate();
};

// Applies the ン, ッ, ム and ー rules to the syllable at cells[i]: the variant n
// cells further in the row, after the syllable's first cell in the row, replaces it,
// and the kana that made it are dropped.
void Run::ApplyFinal(long i, int row) {
    auto variant = [&](int n, std::string_view& v) {
        const std::string_view current = At(i);
        int j = 0;
        if (!current.empty()) j = FindKanaCell(row, current);
        if (j < 0 || j + n >= kKanaCells) return false;
        v = KanaCell(row, j + n);
        return true;
    };
    std::string_view v;
    const std::string_view after = At(i + 1);
    if (after == kMarkN) {
        if (At(i + 2) == kMarkGu) {
            if (variant(40, v)) {
                Replace(i, At(i), v);
                Replace(i + 2, kMarkGu, "");
                Replace(i + 1, kMarkN, "");
            }
        } else if (variant(15, v)) {
            Replace(i, At(i), v);
            Replace(i + 1, kMarkN, "");
        }
    } else if (after == kMarkTsu) {
        const std::string_view following = At(i + 2);
        int n = 35;
        if (!following.empty() && kMarkKRow.find(following) != std::string_view::npos) n = 10;
        else if (following == kMarkGu) n = 10;  // ビッグ 빅
        else if (!following.empty() && kMarkBPRows.find(following) != std::string_view::npos) n = 30;
        if (variant(n, v)) {
            Replace(i, At(i), v);
            if (n == 30 && following == kMarkPu) Replace(i + 2, kMarkPu, "");
            if (following == kMarkGu) Replace(i + 2, kMarkGu, "");
            Replace(i + 1, kMarkTsu, "");
            if (n == 10 && Has(i + 1, kMarkKusu)) Replace(i + 1, kMarkKu, "");
        }
    }
    // ム before the long mark is a syllable of its own (スムーズ, ムーン).
    if (At(i + 1) == kMarkMu && At(i + 2) != kLongVowel && variant(25, v)) {
        Replace(i, At(i), v);
        Replace(i + 1, kMarkMu, "");
    }
    if (At(i + 1) == kLongVowel) Replace(i + 1, kLongVowel, "");
}

// Writes the kana mark at cells[i + 1] as the final of the syllable at cells[i] (final is
// the index of the final consonant), when the syllable has no final.
void Run::AddFinal(long i, int final, std::string_view mark) {
    const text::Rune r = text::DecodeRune(At(i));
    if (r.cp < U'가' || r.cp > U'힣' || (r.cp - U'가') % 28 != 0) return;
    std::string& withFinal = composed.emplace_back();
    text::AppendRune(withFinal, r.cp + static_cast<char32_t>(final));
    Replace(i, At(i), withFinal);
    Replace(i + 1, mark, "");
}

void Run::AddFinalN(long i) { AddFinal(i, 4, kMarkN); }

// Transliterates the katakana at cells[cursor] by the matrix of single katakana,
// looking at the small kana, ル and ー after it.
void Run::Matrix(long cursor) {
    const int index = KanaMatrixIndex(At(cursor));
    if (index < 0) return;
    const int row = index / 5, column = index % 5;
    const std::string_view search = KanaMatrixKatakana(index);
    const std::string_view next = At(cursor + 1);
    const bool longVowelNext = next == kLongVowel;
    if (column != 3 && longVowelNext) {
        if (row == 12 && column == 2) {
            // A long ル is the vowel u (ルーク 루크, ルーン 룬, ルール 룰).
            Replace(cursor, search, kLongRu);
            Replace(cursor + 1, kLongVowel, "");
            if (Has(cursor + 1, kMarkN)) {
                AddFinalN(cursor);
            } else if (cursor + 2 == Size() && Has(cursor + 1, kMarkRu)) {
                AddFinal(cursor, 8, kMarkRu);
            }
        } else if (column == 0 && cursor >= Size() - 1) {
            Replace(cursor, search, KanaCell(row, 6));
            Replace(cursor + 1, kLongVowel, "");
        } else {
            Replace(cursor, search, KanaCell(row, column));
            Replace(cursor + 1, kLongVowel, "");
            ApplyFinal(cursor, row);
        }
    } else if (cursor >= Size() - 2 && next == kMarkRu) {
        Replace(cursor, search, KanaCell(row, column + 20));
        Replace(cursor + 1, kMarkRu, "");
    } else if (column == 1 && (next == kMarkYa || next == kMarkYu || next == kMarkYo)) {
        const int cell = next == kMarkYa ? 5 : next == kMarkYu ? 7 : 9;
        Replace(cursor, search, KanaCell(row, cell));
        Replace(cursor + 1, next, "");
        ApplyFinal(cursor, row);
    } else if (column == 3 && longVowelNext) {
        Replace(cursor, search, KanaCell(row, 3));
        Replace(cursor + 1, kLongVowel, kSourceI);
        ApplyFinal(cursor + 1, 0);
    } else if (row == 12 && column == 2 && cursor == 0) {
        Replace(cursor, search, KanaCell(12, 8));
        ApplyFinal(cursor, 12);
    } else if (row == 1 && column == 2 && cursor == 0) {
        Replace(cursor, search, kInitialKu);
    } else {
        Replace(cursor, search, KanaCell(row, column));
        ApplyFinal(cursor, row);
    }
}

void Run::Transliterate() {
    for (long cursor = 0; cursor < Size() && !fault; cursor++) {
        if (At(cursor) == kLongVowel) cursor++;
        bool direct = false;
        for (const KanaPair& e : KanaDirect()) {
            const std::string_view search = e.kana, value = e.hangul;
            if (!Has(cursor, search)) continue;
            Replace(cursor, search, value);
            if (text::RuneCount(search) == 2 && kDirectSet.find(search) != std::string_view::npos) {
                ApplyFinal(cursor, 10);
            } else if (text::RuneCount(value) == 1 && At(cursor + 1) == kLongVowel) {
                ApplyFinal(cursor, 10);
            }
            // A ン after the syllable, or after the long mark it drops, is its final ㄴ
            // (ヴァン 반, ティーン 틴).
            if (Has(cursor + 1, kMarkN) && text::RuneCount(value) == 1) AddFinalN(cursor);
            direct = true;
            break;
        }
        if (!direct) Matrix(cursor);
    }
}

}  // namespace

bool TransliterateKatakana(std::string_view s, std::string& out) {
    std::vector<std::string_view> cells;
    AppendCells(cells, s);
    Run run;
    for (std::size_t i = 0; i < cells.size();) {
        if (!KatakanaCell(cells[i])) {
            i++;
            continue;
        }
        std::size_t end = i;
        while (end < cells.size() && KatakanaCell(cells[end])) end++;
        run.cells.assign(cells.begin() + static_cast<long>(i), cells.begin() + static_cast<long>(end));
        run.Transliterate();
        if (run.fault) return false;
        cells.erase(cells.begin() + static_cast<long>(i), cells.begin() + static_cast<long>(end));
        cells.insert(cells.begin() + static_cast<long>(i), run.cells.begin(), run.cells.end());
        i += run.cells.size();
    }
    out.clear();
    for (const std::string_view c : cells) {
        if (!KatakanaCell(c)) out += c;
    }
    return true;
}

bool TransliterateName(std::string_view s, std::string& out) {
    out.clear();
    std::string part;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        const std::size_t next = i + r.size;
        if (r.cp == U'ツ') {
            const char32_t after = next < s.size() ? text::DecodeRune(s.substr(next)).cp : 0;
            const bool small = next < s.size() && (after == U'ァ' || after == U'ィ' || after == U'ゥ' ||
                                                   after == U'ェ' || after == U'ォ');
            if (!small) {
                std::string_view before = s.substr(start, i - start);
                if (before.ends_with("ッ")) before.remove_suffix(std::string_view("ッ").size());
                if (!TransliterateKatakana(before, part)) return false;
                out += part;
                out += "츠";
                start = next;
            }
        }
        i = next;
    }
    if (!TransliterateKatakana(s.substr(start), part)) return false;
    out += part;
    return true;
}

std::string TransliterateHiragana(std::string_view s) {
    std::u32string out;
    auto setFinal = [&](int final) {
        if (!out.empty()) out.back() = text::SetFinal(out.back(), final);
    };
    const auto rows = HiraganaRows();
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        if ((r.cp >= U'０' && r.cp <= U'９') || (r.cp >= U'Ａ' && r.cp <= U'Ｚ') ||
            (r.cp >= U'ａ' && r.cp <= U'ｚ' && r.cp != U'ｖ')) {
            out.push_back(r.cp - 0xFEE0);
            i += r.size;
            continue;
        }
        if (r.cp != U'ん') {
            const int row = MatchHiragana(s.substr(i));
            if (row < 0) {
                i += r.size;
                continue;
            }
            for (std::string_view h = rows[static_cast<std::size_t>(row)].hangul; !h.empty();) {
                const text::Rune c = text::DecodeRune(h);
                out.push_back(c.cp);
                h.remove_prefix(c.size);
            }
            i += std::string_view(rows[static_cast<std::size_t>(row)].kana).size();
            continue;
        }
        i += r.size;
        std::string_view mapped;
        if (const int row = MatchHiragana(s.substr(i)); row >= 0) mapped = rows[static_cast<std::size_t>(row)].hangul;
        if (mapped.empty()) {
            setFinal(text::kFinalNieun);
            continue;
        }
        char32_t last = text::kReplacement;
        for (std::string_view h = mapped; !h.empty();) {
            const text::Rune c = text::DecodeRune(h);
            last = c.cp;
            h.remove_prefix(c.size);
        }
        const auto syllable = text::Split(last);
        if (!syllable) continue;
        switch (syllable->initial) {
        case text::kInitialGiyeok:
        case text::kInitialKieuk:
        case text::kInitialHieuh:
            setFinal(text::kFinalIeung);
            break;
        case text::kInitialMieum:
        case text::kInitialBieup:
            setFinal(text::kFinalMieum);
            break;
        default:
            setFinal(text::kFinalNieun);
        }
    }
    std::string result;
    for (const char32_t c : out) text::AppendRune(result, c);
    return result;
}

}  // namespace hdTrans::word
