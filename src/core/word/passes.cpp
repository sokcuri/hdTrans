// Sentence passes that rewrite the chain before and after the selection: katakana
// runs and noun phrases are replaced by one longer item, class-3 words take the
// selection of the word they attach to, and negated plain verbs take their negative.
#include <algorithm>
#include <iterator>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/word/conjugation.h"
#include "core/word/constructions.h"
#include "core/word/flush.h"
#include "core/word/kana.h"
#include "core/word/strings.h"
#include "core/word/util.h"

namespace hdTrans::word {
namespace {

constexpr std::string_view kNegativeFamily = "&NF@";
constexpr std::string_view kNegativeAdjective = "$DH@";
constexpr std::string_view kAbsentFamily = "&M9@";  // 없다

// Every character of s is katakana.
bool AllKatakana(std::string_view s) {
    for (std::string_view rest = s; !rest.empty(); rest.remove_prefix(FirstChar(rest).size())) {
        if (NameClassOf(FirstChar(rest)) != kClassKatakana) return false;
    }
    return true;
}

struct NmWord {
    Item* item = nullptr;
    Record* record = nullptr;
};

// A katakana word with katakana neighbours of its token: one word of the run and its
// record, or no item. On the text path a neighbour joins only when all it covers is
// katakana, so the hiragana ending of a word stays out of the run (リアルなティアーズ).
// The target is left empty: the run grows by a neighbour per call, and DrainNmTail
// transliterates only the words that stay in the chain.
NmWord BuildNmTail(Flusher& f) {
    Worker& w = f.w;
    auto katakana = [&](const Item* x) {
        if (Classify(x->group->surface) != 14) return false;
        if (!w.settings.text) return true;
        return AllKatakana(x->text.substr(static_cast<std::size_t>(x->start), static_cast<std::size_t>(x->length)));
    };
    for (Item* it = w.chain.tail; it != nullptr; it = it->prev) {
        if (Classify(it->group->surface) != 14 || !Has(it->GAttr(0), "Nm")) continue;
        int start = it->start;
        int length = it->length;
        int neighbours = 0;
        if (Item* n = it->After(); n != nullptr && katakana(n) && it->tokenIndex == n->tokenIndex) {
            neighbours = 1;
            length += n->length;
        }
        if (Item* p = it->prev; p != nullptr && katakana(p) && it->tokenIndex == p->tokenIndex) {
            start = p->start;
            neighbours++;
            length += p->length;
        }
        if (neighbours < 1) continue;
        const std::string_view raw = it->text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(length));
        Record* record = w.CreateSyntheticRecord(it->tokenIndex, raw, true);
        return {w.InsertLateCandidate(it->text, start, it->tokenIndex, record), record};
    }
    return {};
}

// phraseFollowsNi: after a word に, the phrase is accepted unless the next
// conjugating word has a UMv attribute (an empty group does not count).
bool PhraseFollowsNi(Item* ni) {
    Item* scan = ni->next;
    while (scan != nullptr && !scan->detail->Yougo()) scan = scan->next;
    if (scan == nullptr || scan->group->entries.empty()) return true;
    for (int slot = 0; slot < static_cast<int>(scan->group->entries.size()); slot++) {
        if (Has(scan->Attr(slot), "UMv")) return false;
    }
    return true;
}

// Whether it is one of three or more copies of a word said again (internal::CopyAfter: 好き of
// 好き好き好き, 好き、好き、好き) that the dictionary also reads as an adjectival noun. Such a run
// is a cry whatever the words (SND-2): each copy reads as the word said alone, whatever stands
// before or after the run, the last copy before a comma and another word too (好き、好き、好き、大好き).
bool CriedCopy(const Item* it) {
    // The copy right before c: a word up to three words back whose copy after it is c
    // (好き|、|好き, 痛い|っ|、|痛い).
    auto before = [](const Item* c) -> const Item* {
        const Item* p = c->prev;
        for (int k = 0; k < 3 && p != nullptr; k++, p = p->prev) {
            if (internal::CopyAfter(p) == c && Slice(p->text, p->start, p->length) == Slice(c->text, c->start, c->length)) {
                return p;
            }
        }
        return nullptr;
    };
    // Two other copies on either side are enough to see three.
    int others = 0;
    for (const Item* c = it; others < 2 && (c = before(c)) != nullptr;) others++;
    for (const Item* c = it; others < 2 && (c = internal::CopyAfter(c)) != nullptr;) others++;
    if (others < 2 || it->w == nullptr) return false;
    // Only a word the dictionary also reads as an adjectival noun says a feeling (好き, 嫌い); a
    // verb stem that is also a noun stays the verb (迷い迷い迷い 헤매어, 休み休み休み).
    const std::string combined = it->group->surface + std::string(it->DirectSurface());
    for (const Record* r = it->w->DoLookup(combined, kModeGeneral).records; r != nullptr; r = r->child) {
        if (r->source == combined && At(r->partCode, 0) == 'G') return true;
    }
    return false;
}

struct Phrase {
    std::string_view text;
    int start = 0;
    int token = 0;
    Record* record = nullptr;
};

// findPhrase looks for a word whose unused records hold a noun entry for the word
// itself or for the word plus its ending, and returns the text, start and token to
// insert it at, with that record.
Phrase FindPhrase(Flusher& f) {
    Worker& w = f.w;
    Chain& chain = w.chain;
    for (Item* it = chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo()) continue;
        int mode = 1;
        if (it->DirectCount() >= 20 && CriedCopy(it)) {
            mode = 3;
        } else if (it->DirectCount() >= 20) {
            for (int slot = 0; slot < it->DirectCount(); slot++) {
                if (!it->detail->enabled[slot]) continue;
                const char sub = At(it->DirectCode(slot), 1);
                if (sub == '0' || sub == '6') {
                    if (it->After() != nullptr && At(it->After()->Code(0), 0) == '4') mode = 2;
                    break;
                }
                if (sub == ':') {
                    Item* prev = it->prev;
                    Item* next = it->After();
                    if (prev != nullptr && prev->group->surface == kJaO && next != nullptr &&
                        next->group->surface.starts_with(PrefixGate(14))) {
                        mode = 0;
                    } else if (prev != nullptr && At(prev->Code(0), 0) == '3') {
                        mode = 0;
                    } else if (prev == nullptr || At(prev->Code(0), 0) != '2' || prev->prev == nullptr ||
                               At(prev->prev->Code(0), 0) != '1' || prev->group->surface == kJaNo) {
                        mode = 3;
                    } else if (next != nullptr && At(next->Code(0), 0) == '2') {
                        mode = 3;
                    } else if (next == nullptr || !Has(next->GAttr(0), "Pj")) {
                        mode = 0;
                    } else {
                        mode = 3;
                    }
                    break;
                }
            }
        }
        switch (mode) {
            case 1: {
                Group* g = it->group;
                if (g->surface == kJaKudasaKanji || g->surface == kJaKudasa || it->Code(0).empty()) continue;
                std::size_t slot = 0;
                for (; slot < g->entries.size(); slot++) {
                    const char sub = At(it->Code(static_cast<int>(slot)), 1);
                    if (sub == '1' || sub == '3' || sub == '5') break;
                }
                if (slot == g->entries.size()) continue;
                Record* record = g->records;
                g->records = nullptr;
                const std::string_view raw = it->text.substr(static_cast<std::size_t>(it->start), static_cast<std::size_t>(it->length));
                for (; record != nullptr; record = record->child) {
                    const bool noun = At(record->partCode, 0) == 'N';
                    bool accepted = false;
                    if (noun && raw == record->source) {
                        Item* prev = it->prev;
                        Item* next = it->After();
                        if (prev != nullptr && prev->group->surface.ends_with(kJaNo) && At(prev->Code(0), 0) == '2') {
                            accepted = true;
                        } else if (next != nullptr) {
                            if (At(next->Code(0), 0) == '2' || Has(next->GAttr(0), "Pj")) {
                                accepted = true;
                            } else if (At(next->Code(0), 0) == '1' && At(it->Code(0), 2) == '7') {
                                for (int direct = 0; direct < it->DirectCount(); direct++) {
                                    if (it->detail->enabled[direct]) {
                                        accepted = At(it->DirectCode(direct), 1) == '8';
                                        break;
                                    }
                                }
                            }
                        } else if (it->text.size() == static_cast<std::size_t>(it->length + it->start)) {
                            const int t = it->tokenIndex + 1;
                            if (t < w.TokenCount()) {
                                const tokenizer::TokenView view = w.Tok(t);
                                if (view.type == 17 && At(view.text, 0) == ',') accepted = true;
                            }
                        }
                    } else if (noun && raw == std::string(record->source) + std::string(kJaComma)) {
                        Item* prev = it->prev;
                        accepted = prev != nullptr && prev->group->surface.ends_with(kJaNo) && At(prev->Code(0), 0) == '2';
                    }
                    if (accepted) return {it->text, it->start, it->tokenIndex, record};
                }
                break;
            }
            case 2: {
                Item* next = it->After();
                if (next == nullptr || !next->detail->Yougo()) break;
                const std::string combined = next->group->surface + std::string(next->DirectSurface());
                Record* record = next->group->records;
                next->group->records = nullptr;
                for (; record != nullptr; record = record->child) {
                    if (combined == record->source && At(record->partCode, 0) == 'N') {
                        return {next->text, next->start, next->tokenIndex, record};
                    }
                }
                break;
            }
            case 3: {
                // After a verb in its て form a kana auxiliary stays a verb (増えていき: いく, not 息).
                if (const Item* prev = it->prev;
                    prev != nullptr && prev->detail->Yougo() && At(prev->Code(0), 0) == '4' &&
                    (prev->DirectSurface().ends_with(kJaTe) || prev->DirectSurface().ends_with(kJaDe)) &&
                    dict::InClass(HDTRANS_CLASS("aux.te-kana-stem"), it->group->surface)) {
                    break;
                }
                const std::string combined = it->group->surface + std::string(it->DirectSurface());
                const bool cry = CriedCopy(it);
                Record* record = it->group->records;
                it->group->records = nullptr;
                for (; record != nullptr; record = record->child) {
                    if (combined != record->source || At(record->partCode, 0) != 'N') continue;
                    // Copies said straight after it are passed over: the word reads like the last
                    // copy of the run (好き好き好き, 迷い迷い、).
                    Item* next = it->After();
                    for (const Item* copy = it; next != nullptr && internal::CopyAfter(copy) == next;) {
                        copy = next;
                        next = next->After();
                    }
                    if (next != nullptr && !cry) {
                        const char k = At(next->Code(0), 0);
                        if (!(k == '2' || k == '1' || Has(next->GAttr(0), "Pj") || (k == '6' && next->EntryText(0) != ","))) {
                            continue;
                        }
                        // そう after the stem is the appearance, not a noun after a noun (泣きそう 울 것
                        // 같다, not 울음 그렇게).
                        if (next->group->surface == kJaSou) continue;
                    }
                    if (next == nullptr || next->group->surface != kJaNi || PhraseFollowsNi(next)) {
                        return {it->text, it->start, it->tokenIndex, record};
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    return {};
}

// cleanupClass3Item: for a class-3 word with a Zn candidate, look right for the word
// it attaches to and select the Zn candidate (or, before an Sp ending, move away from
// it).
void CleanupClass3Item(Item* it) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int index = 0; index < count; index++) {
        if (!Has(it->GAttr(index), "Zn")) continue;
        bool accepted = false;
        for (Item* next = it->After(); next != nullptr; next = next->next) {
            const char kind = At(next->Code(next->selected), 0);
            bool marker = false;
            switch (kind) {
                case '0': case '1': case '9':
                    marker = Has(next->GAttr(next->selected), "Xn");
                    break;
                case '4': case '7': case '8':
                    marker = Has(next->GAttr(next->selected), "Xn") || Has(next->DirectGAttr(next->DirectIndex()), "Yn");
                    if (Item* nn = next->next; !marker && nn != nullptr && At(nn->Code(nn->selected), 0) == '7') {
                        marker = Has(nn->GAttr(nn->selected), "Xn") || Has(nn->DirectGAttr(nn->DirectIndex()), "Yn");
                    }
                    break;
                case '6':
                    accepted = true;
                    break;
                default:
                    break;
            }
            if (marker) {
                accepted = true;
                it->SelectCandidate(index, {Provenance::Rewrite});
            }
            if (Has(next->DirectGAttr(next->DirectIndex()), "Sp")) {
                accepted = true;
                if (index == it->selected) {
                    // The XXX test reads the attributes of the word on the right, slot by slot
                    // over this item's slots.
                    int fallback = -1, xxx = -1;
                    for (int slot = 0; slot < count; slot++) {
                        if (slot == index) continue;
                        if (!Has(next->Attr(slot), "XXX")) {
                            if (fallback == -1) fallback = slot;
                        } else {
                            xxx = slot;
                        }
                    }
                    if (xxx != -1) it->SelectCandidate(xxx, {Provenance::Rewrite});
                    else if (fallback != -1) it->SelectCandidate(fallback, {Provenance::Rewrite});
                }
            }
            const char k = At(next->Code(next->selected), 0);
            if (k == '7' || k == '8' || accepted) return;
        }
    }
}

// insertParenthesized finds a proper noun (code 13...) followed by a parenthesised
// hiragana reading of its target and makes one word of the noun and the reading.
// The parenthesis and the reading are in the noun's token: the new word replaces the
// reading there, so the same noun does not match again.
Item* InsertParenthesized(Flusher& f) {
    Worker& w = f.w;
    Chain& chain = w.chain;
    auto cursorNext = [&] { return chain.cursor == nullptr ? nullptr : (chain.cursor = chain.cursor->next); };
    chain.cursor = chain.head;
    for (Item* it = chain.cursor; it != nullptr; it = cursorNext()) {
        if (!it->Code(it->selected).starts_with("13")) continue;
        const int index = it->tokenIndex;
        const std::string_view sourceText = it->text;
        std::string translated(it->EntryText(it->selected));
        std::string spaced(it->EntryText(it->selected));
        chain.cursor = it->next;
        Item* open = chain.cursor;
        if (open == nullptr) continue;
        while (index == open->tokenIndex) {
            if (!open->Code(open->selected).starts_with("13")) break;
            translated += open->EntryText(open->selected);
            spaced += " ";
            spaced += open->EntryText(open->selected);
            open = cursorNext();
            if (open == nullptr) break;
        }
        if (open == nullptr || open->tokenIndex != index || open->EntryText(open->selected) != "(") continue;
        Item* inner = cursorNext();
        if (inner == nullptr || inner->tokenIndex != index || Classify(inner->group->surface) != 13) continue;
        const int start = inner->start;
        std::string raw(inner->text.substr(static_cast<std::size_t>(inner->start), static_cast<std::size_t>(inner->length)));
        std::string innerTranslation(inner->EntryText(inner->selected));
        Item* closing = cursorNext();
        while (closing != nullptr && index == closing->tokenIndex) {
            if (closing->EntryText(closing->selected) == ")" || Classify(closing->group->surface) != 13) break;
            raw += closing->text.substr(static_cast<std::size_t>(closing->start), static_cast<std::size_t>(closing->length));
            innerTranslation += closing->EntryText(closing->selected);
            closing = cursorNext();
        }
        if (raw.empty() || innerTranslation == spaced) continue;
        if (TransliterateHiragana(raw) != translated || innerTranslation == translated) continue;
        int recordToken = index;
        if (closing != nullptr) recordToken = closing->tokenIndex;
        Record* record = w.CreateSyntheticRecord(recordToken, w.arena.Keep(raw), false);
        record->target = spaced;
        Item* result = w.InsertLateCandidate(sourceText, start, index, record);
        if (result == nullptr) continue;
        result->SelectCandidate(0, {Provenance::Rewrite});
        return result;
    }
    return nullptr;
}

// An upper bound on the insertions of a pass over the chain. Each insertion replaces
// items of the chain or consumes the records of a group, so a pass makes at most about
// one per item; the bound stops a pass whose insertion leaves the chain unchanged.
int InsertionBudget(const Chain& chain) {
    int count = 0;
    for (const Item* it = chain.head; it != nullptr; it = it->next) count++;
    return 2 * count + 2;
}

// An item of kana the analysis could not place: a synthetic hiragana word, made for a
// character no dictionary word covers, whose target the lookup left empty.
bool UnplacedKana(const Item* it) {
    if (it == nullptr || it->group == nullptr || it->group->entries.size() != 1) return false;
    const Entry* e = it->group->entries.front();
    return e->dst.empty() && e->field.empty() && Has(e->gattr, "Nm") && Classify(it->group->surface) == 13;
}

constexpr std::string_view kCaseParticles[] = {"が", "を", "に", "は", "も", "で", "と", "へ", "や", "の"};

// Whether b starts where a ends in the same token.
bool Adjacent(const Item* a, const Item* b) {
    return a != nullptr && b != nullptr && a->tokenIndex == b->tokenIndex && a->text.data() == b->text.data() &&
           a->start + a->length == b->start;
}

}  // namespace

// An unread kana head before a placed particle or ending is still the noun that grammar
// attaches to. Read its sound without changing the grammar or a dictionary word's reading.
void ReadUnplacedHeads(Flusher& f) {
    Worker& w = f.w;
    for (Item* it = w.chain.head; it != nullptr; it = it->next) {
        if (!UnplacedKana(it)) continue;
        const Item* prev = it->prev;
        bool boundary = !Adjacent(prev, it) || At(prev->Code(0), 0) == '2' || At(prev->Code(0), 0) == '6';
        if (!boundary && UnplacedKana(prev) &&
            std::find(std::begin(kCaseParticles), std::end(kCaseParticles), prev->group->surface) != std::end(kCaseParticles) &&
            Adjacent(prev->prev, prev) && At(prev->prev->Code(0), 0) == '1' && !prev->prev->EntryText(0).empty()) {
            boundary = true;
        }
        if (!boundary) continue;
        Item* last = it;
        while (Adjacent(last, last->next) && UnplacedKana(last->next)) last = last->next;
        Item* next = last->next;
        if (!Adjacent(last, next) || next->group == nullptr) continue;
        bool bound = false;
        for (int slot = 0; slot < static_cast<int>(next->group->entries.size()); slot++) {
            const char part = At(next->Code(slot), 0);
            if ((part == '2' || part == '5') && !next->EntryText(slot).empty()) bound = true;
        }
        if (!bound) continue;
        const int length = last->start + last->length - it->start;
        const std::string_view raw = it->text.substr(static_cast<std::size_t>(it->start), static_cast<std::size_t>(length));
        bool word = false;
        for (const dict::Candidate& c : w.snapshot.RetrieveRecords(raw, dict::LookupMode::General)) {
            if (c.source != raw || c.codes == nullptr || c.target.empty() ||
                (c.user != nullptr && c.user->nameLayer) || c.codes->sem.Text().starts_with("QPN")) continue;
            const char part = c.codes->part.ClassDigit();
            if (part == '1' || part == '3' || part == '4' || part == '7' || part == '8' || part == '9') word = true;
        }
        if (word) continue;
        bool voiced = false;
        for (std::string_view rest = raw; !rest.empty(); rest.remove_prefix(FirstChar(rest).size())) {
            if (!Has("ぁぃぅぇぉゃゅょゎっゕゖ", FirstChar(rest))) voiced = true;
        }
        if (!voiced) continue;
        const std::string target = TransliterateHiragana(raw);
        if (target.empty()) continue;
        Record* record = w.CreateSyntheticRecord(it->tokenIndex, raw, true);
        record->target = target;
        Item* read = w.NewItem(w.NewDetail(kDetailInfo, it->tokenIndex), w.NewGroup(record, ' '), it->text, it->start,
                              it->tokenIndex);
        w.chain.Insert(read, true);
        it = read;
    }
}

// A case particle the analysis could not place between a noun and a placed word is read as the
// particle (鼻がもげる with もげる unknown leaves が alone; MNG-5).
void ReadUnplacedParticles(Flusher& f) {
    Worker& w = f.w;
    for (Item* it = w.chain.head; it != nullptr; it = it->next) {
        if (!UnplacedKana(it) || std::find(std::begin(kCaseParticles), std::end(kCaseParticles), it->group->surface) ==
                                     std::end(kCaseParticles)) {
            continue;
        }
        Item* prev = it->prev;
        Item* next = it->next;
        if (!Adjacent(prev, it) || !Adjacent(it, next) || UnplacedKana(prev) || UnplacedKana(next) ||
            At(prev->Code(0), 0) != '1' || prev->EntryText(0).empty()) {
            continue;
        }
        const std::string_view particle = it->group->surface;
        Record* records = nullptr;
        Record** tail = &records;
        for (Record* r = w.DoLookup(particle, kModeGeneral).records; r != nullptr; r = r->child) {
            if (r->source != particle || At(r->partCode, 1) != '2') continue;
            *tail = r;
            tail = &r->child;
        }
        *tail = nullptr;
        if (records == nullptr) continue;
        Item* read = w.InsertLateCandidate(it->text, it->start, it->tokenIndex, records);
        if (read == nullptr || read->length != it->length) continue;
        w.chain.Insert(read, true);
        it = read;
    }
}

void DrainNmTail(Flusher& f) {
    Worker& w = f.w;
    std::vector<NmWord> built;
    for (int budget = InsertionBudget(w.chain); budget > 0; budget--) {
        const NmWord word = BuildNmTail(f);
        if (word.item == nullptr) break;
        w.chain.Insert(word.item, true);
        built.push_back(word);
    }
    if (built.empty()) return;
    auto byItem = [](const NmWord& a, const NmWord& b) { return std::less<const Item*>()(a.item, b.item); };
    std::sort(built.begin(), built.end(), byItem);
    for (Item* it = w.chain.head; it != nullptr; it = it->next) {
        const auto found = std::lower_bound(built.begin(), built.end(), NmWord{it, nullptr}, byItem);
        if (found == built.end() || found->item != it) continue;
        Record* record = found->record;
        if (!w.Transliterate(record->source, record->target)) w.fault = true;
        // The group of the synthetic record starts with its entry.
        if (!it->group->entries.empty()) it->group->entries.front()->dst = record->target;
    }
}

void ReinsertPhrases(Flusher& f) {
    for (int budget = InsertionBudget(f.w.chain); budget > 0; budget--) {
        const Phrase p = FindPhrase(f);
        if (p.record == nullptr) return;
        Item* it = f.w.InsertLateCandidate(p.text, p.start, p.token, p.record);
        if (it == nullptr) return;
        f.w.chain.Insert(it, false);
    }
}

void CleanupClass3(Flusher& f) {
    Chain& chain = f.w.chain;
    for (Item* it = chain.tail; it != nullptr; it = it->prev) {
        if (At(it->Code(it->selected), 0) == '3') CleanupClass3Item(it);
    }
    for (int budget = InsertionBudget(chain); budget > 0; budget--) {
        Item* it = InsertParenthesized(f);
        if (it == nullptr) return;
        chain.Insert(it, true);
    }
}

void ApplyNegatives(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Entry* e = it->group->At(it->selected);
        if (e == nullptr || it->DirectIndex() == -1) continue;
        // A word already read as 없다 (があ, がござ before ません) takes the negative ending
        // as a form of its own: ありませんのに 없는데요, not 없지 않는데요.
        const bool absent = it->EntryText(it->selected).ends_with(kAbsentFamily);
        if (!absent && (e->user == nullptr || e->user->negative.empty())) continue;
        Entry* ending = it->DirectEntry(it->DirectIndex());
        if (ending == nullptr) continue;
        char code = 0;
        std::string after;
        const std::string jiSpace = std::string("1") + std::string(kKoJi) + " ";
        if (ending->dst.starts_with(jiSpace) && std::string_view(ending->dst).substr(jiSpace.size()).starts_with(kKoAnh)) {
            const std::string_view rest = std::string_view(ending->dst).substr(jiSpace.size());
            std::string_view row;
            for (const char k : std::string_view("0123456789AB")) {
                const std::string key = std::string(kNegativeFamily) + k;
                const std::string_view r = LookupSource(key, f.families);
                if (rest.starts_with(r) && r.size() > row.size()) {
                    code = k;
                    row = r;
                }
            }
            after = std::string(rest.substr(row.size()));
        } else if (absent) {
            continue;
        } else if (Item* n = it->After(); ending->dst == std::string("1") + std::string(kKoJi) && n != nullptr &&
                                       n->DirectIndex() != -1 && n->EntryText(n->selected) == kNegativeAdjective) {
            const std::string_view text = n->DirectText(n->DirectIndex());
            if (text.empty()) continue;
            switch (text[0]) {
                case '1': code = '1'; break;
                case '2': code = '2'; break;
                case '3': code = '3'; break;
                case '4': code = '0'; break;
                case '5': code = '5'; break;
                case '6': code = '6'; break;
                case '7': code = '7'; break;
                case '8': code = 'B'; break;
                default: code = 0; break;
            }
            if (code != 0) after = std::string(text.substr(1));
            if (code == '1' && std::string_view(after).starts_with(kKoDa)) code = '4';
            n->AssignEntrySurface(n->selected, "");
        } else {
            continue;
        }
        const std::string_view afterView(after);
        if (code == 0) continue;
        if (code == '0' && afterView.starts_with(kKoDa)) {
            code = '4';
        } else if (code == '2' && !afterView.starts_with(kKoMyeon) && !afterView.starts_with(kKoReo) &&
                   !afterView.starts_with(kKoRyeo)) {
            code = 'A';
        }
        if (!absent) it->AssignEntrySurface(it->selected, e->user->negative);
        it->AssignDirectSlot(it->DirectIndex(), std::string(1, code) + after);
    }
}

}  // namespace hdTrans::word
