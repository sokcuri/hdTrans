#include "core/word/constructions.h"

#include <algorithm>
#include <span>
#include <string>
#include <unordered_map>

#include "core/dict/builtin.h"
#include "core/dict/classes.h"
#include "core/dict/codes.h"
#include "core/output/compose.h"
#include "core/output/korean.h"
#include "core/output/particle.h"
#include "core/output/inflect.h"
#include "core/rules/match.h"
#include "core/text/kana.h"
#include "core/text/unicode.h"
#include "core/tokenizer/classify.h"
#include "core/tokenizer/token.h"
#include "core/word/flush.h"
#include "core/word/util.h"

namespace hdTrans::word {

namespace internal {

namespace {

// Whether the word is only marks that may stand between copies (pause.copy: 、).
bool CopyPause(const Item* it) {
    const std::string_view text = Slice(it->text, it->start, it->length);
    for (std::string_view t = text; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (!dict::InClass("pause.copy", t.substr(0, r.size))) return false;
        t.remove_prefix(r.size);
    }
    return !text.empty();
}

// The word before it that it repeats: the same text with the same ending, of two
// characters or more, in the same token with nothing or a CopyPause word between (いや|いや,
// 待って、待って).
const Item* CopyBefore(const Item* it) {
    const Item* p = it->prev;
    if (p != nullptr && p->tokenIndex == it->tokenIndex && CopyPause(p)) p = p->prev;
    if (p == nullptr || p->tokenIndex != it->tokenIndex) return nullptr;
    const std::string_view text = Slice(it->text, it->start, it->length);
    if (text::RuneCount(text) < 2 || Slice(p->text, p->start, p->length) != text) return nullptr;
    return p;
}

}  // namespace

// The word after it that repeats it.
const Item* CopyAfter(const Item* it) {
    const Item* n = it->next;
    if (n != nullptr && n->tokenIndex == it->tokenIndex && CopyPause(n)) n = n->next;
    return n != nullptr && CopyBefore(n) != nullptr ? n : nullptr;
}

namespace {

// The last copy of the run that it starts or goes on.
const Item* LastCopy(const Item* it) {
    const Item* last = it;
    while (const Item* n = CopyAfter(last)) last = n;
    return last;
}

// Whether the copy after it follows it with no pause between (いやだいやだ, not 好き、好き).
bool CopiedStraight(const Item* it) {
    const Item* n = CopyAfter(it);
    return n != nullptr && it->next == n;
}

// Whether the copies of its run are the last predicate of their sentence: no word after the
// last copy conjugates and writes something, except one that modifies the noun after it
// (見て見て可愛いお菓子).
bool CopiesEnd(const Item* it) {
    for (const Item* n = LastCopy(it)->next; n != nullptr; n = n->next) {
        const Item* after = n->next;
        const bool modifies = after != nullptr && after->selected >= 0 && At(after->Code(after->selected), 0) == '1' &&
                              tokenizer::Class(text::DecodeRune(after->group->surface).cp) != 0;
        if (n->detail->Yougo() && n->selected >= 0 && !n->EntryText(n->selected).empty() && !modifies) return false;
    }
    return true;
}

// Whether the last copy of its run ends in a connective of form 6 (歩いて 걸어, 嬉しくて 기뻐서;
// not an imperative of the ; class, 寝ろ 자라).
bool ConnectiveCopies(const Item* it) {
    const Item* last = LastCopy(it);
    const int from = last->DirectIndex();
    return it->detail->Yougo() && from >= 0 && last->DirectText(from).starts_with("6") &&
           last->DirectCode(from).find(';') == std::string_view::npos;
}

bool Exclaimed(Flusher& f, const Item* it) {
    const Item* tail = it;
    while (tail->next != nullptr) tail = tail->next;
    // The mark is the last word of the sentence, or the token after it.
    for (std::size_t j = static_cast<std::size_t>(tail->tokenIndex); j <= static_cast<std::size_t>(tail->tokenIndex) + 1; j++) {
        if (j >= f.w.tokens.size()) break;
        const std::string_view mark = f.w.tokens.Canonical(j);
        if (mark.starts_with("!") || mark.starts_with("?")) return true;
    }
    return false;
}

// The size of the mark of marks that starts s, 0 for none.
std::size_t MarkAt(std::string_view s, std::span<const std::string_view> marks) {
    for (const std::string_view m : marks) {
        if (s.starts_with(m)) return m.size();
    }
    return 0;
}

// Whether s holds only marks of a pause and spaces.
bool OnlyPauses(std::string_view s) {
    while (!s.empty()) {
        std::size_t n = MarkAt(s, dict::ClassMembers("mark.pause"));
        if (n == 0 && text::IsSpace(text::DecodeRune(s).cp)) n = text::DecodeRune(s).size;
        if (n == 0) return false;
        s.remove_prefix(n);
    }
    return true;
}

// Whether the speech goes on after the item, past pause marks and spaces, within its sentence
// or past a ！ or ？ that ends it inside a quote the next sentence goes on: a word follows that
// is not a closing bracket (……そうだ、ほら, 「……そうだ！　田中さん」; not ……そうだ。 間違いない,
// ……そうだ！ 間違いない outside quotes, 「……そうだ」 or an unclosed 「……そうだ at the end of the text).
bool SpeechGoesOn(Worker& w, const Item* it) {
    const std::string_view rest = From(it->text, it->start + it->length);
    if (!rest.empty() && !OnlyPauses(rest)) return MarkAt(rest, dict::ClassMembers("mark.close-after-space")) == 0;
    bool exclaimed = false;
    for (int j = it->tokenIndex + 1; j < w.TokenCount(); j++) {
        const int type = w.Tok(j).type;
        if (type == tokenizer::kTypeEnd) return exclaimed && w.QuoteDepth(j) > 0;
        if (type == tokenizer::kTypeNewline) return false;
        if (type == tokenizer::kTypeSentenceEnd || type == tokenizer::kTypeAsciiPunct) {
            const std::string_view mark = w.tokens.Canonical(static_cast<std::size_t>(j));
            if (!mark.starts_with("!") && !mark.starts_with("?")) return false;
            exclaimed = true;
            continue;
        }
        const std::string_view raw = w.tokens.Raw(static_cast<std::size_t>(j));
        if (!OnlyPauses(raw)) {
            return (!exclaimed || w.QuoteDepth(j) > 0) && MarkAt(raw, dict::ClassMembers("mark.close-after-space")) == 0;
        }
    }
    return false;
}

bool Narrated(Flusher& f, const Item* it) {
    if (it->InQuote()) return false;
    const Item* tail = it;
    while (tail->next != nullptr) tail = tail->next;
    const auto j = static_cast<std::size_t>(tail->tokenIndex + 1);
    if (j >= f.w.tokens.size()) return false;
    const std::string_view mark = f.w.tokens.Canonical(j);
    return !mark.empty() && text::IsPunct(text::DecodeRune(mark).cp) && f.w.tokens.tokens[j].type == tokenizer::kTypeSymbol;
}

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
    {"last", [](Flusher&, const Item* it) { return it->After() == nullptr; }},
    {"verb", [](Flusher&, const Item* it) { return it->detail->cls == kDetailVerb; }},
    {"boundary", [](Flusher&, const Item* it) { return PauseOrEnd(it); }},
    // The Korean of the next word starts with this word's Korean and a space.
    {"lemma-starts-next",
     [](Flusher& f, const Item* it) {
         const std::string lemma = ItemLemma(f, it, it->selected);
         const Item* n = it->After();
         return !lemma.empty() && n != nullptr && ItemLemma(f, n, n->selected).starts_with(lemma + " ");
     }},
    {"opens-line", [](Flusher& f, const Item* it) { return OpensLine(f.w, it); }},
    {"after-restart", [](Flusher& f, const Item* it) { return AfterRestart(f.w, it); }},
    // An ellipsis or a dash stands right before the word (……そうだ).
    {"after-pause", [](Flusher&, const Item* it) { return it->AfterPause(false); }},
    {"number", [](Flusher&, const Item* it) { return NumberSurface(it->group->surface); }},
    {"quote-verb", [](Flusher&, const Item* it) { return QuoteVerbFollows(it); }},
    // A fixed word span that the phrase or sound stage already read (an utterance in brackets).
    {"word-span", [](Flusher& f, const Item* it) { return it->Fixed() && f.w.Span(it->tokenIndex) == tokenizer::kSpanWord; }},
    // The sense table read the word as an adjectival noun (senses.tsv, adjective).
    {"adjectival", [](Flusher&, const Item* it) { return it->adjectival; }},
    // The word repeats the word before it (CopyBefore), or the word after it repeats it.
    {"copy", [](Flusher&, const Item* it) { return CopyBefore(it) != nullptr; }},
    {"copied", [](Flusher&, const Item* it) { return CopyAfter(it) != nullptr; }},
    // The copy after it follows with no pause (CopiedStraight); the copies are the last
    // predicate of their sentence (CopiesEnd); they end in the connective form 6 (ConnectiveCopies).
    {"copied-straight", [](Flusher&, const Item* it) { return CopiedStraight(it); }},
    {"copies-end", [](Flusher&, const Item* it) { return CopiesEnd(it); }},
    {"connective-copies", [](Flusher&, const Item* it) { return ConnectiveCopies(it); }},
    // The sentence of the word ends in ! or ? (an utterance, not a narrated sentence).
    {"exclaimed", [](Flusher& f, const Item* it) { return Exclaimed(f, it); }},
    // The sentence of the word trails off in narration: outside quotes, it ends in an ellipsis
    // (a て form that ends a sentence with a period is a request: え、待って待って。).
    {"narrated", [](Flusher& f, const Item* it) { return Narrated(f, it); }},
    // A digit ends the text before the word in its token (3から).
    {"after-digit", [](Flusher&, const Item* it) { return text::IsDigit(DecodeLastRune(Slice(it->text, 0, it->start)).cp); }},
    // The selected candidate has a sem code of a person, an animal or a god (Person).
    {"person", [](Flusher&, const Item* it) { return Person(it->Attr(it->selected)); }},
    // The verb was analysed as a causative.
    {"causative", [](Flusher&, const Item* it) { return it->detail->record == 2; }},
    // The verb was analysed as a passive that no Korean passive word has taken over yet (撫でられ).
    {"passive", [](Flusher&, const Item* it) { return it->DetailRecord() == 1 && !it->lexical; }},
    // The verb was analysed as a causative-passive (待たされ, 読ませられ).
    {"causative-passive", [](Flusher&, const Item* it) { return it->DetailRecord() == 5; }},
    // The word starts where the word before it ends: in the same token, or at the start of the next
    // token when the word before ends its token.
    {"touches-prev",
     [](Flusher&, const Item* it) {
         const Item* p = it->prev;
         if (p == nullptr) return false;
         const int end = p->start + p->length;
         return (it->tokenIndex == p->tokenIndex && it->start == end) ||
                (it->tokenIndex == p->tokenIndex + 1 && end == static_cast<int>(p->text.size()) && it->start == 0);
     }},
    // The surface starts with a letter or a digit (Unicode L or Nd).
    {"alnum-start",
     [](Flusher&, const Item* it) {
         const char32_t r = text::DecodeRune(it->group->surface).cp;
         return text::IsLetter(r) || text::IsDigit(r);
     }},
    // The surface is katakana only, with ー (KatakanaSurface).
    {"katakana", [](Flusher&, const Item* it) { return KatakanaSurface(it->group->surface); }},
    // The Korean of the selected candidate is empty or spaces.
    {"blank", [](Flusher&, const Item* it) { return TrimSpace(it->EntryText(it->selected)).empty(); }},
    // The Korean lemma of the selected candidate ends in a final consonant (어떤 것, 책).
    {"final-consonant",
     [](Flusher& f, const Item* it) {
         const char c = output::ParticleClass(ItemLemma(f, it, it->selected));
         return c == 'c' || c == 'l';
     }},
    // The Korean of the selected candidate is in a copula family (큰일$AH@, output::CopulaTarget).
    {"copula-target", [](Flusher& f, const Item* it) { return output::CopulaTarget(it->EntryText(it->selected), f.families); }},
    // The word is a みたい that can follow a clause (Item::MitaiLike).
    {"mitai-like", [](Flusher&, const Item* it) { return it->MitaiLike(); }},
    // A contrast goes on after the stem: ではないが (ContrastFollows).
    {"contrast", [](Flusher&, const Item* it) { return ContrastFollows(it); }},
    // The item ends the utterance or the quote (QuoteEnd); UtteranceEnd also passes over a comma,
    // a dash or a small つ.
    {"quote-end", [](Flusher& f, const Item* it) { return QuoteEnd(f.w, it); }},
    {"utterance-end", [](Flusher& f, const Item* it) { return UtteranceEnd(f.w, it); }},
    // More words follow on the line past the end of the sentence (SpeechGoesOn).
    {"speech-goes-on", [](Flusher& f, const Item* it) { return SpeechGoesOn(f.w, it); }},
    // The item ends its sentence: the end of the utterance, or ！？!? after it (SentenceEnd).
    {"sentence-end", [](Flusher& f, const Item* it) { return SentenceEnd(f.w, it); }},
    // A verb follows in the sentence, not an auxiliary of the て form (PredicateFollows).
    {"predicate-follows", [](Flusher&, const Item* it) { return PredicateFollows(it); }},
    // The verb was analysed with no passive, potential or causative.
    {"plain-voice", [](Flusher&, const Item* it) { return it->detail->record == 0; }},
    // A noun that the word describes follows it in its token (DescribedNoun).
    {"described-noun", [](Flusher& f, const Item* it) { return DescribedNoun(f.w, it); }},
    // The Korean lemma is one of ko-verb.holding or a phrase that ends in one (이를 악물다).
    {"holding-verb",
     [](Flusher& f, const Item* it) {
         const std::string lemma = ItemLemma(f, it, it->selected);
         return std::ranges::any_of(dict::ClassMembers("ko-verb.holding"), [&](std::string_view v) {
             return lemma == v || (lemma.ends_with(v) && std::string_view(lemma).substr(0, lemma.size() - v.size()).ends_with(" "));
         });
     }},
    // The surface holds a hiragana.
    {"hiragana",
     [](Flusher&, const Item* it) {
         for (std::string_view t = it->group->surface; !t.empty();) {
             const text::Rune r = text::DecodeRune(t);
             if (tokenizer::ClassFlags(r.cp, 2) == tokenizer::kClassHiragana) return true;
             t.remove_prefix(r.size);
         }
         return false;
     }},
    // The conjugation of a verb in the potential (潜れる, 使える).
    {"potential", [](Flusher&, const Item* it) { return it->DetailRecord() == 4; }},
    // The Korean of the chosen candidate is an adjective or 있다, 없다 (배고프다, 효과가 있다): a
    // marker of an adjective family, of a family whose declarative is bare, or a stem that ends like
    // an adjective of ko-adjective.stem-end.
    {"ko-state",
     [](Flusher&, const Item* it) {
         namespace oi = output::internal;
         const std::string_view t = it->EntryText(it->selected);
         if (t.size() < 4 || t.back() != '@') return false;
         const oi::KoreanFamily* k = oi::FindKoreanFamily(t.substr(t.size() - 4));
         if (k == nullptr) return false;
         if (k->partOfSpeech == oi::KoreanPartOfSpeech::Adjective || k->inflectionClass == oi::InflectionClass::Existential ||
             (k->flags & oi::kBareDeclarative) != 0) {
             return true;
         }
         const std::string stem = std::string(t.substr(0, t.size() - 4)) + std::string(k->stem);
         for (const std::string_view end : dict::ClassMembers("ko-adjective.stem-end")) {
             if (stem.ends_with(end)) return true;
         }
         return false;
     }},
    // A vowel kana of mark.drawn follows the word (燃えるねぇ, 言うねェ).
    {"drawn",
     [](Flusher&, const Item* it) {
         std::string_view rest = From(it->text, it->start + it->length);
         if (rest.empty()) rest = it->detail->nextText;
         return dict::InClass("mark.drawn", rest.substr(0, text::DecodeRune(rest).size));
     }},
    // An exclamation mark of mark.exclaim is one of the two characters after the word
    // (待ち伏せますか！, ますか？！, ますかっ！).
    {"exclaim-after",
     [](Flusher&, const Item* it) {
         std::string_view rest = From(it->text, it->start + it->length);
         if (rest.empty()) rest = it->detail->nextText;
         for (int n = 0; n < 2 && !rest.empty(); n++) {
             const std::size_t size = text::DecodeRune(rest).size;
             if (size == 0) break;
             if (dict::InClass("mark.exclaim", rest.substr(0, size))) return true;
             rest.remove_prefix(size);
         }
         return false;
     }},
    // The text after the word, past white space, starts with one of mark.prompt-follow, which shows
    // a question as a prompt on screen: a quoting と, a closing 》 or the answers (開始しますか？という,
    // はい／いいえ, （はい／いいえ）), or holds one of the answers of mark.prompt-answer (乗るやめる).
    {"prompt-after",
     [](Flusher&, const Item* it) {
         std::string_view rest = From(it->text, it->start + it->length);
         if (rest.empty()) rest = it->detail->nextText;
         rest = TrimSpace(rest);
         return std::ranges::any_of(dict::ClassMembers("mark.prompt-follow"),
                                    [&](std::string_view m) { return rest.starts_with(m); }) ||
                std::ranges::any_of(dict::ClassMembers("mark.prompt-answer"),
                                    [&](std::string_view m) { return rest.find(m) != std::string_view::npos; });
     }},
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
    {'C', CxField::Candidate}, {'G', CxField::Gram},        {'O', CxField::FirstWord},
    {'T', CxField::Tail},
};

bool SetField(CxField field) {
    return field == CxField::Surface || field == CxField::Lemma || field == CxField::Target ||
           field == CxField::EndingSurface || field == CxField::EndingTarget || field == CxField::Text ||
           field == CxField::Head || field == CxField::Word || field == CxField::Candidate || field == CxField::FirstWord ||
           field == CxField::Tail;
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
            if (a.field == CxField::Gram && (raw.empty() || raw.size() % 2 != 0)) return tokens.Fail("G takes two-letter codes");
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

bool ParseActions(std::string_view text, const std::vector<CxMap>& maps, std::vector<CxAction>& out, const char*& error) {
    constexpr std::pair<std::string_view, CxVerb> kVerbs[] = {
        {"select-lemma", CxVerb::SelectLemma},
        {"select-target", CxVerb::SelectTarget},
        {"take-target", CxVerb::TakeTarget},
        {"set-target", CxVerb::SetTarget},
        {"append-target", CxVerb::AppendTarget},
        {"set-ending", CxVerb::SetEnding},
        {"replace-head", CxVerb::ReplaceHead},
        {"replace-ending-tail", CxVerb::ReplaceEndingTail},
        {"map-lemma", CxVerb::MapLemma},
        {"map-target", CxVerb::MapTarget},
        {"select-map", CxVerb::SelectMap},
        {"set-verb", CxVerb::SetVerb},
        {"copy-ending", CxVerb::CopyEnding},
        {"copy-word", CxVerb::CopyWord},
        {"set-sound", CxVerb::SetSound},
        {"set-active", CxVerb::SetActive},
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
        if (a.verb == CxVerb::MapLemma || a.verb == CxVerb::MapTarget || a.verb == CxVerb::SelectMap) {
            if (!tokens.Next(literal) || !literal.starts_with('@')) {
                tokens.Fail("a map action takes @MAP");
                break;
            }
            const auto map = std::ranges::find(maps, literal.substr(1), &CxMap::name);
            if (map == maps.end()) {
                tokens.Fail("no such map");
                break;
            }
            a.map = static_cast<int>(map - maps.begin());
            out.push_back(std::move(a));
            continue;
        }
        const char* bad = nullptr;
        if (a.verb == CxVerb::CopyEnding || a.verb == CxVerb::CopyWord || a.verb == CxVerb::SetSound ||
            a.verb == CxVerb::SetActive) {
            out.push_back(std::move(a));
            continue;
        }
        if (!tokens.Next(literal) || !rules::AppendLiteral(literal, a.value, bad)) {
            tokens.Fail(bad != nullptr ? bad : "an action takes a string literal");
            break;
        }
        if ((a.verb == CxVerb::ReplaceHead || a.verb == CxVerb::ReplaceEndingTail) &&
            (!tokens.Next(literal) || !rules::AppendLiteral(literal, a.with, bad))) {
            tokens.Fail(bad != nullptr ? bad : "a replace action takes the old and the new text");
            break;
        }
        out.push_back(std::move(a));
    }
    error = tokens.error;
    if (error == nullptr && out.empty()) error = "no action";
    return error == nullptr;
}

// Adds to keys the surfaces one of which the word must have for the condition at node i to hold;
// false when it needs none: an atom S+0== with an exact surface or a class, a list && or & with
// such an operand (the one with the fewest), or a list || of such operands only.
bool SurfaceKeys(const ConstructionTable& t, uint32_t i, std::vector<std::string_view>& keys) {
    const CxNode& n = t.nodes[i];
    if (n.atom) {
        const CxAtom& a = t.atoms[n.count];
        if (a.field != CxField::Surface || a.delta != 0 || a.negated) return false;
        if (a.match == CxMatch::Member) {
            keys.insert(keys.end(), a.set->members.begin(), a.set->members.end());
            return true;
        }
        if (a.match != CxMatch::Literal) return false;
        const std::string_view v = a.value;
        if (v.starts_with("99")) {
            keys.push_back(v.substr(2));
            return true;
        }
        for (const std::string_view marker : {"L", "R", "11", "22", "00"}) {
            if (v.starts_with(marker)) return false;
        }
        if (v.size() > 1 && v[0] == '~') return false;
        keys.push_back(v);
        return true;
    }
    if (n.kind != rules::BodyKind::Or && n.kind != rules::BodyKind::And && n.kind != rules::BodyKind::AndAll) return false;
    bool found = false;
    std::vector<std::string_view> best, own;
    uint32_t child = i + 1;
    for (uint32_t k = 0; k < n.count; k++, child = t.nodes[child].end) {
        own.clear();
        const bool has = SurfaceKeys(t, child, own);
        if (n.kind == rules::BodyKind::Or) {
            if (!has) return false;
            best.insert(best.end(), own.begin(), own.end());
            found = true;
        } else if (has && (!found || own.size() < best.size())) {
            best = own;
            found = true;
        }
    }
    if (found) keys.insert(keys.end(), best.begin(), best.end());
    return found;
}

// Fills the surface index of family c.
void IndexFamily(const ConstructionTable& t, CxFamily& c) {
    std::vector<std::vector<std::string_view>> keys(c.rows.size());
    std::vector<bool> keyed(c.rows.size());
    for (std::size_t k = 0; k < c.rows.size(); k++) {
        keyed[k] = SurfaceKeys(t, t.rows[static_cast<std::size_t>(c.rows[k])].root, keys[k]);
        if (!keyed[k]) c.open.push_back(c.rows[k]);
        c.indexed = c.indexed || keyed[k];
    }
    for (std::size_t k = 0; k < c.rows.size(); k++) {
        for (const std::string_view key : keys[k]) {
            if (c.bySurface.contains(key)) continue;
            std::vector<int>& rows = c.bySurface[std::string(key)];
            for (std::size_t j = 0; j < c.rows.size(); j++) {
                if (!keyed[j] || std::ranges::find(keys[j], key) != keys[j].end()) rows.push_back(c.rows[j]);
            }
        }
    }
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
                t.families.push_back({std::string(f[0]), std::string(f[1].substr(6)), {}, -1, number, {}, {}, false});
            }
            continue;
        }
        if (line.starts_with("//~")) {
            std::string_view head = line.substr(3);
            while (head.starts_with(' ')) head.remove_prefix(1);
            const std::vector<std::string_view> f = SplitTabs(head);
            CxMap map{f.empty() ? std::string() : std::string(f[0]), {}};
            bool ok = f.size() == 3 && FamilyName(f[0]) && !f[1].empty() && !f[2].empty();
            for (std::string_view rest = ok ? f[1] : std::string_view(); ok && !rest.empty();) {
                const std::size_t space = rest.find(' ');
                const std::string_view pair = rest.substr(0, space);
                const std::size_t eq = pair.find('=');
                ok = eq != 0 && eq != std::string_view::npos && eq + 1 < pair.size() &&
                     std::ranges::find(map.pairs, pair.substr(0, eq), &std::pair<std::string, std::string>::first) == map.pairs.end();
                if (ok) map.pairs.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
                rest = space == std::string_view::npos ? std::string_view() : rest.substr(space + 1);
                ok = ok && (space == std::string_view::npos || !rest.empty());
            }
            if (!ok) {
                fail(number, "a map line is //~ MAP <TAB> FROM=TO ... <TAB> what it is for, each FROM once");
            } else if (std::ranges::find(t.maps, map.name, &CxMap::name) != t.maps.end()) {
                fail(number, "a second line for the map " + map.name);
            } else {
                t.maps.push_back(std::move(map));
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
        CxRow row{std::string(id), std::string(f[1]), prio, static_cast<uint32_t>(t.nodes.size()), {}, number};
        rules::Tokens tokens(f[2]);
        CxSink sink(t);
        if (!rules::ParseBody(tokens, sink, true) || !tokens.AtEnd()) {
            fail(number, std::string("condition: ") + (tokens.error != nullptr ? tokens.error : "trailing tokens"));
            continue;
        }
        const char* error = nullptr;
        if (!ParseActions(f[3], t.maps, row.actions, error)) {
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
        if (c.rows.empty()) fail(c.line, "no rows for the family " + c.name);
        std::ranges::stable_sort(c.rows, {}, [&](int r) { return t.rows[static_cast<std::size_t>(r)].prio; });
        for (std::size_t i = 1; i < c.rows.size(); i++) {
            const CxRow& r = t.rows[static_cast<std::size_t>(c.rows[i])];
            if (r.prio == t.rows[static_cast<std::size_t>(c.rows[i - 1])].prio) {
                fail(r.line, "two rows of " + c.name + " share a PRIO");
            }
        }
        IndexFamily(t, c);
        int* slot = nullptr;
        if (const auto pass = std::ranges::find(passes, c.after); pass != passes.end()) {
            slot = &t.afterPass[static_cast<std::size_t>(pass - passes.begin())];
        } else if (const auto anchor = families.find(c.after); anchor != families.end()) {
            slot = &t.families[anchor->second].next;
        } else {
            fail(c.line, "the family " + c.name + " runs after " + c.after + ", which is no pass or family");
            continue;
        }
        if (*slot >= 0) {
            fail(c.line, "two families run after " + c.after + "; order them with after");
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
        if (placed[k] && !reached[k]) fail(t.families[k].line, "the family " + t.families[k].name + " is not reached from a pass");
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

}  // namespace internal

namespace {

using internal::ConstructionTable;
using internal::CxAtom;
using internal::CxField;
using internal::CxMatch;
using internal::CxNode;

// The item delta items away; rightwards from a ruby base, past its reading (Item::Object).
template <typename T>
T* Neighbour(T* it, int delta) {
    const bool base = it != nullptr && it->w != nullptr && it->w->RubyPart(it->tokenIndex) == 1;
    for (; it != nullptr && delta > 0; delta--) it = base ? it->After() : it->next;
    for (; it != nullptr && delta < 0; delta++) it = it->prev;
    return it;
}

// Whether v matches the literal of a condition: "~…" holds the value somewhere; the rest are the
// surface markers of the rules.
bool MatchesLiteral(std::string_view v, std::string_view literal) {
    if (literal.size() > 1 && literal[0] == '~') return Has(v, literal.substr(1));
    return rules::MatchRuleSurface(v, literal);
}

bool MatchesValue(const CxAtom& a, std::string_view v) {
    switch (a.match) {
        case CxMatch::Literal: return MatchesLiteral(v, a.value);
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
        case CxField::Tail: v = MatchesValue(a, From(it->text, it->start + it->length)); break;
        case CxField::Feature: v = a.feature(f, it); break;
        case CxField::Candidate: {
            const int count = static_cast<int>(it->group->entries.size());
            for (int slot = 0; slot < count && !v; slot++) v = MatchesValue(a, ItemLemma(f, it, slot));
            break;
        }
        case CxField::Gram: {
            const std::string_view codes = it->GAttr(it->selected);
            for (std::size_t off = 0; off < a.value.size() && !v; off += 2) {
                const std::string_view code = std::string_view(a.value).substr(off, 2);
                // Qw: a question word is the word or comes before it in its sentence, as in rules.
                v = code == "Qw" ? it->AfterQuestionWord() : Has(codes, code);
            }
            break;
        }
        case CxField::FirstWord: {
            const std::string_view target = it->EntryText(it->selected);
            const std::size_t space = target.find(' ');
            if (space == std::string_view::npos) return false;
            v = MatchesValue(a, target.substr(0, space));
            break;
        }
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

void Apply(Flusher& f, const ConstructionTable& t, Item* origin, const internal::CxAction& a, Selection source) {
    Item* it = Neighbour(origin, a.delta);
    if (it == nullptr) return;
    switch (a.verb) {
        case internal::CxVerb::SelectLemma:
        case internal::CxVerb::SelectTarget: {
            const int count = static_cast<int>(it->group->entries.size());
            for (int slot = 0; slot < count; slot++) {
                const bool match = a.verb == internal::CxVerb::SelectLemma ? MatchesLiteral(ItemLemma(f, it, slot), a.value)
                                                                           : MatchesLiteral(it->EntryText(slot), a.value);
                if (match) {
                    it->SelectCandidate(slot, source);
                    break;
                }
            }
            break;
        }
        case internal::CxVerb::CopyWord: {
            // The copy takes the word of the last copy of its run (なんでなんで 왜 왜).
            // Not before a particle or a copula, whose word the last copy may read in a bound form
            // (まさかまさかの 설마 뜻밖의, ばかりばかりだ), nor from a last copy that writes nothing.
            const Item* last = internal::LastCopy(it);
            if (last == it || last->selected < 0 || it->selected < 0 || last->EntryText(last->selected).empty()) break;
            if (const Item* n = last->next; n != nullptr && n->selected >= 0) {
                if (const Entry* e = n->group->At(n->selected); e != nullptr && e->cls == 'P') break;
            }
            const std::string lemma = ItemLemma(f, last, last->selected);
            if (lemma == ItemLemma(f, it, it->selected)) break;
            bool found = false;
            for (int slot = 0; slot < static_cast<int>(it->group->entries.size()) && !found; slot++) {
                if (ItemLemma(f, it, slot) == lemma) {
                    it->SelectCandidate(slot, source);
                    found = true;
                }
            }
            // A pass may have rewritten the candidate the last copy reads (なんで 어째서 at the
            // start of a sentence): the copy then writes the last copy's Korean.
            if (!found) it->AssignEntrySurface(it->selected, std::string(last->EntryText(last->selected)), source);
            break;
        }
        case internal::CxVerb::CopyEnding: {
            // The copy takes the ending of the last copy of its run (copies rows). A copy reads
            // its own word: the same word may be written apart before a particle (私、私が 나, 내가).
            const Item* last = internal::LastCopy(it);
            if (last == it || last->selected < 0 || it->selected < 0 ||
                it->EntryText(it->selected) != last->EntryText(last->selected)) {
                break;
            }
            // The ending of the same kind as the last copy's: the slot with its code and Korean,
            // or the chosen slot of that code rewritten to its Korean.
            const int from = last->DirectIndex(), to = it->DirectIndex();
            if (from < 0 || to < 0) break;
            const std::string_view code = last->DirectCode(from), ending = last->DirectText(from);
            // Only an ending that starts with its form (6, 1고): a rule's own marker belongs to
            // the words after the last copy (照れ照れになり).
            if (ending.empty() || !text::IsDigit(static_cast<unsigned char>(ending[0])) ||
                (it->DirectCode(to) == code && it->DirectText(to) == ending)) {
                break;
            }
            int same = -1;
            for (int slot = 0; slot < it->DirectCount() && same < 0; slot++) {
                if (it->DirectCode(slot) == code && it->DirectText(slot) == ending) same = slot;
            }
            if (same >= 0) {
                it->SetDirectIndex(same, source);
            } else if (it->DirectCode(to) == code) {
                it->AssignDirectSlot(to, ending, source);
            }
            break;
        }
        case internal::CxVerb::TakeTarget: TakeTarget(it, At(it->Code(it->selected), 0), a.value, source); break;
        case internal::CxVerb::SetTarget: it->AssignEntrySurface(it->selected, a.value, source); break;
        case internal::CxVerb::AppendTarget:
            // The word keeps its candidate; -why names the row that wrote after it.
            it->appended += a.value;
            it->choice = source;
            break;
        case internal::CxVerb::SetEnding: it->AssignDirectSlot(it->DirectIndex(), a.value, source); break;
        case internal::CxVerb::ReplaceHead: {
            const std::string_view target = it->EntryText(it->selected);
            if (target.starts_with(a.value)) {
                it->AssignEntrySurface(it->selected, a.with + std::string(target.substr(a.value.size())), source);
            }
            break;
        }
        case internal::CxVerb::ReplaceEndingTail: {
            const Entry* e = it->DirectEntry(it->DirectIndex());
            if (e != nullptr && e->dst.ends_with(a.value)) {
                it->AssignDirectSlot(it->DirectIndex(), e->dst.substr(0, e->dst.size() - a.value.size()) + a.with, source);
            }
            break;
        }
        case internal::CxVerb::MapLemma:
        case internal::CxVerb::MapTarget: {
            const std::string key =
                a.verb == internal::CxVerb::MapLemma ? ItemLemma(f, it, it->selected) : std::string(it->EntryText(it->selected));
            for (const auto& [from, to] : t.maps[static_cast<std::size_t>(a.map)].pairs) {
                if (from == key) {
                    it->AssignEntrySurface(it->selected, to, source);
                    break;
                }
            }
            break;
        }
        case internal::CxVerb::SelectMap: {
            const std::string key = ItemLemma(f, it, it->selected);
            for (const auto& [from, to] : t.maps[static_cast<std::size_t>(a.map)].pairs) {
                if (from != key) continue;
                const int count = static_cast<int>(it->group->entries.size());
                for (int slot = 0; slot < count; slot++) {
                    if (ItemLemma(f, it, slot) == to) {
                        it->SelectCandidate(slot, source);
                        break;
                    }
                }
                break;
            }
            break;
        }
        case internal::CxVerb::SetVerb:
            it->AssignEntrySurface(it->selected, output::Conjugating(a.value, '&', "", f.families).target, source);
            break;
        case internal::CxVerb::SetActive:
            // The verb is written in the active voice: its Korean without the passive.
            if (it->DetailRecord() == 1) {
                it->detail->record = 0;
                if (Entry* e = it->group->At(it->selected)) e->targetChoice = source;
                it->choice = source;
            }
            break;
        case internal::CxVerb::SetSound: {
            if (std::string sound = text::FromKana(it->group->surface, text::Preset::Fan); !sound.empty()) {
                it->AssignEntrySurface(it->selected, sound, source);
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
            // Rule counts try every row.
            const std::vector<int>* rows = &family.rows;
            if (family.indexed && tally == nullptr) {
                const auto found = family.bySurface.find(std::string_view(it->group->surface));
                rows = found != family.bySurface.end() ? &found->second : &family.open;
            }
            for (const int r : *rows) {
                const internal::CxRow& row = t.rows[static_cast<std::size_t>(r)];
                if (skip != nullptr && skip(row.id)) continue;
                const bool fired = Eval(f, t, row.root, it);
                if (tally != nullptr) {
                    tally->constructionRuns[static_cast<std::size_t>(r)]++;
                    if (fired) tally->constructionFires[static_cast<std::size_t>(r)]++;
                }
                if (!fired) continue;
                for (const internal::CxAction& a : row.actions) Apply(f, t, it, a, {Provenance::Rewrite, r});
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
