#pragma once
// The writes a rule makes while it runs, so that a rule whose conditions fail leaves
// the words exactly as they were.
//
// An assignment takes effect as its atom is evaluated; the atoms after it still run,
// so a conjunction can fail after writing. Each rule runs between Begin and either
// Commit or Rollback. Rollback restores the entries and the choices it changed, newest
// first, so the rule reads as a function of the state before it: it either changes the
// state by all of its writes or by none. A rule that fires keeps them, and the same
// record tells which field each write touched.
#include <cstddef>
#include <vector>

#include "core/word/item.h"

namespace hdTrans::word {

class Journal {
public:
    // Starts recording. A rule runs to its end before the next one starts, so one
    // journal is enough per worker; a Begin inside a recording rule is a bug and is
    // ignored, keeping the outer recording.
    void Begin();
    // Keeps the writes since Begin.
    void Commit();
    // Undoes the writes since Begin, newest first.
    void Rollback();

    bool Recording() const { return recording_; }
    std::size_t Size() const { return writes_.size(); }

    // Saves what a write is about to change. Both do nothing outside a rule.
    void BeforeEntryWrite(Entry* e);
    void BeforeChoiceWrite(Item* it, bool direct);

private:
    struct Write {
        Entry* entry = nullptr;
        Entry old;
        Item* item = nullptr;  // the item whose choice changed, null for an entry only
        bool direct = false;   // the item's ending choice, not its word choice
        Selection oldChoice;
    };
    std::vector<Write> writes_;
    bool recording_ = false;
};

}  // namespace hdTrans::word
