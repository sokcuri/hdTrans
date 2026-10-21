// Text-path passes on the て form that joins two predicates: the sequence 고 of an action verb,
// and the て of an utterance-final verb read as a request.
#include <algorithm>
#include <string_view>

#include "core/output/families.h"
#include "core/word/flush.h"
#include "core/word/strings.h"
#include "core/word/util.h"

namespace hdTrans::word {
namespace {

// Korean verbs of actions done one after the other.
constexpr std::string_view kSequenceVerbs[] = {
    "먹다", "마시다", "읽다", "씻다", "듣다", "닦다", "입다", "벗다", "신다", "끄다", "닫다", "끝내다", "마치다",
    "노래하다", "놀다", "자다", "쉬다", "샤워하다", "목욕하다", "세수하다", "양치질하다", "공부하다", "일하다",
    "청소하다", "요리하다", "식사하다", "춤추다", "숙제하다", "운동하다", "산책하다",
};

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
    for (const std::string_view a : kTeAuxiliaries) {
        if (next.starts_with(a)) return false;
    }
    for (; n != nullptr; n = n->next) {
        if (n->detail->Yougo()) return n->detail->cls == kDetailVerb;
        const std::string_view s = n->group->surface;
        if (s.empty() || AnyRuneIn(s, "、。！？!?…‥「」『』（）()")) return false;
    }
    return false;
}

// Verb stems of the compound particles in て after に, と or を (について 에 대해, によって).
constexpr std::string_view kParticleVerbs[] = {
    "つ", "対し", "よ", "因", "依", "と", "関し", "通し", "お", "於", "めぐ", "巡", "際し", "沿", "向け", "比べ", "あた",
    "当た", "わた", "渡", "かけ", "掛け", "従", "した", "伴", "とも", "応じ",
};

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
            !InSet(kSequenceVerbs, output::Lemma(it->EntryText(it->selected), f.families)) || !PredicateFollows(it)) {
            continue;
        }
        it->AssignDirectSlot(it->DirectIndex(), "1고");
    }
}

// Words of thanks and apology after which a て or 連用 form gives the reason (어서). The て of
// 借りる (この場を借りて 이 자리를 빌려) keeps its form.
constexpr std::string_view kThanks[] = {
    "ありがと", "有難", "有り難", "感謝", "すみません", "済みません", "すいません", "申し訳", "ごめん",
};

void TeThanks(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr || e->dst != "6" || it->group->surface.ends_with("借り")) continue;
        const Item* n = it->next;
        if (n != nullptr && n->group->surface == "、") n = n->next;
        if (n == nullptr) continue;
        const std::string_view next = n->group->surface;
        if (std::any_of(std::begin(kThanks), std::end(kThanks), [&](std::string_view t) { return next.starts_with(t); })) {
            it->AssignDirectSlot(it->DirectIndex(), "6서");
        }
    }
}

// Verbs whose て form with an object gives the means or manner (道具を使って 도구를 써서); they keep 어.
constexpr std::string_view kMeansVerbs[] = {
    "使", "用い", "利用し", "見習", "真似", "通じ", "介し", "頼", "生か", "活か", "込め", "合わせ", "伴", "装", "模し",
};

void TeObjectComma(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb) continue;
        const std::string_view stem = it->group->surface;
        if (std::any_of(std::begin(kMeansVerbs), std::end(kMeansVerbs), [&](std::string_view v) { return stem.ends_with(v); })) {
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
    const bool particleBefore = Has(" に と を ", " " + std::string(pSurface) + " ") && InSet(kParticleVerbs, surface);
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
