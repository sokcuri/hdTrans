#include "core/word/journal.h"

namespace hdTrans::word {

void Journal::Begin() {
    if (recording_) return;
    writes_.clear();
    recording_ = true;
}

void Journal::Commit() {
    writes_.clear();
    recording_ = false;
}

void Journal::Rollback() {
    for (auto it = writes_.rbegin(); it != writes_.rend(); ++it) {
        if (it->item != nullptr) {
            if (it->direct) it->item->directChoice = it->oldChoice;
            else it->item->choice = it->oldChoice;
        }
        if (it->entry != nullptr) *it->entry = std::move(it->old);
    }
    writes_.clear();
    recording_ = false;
}

void Journal::BeforeEntryWrite(Entry* e) {
    if (!recording_ || e == nullptr) return;
    Write w;
    w.entry = e;
    w.old = *e;
    writes_.push_back(std::move(w));
}

void Journal::BeforeChoiceWrite(Item* it, bool direct) {
    if (!recording_ || it == nullptr) return;
    Write w;
    w.item = it;
    w.direct = direct;
    w.oldChoice = direct ? it->directChoice : it->choice;
    writes_.push_back(std::move(w));
}

}  // namespace hdTrans::word
