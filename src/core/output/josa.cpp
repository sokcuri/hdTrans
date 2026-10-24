#include "core/output/josa.h"

#include <vector>

#include "core/dict/char_readings.h"
#include "core/output/cleanup.h"
#include "core/output/compose.h"
#include "core/output/particle.h"
#include "core/output/text.h"
#include "core/text/hangul.h"
#include "core/text/utf8.h"

namespace hdTrans::output {

namespace {

// One width unit of a text as the particle scanner reads it: an ASCII character (half
// 0) or one half (1, 2) of another character.
struct Cell {
    char32_t r = 0;
    int half = 0;
};

void CellsOf(std::string_view s, std::vector<Cell>& cs) {
    cs.clear();
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        if (r.cp < 0x80) {
            cs.push_back({r.cp, 0});
        } else {
            cs.push_back({r.cp, 1});
            cs.push_back({r.cp, 2});
        }
        i += r.size;
    }
}

Cell CellAt(const std::vector<Cell>& cs, int i) {
    if (i < 0 || static_cast<size_t>(i) >= cs.size()) return {};
    return cs[static_cast<size_t>(i)];
}

// The ASCII byte of an ASCII cell; other cells read as 0x80 and cells outside the text
// as 0, neither of which is a letter, digit or %.
char Letter(Cell c) { return c.half != 0 ? static_cast<char>(0x80) : static_cast<char>(c.r); }

bool IsAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// The closing brackets 〕〉》」』】.
bool IsClosing(char32_t r) {
    switch (r) {
        case U'〕': case U'〉': case U'》': case U'」': case U'』': case U'】': return true;
    }
    return false;
}

struct Ending {
    int kind = 0;  // 1 an ASCII letter (or %), 2 another character, 0 none
    char32_t r = 0;
};

// The last character of the first length cells of cs that decides the particle form.
// trailing counts the non-letter ASCII characters after it (it is not reset when
// nothing is found).
Ending ScanEnding(const std::vector<Cell>& cs, int length, int& trailing) {
    if (length <= 0) return {};
    int index = 0;
    int kind = 0;
    if (!cs.empty()) {
        for (;;) {
            const Cell c = CellAt(cs, index);
            if (c.half != 0) {
                if (IsClosing(c.r)) {
                    const char prev = Letter(CellAt(cs, index - 1));
                    if (index >= 2 && index + 2 >= length) {
                        if (CellAt(cs, index - 2).half != 0) {
                            kind = 2;
                            break;
                        }
                        if (IsAlpha(prev) || prev == '%') {
                            kind = 1;
                            trailing = 0;
                            break;
                        }
                    } else if (index >= 1 && index + 1 >= length) {
                        if (IsAlpha(prev) || prev == '%') {
                            kind = 1;
                            trailing = 0;
                            break;
                        }
                    }
                }
                index += 2;
                kind = 2;
                trailing = 0;
                if (index >= length) break;
            } else {
                if (IsAlpha(static_cast<char>(c.r)) || c.r == U'%') {
                    kind = 1;
                    trailing = 0;
                } else {
                    trailing++;
                }
                index++;
                if (index >= length) break;
            }
            if (static_cast<size_t>(index) >= cs.size()) break;
        }
    }
    if (kind == 2) {
        const Cell a = CellAt(cs, index - trailing - 2), b = CellAt(cs, index - trailing - 1);
        if (a.half != 1 || b.half != 2) return {2, 0};
        return {2, a.r};
    }
    return {kind, 0};
}

// 0 no final consonant, 1 final ㄹ, 2 another final; a hanja reads as its Hangul
// reading.
int EndingClass(char32_t r) {
    if (const auto reading = dict::Reading(r)) r = *reading;
    return text::FinalClass(r);
}

}  // namespace

