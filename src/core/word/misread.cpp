// The misread filter: decisions, features, scores and the pass that applies them.
#include "core/word/misread.h"

#include <algorithm>

#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"
#include "core/word/flush.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

constexpr std::string_view kSpaces = " 　";

// The provenances a replacement may override: defaults of candidate order, the punctuation
// digit, the rule program and a single candidate. Fixed items, sense rows, text-path
// rewrites and scorers keep their choice.
constexpr Provenance kReplaceable[] = {
    Provenance::None, Provenance::FirstXxx, Provenance::Digit, Provenance::Single,
    Provenance::LocalRule, Provenance::GlobalRule,
};

constexpr std::size_t kMaxAlternatives = 6;

bool SmallTsu(std::string_view c) { return c == "っ" || c == "ッ"; }

// Korean a reader can judge: empty, or with a Hangul syllable.
bool Readable(std::string_view target) {
    if (target.empty()) return true;
    for (std::string_view t = target; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (r.cp >= U'가' && r.cp <= U'힣') return true;
        t.remove_prefix(r.size);
    }
    return false;
}

// The class letter of a character: _ none, H hiragana (ぁ-ゖ ゝ ゞ), K katakana (ァ-ヺ ー ヽ ヾ),
// C kanji (the CJK ideograph blocks, 々 〆), D digit, L Latin letter, P anything else.
char CharClass(std::string_view c) {
    if (c.empty()) return '_';
    const char32_t r = text::DecodeRune(c).cp;
    if ((r >= U'ぁ' && r <= U'ゖ') || r == U'ゝ' || r == U'ゞ') return 'H';
    if ((r >= U'ァ' && r <= U'ヺ') || r == U'ー' || r == U'ヽ' || r == U'ヾ') return 'K';
    if ((r >= 0x3400 && r <= 0x4DBF) || (r >= 0x4E00 && r <= 0x9FFF) || (r >= 0xF900 && r <= 0xFAFF) ||
        (r >= 0x20000 && r <= 0x3FFFF) || r == U'々' || r == U'〆') {
        return 'C';
    }
    if ((r >= U'0' && r <= U'9') || (r >= U'０' && r <= U'９')) return 'D';
    if ((r >= U'A' && r <= U'Z') || (r >= U'a' && r <= U'z') || (r >= U'Ａ' && r <= U'Ｚ') || (r >= U'ａ' && r <= U'ｚ')) {
        return 'L';
    }
    return 'P';
}

// The class letter of s when every character has it, M for a mix, _ for "".
char ScriptOf(std::string_view s) {
    char script = '_';
    for (std::string_view t = s; !t.empty();) {
        const std::string_view c = FirstChar(t);
        const char k = CharClass(c);
        if (script != '_' && k != script) return 'M';
        script = k;
        t.remove_prefix(c.size());
    }
    return script;
}

// A character as a feature value: its class, or itself for punctuation and symbols.
std::string CharValue(std::string_view c) {
    const char k = CharClass(c);
    return k == 'P' ? std::string(c) : std::string(1, k);
}

std::string PartCode(const Entry* e) {
    if (e == nullptr) return "_";
    std::string s(1, e->cls == 0 ? '_' : e->cls);
    s += e->code;
    return s;
}

// The tag and class digit of the selected entry of a neighbour, _ without one.
std::string NeighbourPart(const Item* it) {
    if (it == nullptr) return "_";
    const Entry* e = it->group->At(it->selected);
    return e == nullptr ? std::string("_") : PartCode(e).substr(0, 2);
}

// The class digit of the selected entry of an item, 0 without one.
char ClassDigit(const Item* it) {
    const Entry* e = it->group->At(it->selected);
    return e == nullptr ? '\0' : At(e->code, 0);
}

// The nearest item before or after it that is not punctuation or a symbol and whose class
// digit is not in skip, or null.
const Item* NearestWord(const Item* it, bool forward, std::string_view skip) {
    for (const Item* n = forward ? it->After() : it->prev; n != nullptr; n = forward ? n->next : n->prev) {
        if (ScriptOf(n->group->surface) != 'P' && !AnyOf(skip, ClassDigit(n))) return n;
    }
    return nullptr;
}

std::string_view SurfaceOf(const Item* it) { return it == nullptr ? std::string_view("_") : std::string_view(it->group->surface); }

