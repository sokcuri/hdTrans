// The packs layer (docs/packs.md): a pack row overrides one decision of a word whose key and
// class digit it names and whose cue holds, and leaves the rest of the sentence to the chain.
// Voice rows decide the reading of (ら)れ inside DecideVoice; sense, name, polarity and tense
// rows run after every other pass of the word stage. Per word and slot the most specific row
// wins: a row that names the lemma before one of a lemma class before one of lemma * (every word of
// its pos), then lexical atoms, class depth, neighbour and form atoms, register atoms and the number
// of atoms (the order of the lexicon's cue DSL), then pri, then load order. A row whose target is -
// keeps the slot as the chain decided it.
#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/dict/codes.h"
#include "core/word/attach.h"
#include "core/userdict/compile.h"
#include "core/dict/pack.h"
#include "core/dict/snapshot.h"
#include "core/text/utf8.h"
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
                                      {"と", "with"}, {"から", "from"}, {"へ", "to"},  {"まで", "to"},
                                      {"とは", "with"}, {"とも", "with"}};
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

// The sentence particles and modals cut from the end of a form (行ったか, 行こうよ, 行くでしょう, 良いんじゃないかしら).
constexpr std::string_view kFinalParticles[] = {"か", "よ", "ね", "ぜ", "ぞ", "わ", "な", "の", "っけ", "かしら", "しら",
                                                "でしょう", "でしょ", "だろう", "だろ"};

// Drawn and clipped kana (kana.drawn-out, mark.clip), small katakana vowels and ellipses: tone, not words.
bool Faint(std::string_view rune) {
    return dict::InClass(HDTRANS_CLASS("kana.drawn-out"), rune) || dict::InClass(HDTRANS_CLASS("mark.clip"), rune) ||
           std::string_view("ァィゥェォー…‥").find(rune) != std::string_view::npos;
}

// Whether what stands between the stem and a negative is a stem's own ending: nothing, one kana (行か,
// 高く, 行き, 来), a て form (読んで, 書いてい) or a voice (書かれ, 読ませ, 食べられ), at most three kana.
bool NegatedStem(std::string_view pre) {
    const std::size_t runes = text::RuneCount(pre);
    if (runes <= 1) return true;
    if (runes > 3) return false;
    for (const std::string_view end : {"て", "で", "てい", "でい", "れ", "せ"})
        if (pre.ends_with(end)) return true;
    return false;
}

// An argument of a predicate as the cue atoms read it: its item, or for an omitted one (with an
// attachment) its lemma and class.
struct Arg {
    std::string_view slot;
    const Item* item = nullptr;
    std::string_view lemma, cls, how;
};

// What the cue atoms of a row read about one word (the context of the lexicon's cue DSL).
struct Context {
    std::vector<Arg> args;       // the arguments of the predicate
    const Item* head = nullptr;  // the noun a predicate stands right before (its adnominal head)
    bool headThroughAux = false;  // the head follows a て form auxiliary: it fills the subject only
    const Item* mod = nullptr;   // the word right before a noun that modifies it
    const Item* pred = nullptr;  // the word the word's phrase depends on
    const Item* next = nullptr;
    const Item* after = nullptr;  // the word before (the word is after it)
    std::set<std::string_view> forms;
    std::string final;  // the sentence particles and the mark that end the sentence after the word (final=)
    bool polite = false;
    std::string_view voice;  // voice rows: the reading of (ら)れ the chain decided
    // With an attachment: the word's own how as an argument, its clause and its main clause.
    bool attached = false;
    std::string_view how;
    const AttachClause* clause = nullptr;
    const AttachClause* main = nullptr;
};

// The attachment of the chain, asked once per pass; null without a provider or when it gives none.
const Flusher::Attached* AttachedOf(Flusher& f) {
    Flusher::Attached& a = f.attached;
    if (!a.asked) {
        a.asked = true;
        const AttachProvider* p = f.w.settings.attach;
        a.items.clear();
        a.attachment = {};
        for (const Item* it = f.w.chain.head; it != nullptr; it = it->next) a.items.push_back(it);
        a.held = p != nullptr && p->attach != nullptr && p->attach(p->context, f.w.tokens, f.w.chain, a.attachment) &&
                 a.attachment.items.size() == a.items.size();
    }
    return a.held ? &a : nullptr;
}

// The context from an attachment: the arguments, head, pred and mod the analysis gave the word.
void AttachedContext(const Flusher::Attached& a, const Item* it, Context& c) {
    const auto at = std::find(a.items.begin(), a.items.end(), it);
    if (at == a.items.end()) return;
    const std::size_t k = static_cast<std::size_t>(at - a.items.begin());
    const Attachment& att = a.attachment;
    const AttachItem& ai = att.items[k];
    auto item = [&](int i) -> const Item* {
        const Item* x = i >= 0 && static_cast<std::size_t>(i) < a.items.size() ? a.items[static_cast<std::size_t>(i)] : nullptr;
        return x != nullptr && x->group != nullptr ? x : nullptr;
    };
    auto clause = [&](int i) -> const AttachClause* {
        return i >= 0 && static_cast<std::size_t>(i) < att.clauses.size() ? &att.clauses[static_cast<std::size_t>(i)] : nullptr;
    };
    c.attached = true;
    c.args.clear();
    c.head = c.mod = c.pred = nullptr;
    c.clause = clause(ai.clause);
    c.main = c.clause;
    for (int guard = 0; guard < 64 && c.main != nullptr && c.main->parent >= 0; guard++) c.main = clause(c.main->parent);
    if (c.main != nullptr && c.main->parent >= 0) c.main = nullptr;  // a loop of parents: no main clause
    if (ai.argOf >= 0) {
        c.how = ai.how;
        c.pred = item(ai.headItem);
    }
    if (ai.modifies >= 0) {
        if (ai.modKind == 2) c.head = item(ai.modifies);
        if (c.pred == nullptr) c.pred = item(ai.modifies);
    }
    for (std::size_t j = 0; j < att.items.size(); j++)
        if (att.items[j].modifies == static_cast<int>(k) && j < k) c.mod = a.items[j];
    if (ai.pred && c.clause != nullptr)
        for (const AttachArg& g : c.clause->args) {
            const std::string_view role = g.role == "topic" ? std::string_view("subj") : std::string_view(g.role);
            c.args.push_back({role, item(g.item), g.lemma, g.cls, g.how});
        }
}

