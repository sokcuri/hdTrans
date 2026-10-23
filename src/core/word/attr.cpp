// The rules::Item methods of Item. di is a candidate index; -1 means the selected
// candidate, or every candidate when none is selected. The gobi (ending) variants work
// on the direct group and are false for Info details.
#include <algorithm>

#include "core/rules/match.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/word/item.h"
#include "core/word/strings.h"
#include "core/word/util.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

Item* AsItem(rules::Item* wi) { return static_cast<Item*>(wi); }

std::string_view Chunk(std::string_view s, std::size_t i, std::size_t n) {
    return s.substr(i, std::min(n, s.size() - i));
}

int Count(const Item& it) { return static_cast<int>(it.group->entries.size()); }

}  // namespace

bool Item::CheckSrc(std::string_view s) { return rules::MatchRuleSurface(group->surface, s); }

bool Item::CheckLLSrc(rules::Item* wi) {
    const Item* o = AsItem(wi);
    return o != nullptr && group->surface == o->group->surface;
}

bool Item::CheckDst(std::string_view s, int di) {
    if (di == -1 && selected == -1) {
        for (int i = 0; i < Count(*this); i++) {
            if (rules::MatchRuleSurface(EntryText(i), s)) return true;
        }
        return false;
    }
    if (di == -1) di = selected;
    return rules::MatchRuleSurface(EntryText(di), s);
}

bool Item::AssignDst(int di, std::string_view s) {
    if (Fixed()) return false;
    if (di == -1 && selected == -1) {
        for (int i = 0; i < Count(*this); i++) AssignEntrySurface(i, s);
        return true;
    }
    if (di == -1) di = selected;
    AssignEntrySurface(di, s);
    return true;
}

bool Item::AssignLLDst(rules::Item*, int) { return false; }

// A Pn, Pe or Pj grammar code on entry i makes every pattern code starting with 1, 3
// or 2 match.
bool Item::CheckSyntheticPumsa(int i, std::string_view pattern) const {
    const std::string_view attrs = GAttr(i);
    char lead;
    if (Has(attrs, "Pn")) lead = '1';
    else if (Has(attrs, "Pe")) lead = '3';
    else if (Has(attrs, "Pj")) lead = '2';
    else return false;
    for (std::size_t off = 0; off < pattern.size(); off += 3) {
        if (pattern[off] == lead) return true;
    }
    return false;
}

bool Item::CheckPumsaFallback(int i, std::string_view pattern) const {
    const std::string_view attrs = GAttr(i);
    const bool pn = Has(attrs, "Pn"), pe = Has(attrs, "Pe"), pj = Has(attrs, "Pj");
    const std::string_view code = Code(i);
    bool actual = false;
    if (pn) {
        actual = pe || pj || At(code, 0) == '1';
        if (!actual && rules::MatchRuleCode("1**", pattern)) return true;
    } else if (pe) {
        actual = pj || At(code, 0) == '3';
        if (!actual && rules::MatchRuleCode("3**", pattern)) return true;
    } else if (pj) {
        actual = At(code, 0) == '2';
        if (!actual && rules::MatchRuleCode("2**", pattern)) return true;
    } else if (rules::MatchRuleCode(code, pattern)) {
        return true;
    }
    return rules::MatchRuleCode(code, pattern);
}

bool Item::CheckPumsa(std::string_view s, int di) {
    auto check = [&](int i) { return CheckSyntheticPumsa(i, s) || CheckPumsaFallback(i, s); };
    if (di == -1 && selected == -1) {
        for (int i = 0; i < Count(*this); i++) {
            if (check(i)) return true;
        }
        return false;
    }
    if (di == -1) di = selected;
    return check(di);
}

bool Item::CheckAttr(std::string_view s, int di) {
    if (di == -1 && selected == -1) {
        if (s.empty()) return false;
        const std::string_view c = s.substr(0, std::min<std::size_t>(3, s.size()));
        for (int i = 0; i < Count(*this); i++) {
            if (w->settings.improvements.semanticAlternatives
                    ? rules::AttrChunksIn(Attr(i), s) : Has(Attr(i), c)) return true;
        }
        return false;
    }
    if (di == -1) di = selected;
    return rules::AttrChunksIn(Attr(di), s);
}

