// Text-path passes on the words next to an adjective: the copula of だろ and なる after an
// adjective, and the 것 before a みたい.
#include <string>
#include <string_view>

#include "core/output/compose.h"
#include "core/word/flush.h"
#include "core/word/strings.h"
#include "core/word/util.h"

namespace hdTrans::word {

void AdjectiveNeighbours(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->next;
        if (n == nullptr || it->selected < 0 || n->selected < 0) continue;
        const char cls = At(it->Code(it->selected), 0);
        if (cls == '7' && it->DirectSurface() == kJaI && n->group->surface == "だろ") {
            Entry* ending = it->DirectEntry(it->DirectIndex());
            const std::string_view entry = n->EntryText(n->selected);
            if (ending != nullptr && entry.starts_with("이겠") && ending->dst == "4") {
                it->AssignDirectSlot(it->DirectIndex(), "1");
                if (n->group->At(n->selected) != nullptr) n->AssignEntrySurface(n->selected, "겠" + std::string(entry.substr(std::string_view("이겠").size())));
            }
        } else if ((cls == '7' || cls == '8') && it->DirectSurface() == "く" && it->DirectText(it->DirectIndex()) == "6" &&
                   n->group->surface == kJaNa && At(n->Code(n->selected), 0) == '4' &&
                   output::CopulaTarget(it->EntryText(it->selected), f.families)) {
            SelectLemma(f, n, "되다");
        }
    }
}

void GeotBeforeMitai(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        if (p == nullptr || p->DirectIndex() == -1 || !it->EntryText(it->selected).starts_with("%") || !it->MitaiLike()) {
            continue;
        }
        const char cls = At(p->Code(p->selected), 0);
        if (cls != '4' && cls != '7') continue;
        Entry* ending = p->DirectEntry(p->DirectIndex());
        if (ending != nullptr && ending->dst.ends_with(kKoDeon)) {
            p->AssignDirectSlot(p->DirectIndex(), ending->dst + std::string(" ") + std::string(kKoGeot));
        }
    }
}

}  // namespace hdTrans::word
