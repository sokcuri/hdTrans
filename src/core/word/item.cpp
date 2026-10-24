#include <cstdint>
#include "core/word/item.h"

#include "core/text/utf8.h"
#include "core/tokenizer/token.h"
#include "core/word/strings.h"
#include "core/word/util.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

// The digit after the first 'J' of attrs, or 0 when there is none or it is not 0..7.
char PunctDigit(std::string_view attrs) {
    const std::size_t j = attrs.find('J');
    if (j == std::string_view::npos) return 0;
    const char d = At(attrs, static_cast<long>(j + 1));
    return d >= '0' && d <= '7' ? d : 0;
}

}  // namespace

int Detail::ClassifyTrailingPunctuation(std::string_view text) const {
    if (text.empty()) {
        switch (nextType) {
        case 3:
        case 4:
        case 17: {
            const std::string_view first = FirstChar(nextText);
            if (first == "、" || first == "，") return 1;
            if (first == "。" || first == "．") return 2;
            if (first == "？") return 3;
            if (first == "！") return 4;
            switch (At(nextText, 0)) {
            case ',': return 1;
            case '"': case '>': case ']': case '}': return 6;
            case '?': return 3;
            case '!': return 4;
            case ')': return 5;
            case ':': case '<': return 7;
            case '.': return 2;
            }
            return 0;
        }
        }
        return nonterminalBoundary ? 0 : 2;
    }
    const char32_t r = text::DecodeRune(text).cp;
    if (r == U'〕' || r == U'〉' || r == U'》' || r == U'」' || r == U'』' || r == U'】') return 6;
    switch (text[0]) {
    case ',': return 1;
    case '"': case '>': case ']': case '}': return 6;
    case '?': return 3;
    case '!': return 4;
    case ')': return 5;
    case ':': case '<': return 7;
    case '.': return 2;
    }
    return 0;
}

std::string_view Item::EntryText(int i) const {
    const Entry* e = group->At(i);
    return e ? std::string_view(e->dst) : std::string_view();
}

std::string_view Item::Code(int i) const {
    const Entry* e = group->At(i);
    return e ? e->code : std::string_view();
}

std::string_view Item::Attr(int i) const {
    const Entry* e = group->At(i);
    return e ? e->attr : std::string_view();
}

std::string_view Item::GAttr(int i) const {
    const Entry* e = group->At(i);
    return e ? std::string_view(e->gattr) : std::string_view();
}

int Item::RuleID(int i) const {
    const Entry* e = group->At(i);
    return e ? e->ruleId : -1;
}

namespace {

Selection CandidateChoice(const Entry* e, Selection source) {
    if (e != nullptr && e->targetChoice.layer != Provenance::None &&
        (source.layer == Provenance::Single || source.layer == Provenance::FirstXxx || source.layer == Provenance::Digit)) {
        return e->targetChoice;
    }
    return source;
}

}  // namespace

void Item::SelectCandidate(int i, Selection source) {
    selected = i;
    choice = CandidateChoice(group->At(i), source);
}

void Item::SetDirectIndex(int i, Selection source) {
    if (!DirectGroup()) return;
    detail->selected = i;
    directChoice = CandidateChoice(DirectEntry(i), source);
}

void Item::SetRuleContext(bool global, int number) {
    if (!w) return;
    w->ruleChoice = number < 0 ? Selection{} : Selection{global ? Provenance::GlobalRule : Provenance::LocalRule, number};
    w->ruleItem = number < 0 ? nullptr : this;
}

void Item::BeginRule() {
    if (w) w->journal.Begin();
}

void Item::EndRule(bool fired) {
    if (!w) return;
    if (fired) w->journal.Commit();
    else w->journal.Rollback();
}

void Item::AssignEntrySurface(int i, std::string_view s, Selection source) {
    if (Entry* e = group->At(i)) {
        // A rule's write is recorded so that a rule that fails can take it back.
        if (w) w->journal.BeforeEntryWrite(e);
        if (e->dst != s) {
            if (w && w->ruleChoice.layer != Provenance::None) {
                source = w->ruleChoice;
                e->neighbourAssigned = w->ruleItem != this;
            }
            e->targetChoice = source;
            if (i == selected) {
                if (w) w->journal.BeforeChoiceWrite(this, false);
                choice = source;
            }
        }
        e->dst.assign(s);
    }
}

std::string_view Item::DirectText(int i) const {
    const Entry* e = DirectEntry(i);
    return e ? std::string_view(e->dst) : std::string_view();
}

std::string_view Item::DirectCode(int i) const {
    const Entry* e = DirectEntry(i);
    return e ? e->code : std::string_view();
}

std::string_view Item::DirectAttr(int i) const {
    const Entry* e = DirectEntry(i);
    return e ? e->attr : std::string_view();
}

std::string_view Item::DirectGAttr(int i) const {
    const Entry* e = DirectEntry(i);
    return e ? std::string_view(e->gattr) : std::string_view();
}

int Item::DirectRuleID(int i) const {
    const Entry* e = DirectEntry(i);
    return e ? e->ruleId : -1;
}

int Item::DirectClassifier(int i) const {
    if (!DirectGroup() || i < 0 || static_cast<std::size_t>(i) >= detail->classes.size()) return 0;
    return detail->classes[i];
}

