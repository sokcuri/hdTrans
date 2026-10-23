// Text-path sense passes: the target of a word from the sense table when a trigger word
// stands near it, the conjunction readings at the start of a sentence, and the 한 of a
// な after a noun that is the root of a 하다 adjective.
#include <string>
#include <string_view>

#include "core/dict/snapshot.h"
#include "core/text/utf8.h"
#include "core/word/flush.h"
#include "core/word/kana.h"
#include "core/word/strings.h"
#include "core/word/terminal.h"
#include "core/word/util.h"

namespace hdTrans::word {

namespace {
// The kana that end a verb in its dictionary form.
constexpr std::string_view kVerbFinals = "うくぐすつぬぶむる";
}  // namespace

// Whether the item is word, a key or the dictionary form of the verb or adjective the item
// is a stem of.
bool IsWord(const Item* it, std::string_view word) {
    const std::string_view surface = it->group->surface;
    if (surface == word) return true;
    if (surface.empty() || !word.starts_with(surface)) return false;
    const std::string_view rest = word.substr(surface.size());
    switch (At(it->Code(it->selected), 0)) {
        case '4':
            return rest == kJaSuru || (text::RuneCount(rest) == 1 && Has(kVerbFinals, rest));
        case '7':
            return rest == kJaI;
    }
    return false;
}

namespace {

constexpr int kSenseWindow = 3;

// A noun item between kanji nouns of its token: a noun right before it that ends in a kanji,
// or one right after it that starts with one.
bool InKanjiCompound(const Item* it) {
    auto kanji = [](char32_t r) { return (r >= 0x4E00 && r <= 0x9FFF) || r == U'々'; };
    auto noun = [&](const Item* x) {
        return x != nullptr && x->tokenIndex == it->tokenIndex && x->selected >= 0 && At(x->Code(x->selected), 0) == '1';
    };
    if (At(it->Code(it->selected), 0) != '1') return false;
    const std::string_view before = std::string_view(it->text).substr(0, static_cast<std::size_t>(it->start));
    const std::string_view after = From(it->text, it->start + it->length);
    const bool beforeKanji = !before.empty() && kanji(DecodeLastRune(before).cp);
    const bool afterKanji = !after.empty() && kanji(text::DecodeRune(after).cp);
    const Item* prev = it->prev;
    const Item* next = it->next;
    return (noun(prev) && prev->start + prev->length == it->start && beforeKanji) ||
           (noun(next) && it->start + it->length == next->start && afterKanji);
}

// Whether s is a quotation mark or a corner bracket.
bool IsQuoteMark(std::string_view s) {
    for (const std::string_view q : {"“", "”", "「", "」", "『", "』", "\"", "'", "‘", "’"}) {
        if (s == q) return true;
    }
    return false;
}

bool HasTag(const Item* it, std::string_view tag) {
    if (tag.starts_with("sem:")) return dict::SemCodes(it->Attr(it->selected)).HasCode(tag.substr(4));
    return tag.starts_with("gram:") && dict::GramCodes(it->GAttr(it->selected)).HasCode(tag.substr(5));
}

bool SenseTrigger(const Item* x, const Item* it, std::string_view trigger) {
    if (!trigger.starts_with("sem:") && !trigger.starts_with("gram:")) return IsWord(x, trigger);
    const std::size_t at = trigger.find('@');
    if (!HasTag(x, trigger.substr(0, at))) return false;
    if (at == std::string_view::npos) return true;
    if (At(x->Code(x->selected), 0) != '1') return false;
    const std::string_view argument = trigger.substr(at + 1);
    const std::size_t slash = argument.find('/');
    const Item* particle = x->next;
    if (particle == nullptr || particle->selected < 0 || At(particle->Code(particle->selected), 0) != '2' ||
        particle->group->surface != argument.substr(0, slash)) return false;
    // A particle sense must use its own argument, not a neighbouring case phrase.
    if (At(it->Code(it->selected), 0) == '2' && particle != it) return false;
    if (slash == std::string_view::npos) return true;
    const std::string_view predicateTag = argument.substr(slash + 1);
    const Item* predicate = particle;
    for (int n = 0; n < 8; n++) {
        predicate = predicate->next;
        if (predicate == nullptr || predicate->selected < 0) return false;
        const char cls = At(predicate->Code(predicate->selected), 0);
        if (cls == '4') return HasTag(predicate, predicateTag);
        if (cls == '7' || cls == '8' || cls == '5' || cls == '6') return false;
    }
    return false;
}

bool SenseTriggers(const Item* x, const Item* it, const userdict::Sense& sense) {
    for (const auto& trigger : sense.triggers) {
        if (SenseTrigger(x, it, trigger)) return true;
    }
    return false;
}

// Whether a trigger of sense s stands on its side of it: for Before and After within the
// window (a quotation mark, or a comma between nouns, counts but does not stop it), with
// no clause boundary and no other predicate in between. A sense with no triggers applies always, but for a one-kanji
// key not inside a kanji compound.
bool Triggered(Flusher& f, const Item* it, const userdict::Sense& s) {
    if (s.triggers.empty()) return text::RuneCount(s.key) != 1 || !InKanjiCompound(it);
    if (s.side == userdict::Side::Sentence) {
        for (const Item* x = f.w.chain.head; x != nullptr; x = x->next) {
            if (x != it && x->selected >= 0 && SenseTriggers(x, it, s)) return true;
        }
        return false;
    }
    const Item* x = it;
    auto step = [&](const Item* y) { return s.side == userdict::Side::After ? y->next : y->prev; };
    for (int n = 0; n < kSenseWindow; n++) {
        x = step(x);
        if (x != nullptr && IsQuoteMark(x->group->surface)) continue;
        // A comma between two nouns lists them (コップとお皿、お盆): the noun across it can be
        // the trigger, but not the words of its own phrase (沖姉、朝ちゃん).
        if (x != nullptr && x->group->surface == "、" && At(it->Code(it->selected), 0) == '1') {
            const Item* y = step(x);
            if (y != nullptr && y->selected >= 0 && At(y->Code(y->selected), 0) == '1') return SenseTriggers(y, it, s);
            // A comma right after the noun drops its particle (背、伸びたね): the predicate after it
            // is still the noun's own.
            if (n == 0 && s.side == userdict::Side::After) continue;
        }
        if (x == nullptr || x->selected < 0) return false;
        if (SenseTriggers(x, it, s)) return true;
        const char cls = At(x->Code(x->selected), 0);
        // Before the word, an auxiliary verb written right after its verb (生えている角) lets
        // that verb be the trigger.
        const Item* main = x->prev;
        if (cls == '4' && s.side == userdict::Side::Before && main != nullptr && main->selected >= 0 &&
            At(main->Code(main->selected), 0) == '4' && main->JoinsNext() && SenseTriggers(main, it, s)) {
            return true;
        }
        if (cls == '6' || cls == '4' || cls == '7') return false;
    }
    return false;
}

struct InitialConjunction {
    std::string_view surface;
    std::string_view target;
};

constexpr InitialConjunction kInitialConjunctions[] = {
    {"でも", "하지만"}, {"では", "그럼"}, {"なんで", "왜"}, {"だって", "왜냐하면"},
};

// Whether the text ends in a dash, or with ellipsis also in an ellipsis, after trailing
// spaces.
bool EndsInPause(std::string_view s, bool ellipsis) {
    while (!s.empty() && (s.back() == ' ' || s.ends_with("　"))) {
        s.remove_suffix(s.ends_with("　") ? 3 : 1);
    }
    if (s.ends_with("―") || s.ends_with("─") || s.ends_with("—")) return true;
    return ellipsis && (s.ends_with("…") || s.ends_with("‥") || s.ends_with("・") || s.ends_with("·"));
}

// An item that starts its sentence: the first of the chain, or one right after an opening
// bracket or a dash, or after an ellipsis when the utterance goes on after it (……でも、
// but not ……だって」).
bool SentenceInitial(Flusher& f, const Item* it) {
    const Item* n = it->next;
    const bool ellipsis = n != nullptr && n->group->surface != "。" && !InSet(kClosingQuoteItems, n->group->surface) &&
                          !InSet(kSentenceEndItems, n->group->surface);
    if (EndsInPause(std::string_view(it->text).substr(0, static_cast<std::size_t>(it->start)), ellipsis)) return true;
    const Item* p = it->prev;
    if (p == nullptr) return true;
    for (int t = it->tokenIndex - 1; t > p->tokenIndex && t >= 0; t--) {
        if (EndsInPause(f.w.Tok(t).text, ellipsis)) return true;
    }
    const std::string_view s = p->group->surface;
    return s == "「" || s == "『" || s == "（" || s == "(" || s == "【" || s == "〈" || s == "《";
}

// A katakana loanword that reads as a 하다 adjective: one that ends like an English adjective.
bool NaRoot(const Item* it) {
    const std::string_view surface = it->group->surface;
    for (std::string_view rest = surface; !rest.empty(); rest.remove_prefix(text::DecodeRune(rest).size)) {
        if (!IsKatakana(text::DecodeRune(rest).cp)) return false;
    }
    constexpr std::string_view kSuffixes[] = {"フル", "カル", "ブル", "プル", "アル", "ティブ", "ィブ", "イジー",
                                              "ーシー", "レス", "ッシュ", "ート"};
    for (const std::string_view suffix : kSuffixes) {
        if (surface.ends_with(suffix) && surface != suffix) return true;
    }
    return false;
}

// A noun item whose target is the root of a 하다 adjective.
bool HadaRoot(Flusher& f, const Item* it) {
    if (it->adjectival) return true;
    const std::string root(it->EntryText(it->selected));
    for (const dict::Candidate& c : f.w.snapshot.RetrieveRecords(it->group->surface, dict::LookupMode::General)) {
        if (c.codes == nullptr || At(c.codes->partText, 1) != '8') continue;
        if (c.target == root + "%A1@" || c.target == root + "%D1@") return true;
    }
    return false;
}

}  // namespace

// Writes target as the target of the candidate of class that the item takes: the selected
// candidate becomes the first of class with that target, else the selected candidate
// takes it. The ending of an Av candidate (an adjective with verb endings) was chosen for
// a verb, so another candidate of class takes the target in its place.
void TakeTarget(Item* it, char cls, std::string_view target, Selection source) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        const Entry* e = it->group->At(slot);
        if (At(e->code, 0) == cls && e->dst == target) {
            it->SelectCandidate(slot, source);
            return;
        }
    }
    if (Has(it->GAttr(it->selected), "Av")) {
        for (int slot = 0; slot < count; slot++) {
            const Entry* e = it->group->At(slot);
            if (At(e->code, 0) == cls && !Has(e->gattr, "Av")) {
                it->SelectCandidate(slot, source);
                break;
            }
        }
    }
    it->AssignEntrySurface(it->selected, target, source);
    it->choice = source;
}