std::string RankValue(int slot) {
    if (slot < 0) return "s";
    return slot < 2 ? std::to_string(slot) : std::string("2+");
}

std::string CountValue(std::size_t n) { return n < 3 ? std::to_string(n) : std::string("3+"); }

std::string RunesValue(std::string_view s) {
    const std::size_t n = text::RuneCount(s);
    return n < 4 ? std::to_string(n) : std::string("4+");
}

// The features every candidate of the decision shares.
void ItemFeatures(const MisreadDecision& d, std::vector<std::string>& out) {
    const Item& it = *d.item;
    const MisreadContext& c = d.context;
    const std::string_view surface = it.group->surface;
    const std::string before = CharValue(c.before), after = CharValue(c.after);
    const std::string_view first = FirstChar(surface), last = LastChar(surface);
    out.push_back(d.kind == MisreadDecisionKind::Word ? "k=W" : "k=E");
    out.push_back("s=" + std::string(surface));
    out.push_back(std::string("sc=") + ScriptOf(surface));
    out.push_back("n=" + RunesValue(surface));
    out.push_back("b=" + before);
    out.push_back("a=" + after);
    out.push_back("s|b=" + std::string(surface) + "|" + before);
    out.push_back("s|a=" + std::string(surface) + "|" + after);
    if (c.lineStart) out.push_back("ls");
    const char firstClass = CharClass(first), lastClass = CharClass(last);
    if ((firstClass == 'H' || firstClass == 'K') && CharClass(c.before) == firstClass) out.push_back("split=b");
    if ((lastClass == 'H' || lastClass == 'K') && CharClass(c.after) == lastClass) out.push_back("split=a");
    if (SmallTsu(c.before)) out.push_back("tsu=b");
    if (SmallTsu(c.after)) out.push_back("tsu=a");
    if (SmallTsu(last)) out.push_back("tsu=e");
    out.push_back("pp=" + NeighbourPart(it.prev));
    out.push_back("np=" + NeighbourPart(it.next));
    out.push_back("ps=" + std::string(SurfaceOf(it.prev)));
    out.push_back("ns=" + std::string(SurfaceOf(it.next)));
    out.push_back("pw=" + std::string(SurfaceOf(NearestWord(&it, false, ""))));
    out.push_back("nw=" + std::string(SurfaceOf(NearestWord(&it, true, ""))));
    const std::string pk(SurfaceOf(NearestWord(&it, false, "25"))), nk(SurfaceOf(NearestWord(&it, true, "25")));
    out.push_back("pk=" + pk);
    out.push_back("nk=" + nk);
    out.push_back("s|pk=" + std::string(surface) + "|" + pk);
    out.push_back("s|nk=" + std::string(surface) + "|" + nk);
    out.push_back("m=" + CountValue(d.candidates.size() - 1));
    if (d.kind == MisreadDecisionKind::Ending) out.push_back("es=" + std::string(it.DirectSurface()));
}

// The features of one candidate: entry e (null for the sound reading). The key is the
// surface of the item or, for an ending, of the ending.
void CandidateFeatures(const MisreadDecision& d, const MisreadCandidate& cand, const Entry* e,
                       std::vector<std::string>& out) {
    const Item& it = *d.item;
    const MisreadContext& c = d.context;
    const std::string_view surface = d.kind == MisreadDecisionKind::Word ? std::string_view(it.group->surface)
                                                                         : it.DirectSurface();
    const Selection choice = d.kind == MisreadDecisionKind::Word ? it.choice : it.directChoice;
    const std::string provenance = cand.chosen ? ProvenanceName(choice.layer) : "Alt";
    const std::string part = cand.slot < 0 ? std::string("snd") : PartCode(e);
    const std::string part2 = part.substr(0, 2);
    out.push_back("pv=" + provenance);
    out.push_back("pos=" + part);
    out.push_back("pos2=" + part2);
    out.push_back("pv|pos2=" + provenance + "|" + part2);
    out.push_back("pos2|b=" + part2 + "|" + CharValue(c.before));
    out.push_back("pos2|a=" + part2 + "|" + CharValue(c.after));
    if (c.lineStart) out.push_back("pos2|ls=" + part2);
    out.push_back("r=" + RankValue(cand.slot));
    out.push_back("t=" + cand.target);
    out.push_back("st=" + std::string(surface) + "|" + cand.target);
    if (e == nullptr) {
        out.push_back("snd");
        return;
    }
    const std::string_view sem = e->attr;
    for (std::size_t i = 0; i + 3 <= sem.size(); i += 3) out.push_back("sem=" + std::string(sem.substr(i, 3)));
    const std::string_view gram = e->gattr;
    for (std::size_t i = 0; i + 2 <= gram.size(); i += 2) out.push_back("g=" + std::string(gram.substr(i, 2)));
    const char digit = At(e->code, 0);
    if (digit == '2' || digit == '5') out.push_back("fn");
}