bool Item::MinAttrOperation(int di, std::string_view s) {
    if (di == -1 && selected == -1) {
        for (int i = 0; i < Count(*this); i++) {
            if (rules::MinAttrValues(Attr(i), s)) return true;
        }
        return false;
    }
    if (di == -1) di = selected;
    return rules::MinAttrValues(Attr(di), s);
}

// For op 2 the codes of wi's candidates are tested with CheckAttr; for ops 5 and 6
// each 3-byte code of wi's candidates with MinAttrOperation on this item's current
// candidate.
bool Item::CheckLLAttr(int op, rules::Item* wi, int di) {
    Item* o = AsItem(wi);
    if (o == nullptr) return false;
    switch (op) {
    case 2:
        if (o->compDI != -1) return false;
        if (di == -1) {
            if (group->entries.empty()) return false;
            for (int i = 0; i < Count(*o); i++) {
                if (CheckAttr(o->Attr(i), compDI)) return true;
            }
            return false;
        }
        for (int i = 0; i < Count(*o); i++) {
            if (!CheckAttr(o->Attr(i), di)) return true;
        }
        return false;
    case 5:
    case 6:
        if (di == -1 && group->entries.empty()) return false;
        for (int i = 0; i < Count(*o); i++) {
            const std::string_view values = o->Attr(i);
            for (std::size_t off = 0; off < values.size(); off += 3) {
                if (MinAttrOperation(compDI, Chunk(values, off, 3))) return true;
            }
        }
        break;
    }
    return false;
}

bool Item::StartsSentence() const {
    if (prev == nullptr) return true;
    if (prev->prev != nullptr) return false;
    const std::string_view b = prev->group->surface;
    return b == "「" || b == "『" || b == "（" || b == "(" || b == "“" || b == "\"";
}

bool Item::InQuote() const {
    const int depth = w != nullptr ? w->QuoteDepth(tokenIndex) : 0;
    return QuoteDepthAfter(text.substr(0, std::min(text.size(), static_cast<std::size_t>(std::max(start, 0)))), depth) > 0;
}

bool Item::JoinsNext() const {
    return next != nullptr && next->tokenIndex == tokenIndex && next->start == start + length;
}

bool Item::ClosingTsu() const {
    if (group->surface != "っ" || !EntryText(0).empty()) return false;
    if (next == nullptr) return true;
    const char32_t c = text::DecodeRune(next->group->surface).cp;
    return !text::IsLetter(c) && !text::IsDigit(c);
}

