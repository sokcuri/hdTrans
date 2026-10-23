#include "core/word/constructions.h"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "core/dict/builtin.h"
#include "core/dict/classes.h"
#include "core/dict/codes.h"
#include "core/rules/match.h"
#include "core/text/unicode.h"
#include "core/tokenizer/token.h"
#include "core/word/flush.h"
#include "core/word/util.h"

namespace hdTrans::word {

namespace internal {

namespace {

struct NamedFeature {
    std::string_view name;
    CxFeature test;
};

// The features a condition names with F. Each is a property of the item at the atom's position.
constexpr NamedFeature kFeatures[] = {
    {"exists", [](Flusher&, const Item*) { return true; }},
    {"selected", [](Flusher&, const Item* it) { return it->selected >= 0; }},
    {"conjugating", [](Flusher&, const Item* it) { return it->detail->Yougo(); }},
    {"fixed", [](Flusher&, const Item* it) { return it->Fixed(); }},
    {"first", [](Flusher&, const Item* it) { return it->prev == nullptr; }},
    {"last", [](Flusher&, const Item* it) { return it->next == nullptr; }},
    {"verb", [](Flusher&, const Item* it) { return it->detail->cls == kDetailVerb; }},
    {"boundary", [](Flusher&, const Item* it) { return PauseOrEnd(it); }},
    // The Korean of the next word starts with this word's Korean and a space.
    {"lemma-starts-next",
     [](Flusher& f, const Item* it) {
         const std::string lemma = ItemLemma(f, it, it->selected);
         return !lemma.empty() && it->next != nullptr && ItemLemma(f, it->next, it->next->selected).starts_with(lemma + " ");
     }},
    {"opens-line", [](Flusher& f, const Item* it) { return OpensLine(f.w, it); }},
    {"number", [](Flusher&, const Item* it) { return NumberSurface(it->group->surface); }},
    {"quote-verb", [](Flusher&, const Item* it) { return QuoteVerbFollows(it); }},
    // A fixed word span that the phrase or sound stage already read (an utterance in brackets).
    {"word-span", [](Flusher& f, const Item* it) { return it->Fixed() && f.w.Span(it->tokenIndex) == tokenizer::kSpanWord; }},
    // A digit ends the text before the word in its token (3から).
    {"after-digit", [](Flusher&, const Item* it) { return text::IsDigit(DecodeLastRune(Slice(it->text, 0, it->start)).cp); }},
};

struct FieldSpelling {
    char letter;
    CxField field;
};

constexpr FieldSpelling kFields[] = {
    {'S', CxField::Surface}, {'L', CxField::Lemma},         {'D', CxField::Target},
    {'P', CxField::Part},    {'A', CxField::Sem},           {'K', CxField::Class},
    {'U', CxField::EndingSurface}, {'Y', CxField::EndingTarget}, {'B', CxField::Word},
    {'I', CxField::Text},    {'H', CxField::Head},          {'F', CxField::Feature},
};

bool SetField(CxField field) {
    return field == CxField::Surface || field == CxField::Lemma || field == CxField::Target ||
           field == CxField::EndingSurface || field == CxField::EndingTarget || field == CxField::Text ||
           field == CxField::Head || field == CxField::Word;
}

// The nodes and atoms of one condition appended to a table.
class CxSink final : public rules::BodySink {
public:
    explicit CxSink(ConstructionTable& t) : t_(t) {}

    std::size_t List(rules::BodyKind kind, int count) override {
        t_.nodes.push_back({kind, false, static_cast<uint32_t>(count), 0});
        return t_.nodes.size() - 1;
    }

    void End(std::size_t list) override { t_.nodes[list].end = static_cast<uint32_t>(t_.nodes.size()); }

