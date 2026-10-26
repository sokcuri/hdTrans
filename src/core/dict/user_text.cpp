#include "core/dict/user_text.h"

#include <algorithm>

#include "core/text/utf8.h"

namespace hdTrans::dict {

namespace {

bool EndingTag(char tag) { return tag == 'S' || tag == 'T' || tag == 'U'; }

bool EntryBefore(const UserEntry* a, const UserEntry* b) {
    if (a->prio != b->prio) return a->prio > b->prio;
    if (a->file != b->file) return a->file > b->file;
    if (a->line != b->line) return a->line < b->line;
    return a->order < b->order;
}

}  // namespace

bool AcceptsTag(LookupMode mode, char tag) {
    switch (mode) {
    case LookupMode::General:
        return tag != 'U' && tag != 'T' && tag != 'S' && tag != 'V';
    case LookupMode::AdjectivalNounEnding:
        return tag == 'U';
    case LookupMode::AdjectiveEnding:
        return tag == 'T';
    case LookupMode::VerbEnding:
        return tag == 'S';
    case LookupMode::PrefixVerb:
        return tag == 'V';
    }
    return true;
}

UserText::UserText(std::vector<UserEntry> entries) : entries_(std::move(entries)) {
    for (const UserEntry& e : entries_) {
        const PartCode part = e.Part();
        if (e.hidden || part.Empty() || (e.key.empty() && !EndingTag(part.Tag()))) continue;
        if (e.honorific) {
            honorifics_.push_back(&e);
            continue;
        }
        auto [it, added] = byKey_.try_emplace(e.key);
        it->second.push_back(&e);
        maxKey_ = std::max(maxKey_, e.key.size());
    }
    for (auto& [key, list] : byKey_) {
        std::vector<const UserEntry*> kept;
        for (const UserEntry* e : list) {
            const bool later = std::any_of(list.begin(), list.end(), [e](const UserEntry* f) {
                return f->file > e->file && f->codes->partText == e->codes->partText &&
                       f->registerLevel == e->registerLevel && f->polite == e->polite && f->stative == e->stative;
            });
            if (!later) kept.push_back(e);
        }
        std::stable_sort(kept.begin(), kept.end(), EntryBefore);
        list = std::move(kept);
    }
}

UserText::UserText(const UserText& base, std::vector<UserEntry> entries) : UserText(std::move(entries)) {
    base_ = &base;
    maxKey_ = std::max(maxKey_, base.maxKey_);
    for (auto& [key, list] : byKey_) {
        const auto it = base.byKey_.find(key);
        if (it == base.byKey_.end()) continue;
        list.insert(list.begin(), it->second.begin(), it->second.end());
        const auto all = list;
        std::erase_if(list, [&all](const UserEntry* e) {
            return std::any_of(all.begin(), all.end(), [e](const UserEntry* f) {
                return f->file > e->file && f->codes->partText == e->codes->partText &&
                       f->registerLevel == e->registerLevel && f->polite == e->polite && f->stative == e->stative;
            });
        });
        std::stable_sort(list.begin(), list.end(), EntryBefore);
    }
}

std::size_t UserText::Len() const {
    std::size_t n = base_ ? base_->Len() : 0;
    for (const auto& [key, list] : byKey_) {
        n += list.size();
        if (base_) {
            const auto it = base_->byKey_.find(key);
            if (it != base_->byKey_.end()) n -= it->second.size();
        }
    }
    return n;
}

const UserEntry* UserText::Honorific(std::string_view text) const {
    const UserEntry* best = base_ ? base_->Honorific(text) : nullptr;
    for (const UserEntry* e : honorifics_) {
        if (text.starts_with(e->key) && (!best || e->key.size() >= best->key.size())) best = e;
    }
    return best;
}

std::vector<const UserEntry*> UserText::Search(std::string_view text, LookupMode mode) const {
    std::vector<const UserEntry*> out;
    if (byKey_.empty()) return base_ ? base_->Search(text, mode) : out;
    std::vector<std::size_t> ends;
    if (text.empty()) ends.push_back(0);
    for (std::size_t i = 0; i < text.size() && i < maxKey_;) {
        i += text::DecodeRune(text.substr(i)).size;
        ends.push_back(i);
    }
    for (std::size_t k = ends.size(); k-- > 0;) {
        const std::string_view key = text.substr(0, ends[k]);
        const auto it = byKey_.find(key);
        const std::vector<const UserEntry*>* list = it == byKey_.end() ? nullptr : &it->second;
        if (list == nullptr && base_) {
            const auto inherited = base_->byKey_.find(key);
            if (inherited != base_->byKey_.end()) list = &inherited->second;
        }
        if (list == nullptr) continue;
        for (const UserEntry* e : *list) {
            if (AcceptsTag(mode, e->Part().Tag())) out.push_back(e);
        }
    }
    return out;
}

bool UserText::KeyPast(std::string_view text, std::size_t past) const {
    if (byKey_.empty()) return base_ != nullptr && base_->KeyPast(text, past);
    for (std::size_t i = 0; i < text.size() && i < maxKey_;) {
        i += text::DecodeRune(text.substr(i)).size;
        if (i <= past) continue;
        const std::string_view key = text.substr(0, i);
        if (byKey_.find(key) != byKey_.end() || (base_ != nullptr && base_->byKey_.find(key) != base_->byKey_.end())) return true;
    }
    return false;
}

}  // namespace hdTrans::dict