void ChooseSenses(Flusher& f) {
    const dict::Snapshot& snapshot = f.w.snapshot;
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->Fixed() || it->selected < 0 || it->selected >= static_cast<int>(it->group->entries.size())) continue;
        if (it->group->At(it->selected)->neighbourAssigned) continue;
        const auto senses = snapshot.Senses().find(it->group->surface);
        if (senses == snapshot.Senses().end()) continue;
        for (const userdict::Sense& s : senses->second) {
            if (At(it->Code(it->selected), 0) == s.classDigit && Triggered(f, it, s)) {
                TakeTarget(it, s.classDigit, s.target, {s.triggers.empty() ? Provenance::SenseDefault : Provenance::Sense, s.row});
                it->adjectival = s.adjective;
                break;
            }
        }
    }
}

void ChooseInitialConjunctions(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->Fixed() || it->selected < 0 || !SentenceInitial(f, it)) continue;
        const std::string_view surface = it->group->surface;
        bool done = false;
        for (const InitialConjunction& c : kInitialConjunctions) {
            if (c.surface != surface) continue;
            // 왜 takes no copula or particle (なんでだ 어째서야, なんでって 어째서냐니, not 왜야).
            const Item* n = it->next;
            const char32_t first = n != nullptr ? text::DecodeRune(n->group->surface).cp : 0;
            const bool joined = n != nullptr && n->selected >= 0 && n->tokenIndex == it->tokenIndex &&
                                At(n->Code(n->selected), 0) == '2' && first >= U'ぁ' && first <= U'ゖ';
            TakeTarget(it, At(it->Code(it->selected), 0), c.target == "왜" && joined ? "어째서" : c.target,
                       {Provenance::Initial});
            done = true;
            break;
        }
        if (done) continue;
        Item* ni = it->next;
        if (surface == "とき" && ni != nullptr && ni->selected >= 0 && !ni->Fixed() && ni->group->surface == "に" &&
            ni->next != nullptr && ni->next->group->surface == "、") {
            TakeTarget(it, At(it->Code(it->selected), 0), "그런데", {Provenance::Initial});
            TakeTarget(ni, At(ni->Code(ni->selected), 0), "", {Provenance::Initial});
        }
    }
}

void HanBeforeNa(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->next;
        if (n != nullptr && (n->group->surface == "』" || n->group->surface == "」")) n = n->next;
        if (n == nullptr || it->selected < 0 || n->selected < 0 || n->Fixed() || At(it->Code(it->selected), 0) != '1' ||
            !n->group->surface.starts_with(kJaNa) || !n->EntryText(n->selected).starts_with(kKoIn) ||
            (!HadaRoot(f, it) && !NaRoot(it))) {
            continue;
        }
        const std::string_view entry = n->EntryText(n->selected);
        const std::string target = std::string(kKoHan) + std::string(entry.substr(kKoIn.size()));
        TakeTarget(n, At(n->Code(n->selected), 0), target);
    }
}

}  // namespace hdTrans::word
