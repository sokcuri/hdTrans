#include <string>
#include <vector>

#include "core/engine/glue.h"
#include "core/output/item.h"
#include "core/output/text.h"
#include "core/word/item.h"

namespace hdTrans::internal {

namespace {

// Entry i of the item's group; empty outside the group.
output::ResolvedEntry EntryOf(const word::Item& it, int i) {
    output::ResolvedEntry e;
    if (it.group == nullptr || it.group->At(i) == nullptr) return e;
    e.code = std::string(it.Code(i));
    e.gattr = std::string(it.GAttr(i));
    e.text = std::string(it.EntryText(i));
    return e;
}

// Direct (conjugation ending) entry i of the item; empty outside the group.
output::ResolvedEntry DirectOf(const word::Item& it, int i) {
    output::ResolvedEntry e;
    if (it.DirectEntry(i) == nullptr) return e;
    e.code = std::string(it.DirectCode(i));
    e.gattr = std::string(it.DirectGAttr(i));
    e.text = std::string(it.DirectText(i));
    return e;
}

output::ResolvedItem ItemOf(const word::Item& it) {
    output::ResolvedItem r;
    r.tokenIndex = it.tokenIndex;
    r.offset = it.start;
    r.length = it.length;
    if (it.group != nullptr) r.surface = it.group->surface;
    r.selected = it.selected;
    r.selectionFlag = it.selectionFlag;
    r.detailClass = static_cast<output::DetailClass>(it.detail->cls);
    r.detailKind = it.DetailKind();
    r.detailRecord = it.DetailRecord();
    r.causativePotential = r.detailRecord == 5 && it.detail->syntheticCausative &&
                           it.detail->voice == word::Voice::Potential;
    r.directIndex = it.DirectIndex();
    r.entry = EntryOf(it, it.selected);
    if (const word::Entry* e = it.group == nullptr ? nullptr : it.group->At(it.selected)) {
        r.reread = e->targetChoice.layer == word::Provenance::Rewrite;
    }
    const int entries = it.group == nullptr ? 0 : static_cast<int>(it.group->entries.size());
    if (entries > 0) {
        r.first = EntryOf(it, 0);
        r.fixed = it.group->At(0)->fixed;
    }
    if (r.directIndex >= 0 && r.directIndex < it.DirectCount()) r.ending = DirectOf(it, r.directIndex);
    r.endingSurface = std::string(it.DirectSurface());
    r.endingLength = it.DirectLength();
    r.appended = it.appended;
    r.ownedEnding = it.DirectCount() > 0 && it.DirectEntry(0)->user != nullptr;
    for (int i = 0; it.Code(i) != "" || it.EntryText(i) != ""; i++) {
        if (i == 0 || i == it.selected) continue;
        if (output::internal::HasSyllable(it.EntryText(i))) r.otherHangul = true;
    }
    return r;
}

}  // namespace

void ResolvedItems(const word::Chain& chain, std::vector<output::ResolvedItem>& out) {
    out.clear();
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) out.push_back(ItemOf(*it));
}

}  // namespace hdTrans::internal
