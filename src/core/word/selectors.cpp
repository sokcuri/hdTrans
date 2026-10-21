// Candidate and ending selection: each item picks the entry of its group and the entry
// of its ending group, by the local rule of an entry, the global rules, then defaults.
#include <cstdint>
#include <string>
#include <string_view>

#include "core/word/conjugation.h"
#include "core/word/flush.h"
#include "core/word/strings.h"
#include "core/word/util.h"

namespace hdTrans::word {
namespace {

// The polite register of Settings::registers (userdict::Register::Polite; 2 is casual).
constexpr uint8_t kPoliteRegister = 3;

// The byte at i as an unsigned value; 0 outside s.
int ByteAt(std::string_view s, long i) { return static_cast<unsigned char>(At(s, i)); }

bool RunLocalRule(Flusher& f, Item* it, int rule) { return rules::RunLocal(f.program, it, rule, f.w.settings.ruleTally); }

bool RunGlobalRule(Flusher& f, Item* it, bool uganRule, unsigned char pumsa, bool last, int* fired) {
    return rules::RunGlobal(f.program, it, uganRule, pumsa, last, fired, f.w.settings.ruleTally);
}

// The entry of the first "XXX" attribute.
void SelectLastXXX(Item* it) {
    for (int i = 0; i < static_cast<int>(it->group->entries.size()); i++) {
        if (Has(it->Attr(i), "XXX")) {
            it->SelectCandidate(i, {Provenance::FirstXxx});
            return;
        }
    }
}

// The first enabled ending with an "XXX" attribute.
void SelectDirectLastXXX(Item* it) {
    if (!it->detail->Yougo()) return;
    for (int i = 0; i < it->DirectCount(); i++) {
        if (it->detail->enabled[i] && Has(it->DirectAttr(i), "XXX")) {
            it->SetDirectIndex(i, {Provenance::FirstXxx});
            return;
        }
    }
}

// The ending of a plain-dictionary ending group, the first enabled one that is not a
// stative row when the predicate is not stative, with its politeness slot filled.
void SelectOwnedDirect(Flusher& f, Item* it) {
    Group* g = it->DirectGroup();
    const bool stative = IsStative(f, it);
    for (int slot = 0; slot < static_cast<int>(g->entries.size()); slot++) {
        Entry* e = g->entries[static_cast<std::size_t>(slot)];
        if (!it->detail->enabled[slot] || (e->user != nullptr && e->user->stative && !stative)) continue;
        it->SetDirectIndex(slot, {Provenance::Fixed});
        const bool polite = e->user != nullptr &&
                            it->w->Register(*e->user) == kPoliteRegister;
        const std::string yo = polite ? std::string(kKoYo) : std::string();
        std::string target(e->dst);
        const std::string placeholder = "(" + std::string(kKoYo) + ")";
        for (std::size_t at = target.find(placeholder); at != std::string::npos; at = target.find(placeholder, at + yo.size())) {
            target.replace(at, placeholder.size(), yo);
        }
        e->dst = target;
        return;
    }
}

// selectDigit selects the candidate of an item by its J attribute against the
// following punctuation (preferring "XXX" candidates), or for the other classes the
// "XXX" candidate.
void SelectDigit(Item* it) {
    const int count = static_cast<int>(it->group->entries.size());
    switch (At(it->Code(0), 0)) {
        case '0': case '1': case '2': case '3': case '6': case '9': {
            if (count <= 0) {
                it->SelectCandidate(0, {Provenance::Digit});
                return;
            }
            int positive = 0, negative = 0, firstPositive = 0, firstNegative = 0;
            for (int slot = 0; slot < count; slot++) {
                switch (it->CheckJ(slot)) {
                    case -1:
                        if (negative == 0) firstNegative = slot;
                        negative++;
                        break;
                    case 1:
                        if (positive == 0) firstPositive = slot;
                        positive++;
                        break;
                    default:
                        break;
                }
            }
            auto withXXX = [&](int want) {
                for (int slot = 0; slot < count; slot++) {
                    if (it->CheckJ(slot) == want && Has(it->Attr(slot), "XXX")) {
                        it->SelectCandidate(slot, {Provenance::Digit});
                        return true;
                    }
                }
                return false;
            };
            if (positive > 1) {
                if (!withXXX(1)) it->SelectCandidate(firstPositive, {Provenance::Digit});
            } else if (positive == 1) {
                it->SelectCandidate(firstPositive, {Provenance::Digit});
            } else if (negative > 1) {
                if (!withXXX(-1)) it->SelectCandidate(firstNegative, {Provenance::Digit});
            } else if (negative == 1) {
                it->SelectCandidate(firstNegative, {Provenance::Digit});
            } else {
                it->SelectCandidate(0, {Provenance::Digit});
            }
            return;
        }
        case '4': case '7': case '8': {
            int chosen = 0;
            for (int slot = 0; slot < count; slot++) {
                if (Has(it->Attr(slot), "XXX")) {
                    chosen = slot;
                    break;
                }
            }
            it->SelectCandidate(chosen, {Provenance::Digit});
            return;
        }
        default:
            return;
    }
}

// selectDirectByJ selects an ending of the conjugation branch of the item by its J
// attribute, preferring "XXX" endings; else the first possible one.
void SelectDirectByJ(Item* it) {
    int flags[kDirectSlots];
    for (int& flag : flags) flag = 1;
    const int count = it->w->settings.improvements.expandedGroups
        ? it->DirectCount() : static_cast<int>(static_cast<int8_t>(it->DirectCount()));
    int matches = 0;
    int want = it->detail->kind;
    if (At(it->Code(0), 0) == '4') want += '0';
    for (int i = 0; i < count; i++) {
        if (ByteAt(it->DirectCode(i), 1) != want) {
            flags[i] = 0;
            continue;
        }
        const int check = it->CheckDirectJ(i);
        if (check != 0) {
            flags[i] = check & 0xFF;
            matches++;
        }
    }
    if (matches > 0) {
        int first = 0;
        while (first < count && flags[first] != 1) first++;
        if (first < count) {
            int chosen = first;
            for (int i = 0; i < count; i++) {
                if (flags[i] == 1 && Has(it->DirectAttr(i), "XXX")) {
                    chosen = i;
                    break;
                }
            }
            it->SetDirectIndex(chosen, {Provenance::Digit});
            return;
        }
        int chosen = -1;
        for (int i = 0; i < count; i++) {
            if (flags[i] == 0) continue;
            if (chosen == -1) chosen = i;
            if (Has(it->DirectAttr(i), "XXX")) {
                it->SetDirectIndex(i, {Provenance::Digit});
                return;
            }
        }
        it->SetDirectIndex(chosen, {Provenance::Digit});
        return;
    }
    for (int i = 0; i < count; i++) {
        if (flags[i] != 0) {
            it->SetDirectIndex(i, {Provenance::Digit});
            return;
        }
    }
    it->SetDirectIndex(0, {Provenance::Digit});
}

// selectDirect selects the ending of a conjugating item.
void SelectDirect(Flusher& f, Item* it) {
    if (OwnedGroup(it->DirectGroup())) {
        SelectOwnedDirect(f, it);
        return;
    }
    const int n = it->DirectCount();
    auto selectBy = [&](int slot, Selection& source, auto fire) {
        if (!it->detail->enabled[slot]) return false;
        it->compUmiDI = slot;
        if (!fire()) return false;
        it->SetDirectIndex(slot, source);
        if (Has(it->DirectGAttr(slot), "Kj")) ApplyKj(it);
        return true;
    };
    bool chosen = false;
    for (int slot = 0; slot < n && !chosen; slot++) {
        Selection source{Provenance::LocalRule, it->DirectRuleID(slot)};
        chosen = selectBy(slot, source, [&] {
            return source.id != -1 && RunLocalRule(f, it, source.id);
        });
    }
    for (int slot = 0; slot < n && !chosen; slot++) {
        Selection source{Provenance::GlobalRule};
        chosen = selectBy(slot, source, [&] {
            return RunGlobalRule(f, it, false, static_cast<unsigned char>(At(it->Code(0), 0)), false, &source.id);
        });
    }
    if (!chosen) {
        if (n == 1) it->SetDirectIndex(0, {Provenance::Single});
        else SelectDirectLastXXX(it);
        if (Has(it->DirectGAttr(it->DirectIndex()), "Kj")) ApplyKj(it);
    }
    if (it->DetailSelectedIndex() == -1) {
        SelectDirectByJ(it);
        if (Has(it->DirectGAttr(it->DirectIndex()), "Kj")) ApplyKj(it);
    }
}

}  // namespace

bool IsStative(Flusher& f, Item* it) {
    if (it->group->surface == kJaTanomuKanji) return true;
    const Entry* e = it->group->At(it->selected);
    const std::string_view t = e ? std::string_view(e->dst) : std::string_view();
    if (t.size() < 4 || t.back() != '@') return false;
    const std::string_view family = t.substr(t.size() - 4);
    auto lookup = [&](std::string_view key) {
        return family[0] == '$' ? LookupDollar(key, f.families) : LookupSource(key, f.families);
    };
    const std::string terminal = std::string(family) + "4";
    const std::string first = std::string(family) + "1";
    const std::string_view terminalRow = lookup(terminal);
    return !terminalRow.empty() && terminalRow == lookup(first);
}

void ApplyKj(Item* it) {
    for (Item* p = it->prev; p != nullptr; p = p->prev) {
        if (p->group->surface != PrefixGate(14) && p->group->surface != kJaKara) continue;
        if (At(p->Code(p->selected), 0) != '2') continue;
        Item* pp = p->prev;
        Item* pn = p->next;
        if (pp != nullptr && At(pp->Code(pp->selected), 0) == '1' && pp->MinAttrOperation(pp->selected, "VHnVGt") &&
            pn != nullptr && At(pn->Code(pn->selected), 0) == '4' && Has(pn->GAttr(pn->selected), "Ke")) {
            p->AssignEntrySurface(p->selected, kKoIGa);
            return;
        }
    }
}

void CleanupSelectors(Flusher& f) {
    Chain& chain = f.w.chain;
    for (Item* it = chain.head; it != nullptr; it = it->next) {
        // An item whose only candidate has no part code is dropped.
        if (it->group->entries.size() == 1 && it->Code(0).empty()) chain.Unlink(it);
    }
    for (Item* it = chain.head; it != nullptr; it = it->next) {
        if (it->Fixed()) {
            // The ending of a fixed stem is still selected by its rules.
            it->SelectCandidate(0, {Provenance::Fixed});
            it->compDI = 0;
            if (it->detail->Yougo()) SelectDirect(f, it);
            it->compUmiDI = it->DetailSelectedIndex();
            continue;
        }
        const int count = static_cast<int>(it->group->entries.size());
        auto selectBy = [&](int slot, Selection& source, auto fire) {
            it->compDI = slot;
            it->compUmiDI = -1;
            if (f.w.settings.text && it->EntryText(slot).starts_with(kKoBogo) && it->MitaiLike()) return false;
            if (!fire()) return false;
            it->SelectCandidate(slot, source);
            if (Has(it->GAttr(slot), "Kj")) ApplyKj(it);
            return true;
        };
        bool chosen = false;
        for (int slot = 0; slot < count && !chosen; slot++) {
            Selection source{Provenance::LocalRule, it->RuleID(slot)};
            chosen = selectBy(slot, source, [&] {
                return source.id != -1 && RunLocalRule(f, it, source.id);
            });
        }
        for (int slot = 0; slot < count && !chosen; slot++) {
            Selection source{Provenance::GlobalRule};
            chosen = selectBy(slot, source, [&] {
                return RunGlobalRule(f, it, true, static_cast<unsigned char>(At(it->Code(slot), 0)), false, &source.id);
            });
        }
        if (!chosen) {
            if (count == 1) it->SelectCandidate(0, {Provenance::Single});
            else SelectLastXXX(it);
            if (Has(it->GAttr(it->selected), "Kj")) ApplyKj(it);
        }
        if (it->selected == -1) {
            SelectDigit(it);
            if (Has(it->GAttr(it->selected), "Kj")) ApplyKj(it);
        }
        it->compDI = it->selected;
        if (it->detail->Yougo()) SelectDirect(f, it);
        it->compUmiDI = it->DetailSelectedIndex();

        const std::string_view attrs = it->GAttr(it->selected);
        std::string_view previousAttrs;
        int previousKind = ' ';
        if (const Item* p = it->prev; p != nullptr) {
            previousAttrs = p->GAttr(p->selected);
            previousKind = ByteAt(p->Code(p->selected), 0);
        }
        if (Has(attrs, "Sk") && !Has(attrs, "Tu") && previousKind == '1' && Has(previousAttrs, "Hd")) {
            const std::string marker = std::string(attrs) + "Tu";
            it->AppendRuleCandidateMarker(marker);
        }
        for (int slot = 0; slot < count; slot++) {
            Selection source{Provenance::GlobalRule};
            const bool fired = selectBy(slot, source, [&] {
                return RunGlobalRule(f, it, true, static_cast<unsigned char>(At(it->Code(slot), 0)), true, &source.id);
            });
            if (fired) break;
        }
    }
}

}  // namespace hdTrans::word
