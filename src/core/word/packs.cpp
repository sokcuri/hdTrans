// The packs layer (docs/packs.md): a pack row overrides one decision of a word whose key and
// class digit it names and whose cue holds, and leaves the rest of the sentence to the chain.
// Voice rows decide the reading of (ら)れ inside DecideVoice; sense, name, polarity and tense
// rows run after every other pass of the word stage. Per word and slot the most specific row
// wins: lexical atoms, then class depth, then neighbour and form atoms, then register atoms,
// then the number of atoms (the order of the lexicon's cue DSL), then pri, then load order.
#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/pack.h"
#include "core/dict/snapshot.h"
#include "core/word/flush.h"
#include "core/word/util.h"

namespace hdTrans::word {

namespace {

// The class digit of a word: of its selected candidate, or of its first before selection.
char ClassOf(const Item* x) {
    if (x == nullptr || x->group == nullptr || x->group->entries.empty()) return 0;
    return At(x->Code(std::max(x->selected, 0)), 0);
}
bool Predicate(char c) { return c == '4' || c == '7' || c == '8'; }
bool SentenceEnd(const Item* x) {
    const std::string_view s = x->group->surface;
    return s == "。" || s == "！" || s == "？" || s == "!" || s == "?" || s == "」" || s == "』";
}

// The argument slot a particle marks, "" for none.
std::string_view ArgSlot(std::string_view particle) {
    struct Mark {
        std::string_view particle, slot;
    };
    static constexpr Mark kMarks[] = {{"が", "subj"}, {"は", "subj"},  {"を", "obj"}, {"に", "dat"},  {"で", "loc"},
                                      {"と", "with"}, {"から", "from"}, {"へ", "to"},  {"まで", "to"}};
    for (const Mark& m : kMarks)
        if (particle == m.particle) return m.slot;
    return {};
}

// The negative auxiliary item written right after a verb or adjective (行か|ない, 高く|なかっ), or null.
Item* NegativeAux(const Item* it) {
    Item* n = it->next;
    if (n == nullptr || n->group == nullptr || n->selected < 0 || n->Fixed() || n->tokenIndex != it->tokenIndex ||
        n->start != it->start + it->length)
        return nullptr;
    const std::string_view s = n->group->surface;
    return s == "な" || s == "ない" || s == "なかっ" || s == "なく" || s == "ね" || s == "ん" ? n : nullptr;
}

// What the cue atoms of a row read about one word (the context of the lexicon's cue DSL).
struct Context {
    std::vector<std::pair<std::string_view, const Item*>> args;  // slot, noun of a case phrase of the predicate
    const Item* head = nullptr;  // the noun a predicate stands right before (its adnominal head)
    const Item* mod = nullptr;   // the word right before a noun that modifies it
    const Item* pred = nullptr;  // the word the word's phrase depends on
    const Item* next = nullptr;
    const Item* after = nullptr;  // the word before (the word is after it)
    std::set<std::string_view> forms;
    bool polite = false;
    std::string_view voice;  // voice rows: the reading of (ら)れ the chain decided
};

Context ContextOf(const Flusher& f, const Item* it) {
    Context c;
    const char cls = ClassOf(it);
    c.next = it->After();
    c.after = it->prev;
    if (Predicate(cls)) {
        // the case phrases before it in its clause (noun + particle), right to left
        for (const Item* q = it->prev; q != nullptr && q->group != nullptr; q = q->prev) {
            if (SentenceEnd(q) || q->group->surface == "「" || q->group->surface == "『" || Predicate(ClassOf(q))) break;
            if (ClassOf(q) == '2' && q->prev != nullptr && ClassOf(q->prev) == '1') {
                if (const std::string_view slot = ArgSlot(q->group->surface); !slot.empty()) c.args.emplace_back(slot, q->prev);
                q = q->prev;
            }
        }
        if (c.next != nullptr && ClassOf(c.next) == '1' && it->DirectSurface().find("て") == std::string_view::npos) {
            c.head = c.next;
            c.pred = c.next;
        }
    } else if (cls == '1') {
        if (it->prev != nullptr && (Predicate(ClassOf(it->prev)) || ClassOf(it->prev) == '1')) c.mod = it->prev;
        const Item* p = c.next;
        if (p != nullptr && ClassOf(p) == '2') {
            for (int n = 0; n < 8 && (p = p->next) != nullptr && p->group != nullptr; n++) {
                if (SentenceEnd(p)) break;
                if (Predicate(ClassOf(p)) || ClassOf(p) == '1') {
                    c.pred = p;
                    break;
                }
            }
        }
    }
    // the form of the word, from its ending and the word after it
    // the ending, with that of a negative auxiliary written right after the word
    std::string ending(it->DirectSurface());
    if (const Item* aux = NegativeAux(it)) {
        ending += aux->group->surface;
        ending += aux->DirectSurface();
    }
    const std::string_view after = c.next != nullptr ? std::string_view(c.next->group->surface) : std::string_view();
    if ((ending.find("た") != std::string_view::npos || ending.find("だ") != std::string_view::npos) &&
        ending.find("たい") == std::string_view::npos && ending.find("たく") == std::string_view::npos &&
        ending.find("だろ") == std::string_view::npos) {
        c.forms.insert("ta");
    }
    for (const std::string_view neg : {"ない", "なかっ", "なく", "ず", "ぬ", "ません"})
        if (ending.find(neg) != std::string_view::npos) c.forms.insert("neg");
    if (ending.ends_with("て") || ending.ends_with("で")) {
        c.forms.insert("te");
        if (after.starts_with("くださ") || after.starts_with("下さ") || after.starts_with("くれ")) c.forms.insert("te-request");
    }
    if (ending.find("よう") != std::string_view::npos || ending.find("おう") != std::string_view::npos ||
        ending.find("ましょう") != std::string_view::npos) {
        c.forms.insert("vol");
    }
    if (cls == '1' || cls == '8') {
        if (after == "だ" || after == "です" || after == "じゃ" || after == "だった" || after == "でした" || after == "だろう" ||
            after == "でしょう")
            c.forms.insert("cop");
        if (after == "な") c.forms.insert("attr");
    }
    if (cls == '8' && ending.starts_with("な")) c.forms.insert("attr");
    if (cls == '7' && ending.ends_with("く") && c.next != nullptr && Predicate(ClassOf(c.next))) c.forms.insert("adv");
    for (const Item* x = f.w.chain.head; x != nullptr; x = x->next) {
        if (x->group == nullptr) continue;
        const std::string_view e = x->DirectSurface(), s = x->group->surface;
        if (e.find("ます") != std::string_view::npos || e.find("まし") != std::string_view::npos ||
            e.find("ませ") != std::string_view::npos || e.find("です") != std::string_view::npos || s == "です" ||
            s == "でした" || s == "でしょう") {
            c.polite = true;
            break;
        }
    }
    return c;
}

// Whether every atom of the row holds for the word, and its specificity: lexical atoms, class
// depth, neighbour and form atoms, register atoms, atom count.
bool Holds(const dict::PackTable& t, const dict::PackRow& r, const Context& c, std::array<int, 5>& spec) {
    spec = {0, 0, 0, 0, static_cast<int>(r.atoms.size())};
    for (const dict::PackAtom& a : r.atoms) {
        const bool none = a.values.size() == 1 && a.values[0] == "-";
        const bool filled = a.values.size() == 1 && a.values[0] == "*";
        std::vector<const Item*> pool;
        bool nouns = true;  // the atom reads nouns: a value matches by lemma or class (LEMMA/CLASS kinds)
        if (a.slot == "arg" || a.slot == "subj" || a.slot == "obj" || a.slot == "dat" || a.slot == "loc" ||
            a.slot == "with" || a.slot == "from" || a.slot == "to") {
            for (const auto& [slot, noun] : c.args)
                if (a.slot == "arg" || slot == a.slot) pool.push_back(noun);
            // the noun an adnominal predicate modifies fills its empty subject or object
            if (pool.empty() && (a.slot == "subj" || a.slot == "obj") && c.head != nullptr) pool.push_back(c.head);
        } else if (a.slot == "head") {
            if (c.head != nullptr) pool.push_back(c.head);
        } else if (a.slot == "mod" || a.slot == "pred" || a.slot == "next" || a.slot == "after") {
            nouns = false;
            const Item* x = a.slot == "mod" ? c.mod : a.slot == "pred" ? c.pred : a.slot == "next" ? c.next : c.after;
            if (x != nullptr && x->group != nullptr) pool.push_back(x);
        } else if (a.slot == "form") {
            if (std::none_of(a.values.begin(), a.values.end(), [&](const std::string& v) { return c.forms.count(v) > 0; }))
                return false;
            spec[2]++;
            continue;
        } else if (a.slot == "voice") {
            if (std::find(a.values.begin(), a.values.end(), c.voice) == a.values.end()) return false;
            spec[2]++;
            continue;
        } else if (a.slot == "register") {
            if (std::none_of(a.values.begin(), a.values.end(), [&](const std::string& v) {
                    return (v == "polite" && c.polite) || ((v == "casual" || v == "plain") && !c.polite);
                }))
                return false;
            spec[3]++;
            continue;
        } else {
            return false;  // domain, main.vol: the chain has no such feature
        }
        if (none || filled) {
            if (pool.empty() != none) return false;
            spec[2]++;
            continue;
        }
        bool lexical = false, held = false;
        int depth = 0;
        for (const Item* x : pool) {
            for (const std::string& v : a.values) {
                if (v.starts_with("@")) {
                    if (t.Member(x->group->surface, v)) {
                        held = true;
                        depth = std::max(depth, t.Depth(v));
                    }
                } else if (IsWord(x, v, std::max(x->selected, 0))) {
                    held = lexical = true;
                }
            }
        }
        if (!held) return false;
        if (!nouns) spec[2]++;
        else if (lexical) spec[0]++;
        else spec[1] += depth;
    }
    return true;
}

// The row of slot that decides the word, or -1.
int BestRow(const Flusher& f, const dict::PackTable& t, const Item* it, std::string_view key, dict::PackSlot slot,
            std::string_view voice = {}) {
    const auto rows = t.byKey.find(key);
    if (rows == t.byKey.end()) return -1;
    const char cls = ClassOf(it);
    bool any = false;
    for (const int i : rows->second) {
        const dict::PackRow& r = t.rows[static_cast<std::size_t>(i)];
        any = any || (r.slot == slot && r.classDigit == cls);
    }
    if (!any) return -1;
    Context c = ContextOf(f, it);
    c.voice = voice;
    int best = -1;
    std::array<int, 5> bestSpec{};
    for (const int i : rows->second) {
        const dict::PackRow& r = t.rows[static_cast<std::size_t>(i)];
        std::array<int, 5> spec{};
        if (r.slot != slot || r.classDigit != cls || !Holds(t, r, c, spec)) continue;
        if (best < 0 || spec > bestSpec || (spec == bestSpec && r.pri > t.rows[static_cast<std::size_t>(best)].pri)) {
            best = i;
            bestSpec = spec;
        }
    }
    return best;
}

}  // namespace

const dict::PackRow* PackVoice(Flusher& f, Item* it, std::string_view stem, Voice& v) {
    const dict::PackTable& t = f.w.snapshot.Packs();
    if (t.Empty() || it->Fixed()) return nullptr;
    const std::string_view decided = v == Voice::Passive       ? "passive"
                                     : v == Voice::Potential   ? "potential"
                                     : v == Voice::Respectful  ? "respectful"
                                     : v == Voice::Spontaneous ? "spontaneous"
                                                               : "none";
    // A word in (ら)れ of the dictionary (読まれ) is keyed by the verb it is made from.
    int best = BestRow(f, t, it, it->group->surface, dict::PackSlot::Voice, decided);
    if (best < 0 && stem != it->group->surface) best = BestRow(f, t, it, stem, dict::PackSlot::Voice, decided);
    if (best < 0) return nullptr;
    const dict::PackRow& r = t.rows[static_cast<std::size_t>(best)];
    v = r.target == "passive"       ? Voice::Passive
        : r.target == "potential"   ? Voice::Potential
        : r.target == "respectful"  ? Voice::Respectful
        : r.target == "spontaneous" ? Voice::Spontaneous
                                    : Voice::None;
    it->packRows.push_back(r.global);
    return &r;
}

void ApplyPacks(Flusher& f) {
    const dict::PackTable& t = f.w.snapshot.Packs();
    if (t.Empty()) return;
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->group == nullptr || it->selected < 0 || it->selected >= static_cast<int>(it->group->entries.size()))
            continue;
        // A fixed word (a user's word, a span the sound stage wrote) keeps its decisions; a name of the
        // name layer takes name rows.
        const Entry* chosen = it->group->At(it->selected);
        const bool nameOnly = it->Fixed();
        if (nameOnly && (chosen->user == nullptr || !chosen->user->nameLayer)) continue;
        const char cls = ClassOf(it);
        for (const dict::PackSlot slot :
             {dict::PackSlot::Sense, dict::PackSlot::Name, dict::PackSlot::Polarity, dict::PackSlot::Tense}) {
            if (nameOnly && slot != dict::PackSlot::Name) continue;
            const int best = BestRow(f, t, it, it->group->surface, slot);
            if (best < 0) continue;
            const dict::PackRow& r = t.rows[static_cast<std::size_t>(best)];
            const Selection source{Provenance::Pack, r.global};
            if (nameOnly) {
                it->AssignEntrySurface(it->selected, r.target, source);
                it->choice = source;
            } else if (slot == dict::PackSlot::Sense || slot == dict::PackSlot::Name) {
                if (it->group->At(it->selected)->neighbourAssigned) continue;
                TakeTarget(it, cls, r.target, source);
                it->adjectival = r.adjective;
            } else {
                // Polarity and tense rows both write the ending; the first that applies holds it.
                if (it->DirectIndex() < 0 || it->directChoice.layer == Provenance::Pack) continue;
                it->AssignDirectSlot(it->DirectIndex(), r.target, source);
                it->directChoice = source;
                // A polarity row writes the whole negation: the negative auxiliary after the word
                // (行か|ない) is left empty.
                if (Item* aux = NegativeAux(it); slot == dict::PackSlot::Polarity && aux != nullptr) {
                    TakeTarget(aux, ClassOf(aux), "", source);
                    if (aux->DirectIndex() >= 0) aux->AssignDirectSlot(aux->DirectIndex(), "", source);
                }
            }
            it->packRows.push_back(r.global);
        }
    }
}

}  // namespace hdTrans::word