void AddWordDecision(Item* it, const MisreadContext& c, std::vector<MisreadDecision>& out) {
    const int selected = it->selected;
    const int count = static_cast<int>(it->group->entries.size());
    if (selected < 0 || selected >= count || !Readable(it->EntryText(selected))) return;
    MisreadDecision d;
    d.item = it;
    d.kind = MisreadDecisionKind::Word;
    d.context = c;
    d.candidates.push_back({selected, std::string(it->EntryText(selected)), true, {}, 0});
    const char digit = At(it->Code(selected), 0);
    for (int i = 0; i < count; i++) {
        const std::string_view t = it->EntryText(i);
        if (d.candidates.size() > kMaxAlternatives) break;
        if (i == selected || At(it->Code(i), 0) != digit || !Readable(t)) continue;
        if (std::any_of(d.candidates.begin(), d.candidates.end(), [&](const MisreadCandidate& m) { return m.target == t; })) {
            continue;
        }
        d.candidates.push_back({i, std::string(t), false, {}, 0});
    }
    d.sound = SoundReading(*it, c);
    if (!d.sound.empty() && std::none_of(d.candidates.begin(), d.candidates.end(),
                                         [&](const MisreadCandidate& m) { return m.target == d.sound; })) {
        d.candidates.push_back({-1, d.sound, false, {}, 0});
    }
    if (d.candidates.size() >= 2) out.push_back(std::move(d));
}

void AddEndingDecision(Item* it, const MisreadContext& c, std::vector<MisreadDecision>& out) {
    const int selected = it->DirectIndex();
    if (selected < 0 || selected >= it->DirectCount() || it->directChoice.layer == Provenance::Fixed ||
        !Readable(it->DirectText(selected))) {
        return;
    }
    MisreadDecision d;
    d.item = it;
    d.kind = MisreadDecisionKind::Ending;
    d.context = c;
    d.candidates.push_back({selected, std::string(it->DirectText(selected)), true, {}, 0});
    for (int i = 0; i < it->DirectCount(); i++) {
        const std::string_view t = it->DirectText(i);
        if (d.candidates.size() > kMaxAlternatives) break;
        if (i == selected || !it->DirectEnabled(i) || !Readable(t)) continue;
        if (std::any_of(d.candidates.begin(), d.candidates.end(), [&](const MisreadCandidate& m) { return m.target == t; })) {
            continue;
        }
        d.candidates.push_back({i, std::string(t), false, {}, 0});
    }
    if (d.candidates.size() >= 2) out.push_back(std::move(d));
}

}  // namespace

MisreadContext MisreadContextOf(const Item& it) {
    MisreadContext c;
    const Worker& w = *it.w;
    if (const std::string_view head = TrimRunes(Slice(it.text, 0, it.start), kSpaces); !head.empty()) {
        c.before = LastChar(head);
    }
    for (int j = it.tokenIndex - 1; c.before.empty() && j >= 0; j--) {
        if (const std::string_view t = TrimRunes(w.Tok(j).text, kSpaces); !t.empty()) c.before = LastChar(t);
    }
    if (c.before.empty()) c.before = w.settings.before;
    c.lineStart = c.before.empty();
    if (const std::string_view tail = TrimRunes(From(it.text, it.start + it.length), kSpaces); !tail.empty()) {
        c.after = FirstChar(tail);
    }
    for (int j = it.tokenIndex + 1; c.after.empty() && j < w.TokenCount(); j++) {
        if (const std::string_view t = TrimRunes(w.Tok(j).text, kSpaces); !t.empty()) c.after = FirstChar(t);
    }
    return c;
}

