#include "core/word/worker.h"

#include <algorithm>

#include "core/dict/classes.h"
#include "core/output/pieces.h"
#include "core/word/strings.h"
#include "core/tokenizer/tokenize.h"

namespace hdTrans::word {

Worker::Worker(const tokenizer::Tokens& chunkTokens, const dict::Snapshot& dictionary, const dict::LookupContext& lookup,
               const Settings& stageSettings, Arena& storage, LookupObserver lookupObserver)
    : tokens(chunkTokens),
      snapshot(dictionary),
      lookupContext(lookup),
      settings(stageSettings),
      arena(storage),
      observer(lookupObserver) {
    spaced.assign(tokens.size(), 0);
}

tokenizer::TokenView Worker::Tok(int j) const {
    if (spaced[static_cast<std::size_t>(j)]) return {tokenizer::kTypeWideSpace, " "};
    if (current < 0) return {tokens[static_cast<std::size_t>(j)].type, tokens.Raw(static_cast<std::size_t>(j))};
    return tokens.View(static_cast<std::size_t>(j), static_cast<std::size_t>(current));
}

int Worker::RubyPart(int j) {
    if (rubyParts_.empty()) {
        rubyParts_.assign(tokens.size(), 0);
        uint8_t part = 0;
        for (std::size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i].span == 0) {
                rubyParts_[i] = part;
                continue;
            }
            const std::string_view tag = tokens.Raw(i);
            if (tag == "{rb}") part = 1;
            else if (tag == "{rt}") part = 2;
            else if (tag == "{/rb}" || tag == "{/rt}") part = 0;
        }
    }
    return j >= 0 && static_cast<std::size_t>(j) < rubyParts_.size() ? rubyParts_[static_cast<std::size_t>(j)] : 0;
}

int Worker::QuoteDepth(int j) {
    if (quoteDepths_.empty()) {
        quoteDepths_.reserve(tokens.size() + 1);
        int depth = settings.quoteDepth;
        for (std::size_t i = 0; i < tokens.size(); i++) {
            quoteDepths_.push_back(depth);
            depth = QuoteDepthAfter(tokens.Raw(i), depth);
        }
        quoteDepths_.push_back(depth);
    }
    if (j < 0) return settings.quoteDepth;
    return quoteDepths_[std::min(static_cast<std::size_t>(j), quoteDepths_.size() - 1)];
}

OpenMarks OpenMarksAfter(std::string_view s, OpenMarks open) {
    while (!s.empty()) {
        const text::Rune c = text::DecodeRune(s);
        const std::string_view mark = s.substr(0, std::max<std::size_t>(c.size, 1));
        if (c.cp == output::kEmphasisOpen) open.spelled++;
        if (c.cp == output::kEmphasisClose && open.spelled > 0) open.spelled--;
        if (dict::InClass(HDTRANS_CLASS("mark.thought-open"), mark)) open.thought++;
        if (dict::InClass(HDTRANS_CLASS("mark.thought-close"), mark) && open.thought > 0) open.thought--;
        s.remove_prefix(mark.size());
    }
    return open;
}

OpenMarks Worker::OpenMarksAt(int j) {
    if (openMarks_.empty()) {
        openMarks_.reserve(tokens.size() + 1);
        OpenMarks open;
        for (std::size_t i = 0; i < tokens.size(); i++) {
            openMarks_.push_back(open);
            open = OpenMarksAfter(tokens.Raw(i), open);
        }
        openMarks_.push_back(open);
    }
    return openMarks_[std::min(static_cast<std::size_t>(std::max(j, 0)), openMarks_.size() - 1)];
}

std::string_view Worker::NextText(int j) const {
    const tokenizer::TokenView token = Tok(j);
    if (!settings.improvements.preparedNextToken || j <= current || Span(j) != 0 ||
        !tokenizer::CanonicalType(token.type) || token.text.empty()) return token.text;
    std::string canonical;
    tokenizer::AppendCanonicalText(canonical, token.text);
    return arena.Keep(std::move(canonical));
}

std::string_view Worker::Typed(int j) const {
    if (spaced[static_cast<std::size_t>(j)]) return " ";
    return tokens.Merged(static_cast<std::size_t>(j));
}

Record* Worker::NewRecord() {
    Record* r = arena.records.New();
    r->source = {};
    r->target.clear();
    r->partCode = {};
    r->semantic = {};
    r->attr.clear();
    r->dictCode = {};
    r->ruleNo = 0;
    r->id = 0;
    r->user = nullptr;
    r->child = nullptr;
    return r;
}

Entry* Worker::NewEntry() {
    Entry* e = arena.entries.New();
    e->dst.clear();
    e->code = {};
    e->attr = {};
    e->field = {};
    e->gattr.clear();
    e->cls = 0;
    e->ruleId = -1;
    e->fixed = false;
    e->user = nullptr;
    e->targetChoice = {};
    e->neighbourAssigned = false;
    return e;
}