std::string ApplyJosaAlternation(std::string_view text, std::string_view context, bool hasContext) {
    if (hasContext && context.find('%') != std::string_view::npos) {
        if (const size_t p = text.find(')'); p != std::string_view::npos) return std::string(text.substr(p + 1));
    }
    std::string v(text);
    if (internal::WidthOf(v) < 6) return v;
    const auto table = JosaTable();
    auto replace = [&](size_t pos, int row, int column) {
        const JosaForms& f = table[static_cast<size_t>(row)];
        const std::string_view form = column == 1 ? f.consonant : f.vowel;
        v.replace(pos, f.pattern.size(), form);
        return pos + form.size();
    };
    auto skipChar = [&](size_t pos) { return pos + text::DecodeRune(std::string_view(v).substr(pos)).size; };
    std::vector<Cell> contextCells, cells;
    CellsOf(context, contextCells);
    const int contextLength = static_cast<int>(contextCells.size());
    size_t pos = 0;
    while (pos < v.size()) {
        const int row = JosaRow(std::string_view(v).substr(pos));
        if (row < 0) {
            pos = skipChar(pos);
            continue;
        }
        CellsOf(v, cells);
        const int before = internal::WidthOf(std::string_view(v).substr(0, pos));
        int trailing = 0;
        Ending e;
        if (pos == 0 && hasContext) {
            e = ScanEnding(contextCells, contextLength, trailing);
        } else if (v.starts_with("」")) {
            if (hasContext && !context.empty()) e = ScanEnding(contextCells, contextLength, trailing);
        } else {
            e = ScanEnding(cells, before, trailing);
        }
        char tail[internal::kLatinWindow];  // the ASCII letters or the digit before the particle
        size_t tailSize = 0;
        auto read = [&](const std::vector<Cell>& cs, int n) {
            tailSize = 0;
            for (int k = n - 1; k >= 0 && tailSize < internal::kLatinWindow; k--) {
                const char c = Letter(CellAt(cs, k));
                if (!IsAlpha(c) && (tailSize > 0 || c < '0' || c > '9')) break;
                tail[internal::kLatinWindow - 1 - tailSize++] = c;
                if (!IsAlpha(c)) break;
            }
        };
        if (e.kind == 2) {
            switch (EndingClass(e.r)) {
                case 0: pos = replace(pos, row, 2); break;
                case 1: pos = replace(pos, row, row == 1 ? 2 : 1); break;  // 로 after ㄹ
                default: pos = replace(pos, row, 1); break;
            }
            continue;
        }
        if (e.kind == 0) {
            if (pos != 0 && !hasContext) {
                read(cells, trailing);
            } else if (pos == 0 && hasContext) {
                read(contextCells, trailing);
            } else {
                pos++;  // past the '(' that starts every row pattern
                continue;
            }
        } else if (hasContext) {
            if (pos == 0) {
                read(cells, contextLength - trailing);
            } else {
                read(contextCells, before - trailing);
            }
        }
        const std::string_view letters(tail + internal::kLatinWindow - tailSize, tailSize);
        pos = replace(pos, row, internal::EndingConsonant(letters) ? 1 : 2);
        if (pos < v.size()) pos = skipChar(pos);
    }
    return v;
}


std::string Composer::ApplyParenthesisTextFilter(std::string text, int i) {
    if (internal::WidthOf(text) < 4 || text.find('(') == std::string::npos || text.find(')') == std::string::npos) {
        return text;
    }
    const std::vector<ResolvedItem>& items = *items_;
    const int linked = i - 1;
    if (linked < 0) return Alternate(text, {}, false);
    auto entry = [&](int k) { return std::string_view(internal::Selected(items[k]).text); };
    int ctx;
    const std::string_view e = entry(linked);
    if (e == ")") {
        int opening = linked;
        while (opening >= 0 && entry(opening) != "(") opening--;
        ctx = opening >= 0 ? opening - 1 : linked - 1;
        if (ctx < 0) return Alternate(text, {}, false);
        return Alternate(text, Context(std::string(entry(ctx))), true);
    }
    if (e == "」" || e == "," || e == "”") {
        ctx = linked - 1;
        if (ctx < 0) return Alternate(text, {}, false);
    } else {
        ctx = linked;
    }
    return Alternate(text, Context(WriteCount(ctx, Build(ctx, 0, {}))), true);
}

std::string Composer::Alternate(std::string_view text, std::string_view context, bool hasContext) const {
    if (settings_.text) return internal::AlternateParticles(text, context);
    return ApplyJosaAlternation(text, context, hasContext);
}

std::string Composer::Context(std::string text) const {
    if (settings_.cleanup) RemoveGlosses(text);
    return text;
}

}  // namespace hdTrans::output
