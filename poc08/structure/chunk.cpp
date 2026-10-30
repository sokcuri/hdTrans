#include <algorithm>
#include <string>

#include "structure.h"

namespace hd8 {
namespace {

bool IsNounLike(Wc w) {
    return w == Wc::noun || w == Wc::noun_adv || w == Wc::propn || w == Wc::person || w == Wc::num || w == Wc::pron;
}

bool IsContentClass(Wc w) {
    switch (w) {
        case Wc::noun: case Wc::noun_adv: case Wc::propn: case Wc::person: case Wc::num: case Wc::pron:
        case Wc::verb: case Wc::verb_dep: case Wc::adj: case Wc::adj_dep: case Wc::na: case Wc::adv: case Wc::adn:
        case Wc::conj: case Wc::intj: case Wc::filler: case Wc::pref:
            return true;
        default:
            return false;
    }
}

bool IsPunct(Wc w) {
    return w == Wc::comma || w == Wc::period || w == Wc::open || w == Wc::close || w == Wc::sym || w == Wc::space;
}

// A content-class token that still belongs to the previous bunsetsu: compounds, auxiliary verbs after て,
// light verbs after a noun, negation after じゃ/で/く.
bool JoinsPrevious(const std::vector<TokInfo>& info, size_t i) {
    const TokInfo& t = info[i];
    const TokInfo& p = info[i - 1];
    if (p.wc == Wc::pref) return true;
    if (IsNounLike(t.wc) && (IsNounLike(p.wc) || p.wc == Wc::suf_n) && t.wc != Wc::pron && p.wc != Wc::pron)
        return true;
    if (t.row != nullptr && !t.row->afters.empty()) {
        switch (t.row->kind) {
            case AuxKind::aspect: case AuxKind::bene: case AuxKind::light: case AuxKind::neg: case AuxKind::cop:
            case AuxKind::polite: case AuxKind::force: case AuxKind::suffix: case AuxKind::modal:
                if (t.row->sets.role == R::eval || t.row->sets.role == R::eval_neg || t.row->sets.role == R::eval_ok ||
                    t.row->sets.role == R::absorb_neg || t.row->sets.role == R::sumu ||
                    t.row->sets.role == R::irareru)
                    return false;
                return true;
            default:
                break;
        }
    }
    if ((t.wc == Wc::verb || t.wc == Wc::verb_dep) && (p.wc == Wc::verb || p.wc == Wc::verb_dep) &&
        (p.form == Form::renyo))
        return true;
    return false;
}

Case LastCase(const std::vector<TokInfo>& info, int first, int last) {
    for (int i = last; i >= first; --i) {
        if (IsPunct(info[i].wc)) continue;
        const AuxRow* r = info[i].row;
        if (r != nullptr && r->sets.kase >= 0) return static_cast<Case>(r->sets.kase);
        if (r == nullptr || (r->kind != AuxKind::focus && r->kind != AuxKind::final)) break;
    }
    return Case::none;
}

}  // namespace

void Chunk(const StructureData&, const Strings&, const std::vector<TokInfo>& info, Path& path) {
    path.buns.clear();
    const size_t n = info.size();
    Bunsetsu cur;
    bool open = false, hasContent = false;
    size_t pendingOpen = n;  // index of an opening bracket waiting for the next bunsetsu
    auto close = [&](size_t lastTok) {
        if (!open) return;
        cur.last = static_cast<uint16_t>(lastTok);
        path.buns.push_back(cur);
        open = false;
        hasContent = false;
    };
    for (size_t i = 0; i < n; ++i) {
        const TokInfo& t = info[i];
        if (t.wc == Wc::open) {
            if (open && hasContent) close(i - 1);
            if (pendingOpen == n) pendingOpen = i;
            continue;
        }
        bool content = IsContentClass(t.wc) && !(i > 0 && JoinsPrevious(info, i));
        if (content && open && hasContent) close(i - 1);
        if (!open) {
            cur = Bunsetsu{};
            cur.first = static_cast<uint16_t>(pendingOpen < i ? pendingOpen : i);
            cur.content = static_cast<uint16_t>(i);
            open = true;
            pendingOpen = n;
        }
        if (content && !hasContent) {
            hasContent = true;
            cur.content = static_cast<uint16_t>(i);
        }
    }
    if (pendingOpen < n && !open) {
        cur = Bunsetsu{};
        cur.first = cur.content = static_cast<uint16_t>(pendingOpen);
        open = true;
    }
    close(n - 1);
    // Content token: the head noun of a compound (its last noun), else the first content token.
    for (auto& b : path.buns) {
        int c = b.content;
        if (c < b.first || c > b.last) c = b.first;
        if (IsNounLike(info[c].wc) || info[c].wc == Wc::pref) {
            int k = c;
            while (k + 1 <= b.last && (IsNounLike(info[k + 1].wc) || info[k + 1].wc == Wc::suf_n ||
                                       (info[k].wc == Wc::pref && IsContentClass(info[k + 1].wc))))
                ++k;
            if (info[k].wc == Wc::suf_n) --k;
            c = std::max(k, static_cast<int>(b.content));
        }
        b.content = static_cast<uint16_t>(c);
        b.kase = LastCase(info, b.first, b.last);
        uint8_t flags = 0;
        if (b.kase == Case::wa || b.kase == Case::mo) flags |= Topic;
        for (int i = b.first; i <= b.last; ++i) {
            if (info[i].wc == Wc::comma) flags |= CommaAfter;
            if (info[i].wc == Wc::close) flags |= QuoteClose;
        }
        b.flags = flags;
        // Type: P predicate, N nominal, A adnominal or adverbial modifier, C conjunction, I interjection,
        // Q quote (ends in a quoting と/って).
        bool pred = false;
        for (int i = b.first; i <= b.last; ++i) {
            Wc w = info[i].wc;
            if (w == Wc::verb || w == Wc::verb_dep || w == Wc::adj || w == Wc::adj_dep || w == Wc::aux ||
                w == Wc::na_aux || w == Wc::suf_adj || w == Wc::suf_v)
                pred = true;
        }
        Wc cw = info[b.content].wc;
        uint8_t type = 'N';
        if (cw == Wc::conj) type = 'C';
        else if (cw == Wc::intj || cw == Wc::filler) type = 'I';
        else if (pred) type = 'P';
        else if (b.kase == Case::no || cw == Wc::adn || cw == Wc::adv) type = 'A';
        if (b.kase == Case::quote || (b.kase == Case::to && pred)) type = 'Q';
        b.type = type;
    }
}

}  // namespace hd8