Entry* Worker::NewEntry(const Record& r) {
    Entry* e = NewEntry();
    e->cls = At(r.partCode, 0);
    e->dst.assign(r.target);
    e->code = From(r.partCode, 1);
    e->attr = r.semantic;
    e->field = r.dictCode;
    e->gattr.assign(r.attr);
    e->ruleId = r.ruleNo;
    e->user = r.user;
    e->fixed = r.user != nullptr && !r.user->kana;
    return e;
}

// An empty list gives an empty group: a lookup restricted to one bucket (modes 1..4)
// may find records only in other buckets.
Group* Worker::NewGroup(Record* records, char mode) {
    Group* g = arena.groups.New();
    g->surface.clear();
    g->length = 0;
    g->entries.clear();
    g->records = nullptr;
    if (records == nullptr) return g;
    const char firstSubtype = At(records->partCode, 3);
    const char kind = At(records->partCode, 1);
    g->surface.assign(records->source);
    g->length = static_cast<int>(g->surface.size());
    Record* rec = records;
    const int limit = settings.improvements.expandedGroups ? kDirectSlots : kReferenceDirectSlots;
    for (int j = 0; j < limit && rec != nullptr; j++) {
        const std::string_view code = rec->partCode;
        if (kind != At(code, 1) || g->surface != rec->source || (rec->user == nullptr) != (records->user == nullptr)) {
            break;
        }
        bool accept = true;
        if (kind == '4' && !g->entries.empty()) {
            const bool special = (rec->source == kJaGyou || rec->source == kJaI) && (At(code, 3) == '0' || At(code, 3) == '1');
            if (!special || (mode != '6' && mode != '7')) accept = VerbClassJoins(mode, firstSubtype, At(code, 3));
        }
        if (accept) g->entries.push_back(NewEntry(*rec));
        rec = rec->child;
    }
    g->records = rec;
    return g;
}

Detail* Worker::NewDetail(int cls, int token) {
    Detail* d = arena.details.New();
    d->cls = cls;
    d->nextType = 0;
    d->nextText = {};
    d->nonterminalBoundary = settings.improvements.nonterminalBoundary;
    d->tailLengthCheck = settings.improvements.tailLengthCheck;
    d->causativePassive = settings.text && settings.improvements.causativePassive;
    d->kind = -1;
    d->record = 0;
    d->voice = Voice::None;
    d->punct = 0;
    d->group = nullptr;
    d->ResetDirect(0);
    d->resultCount = 0;
    d->selected = -1;
    d->scanOffset = 0;
    d->patternKind = -1;
    d->patternMatched = 0;
    d->patternLength = 0;
    if (token + 1 < TokenCount()) {
        const tokenizer::TokenView next = Tok(token + 1);
        d->nextType = next.type;
        d->nextText = NextText(token + 1);
    }
    return d;
}

Item* Worker::NewItem(Detail* d, Group* g, std::string_view text, int start, int token) {
    Item* it = arena.items.New();
    it->w = this;
    it->group = g;
    it->detail = d;
    it->tokenIndex = token;
    it->start = start;
    it->text = text;
    it->length = g->length;
    it->selected = -1;
    it->choice = {};
    it->directChoice = {};
    it->shadowChoices.clear();
    it->prev = nullptr;
    it->next = nullptr;
    it->compDI = -1;
    it->compUmiDI = -1;
    it->selectionFlag = 0;
    it->adjectival = false;
    it->lexical = false;
    it->appended.clear();
    return it;
}

bool VerbClassJoins(char mode, char first, char sub) {
    auto in = [](char c, std::string_view set) { return set.find(c) != std::string_view::npos; };
    if (first == sub) return true;
    switch (mode) {
    case '0': case '=':
        return in(first, "78") && in(sub, "78");
    case '1': case '3':
        return in(first, "78;=") && in(sub, "78;=");
    case '2':
        return in(first, "37;=") && in(sub, "3;=");
    case '4': case '@':
        return (in(first, "7;=") && in(sub, "7;=")) || (in(first, "9:") && in(sub, "9:"));
    case '5': case '9':
        return in(first, "7;=") && in(sub, "7;=");
    case '6': case '7':
        if (in(first, "048")) return in(sub, "048");
        if (in(first, "39")) return in(sub, "39");
        if (in(first, "56<")) return in(sub, "56<");
        return in(first, "7;=") && in(sub, "7;=");
    case '8': case ':': case ';': case '<':
        return false;
    case '>': case '?':
        return in(first, "79;=") && in(sub, "79;=");
    case 'A':
        return (in(first, "39") && in(sub, "39")) || (in(first, "48") && in(sub, "48")) || (in(first, "56") && in(sub, "56"));
    }
    return true;
}

}  // namespace hdTrans::word
