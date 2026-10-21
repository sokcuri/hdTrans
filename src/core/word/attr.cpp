// The rules::Item methods of Item. di is a candidate index; -1 means the selected
// candidate, or every candidate when none is selected. The gobi (ending) variants work
// on the direct group and are false for Info details.
#include <algorithm>

#include "core/rules/match.h"
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

bool Item::JoinsNext() const {
    return next != nullptr && next->tokenIndex == tokenIndex && next->start == start + length;
}

bool Item::CheckGAttr(std::string_view s, int di) {
    auto chunkMatches = [&](std::string_view c, std::string_view attrs, bool hzOnly) {
        if (c == "Ow") return IsRuleRightBoundary();
        // Oj: the On reading of a word written right before the next one (事私, not 事…私).
        if (c == "Oj") return Has(attrs, "On") && JoinsNext();
        if (c == "Hz" && hzOnly) return StartsSentence();
        if (c == "Hz" && StartsSentence()) return true;
        if (c == "Td" && next == nullptr && prev == nullptr) return true;
        return Has(attrs, c);
    };
    if (di == -1 && selected == -1) {
        for (int i = 0; i < Count(*this); i++) {
            for (std::size_t off = 0; off < s.size(); off += 2) {
                if (chunkMatches(Chunk(s, off, 2), GAttr(i), false)) return true;
            }
        }
        return false;
    }
    const bool hzOnly = di == -1;
    if (di == -1) di = selected;
    for (std::size_t off = 0; off < s.size(); off += 2) {
        if (chunkMatches(Chunk(s, off, 2), GAttr(di), hzOnly)) return true;
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
    if (n == nullptr) return true;
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