std::string SoundReading(const Item& it, const MisreadContext& context) {
    const std::string_view surface = it.group->surface;
    const std::size_t runes = text::RuneCount(surface);
    if (it.detail->Yougo() || runes == 0 || runes > 4) return {};
    const char script = ScriptOf(surface);
    if (script == 'H') {
        if (CharClass(context.after) != 'K' && !SmallTsu(context.after) && context.after != "ー") return {};
    } else if (script != 'K') {
        return {};
    }
    const std::string_view chosen = it.EntryText(it.selected);
    std::string sound = text::FromKana(surface, text::Preset::Fan);
    if (sound.empty() || chosen.empty() || text::SameSound(chosen, sound)) return {};
    return sound;
}

std::vector<MisreadDecision> MisreadDecisions(const Chain& chain, const MisreadModel& model) {
    std::vector<MisreadDecision> out;
    for (Item* it = chain.head; it != nullptr; it = it->next) {
        if (it->Fixed()) continue;
        const MisreadContext c = MisreadContextOf(*it);
        AddWordDecision(it, c, out);
        AddEndingDecision(it, c, out);
    }
    for (MisreadDecision& d : out) {
        std::vector<std::string> shared;
        ItemFeatures(d, shared);
        for (MisreadCandidate& cand : d.candidates) {
            const Entry* e = cand.slot < 0 ? nullptr
                             : d.kind == MisreadDecisionKind::Word ? d.item->group->At(cand.slot)
                                                                    : d.item->DirectEntry(cand.slot);
            cand.features = shared;
            CandidateFeatures(d, cand, e, cand.features);
            cand.score = MisreadScore(model, cand.features);
        }
    }
    return out;
}

int32_t MisreadScore(const MisreadModel& model, std::span<const std::string> features) {
    int64_t sum = model.bias;
    for (const std::string& f : features) {
        const auto w = std::lower_bound(model.weights.begin(), model.weights.end(), std::string_view(f),
                                        [](const MisreadWeight& a, std::string_view b) { return a.feature < b; });
        if (w != model.weights.end() && w->feature == f) sum += w->weight;
    }
    return static_cast<int32_t>(std::clamp<int64_t>(sum, INT32_MIN, INT32_MAX));
}

bool MisreadReplaceable(Provenance layer) {
    return std::find(std::begin(kReplaceable), std::end(kReplaceable), layer) != std::end(kReplaceable);
}

const MisreadCandidate* MisreadReplacement(const MisreadDecision& d, const MisreadModel& model) {
    const MisreadCandidate& chosen = d.candidates.front();
    const Selection choice = d.kind == MisreadDecisionKind::Word ? d.item->choice : d.item->directChoice;
    if (chosen.score < model.threshold || !MisreadReplaceable(choice.layer)) return nullptr;
    const MisreadCandidate* best = nullptr;
    for (std::size_t k = 1; k < d.candidates.size(); k++) {
        const MisreadCandidate& c = d.candidates[k];
        if (c.score < model.keep && (best == nullptr || c.score < best->score)) best = &c;
    }
    return best;
}

void FilterMisreads(Flusher& f) {
    const Settings& settings = f.w.settings;
    const MisreadModel& model = settings.misreadModel != nullptr ? *settings.misreadModel : BuiltinMisreadModel();
    const std::vector<MisreadDecision> decisions = MisreadDecisions(f.w.chain, model);
    struct Change {
        Item* item;
        MisreadDecisionKind kind;
        int slot;
        std::string_view sound;
    };
    std::vector<Change> changes;
    for (const MisreadDecision& d : decisions) {
        if (const MisreadCandidate* c = MisreadReplacement(d, model)) {
            changes.push_back({d.item, d.kind, c->slot, c->slot < 0 ? std::string_view(d.sound) : std::string_view()});
        }
    }
    const Selection misread{Provenance::Misread, -1};
    for (const Change& c : changes) {
        if (c.kind == MisreadDecisionKind::Ending) c.item->SetDirectIndex(c.slot, misread);
        else if (c.slot >= 0) c.item->SelectCandidate(c.slot, misread);
        else c.item->AssignEntrySurface(c.item->selected, c.sound, misread);
    }
}

}  // namespace hdTrans::word