Context ContextOf(Flusher& f, const Item* it) {
    Context c;
    const char cls = ClassOf(it);
    c.next = it->After();
    c.after = it->prev;
    if (Predicate(cls)) {
        // a word of a noun, が or は and a verb read as one (雨が降る 비가 오다) holds its own subject
        if (const std::string_view w = it->group->surface; cls == '4') {
            for (const std::string_view p : {"が", "は"}) {
                const std::size_t at = w.find(p);
                if (at != std::string_view::npos && at > 0 && at + p.size() < w.size()) c.args.emplace_back("subj", it);
            }
        }
        // the case phrases before it in its clause (noun + particle), right to left
        for (const Item* q = it->prev; q != nullptr && q->group != nullptr; q = q->prev) {
            if (SentenceEnd(q) || q->group->surface == "「" || q->group->surface == "『" || Predicate(ClassOf(q))) break;
            if (ClassOf(q) == '2' && q->prev != nullptr && ClassOf(q->prev) == '1') {
                if (const std::string_view slot = ArgSlot(q->group->surface); !slot.empty())
                    c.args.push_back({slot, q->prev, {}, {}, "marked"});
                q = q->prev;
            }
        }
        // through the auxiliaries of a て form (伸びてくる指先, 伸びている指) the predicate still modifies the noun; a
        // quoting って after a dictionary or past form (引くっていうか, 見たって) is no て form
        const auto teForm = [](const Item* v) {
            const std::string_view ending = v->DirectSurface();
            if (!ending.ends_with("て") && !ending.ends_with("で")) return false;
            const std::string_view text = v->text;
            const auto from = static_cast<std::size_t>(std::max(v->start, 0));
            const std::string_view covered = from < text.size() ? text.substr(from, static_cast<std::size_t>(std::max(v->length, 0))) : std::string_view();
            return !covered.starts_with(v->group->surface) || covered.substr(v->group->surface.size()) == ending;
        };
        const Item* last = it;
        const Item* next = c.next;
        for (int k = 0; k < 3 && next != nullptr && next->group != nullptr && ClassOf(next) == '4' && teForm(last) &&
                        dict::InClass(HDTRANS_CLASS("aux.te-through"), next->group->surface);
             k++) {
            last = next;
            next = next->After();
        }
        if (next != nullptr && ClassOf(next) == '1' && !Faint(next->group->surface) &&
            last->DirectSurface().find("て") == std::string_view::npos) {
            c.head = next;
            c.pred = next;
            c.headThroughAux = last != it;
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
    // An ending that starts with the formal noun こと (向けられたことが) holds no form: the form is the
    // conjugated text between the stem and the ending (られた).
    if (Predicate(cls) && ending.starts_with("こと")) {
        const std::string_view whole = Slice(it->text, it->start, it->length);
        const std::string_view stem = it->group->surface;
        if (whole.starts_with(stem) && whole.ends_with(ending) && whole.size() >= stem.size() + ending.size()) {
            ending = std::string(whole.substr(stem.size(), whole.size() - stem.size() - ending.size()));
        }
    } else if (const Item* aux = NegativeAux(it)) {
        ending += aux->group->surface;
        ending += aux->DirectSurface();
    }
    const std::string_view after = c.next != nullptr ? std::string_view(c.next->group->surface) : std::string_view();
    // the conjugated form without the sentence particles after it (行ったか, 行こうよ)
    std::string_view form = ending;
    for (bool cut = true; cut;) {
        cut = false;
        for (const std::string_view p : kFinalParticles) {
            if (form.size() > p.size() && form.ends_with(p)) {
                form.remove_suffix(p.size());
                cut = true;
            }
        }
    }
    // final: the particles cut from the form, then the text after the word (and its negative auxiliary)
    // up to the first sentence mark (。！？、, kept) or a closing bracket or the end (not kept); drawn and
    // clipped kana and ellipses (Faint) are left out and blanks right before a mark skipped (行かないか？ か？,
    // 吐かんかッ!! か！, 返さないよ？ よ？, 黙らんかァ！！ か！, 早くしないか……！ か！, いいんじゃない ？ ？)
    std::string_view core = ending;  // the form without its particles, past drawn and clipped kana (行かないかっ)
    for (bool cut = true; cut;) {
        cut = false;
        if (!core.empty() && Faint(core.substr(core.size() - DecodeLastRune(core).size))) {
            core.remove_suffix(DecodeLastRune(core).size);
            cut = true;
        }
        for (const std::string_view p : kFinalParticles) {
            if (core.size() > p.size() && core.ends_with(p)) {
                core.remove_suffix(p.size());
                cut = true;
            }
        }
    }
    for (std::string_view rest = std::string_view(ending).substr(core.size()); !rest.empty();) {
        const std::size_t size = text::DecodeRune(rest).size;
        if (size == 0) break;
        if (!Faint(rest.substr(0, size))) c.final += rest.substr(0, size);
        rest.remove_prefix(size);
    }
    const Item* last = NegativeAux(it) != nullptr ? NegativeAux(it) : it;
    bool stop = false, blank = false;
    const auto take = [&](std::string_view rest) {
        while (!rest.empty() && !stop) {
            const std::size_t size = text::DecodeRune(rest).size;
            if (size == 0) break;
            std::string_view rune = rest.substr(0, size);
            rest.remove_prefix(size);
            if (rune == "!") rune = "！";
            if (rune == "?") rune = "？";
            if (rune == "｡") rune = "。";
            if (rune == "！" || rune == "？" || rune == "。" || rune == "、") {
                c.final += rune;
                stop = true;
            } else if (rune == " " || rune == "　") {
                blank = true;
            } else if (rune == "」" || rune == "』" || rune == "）" || rune == ")" || blank) {
                stop = true;
            } else if (!Faint(rune)) {
                c.final += rune;
            }
        }
    };
    // the rest of the word's token, then the tokens after it as typed
    take(From(last->text, static_cast<long>(last->start + last->length)));
    for (int j = last->tokenIndex + 1; !stop && j < f.w.TokenCount(); j++) take(f.w.Typed(j));
    // ta: the past auxiliary closes the form (行った, 読んだ, 高かった, 行ったら); not the た of a stem (待たない)
    // (the だ forms only on verbs: 静かだ is the copula)
    for (const std::string_view ta : {"た", "たら", "たり", "たろう", "だ", "だら", "だり"}) {
        if (form.ends_with(ta) && (ta.starts_with("た") || cls == '4')) c.forms.insert("ta");
    }
    // neg: the last negative auxiliary of the ending right after the stem (NegatedStem: 行か|ない, 高く|ない,
    // 行き|ません, 食べ|ない, 読んで|ない, 書か|れ|ない), not after a noun phrase, the copula, しか or っきゃ
    // (食べるものはないか, 行ったじゃないか, 行くしかないか: nja for the copula of んじゃない); not the ぬ of 死ぬ
    std::size_t lastNeg = std::string::npos;
    for (const std::string_view neg : {"ない", "なかっ", "なく", "なけれ", "ません", "ず", "ぬ"}) {
        const std::size_t at = ending.rfind(neg);
        if (at != std::string::npos && (lastNeg == std::string::npos || at > lastNeg)) lastNeg = at;
    }
    if (lastNeg != std::string::npos && (lastNeg > 0 || (it->DirectSurface().empty() && NegativeAux(it) != nullptr)) &&
        NegatedStem(std::string_view(ending).substr(0, lastNeg)))
        c.forms.insert("neg");
    // the old negative ん right after the stem or after its negative kana (吐|んか, 行かん, 待たんか, 勉強せんか, 来ん)
    if (form == "ん" || (form.ends_with("ん") && form.size() >= 6 &&
                         std::string_view("かがさたなばまらわせこ").find(form.substr(form.size() - 6, 3)) != std::string_view::npos)) {
        c.forms.insert("neg");
    }
    // nja: the non-past predicate negated through the の of んじゃない, のではない (いいんじゃない, 行くのではない);
    // a past before the の (よかったのではないか) asks about what happened and is not nja
    for (const std::string_view nja : {"んじゃな", "のじゃな", "のではな", "んではな"}) {
        const std::size_t at = ending.find(nja);
        if (at == std::string_view::npos) continue;
        const std::string_view before = std::string_view(ending).substr(0, at);
        if (!before.ends_with("た") && !before.ends_with("だ")) c.forms.insert("nja");
    }
    if (form.ends_with("て") || form.ends_with("で")) {
        c.forms.insert("te");
        if (after.starts_with("くださ") || after.starts_with("下さ") || after.starts_with("くれ")) c.forms.insert("te-request");
    }
    // vol: よう, ましょう, or う after an o-row kana (行こう), at the end of the form
    if (form.ends_with("よう") || form.ends_with("ましょう") ||
        (form.ends_with("う") && form.size() >= 6 &&
         std::string_view("おこごそぞとどのぼぽもろ").find(form.substr(form.size() - 6, 3)) != std::string_view::npos)) {
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
            e.find("ませ") != std::string_view::npos || e.find("です") != std::string_view::npos ||
            e.find("でしょう") != std::string_view::npos || s == "です" ||
            s == "でした" || s == "でしょう") {
            c.polite = true;
            break;
        }
    }
    if (const Flusher::Attached* a = AttachedOf(f)) AttachedContext(*a, it, c);
    return c;
}

// Whether class cls is v or below it.
bool ClassUnder(const dict::PackTable& t, std::string_view cls, std::string_view v) {
    for (int guard = 0; guard < 32 && !cls.empty(); guard++) {
        if (cls == v) return true;
        const auto p = t.parent.find(cls);
        if (p == t.parent.end()) return false;
        cls = p->second;
    }
    return false;
}

// The value of feature key of a clause, "" for none.
std::string_view Feature(const AttachClause* c, std::string_view key) {
    if (c == nullptr) return {};
    for (const auto& [k, v] : c->features)
        if (k == key) return v;
    return {};
}

// Whether the argument word x is of class cls by the pack-classes files; a plural of a noun of the class is of it
// (猫たち, 子供ら, 少年たち: noun.plural-suffix), whether the suffix is a word of its own or ends the word.
bool ArgMember(const dict::PackTable& t, const Item* x, std::string_view cls) {
    const std::string_view s = x->group->surface;
    if (t.Member(s, cls)) return true;
    if (dict::InClass(HDTRANS_CLASS("noun.plural-suffix"), s)) {
        const Item* head = x->prev;
        return head != nullptr && head->group != nullptr && head->tokenIndex == x->tokenIndex && t.Member(head->group->surface, cls);
    }
    const std::size_t n = dict::EndsWithMember(HDTRANS_CLASS("noun.plural-suffix"), s);
    return n > 0 && n < s.size() && t.Member(s.substr(0, s.size() - n), cls);
}

// Whether every atom of the row holds for the word, and its specificity: lexical atoms, class
// depth, neighbour and form atoms, register atoms, atom count.
bool Holds(const dict::PackTable& t, const dict::PackRow& r, const Context& c, std::array<int, 5>& spec) {
    spec = {0, 0, 0, 0, static_cast<int>(r.atoms.size())};
    for (const dict::PackAtom& a : r.atoms) {
        const bool none = a.values.size() == 1 && a.values[0] == "-";
        const bool filled = a.values.size() == 1 && a.values[0] == "*";
        std::vector<Arg> pool;
        bool nouns = true;  // the atom reads nouns: a value matches by lemma or class (LEMMA/CLASS kinds)
        static constexpr std::string_view kArgSlots[] = {"arg", "subj", "obj", "dat", "loc", "with", "from", "to"};
        const std::size_t dot = a.slot.find('.');
        const std::string_view base = std::string_view(a.slot).substr(0, dot);
        if (dot != std::string_view::npos && base != "cl" && base != "main") {
            // <slot>.how: how an argument of that slot is written (bare, omitted …), with an attachment
            if (!c.attached) return false;
            bool held = false;
            for (const Arg& g : c.args)
                if ((base == "arg" || g.slot == base) && std::find(a.values.begin(), a.values.end(), g.how) != a.values.end())
                    held = true;
            if (!held) return false;
            spec[2]++;
            continue;
        }
        if (base == "cl" || base == "main") {
            // cl.<feature>, main.<feature>: the word's clause or its main clause, with an attachment
            const std::string_view key = std::string_view(a.slot).substr(dot + 1);
            const std::string_view v = Feature(base == "cl" ? c.clause : c.main, key);
            if (v.empty() || std::find(a.values.begin(), a.values.end(), v) == a.values.end()) return false;
            spec[2]++;
            continue;
        }
        if (a.slot == "how") {
            if (!c.attached || c.how.empty() || std::find(a.values.begin(), a.values.end(), c.how) == a.values.end())
                return false;
            spec[2]++;
            continue;
        }
        if (std::find(std::begin(kArgSlots), std::end(kArgSlots), a.slot) != std::end(kArgSlots)) {
            for (const Arg& g : c.args)
                if (a.slot == "arg" || g.slot == a.slot) pool.push_back(g);
            // the noun an adnominal predicate modifies fills its empty subject or object
            // (an attachment says itself which argument the head is)
            if (pool.empty() && !c.attached && (a.slot == "subj" || (a.slot == "obj" && !c.headThroughAux)) && c.head != nullptr)
                pool.push_back({a.slot, c.head, {}, {}, {}});
        } else if (a.slot == "head") {
            if (c.head != nullptr) pool.push_back({a.slot, c.head, {}, {}, {}});
        } else if (a.slot == "mod" || a.slot == "pred" || a.slot == "next" || a.slot == "after") {
            nouns = false;
            const Item* x = a.slot == "mod" ? c.mod : a.slot == "pred" ? c.pred : a.slot == "next" ? c.next : c.after;
            if (x != nullptr && x->group != nullptr) pool.push_back({a.slot, x, {}, {}, {}});
        } else if (a.slot == "form") {
            if (std::none_of(a.values.begin(), a.values.end(), [&](const std::string& v) { return c.forms.count(v) > 0; }))
                return false;
            spec[2]++;
            continue;
        } else if (a.slot == "voice") {
            if (std::find(a.values.begin(), a.values.end(), c.voice) == a.values.end()) return false;
            spec[2]++;
            continue;
        } else if (a.slot == "final") {
            if (std::none_of(a.values.begin(), a.values.end(),
                             [&](const std::string& v) { return v == "-" ? c.final.empty() : v == c.final; }))
                return false;
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
            return false;  // domain: the chain has no such feature
        }
        if (none || filled) {
            if (pool.empty() != none) return false;
            spec[2]++;
            continue;
        }
        bool lexical = false, held = false;
        int depth = 0;
        for (const Arg& g : pool) {
            const Item* x = g.item;
            for (const std::string& v : a.values) {
                if (v.starts_with("@")) {
                    // by the pack-classes files, or by the class the attachment gives the argument
                    if ((x != nullptr && ArgMember(t, x, v)) || (!g.cls.empty() && ClassUnder(t, g.cls, v))) {
                        held = true;
                        depth = std::max(depth, t.Depth(v));
                    }
                } else if ((x != nullptr && IsWord(x, v, std::max(x->selected, 0))) || (x == nullptr && g.lemma == v)) {
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

// Whether a row of lemma * or lemma @class applies to the word, and how it names it (0 for *, 1 for @class).
bool OpenRowNames(const dict::PackRow& r, const Item* it, int& named) {
    named = 0;
    if (r.lemmaClass.empty()) return true;
    named = 1;
    const auto keyed = r.lemmaKeys.find(it->group->surface);
    if (keyed == r.lemmaKeys.end()) return false;
    for (const std::string& lemma : keyed->second)
        if (IsWord(it, lemma, std::max(it->selected, 0))) return true;
    return false;
}

// The row of slot that decides the word, or -1.
int BestRow(Flusher& f, const dict::PackTable& t, const Item* it, std::string_view key, dict::PackSlot slot,
            std::string_view voice = {}) {
    const auto keyed = t.byKey.find(key);
    const char cls = ClassOf(it);
    // the rows that may decide the word: those of its key (naming it, 2), then those of lemma @class (1)
    // and lemma * (0) of its class digit
    std::vector<std::pair<int, int>> rows;
    if (keyed != t.byKey.end())
        for (const int i : keyed->second) rows.emplace_back(i, 2);
    if (const auto open = t.open.find(dict::OpenIndex(slot, cls)); open != t.open.end()) {
        for (const int i : open->second) {
            int named = 0;
            if (OpenRowNames(t.rows[static_cast<std::size_t>(i)], it, named)) rows.emplace_back(i, named);
        }
    }
    bool any = false;
    for (const auto& [i, named] : rows) {
        const dict::PackRow& r = t.rows[static_cast<std::size_t>(i)];
        any = any || (r.slot == slot && r.classDigit == cls);
    }
    if (!any) return -1;
    Context c = ContextOf(f, it);
    // polarity and tense rows read the reading of (ら)れ the voice layer decided, and a potential of the dictionary
    c.voice = voice;
    if (voice.empty() && it->detail != nullptr) {
        const Voice v = it->detail->voice;
        c.voice = v == Voice::Passive       ? "passive"
                  : v == Voice::Potential   ? "potential"
                  : v == Voice::Respectful  ? "respectful"
                  : v == Voice::Spontaneous ? "spontaneous"
                  : it->detail->record == 4 ? "potential"
                  : it->detail->record == 1 ? "passive"
                                            : "none";
    }
    int best = -1;
    std::array<int, 6> bestSpec{};
    for (const auto& [i, named] : rows) {
        const dict::PackRow& r = t.rows[static_cast<std::size_t>(i)];
        std::array<int, 5> own{};
        if (r.slot != slot || r.classDigit != cls || !Holds(t, r, c, own)) continue;
        const std::array<int, 6> spec{named, own[0], own[1], own[2], own[3], own[4]};
        if (best < 0 || spec > bestSpec || (spec == bestSpec && r.pri > t.rows[static_cast<std::size_t>(best)].pri)) {
            best = i;
            bestSpec = spec;
        }
    }
    return best;
}

// The row of slot that decides the word, or null: a row of the document's name list
// (Settings::documentPacks) that holds is final for its document, as user rows are elsewhere;
// otherwise the snapshot's rows decide, unless snapshotRows is false.
const dict::PackRow* DecidingRow(Flusher& f, const Item* it, std::string_view key, dict::PackSlot slot,
                                 bool snapshotRows) {
    if (const dict::PackTable* list = f.w.settings.documentPacks; list != nullptr) {
        if (const int i = BestRow(f, *list, it, key, slot); i >= 0) return &list->rows[static_cast<std::size_t>(i)];
    }
    if (!snapshotRows) return nullptr;
    const dict::PackTable& t = f.w.snapshot.Packs();
    const int i = BestRow(f, t, it, key, slot);
    return i < 0 ? nullptr : &t.rows[static_cast<std::size_t>(i)];
}

// The text an item reads in its token: its surface and its conjugation ending.
std::string_view Segment(const Item* it) {
    const std::size_t start = static_cast<std::size_t>(std::max(it->start, 0));
    if (start >= it->text.size()) return it->group->surface;
    return it->text.substr(start, static_cast<std::size_t>(std::max(it->length, 0)));
}

// Whether a word b right after word a is written with a space before it: b goes on in a's token
// after a word, not after a mark (・, 、) or the start of a token (……), and not after a digit or a
// Latin letter, after which the source's spacing stays (NUM-4: 3肉じゃが 3고기감자조림).
bool SpacedAfter(const Item* a, const Item* b) {
    if (a->tokenIndex != b->tokenIndex || ClassOf(a) == '6') return false;
    char32_t r = DecodeLastRune(Segment(a)).cp;
    if (r >= U'０' && r <= U'ｚ') r -= 0xFEE0;
    return !((r >= U'0' && r <= U'9') || (r >= U'A' && r <= U'Z') || (r >= U'a' && r <= U'z'));
}

// Whether the words from items[i] on read text, text's last word in any conjugated form: the
// span ends with the item past the last one read (end).
bool ReadsText(const std::vector<Item*>& items, std::size_t i, std::string_view text, std::size_t& end) {
    while (!text.empty()) {
        if (i >= items.size()) return false;
        const Item* x = items[i];
        if (x->group == nullptr || x->selected < 0) return false;
        const std::string_view seg = Segment(x);
        // the last word: written as it is, or in a form of the dictionary form text ends with
        if (seg == text || x->group->surface == text || IsWord(x, text)) {
            end = i + 1;
            return true;
        }
        if (seg.empty() || !text.starts_with(seg)) return false;
        text.remove_prefix(seg.size());
        i++;
    }
    end = i;
    return true;
}

// Whether x names a person in the engine's own codes: a name, or a word with a person sense.
bool PersonWord(const Item* x) {
    const Entry* e = x->group->At(x->selected);
    if (e != nullptr && e->user != nullptr && (e->user->name || e->user->nameLayer)) return true;
    const dict::SemCodes sem(x->Attr(x->selected));
    for (const std::string_view code : {"QPN", "QNP", "VHn", "VQw", "VQm", "UAq", "UQf"})
        if (sem.HasCode(code)) return true;
    return false;
}

// Whether noun (the word x, or its noun part) is of class cls: by the pack-classes files, and for
// @person and the classes above it also by the engine's person codes.
bool OfClass(const dict::PackTable& t, const Item* x, std::string_view noun, std::string_view cls) {
    if (t.Member(noun, cls)) return true;
    // @person or a class above it (its parent chain reaches cls)
    bool covers = false;
    std::string_view c = "@person";
    for (int guard = 0; guard < 32 && !c.empty() && !covers; guard++) {
        covers = c == cls;
        const auto p = t.parent.find(c);
        if (p == t.parent.end()) break;
        c = p->second;
    }
    return covers && PersonWord(x);
}

// The honorific suffixes that stay with the noun of a slot (青海さん, オクタヴィア様).
bool SlotSuffix(const Item* x) {
    static constexpr std::string_view kSuffixes[] = {"さん", "様", "さま", "ちゃん", "くん", "君", "殿", "先生", "先輩", "氏"};
    return x->group != nullptr && x->selected >= 0 &&
           std::find(std::begin(kSuffixes), std::end(kSuffixes), x->group->surface) != std::end(kSuffixes);
}

// Whether x ends an utterance on its side: no word (the start or end of the text), a mark, or a word
// of another token (a space between).
bool UtteranceEdge(const Item* x, const Item* inside) {
    if (x == nullptr || x->group == nullptr) return true;
    if (x->tokenIndex != inside->tokenIndex) return true;
    const std::string_view s = x->group->surface;
    if (TrimSpace(s).empty()) return true;
    for (const std::string_view mark : {"。", "、", "！", "？", "!", "?", "…", "‥", "「", "」", "『", "』", "（", "）", "～", "ー", "，", ","}) {
        if (s == mark) return true;
    }
    return false;
}

// The last rune of t that is no space, or 0.
char32_t LastVisibleRune(std::string_view t) {
    char32_t found = 0;
    for (std::size_t q = 0; q < t.size();) {
        const text::Rune r = text::DecodeRune(t.substr(q));
        q += std::max<std::size_t>(r.size, 1);
        if (r.cp != U' ' && r.cp != U'\t' && r.cp != U'\u3000') found = r.cp;
    }
    return found;
}

// Whether the words from items[i] write text exactly and make the whole utterance (^text$): [i, end).
bool Utterance(const Worker& w, const std::vector<Item*>& items, std::size_t i, std::string_view text, std::size_t& end) {
    std::size_t k = i;
    std::string_view rest = text;
    while (!rest.empty()) {
        if (k >= items.size() || items[k]->group == nullptr || items[k]->selected < 0) return false;
        if (k > i && items[k]->tokenIndex != items[i]->tokenIndex) return false;
        const std::string_view seg = Segment(items[k]);
        if (seg.empty() || !rest.starts_with(seg)) return false;
        rest.remove_prefix(seg.size());
        k++;
    }
    if (k == i) return false;
    if (!UtteranceEdge(i > 0 ? items[i - 1] : nullptr, items[i])) return false;
    // a closing bracket before is the end of a quote the utterance goes on from (『物』なんだって, 」だからね)
    if (i > 0 && items[i - 1]->group != nullptr) {
        const std::string_view ps = items[i - 1]->group->surface;
        if (ps == "」" || ps == "』" || ps == "）" || ps == ")") return false;
        // an ellipsis after a word of the sentence is a pause inside it (それだけ…だからね)
        if ((ps == "…" || ps == "‥") && i > 1 && items[i - 2]->group != nullptr && items[i - 2]->tokenIndex == items[i - 1]->tokenIndex &&
            !UtteranceEdge(items[i - 2], items[i - 2])) {
            return false;
        }
    }
    // nor right after a number or a symbol (100%だからね)
    if (i > 0 && items[i - 1]->group != nullptr && !TrimSpace(items[i - 1]->group->surface).empty() &&
        !UtteranceEdge(items[i - 1], items[i - 1])) {
        const char32_t last = DecodeLastRune(items[i - 1]->group->surface).cp;
        const bool symbol = ClassOf(items[i - 1]) == '6' || (last >= U'0' && last <= U'9') || (last >= U'０' && last <= U'９') ||
                            last == U'％' || last == U'%' || (last >= U'A' && last <= U'z') || (last >= U'Ａ' && last <= U'ｚ');
        if (symbol) return false;
    }
    if (!UtteranceEdge(k < items.size() ? items[k] : nullptr, items[k - 1])) return false;
    // After it the text ends or a mark follows; a space may only begin the next clause of a sentence
    // (こっちだって もう１人), so the utterance must close there.
    if (k < items.size() && items[k]->group != nullptr && items[k]->tokenIndex != items[k - 1]->tokenIndex &&
        !UtteranceEdge(items[k], items[k])) {
        return false;
    }
    // nothing else of the token before or after the span: a word the chain does not hold (an unknown run)
    // would stand beside it
    const Item* first = items[i];
    const Item* last = items[k - 1];
    const std::string_view before = std::string_view(first->text).substr(0, static_cast<std::size_t>(std::max(first->start, 0)));
    const std::size_t stop = static_cast<std::size_t>(std::max(last->start, 0) + std::max(last->length, 0));
    const std::string_view after = stop < last->text.size() ? std::string_view(last->text).substr(stop) : std::string_view();
    auto marks = [](std::string_view t) {
        static constexpr std::u32string_view kMarks = U"。、！？!?…‥「」『』（）()～ー　 ,.・，";
        for (std::size_t q = 0; q < t.size();) {
            const text::Rune r = text::DecodeRune(t.substr(q));
            if (kMarks.find(r.cp) == std::u32string_view::npos) return false;
            q += std::max<std::size_t>(r.size, 1);
        }
        return true;
    };
    const std::size_t from = static_cast<std::size_t>(std::max(first->start, 0));
    if (i > 0 && items[i - 1]->tokenIndex == first->tokenIndex) {
        const Item* p = items[i - 1];
        if (static_cast<std::size_t>(std::max(p->start, 0) + std::max(p->length, 0)) != from) return false;
    } else if (!marks(before)) {
        return false;
    }
    if (k < items.size() && items[k]->tokenIndex == last->tokenIndex && !TrimSpace(items[k]->group->surface).empty()) {
        if (static_cast<std::size_t>(std::max(items[k]->start, 0)) != stop) return false;
    } else if (!marks(after)) {
        return false;
    }
    // an ellipsis token after a word of the sentence is a pause inside it (それだけ…だからね)
    if (const int t = first->tokenIndex - 1; t >= 1 && t < w.TokenCount() && (w.Tok(t).text.starts_with("…") || w.Tok(t).text.starts_with("‥"))) {
        const tokenizer::TokenView prev = w.Tok(t - 1);
        if (prev.type == tokenizer::kTypeJapanese && !prev.text.ends_with("、") && !prev.text.ends_with("。")) return false;
    }
    // a row that may go on from the word before it (the copula of だからね, the じゃん of ジャン, the って of
    // てことで, なんだって) does not start after a word, with or without a space (卒業 だからね, 卒業だからね)
    bool attaches = false;
    for (const std::string_view a : {"だ", "で", "じゃ", "ジャ", "て", "な"}) attaches = attaches || text.starts_with(a);
    if (attaches) {
        char32_t open = LastVisibleRune(before);
        for (int t = first->tokenIndex - 1; open == 0 && t >= 0; t--) open = LastVisibleRune(w.Tok(t).text);
        if (open != 0 && std::u32string_view(U"。．.！？!?…‥「『（(“《≪【［[、，,").find(open) == std::u32string_view::npos) return false;
    }
    end = k;
    return true;
}

// The span of colloc c that starts at items[i]: [i, end), the slot's words [slot, slotEnd) (none
// without a slot); absorbed: the slot's last word holds the start of the text after the slot.
bool MatchColloc(const Worker& w, const dict::PackTable& t, const dict::Colloc& c, std::string_view before,
                 const std::vector<Item*>& items, std::size_t i, std::size_t& end, std::size_t& slot,
                 std::size_t& slotEnd, bool& absorbed) {
    slot = slotEnd = static_cast<std::size_t>(-1);
    absorbed = false;
    if (!c.slot && c.anchored) return Utterance(w, items, i, before, end);
    if (!c.slot) return ReadsText(items, i, before, end) && end > i;
    std::size_t at = i;
    if (!before.empty() && !ReadsText(items, i, before, at)) return false;
    // ReadsText ends a span on a conjugated word; before the slot the text must end on a word boundary
    if (!before.empty()) {
        std::size_t len = 0;
        for (std::size_t k = i; k < at; k++) len += Segment(items[k]).size();
        if (len != before.size()) return false;
    }
    if (at >= items.size()) return false;
    const Item* x = items[at];
    if (x->group == nullptr || x->selected < 0) return false;
    slot = at;
    std::size_t last = at;
    while (last + 1 < items.size() && SlotSuffix(items[last + 1])) last++;
    slotEnd = last + 1;
    // The text after the slot read from the word after it; else the slot's last word may hold its
    // start (最後の of {X}の一体), and that part is read. The text is tried before the class, the
    // dearer test.
    if (ClassOf(x) == '1' && ReadsText(items, slotEnd, c.after, end) && end > slotEnd &&
        (c.slotClass.empty() || OfClass(t, x, x->group->surface, c.slotClass)))
        return true;
    std::string_view after = c.after;
    const std::string_view seg = Segment(items[last]);
    for (std::size_t q = std::min(after.size(), seg.size() > 0 ? seg.size() - 1 : 0); q > 0; q--) {
        if (q < after.size() && (static_cast<unsigned char>(after[q]) & 0xC0) == 0x80) continue;  // inside a letter
        if (seg.ends_with(after.substr(0, q))) {
            after.remove_prefix(q);
            absorbed = true;
            break;
        }
    }
    // The slot is a noun, or a word that holds a noun with the text after it (最後の).
    if (ClassOf(x) != '1' && !(absorbed && last == at)) return false;
    if (after.empty()) end = slotEnd;
    else if (!ReadsText(items, slotEnd, after, end) || end <= slotEnd) return false;
    if (!c.slotClass.empty()) {
        std::string_view noun = x->group->surface;
        const std::string_view read = std::string_view(c.after).substr(0, c.after.size() - after.size());
        if (absorbed && last == at && noun.size() > read.size() && noun.ends_with(read)) noun.remove_suffix(read.size());
        if (!OfClass(t, x, noun, c.slotClass)) return false;
    }
    return true;
}

// Writes target over word x (its ending emptied with an empty target).
void Write(Item* x, std::string_view target, Selection source) {
    if (x->selected < 0) return;
    TakeTarget(x, ClassOf(x), target, source);
    if (target.empty() && x->DirectIndex() >= 0) x->AssignDirectSlot(x->DirectIndex(), "", source);
}

// The colloc rows, left to right: at each word the longest pattern that reads the words from it
// (then pri, then load order). The words of a span take no other pack row.
// The particle notation (을)를 … for a word whose Korean ends in text: the form its last syllable takes.
std::string ResolvedParticle(std::string_view text, std::string_view notation) {
    if (!notation.starts_with("(")) return std::string(notation);
    const std::size_t close = notation.find(')');
    if (close == std::string_view::npos) return std::string(notation);
    const std::string_view withFinal = notation.substr(1, close - 1), plain = notation.substr(close + 1);
    char32_t last = 0;
    for (const char32_t ch : userdict::internal::Runes(text))
        if (ch >= 0xAC00 && ch <= 0xD7A3) last = ch;
    if (last == 0) return std::string(plain);
    const int final = static_cast<int>((last - 0xAC00) % 28);
    // (으)로 takes 로 after ㄹ (final 8) as after a vowel
    if (final == 0 || (notation == "(으)로" && final == 8)) return std::string(plain);
    if (notation == "(으)로") return "으로";
    return std::string(withFinal);
}

// The Korean of an idiom with the particle of its first word said as the topic or the focus: 은/는
// for は, 도 for も, after the syllable before it (꽃이 피다 꽃은 피다, 신경을 쓰다 신경도 쓰다).
std::string IdiomTopic(std::string_view text, std::string_view seen, std::string_view own) {
    std::size_t lead = 0;
    while (lead < text.size() && text[lead] == ' ') lead++;
    if (lead > 0) return std::string(text.substr(0, lead)) + IdiomTopic(text.substr(lead), seen, own);
    const std::size_t space = text.find(' ');
    if (space == std::string_view::npos) return std::string(text);
    const std::string_view word = text.substr(0, space);
    if (own == "に") {
        if (!word.ends_with("에") || word.size() == std::string_view("에").size()) return std::string(text);
        return std::string(word) + (seen == "も" ? "도" : "는") + std::string(text.substr(space));
    }
    if (own != "を" && own != "が") return std::string(text);
    for (const std::string_view p : {"을", "를", "이", "가"}) {
        if (!word.ends_with(p) || word.size() == p.size()) continue;
        const std::string_view stem = word.substr(0, word.size() - p.size());
        std::string out(stem);
        if (seen == "も") out += "도";
        else out += ResolvedParticle(stem, "(은)는");
        out += text.substr(space);
        return out;
    }
    return std::string(text);
}

// One match of a colloc row: its span [begin, end), the slot's words [slot, slotEnd), and the
// Korean its head takes.
struct CollocMatch {
    const dict::Colloc* c = nullptr;
    std::size_t end = 0, slot = 0, slotEnd = 0, length = 0;
    bool absorbed = false;
    std::string_view head;  // compiled for the head's class, or raw
    bool conjugates = false;
    std::vector<std::size_t> kept;  // words inside the span that keep their own Korean (an idiom's adverbs)
    std::string_view seen;          // the particle an idiom's noun took in the text, when not its own
};

// Whether the particle seen after an idiom's noun is the idiom's own.
bool IdiomParticle(std::string_view written, std::string_view seen) {
    // は and も said in place of the particle often make another phrase or another idiom (虫も殺さない,
    // 足はついてる, 人は選ばない): only the particle itself counts.
    return seen == written;
}

// Whether x writes nothing of its own in the source: a space.
bool BlankItem(const Item* x) {
    const std::string_view s = TrimSpace(x->group->surface);
    return s.empty() || s == "　";
}

// The span of idiom c from items[i]: its noun, then its particle, one that stands for it or none (を,
// が, は only), then up to two spaces or adverbs, which keep their Korean, then its verb in any form
// (気は遣ってくれる, 気 使わなくていい, 手挙げて, 鼻を 明かして).
bool MatchIdiom(const Worker& w, const dict::Colloc& c, const std::vector<Item*>& items, std::size_t i, CollocMatch& m) {
    const Item* noun = items[i];
    if (noun->group == nullptr || noun->selected < 0 || ClassOf(noun) != '1' || Segment(noun) != c.noun) return false;
    std::size_t j = i + 1;
    if (j >= items.size()) return false;
    bool omitted = false;
    m.seen = {};
    if (ClassOf(items[j]) == '2' && IdiomParticle(c.idiomParticle, items[j]->group->surface)) {
        if (items[j]->group->surface != c.idiomParticle) {
            m.seen = items[j]->group->surface;
            // the Korean must say the topic or the focus on its own noun (기분은, 비도); else the idiom
            // reads past what the particle adds (気にもなる, 話はなんとなくわかった)
            if (IdiomTopic(c.headRaw, m.seen.substr(m.seen.size() - std::string_view("は").size()), c.idiomParticle) == c.headRaw) return false;
        }
        j++;
    } else if (c.idiomParticle == "を" || c.idiomParticle == "が") {
        omitted = true;
    } else {
        return false;
    }
    m.kept.clear();
    bool spaced = false;
    for (int skipped = 0; j < items.size() && skipped < 2; skipped++) {
        const Item* x = items[j];
        if (x->group == nullptr || x->selected < 0) return false;
        if (BlankItem(x)) {
            spaced = true;
            j++;
            continue;
        }
        // an adverb in letters (すごく, とても); not a mark (…) or a word of another class
        const char32_t first = text::DecodeRune(x->group->surface).cp;
        const bool letters = (first >= 0x3041 && first <= 0x30FF) || (first >= 0x4E00 && first <= 0x9FFF);
        if (omitted || ClassOf(x) != '3' || !letters || c.idiomParticle == "に" || (c.idiomParticle == "を" && !m.seen.empty())) break;
        m.kept.push_back(j);
        j++;
    }
    if (j >= items.size()) return false;
    const Item* verb = items[j];
    if (verb->group == nullptr || verb->selected < 0 || ClassOf(verb) != '4' || !IsWord(verb, c.verb)) return false;
    // The verb itself, not another of its stem (元気を出す, not 出る) nor its passive or causative (先越され).
    {
        const char kind = At(verb->Code(verb->selected), 2);
        const std::string_view verbText = c.verb;
        const std::string_view end = verbText.substr(verbText.size() - DecodeLastRune(verbText).size);
        static constexpr std::pair<std::string_view, std::string_view> kKinds[] = {
            {"う", "0"}, {"く", "1"}, {"ぐ", "2"}, {"す", "3"}, {"つ", "4"}, {"む", "5"}, {"ぶ", "6"}, {"る", "78"}, {"ぬ", "<"}};
        for (const auto& [e, k] : kKinds) {
            if (end == e && k.find(kind) == std::string_view::npos && !c.verb.ends_with("する")) return false;
        }
        if (verb->detail != nullptr && (verb->detail->record != 0 || verb->detail->voice != Voice::None)) return false;
    }
    // Between the words only spaces: a mark (…, 、) or other text in a token of its own breaks the idiom.
    for (int t = noun->tokenIndex + 1; t < verb->tokenIndex; t++) {
        if (t < 0 || t >= w.TokenCount()) return false;
        const int type = w.Tok(t).type;
        if (type != tokenizer::kTypeAsciiSpace && type != tokenizer::kTypeWideSpace && type != tokenizer::kTypeJapanese) return false;
        if (type == tokenizer::kTypeAsciiSpace || type == tokenizer::kTypeWideSpace) spaced = true;
        if (type == tokenizer::kTypeJapanese) {
            bool has = false;
            for (std::size_t k = i; k <= j; k++) has = has || items[k]->tokenIndex == t;
            if (!has) return false;
        }
    }
    // Words in tokens of their own stand apart by a space the tokenizer drops (目に 遭わない).
    if (verb->tokenIndex != items[j - 1]->tokenIndex) spaced = true;
    // A particle left out only before a verb in kanji, right after the noun or after a space (手挙げて, 気 使わ;
    // not the compound 手こぎ).
    if (omitted) {
        const char32_t v = text::DecodeRune(verb->group->surface).cp;
        if (!(v >= 0x4E00 && v <= 0x9FFF)) return false;
        if (!spaced && verb->tokenIndex == noun->tokenIndex && j != i + 1) return false;
    }
    // The words as the dictionary writes them, side by side, are the word stage's to read (蓋を開ける
    // 뚜껑을 열다 where it chose so); the idiom row reads only what that stage could not see as one.
    if (m.seen.empty() && !omitted && !spaced && m.kept.empty()) return false;
    // A noun pointed at or owned (そんな手に, 女房の鼻を) belongs to the phrase before it, not to the idiom.
    if (i > 0) {
        const std::string_view prev = items[i - 1]->group->surface;
        const bool together = items[i - 1]->tokenIndex == noun->tokenIndex;
        for (const std::string_view mod : {"の", "そんな", "こんな", "あんな", "その", "この", "あの", "どんな"}) {
            if (prev == mod && together) return false;
        }
        // Nor a noun with a word that describes it or a noun it ends (大いなる力を 借りる, 欲しい物 言う,
        // 伯爵家継ぐ): the idiom's noun stands bare.
        const char pc = ClassOf(items[i - 1]);
        const Entry* pe = items[i - 1]->group->At(items[i - 1]->selected);
        // An idiom whose noun takes a describing word takes an adjective, a verb or an adnominal before it
        // (悲しい目に 遭う, 重い腰を 上げる); a noun before it still makes a compound.
        const bool describes = pc == '0' || pc == '4' || pc == '7' || pc == '8';
        if (together && ((describes && !c.modified) || pc == '1' || (pe != nullptr && pe->cls == 'N' && !(c.modified && describes)))) return false;
    }
    // A noun said again right after the verb restates it with its particle (足 出せ 足を): the words are
    // taken literally.
    // An order with the particle left out is said of the thing itself (足 出せ, 手 挙げろ), not the idiom.
    if (omitted && verb->detail != nullptr && (verb->detail->kind == 11 || verb->detail->kind == 12)) return false;
    m.end = j + 1;
    return true;
}

// Whether the match can be written: no user word in it but in the slot, no word another span took,
// a head outside the slot, and a predicate head only with a phrase of its class.
bool Applicable(Flusher& f, const dict::PackTable& t, const std::vector<Item*>& items, std::size_t i,
                const std::set<const Item*>& taken, CollocMatch& m) {
    const dict::Colloc& c = *m.c;
    // the row's cue atoms hold for the last word of the span (head=- : it modifies no noun)
    if (const dict::PackRow& r = t.rows[static_cast<std::size_t>(c.row)]; !r.atoms.empty()) {
        std::array<int, 5> spec{};
        if (!Holds(t, r, ContextOf(f, items[m.end - 1]), spec)) return false;
    }
    for (std::size_t k = i; k < m.end; k++) {
        const bool inSlot = c.slot && k >= m.slot && k < m.slotEnd;
        // an utterance row reads a fixed word too (何で of 何でだよ): the whole utterance is its reading
        if (!inSlot && ((items[k]->Fixed() && !c.anchored) || taken.contains(items[k]))) return false;
    }
    if (c.slot && m.end == m.slotEnd) return false;
    const char cls = ClassOf(items[m.end - 1]);
    const std::string& compiled = cls == '4' ? c.headVerb : cls == '7' ? c.headAdjective : cls == '8' ? c.headNa : c.headRaw;
    // an utterance is written as it is, its last word's ending dropped
    if (c.anchored) {
        m.conjugates = false;
        m.head = c.headRaw;
        return true;
    }
    m.conjugates = !compiled.empty() && Predicate(cls);
    m.head = compiled.empty() ? std::string_view(c.headRaw) : std::string_view(compiled);
    return m.conjugates || !Predicate(cls);
}

// The colloc rows, left to right: at each word the longest applicable pattern that reads the words
// from it (then pri, then load order). The words of a span take no other pack row.
// heads: the last words that keep their ending, whose ending slots (polarity, tense) stay open.
void ApplyCollocs(Flusher& f, const dict::PackTable& t, std::set<const Item*>& taken, std::set<const Item*>& heads) {
    std::vector<Item*> items;
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next)
        if (it->group != nullptr) items.push_back(it);
    std::vector<int> patterns;
    for (std::size_t i = 0; i < items.size(); i++) {
        // The patterns that can read from items[i]: text before a slot starts with its surface or
        // with what it reads in its token (ReadsText), so with the first letter of either; a slot
        // first may be any word. In load order, as ties go to the first.
        patterns.assign(t.collocsSlotFirst.begin(), t.collocsSlotFirst.end());
        const auto lead = [&](std::string_view letter) {
            if (letter.empty()) return;
            const auto found = t.collocsByLead.find(letter);
            if (found == t.collocsByLead.end()) return;
            const std::size_t mid = patterns.size();
            patterns.insert(patterns.end(), found->second.begin(), found->second.end());
            std::inplace_merge(patterns.begin(), patterns.begin() + static_cast<std::ptrdiff_t>(mid), patterns.end());
        };
        const std::string_view seg = Segment(items[i]), surface = items[i]->group->surface;
        const std::string_view segLetter = seg.substr(0, text::DecodeRune(seg).size);
        const std::string_view surfaceLetter = surface.substr(0, text::DecodeRune(surface).size);
        lead(segLetter);
        if (surfaceLetter != segLetter) lead(surfaceLetter);
        CollocMatch best;
        for (const int p : patterns) {
            const auto [colloc, b] = t.collocPatterns[static_cast<std::size_t>(p)];
            const dict::Colloc& c = t.collocs[static_cast<std::size_t>(colloc)];
            const std::string& before = c.befores[static_cast<std::size_t>(b)];
            CollocMatch m;
            m.c = &c;
            if (c.loose ? !MatchIdiom(f.w, c, items, i, m) : !MatchColloc(f.w, t, c, before, items, i, m.end, m.slot, m.slotEnd, m.absorbed)) continue;
            if (!Applicable(f, t, items, i, taken, m)) continue;
            m.length = before.size() + c.after.size();
            const int pri = t.rows[static_cast<std::size_t>(c.row)].pri;
            if (best.c == nullptr || m.length > best.length ||
                (m.length == best.length && pri > t.rows[static_cast<std::size_t>(best.c->row)].pri))
                best = m;
        }
        if (best.c == nullptr) continue;
        const dict::Colloc& c = *best.c;
        const dict::PackRow& r = t.rows[static_cast<std::size_t>(c.row)];
        const Selection source{Provenance::Pack, r.global};
        Item* head = items[best.end - 1];
        // the particle after the slot is left out when the slot's word already reads it (俺の 내)
        const bool particleLeft = c.slot && !c.particle.empty() && !best.absorbed && best.slotEnd < best.end &&
                                  items[best.slotEnd]->EntryText(items[best.slotEnd]->selected).empty();
        if (c.anchored) {
            // an utterance: its first word that conjugates nothing takes the target and the others write
            // nothing; with none, the target follows the last word
            std::size_t carrier = best.end;
            for (std::size_t k = i; k < best.end && carrier == best.end; k++)
                if (!Predicate(ClassOf(items[k]))) carrier = k;
            const bool spaced = i > 0 && items[i - 1]->tokenIndex != items[i]->tokenIndex &&
                                !UtteranceEdge(items[i - 1], items[i - 1]);
            const std::string carried = spaced ? " " + c.headRaw : c.headRaw;
            for (std::size_t k = i; k < best.end; k++) {
                Write(items[k], k == carrier ? std::string_view(carried) : std::string_view(), source);
                if (items[k]->DirectIndex() >= 0) items[k]->AssignDirectSlot(items[k]->DirectIndex(), "", source);
                taken.insert(items[k]);
            }
            if (carrier == best.end) {
                // written after its emptied words, the target takes the space a comma before it leaves
                const bool comma = i > 0 && (items[i - 1]->group->surface == "、" || items[i - 1]->group->surface == "，");
                head->appended += comma ? " " + c.headRaw : carried;
            }
            head->packRows.push_back(r.global);
            i = best.end - 1;
            continue;
        }
        for (std::size_t k = i; k < best.end; k++) {
            Item* x = items[k];
            if (c.slot && k >= best.slot && k < best.slotEnd) continue;
            if (std::find(best.kept.begin(), best.kept.end(), k) != best.kept.end()) continue;
            const bool afterSlot = c.slot && k == best.slotEnd && !best.absorbed;
            if (x == head) {
                std::string text(best.head);
                if (afterSlot && !c.particle.empty()) {
                    // the head right after the slot: the slot's particle is written after the slot's word
                    Item* own = items[best.slotEnd - 1];
                    own->appended += ResolvedParticle(own->EntryText(own->selected), c.particle);
                } else if (c.slot && ((c.spaced && c.particle.empty()) || particleLeft) && k >= best.slotEnd) {
                    // the space the target puts after the slot's Korean
                    text.insert(0, " ");
                }
                // the space before the span, when its first words are left empty and the head is written joined
                if (!c.slot && k > i && i > 0 && c.lead.empty() && SpacedAfter(items[i - 1], items[i])) text.insert(0, " ");
                // an idiom whose noun took は or も in the text keeps it on the Korean noun (花は咲く 꽃은 피다)
                // an idiom whose noun starts a token of its own keeps the space before it (카케루 힘내)
                if (c.loose && k > i && i > 0 && items[i - 1]->tokenIndex != items[i]->tokenIndex && !text.starts_with(" ")) text.insert(0, " ");
                if (c.loose && !best.seen.empty()) text = IdiomTopic(text, best.seen.substr(best.seen.size() - std::string_view("は").size()), c.idiomParticle);
                // the adverbs inside an idiom go after its first word (写真をいっぱい撮った 사진을 많이 찍었다)
                if (c.loose && !best.kept.empty()) {
                    std::string adverbs;
                    for (const std::size_t a : best.kept) {
                        const std::string_view ko = TrimSpace(items[a]->EntryText(items[a]->selected));
                        if (ko.empty() || ko.find('@') != std::string_view::npos) continue;
                        adverbs += " ";
                        adverbs += ko;
                    }
                    std::size_t at = 0;
                    while (at < text.size() && text[at] == ' ') at++;
                    const std::size_t space = text.find(' ', at);
                    if (!adverbs.empty() && space != std::string::npos) {
                        text.insert(space, adverbs);
                        for (const std::size_t a : best.kept) Write(items[a], "", source);
                    }
                }
                Write(x, text, source);
                // a phrase that does not conjugate keeps none of the word's ending
                if (!best.conjugates && x->DirectIndex() >= 0) x->AssignDirectSlot(x->DirectIndex(), "", source);
            } else if (afterSlot) {
                // a particle the slot's word already reads (俺の 내: の left empty) stays empty
                Write(x, particleLeft ? std::string_view() : std::string_view(c.particle), source);
            } else if (k == i && !c.lead.empty()) {
                Write(x, c.lead, source);
            } else {
                Write(x, "", source);
            }
            taken.insert(x);
        }
        head->packRows.push_back(r.global);
        if (best.conjugates) heads.insert(head);
        i = best.end - 1;
    }
}

}  // namespace

const dict::PackRow* PackVoice(Flusher& f, Item* it, std::string_view stem, Voice& v, bool atLast) {
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
    if (atLast && r.target.starts_with("passive")) return nullptr;
    it->packRows.push_back(r.global);
    if (r.target == "-") return nullptr;
    v = r.target == "passive"       ? Voice::Passive
        : r.target == "potential"   ? Voice::Potential
        : r.target == "respectful"  ? Voice::Respectful
        : r.target == "spontaneous" ? Voice::Spontaneous
                                    : Voice::None;
    return &r;
}

void ApplyPacks(Flusher& f) {
    const dict::PackTable& t = f.w.snapshot.Packs();
    if (t.Empty() && f.w.settings.documentPacks == nullptr) return;
    f.attached.asked = false;  // the chain may have changed since the voice layer asked
    std::set<const Item*> taken, heads;
    if (!t.collocs.empty()) ApplyCollocs(f, t, taken, heads);
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        // the head of a colloc span keeps its ending, and the ending's rows (polarity, tense) still apply
        const bool head = heads.contains(it);
        if (it->group == nullptr || it->selected < 0 || it->selected >= static_cast<int>(it->group->entries.size()) ||
            (taken.contains(it) && !head))
            continue;
        // A fixed word (a user's word, a span the sound stage wrote) keeps its decisions; a name of the
        // name layer takes name rows, and a word of a plain entry the document's name rows (the name
        // list stands in for the plain rows of its names in its document).
        const Entry* chosen = it->group->At(it->selected);
        const bool nameOnly = it->Fixed();
        const bool nameLayer = chosen->user != nullptr && chosen->user->nameLayer;
        if (nameOnly && !nameLayer && (chosen->user == nullptr || f.w.settings.documentPacks == nullptr)) continue;
        const char cls = ClassOf(it);
        // A voice row's agent particle and Korean verb hold against the passes after the voice layer.
        if (Item* p = it->packAgent; !head && p != nullptr && p->selected >= 0 &&
                                     p->selected < static_cast<int>(p->group->entries.size()) && !p->Fixed() &&
                                     !taken.contains(p) && !it->packRows.empty()) {
            TakeTarget(p, ClassOf(p), it->packAgentText, {Provenance::Pack, it->packRows.front()});
            taken.insert(p);
        }
        if (!head && !it->packLexeme.empty() && cls == '4') {
            it->detail->record = 0;
            it->lexical = true;
            const Selection source{Provenance::Pack, it->packRows.front()};
            it->AssignEntrySurface(it->selected, it->packLexeme, source);
            it->choice = source;
        }
        for (const dict::PackSlot slot :
             {dict::PackSlot::Sense, dict::PackSlot::Name, dict::PackSlot::Polarity, dict::PackSlot::Tense}) {
            if (nameOnly && slot != dict::PackSlot::Name) continue;
            if (head && (slot == dict::PackSlot::Sense || slot == dict::PackSlot::Name)) continue;
            // The Korean verb of a voice row is the word's sense in that voice (紅茶を振る舞われた 대접받았다):
            // a sense row, which writes the verb in the active, does not replace it.
            if (slot == dict::PackSlot::Sense && !it->packLexeme.empty() && cls == '4') continue;
            const dict::PackRow* best = DecidingRow(f, it, it->group->surface, slot, !nameOnly || nameLayer);
            if (best == nullptr) continue;
            const dict::PackRow& r = *best;
            const Selection source{Provenance::Pack, r.global};
            if (r.target == "-") {
                it->packRows.push_back(r.global);
                if (slot == dict::PackSlot::Polarity) break;  // a kept polarity holds the ending against tense rows
                continue;
            }
            if (nameOnly) {
                it->AssignEntrySurface(it->selected, r.target, source);
                it->choice = source;
            } else if (slot == dict::PackSlot::Sense || slot == dict::PackSlot::Name) {
                if (it->group->At(it->selected)->neighbourAssigned) continue;
                TakeTarget(it, cls, r.target, source);
                it->adjectival = r.adjective;
            } else {
                // Polarity and tense rows both write the ending; the first that applies holds it.
                if (it->DirectIndex() < 0 || it->directChoice.layer == Provenance::Pack ||
                    it->DirectEntry(it->DirectIndex())->neighbourAssigned)
                    continue;
                // A polarity row replaces a plain negative auxiliary (않다) only: one read 없다 or 못하다 carries a
                // potential or an inability the row would lose (信じられないか 믿을 수 없는가).
                if (const Item* aux = NegativeAux(it); slot == dict::PackSlot::Polarity && aux != nullptr) {
                    const std::string_view d = aux->group->At(aux->selected)->dst;
                    if (d != "$DH@" && d != "$D7@") continue;
                }
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
