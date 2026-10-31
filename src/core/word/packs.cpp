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

// What the cue atoms of a row read about one word (the context of the lexicon's cue DSL).
struct Context {
    std::vector<std::pair<std::string_view, const Item*>> args;  // slot, noun of a case phrase of the predicate
    const Item* head = nullptr;  // the noun a predicate stands right before (its adnominal head)
    const Item* mod = nullptr;   // the word right before a noun that modifies it
    const Item* pred = nullptr;  // the word the word's phrase depends on
    const Item* next = nullptr;
    const Item* after = nullptr;  // the word before (the word is after it)
    std::set<std::string_view> forms;
    std::string final;  // the sentence particles and the mark that end the sentence after the word (final=)
    bool polite = false;
    std::string_view voice;  // voice rows: the reading of (ら)れ the chain decided
};

Context ContextOf(const Flusher& f, const Item* it) {
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
                if (const std::string_view slot = ArgSlot(q->group->surface); !slot.empty()) c.args.emplace_back(slot, q->prev);
                q = q->prev;
            }
        }
        if (c.next != nullptr && ClassOf(c.next) == '1' && !Faint(c.next->group->surface) &&
            it->DirectSurface().find("て") == std::string_view::npos) {
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
int BestRow(const Flusher& f, const dict::PackTable& t, const Item* it, std::string_view key, dict::PackSlot slot,
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

// The text an item reads in its token: its surface and its conjugation ending.
std::string_view Segment(const Item* it) {
    const std::size_t start = static_cast<std::size_t>(std::max(it->start, 0));
    if (start >= it->text.size()) return it->group->surface;
    return it->text.substr(start, static_cast<std::size_t>(std::max(it->length, 0)));
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

// The span of colloc c that starts at items[i]: [i, end), the slot's words [slot, slotEnd) (none
// without a slot); absorbed: the slot's last word holds the start of the text after the slot.
bool MatchColloc(const dict::PackTable& t, const dict::Colloc& c, std::string_view before,
                 const std::vector<Item*>& items, std::size_t i, std::size_t& end, std::size_t& slot,
                 std::size_t& slotEnd, bool& absorbed) {
    slot = slotEnd = static_cast<std::size_t>(-1);
    absorbed = false;
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
    // start (最後の of {X}の一体), and that part is read.
    if (ClassOf(x) == '1' && (c.slotClass.empty() || OfClass(t, x, x->group->surface, c.slotClass)) &&
        ReadsText(items, slotEnd, c.after, end) && end > slotEnd)
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
    if (!c.slotClass.empty()) {
        std::string_view noun = x->group->surface;
        const std::string_view read = std::string_view(c.after).substr(0, c.after.size() - after.size());
        if (absorbed && last == at && noun.size() > read.size() && noun.ends_with(read)) noun.remove_suffix(read.size());
        if (!OfClass(t, x, noun, c.slotClass)) return false;
    }
    if (after.empty()) {
        end = slotEnd;
        return true;
    }
    return ReadsText(items, slotEnd, after, end) && end > slotEnd;
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

// One match of a colloc row: its span [begin, end), the slot's words [slot, slotEnd), and the
// Korean its head takes.
struct CollocMatch {
    const dict::Colloc* c = nullptr;
    std::size_t end = 0, slot = 0, slotEnd = 0, length = 0;
    bool absorbed = false;
    std::string_view head;  // compiled for the head's class, or raw
    bool conjugates = false;
};

// Whether the match can be written: no user word in it but in the slot, no word another span took,
// a head outside the slot, and a predicate head only with a phrase of its class.
bool Applicable(const std::vector<Item*>& items, std::size_t i, const std::set<const Item*>& taken, CollocMatch& m) {
    const dict::Colloc& c = *m.c;
    for (std::size_t k = i; k < m.end; k++) {
        const bool inSlot = c.slot && k >= m.slot && k < m.slotEnd;
        if (!inSlot && (items[k]->Fixed() || taken.contains(items[k]))) return false;
    }
    if (c.slot && m.end == m.slotEnd) return false;
    const char cls = ClassOf(items[m.end - 1]);
    const std::string& compiled = cls == '4' ? c.headVerb : cls == '7' ? c.headAdjective : cls == '8' ? c.headNa : c.headRaw;
    m.conjugates = !compiled.empty() && Predicate(cls);
    m.head = compiled.empty() ? std::string_view(c.headRaw) : std::string_view(compiled);
    return m.conjugates || !Predicate(cls);
}

// The colloc rows, left to right: at each word the longest applicable pattern that reads the words
// from it (then pri, then load order). The words of a span take no other pack row.
void ApplyCollocs(Flusher& f, const dict::PackTable& t, std::set<const Item*>& taken) {
    std::vector<Item*> items;
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next)
        if (it->group != nullptr) items.push_back(it);
    for (std::size_t i = 0; i < items.size(); i++) {
        CollocMatch best;
        for (const dict::Colloc& c : t.collocs) {
            for (const std::string& before : c.befores) {
                CollocMatch m;
                m.c = &c;
                if (!MatchColloc(t, c, before, items, i, m.end, m.slot, m.slotEnd, m.absorbed)) continue;
                if (!Applicable(items, i, taken, m)) continue;
                m.length = before.size() + c.after.size();
                const int pri = t.rows[static_cast<std::size_t>(c.row)].pri;
                if (best.c == nullptr || m.length > best.length ||
                    (m.length == best.length && pri > t.rows[static_cast<std::size_t>(best.c->row)].pri))
                    best = m;
            }
        }
        if (best.c == nullptr) continue;
        const dict::Colloc& c = *best.c;
        const dict::PackRow& r = t.rows[static_cast<std::size_t>(c.row)];
        const Selection source{Provenance::Pack, r.global};
        Item* head = items[best.end - 1];
        for (std::size_t k = i; k < best.end; k++) {
            Item* x = items[k];
            if (c.slot && k >= best.slot && k < best.slotEnd) continue;
            const bool afterSlot = c.slot && k == best.slotEnd && !best.absorbed;
            if (x == head) {
                std::string text(best.head);
                if (afterSlot && !c.particle.empty()) {
                    // the head right after the slot: the slot's particle is written after the slot's word
                    Item* own = items[best.slotEnd - 1];
                    own->appended += ResolvedParticle(own->EntryText(own->selected), c.particle);
                } else if (c.slot && c.spaced && c.particle.empty() && k >= best.slotEnd) {
                    // the space the target puts after the slot's Korean
                    text.insert(0, " ");
                }
                // the space before the span, when its first words are left empty and the head is written joined
                if (!c.slot && k > i && i > 0 && c.lead.empty()) text.insert(0, " ");
                Write(x, text, source);
                // a phrase that does not conjugate keeps none of the word's ending
                if (!best.conjugates && x->DirectIndex() >= 0) x->AssignDirectSlot(x->DirectIndex(), "", source);
            } else if (afterSlot) {
                Write(x, c.particle, source);
            } else if (k == i && !c.lead.empty()) {
                Write(x, c.lead, source);
            } else {
                Write(x, "", source);
            }
            taken.insert(x);
        }
        head->packRows.push_back(r.global);
        i = best.end - 1;
    }
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
    if (t.Empty()) return;
    std::set<const Item*> taken;
    if (!t.collocs.empty()) ApplyCollocs(f, t, taken);
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->group == nullptr || it->selected < 0 || it->selected >= static_cast<int>(it->group->entries.size()) ||
            taken.contains(it))
            continue;
        // A fixed word (a user's word, a span the sound stage wrote) keeps its decisions; a name of the
        // name layer takes name rows.
        const Entry* chosen = it->group->At(it->selected);
        const bool nameOnly = it->Fixed();
        if (nameOnly && (chosen->user == nullptr || !chosen->user->nameLayer)) continue;
        const char cls = ClassOf(it);
        // A voice row's agent particle and Korean verb hold against the passes after the voice layer.
        if (Item* p = it->packAgent; p != nullptr && p->selected >= 0 &&
                                     p->selected < static_cast<int>(p->group->entries.size()) && !p->Fixed() &&
                                     !taken.contains(p) && !it->packRows.empty()) {
            TakeTarget(p, ClassOf(p), it->packAgentText, {Provenance::Pack, it->packRows.front()});
            taken.insert(p);
        }
        if (!it->packLexeme.empty() && cls == '4') {
            it->detail->record = 0;
            it->lexical = true;
            const Selection source{Provenance::Pack, it->packRows.front()};
            it->AssignEntrySurface(it->selected, it->packLexeme, source);
            it->choice = source;
        }
        for (const dict::PackSlot slot :
             {dict::PackSlot::Sense, dict::PackSlot::Name, dict::PackSlot::Polarity, dict::PackSlot::Tense}) {
            if (nameOnly && slot != dict::PackSlot::Name) continue;
            const int best = BestRow(f, t, it, it->group->surface, slot);
            if (best < 0) continue;
            const dict::PackRow& r = t.rows[static_cast<std::size_t>(best)];
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
