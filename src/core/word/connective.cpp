// Text-path passes on the て form that joins two predicates: the sequence 고 of an action verb,
// and the て of an utterance-final verb read as a request.
#include <algorithm>
#include <string_view>

#include "core/dict/classes.h"
#include "core/output/families.h"
#include "core/word/flush.h"
#include "core/word/strings.h"
#include "core/word/util.h"

namespace hdTrans::word {
namespace {

// The item's text as typed, from its start over its length.
std::string_view ItemText(const Item* it) { return Slice(it->text, it->start, it->length); }

// Whether a verb follows it in its sentence (not an auxiliary of its て form), with only words
// and particles between.
bool PredicateFollows(const Item* it) {
    const Item* n = it->next;
    if (n == nullptr) return false;
    constexpr std::string_view kStops[] = {"ゆ", "こ", "ね"};
    const std::string_view next = n->group->surface;
    for (const std::string_view a : kStops) {
        if (next.starts_with(a)) return false;
    }
    for (const std::string_view a : dict::ClassMembers("aux.te-start")) {
        if (next.starts_with(a)) return false;
    }
    for (; n != nullptr; n = n->next) {
        if (n->detail->Yougo()) return n->detail->cls == kDetailVerb;
        const std::string_view s = n->group->surface;
        if (s.empty() || AnyRuneIn(s, "、。！？!?…‥「」『』（）()")) return false;
    }
    return false;
}

}  // namespace

bool UtteranceEnd(const Worker& w, const Item* it) {
    std::string_view rest = From(it->text, it->start + it->length);
    if (rest.empty() && it->tokenIndex + 1 < w.TokenCount()) {
        if (w.Span(it->tokenIndex + 1) != 0) return false;
        rest = w.Tok(it->tokenIndex + 1).text;
    }
    return TrimRunes(rest, "…‥」』）)。．、ーっッ～").empty();
}

void TeSequence(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string_view d = ItemText(it);
        if (e->dst != "6" || (!d.ends_with("て") && !d.ends_with("で")) || d.ends_with("ないで") ||
            !dict::InClass("ko-verb.sequence", output::Lemma(it->EntryText(it->selected), f.families)) ||
            !PredicateFollows(it)) {
            continue;
        }
        it->AssignDirectSlot(it->DirectIndex(), "1고");
    }
}

// Words of thanks, apology and feeling after which a て or 連用 form gives the reason (어서:
// 와 줘서 기뻐, 도와줘서 살았다). The て of 借りる (この場を借りて 이 자리를 빌려) keeps its form.

void TeThanks(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr || e->dst != "6" || it->group->surface.ends_with("借り")) continue;
        const Item* n = it->next;
        if (n != nullptr && n->group->surface == "、") n = n->next;
        if (n != nullptr && std::ranges::any_of(dict::ClassMembers("adverb.degree-feeling"), [&](std::string_view d) {
                return n->group->surface == d;
            })) {
            n = n->next;
        }
        if (n == nullptr) continue;
        const std::string_view next = n->group->surface;
        if (next.find("たら") != std::string_view::npos) continue;  // 行ってよかったら: permission
        // A word of feeling that describes a noun or a manner (幸せな未来, 幸せになる) is no
        // predicate the て form gives the reason for.
        if (const Item* a = n->next; a != nullptr && (a->Code(0).starts_with("1") || a->group->surface == "な" ||
                                                     a->group->surface == "に" || a->group->surface == "の")) {
            continue;
        }
        if (std::ranges::any_of(dict::ClassMembers("word.thanks"),
                                [&](std::string_view t) { return next.starts_with(t); })) {
            it->AssignDirectSlot(it->DirectIndex(), "6서");
        }
    }
}

void TeObjectComma(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb) continue;
        const std::string_view stem = it->group->surface;
        if (std::ranges::any_of(dict::ClassMembers("verb.means"),
                                [&](std::string_view v) { return stem.ends_with(v); })) {
            continue;
        }
        const Item* p = it->prev;
        const Item* n = it->next;
        if (p == nullptr || n == nullptr || p->group->surface != "を" || n->group->surface != "、") continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr || e->dst != "6") continue;
        it->AssignDirectSlot(it->DirectIndex(), "1고");
    }
}

void VerseTe(Flusher& f) {
    if (!f.w.settings.verse) return;
    for (const Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (AnyRuneIn(it->group->surface, "、。！？!?…‥「」『』（）()―")) return;
    }
    Item* it = f.w.chain.tail;
    if (it == nullptr || it->prev == nullptr || !it->detail->Yougo() || it->DirectIndex() < 0 ||
        it->detail->cls != kDetailVerb) {
        return;
    }
    std::string_view rest = From(it->text, it->start + it->length);
    if (rest.empty() && it->tokenIndex + 1 < f.w.TokenCount()) rest = f.w.Tok(it->tokenIndex + 1).text;
    if (!TrimSpace(rest).empty()) return;
    Entry* e = it->DirectEntry(it->DirectIndex());
    if (e == nullptr) return;
    const std::string_view d = ItemText(it);
    const std::string_view surface = it->group->surface;
    if ((e->dst != "6" && e->dst != "2며") || (!d.ends_with("て") && !d.ends_with("で")) || d.ends_with("ないで") ||
        ItemLemma(f, it, it->selected).starts_with("있") || surface.ends_with("しま") || surface.ends_with("ちゃ")) {
        return;
    }
    const Item* p = it->prev;
    const char cls = At(p->Code(p->selected), 0);
    const std::string_view pSurface = p->group->surface;
    const bool particleBefore =
        Has(" に と を ", " " + std::string(pSurface) + " ") && dict::InClass("stem.compound-particle", surface);
    if ((cls != '1' && cls != '2' && !(p->detail->Yougo() && (p->DirectSurface() == "もの" || p->DirectSurface() == "こと"))) ||
        particleBefore || (pSurface == "と" && surface == "し") || pSurface == "って" || f.w.Span(p->tokenIndex) != 0) {
        return;
    }
    it->AssignDirectSlot(it->DirectIndex(), "1고");
}

void TeRequest(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string_view surface = ItemText(it);
        if (output::Lemma(e->dst, f.families) != "2며" || (!surface.ends_with("て") && !surface.ends_with("で")) ||
            it->EntryText(it->selected).starts_with("있") || !UtteranceEnd(f.w, it)) {
            continue;
        }
        const std::string_view s = it->group->surface;
        it->AssignDirectSlot(it->DirectIndex(), s.ends_with("しま") || s.ends_with("ちゃ") ? "6서" : "6");
    }
}

}  // namespace hdTrans::word