    bool Atom(char letter, int8_t delta, std::string_view op, std::string_view arg, rules::Tokens& tokens) override {
        CxAtom a;
        a.delta = delta;
        const FieldSpelling* field = nullptr;
        for (const FieldSpelling& s : kFields) {
            if (s.letter == letter) field = &s;
        }
        if (field == nullptr) return tokens.Fail("unknown field");
        a.field = field->field;
        if (op != "==" && op != "!=") return tokens.Fail("unknown operator");
        a.negated = op == "!=";
        if (arg[0] == '@') {
            std::string_view name = arg.substr(1);
            if (!SetField(a.field)) return tokens.Fail("this field takes no word class");
            a.match = CxMatch::Member;
            if (name.starts_with('L') || name.starts_with('R')) {
                if (a.field == CxField::Word) return tokens.Fail("B takes a whole word class");
                a.match = name[0] == 'L' ? CxMatch::MemberStart : CxMatch::MemberEnd;
                name.remove_prefix(1);
            }
            a.set = dict::internal::FindClass(dict::internal::BuiltinClasses(), name, dict::internal::ClassHash(name));
            if (a.set == nullptr) return tokens.Fail("no such word class");
        } else {
            const char* error = nullptr;
            std::string raw;
            if (!rules::AppendLiteral(arg, raw, error)) return tokens.Fail(error);
            if (a.field == CxField::Part) raw = dict::PartPatternOf('P', raw);
            if (a.field == CxField::Feature) {
                for (const NamedFeature& n : kFeatures) {
                    if (n.name == raw) a.feature = n.test;
                }
                if (a.feature == nullptr) return tokens.Fail("no such feature");
                a.match = CxMatch::Feature;
            }
            if (a.field == CxField::Class && raw.size() != 1) return tokens.Fail("K takes one letter");
            a.value = std::move(raw);
        }
        t_.nodes.push_back({rules::BodyKind::Or, true, static_cast<uint32_t>(t_.atoms.size()), 0});
        t_.nodes.back().end = static_cast<uint32_t>(t_.nodes.size());
        t_.atoms.push_back(std::move(a));
        return true;
    }

private:
    ConstructionTable& t_;
};

std::string_view Chomp(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
    return line;
}

std::vector<std::string_view> SplitTabs(std::string_view s) {
    std::vector<std::string_view> out;
    for (;;) {
        const std::size_t tab = s.find('\t');
        out.push_back(s.substr(0, tab));
        if (tab == std::string_view::npos) return out;
        s.remove_prefix(tab + 1);
    }
}

bool FamilyName(std::string_view s) {
    if (s.empty() || s[0] < 'a' || s[0] > 'z') return false;
    return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

bool ParseActions(std::string_view text, std::vector<CxAction>& out, const char*& error) {
    constexpr std::pair<std::string_view, CxVerb> kVerbs[] = {
        {"select-lemma", CxVerb::SelectLemma}, {"take-target", CxVerb::TakeTarget}, {"set-target", CxVerb::SetTarget},
        {"set-ending", CxVerb::SetEnding},     {"replace-head", CxVerb::ReplaceHead},
    };
    rules::Tokens tokens(text);
    while (!tokens.AtEnd()) {
        std::string_view word, literal;
        if (!tokens.Next(word)) break;
        CxAction a;
        const std::size_t at = word.find('@');
        const std::string_view verb = word.substr(0, at);
        if (at != std::string_view::npos && !rules::ParseDelta(word.substr(at + 1), a.delta)) {
            tokens.Fail("bad action position");
            break;
        }
        bool known = false;
        for (const auto& [name, v] : kVerbs) {
            if (name == verb) {
                a.verb = v;
                known = true;
            }
        }
        if (!known) {
            tokens.Fail("unknown action");
            break;
        }
        const char* bad = nullptr;
        if (!tokens.Next(literal) || !rules::AppendLiteral(literal, a.value, bad)) {
            tokens.Fail(bad != nullptr ? bad : "an action takes a string literal");
            break;
        }
        if (a.verb == CxVerb::ReplaceHead && (!tokens.Next(literal) || !rules::AppendLiteral(literal, a.with, bad))) {
            tokens.Fail(bad != nullptr ? bad : "replace-head takes the old and the new head");
            break;
        }
        out.push_back(std::move(a));
    }
    error = tokens.error;
    if (error == nullptr && out.empty()) error = "no action";
    return error == nullptr;
}

}  // namespace

ConstructionTable ParseConstructions(std::span<const std::string_view> lines, std::span<const std::string_view> passes) {
    ConstructionTable t;
    std::unordered_map<std::string_view, std::size_t> families;
    std::vector<std::string> ids;
    const auto fail = [&](int line, std::string message) { t.errors.push_back({line, std::move(message)}); };
    int number = 0;
    for (const std::string_view raw : lines) {
        number++;
        const std::string_view line = Chomp(raw);
        if (line.empty()) continue;
        if (line.starts_with("//=")) {
            std::string_view head = line.substr(3);
            while (head.starts_with(' ')) head.remove_prefix(1);
            const std::vector<std::string_view> f = SplitTabs(head);
            if (f.size() != 3 || !FamilyName(f[0]) || !f[1].starts_with("after ") || f[1].size() == 6 || f[2].empty()) {
                fail(number, "a family line is //= FAMILY <TAB> after NAME <TAB> what it is for");
            } else if (!families.emplace(f[0], t.families.size()).second) {
                fail(number, "a second line for the family " + std::string(f[0]));
            } else {
                t.families.push_back({std::string(f[0]), std::string(f[1].substr(6)), {}, -1});
            }
            continue;
        }
        if (line.starts_with("//")) continue;
        const std::vector<std::string_view> f = SplitTabs(line);
        if (f.size() != 5 || f[2].empty() || f[3].empty()) {
            fail(number, "a row is ID <TAB> SPEC <TAB> CONDITION <TAB> ACTIONS <TAB> NOTE");
            continue;
        }
        const std::string_view id = f[0];
        const std::size_t colon = id.rfind(':');
        int prio = 0;
        if (!id.starts_with("cx:") || colon <= 3 || !rules::ParseInt(id.substr(colon + 1), 0, 1 << 20, prio) ||
            id[colon + 1] < '0' || id[colon + 1] > '9') {
            fail(number, "an id is cx:FAMILY:PRIO");
            continue;
        }
        const auto family = families.find(id.substr(3, colon - 3));
        if (family == families.end()) {
            fail(number, "no family line for " + std::string(id));
            continue;
        }
        if (std::ranges::find(ids, id) != ids.end()) {
            fail(number, "a second row " + std::string(id));
            continue;
        }
        CxRow row{std::string(id), std::string(f[1]), prio, static_cast<uint32_t>(t.nodes.size()), {}};
        rules::Tokens tokens(f[2]);
        CxSink sink(t);
        if (!rules::ParseBody(tokens, sink, true) || !tokens.AtEnd()) {
            fail(number, std::string("condition: ") + (tokens.error != nullptr ? tokens.error : "trailing tokens"));
            continue;
        }
        const char* error = nullptr;
        if (!ParseActions(f[3], row.actions, error)) {
            fail(number, std::string("actions: ") + error);
            continue;
        }
        ids.emplace_back(id);
        t.families[family->second].rows.push_back(static_cast<int>(t.rows.size()));
        t.rows.push_back(std::move(row));
    }
    t.afterPass.assign(passes.size(), -1);
    std::vector<bool> placed(t.families.size());
    for (std::size_t k = 0; k < t.families.size(); k++) {
        CxFamily& c = t.families[k];
        if (c.rows.empty()) fail(0, "no rows for the family " + c.name);
        std::ranges::stable_sort(c.rows, {}, [&](int r) { return t.rows[static_cast<std::size_t>(r)].prio; });
        for (std::size_t i = 1; i < c.rows.size(); i++) {
            if (t.rows[static_cast<std::size_t>(c.rows[i])].prio == t.rows[static_cast<std::size_t>(c.rows[i - 1])].prio) {
                fail(0, "two rows of " + c.name + " share a PRIO");
            }
        }
        int* slot = nullptr;
        if (const auto pass = std::ranges::find(passes, c.after); pass != passes.end()) {
            slot = &t.afterPass[static_cast<std::size_t>(pass - passes.begin())];
        } else if (const auto anchor = families.find(c.after); anchor != families.end()) {
            slot = &t.families[anchor->second].next;
        } else {
            fail(0, "the family " + c.name + " runs after " + c.after + ", which is no pass or family");
            continue;
        }
        if (*slot >= 0) {
            fail(0, "two families run after " + c.after + "; order them with after");
            continue;
        }
        *slot = static_cast<int>(k);
        placed[k] = true;
    }
    // Every placed family is reached from a pass, not from a cycle of families.
    std::vector<bool> reached(t.families.size());
    for (const int first : t.afterPass) {
        for (int k = first; k >= 0 && !reached[static_cast<std::size_t>(k)]; k = t.families[static_cast<std::size_t>(k)].next) {
            reached[static_cast<std::size_t>(k)] = true;
        }
    }
    for (std::size_t k = 0; k < t.families.size(); k++) {
        if (placed[k] && !reached[k]) fail(0, "the family " + t.families[k].name + " is not reached from a pass");
    }
    return t;
}

const ConstructionTable& BuiltinConstructions() {
    static const ConstructionTable table = [] {
        for (const dict::internal::BuiltinTable& b : dict::internal::BuiltinTables()) {
            if (b.name == "constructions.tsv") return ParseConstructions(b.lines, TextPassNames());
        }
        return ConstructionTable{};
    }();
    return table;
}

std::vector<std::string_view> ConstructionFeatures() {
    std::vector<std::string_view> out;
    for (const NamedFeature& n : kFeatures) out.push_back(n.name);
    return out;
}

}  // namespace internal

namespace {

using internal::ConstructionTable;
using internal::CxAtom;
using internal::CxField;
using internal::CxMatch;
using internal::CxNode;

const Item* Neighbour(const Item* it, int delta) {
    for (; it != nullptr && delta > 0; delta--) it = it->next;
    for (; it != nullptr && delta < 0; delta++) it = it->prev;
    return it;
}

bool MatchesValue(const CxAtom& a, std::string_view v) {
    switch (a.match) {
        case CxMatch::Literal:
            // "~…" holds the value somewhere; the rest are the markers of the rules.
            if (a.value.size() > 1 && a.value[0] == '~') return Has(v, std::string_view(a.value).substr(1));
            return rules::MatchRuleSurface(v, a.value);
        case CxMatch::Member: return a.set->index.contains(v);
        case CxMatch::MemberStart:
            return std::ranges::any_of(a.set->members, [&](std::string_view m) { return v.starts_with(m); });
        case CxMatch::MemberEnd:
            return std::ranges::any_of(a.set->members, [&](std::string_view m) { return v.ends_with(m); });
        case CxMatch::Feature: break;
    }
    return false;
}

// Whether the atom holds at it; an atom whose item or ending is missing is false, negated or not.
bool Holds(Flusher& f, const Item* origin, const CxAtom& a) {
    const Item* it = Neighbour(origin, a.delta);
    if (it == nullptr) return false;
    bool v = false;
    switch (a.field) {
        case CxField::Surface: v = MatchesValue(a, it->group->surface); break;
        case CxField::Lemma: v = MatchesValue(a, ItemLemma(f, it, it->selected)); break;
        case CxField::Target: v = MatchesValue(a, it->EntryText(it->selected)); break;
        case CxField::Part: v = rules::MatchRuleCode(it->Code(it->selected), a.value); break;
        case CxField::Sem: v = rules::AttrChunksIn(it->Attr(it->selected), a.value); break;
        case CxField::Class: {
            const Entry* e = it->group->At(it->selected);
            if (e == nullptr) return false;
            v = e->cls == a.value[0];
            break;
        }
        case CxField::EndingSurface:
            if (it->DirectIndex() < 0) return false;
            v = MatchesValue(a, it->DirectSurface());
            break;
        case CxField::EndingTarget: {
            const Entry* e = it->DirectEntry(it->DirectIndex());
            if (e == nullptr) return false;
            v = MatchesValue(a, e->dst);
            break;
        }
        case CxField::Word:
            v = a.set != nullptr ? IsAny(it, a.set->members) : IsWord(it, a.value);
            break;
        case CxField::Text: v = MatchesValue(a, Slice(it->text, it->start, it->length)); break;
        case CxField::Head: v = MatchesValue(a, Slice(it->text, 0, it->start)); break;
        case CxField::Feature: v = a.feature(f, it); break;
    }
    return v != a.negated;
}

bool Eval(Flusher& f, const ConstructionTable& t, uint32_t i, const Item* it) {
    const CxNode& n = t.nodes[i];
    if (n.atom) return Holds(f, it, t.atoms[n.count]);
    switch (n.kind) {
        case rules::BodyKind::Not: return !Eval(f, t, i + 1, it);
        case rules::BodyKind::ScanLeft:
        case rules::BodyKind::ScanRight: {
            const uint32_t target = i + 1;
            const uint32_t barrier = t.nodes[target].end;
            const int step = n.kind == rules::BodyKind::ScanLeft ? -1 : 1;
            int k = 0;
            for (const Item* x = Neighbour(it, step); x != nullptr && (n.count == 0 || k < static_cast<int>(n.count));
                 x = Neighbour(x, step), k++) {
                if (Eval(f, t, target, x)) return true;
                if (Eval(f, t, barrier, x)) return false;
            }
            return false;
        }
        case rules::BodyKind::Or:
        case rules::BodyKind::And:
        case rules::BodyKind::AndAll: break;
    }
    bool all = true;
    uint32_t child = i + 1;
    for (uint32_t k = 0; k < n.count; k++) {
        const bool v = Eval(f, t, child, it);
        if (n.kind == rules::BodyKind::Or && v) return true;
        if (n.kind == rules::BodyKind::And && !v) return false;
        all = all && v;
        child = t.nodes[child].end;
    }
    return n.kind != rules::BodyKind::Or && all;
}

void Apply(Flusher& f, Item* origin, const internal::CxAction& a, Selection source) {
    Item* it = origin;
    for (int d = a.delta; it != nullptr && d > 0; d--) it = it->next;
    for (int d = a.delta; it != nullptr && d < 0; d++) it = it->prev;
    if (it == nullptr) return;
    switch (a.verb) {
        case internal::CxVerb::SelectLemma: {
            const int count = static_cast<int>(it->group->entries.size());
            for (int slot = 0; slot < count; slot++) {
                if (ItemLemma(f, it, slot) == a.value) {
                    it->SelectCandidate(slot, source);
                    break;
                }
            }
            break;
        }
        case internal::CxVerb::TakeTarget: TakeTarget(it, At(it->Code(it->selected), 0), a.value, source); break;
        case internal::CxVerb::SetTarget: it->AssignEntrySurface(it->selected, a.value, source); break;
        case internal::CxVerb::SetEnding: it->AssignDirectSlot(it->DirectIndex(), a.value, source); break;
        case internal::CxVerb::ReplaceHead: {
            const std::string_view target = it->EntryText(it->selected);
            if (target.starts_with(a.value)) {
                it->AssignEntrySurface(it->selected, a.with + std::string(target.substr(a.value.size())), source);
            }
            break;
        }
    }
}

}  // namespace

void RunConstructions(Flusher& f, std::size_t pass, bool (*skip)(std::string_view name)) {
    const ConstructionTable& t = internal::BuiltinConstructions();
    if (pass >= t.afterPass.size()) return;
    rules::Tally* tally = f.w.settings.ruleTally;
    if (tally != nullptr && tally->constructionRuns.size() < t.rows.size()) {
        tally->constructionRuns.resize(t.rows.size());
        tally->constructionFires.resize(t.rows.size());
    }
    for (int k = t.afterPass[pass]; k >= 0; k = t.families[static_cast<std::size_t>(k)].next) {
        const internal::CxFamily& family = t.families[static_cast<std::size_t>(k)];
        if (skip != nullptr && skip(family.name)) continue;
        for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
            for (const int r : family.rows) {
                const internal::CxRow& row = t.rows[static_cast<std::size_t>(r)];
                if (skip != nullptr && skip(row.id)) continue;
                const bool fired = Eval(f, t, row.root, it);
                if (tally != nullptr) {
                    tally->constructionRuns[static_cast<std::size_t>(r)]++;
                    if (fired) tally->constructionFires[static_cast<std::size_t>(r)]++;
                }
                if (!fired) continue;
                for (const internal::CxAction& a : row.actions) Apply(f, it, a, {Provenance::Rewrite, r});
                break;
            }
        }
    }
}

std::string_view ConstructionId(int index) {
    const ConstructionTable& t = internal::BuiltinConstructions();
    if (index < 0 || static_cast<std::size_t>(index) >= t.rows.size()) return {};
    return t.rows[static_cast<std::size_t>(index)].id;
}

}  // namespace hdTrans::word
