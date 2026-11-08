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

}  // namespace

bool PredicateFollows(const Item* it) {
    const Item* n = it->After();
    if (n == nullptr) return false;
    constexpr std::string_view kStops[] = {"ゆ", "こ", "ね"};
    const std::string_view next = n->group->surface;
    for (const std::string_view a : kStops) {
        if (next.starts_with(a)) return false;
    }
    if (dict::StartsWithMember(HDTRANS_CLASS("aux.te-start"), next) > 0) return false;
    for (; n != nullptr; n = n->next) {
        if (n->detail->Yougo()) return n->detail->cls == kDetailVerb;
        const std::string_view s = n->group->surface;
        if (s.empty() || AnyRuneIn(s, "、。！？!?…‥「」『』（）()")) return false;
    }
    return false;
}

namespace {

constexpr std::string_view kUtteranceTail = "…‥」』）)≫》】〉＞。．、ーっッ～♪☆★♡♥";

std::size_t ScanMarks(std::string_view text, std::size_t at, std::string_view set) {
    while (at < text.size()) {
        const text::Rune r = text::DecodeRune(text.substr(at));
        if (!RuneIn(r.cp, set)) return at;
        at += std::max<std::size_t>(r.size, 1);
    }
    return text.size();
}

}  // namespace

std::size_t MarksEnd(const Worker& w, std::string_view text, std::size_t at, std::string_view set) {
    if (at >= text.size()) return text.size();
    Worker::MarkRunMemo& memo = w.markRuns;
    // A short rest is scanned: it costs less than the memo, and keeps the long texts in it.
    constexpr std::size_t kShort = 256;
    if (!memo.on || text.size() - at < kShort) return ScanMarks(text, at, set);
    const char* end = text.data() + text.size();
    Worker::MarkRuns* found = nullptr;
    for (Worker::MarkRuns& m : memo.runs) {
        if (m.end == end && m.set == set.data() && m.size >= text.size()) found = &m;
    }
    if (found == nullptr) {
        found = &memo.runs[memo.next++ % std::size(memo.runs)];
        found->end = end;
        found->size = text.size();
        found->set = set.data();
        // stops[d] for each rune that starts d bytes before the end: how far before the end the run
        // of marks of set from it stops (0: it reaches the end). An offset inside a rune keeps the scan.
        found->stops.assign(text.size() + 1, UINT32_MAX);
        std::vector<std::size_t> starts;
        for (std::size_t k = 0; k < text.size();) {
            starts.push_back(k);
            k += std::max<std::size_t>(text::DecodeRune(text.substr(k)).size, 1);
        }
        uint32_t stop = 0;
        found->stops[0] = 0;
        for (auto k = starts.rbegin(); k != starts.rend(); ++k) {
            const auto d = static_cast<uint32_t>(text.size() - *k);
            if (!RuneIn(text::DecodeRune(text.substr(*k)).cp, set)) stop = d;
            found->stops[d] = stop;
        }
    }
    const uint32_t stop = found->stops[text.size() - at];
    return stop != UINT32_MAX ? text.size() - stop : ScanMarks(text, at, set);
}

bool UtteranceEnd(const Worker& w, const Item* it) {
    std::string_view rest = it->text;
    std::size_t at = static_cast<std::size_t>(it->start + it->length);
    if (at >= rest.size() && it->tokenIndex + 1 < w.TokenCount()) {
        if (w.Span(it->tokenIndex + 1) != 0) return false;
        rest = w.Tok(it->tokenIndex + 1).text;
        at = 0;
    }
    return MarksEnd(w, rest, at, kUtteranceTail) == rest.size();
}

// Words of thanks, apology and feeling after which a て or 連用 form gives the reason (어서:
// 와 줘서 기뻐, 도와줘서 살았다). The て of 借りる (この場を借りて 이 자리를 빌려) keeps its form.

void TeThanks(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr || e->dst != "6" || it->group->surface.ends_with("借り")) continue;
        const Item* n = it->After();
        if (n != nullptr && n->group->surface == "、") n = n->next;
        if (n != nullptr && dict::InClass(HDTRANS_CLASS("adverb.degree-feeling"), n->group->surface)) {
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
        if (dict::StartsWithMember(HDTRANS_CLASS("word.thanks"), next) > 0) {
            it->AssignDirectSlot(it->DirectIndex(), "6서");
        }
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
        Has(" に と を ", " " + std::string(pSurface) + " ") && dict::InClass(HDTRANS_CLASS("stem.compound-particle"), surface);
    if ((cls != '1' && cls != '2' && !(p->detail->Yougo() && (p->DirectSurface() == "もの" || p->DirectSurface() == "こと"))) ||
        particleBefore || (pSurface == "と" && surface == "し") || pSurface == "って" || f.w.Span(p->tokenIndex) != 0) {
        return;
    }
    it->AssignDirectSlot(it->DirectIndex(), "1고");
}

}  // namespace hdTrans::word
