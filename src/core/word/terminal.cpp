// The terminal pass: a bare attributive verb or adjective becomes the terminal form of
// the same tense when what follows it ends the sentence or a clause, or when an ellipsis
// or a dash separates it from the next clause.
#include <cstddef>
#include <string>
#include <string_view>

#include "core/dict/classes.h"
#include "core/tokenizer/token.h"
#include "core/word/flush.h"
#include "core/word/terminal.h"
#include "core/word/strings.h"
#include "core/word/util.h"

namespace hdTrans::word {
namespace {

bool IsOneOfClosing(const Item* it) { return it != nullptr && InSet(kClosingQuoteItems, it->group->surface); }

bool IsOneOfCommas(const Item* it) { return it != nullptr && InSet(kCommaItems, it->group->surface); }

bool IsOneOfSentenceEnds(const Item* it) { return it != nullptr && InSet(kSentenceEndItems, it->group->surface); }

// A verb or adjective item that does not start with a noun.
bool IsPredicate(const Item* it) {
    const std::string_view code = it->Code(it->selected);
    const char cls = At(code, 0);
    if ((cls != '4' && cls != '7') || (cls == '4' && At(code, 2) == '9')) return false;
    std::string_view entry = it->EntryText(it->selected);
    while (!entry.empty() && entry.front() == ' ') entry.remove_prefix(1);
    if (entry.starts_with(kKoGeot)) return false;
    const std::string_view s = it->group->surface;
    if (dict::StartsWithMember(HDTRANS_CLASS("follower.not-predicate-start"), s) > 0) return false;
    // A particle after the first character makes a phrase that starts with a noun.
    for (std::size_t i = FirstChar(s).size(); i < s.size();) {
        const std::string_view r = FirstChar(s.substr(i));
        if (dict::InClass(HDTRANS_CLASS("particle.phrase"), r)) return false;
        i += r.size();
    }
    return true;
}

// The form code of the nominal ending of a verb or an adjective; 0 for others.
char NominalForm(char cls) {
    if (cls == '4') return 'B';
    if (cls == '7') return '8';
    return 0;
}

// A run of predicate items starting at it whose last item ends the sentence (past
// closing quotes), or ends in 다 before a comma.
bool EndsClause(const Item* it) {
    if (!IsPredicate(it)) return false;
    while (it->After() != nullptr && IsPredicate(it->After())) it = it->After();
    const std::string_view ending = it->DirectText(it->DirectIndex());
    if (At(ending, 0) == NominalForm(At(it->Code(it->selected), 0))) return false;
    const Item* next = it->After();
    while (IsOneOfClosing(next)) next = next->next;
    if (next == nullptr || IsOneOfSentenceEnds(next)) return true;
    return IsOneOfCommas(next) && it->DirectIndex() >= 0 && it->DirectText(it->DirectIndex()).ends_with(kKoDa);
}

// Whether s is a run of ellipsis or dash characters as the tokenizer writes them.
bool PauseMarks(std::string_view s) {
    static constexpr std::string_view kMarks[] = {"…", "‥", "―", "—", "·", "・", "."};
    if (s.empty()) return false;
    while (!s.empty()) {
        const std::size_t n = s.size();
        for (const std::string_view m : kMarks) {
            if (s.starts_with(m)) s.remove_prefix(m.size());
        }
        if (s.size() == n) return false;
    }
    return true;
}

// Text input: it ends its token and is not the quotative いう (とかいう……), only ellipsis
// or dash tokens stand between it and the next item, and that item is a conjunction, or
// an adverb, a predicate or a noun that does not start with one of noun.dependent-ellipsis, the
// noun not ending the sentence (知らない……本当に, 分からない……でも, 帰る――それでいい,
// 熱すぎる・・・勘弁して, but 行く……ことにした, いた――んだけど, 消えた……直後。).
bool PausedClause(const Flusher& f, const Item* it) {
    const Item* next = it->After();
    if (!f.w.settings.text || next == nullptr || next->tokenIndex <= it->tokenIndex + 1 ||
        static_cast<std::size_t>(it->start + it->length) != it->text.size()) {
        return false;
    }
    const std::string ja = it->group->surface + std::string(it->DirectSurface());
    if (ja == "いう" || ja == "言う") return false;
    for (int j = it->tokenIndex + 1; j < next->tokenIndex; j++) {
        const tokenizer::TokenView t = f.w.Tok(j);
        if (t.type != tokenizer::kTypeSymbol || !PauseMarks(t.text)) return false;
    }
    if (next->choice.layer == Provenance::Initial) return true;
    const char cls = At(next->Code(next->selected), 0);
    if (cls != '1' && cls != '3' && cls != '4' && cls != '7') return false;
    if (dict::StartsWithMember(HDTRANS_CLASS("noun.dependent-ellipsis"), next->group->surface) > 0) return false;
    if (cls != '1') return true;
    const Item* after = next->next;
    while (IsOneOfClosing(after)) after = after->next;
    return after != nullptr && !IsOneOfSentenceEnds(after) && after->group->surface != "。";
}

}  // namespace

void PreferTerminal(Flusher& f) {
    const bool forward = f.w.settings.improvements.forwardTerminal;
    for (Item* it = forward ? f.w.chain.head : f.w.chain.tail; it != nullptr; it = forward ? it->next : it->prev) {
        if (it->DirectIndex() < 0 || OwnedGroup(it->DirectGroup()) || !Has(it->EntryText(it->selected), "@")) continue;
        const char cls = At(it->Code(it->selected), 0);
        const std::string_view text = it->DirectText(it->DirectIndex());
        const std::string ja = it->group->surface + std::string(it->DirectSurface());
        for (const TerminalForm& form : kTerminalForms) {
            if (form.cls != cls || form.attributive != text) continue;
            bool endsWithForm = false;
            for (int i = 0; i < form.lastCount; i++) endsWithForm = endsWithForm || LastChar(ja) == form.last[i];
            if (!endsWithForm) continue;
            const Item* next = it->After();
            bool comma = false;
            while (IsOneOfCommas(next) || IsOneOfClosing(next)) {
                comma = comma || IsOneOfCommas(next);
                next = next->next;
            }
            if (!PausedClause(f, it) && next != nullptr && !IsOneOfSentenceEnds(next) &&
                (!EndsClause(next) || (cls == '7' && !comma && !it->group->surface.ends_with(kJaNa) &&
                                       At(next->Code(next->selected), 0) != '7'))) {
                break;
            }
            for (int slot = 0; slot < it->DirectCount(); slot++) {
                if (it->detail->enabled[slot] && it->DirectText(slot) == form.terminal) {
                    it->SetDirectIndex(slot);
                    break;
                }
            }
            break;
        }
    }
}

}  // namespace hdTrans::word