void Item::AssignDirectSlot(int i, std::string_view s, Selection source) {
    if (Entry* e = DirectEntry(i)) {
        if (w) w->journal.BeforeEntryWrite(e);
        if (e->dst != s) {
            if (w && w->ruleChoice.layer != Provenance::None) source = w->ruleChoice;
            e->targetChoice = source;
            if (i == DirectIndex()) {
                if (w) w->journal.BeforeChoiceWrite(this, true);
                directChoice = source;
            }
        }
        e->dst.assign(s);
    }
}

void Item::AppendRuleCandidateMarker(std::string_view marker) {
    if (selected != -1) {
        if (Entry* e = group->At(selected)) e->gattr += marker;
        return;
    }
    for (Entry* e : group->entries) e->gattr += marker;
}

bool Item::Fixed() const { return !group->entries.empty() && group->entries[0]->fixed; }

bool Item::IsNameItem() const {
    if (group->entries.empty()) return false;
    if (const dict::UserEntry* u = group->entries[0]->user) return u->name;
    return w->tokens[static_cast<std::size_t>(tokenIndex)].type == tokenizer::kTypeProtected;
}

int Item::CrossEnd() const {
    if (w->Span(tokenIndex) != 0) return 1;
    return start + length;
}

int Item::CheckJ(int i) const {
    const char digit = PunctDigit(GAttr(i));
    if (digit == 0) return -1;
    if (digit == detail->ClassifyTrailingPunctuation(From(text, length + start)) + '0') return 1;
    return -1;
}

int Item::CheckDirectJ(int i) const {
    const char digit = PunctDigit(DirectGAttr(i));
    if (digit == 0) return -1;
    if (w->settings.tokenizerOption != 0 && From(text, start + length).empty()) return digit == '2' ? 1 : 0;
    return digit == DirectClassifier(i) + '0' ? 1 : 0;
}

Item* Item::After() const {
    Item* to = next;
    if (w == nullptr || w->RubyPart(tokenIndex) != 1) return to;
    while (to != nullptr && w->RubyPart(to->tokenIndex) == 2) to = to->next;
    return to;
}

Item* Item::Object(int delta) {
    Item* item = this;
    // The words a ruby base reads after it are those after its reading, so that the base
    // and the reading each read the sentence; the words around them read them as written.
    const bool base = w->RubyPart(tokenIndex) == 1;
    if (delta >= 0) {
        for (int i = 0; i < delta; i++) {
            if (item == nullptr) return nullptr;
            item = base ? item->After() : item->next;
            if (item != nullptr) {
                if (item->EntryText(0) == "(") {
                    while (item != nullptr && item->EntryText(0) != ")") item = item->next;
                    if (item != nullptr) item = item->next;
                }
                if (item != nullptr && item->EntryText(0) == ")") item = item->next;
                // A small っ closing the word before (行くっ) is part of its sound.
                while (item != nullptr && item->ClosingTsu()) item = item->next;
            }
        }
    } else {
        for (int i = 0; i < -delta; i++) {
            if (item == nullptr) return nullptr;
            item = item->prev;
            if (item != nullptr) {
                if (item->EntryText(0) == ")") {
                    while (item != nullptr && item->EntryText(0) != "(") item = item->prev;
                    if (item != nullptr) item = item->prev;
                } else if (item->EntryText(0) == kKoCloseQuote) {
                    item = item->prev;
                }
            }
        }
    }
    if (item != nullptr && !w->settings.improvements.preserveNeighbourSlots) {
        if (delta != 0) {
            item->compDI = -1;
            item->compUmiDI = -1;
        } else {
            item->compDI = compDI;
            item->compUmiDI = compUmiDI;
        }
    }
    return item;
}

rules::Item* Item::GetObject(int delta) { return Object(static_cast<int16_t>(delta)); }

void Chain::Unlink(Item* it) {
    if (it->prev != nullptr) it->prev->next = it->next;
    else head = it->next;
    if (it->next != nullptr) it->next->prev = it->prev;
    else tail = it->prev;
}

int Chain::Insert(Item* item, bool overlapping) {
    if (item == nullptr) return 0;
    item->prev = nullptr;
    item->next = nullptr;
    int removed = 0;
    for (Item* scan = head; scan != nullptr;) {
        Item* next = scan->next;
        bool hit;
        if (overlapping) {
            const int end = scan->start + scan->length;
            hit = scan->tokenIndex == item->tokenIndex && item->start < end && end <= item->start + item->length;
        } else {
            hit = scan->tokenIndex == item->tokenIndex && scan->start == item->start;
        }
        if (hit) {
            removed -= scan->length;
            Unlink(scan);
        }
        scan = next;
    }
    removed += item->length;
    if (head == nullptr) {
        head = tail = item;
        return removed;
    }
    const int end = item->start + item->length;
    Item* scan = head;
    for (; scan != nullptr; scan = scan->next) {
        if (scan->tokenIndex > item->tokenIndex) {
            if (Item* p = scan->prev; p == nullptr || p->CrossEnd() < end) break;
        } else if (scan->tokenIndex == item->tokenIndex && scan->start + scan->length >= end) {
            break;
        }
    }
    if (scan == nullptr) {
        tail->next = item;
        item->prev = tail;
        tail = item;
        return removed;
    }
    item->next = scan;
    item->prev = scan->prev;
    if (scan->prev != nullptr) scan->prev->next = item;
    else head = item;
    scan->prev = item;
    return removed;
}

}  // namespace hdTrans::word