bool Item::ReadsCount(int i) {
    const std::string_view s = group->surface;
    std::size_t k = 0;
    bool numeral = false;
    while (k < s.size()) {
        const text::Rune r = text::DecodeRune(s.substr(k));
        if (text::IsDigit(r.cp) || std::u32string_view(U"〇○一二三四五六七八九十百千半").find(r.cp) != std::u32string_view::npos) {
            numeral = true;
        } else if (r.cp != U'数' && r.cp != U'何' && r.cp != U'幾') {
            break;
        }
        k += r.size;
    }
    if (k == 0) return false;
    std::string_view rest = s.substr(k);
    if (rest.empty()) return numeral;
    // A fraction (五分の一) and a count with a half (四畳半, 一時間半) are counts too.
    if (rest.starts_with("分の")) {
        const std::string_view m = rest.substr(std::string_view("分の").size());
        const char32_t r = text::DecodeRune(m).cp;
        return text::IsDigit(r) || std::u32string_view(U"一二三四五六七八九十百千").find(r) != std::u32string_view::npos;
    }
    if (rest.size() > std::string_view("半").size() && rest.ends_with("半")) rest.remove_suffix(std::string_view("半").size());
    if (w == nullptr || rest == "月" || rest == "時") return false;
    // The word must read as a count: digits or a Korean numeral first (三人 세 명, 十日 열흘,
    // 一年 일 년), not a word of its own (一番 제일).
    constexpr std::string_view kNumerals[] = {
        "한", "두", "세", "네", "다섯", "여섯", "일곱", "여덟", "아홉", "열", "스물", "스무", "서른", "마흔", "쉰",
        "예순", "일흔", "여든", "아흔", "하나", "둘", "셋", "넷", "하루", "이틀", "사흘", "나흘", "닷새", "엿새",
        "이레", "여드레", "아흐레", "보름", "며칠", "몇", "수", "반", "일", "이", "삼", "사", "오", "육", "칠",
        "팔", "구", "십", "백", "천"};
    const std::string_view ko = EntryText(i);
    if (std::none_of(ko.begin(), ko.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
        std::none_of(std::begin(kNumerals), std::end(kNumerals), [&](std::string_view n) { return ko.starts_with(n); })) {
        return false;
    }
    const Lookup l = w->DoLookup(rest, kModeGeneral);
    for (Record* c = l.records; c != nullptr; c = c->child) {
        if (c->source == rest && Has(c->attr, "Ct")) return true;
    }
    return false;
}

bool Item::CheckGAttr(std::string_view s, int di) {
    auto chunkMatches = [&](std::string_view c, std::string_view attrs, bool hzOnly, int i) {
        if (c == "Ow") return IsRuleRightBoundary();
        // Oj: the On reading of a word written right before the next one (事私, not 事…私).
        if (c == "Oj") return Has(attrs, "On") && JoinsNext();
        if (c == "Hz" && hzOnly) return StartsSentence();
        if (c == "Hz" && StartsSentence()) return true;
        // Qt: the word stands inside a quote (G!= "Qt" for narration).
        if (c == "Qt") return InQuote();
        if (c == "Td" && next == nullptr && prev == nullptr) return true;
        // Vp: a verb in the potential voice (頑張れる, 食べられる).
        if (c == "Vp") return detail != nullptr && detail->record == 4;
        // Nq: a count (三, 二十, 三人, 十日, 数日), see ReadsCount.
        if (c == "Nq") return ReadsCount(i);
        return Has(attrs, c);
    };
    if (di == -1 && selected == -1) {
        for (int i = 0; i < Count(*this); i++) {
            for (std::size_t off = 0; off < s.size(); off += 2) {
                if (chunkMatches(Chunk(s, off, 2), GAttr(i), false, i)) return true;
            }
        }
        return false;
    }
    const bool hzOnly = di == -1;
    if (di == -1) di = selected;
    for (std::size_t off = 0; off < s.size(); off += 2) {
        if (chunkMatches(Chunk(s, off, 2), GAttr(di), hzOnly, di)) return true;
    }
    return false;
}

// Whether the next item (after a parenthesised group) is missing or its first target
// starts with ) 」 ! ” = or -.
bool Item::IsRuleRightBoundary() const {
    const Item* n = next;
    if (n == nullptr) return true;
    if (n->EntryText(0) == "(") {
        while (n != nullptr && n->EntryText(0) != ")") n = n->next;
        if (n != nullptr) n = n->next;
    }
    // A small っ closing the word (行くっ」) is part of its sound, not a word after it.
    const Item* tsu = nullptr;
    while (n != nullptr && n->ClosingTsu()) {
        tsu = n;
        n = n->next;
    }
    if (n == nullptr) return true;
    // A word closed by っ before a comma or an ellipsis ends its clause (いいっ、それ,
    // すごいっ……」): the next word is 、, or only tokens of ellipsis marks stand between.
    if (tsu != nullptr && n->group->surface == "、") return true;
    if (tsu != nullptr && w != nullptr && n->tokenIndex > tsu->tokenIndex + 1) {
        bool ellipsis = true;
        for (int k = tsu->tokenIndex + 1; k < n->tokenIndex && ellipsis; k++) {
            const std::string_view t = w->tokens.Raw(static_cast<std::size_t>(k));
            ellipsis = !t.empty() && TrimRunes(t, "…‥・.").empty();
        }
        if (ellipsis) return true;
    }
    const std::string_view dst = n->EntryText(0);
    for (const std::string_view p : {std::string_view(")"), kKoCloseQuote, std::string_view("!"), std::string_view("”"),
                                     std::string_view("="), std::string_view("-")}) {
        if (dst.starts_with(p)) return true;
    }
    return false;
}

bool Item::CheckGobiSrc(std::string_view s) {
    const Group* g = DirectGroup();
    return g != nullptr && rules::MatchRuleSurface(g->surface, s);
}

bool Item::CheckGobiDst(std::string_view s, int di) {
    if (DirectGroup() == nullptr) return false;
    if (di == -1 && detail->selected == -1) {
        for (int i = 0; i < DirectCount(); i++) {
            if (detail->enabled[i] && rules::MatchRuleSurface(DirectText(i), s)) return true;
        }
        return false;
    }
    if (di == -1) di = detail->selected;
    return rules::MatchRuleSurface(DirectText(di), s);
}

bool Item::AssignGobiDst(int di, std::string_view s) { return AssignGobi(di, s); }

bool Item::AssignGobi(int di, std::string_view value) {
    if (!detail->Yougo() || OwnedGroup(DirectGroup())) return false;
    if (di == -1 && DirectIndex() == -1) {
        for (int i = 0; i < DirectCount(); i++) {
            if (detail->enabled[i]) AssignDirectSlot(i, value);
        }
        return true;
    }
    if (di == -1) di = DirectIndex();
    AssignDirectSlot(di, value);
    return true;
}

// Copies wi's selected (or first) ending target into this item's ending candidates.
bool Item::AssignLLGobiDst(rules::Item* wi, int di) {
    const Item* o = AsItem(wi);
    if (!detail->Yougo() || o == nullptr || o->DirectCount() < 1) return false;
    int sel = o->DetailSelectedIndex();
    if (sel == -1) sel = 0;
    const std::string value(o->DirectText(sel));
    return AssignGobi(di, value);
}

bool Item::CheckGobiPumsa(std::string_view s, int di) {
    if (!detail->Yougo()) return false;
    const Group* g = detail->group;
    auto code = [&](int i) {
        const Entry* e = g != nullptr ? g->At(i) : nullptr;
        return e ? e->code : std::string_view();
    };
    if (di == -1 && detail->selected == -1) {
        for (int i = 0; i < DirectCount(); i++) {
            if (detail->enabled[i] && rules::MatchRuleCode(code(i), s)) return true;
        }
        return false;
    }
    if (di == -1) di = detail->selected;
    return rules::MatchRuleCode(code(di), s);
}

bool Item::CheckGobiAttr(std::string_view s, int di) {
    if (!detail->Yougo()) return false;
    if (di == -1 && DirectIndex() == -1) {
        for (int i = 0; i < DirectCount(); i++) {
            if (detail->enabled[i] && rules::GobiAttrValues(DirectAttr(i), s)) return true;
        }
        return false;
    }
    if (di == -1) di = DirectIndex();
    return rules::GobiAttrValues(DirectAttr(di), s);
}

// With no ending selected it tests the base group's codes, slot by slot over the
// enabled ending slots.
bool Item::MinGobiAttrOperation(int di, std::string_view s) {
    if (!detail->Yougo()) return false;
    if (di == -1 && DirectIndex() == -1) {
        for (int i = 0; i < DirectCount(); i++) {
            if (detail->enabled[i] && rules::MinGobiAttrValues(Attr(i), s)) return true;
        }
        return false;
    }
    if (di == -1) di = DirectIndex();
    return rules::MinGobiAttrValues(DirectAttr(di), s);
}

// With an explicit index it still reads the selected ending; with none selected it
// takes the pattern chunk at the slot number and an empty code string.
bool Item::CheckGobiGAttr(std::string_view s, int di) {
    if (!detail->Yougo()) return false;
    auto chunkMatches = [&](std::string_view c, std::string_view attrs) {
        if (c == "Ow") return IsRuleRightBoundary();
        if (c == "Hz") return StartsSentence();
        if (c == "Td" && next == nullptr && prev == nullptr) return true;
        return Has(attrs, c);
    };
    const int sel = DirectIndex();
    if (di == -1 && sel == -1) {
        for (int slot = 0; slot < DirectCount(); slot++) {
            if (!detail->enabled[slot]) continue;
            const std::size_t a = std::min<std::size_t>(static_cast<std::size_t>(slot), s.size());
            const std::size_t b = std::min<std::size_t>(static_cast<std::size_t>(slot) + 2, s.size());
            const std::string_view c = s.substr(a, b - a);
            for (std::size_t off = 0; off < s.size(); off += 2) {
                if (chunkMatches(c, "")) return true;
            }
        }
        return false;
    }
    for (std::size_t off = 0; off < s.size(); off += 2) {
        if (chunkMatches(Chunk(s, off, 2), DirectGAttr(sel))) return true;
    }
    return false;
}

// Some 2-byte grammar code of the candidate occurs in the pattern. With nothing
// selected, any candidate with a grammar code matches.
bool Item::MinGAttrOperation(int di, std::string_view s) {
    if (di == -1 && selected == -1) {
        for (int i = 0; i < Count(*this); i++) {
            if (!GAttr(i).empty()) return true;
        }
        return false;
    }
    if (di == -1) di = selected;
    return rules::MinGAttrValues(GAttr(di), s);
}

}  // namespace hdTrans::word
