// Grammar passes of the text path: the readings of a verb's られ, and the conjugating
// targets of passive verbs that have no static family.
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/output/compose.h"
#include "core/output/korean.h"
#include "core/output/families.h"
#include "core/output/inflect.h"
#include "core/output/particle.h"
#include "core/text/hangul.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/tokenizer/classify.h"
#include "core/word/flush.h"
#include "core/word/strings.h"
#include "core/word/terminal.h"
#include "core/word/util.h"

namespace hdTrans::word {
namespace {

// Passive verbs written with a Korean passive rather than 지다, by the surface of the item.
// A class after the colon is the conjugation class of the Korean lemma.
struct PassiveLemma {
    std::string_view surface;
    std::string_view lemma;
};

constexpr PassiveLemma kPassiveLemmas[] = {
    {"恐れ", "두려움을 사다"}, {"褒め", "칭찬받다"}, {"助け", "도움을 받다"}, {"責め", "비난받다"}, {"認め", "인정받다"},
    {"求め", "요구되다"}, {"見つめ", "응시당하다"}, {"遠ざけ", "경원시되다"}, {"食べ", "먹히다"}, {"忘れ", "잊히다"},
    {"捕まえ", "붙잡히다"}, {"閉じ込め", "갇히다"}, {"見捨て", "버림받다"}, {"抱きしめ", "안기다"}, {"迎え", "환영받다"},
    {"苛め", "괴롭힘을 당하다"}, {"虐げ", "학대받다"}, {"慰め", "위로받다"}, {"称え", "칭송받다"}, {"讃え", "칭송받다"},
    {"崇め", "숭배받다"}, {"咎め", "문책당하다"}, {"舐め", "얕보이다:규칙"}, {"見下げ", "멸시당하다"}, {"教え", "가르침을 받다"},
    {"授け", "받다"}, {"告げ", "듣다:ㄷ"}, {"止め", "저지당하다"}, {"見せ", "보여지다"},
    {"押し付け", "떠맡겨지다"}, {"責め立て", "추궁당하다"}, {"追い詰め", "몰리다"}, {"攻め", "공격받다"},
    {"何を言", "무슨 말을 듣다:ㄷ"}, {"追い出", "쫓겨나다"}, {"叩き出", "쫓겨나다"}, {"脅", "협박당하다"}, {"乱入", "난입당하다"},
    {"閉め", "닫히다:규칙"}, {"開け", "열리다:규칙"}, {"連れ", "이끌리다:규칙"}, {"込め", "담기다:규칙"},
    {"括り付け", "묶이다:규칙"}, {"縛り付け", "묶이다:규칙"}, {"入れ替え", "바뀌다:규칙"}, {"取り替え", "바뀌다:규칙"},
    {"捨て", "버려지다:규칙"}, {"育て", "길러지다:규칙"}, {"命じ", "명령받다"},
};

// Korean verbs with a passive of their own (먹다 먹히다, 둘러싸다 둘러싸이다), which a
// passive item takes instead of 어지다.
struct KoreanPassive {
    std::string_view active;
    std::string_view passive;
};

constexpr KoreanPassive kKoreanPassives[] = {
    {"먹다", "먹히다"},     {"잡다", "잡히다"},     {"밟다", "밟히다"},     {"막다", "막히다"},
    {"찍다", "찍히다"},     {"뽑다", "뽑히다"},     {"박다", "박히다"},     {"둘러싸다", "둘러싸이다"},
    {"에워싸다", "에워싸이다"}, {"쌓다", "쌓이다"}, {"놓다", "놓이다"},     {"섞다", "섞이다"},
    {"꺾다", "꺾이다"},     {"덮다", "덮이다"},     {"묶다", "묶이다"},     {"쫓다", "쫓기다"},
    {"빼앗다", "빼앗기다"}, {"찢다", "찢기다"},     {"끊다", "끊기다"},     {"끌다", "끌리다"},
    {"물다", "물리다"},     {"팔다", "팔리다"},     {"밀다", "밀리다"},     {"누르다", "눌리다"},
    {"부르다", "불리다"},   {"자르다", "잘리다"},   {"흔들다", "흔들리다"}, {"잡아먹다", "잡아먹히다"},
    {"핥다", "핥이다"},
};

// The heads of a て ending that holds the existential 있 (1고 있다, 6 있다), the family of 있다 and
// the family of the honorifics in 시.
constexpr std::string_view kProgressiveHead = "1고 ";
constexpr std::string_view kResultativeHead = "6 ";
constexpr std::string_view kExistenceMarker = "&HN@";
constexpr std::string_view kHonorificMarker = "&AE@";

// The (ら)れ of the passive, potential and respectful forms, the に of an agent and its Korean,
// and the Korean of the potential.
constexpr std::string_view kRare = "られ";
constexpr std::string_view kRareRe = "れ";
constexpr std::string_view kIware = "言われ";
constexpr std::string_view kAgentNi = "に";
constexpr std::string_view kKoAgent = "에게";
constexpr std::string_view kKoCan = "수 있";
constexpr std::string_view kKoCannot = "수 없";

// Whether the 2-byte grammar codes gattr hold code.
bool GrammarCode(std::string_view gattr, std::string_view code) {
    for (std::size_t i = 0; i + 2 <= gattr.size(); i += 2) {
        if (gattr.substr(i, 2) == code) return true;
    }
    return false;
}

// Removes the two-byte grammar code from gattr.
void DropGrammarCode(std::string& gattr, std::string_view code) {
    for (std::size_t i = 0; i + 2 <= gattr.size();) {
        if (gattr.compare(i, 2, code) == 0) gattr.erase(i, 2);
        else i += 2;
    }
}

// Whether item p ends the clause a (ら)れ verb reads back through: another verb or adjective, an
// empty item or punctuation.
bool ClauseBreak(const Item* p) {
    const std::string_view s = p->group->surface;
    return p->detail->Yougo() || s.empty() || Has("、。,「」『』！？", s);
}

// The target of the passive lemma of surface; false when surface has none.
bool PassiveTarget(Flusher& f, std::string_view surface, std::string& target) {
    for (const PassiveLemma& p : kPassiveLemmas) {
        if (p.surface != surface) continue;
        const std::string_view lemma = p.lemma;
        const std::size_t colon = lemma.find(':');
        const std::string_view ko = lemma.substr(0, colon);
        const std::string_view cls = colon == std::string_view::npos ? std::string_view() : lemma.substr(colon + 1);
        output::Conjugated c = output::Conjugating(ko, '&', cls, f.families);
        if (!c.error.empty()) return false;
        target = std::move(c.target);
        return true;
    }
    return false;
}

// Whether the object of を, item o, is a part of the body or what one wears (VBp, VAc).
bool BodyObject(const Item* o) {
    if (o == nullptr) return false;
    const std::string_view attr = o->Attr(std::max(o->selected, 0));
    for (std::size_t i = 0; i + 3 <= attr.size(); i += 3) {
        const std::string_view c = attr.substr(i, 3);
        if (c == "VBp" || c == "VAc") return true;
    }
    return false;
}

// Whether stem is a compound verb of action on someone (睨み付け, 突きつけ, 投げかけ, 責め立て).
bool CompoundAction(std::string_view stem) {
    for (const std::string_view s : dict::ClassMembers(HDTRANS_CLASS("suffix.compound-action"))) {
        if (stem.ends_with(s) && stem.size() > s.size()) return true;
    }
    return false;
}

// A verb item with a record that takes an object (part code class 3 to 6).
bool Transitive(const Item* it) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        const char c = At(it->Code(slot), 1);
        if (c >= '3' && c <= '6') return true;
    }
    return false;
}

// The passive reading of a られた, られて or られ、 with no other cue.
bool NarrativePassive(const Item* it, std::string_view rest) {
    if (!rest.starts_with("た") && !rest.starts_with("て") && !rest.starts_with(kJaComma)) return false;
    return it->detail->syntheticCausative ||
           !dict::InClass(HDTRANS_CLASS("verb.potential-rareta"), it->group->surface);
}

// Whether the clause before item it has an object を and no に (夜伽を命じられれば, not
// 証拠を上に見せられれば).
bool ObjectWithoutRecipient(const Item* it) {
    bool object = false;
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s == "に" || s == "には") return false;
        if (s == "を") object = true;
        if (ClauseBreak(p)) break;
    }
    return object;
}

// The passive reading of the られ of verb item it; rest is the text after られ:
//   - a conditional of a verb of verb.passive-after-object is passive after its object without a
//     recipient に;
//   - a negative, conditional or volitional form and the polite non-past are potential;
//   - a verb that takes no object is potential, unless it is listed or a compound action;
//   - ている, ていた, てる, ており are passive;
//   - going back through the clause, the nearest を (potential unless listed, or a narrative
//     form of a verb of verb.passive-after-object, after the body or before a noun it describes)
//     or an agent
//     に, から, によって, により after a thing-free word decides;
//   - listed verbs and narrative forms are passive;
//   - a subject は or が that is a thing makes the reading passive, unless the verb is the
//     plain non-past form;
//   - otherwise potential.
bool PassiveReading(const Worker& w, const Item* it, std::string_view rest) {
    if ((rest.starts_with("れば") || rest.starts_with("たら")) &&
        dict::InClass(HDTRANS_CLASS("verb.passive-after-object"), it->group->surface)) {
        return ObjectWithoutRecipient(it);
    }
    if (dict::StartsWithMember(HDTRANS_CLASS("ending.rare-potential"), rest) > 0) return false;
    const bool listed =
        dict::InClass(HDTRANS_CLASS("verb.passive-any-object"), it->group->surface) || CompoundAction(it->group->surface);
    if (!listed && !Transitive(it) && !it->detail->syntheticCausative) return false;
    if (dict::StartsWithMember(HDTRANS_CLASS("ending.rare-passive"), rest) > 0) return true;
    const Item* subject = nullptr;
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s == "を" && !listed &&
            (!NarrativePassive(it, rest) ||
             !(dict::InClass(HDTRANS_CLASS("verb.passive-after-object"), it->group->surface) || BodyObject(p->prev) ||
               DescribedNoun(w, it)))) {
            return false;
        } else if (s == "に" || s == "から" || s == "によって" || s == "により") {
            const Item* a = p->prev;
            if (a != nullptr && !Thing(a->Attr(std::max(a->selected, 0)))) return true;
        } else if ((s == "は" || s == "が") && p->prev != nullptr && subject == nullptr) {
            subject = p->prev;
        } else if (ClauseBreak(p)) {
            break;
        }
    }
    if (listed || NarrativePassive(it, rest)) return true;
    return subject != nullptr && Thing(subject->Attr(std::max(subject->selected, 0))) && !rest.starts_with("る");
}

// The Korean passive of the Korean of a passive verb: a verb of kKoreanPassives, or a
// noun with 하다 as the noun with 되다, 받다 or 당하다; "" for others (a psych verb in 어하다).
std::string KoreanPassiveLemma(std::string_view lemma) {
    for (const KoreanPassive& k : kKoreanPassives) {
        if (lemma == k.active || (lemma.ends_with(k.active) && lemma[lemma.size() - k.active.size() - 1] == ' ')) {
            return std::string(lemma.substr(0, lemma.size() - k.active.size())) + std::string(k.passive);
        }
    }
    constexpr std::string_view kHada = "하다";
    if (!lemma.ends_with(kHada) || lemma.size() < kHada.size() + 6) return {};
    const std::string_view stem = lemma.substr(0, lemma.size() - kHada.size());
    const std::size_t space = stem.rfind(' ');
    const std::string_view noun = space == std::string_view::npos ? stem : stem.substr(space + 1);
    const char32_t last = DecodeLastRune(noun).cp;
    if (last < 0xAC00 || last > 0xD7A3 || noun.size() < 6) return {};
    // 좋아하다, 미워하다: a feeling in 어하다, whose passive is another word (미움받다).
    if (last == U'아' || last == U'어' || last == U'워' || last == U'여' || last == U'와') return {};
    if (dict::InClass(HDTRANS_CLASS("ko-noun.passive-eojida"), noun)) return {};
    const std::string_view by = dict::InClass(HDTRANS_CLASS("ko-noun.passive-batda"), noun)      ? "받다"
                                : dict::InClass(HDTRANS_CLASS("ko-noun.passive-danghada"), noun) ? "당하다"
                                                                                   : "되다";
    return std::string(stem) + std::string(by);
}

// Whether verb item it ends in its attributive form (る, た) before a noun in its token that
// names no clause (と言われた下女, not と言われるのが or と言われたこと); an auxiliary いる after
// its て form ends it instead (と言われている騎士団長).
bool ModifiesNoun(const Item* it) {
    if (const Item* aux = it->After(); aux != nullptr && aux->tokenIndex == it->tokenIndex && aux->detail->Yougo() &&
                                    (aux->group->surface == "い" || aux->group->surface == "お")) {
        it = aux;
    }
    const Item* n = it->After();
    const std::string_view own = it->text.substr(static_cast<std::size_t>(it->start), static_cast<std::size_t>(it->length));
    return n != nullptr && n->tokenIndex == it->tokenIndex && (own.ends_with("る") || own.ends_with("た")) &&
           At(n->Code(std::max(n->selected, 0)), 0) == '1' &&
           dict::StartsWithMember(HDTRANS_CLASS("noun.clause"), n->group->surface) == 0;
}

// Whether noun item n names someone the speaker honours.
bool HonouredPerson(const Item* n) {
    if (n == nullptr || n->detail->Yougo() || n->group->entries.empty()) return false;
    return dict::EndsWithMember(HDTRANS_CLASS("honorific.honoured-person"), n->group->surface) > 0;
}

// The Korean of the quote passive of item it (天才と言われた) that is not before a noun it
// describes, by the subject and the form:
//   - after a condition or a concession (と言われれば, と言われても) the impersonal 하다;
//   - with a subject は, が or も before a quoted noun (王子は天才と言われていた), the subject
//     is called (불리다), or told (라는 말을 듣다) when it is a person and the form is neither a
//     ている form nor the continuative before a comma (彼は天才と言われた);
//   - another quote with an honoured subject in the sentence is the respectful 言われる, which
//     the active reads (382さんは10体くらいと言われていました);
//   - with no such subject: said by people (라고들 하다) in the ている forms, the impersonal
//     하다 before a comma, told otherwise (バカと言われたことがある 바보라는 말을 들은 적이 있다).
// head is the Korean before 라고 ((이)), rest the text after the item (われていた); "" keeps the
// active reading.
std::string QuotePassive(const Item* it, std::string_view head, std::string_view rest) {
    const std::string h(head);
    if (rest.starts_with("われ")) rest.remove_prefix(std::string_view("われ").size());
    for (const std::string_view c : {"れば", "たら", "ても", "ては", "ちゃ"}) {
        if (rest.starts_with(c) && !rest.starts_with("てはい") && !rest.starts_with("てはお")) return h + "라고 &AV@";
    }
    bool stative = false;
    for (const std::string_view p : {"てい", "てる", "てお", "てた", "ては"}) stative = stative || rest.starts_with(p);
    const bool comma = rest.starts_with(kJaComma) || rest.starts_with(",");
    const Item* quote = it->prev;
    if (quote == nullptr) return h + (stative ? "라고들 &AV@" : "라는 말을 &J6@");
    const Entry* label = quote->detail->Yougo() ? nullptr : quote->group->At(std::max(quote->selected, 0));
    if (const Item* p = quote->prev; label != nullptr && label->cls == 'N' && At(label->code, 0) == '1' &&
                                     p != nullptr && p->prev != nullptr && !p->prev->detail->Yougo() &&
                                     (p->group->surface == "は" || p->group->surface == "が" || p->group->surface == "も")) {
        const bool person = Person(p->prev->Attr(std::max(p->prev->selected, 0)));
        return h + (person && !stative && !comma ? "라는 말을 &J6@" : "라고 불&AA@");
    }
    for (const Item* p = quote; p != nullptr && !Has("。！？!?「」『』", p->group->surface); p = p->prev) {
        const std::string_view s = p->group->surface;
        if ((s == "は" || s == "が" || s == "も") && p->prev != nullptr &&
            (HonouredPerson(p->prev) || p->prev->group->surface == "さん")) {
            return {};
        }
    }
    return h + (stative ? "라고들 &AV@" : comma ? "라고 &AV@" : "라는 말을 &J6@");
}

// The object of 듣다 for the Korean head of an object word in 言 before its 하다: the object
// with its particle (비꼬는 말을), without the 말 of 말하다 after it (의견을 말 의견을), or a
// bare noun of 하다 with 을/를 (불평 불평을).
std::string HeardObject(std::string_view head) {
    if (head.empty() || head.ends_with(" ")) return std::string(head);
    for (const std::string_view p : {"을 말", "를 말"}) {
        if (head.ends_with(p)) return std::string(head.substr(0, head.size() - std::string_view("말").size()));
    }
    const int final = text::Final(DecodeLastRune(head).cp);
    if (final < 0) return std::string(head);
    return std::string(head) + (final == text::kFinalNone ? "를 " : "을 ");
}

// Whether SayPassive writes the passive of item it: a word in 言 whose every candidate holds a
// quote (라고) or, after an object, ends in 하다.
bool SayPassiveApplies(const Item* it) {
    const std::string_view s = it->group->surface;
    if (!s.ends_with("言") || s == "言") return false;
    const bool object = s.ends_with("を言");
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        const std::string_view t = it->EntryText(slot);
        if (!Has(t, "라고") && !(object && t.ends_with("&AV@"))) return false;
    }
    return true;
}

// The passive of a word in 言 (言う with what is said before it) has no 지다 passive in
// Korean: after a quote it is 불리다 before a noun it describes (無口と言われた下女 과묵이라고
// 불린 하녀) and otherwise QuotePassive; after an object 하다 becomes 듣다 (嫌味を言われる
// 비꼬는 말을 듣다). Other words in 言 keep their passive.
void SayPassive(Item* it) {
    if (!SayPassiveApplies(it)) return;
    const std::string_view s = it->group->surface;
    const std::string_view rest = From(it->text, it->start + static_cast<long>(s.size()));
    const int count = static_cast<int>(it->group->entries.size());
    std::vector<std::string> targets;
    for (int slot = 0; slot < count; slot++) {
        const std::string_view t = it->EntryText(slot);
        if (const std::size_t q = t.find("라고"); q != std::string_view::npos) {
            std::string target = ModifiesNoun(it) ? std::string(t.substr(0, q)) + "라고 불&AA@"
                                                  : QuotePassive(it, t.substr(0, q), rest);
            targets.push_back(target.empty() ? std::string(t) : std::move(target));
        } else {
            targets.push_back(HeardObject(t.substr(0, t.size() - std::string_view("&AV@").size())) + "&J6@");
        }
    }
    it->detail->record = 0;
    it->lexical = true;
    for (int slot = 0; slot < count; slot++) it->AssignEntrySurface(slot, targets[static_cast<std::size_t>(slot)]);
}

// Verbs of kPassiveLemmas whose lemma reads an attitude to a person (舐められる 얕보이다);
// with an object of を in the clause the passive is the act done to the speaker's body
// (鈴口を舐められて), which the verb's own reading gives.
constexpr std::string_view kAttitudePassives[] = {"舐め"};

// Whether a を stands before item it in its clause.
bool ObjectBefore(const Item* it) {
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s == "を") return true;
        if (ClauseBreak(p)) return false;
    }
    return false;
}

// Whether an object を stands before the item in its clause with no pause between (not 全滅するところを…
// 助けられた, where the を before the ellipsis is the moment, not what was done).
bool ActedObjectBefore(const Item* it) {
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        if (p->AfterPause(false) && p->prev != nullptr && p->prev->group->surface == "を") return false;
        if (p->group->surface == "を") return true;
        if (ClauseBreak(p)) return false;
    }
    return false;
}

// Writes a passive verb item of kPassiveLemmas as its Korean passive in the active voice.
void WriteLexicalPassive(Flusher& f, Item* it) {
    std::string lexical;
    if (InSet(kAttitudePassives, it->group->surface) && ObjectBefore(it)) return;
    if (!PassiveTarget(f, it->group->surface, lexical)) {
        SayPassive(it);
        return;
    }
    it->detail->record = 0;
    it->lexical = true;
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) it->AssignEntrySurface(slot, lexical);
}

// Whether the text after ではな is a contrast that goes on (大した傷ではないが), which keeps the
// topic particle.
bool Contrast(std::string_view rest) {
    constexpr std::string_view kForms[] = {"い", "かった"};
    for (const std::string_view form : kForms) {
        if (!rest.starts_with(form)) continue;
        std::string_view after = rest.substr(form.size());
        if (after.starts_with(kJaComma)) after.remove_prefix(kJaComma.size());
        if (dict::StartsWithMember(HDTRANS_CLASS("particle.contrast"), after) > 0) return true;
    }
    return false;
}

}  // namespace

bool DescribedNoun(const Worker& w, const Item* it) {
    const Item* n = it->After();
    return n != nullptr && n->tokenIndex == it->tokenIndex && At(n->Code(std::max(n->selected, 0)), 0) == '1' &&
           !UtteranceEnd(w, n) && !RuneIn(text::DecodeRune(From(n->text, n->start + n->length)).cp, "、。！？!?…‥」』）)") &&
           dict::StartsWithMember(HDTRANS_CLASS("noun.clause"), n->group->surface) == 0;
}

// Dictionary attributes of a thing: some, none of them a person, an animal or a god.
std::string ItemLemma(Flusher& f, const Item* it, int slot) {
    return output::Lemma(it->EntryText(slot), f.families);
}

bool Thing(std::string_view attr) {
    if (attr.empty()) return false;
    constexpr std::string_view kPersons = "VHn VQw VQm VGn VAm QPN QNP";
    for (std::size_t i = 0; i + 3 <= attr.size(); i += 3) {
        const std::string_view c = attr.substr(i, 3);
        if (Has(kPersons, c) || (c >= "QAA" && c <= "QAH")) return false;
    }
    return true;
}

bool Person(std::string_view attr) { return !attr.empty() && !Thing(attr); }

namespace {

// Whether the sem codes attr name a person or an animal that Korean marks with 에게: not an insect
// or a microbe (모기에 물리다), nor what is only supernatural (VGn: 魔法, 天国).
bool Animate(std::string_view attr) {
    constexpr std::string_view kAnimate = "VHn VQw VQm VAm QPN QNP QAA QAB QAD";
    bool animate = false;
    for (std::size_t i = 0; i + 3 <= attr.size(); i += 3) {
        const std::string_view c = attr.substr(i, 3);
        if (c == "QAE" || c == "QAH") return false;
        animate = animate || Has(kAnimate, c);
    }
    return animate;
}

}  // namespace

bool ContrastFollows(const Item* it) {
    return Contrast(From(it->text, it->start + static_cast<long>(it->group->surface.size())));
}

bool KatakanaSurface(std::string_view s) {
    for (std::string_view t = s; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (!((r.cp >= U'ァ' && r.cp <= U'ヶ') || r.cp == U'ー')) return false;
        t.remove_prefix(r.size == 0 ? 1 : r.size);
    }
    return !s.empty();
}

bool PauseOrEnd(const Item* it) {
    const std::string_view s = it->group->surface;
    return InSet(kCommaItems, s) || InSet(kSentenceEndItems, s) || InSet(kClosingQuoteItems, s) || s == "。";
}

bool NumberSurface(std::string_view s) {
    if (s.empty()) return false;
    for (std::string_view rest = s; !rest.empty(); rest.remove_prefix(text::DecodeRune(rest).size)) {
        const char32_t r = text::DecodeRune(rest).cp;
        const bool digit = (r >= U'0' && r <= U'9') || (r >= U'０' && r <= U'９');
        if (!digit && !RuneIn(r, "〇一二三四五六七八九十百千万億")) return false;
    }
    return true;
}

std::string_view TextBefore(const Worker& w, const Item* it, int& token) {
    std::string_view before = it->text.substr(0, static_cast<std::size_t>(std::max(it->start, 0)));
    for (token = it->tokenIndex;;) {
        before = TrimSpace(before);
        if (!before.empty() || token == 0) return before;
        before = w.Tok(--token).text;
    }
}

bool OpensLine(const Worker& w, const Item* it) {
    int token = 0;
    const std::string_view before = TextBefore(w, it, token);
    return before.empty() || RuneIn(DecodeLastRune(before).cp, "。．.！？!?");
}

bool AfterRestart(const Worker& w, const Item* it) {
    int token = 0;
    std::string_view before = TextBefore(w, it, token);
    bool mark = false;
    while (const std::size_t n = dict::EndsWithMember(HDTRANS_CLASS("mark.restart"), before)) {
        before.remove_suffix(n);
        mark = true;
    }
    if (!mark) return false;
    if (!TrimSpace(before).empty()) return true;
    while (token > 0) {
        if (!TrimSpace(w.Tok(--token).text).empty()) return true;
    }
    return false;
}

bool SelectLemma(Flusher& f, Item* it, std::string_view lemma) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        if (output::Lemma(it->EntryText(slot), f.families) == lemma) {
            it->SelectCandidate(slot, {Provenance::Rewrite});
            return true;
        }
    }
    return false;
}


namespace {

// The respectful reading of the passive or potential (ら)れ of item it: going back through the
// sentence, an agent に, には or から after a person, or によって or により, makes it the passive;
// otherwise the nearest は, が or も in the clause decides, respectful when the noun before it
// names someone the speaker honours (王子様がお見舞いに来られた, 陛下は手紙を書かれた), and so
// does a subject の right before an intransitive verb that holds no object (先生の言われる通り).
// A transitive verb also needs its object before that subject: without one its subject is what
// the act is done to (陛下が幽閉された, 姫様は許された).
bool RespectfulReading(const Item* it, bool transitive) {
    bool object = false;
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s.starts_with("によ")) {
            return false;
        } else if (s == "に" || s == "には" || s == "から") {
            const Item* a = p->prev;
            if (a != nullptr && Person(a->Attr(std::max(a->selected, 0)))) return false;
        } else if (s == "を") {
            object = true;
        } else if (s == "の" && p == it->prev && !transitive && !Has(it->group->surface, "を")) {
            return HonouredPerson(p->prev);
        } else if (transitive && !object && (s == "は" || s == "が" || s == "も")) {
            return false;
        } else if (s == "は" || s == "が" || s == "も") {
            return HonouredPerson(p->prev);
        } else if (s.empty() || Has("。！？「」『』!?", s) ||
                   ((s == "、" || s == ",") && p->prev != nullptr && p->prev->detail->Yougo())) {
            break;
        }
    }
    return false;
}

// Whether item n is a noun whose する takes an object: any noun but one tagged Hi (気絶, 崩御,
// 到着), whose する is done by its subject alone.
bool SinoNounTakesObject(const Item* n) {
    if (n == nullptr || n->detail->Yougo() || n->group->entries.empty()) return false;
    const int slot = std::max(n->selected, 0);
    return At(n->Code(slot), 0) == '1' && !GrammarCode(n->GAttr(slot), "Hi");
}

// The passive endings by the conjugation kinds (the last digit of the part code) of the verbs
// they follow; '?' is 行く.
constexpr std::string_view kPassiveEndings[][2] = {
    {"られ", "78"}, {"され", "3"}, {"かれ", "1?"}, {"がれ", "2"}, {"たれ", "4"}, {"なれ", "<"},
    {"ばれ", "6"},  {"まれ", "5"}, {"われ", "0"},
};

// Whether a verb part code without its tag takes an object (class 3 to 6: 431 書く; not 418 帰る).
bool TransitiveCode(std::string_view code) { return At(code, 1) >= '3' && At(code, 1) <= '6'; }

// Whether a verb candidate with that code and sem codes takes an object: a verb of motion (UMv,
// 行く) takes none.
bool TakesObject(std::string_view code, std::string_view attr) { return TransitiveCode(code) && !Has(attr, "UMv"); }

// How an honorific word of its own takes the いる of its て form: as the plain verb of its stem
// (말씀하고 계신다), as itself (드시고 계신다), or not at all (계시다 is the いる; 돌아가셨다 the state).
enum class OwnProgressive { Stem, Itself, None };

struct Suppletive {
    std::string_view plain, honorific;
    OwnProgressive progressive;
};

// Verbs with their own honorific word.
constexpr Suppletive kSuppletive[] = {
    {"말하다", "말씀하시다", OwnProgressive::Stem}, {"먹다", "드시다", OwnProgressive::Itself},
    {"마시다", "드시다", OwnProgressive::Itself},   {"자다", "주무시다", OwnProgressive::Itself},
    {"있다", "계시다", OwnProgressive::None},       {"죽다", "돌아가시다", OwnProgressive::None},
};

// The honorific 시 with the dictionary ending, which ends every honorific lemma (쓰시다).
constexpr std::string_view kHonorificDa = "시다";

// The honorific target of the conjugating verb target text (쓰&AF@ 쓰시다); "" when it has none.
std::string HonorificTarget(Flusher& f, std::string_view text) {
    if (text.size() < 4 || text.back() != '@' || text[text.size() - 4] != '&') return {};
    const std::string_view head = text.substr(0, text.size() - 4);
    std::string lemma;
    const std::string plain = output::Lemma(text, f.families);
    for (const auto& [verb, honorific, progressive] : kSuppletive) {
        if (plain == verb || plain.ends_with(" " + std::string(verb))) {
            lemma = plain.substr(0, plain.size() - verb.size()) + std::string(honorific);
        }
    }
    if (lemma.empty()) {
        const std::string_view stem = LookupSource(std::string(text.substr(text.size() - 4)) + "A", f.families);
        if (stem.empty()) return {};
        lemma = std::string(head) + std::string(stem) + std::string(kHonorificDa);
    }
    output::Conjugated c = output::Conjugating(lemma, '&', "", f.families);
    return c.error.empty() ? std::move(c.target) : std::string();
}

// The records of the verb whose (ら)れ form is spelled (書かれ 書く, 調べられ 調べる): the verb
// before an ending of kPassiveEndings, of a conjugation kind of that ending, in lookup order; base
// is that verb's stem. None when the dictionary has no such verb.
std::vector<const Record*> BaseVerbRecords(Flusher& f, std::string_view spelled, std::string_view& base) {
    std::vector<const Record*> verbs;
    for (const auto& [ending, kinds] : kPassiveEndings) {
        if (!spelled.ends_with(ending) || spelled.size() == ending.size()) continue;
        base = f.w.arena.Keep(std::string(spelled.substr(0, spelled.size() - ending.size())));
        const Lookup found = f.w.DoLookup(base, kModeGeneral);
        for (const Record* r = found.records; r != nullptr; r = r->child) {
            if (r->source == base && At(r->partCode, 1) == '4' && Has(kinds, std::string(1, At(r->partCode, 3))) &&
                r->target.ends_with("@")) {
                verbs.push_back(r);
            }
        }
        break;
    }
    return verbs;
}

// The record the defaults choose among verbs: the first coded XXX, else the first.
const Record* DefaultRecord(const std::vector<const Record*>& verbs) {
    for (const Record* r : verbs) {
        if (Has(r->semantic, "XXX")) return r;
    }
    return verbs.front();
}

// The honorific target of the verb whose passive stem is surface (書かれ 書 쓰&AF@ 쓰시다), its
// default record first, and the verb's own target in plain; "" when the dictionary has no such
// verb.
std::string BaseVerbTarget(Flusher& f, std::string_view surface, bool& transitive, std::string& plain) {
    std::string_view base;
    const std::vector<const Record*> verbs = BaseVerbRecords(f, surface, base);
    if (verbs.empty() || dict::InClass(HDTRANS_CLASS("verb.passive-any-object"), base) || dict::InClass(HDTRANS_CLASS("verb.passive-after-object"), base) ||
        dict::InClass(HDTRANS_CLASS("verb.receiver-passive"), base)) {
        return {};
    }
    const Record* verb = DefaultRecord(verbs);
    transitive = TakesObject(verb->partCode.substr(1), verb->semantic);
    plain = verb->target;
    return HonorificTarget(f, verb->target);
}

// Whether the verb whose (ら)れ form is spelled takes an object, by its default record.
bool BaseTakesObject(Flusher& f, std::string_view spelled) {
    std::string_view base;
    const std::vector<const Record*> verbs = BaseVerbRecords(f, spelled, base);
    if (verbs.empty()) return false;
    const Record* verb = DefaultRecord(verbs);
    return TakesObject(verb->partCode.substr(1), verb->semantic);
}

// Reads verb item it, whose (ら)れ form is spelled, as the passive of the verb it is made from, the
// way the analysis reads such a verb (調べられた 조사되었다, 掴まれた); false, with nothing changed,
// without that verb.
bool ReadAsBaseVerb(Flusher& f, Item* it, std::string_view spelled) {
    std::string_view base;
    const std::vector<const Record*> verbs = BaseVerbRecords(f, spelled, base);
    if (verbs.empty()) return false;
    std::vector<Entry*> entries;
    for (const Record* r : verbs) {
        Entry* e = f.w.NewEntry(*r);
        e->gattr += "Pi";
        entries.push_back(e);
    }
    it->group->entries = std::move(entries);
    it->group->surface.assign(base);
    it->group->length = static_cast<int>(base.size());
    it->detail->record = 1;
    return true;
}

// The plain verb of the honorific verb target text (읽으&AE@ 읽다, 입원하&AE@ 입원하다) as a
// conjugating target: the verb of the passive stem surface when the dictionary has one (作られ
// 만들다), else the verb whose honorific is text (its stem without the syllable before 시, the
// 으 that HonorificTarget puts after a final consonant, or the stem itself), or the honorific
// itself as kSuppletive says (드시고 계신다); "" for an honorific suffix alone, 계시다 and 돌아가시다.
std::string PlainOfHonorific(Flusher& f, std::string_view surface, std::string_view text) {
    const std::string lemma = output::Lemma(text, f.families);
    if (!lemma.ends_with(kHonorificDa) || lemma.size() == kHonorificDa.size()) return {};
    for (const Suppletive& own : kSuppletive) {
        if (!lemma.ends_with(own.honorific)) continue;
        if (own.progressive == OwnProgressive::None) return {};
        if (own.progressive == OwnProgressive::Itself) return std::string(text);
    }
    bool transitive = false;
    std::string plain;
    if (!BaseVerbTarget(f, surface, transitive, plain).empty() && !plain.empty()) return plain;
    const std::string_view head = std::string_view(lemma).substr(0, lemma.size() - kHonorificDa.size());
    for (const std::string_view stem : {head.substr(0, head.size() - DecodeLastRune(head).size), head}) {
        if (stem.empty()) continue;
        output::Conjugated c = output::Conjugating(std::string(stem) + std::string(kKoDa), '&', "", f.families);
        if (!c.error.empty()) continue;
        if (output::Lemma(HonorificTarget(f, c.target), f.families) == lemma) return std::move(c.target);
    }
    return {};
}

std::string ExistentialToVerb(Flusher& f, std::string_view rest);

// The honorific of the existential 있다 (계&AE@ 계시다).
std::string HonorificExistence(Flusher& f) { return HonorificTarget(f, kExistenceMarker); }

// The ending written for the existential 있 in rest (있다, 있으니까, 있었다) for its honorific
// 계시다 (계신다, 계시니까, 계셨다); "" for an ending ExistentialToVerb does not cover.
std::string HonorificExistential(Flusher& f, std::string_view rest) {
    const std::string verb = ExistentialToVerb(f, rest);
    const std::string honorific = HonorificExistence(f);
    if (verb.empty() || honorific.size() < 4) return {};
    const std::string_view family = std::string_view(honorific).substr(honorific.size() - 4);
    const std::string_view row = LookupSource(std::string(family) + verb[0], f.families);
    if (row.empty()) return {};
    return honorific.substr(0, honorific.size() - 4) + std::string(row) + verb.substr(1);
}

// The respectful verb item it before the auxiliary いる of its て form in its token is the plain
// verb with 고 and the honorific of いる (書かれている 쓰고 계신다, 置かれていた 두고 계셨다): the
// auxiliary takes the honorific. A verb whose ending holds the いる (書かれてる 1고 있다) takes
// 고 and 계시다 in its ending. False, with nothing changed, when no such auxiliary follows.
bool HonorificProgressive(Flusher& f, Item* it) {
    if (it->DirectIndex() < 0) return false;
    const std::string_view ending = it->DirectText(it->DirectIndex());
    Item* aux = it->After();
    if (aux == nullptr || aux->tokenIndex != it->tokenIndex || !aux->detail->Yougo() || aux->group->surface != "い" ||
        aux->selected < 0 || ItemLemma(f, aux, aux->selected) != "있다") {
        for (const std::string_view head : {kProgressiveHead, kResultativeHead}) {
            if (!ending.starts_with(head)) continue;
            const std::string honorific = HonorificExistential(f, ending.substr(head.size()));
            if (honorific.empty()) return false;
            it->AssignDirectSlot(it->DirectIndex(), std::string(kProgressiveHead) + honorific);
            return true;
        }
        return false;
    }
    if (ending != "6" && ending != "1고") return false;
    const std::string honorific = HonorificTarget(f, aux->EntryText(aux->selected));
    if (honorific.empty()) return false;
    aux->AssignEntrySurface(aux->selected, honorific);
    // 계시다 takes the present of a verb (계신다), not the 있다 of gram Va.
    if (Entry* e = aux->group->At(aux->selected); e != nullptr) DropGrammarCode(e->gattr, "Va");
    it->AssignDirectSlot(it->DirectIndex(), "1고");
    return true;
}

// The object that the passive made a subject is the object again before the respectful verb item
// it (本を説明された 책을).
void ObjectAgain(Item* it) {
    for (Item* p = it->prev; p != nullptr; p = p->prev) {
        if (p->group->surface != "を") continue;
        if (p->selected >= 0 && p->EntryText(p->selected) == "(이)가") p->AssignEntrySurface(p->selected, "(을)를");
        break;
    }
}

// The kind of (ら)れ a verb item carries: the れ of a godan passive (書かれ, record 1), the られ of
// an ichidan or くる verb (食べられ, 来られ, record 4), the potential of a godan verb in ーaる that
// also spells the passive of the shorter verb before its a-row kana (絡まれ, 掴まれ, record 4), or
// a dictionary word that ends in (ら)れ (見られ, 盗まれ, され).
enum class VoiceForm { None, Godan, Rare, Shorter, Lexical };

// The empty ending of a folded causative has both the causative's S52 and the potential's S54
// candidates until voice is known. S54 carries 수 itself, leaving only the causative to compose.
bool CausativeEnding(Item* it, bool potential) {
    Detail& d = *it->detail;
    if (!d.syntheticCausative || d.kind != 2 || d.group == nullptr) return false;
    bool found = false;
    for (std::size_t i = 0; i < d.group->entries.size(); i++) {
        const char form = At(d.group->entries[i]->code, 1);
        if (form != '2' && form != '4') continue;
        if (i < d.enabled.size()) {
            d.enabled[i] = d.enabled[i] && form == (potential ? '4' : '2');
            found = found || (potential && d.enabled[i]);
        }
    }
    return found;
}

// The slot the defaults choose before any rule: the first candidate coded XXX, else the first.
int DefaultSlot(const Item* it) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        if (Has(it->Attr(slot), "XXX")) return slot;
    }
    return 0;
}

bool ExperiencedCausative(const Item* it, int slot) {
    return std::ranges::any_of(dict::ClassMembers(HDTRANS_CLASS("verb.cp-experience")),
                               [&](std::string_view word) { return IsWord(it, word, slot); }) ||
           dict::StartsWithMember(HDTRANS_CLASS("noun.cp-change"), it->group->surface) > 0;
}

// The form of the (ら)れ of verb item it. stem is the verb before it, rest the text after it,
// spelled the verb with it (for Shorter, 絡まれ) and ichidan whether it is the られ of an ichidan or
// くる verb.
VoiceForm VoiceFormOf(Flusher& f, const Item* it, std::string_view& stem, std::string_view& rest, std::string& spelled,
                      bool& ichidan) {
    const int record = it->detail->record;
    const bool synthetic = record == 5 && it->detail->syntheticCausative;
    if (!synthetic && record != 0 && record != 1 && record != 4) return VoiceForm::None;
    const std::string_view surface = it->group->surface;
    rest = From(it->text, it->start + static_cast<long>(surface.size()));
    stem = surface;
    if (synthetic) {
        const int slot = it->selected >= 0 ? it->selected : DefaultSlot(it);
        const bool copula = output::CopulaTarget(it->EntryText(slot), f.families);
        const bool particle = GrammarCode(it->GAttr(slot), "Pj");
        const bool experienced = ExperiencedCausative(it, slot);
        // Selection runs later; mixed classifications cannot authorize a new potential reading.
        for (int candidate = 0; candidate < static_cast<int>(it->group->entries.size()); candidate++) {
            if (output::CopulaTarget(it->EntryText(candidate), f.families) != copula ||
                GrammarCode(it->GAttr(candidate), "Pj") != particle ||
                ExperiencedCausative(it, candidate) != experienced) {
                return VoiceForm::None;
            }
        }
        if (copula || (particle && HonouredPerson(it->prev))) return VoiceForm::None;
        const std::size_t rare = rest.find(kRare);
        if (rare == std::string_view::npos ||
            surface.size() + rare + kRare.size() > static_cast<std::size_t>(it->length)) {
            return VoiceForm::None;
        }
        spelled.assign(surface);
        spelled += rest.substr(0, rare);
        stem = spelled;
        rest.remove_prefix(rare + kRare.size());
        // A following noun such as なし is not a conjugated negative.
        const Item* n = it->After();
        if (n != nullptr && !n->group->surface.empty() && n->Code(0).starts_with("1") &&
            rest.starts_with(n->group->surface) &&
            dict::StartsWithMember(HDTRANS_CLASS("ending.rare-potential"), rest) > 0) {
            return VoiceForm::None;
        }
        ichidan = true;
        return VoiceForm::Rare;
    }
    // The れ comes right after the stem or after its one a-row kana.
    const std::size_t re = rest.substr(0, 2 * kRareRe.size()).find(kRareRe);
    if (record == 1) {
        if (re == std::string_view::npos) return VoiceForm::None;
        rest.remove_prefix(re + kRareRe.size());
        return VoiceForm::Godan;
    }
    if (record == 4) {
        const char k = At(it->Code(0), 2);
        if ((k == '7' || k == ';' || k == '=') && rest.starts_with(kRare)) {
            rest.remove_prefix(kRare.size());
            ichidan = true;
            return VoiceForm::Rare;
        }
        // The passive of a shorter verb, of more than one kana.
        if (k != '8' || re != 0) return VoiceForm::None;
        spelled.assign(surface);
        spelled += kRareRe;
        std::string_view base;
        const std::vector<const Record*> verbs = BaseVerbRecords(f, spelled, base);
        if (verbs.empty() || (text::RuneCount(base) < 2 && !text::IsKanji(text::DecodeRune(base).cp))) return VoiceForm::None;
        rest.remove_prefix(kRareRe.size());
        stem = base;
        return VoiceForm::Shorter;
    }
    if (it->detail->cls != kDetailVerb) return VoiceForm::None;
    for (const auto& [ending, kinds] : kPassiveEndings) {
        // A word needs a verb before its ending, except the bare される (kind 3); なれ is なれる.
        if (!surface.ends_with(ending) || (surface.size() == ending.size() && kinds != "3")) continue;
        stem = surface.substr(0, surface.size() - ending.size());
        spelled.assign(surface);
        ichidan = Has(kinds, "7");
        return VoiceForm::Lexical;
    }
    return VoiceForm::None;
}

// The agent of the (ら)れ of verb item it, the verb stem before it: going back through its clause,
// the nearest に, によって or により after a person or an animal (Animate), not a に that ends a
// quotation (次の王になんて言われたら: particle.quote right after it). A verb whose に names the other party
// (gram Rc, verb.transfer: 任せ, 借り; the acts done to someone of verb.passive-after-object) has
// none in に, which the clause decides (PassiveReading), and the に right before a bare される is
// what one is made into (バカにされる). The particle item, nullptr without one; ni tells whether it
// is に.
Item* AnimateAgent(const Item* it, std::string_view stem, bool& ni) {
    bool recipient = dict::InClass(HDTRANS_CLASS("verb.transfer"), stem) || dict::InClass(HDTRANS_CLASS("verb.passive-after-object"), stem);
    for (int slot = 0; slot < static_cast<int>(it->group->entries.size()); slot++) {
        recipient = recipient || GrammarCode(it->GAttr(slot), "Rc");
    }
    for (Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        ni = s == kAgentNi;
        if (ni && p == it->prev && stem.empty()) return nullptr;
        if ((ni && !recipient) || dict::InClass(HDTRANS_CLASS("particle.voice-agent"), s)) {
            const Item* a = p->prev;
            const Item* n = p->next;
            if (a != nullptr && !a->detail->Yougo() && Animate(a->Attr(std::max(a->selected, 0))) &&
                (n == nullptr || !dict::InClass(HDTRANS_CLASS("particle.quote"), n->group->surface))) {
                return p;
            }
        } else if (ClauseBreak(p)) {
            break;
        }
    }
    return nullptr;
}

// Whether a word of word.ability-scope (誰でも, 自分で), or a particle of particle.even-person after a
// person (子供でも), stands before item it in its clause, whose (ら)れ, followed by rest, is no
// condition.
// Whether a word of noun.ability-condition with なら stands before item it in its clause: the
// situation that makes the act possible (これなら, ここなら).
bool AbilityCondition(const Item* it) {
    for (const Item* p = it->prev; p != nullptr && !ClauseBreak(p); p = p->prev) {
        if (p->group->surface == "なら" && p->prev != nullptr &&
            dict::InClass(HDTRANS_CLASS("noun.ability-condition"), p->prev->group->surface)) {
            return true;
        }
    }
    return false;
}

bool AbilityScope(const Item* it, std::string_view rest) {
    if (dict::StartsWithMember(HDTRANS_CLASS("ending.voice-condition"), rest) > 0) return false;
    for (const Item* p = it->prev; p != nullptr && !ClauseBreak(p); p = p->prev) {
        const std::string_view s = p->group->surface;
        if (dict::InClass(HDTRANS_CLASS("word.ability-scope"), s)) return true;
        if (dict::InClass(HDTRANS_CLASS("particle.even-person"), s) && p->prev != nullptr &&
            Person(p->prev->Attr(std::max(p->prev->selected, 0)))) {
            return true;
        }
    }
    return false;
}

// Whether the (ら)れ of item it, followed by rest, is a condition whose main clause right after it
// is a trouble of stem.adverse-outcome (見られたら困る).
bool AdverseCondition(const Item* it, std::string_view rest) {
    if (dict::StartsWithMember(HDTRANS_CLASS("ending.voice-condition"), rest) == 0) return false;
    const Item* n = it->After();
    return n != nullptr && dict::StartsWithMember(HDTRANS_CLASS("stem.adverse-outcome"), n->group->surface) > 0;
}

// Whether a noun of noun.suffered-act, or a noun that ends in こと (同じこと), stands right before
// item it, or before a を right before it (どんなことされた, 何をされた).
bool SufferedActBefore(const Item* it) {
    const Item* p = it->prev;
    if (p != nullptr && p->group->surface == "を") p = p->prev;
    if (p == nullptr || p->detail->Yougo()) return false;
    const std::string_view s = p->group->surface;
    return dict::InClass(HDTRANS_CLASS("noun.suffered-act"), s) || s.ends_with("こと");
}

// Whether the (ら)れ of verb item it, followed by rest, is a condition (ending.voice-condition but ては)
// with an object of を before it in its clause or in the word (服を着られたら, 銃口を向けられても).
bool ConditionWithObject(const Item* it, std::string_view rest) {
    // たらしい is a guess, not a condition (話を振られたらしく), and ては also strings acts one after another
    // (胸を揉まれては、はだけ).
    return dict::StartsWithMember(HDTRANS_CLASS("ending.voice-condition"), rest) > 0 && !rest.starts_with("たらし") &&
           !rest.starts_with("ては") && (Has(it->group->surface, "を") || ActedObjectBefore(it));
}

// Whether a noun before a subject or topic particle (が, は, も), or a person or an animal before a
// particle in に or before から, によって or により, stands before item it in its sentence: who the
// passive is done to (私がこんな演劇を見せられたら, モデルはカメラを向けられると) or who may do it
// (嬉野さんに俺たちが付き合っていることを知られたら, 誰にだって), though another clause stands between.
bool PassiveRoleInSentence(const Item* it) {
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s.empty() || Has("。！？!?「」『』", s)) break;
        const Entry* e = p->detail->Yougo() ? nullptr : p->group->At(std::max(p->selected, 0));
        const Item* a = p->prev;
        if (e == nullptr || e->cls != 'P' || a == nullptr || a->detail->Yougo()) continue;
        if (s == "が" || s == "は" || s == "も") return true;
        if ((s.starts_with(kAgentNi) || s == "から" || s.starts_with("によ")) && Person(a->Attr(std::max(a->selected, 0)))) {
            return true;
        }
    }
    return false;
}

// Whether surface has a Korean passive lemma of its own in kPassiveLemmas (褒め 칭찬받다), not one in
// 지다 that only puts 어지다 on a verb (見せ 보여지다).
bool OwnPassiveLemma(std::string_view surface) {
    for (const PassiveLemma& p : kPassiveLemmas) {
        if (p.surface == surface) return !p.lemma.substr(0, p.lemma.find(':')).ends_with("지다");
    }
    return false;
}

// Whether every candidate of the dictionary word item it reads its passive with 어지다 or 되다 (知られ
// 알려지다), no Korean passive word that takes an object.
bool WordlessPassive(Flusher& f, const Item* it) {
    for (int slot = 0; slot < static_cast<int>(it->group->entries.size()); slot++) {
        const std::string lemma = ItemLemma(f, it, slot);
        if (!lemma.ends_with("지다") && !lemma.ends_with("되다")) return false;
    }
    return !it->group->entries.empty();
}

// The voice that candidate slot of item it reads: the potential (볼 수 있다, gram Ke), the
// respectful form in 시, or else none.
Voice CandidateVoice(Flusher& f, const Item* it, int slot) {
    if (it->EntryText(slot).ends_with(kHonorificMarker)) return Voice::Respectful;
    const std::string lemma = ItemLemma(f, it, slot);
    if (GrammarCode(it->GAttr(slot), "Ke") || Has(lemma, kKoCan) || Has(lemma, kKoCannot)) return Voice::Potential;
    return Voice::None;
}

// Keeps the candidates of item it that read voice v (the passive and the spontaneous form: neither
// the potential nor the respectful form) and reports whether one does.
bool KeepVoiceCandidates(Flusher& f, Item* it, Voice v) {
    const Voice want = v == Voice::Potential || v == Voice::Respectful ? v : Voice::None;
    std::vector<Entry*> kept;
    for (int slot = 0; slot < static_cast<int>(it->group->entries.size()); slot++) {
        if (CandidateVoice(f, it, slot) == want) kept.push_back(it->group->entries[static_cast<std::size_t>(slot)]);
    }
    if (kept.empty()) return false;
    it->group->entries = std::move(kept);
    return true;
}

// Whether the (ら)れ of verb item it, followed by rest, asks the listener politely about an act of
// theirs: only kana up to a polite form of ending.voice-polite (no other word between) and a question
// at the end of its sentence (mark.voice-question), no particle of particle.voice-subject before it
// in its clause, and an object unless the verb takes none (何を読まれているんですか, 確認されますか).
bool ListenerQuestion(const Item* it, std::string_view rest, bool transitive) {
    std::string_view sentence = rest;
    for (const std::string_view mark : dict::ClassMembers(HDTRANS_CLASS("mark.voice-end"))) sentence = sentence.substr(0, sentence.find(mark));
    const std::string_view after = From(rest, static_cast<long>(sentence.size()));
    if (dict::StartsWithMember(HDTRANS_CLASS("mark.voice-question"), after) == 0 && dict::EndsWithMember(HDTRANS_CLASS("mark.voice-question"), sentence) == 0) {
        return false;
    }
    std::size_t polite = std::string_view::npos;
    for (const std::string_view m : dict::ClassMembers(HDTRANS_CLASS("ending.voice-polite"))) polite = std::min(polite, sentence.find(m));
    if (polite == std::string_view::npos) return false;
    for (std::string_view t = sentence.substr(0, polite); !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (tokenizer::ClassFlags(r.cp, 2) != tokenizer::kClassHiragana) return false;
        t.remove_prefix(r.size == 0 ? 1 : r.size);
    }
    for (const Item* p = it->prev; p != nullptr && !ClauseBreak(p); p = p->prev) {
        if (dict::InClass(HDTRANS_CLASS("particle.voice-subject"), p->group->surface)) return false;
    }
    return !transitive || ObjectBefore(it);
}

// Whether WriteLexicalPassive makes the passive verb item it a word of its own (褒められ
// 칭찬받다, と言われ 라고 불리다), which keeps the passive reading.
bool LexicalPassive(Flusher& f, const Item* it) {
    std::string target;
    if (InSet(kAttitudePassives, it->group->surface) && ObjectBefore(it)) return false;
    return PassiveTarget(f, it->group->surface, target) || SayPassiveApplies(it);
}

// The respectful reading of the (ら)れ of verb item it of the given form (RespectfulReading): not
// for a verb whose (ら)れ is the passive by itself, a potential before a negative or a condition, or
// a passive that a Korean word of its own reads; a dictionary word needs a candidate in 시 or a
// verb it is made from.
bool Respectful(Flusher& f, const Item* it, VoiceForm form, std::string_view stem, std::string_view rest, bool passive,
                bool ichidan) {
    const std::string_view surface = it->group->surface;
    if (form == VoiceForm::Shorter) return false;
    if (form != VoiceForm::Lexical) {
        if (dict::InClass(HDTRANS_CLASS("verb.passive-any-object"), surface) || dict::InClass(HDTRANS_CLASS("verb.passive-after-object"), surface) ||
            dict::InClass(HDTRANS_CLASS("verb.receiver-passive"), surface) || CompoundAction(surface)) {
            return false;
        }
        if (form == VoiceForm::Rare && !passive &&
            (dict::InClass(HDTRANS_CLASS("verb.potential-rareta"), surface) || rest.starts_with("な") || rest.starts_with("ず") ||
             rest.starts_with("ぬ") || rest.starts_with("ん") || rest.starts_with("れば") || rest.starts_with("たら"))) {
            return false;
        }
        if (passive && LexicalPassive(f, it)) return false;
        const int slot = DefaultSlot(it);
        const bool transitive = TakesObject(it->Code(slot), it->Attr(slot));
        return RespectfulReading(it, transitive) || (form == VoiceForm::Godan && ListenerQuestion(it, rest, transitive));
    }
    // The られ of an ichidan or くる word asks for what the listener can do as well (来られますか), so
    // only a subject makes it respectful. Without an object a verb reads respectful in the most
    // clauses; no lookup when not even then.
    const bool asks = !ichidan && ListenerQuestion(it, rest, false);
    if (!RespectfulReading(it, false) && !asks) return false;
    bool transitive = false;
    const int count = static_cast<int>(it->group->entries.size());
    bool honorific = false;
    for (int slot = 0; slot < count; slot++) honorific = honorific || it->EntryText(slot).ends_with(kHonorificMarker);
    if (honorific) {
        transitive = stem.empty() && SinoNounTakesObject(it->prev);
    } else if (surface != kIware) {
        std::string plain;
        if (BaseVerbTarget(f, surface, transitive, plain).empty()) return false;
    }
    return RespectfulReading(it, transitive) || (asks && ListenerQuestion(it, rest, transitive));
}

// A forcing adverb in the same clause names a forced act, even when its predicate is negative.
bool ForcingInClause(const Item* it) {
    for (const Item* p = it->prev; p != nullptr && !ClauseBreak(p); p = p->prev) {
        if (dict::StartsWithMember(HDTRANS_CLASS("adverb.forcing"), p->group->surface) > 0) return true;
    }
    return false;
}

// たら, たり, affirmative ます and the presumptive よう alone do not establish ability after a
// derived causative.
bool CausativePotentialEnding(std::string_view rest) {
    return dict::StartsWithMember(HDTRANS_CLASS("ending.rare-potential"), rest) > 0 &&
           !rest.starts_with("たら") && !rest.starts_with("たり") && !rest.starts_with("ます") && !rest.starts_with("よう");
}

// The voice of the (ら)れ of verb item it, first signal first (spec GRM-10): an agent that is a
// person or an animal makes the passive; then the respectful form; a godan passive, and たい after
// it, the passive; a word of ability the potential; a trouble after its condition the passive; a
// verb of feeling the spontaneous form; the form of an ichidan or くる verb and its clause the rest
// (PassiveReading). The potential of a verb in ーaる that spells a shorter verb's passive reads only
// its own signals. A dictionary word no signal decides keeps its candidates (None).
Voice VoiceOf(Flusher& f, const Item* it, VoiceForm form, std::string_view stem, std::string_view rest, bool passive,
              bool ichidan, const Item* agent) {
    // The shorter verb's passive (腕を掴まれた), with its object or after an agent in a narrative form;
    // else the potential of the verb in ーaる stays (繋がれると思った).
    if (form == VoiceForm::Shorter) {
        return ObjectBefore(it) || (agent != nullptr && NarrativePassive(it, rest)) ? Voice::Passive : Voice::None;
    }
    // The られ of an ichidan verb that takes no object has no Korean passive (寝られたら困る), so only
    // its form and clause decide it; an object in its clause or in the word (服を着) shows one.
    const std::string_view surface = it->group->surface;
    const bool synthetic = it->detail->record == 5 && it->detail->syntheticCausative;
    const bool object = synthetic || form != VoiceForm::Rare || dict::InClass(HDTRANS_CLASS("verb.passive-any-object"), surface) ||
                        CompoundAction(surface) || Transitive(it) || ObjectBefore(it) || Has(surface, "を");
    if ((synthetic && ForcingInClause(it)) || (agent != nullptr && object)) return Voice::Passive;
    if (synthetic && !CausativePotentialEnding(rest) &&
        (NarrativePassive(it, rest) || rest.starts_with("ちゃ") || rest.starts_with("ちま"))) {
        return Voice::Passive;
    }
    if (Respectful(f, it, form, stem, rest, passive, ichidan)) return Voice::Respectful;
    if (form == VoiceForm::Godan || (form == VoiceForm::Lexical && stem.empty() && SufferedActBefore(it))) {
        return Voice::Passive;
    }
    if (object && dict::StartsWithMember(HDTRANS_CLASS("ending.voice-desire"), rest) > 0) return Voice::Passive;
    if (AbilityScope(it, rest)) return Voice::Potential;
    // A trouble and a verb of feeling tell the passive from the potential, which only the られ of an
    // ichidan or くる verb also reads.
    if (ichidan && object && AdverseCondition(it, rest)) return Voice::Passive;
    if (ichidan && object && dict::InClass(HDTRANS_CLASS("verb.spontaneous"), stem)) return Voice::Spontaneous;
    if (synthetic) {
        // A derived causative needs a potential cue; unmarked られる keeps the causative-passive.
        // An object alone does not turn an experienced event into an ability.
        // The presumptive よう reads the potential only after a condition that makes the act possible.
        const bool potentialEnding = CausativePotentialEnding(rest) || (rest.starts_with("よう") && AbilityCondition(it));
        const int slot = it->selected >= 0 ? it->selected : DefaultSlot(it);
        if (ExperiencedCausative(it, slot) && !potentialEnding) return Voice::Passive;
        return !passive && potentialEnding ? Voice::Potential : Voice::Passive;
    }
    if (form == VoiceForm::Rare) return passive ? Voice::Passive : Voice::Potential;
    return Voice::None;
}

// The e-row kana that ends the potential of a godan verb and the conjugation kind (the last digit of
// the part code) of that verb (撮れ 撮る 8, 書け 書く 1).
constexpr std::string_view kPotentialKana[][2] = {
    {"え", "0"}, {"け", "1?"}, {"げ", "2"}, {"せ", "3"}, {"て", "4"}, {"ね", "<"}, {"べ", "6"}, {"め", "5"}, {"れ", "8"},
};

// Whether item p, before a verb, names the place where it is done: ここで, そこで, どこで, or で, では
// after a noun of a place (VPl).
bool PlaceDe(const Item* p) {
    const std::string_view s = p->group->surface;
    if (s == "ここで" || s == "そこで" || s == "あそこで" || s == "どこで" || s == "ここでは" || s == "そこでは") return true;
    const Item* a = p->prev;
    return (s == "で" || s == "では") && a != nullptr && !a->detail->Yougo() && Has(a->Attr(std::max(a->selected, 0)), "VPl");
}

// Whether the clause before verb item it, followed by rest, asks for the potential of a godan verb
// rather than the intransitive word of the same spelling (spec GRM-10): an object of を, a word of
// word.ability-scope, or a place of で before a negative or a polite non-past (ここで写真は撮れません).
bool PotentialClause(const Item* it, std::string_view rest) {
    if (ObjectBefore(it) || AbilityScope(it, rest)) return true;
    const bool form = rest.starts_with("な") || rest.starts_with("ず") || rest.starts_with("ぬ") || rest.starts_with("ませ") ||
                      rest.starts_with("ます");
    for (const Item* p = it->prev; form && p != nullptr && !ClauseBreak(p); p = p->prev) {
        if (PlaceDe(p)) return true;
    }
    return false;
}

// A dictionary word that spells the potential of a godan verb (撮れ 撮る) and reads only the Korean
// passive of that verb (찍히다 of 찍다) is read as that potential when its clause asks for one
// (PotentialClause): the godan verb analysed at its place, as the analysis reads a godan potential
// (ここで写真は撮れません 사진은 찍을 수 없습니다).
void SpelledPotential(Flusher& f, Item* it) {
    if (it->detail->record != 0 || it->detail->cls != kDetailVerb || it->Fixed() || it->group->entries.empty()) return;
    const std::string_view surface = it->group->surface;
    const text::Rune last = DecodeLastRune(surface);
    if (surface.size() <= last.size || !text::IsKanji(text::DecodeRune(surface).cp)) return;
    const std::string_view kana = surface.substr(surface.size() - last.size);
    std::string_view kinds;
    for (const auto& [k, kind] : kPotentialKana) {
        if (kana == k) kinds = kind;
    }
    if (kinds.empty()) return;
    const std::string_view base = f.w.arena.Keep(std::string(surface.substr(0, surface.size() - kana.size())));
    const Lookup found = f.w.DoLookup(base, kModeGeneral);
    std::vector<std::string> passives;
    for (const Record* r = found.records; r != nullptr; r = r->child) {
        if (r->source != base || At(r->partCode, 1) != '4' || !Has(kinds, std::string(1, At(r->partCode, 3)))) continue;
        if (std::string p = KoreanPassiveLemma(output::Lemma(r->target, f.families)); !p.empty()) passives.push_back(std::move(p));
    }
    for (int slot = 0; slot < static_cast<int>(it->group->entries.size()); slot++) {
        if (At(it->Code(slot), 2) != '7' || std::ranges::find(passives, ItemLemma(f, it, slot)) == passives.end()) return;
    }
    const std::string_view rest = From(it->text, it->start + static_cast<long>(surface.size()));
    if (!PotentialClause(it, rest)) return;
    Item* potential = f.w.InsertLateCandidate(it->text, it->start, it->tokenIndex, found.records);
    if (potential == nullptr || potential->detail->record != 4 || potential->length != it->length ||
        potential->group->surface != base) {
        return;
    }
    it->group = potential->group;
    it->detail = potential->detail;
    it->detail->voice = Voice::Potential;
}

// Whether surface is a dictionary word in (ら)れ made from a godan verb (知られ 知る), whose (ら)れ
// with an object is the passive; that of an ichidan verb (受け止められ) is as often the potential.
bool GodanPassiveWord(Flusher& f, std::string_view surface) {
    std::string_view base;
    const std::vector<const Record*> verbs = BaseVerbRecords(f, surface, base);
    return !verbs.empty() && At(DefaultRecord(verbs)->partCode, 3) != '7' && !PassiveBaseVerb(f, surface).empty();
}

// Whether the Korean that verb item it selected reads a passive that takes no object: 어지다 yet to be
// written (record 1), a one-word lemma in 지다 or in a noun with 되다 (not the 게 되다 of a
// causative-passive), or one of ko-verb.passive-subject.
bool SubjectOnlyPassive(Flusher& f, const Item* it) {
    const std::string lemma = output::Lemma(it->EntryText(it->selected), f.families);
    // A Korean verb in 지다 takes no 어지다: it is written as it is, an active verb (데리고 사라지다).
    if (it->detail->record == 1 && !it->lexical) return !lemma.ends_with("지다");
    if (it->detail->record != 0) return false;
    constexpr std::string_view kDoeda = "되다";
    // A phrase takes its object with its first verb (데리고 사라지다).
    if (Has(lemma, " ")) return false;
    return lemma.ends_with("지다") ||
           (lemma.ends_with(kDoeda) && lemma.size() > kDoeda.size() && lemma[lemma.size() - kDoeda.size() - 1] != ' ') ||
           dict::InClass(HDTRANS_CLASS("ko-verb.passive-subject"), lemma);
}

// The object of a passive whose Korean takes none is its subject (名前を呼ばれた 이름이 불렸다, 秘密を
// 知られた 비밀이 알려졌다): the を before verb item it in its clause, with no pause, no other particle and
// no verbal noun between, is (이)가, unless a subject が stands before it in the clause (彼が手紙を書かれた), which would
// make two.
// The passive is the reading the voice step decided, or a dictionary word in (ら)れ of a godan verb
// that no signal decided (知られ 알려지다).
void ObjectAsSubject(Flusher& f, Item* it) {
    const bool passive = it->detail->voice == Voice::Passive || it->detail->voice == Voice::Spontaneous ||
                         (it->detail->voice == Voice::None && GodanPassiveWord(f, it->group->surface));
    // The (ら)れ of the auxiliary しまう (姿を隠してしまわれた) is respectful, and its object is the verb's.
    const std::string_view surface = it->group->surface;
    if (!passive || dict::InClass(HDTRANS_CLASS("verb.respectful"), surface) || surface.ends_with("てしま") || surface.ends_with("でしま") ||
        !SubjectOnlyPassive(f, it)) {
        return;
    }
    // The られ of an ichidan verb with an object outside the past and the て form is as often the
    // potential (これなら奴らをやっつけられる).
    const std::string_view rest = From(it->text, it->start + static_cast<long>(surface.size()));
    if (const char k = At(it->Code(it->selected), 2); (k == '7' || k == ';' || k == '=') && rest.starts_with(kRare)) {
        const std::string_view form = From(rest, static_cast<long>(kRare.size()));
        if (!form.starts_with("た") && !form.starts_with("て")) return;
    }
    for (Item* p = it->prev; p != nullptr; p = p->prev) {
        if (p->AfterPause(false) && p->prev != nullptr && p->prev->group->surface == "を") return;
        // A particle or a verbal noun between belongs to a word of its own (解消を前提に結ばれた,
        // 一生を本に囲まれて, ビームを反射するために作られた).
        if (const Entry* e = p->detail->Yougo() ? nullptr : p->group->At(std::max(p->selected, 0));
            e != nullptr && ((e->cls == 'P' && p->group->surface != "を") || GrammarCode(e->gattr, "Hd"))) {
            return;
        }
        if (p->group->surface == "を") {
            for (const Item* q = p->prev; q != nullptr && !ClauseBreak(q); q = q->prev) {
                if (q->group->surface == "が" && q->prev != nullptr && !q->prev->detail->Yougo()) return;
            }
            if (!p->Fixed() && p->selected >= 0 && p->EntryText(p->selected) == "(을)를") {
                p->AssignEntrySurface(p->selected, "(이)가");
            }
            return;
        }
        if (ClauseBreak(p)) return;
    }
}

}  // namespace

void DecideVoice(Flusher& f) {
    std::string spelled;
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->group->entries.empty()) continue;
        std::string_view stem, rest;
        bool ichidan = false;
        const VoiceForm form = VoiceFormOf(f, it, stem, rest, spelled, ichidan);
        if (form == VoiceForm::None) {
            CausativeEnding(it, false);
            SpelledPotential(f, it);
            continue;
        }
        const bool passive = form == VoiceForm::Godan || (form == VoiceForm::Rare && PassiveReading(f.w, it, rest));
        bool ni = false;
        Item* agent = AnimateAgent(it, stem, ni);
        const Voice v = VoiceOf(f, it, form, stem, rest, passive, ichidan, agent);
        if (it->detail->record == 5 && it->detail->syntheticCausative) {
            it->detail->voice = v == Voice::Potential ? Voice::Potential : Voice::Passive;
            if (CausativeEnding(it, v == Voice::Potential)) it->detail->record = 2;
            if (v == Voice::Potential) it->AppendRuleCandidateMarker("Ka");
            continue;
        }
        it->detail->voice = v;
        const bool passiveVoice = v == Voice::Passive || v == Voice::Spontaneous;
        // The candidates of a word no rule may change (a user's word) stay as they are.
        const bool own = it->Fixed();
        // A passive in a condition with its object and no agent whose Korean has no passive word
        // of its own (a dictionary word no signal decides reads the passive of its candidates) is
        // left to the analysed passive, which condition-active writes in the active (服を着られたら
        // 옷을 입으면, 正体を知られても 정체를 알아도).
        const bool doerLeftOut = (passiveVoice || (form == VoiceForm::Lexical && v == Voice::None)) && !own &&
                                 agent == nullptr && ConditionWithObject(it, rest) && !PassiveRoleInSentence(it);
        it->detail->doerLeftOut = doerLeftOut;
        if (form == VoiceForm::Lexical) {
            // A word whose candidates all read the potential is read as the passive of the verb it is
            // made from when that verb takes an object (調べられた 조사되었다).
            if (!own && v != Voice::None && !KeepVoiceCandidates(f, it, v) && passiveVoice &&
                BaseTakesObject(f, spelled) && ReadAsBaseVerb(f, it, spelled)) {
                WriteLexicalPassive(f, it);
            } else if (doerLeftOut && WordlessPassive(f, it)) {
                ReadAsBaseVerb(f, it, spelled);
            }
        } else if (form == VoiceForm::Shorter) {
            if (!own && passiveVoice && ReadAsBaseVerb(f, it, spelled)) WriteLexicalPassive(f, it);
        } else if (passiveVoice || (v == Voice::Respectful && passive)) {
            if (form == VoiceForm::Rare) {
                it->detail->record = 1;
                for (Entry* e : it->group->entries) {
                    if (e->gattr.ends_with("Ka")) e->gattr = e->gattr.substr(0, e->gattr.size() - 2) + "Pi";
                }
            }
            // A passive with a person before に and an object is that person's act (voice-agent,
            // GRM-2): no Korean passive word is written for it (弟にケーキを食べられた 남동생이
            // 케이크를 먹었다, not 먹혔다).
            if (passiveVoice && !(agent != nullptr && ni && ActedObjectBefore(it)) &&
                !(doerLeftOut && !OwnPassiveLemma(it->group->surface) && !SayPassiveApplies(it))) {
                WriteLexicalPassive(f, it);
            }
        }
        // The agent of a passive after a person or an animal is 에게 (猫に引っかかれた 고양이에게).
        if (passiveVoice && agent != nullptr && ni && !agent->Fixed()) {
            std::vector<Entry*> kept;
            for (Entry* e : agent->group->entries) {
                if (e->dst == kKoAgent) kept.push_back(e);
            }
            if (!kept.empty()) agent->group->entries = std::move(kept);
        }
    }
}

void OwnPassives(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->detail->record != 1 || it->lexical || it->selected < 0) continue;
        const std::string passive = KoreanPassiveLemma(output::Lemma(it->EntryText(it->selected), f.families));
        if (passive.empty()) {
            // Without a Korean passive word Korean says such a passive in the active (服を着られたら
            // 옷을 입으면), not with 어지다 and an object.
            if (it->detail->doerLeftOut) it->detail->record = 0;
            continue;
        }
        // A passive in 이다 is a plain verb, not the copula.
        output::Conjugated c = output::Conjugating(passive, '&', passive.ends_with("이다") ? "규칙" : "", f.families);
        if (!c.error.empty()) continue;
        it->detail->record = 0;
        it->lexical = true;
        it->AssignEntrySurface(it->selected, c.target);
    }
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->detail->Yougo() && it->selected >= 0) ObjectAsSubject(f, it);
    }
}

std::string PassiveBaseVerb(Flusher& f, std::string_view surface) {
    bool transitive = false;
    std::string plain;
    BaseVerbTarget(f, surface, transitive, plain);
    return plain;
}

void RespectfulPassive(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->group->entries.empty() || it->selected < 0 || it->lexical) continue;
        const std::string_view surface = it->group->surface;
        const int record = it->detail->record;
        if (record == 1 && surface.ends_with("にな")) {
            // なられ has no passive: お休みになられる is お休みになる said with more respect.
            it->detail->record = 0;
            if (it->EntryText(it->selected).ends_with("&AE@")) continue;
            const std::string target = HonorificTarget(f, it->EntryText(it->selected));
            if (!target.empty()) it->AssignEntrySurface(it->selected, target);
            continue;
        }
        const bool respectful = it->detail->voice == Voice::Respectful;
        if (respectful && (record == 1 || record == 4)) {
            if (HonorificProgressive(f, it)) {
                it->detail->record = 0;
                continue;
            }
            const int count = static_cast<int>(it->group->entries.size());
            std::vector<std::string> targets(static_cast<std::size_t>(count));
            bool ok = true;
            for (int slot = 0; slot < count && ok; slot++) {
                targets[static_cast<std::size_t>(slot)] = HonorificTarget(f, it->EntryText(slot));
                ok = !targets[static_cast<std::size_t>(slot)].empty();
            }
            if (!ok) continue;
            it->detail->record = 0;
            it->lexical = true;
            for (int slot = 0; slot < count; slot++) it->AssignEntrySurface(slot, targets[static_cast<std::size_t>(slot)]);
            continue;
        }
        // A (ら)れ word or a respectful verb (verb.respectful) read as an honorific already (読まれ
        // 읽으시다, 入院され 입원하시다, 仰 말씀하시다) before the auxiliary いる is the plain verb with
        // 고 and the honorific of いる.
        const bool stemRe = surface.ends_with("れ");
        if (record == 0 && it->EntryText(it->selected).ends_with(kHonorificMarker)) {
            if (!stemRe && !dict::InClass(HDTRANS_CLASS("verb.respectful"), surface)) continue;
            const std::string plain = PlainOfHonorific(f, surface, it->EntryText(it->selected));
            if (!plain.empty() && HonorificProgressive(f, it)) it->AssignEntrySurface(it->selected, plain);
            if (respectful) ObjectAgain(it);
            continue;
        }
        // A word written as a passive or potential stem (され, 来られ, 書かれ): its honorific
        // candidate, or the honorific of the verb it is made from.
        if (!respectful || record != 0 || !stemRe) continue;
        // 言われ read as 듣다 (言われた通り 들은 대로) is the respectful 言う, 말씀하시다 (陛下が言われた
        // こと 말씀하신 것).
        if (surface == kIware && ItemLemma(f, it, it->selected) == "듣다") {
            if (const std::string target = HonorificTarget(f, "말&AV@"); !target.empty()) {
                it->AssignEntrySurface(it->selected, target);
            }
            continue;
        }
        // Only a passive or potential Korean (ko-verb.voice-end), not a verb of its own (生まれ 태어나다).
        const std::string chosen = ItemLemma(f, it, it->selected);
        if (dict::EndsWithMember(HDTRANS_CLASS("ko-verb.voice-end"), chosen) == 0) {
            continue;
        }
        const int count = static_cast<int>(it->group->entries.size());
        int honorific = -1;
        for (int slot = 0; slot < count && honorific < 0; slot++) {
            if (it->EntryText(slot).ends_with("&AE@")) honorific = slot;
        }
        if (honorific >= 0) {
            it->SelectCandidate(honorific, {Provenance::Rewrite});
            // Before いる it is the plain verb with 고 and 계시다 (読まれてる 읽고 계신다).
            if (const std::string plain = PlainOfHonorific(f, surface, it->EntryText(honorific));
                !plain.empty() && HonorificProgressive(f, it)) {
                it->AssignEntrySurface(honorific, plain);
            }
            ObjectAgain(it);
            continue;
        }
        bool transitive = false;
        std::string plain;
        const std::string target = BaseVerbTarget(f, surface, transitive, plain);
        if (target.empty()) continue;
        // The state of the passive (gram Dx, 書かれている 쓰여 있다) is no state of the active.
        if (Entry* e = it->group->At(it->selected); e != nullptr) DropGrammarCode(e->gattr, "Dx");
        it->AssignEntrySurface(it->selected, HonorificProgressive(f, it) ? plain : target);
    }
}

namespace {

// Whether stem is a result verb of verb.result-state, or a compound or phrase that ends in one of
// two or more characters that starts with a kanji (根性が曲が 曲が, 折れ曲が 曲が); a single kanji
// matches only itself (目立 is no 立). 掛か hangs only after the place に (壁に掛か, 罠に掛か):
// after a verb かかる begins, approaches or assaults (取り掛かる, 差し掛かる, 襲い掛かる) and after
// が it takes time or comes as a call (時間が掛かる), none of them a state; the compounds that
// are one (引っ掛かる, 寄り掛かる) are listed.
bool ResultVerbStem(std::string_view stem) {
    // 見惚れる is gazing, not having fallen for someone.
    static constexpr std::string_view kActions[] = {"見惚れ", "見とれ"};
    constexpr std::string_view kHang = "掛か";
    if (dict::InClass(HDTRANS_CLASS("verb.result-state"), stem)) return true;
    if (InSet(kActions, stem)) return false;
    for (const std::string_view v : dict::ClassMembers(HDTRANS_CLASS("verb.result-state"))) {
        if (stem.size() > v.size() && stem.ends_with(v) && text::RuneCount(v) >= 2 && text::IsKanji(text::DecodeRune(v).cp) &&
            (v != kHang || stem.substr(0, stem.size() - v.size()).ends_with("に"))) {
            return true;
        }
    }
    return false;
}

std::string ExistentialToVerb(Flusher& f, std::string_view rest);

// The past of a verb ending written for the existential 있 (A니까 → 5으니까, 0 → 8,
// 0 것 같다 → 8 것 같다, 0데 → 5는데); "" for an ending it does not cover.
std::string PastOfVerbEnding(std::string_view ending) {
    if (ending.empty()) return {};
    const std::string after(ending.substr(1));
    switch (ending[0]) {
        case '0': return after.empty() || after.starts_with(" ") ? "8" + after : "5는" + after;
        case '1':
        case '4':
        case '5': return "5" + after;
        case '2':
        case 'A': return "5으" + after;
        case '3': return "5을" + after;
        case '6': return "5어" + after;
        case '7': return "5습" + after;
        case '8': return "8" + after;
        default: return {};
    }
}

// Whether an adverb of gradual change stands before it in its sentence.
bool GradualBefore(const Item* it) {
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        if (dict::StartsWithMember(HDTRANS_CLASS("adverb.gradual"), p->group->surface) > 0) return true;
    }
    return false;
}

// Whether verb item v is an auxiliary after the て form of the verb before it (近づいて来る).
bool AfterTeForm(const Item* v) {
    const Item* p = v->prev;
    return p != nullptr && p->tokenIndex == v->tokenIndex && p->detail->Yougo() &&
           (p->DirectSurface().ends_with(kJaTe) || p->DirectSurface().ends_with(kJaDe));
}

// Whether the て form before the auxiliary 来る, item v, is a move that ends where the speaker
// is (帰ってきている 돌아와 있다, 転属してきている 전속해 와 있다): a verb of verb.arrival-te or a
// verbal noun of noun.arrival-suru with its する. An approach or a change (近づいてきている,
// 寒くなってきている) is none.
bool ArrivalBefore(const Item* v) {
    const Item* p = v->prev;
    if (p == nullptr || p->selected < 0) return false;
    if (dict::InClass(HDTRANS_CLASS("verb.arrival-te"), p->group->surface)) return true;
    const Item* noun = p->prev;
    return p->EntryText(p->selected) == "&AV@" && noun != nullptr && noun->tokenIndex == p->tokenIndex &&
           dict::InClass(HDTRANS_CLASS("noun.arrival-suru"), noun->group->surface);
}

// Whether a noun of noun.coming-flow is the subject of 来る, item v: right before it or before
// its が, は or も.
bool FlowBefore(const Item* v) {
    const Item* p = v->prev;
    if (p != nullptr && (p->group->surface == "が" || p->group->surface == "は" || p->group->surface == "も")) p = p->prev;
    return p != nullptr && dict::InClass(HDTRANS_CLASS("noun.coming-flow"), p->group->surface);
}

// The verb in its 連用形 (the bare stem or its one i-row kana) that an unlisted 込む, item n,
// follows in the same token (詰め込む, 惚れ込んでいる); nullptr when n is no such 込む.
Item* InwardVerb(Item* n) {
    Item* it = n->prev;
    if (it == nullptr || n->tokenIndex != it->tokenIndex || n->selected < 0 || n->DirectIndex() < 0 ||
        n->detail->cls != kDetailVerb || n->detail->record != 0) {
        return nullptr;
    }
    const std::string_view surface = n->group->surface;
    if (surface != "込" && !(surface == "こ" && At(n->Code(n->selected), 2) == '5')) return nullptr;
    if (it->selected < 0 || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb) return nullptr;
    const std::string_view stem = it->DirectSurface();
    constexpr std::string_view kIRow = "いきしちにひみりぎじび";
    if (!stem.empty() && (text::RuneCount(stem) != 1 || kIRow.find(stem) == std::string_view::npos)) return nullptr;
    return it;
}

// Whether verb item it is an honorific before the いる of its て form: its Korean is an honorific
// (돌아가&AE@), or the いる is 계시다, in the auxiliary after it or in its ending (来られてる 1고
// 계신다).
bool HonorificAspect(Flusher& f, const Item* it) {
    if (it->selected >= 0 && it->EntryText(it->selected).ends_with(kHonorificMarker)) return true;
    const std::string honorific = HonorificExistence(f);
    if (const Item* n = it->After(); n != nullptr && n->selected >= 0 && n->detail->Yougo() && n->EntryText(n->selected) == honorific) {
        return true;
    }
    if (honorific.size() <= kHonorificMarker.size() || it->DirectIndex() < 0) return false;
    const std::string ending = std::string(kProgressiveHead) + honorific.substr(0, honorific.size() - kHonorificMarker.size());
    return it->DirectText(it->DirectIndex()).starts_with(ending);
}

}  // namespace

void ResultState(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0) continue;
        // An unlisted 込む holds the ending of the verb before it, which decides (惚れ込んでいる
        // 반해 있다); InwardAuxiliary moves the ending onto that verb later.
        const Item* v = InwardVerb(it);
        if (v == nullptr) v = it;
        const std::string_view stem = v->group->surface;
        const char c1 = At(v->Code(v->selected), 1);
        const bool object = c1 >= '3' && c1 <= '6';
        if ((stem == "な" || stem == "にな") && GradualBefore(v)) continue;
        if (stem == "にな" && v->prev != nullptr && dict::InClass(HDTRANS_CLASS("noun.ongoing-ninaru"), v->prev->group->surface)) {
            continue;
        }
        const bool nounPassive =
            stem == "され" && v->prev != nullptr && dict::InClass(HDTRANS_CLASS("noun.result-sareru"), v->prev->group->surface);
        // A られ word of a kanji verb read as an honorific (亡くなられている, 倒れられている) has the
        // aspect of its verb; the なる of お慕いになられる is the respectful form.
        std::string_view verb = stem;
        if (v->detail->record == 0 && stem.size() > kRare.size() && stem.ends_with(kRare) &&
            text::IsKanji(text::DecodeRune(stem).cp) && HonorificAspect(f, it)) {
            verb.remove_suffix(kRare.size());
        }
        // 来る written in kana after a noun or a particle (手紙きてる 편지 와 있어); after a verb
        // or a て form it is the auxiliary (練習してきている 연습해 오고 있다, 違ってきている).
        if (verb == "き" && v->selected >= 0 && v->EntryText(v->selected) == "&AH@") {
            const Item* p = v->prev;
            const bool afterTe = p != nullptr && p->selected >= 0 &&
                                 (p->group->surface.ends_with("て") ||
                                  (p->group->surface.ends_with("で") && At(p->Code(p->selected), 0) != '2'));
            if (ArrivalBefore(v) || (!afterTe && (p == nullptr || p->selected < 0 || At(p->Code(p->selected), 0) != '4'))) {
                verb = "来";
            }
        }
        // What keeps coming, as wind or waves, comes on (いい風きてる 좋은 바람 오고 있어).
        if (verb == "来" && FlowBefore(v)) continue;
        const bool past = v->detail->record == 0 && !v->lexical && dict::InClass(HDTRANS_CLASS("verb.result-state-past"), verb);
        const bool state = past || (v->detail->record == 0 && !v->lexical && (ResultVerbStem(verb) || nounPassive) && !object) ||
                           ((v->detail->record == 1 || v->lexical) && dict::InClass(HDTRANS_CLASS("verb.result-passive"), stem)) ||
                           (v->detail->record <= 1 && GrammarCode(v->GAttr(v->selected), "Dx") &&
                            !v->EntryText(v->selected).ends_with("&HN@"));
        if (!state) continue;
        // A verb that is also a て auxiliary (来る) is a change or approach after a て form
        // (近づいて来られている 가까워져 오고 계신다), no state of its own, unless the move ends
        // here (ArrivalBefore).
        if (dict::InClass(HDTRANS_CLASS("aux.te-start"), verb) && AfterTeForm(v) && !ArrivalBefore(v)) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string lemma = output::Lemma(e->dst, f.families);
        if (!lemma.starts_with("1고")) continue;
        const std::string_view rest = std::string_view(lemma).substr(std::string_view("1고").size());
        if (past && rest.starts_with(" 있")) {
            if (const std::string ending = PastOfVerbEnding(ExistentialToVerb(f, rest.substr(1))); !ending.empty()) {
                it->AssignDirectSlot(it->DirectIndex(), ending);
                continue;
            }
        }
        if (Item* n = it->After(); past && rest.empty() && n != nullptr && n->group->surface == "い" && n->selected >= 0 &&
                                n->DirectIndex() >= 0 && ItemLemma(f, n, n->selected).starts_with("있")) {
            const Entry* d = n->DirectEntry(n->DirectIndex());
            if (const std::string ending = d != nullptr ? PastOfVerbEnding(d->dst) : std::string(); !ending.empty()) {
                it->AssignDirectSlot(it->DirectIndex(), ending);
                n->AssignEntrySurface(n->selected, "");
                n->AssignDirectSlot(n->DirectIndex(), "");
                continue;
            }
        }
        // The honorific 계시다 of the auxiliary is its 있다 (座られている 앉아 계신다).
        const std::string auxiliary = rest.empty() && it->After() != nullptr ? ItemLemma(f, it->After(), it->After()->selected) : "";
        const std::string honorific = HonorificExistence(f);
        const bool honorificRest = honorific.size() > 4 && rest.starts_with(" " + honorific.substr(0, honorific.size() - 4));
        if (rest.starts_with(" 있") || honorificRest || auxiliary.starts_with("있") || auxiliary == "계시다") {
            it->AssignDirectSlot(it->DirectIndex(), "6" + std::string(rest));
        }
    }
}

namespace {

// Ends of the stems of the verbs of saying (言っていた 말했다), which keep the progressive like
// verb.say-teiru.
constexpr std::string_view kSayEnds[] = {"言", "云"};

enum class Aspect { None, State, Say };

// Whether a verb's Korean ends in a marker family of an adjective (困って 곤란&Mf@), or in a
// family that verbs share with a stem that ends like an adjective of ko-adjective.stem-end
// (腹が減って 배가 고&Aa@ 고프다, そうになって 뻔&AV@ 뻔하다): an adjective takes neither 고 있다
// nor 어 있다.
// verbFamily tells that the family is a verb's and the stem made it an adjective.
bool AdjectiveTarget(std::string_view target, bool* verbFamily = nullptr) {
    if (verbFamily != nullptr) *verbFamily = false;
    if (target.size() < 4 || !target.ends_with("@") || target[target.size() - 4] != '&') return false;
    const output::internal::KoreanFamily* family = output::internal::FindKoreanFamily(target.substr(target.size() - 4));
    if (family == nullptr || family->inflectionClass == output::internal::InflectionClass::Copula) return false;
    if (family->partOfSpeech == output::internal::KoreanPartOfSpeech::Adjective) return true;
    const std::string stem = std::string(target.substr(0, target.size() - 4)) + std::string(family->stem);
    const bool adjective = dict::EndsWithMember(HDTRANS_CLASS("ko-adjective.stem-end"), stem) > 0;
    if (verbFamily != nullptr) *verbFamily = adjective;
    return adjective;
}

Aspect SimpleAspectOf(std::string_view stem, std::string_view target) {
    if (dict::InClass(HDTRANS_CLASS("verb.state-teiru"), stem) || stem.ends_with("思") || AdjectiveTarget(target)) return Aspect::State;
    if (dict::InClass(HDTRANS_CLASS("verb.say-teiru"), stem) ||
        std::any_of(std::begin(kSayEnds), std::end(kSayEnds), [&](std::string_view e) { return stem.ends_with(e); })) {
        return Aspect::Say;
    }
    return Aspect::None;
}

// Whether a verb of saying keeps its progressive before ending, the simple form's ending.
bool KeepsProgressive(Aspect aspect, std::string_view ending) {
    return aspect == Aspect::Say && (ending.starts_with("4") || ending.starts_with("1지"));
}

// The ending of a verb that takes the place of 있 and what follows it in an ending of the
// existential family (있었다 → 5다, 있는데 → 0데, 있으면 → 2면); "" when rest starts otherwise.
std::string ExistentialToVerb(Flusher& f, std::string_view rest) {
    char code = 0;
    std::string_view row;
    for (const char k : std::string_view("0123456789AB")) {
        const std::string_view r = LookupSource("&HN@" + std::string(1, k), f.families);
        if (!r.empty() && rest.starts_with(r) && r.size() > row.size()) {
            code = k;
            row = r;
        }
    }
    if (code == 0) return {};
    const std::string_view after = rest.substr(row.size());
    if ((code == '1' || code == '4') && after.starts_with(kKoDa) && !after.starts_with("다가")) code = '4';
    return std::string(1, code) + std::string(after);
}

}  // namespace

void SimpleAspect(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb || it->detail->record != 0) {
            continue;
        }
        const Aspect aspect = SimpleAspectOf(it->group->surface, it->EntryText(it->selected));
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (aspect == Aspect::None || e == nullptr) continue;
        const std::string_view te = it->DirectSurface();
        const std::string progressive(kProgressiveHead);
        const std::string resultative(kResultativeHead);
        const bool contracted = std::string_view(e->dst).starts_with(progressive) ||
                                (std::string_view(e->dst).starts_with(resultative) && AdjectiveTarget(it->EntryText(it->selected)));
        if (contracted) {
            const std::size_t cut = e->dst.starts_with(progressive) ? progressive.size() : resultative.size();
            std::string simple = ExistentialToVerb(f, std::string_view(e->dst).substr(cut));
            bool verbFamily = false;
            const bool adjective = AdjectiveTarget(it->EntryText(it->selected), &verbFamily);
            // The form 8 of an adjective is no past (곤란한): 있던 there is its past adnominal (곤란했던).
            if (const std::span<const std::string_view> past = dict::ClassMembers(HDTRANS_CLASS("ko.adjective-past-adnominal"));
                simple.starts_with("8") && !past.empty() && adjective) {
                simple = std::string(past.front()) + simple.substr(1);
            }
            // A family of verbs read as an adjective takes the adjective forms 8 and 1 for the verb
            // forms 0 and 4 (腹が減っておる 배가 고프다).
            if (verbFamily && (simple.starts_with("0") || simple.starts_with("4"))) simple[0] = simple[0] == '0' ? '8' : '1';
            if (!simple.empty() && !KeepsProgressive(aspect, simple)) it->AssignDirectSlot(it->DirectIndex(), simple);
            continue;
        }
        Item* n = it->After();
        if ((e->dst != "1고" && e->dst != "6") || (!te.ends_with("て") && !te.ends_with("で")) || n == nullptr ||
            n->group->surface != "い" || n->selected < 0 || n->DirectIndex() < 0 ||
            !ItemLemma(f, n, n->selected).starts_with("있") || n->DirectSurface().ends_with("っけ")) {
            continue;
        }
        const Entry* d = n->DirectEntry(n->DirectIndex());
        if (d == nullptr || d->dst.empty() || KeepsProgressive(aspect, d->dst)) continue;
        // An adjective target has no verb form 0 or 4 (있는, 있다); the composer reads it with its
        // いる (苛々している 초조하다).
        bool verbFamily = false;
        const bool adjective = AdjectiveTarget(it->EntryText(it->selected), &verbFamily);
        std::string simple = d->dst;
        // A family of verbs has the verb forms 0 and 4 (고프는, 고픈다); an adjective takes 8 and 1
        // there (고픈, 고프다).
        if (verbFamily && (simple[0] == '0' || simple[0] == '4')) simple[0] = simple[0] == '0' ? '8' : '1';
        else if (adjective && (simple[0] == '0' || simple[0] == '4')) continue;
        if (const std::span<const std::string_view> past = dict::ClassMembers(HDTRANS_CLASS("ko.adjective-past-adnominal"));
            d->dst[0] == '8' && !past.empty() && verbFamily) {
            simple = std::string(past.front()) + simple.substr(1);
        }
        it->AssignDirectSlot(it->DirectIndex(), simple);
        n->AssignEntrySurface(n->selected, "");
        n->AssignDirectSlot(n->DirectIndex(), "");
    }
}

// A verb continuative before も and する is the verb itself, said with emphasis: V-지도 않다
// when the する is negative, V-기도 하다 otherwise (立ち上がりもせず 일어서지도 않고, 読みもしません
// 읽지도 않습니다, 読みもした 읽기도 했다). The negative of する moves onto the verb's ending
// and the する writes nothing.
void EmphaticMo(Flusher& f) {
    const std::string jido = "1" + std::string(kKoJi) + "도";
    const std::string ji = "1" + std::string(kKoJi) + " ";
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->selected < 0 || it->DirectIndex() < 0 ||
            it->DirectText(it->DirectIndex()) != jido || !it->DirectSurface().starts_with("も")) {
            continue;
        }
        Item* n = it->After();
        if (n == nullptr || !n->detail->Yougo() || n->group->surface != "する" || n->selected < 0 ||
            n->DirectIndex() < 0) {
            continue;
        }
        const std::string_view d = n->DirectText(n->DirectIndex());
        if (d.starts_with(ji)) {
            it->AssignDirectSlot(it->DirectIndex(), jido + " " + std::string(d.substr(ji.size())));
            n->AssignEntrySurface(n->selected, "");
            n->AssignDirectSlot(n->DirectIndex(), "");
        } else if (!d.starts_with("1" + std::string(kKoJi))) {
            it->AssignDirectSlot(it->DirectIndex(), "1기도");
        }
    }
}

void InwardAuxiliary(Flusher& f) {
    for (Item* n = f.w.chain.head; n != nullptr; n = n->next) {
        Item* it = InwardVerb(n);
        if (it == nullptr) continue;
        const Entry* d = n->DirectEntry(n->DirectIndex());
        if (d == nullptr || d->dst.empty()) continue;
        it->AssignDirectSlot(it->DirectIndex(), d->dst);
        n->AssignEntrySurface(n->selected, "");
        n->AssignDirectSlot(n->DirectIndex(), "");
    }
}

namespace {

// The perfect of an ending of the existential 있다 that follows a て form, for a verb whose
// state is the result of what it did: the present reads as done (4다 5다, 0 8, 7니다 5습니다,
// 2니까 5으니까); a condition (2면), a connective 6 that goes on and the past keep their form.
// "" for other endings.
std::string PerfectEnding(std::string_view ending, bool utteranceEnd) {
    if (ending.empty()) return {};
    const std::string_view rest = ending.substr(1);
    switch (ending[0]) {
        case '4': return "5" + std::string(rest);
        case '0': return "8" + std::string(rest);
        case '7': return "5습" + std::string(rest);
        case '3': return "5을" + std::string(rest);
        case '5': return std::string(ending);
        case '1':
            if (rest.starts_with("지") || rest.starts_with("고") || rest.starts_with("네") || rest.starts_with("나") ||
                rest.starts_with("니")) {
                return "5" + std::string(rest);
            }
            return {};
        case '2':
        case 'A':
            if (rest.starts_with("면")) return std::string(ending);
            return "5으" + std::string(rest);
        case '6':
            if (rest.empty() && utteranceEnd) return "5어";
            if (rest.starts_with("요")) return "5어" + std::string(rest);
            return std::string(ending);
    }
    return {};
}

// Whether a verb stem ends in the passive れ of a verb (書かれ, 包まれ, され), which takes no 두다.
bool PassiveStem(std::string_view stem) {
    if (!stem.ends_with("れ")) return false;
    stem.remove_suffix(std::string_view("れ").size());
    return !stem.empty() && RuneIn(DecodeLastRune(stem).cp, "かさたなはまやらわがざだばぱ");
}

// Whether a が subject stands before verb item it in its clause: then てある is the state the
// thing is in (窓が開けてある 창이 열려 있다), not an action done beforehand.
bool SubjectGaBefore(const Item* it) {
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s == "が") return p->prev != nullptr;
        if (p->detail->Yougo() || s.empty() || Has("、。,「」『』！？", s)) break;
    }
    return false;
}

// Whether a Korean verb lemma already reads as a passive or a change of state, which takes no
// 두다: 되다 or 당하다 (정리되다, 이용당하다), or 지다 after the 어 form of a verb, a syllable
// without a final consonant in ㅏ, ㅐ, ㅓ, ㅕ, ㅘ, ㅙ, ㅝ or ㅞ (닫혀지다, 열어지다; not 던지다,
// 만지다 or 가지다).
bool KoreanPassive(std::string_view lemma) {
    if (lemma.ends_with("되다") || lemma.ends_with("당하다")) return true;
    if (!lemma.ends_with("지다") || lemma.ends_with("가지다")) return false;
    const std::string_view stem = lemma.substr(0, lemma.size() - std::string_view("지다").size());
    if (stem.empty()) return false;
    const char32_t c = DecodeLastRune(stem).cp;
    if (c < 0xAC00 || c > 0xD7A3 || (c - 0xAC00) % 28 != 0) return false;
    const int medial = static_cast<int>((c - 0xAC00) / 28 % 21);
    return medial == 0 || medial == 1 || medial == 4 || medial == 6 || medial == 9 || medial == 10 || medial == 14 ||
           medial == 15;
}

}  // namespace

void PreparedState(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb || it->detail->record != 0 ||
            it->lexical || dict::InClass(HDTRANS_CLASS("verb.writing"), it->group->surface) ||
            GrammarCode(it->GAttr(it->selected), "Dx") ||
            PassiveStem(it->group->surface) || SubjectGaBefore(it) || KoreanPassive(ItemLemma(f, it, it->selected))) {
            continue;
        }
        const Entry* e = it->DirectEntry(it->DirectIndex());
        const std::string_view te = it->DirectSurface();
        Item* n = it->After();
        if (e == nullptr || e->dst != "6" || (!te.ends_with("て") && !te.ends_with("で")) || n == nullptr ||
            n->group->surface != "あ" || n->selected < 0 || n->DirectIndex() < 0 ||
            n->EntryText(n->selected) != "&HN@") {
            continue;
        }
        const Entry* d = n->DirectEntry(n->DirectIndex());
        if (d == nullptr) continue;
        const std::string prepared = PerfectEnding(d->dst, SentenceEnd(f.w, n));
        if (prepared.empty()) continue;
        n->AssignEntrySurface(n->selected, "&C7@");
        n->AssignDirectSlot(n->DirectIndex(), prepared);
    }
}

void NotYet(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->After();
        if (n == nullptr || !it->detail->Yougo() || it->DirectIndex() < 0) continue;
        const Entry* direct = it->DirectEntry(it->DirectIndex());
        if (direct == nullptr || direct->dst != std::string("1") + std::string(kKoJi) || (n->group->surface != "いな" && n->group->surface != "な") ||
            ItemLemma(f, n, n->selected) != "않다") {
            continue;
        }
        // The ていない of a potential verb says it has not managed (描けていない 그리지 못했다), where
        // 할 수 있지 않다 is no Korean.
        const std::string_view te = it->DirectSurface();
        const bool potential = (te.ends_with("て") || te.ends_with("で")) &&
                               (it->detail->record == 4 || it->EntryText(it->selected) == "할 수 &HN@");
        // A passive of something wished for (verb.unattained) that does not come is not attained, not
        // refused: 愛されなかった 사랑받지 못했다; not a concession or a condition (認められなくても).
        const std::string_view surface = it->group->surface;
        const Entry* after = n->DirectEntry(n->DirectIndex());
        const bool received = after != nullptr && !Has(after->dst, "도") && !Has(after->dst, "면") &&
                              dict::EndsWithMember(HDTRANS_CLASS("verb.unattained"), surface) > 0;
        const bool remember = surface.ends_with("覚え") || surface.ends_with("憶え");
        if (!potential && !received && dict::EndsWithMember(HDTRANS_CLASS("verb.notyet"), surface) == 0) {
            continue;
        }
        Entry* d = n->DirectEntry(n->DirectIndex());
        // A plain ない of 覚える (覚えない) is not learning, which keeps 않다 before an attributive.
        if (remember && n->group->surface == "な" && (d == nullptr || d->dst.starts_with("4"))) continue;
        if (potential) {
            if (it->detail->record == 4) {
                it->detail->record = 0;
            } else {
                it->AssignEntrySurface(it->selected, "&AV@");
            }
        }
        output::Conjugated target = output::Conjugating("못하다", '$', "", f.families);
        if (n->group->At(n->selected) != nullptr) n->AssignEntrySurface(n->selected, target.target);
        if (d == nullptr) continue;
        // 覚えていない says the memory is not there now (기억하지 못한다), but read as learning
        // or feeling it is an action not done, past like other verbs (単語をまだ覚えていない
        // 아직 배우지 못했다).
        if (remember && !ItemLemma(f, it, it->selected).starts_with("기억") && (d->dst == "1는다" || d->dst == "1다")) {
            n->AssignDirectSlot(n->DirectIndex(), "5다");
            continue;
        }
        for (const std::string_view form : {std::string_view("1는다"), std::string_view("1다")}) {
            if (d->dst.starts_with(form)) {
                n->AssignDirectSlot(n->DirectIndex(), "4다" + d->dst.substr(form.size()));
                break;
            }
        }
        // The attributive 4 of a plain ない (않는) is 하는 on 못하다 (気付かない隣 눈치채지 못하는);
        // after ている it stays 한, what has not happened (気付いてないようで 깨닫지 못한 것 같아서).
        const bool perfect = te.ends_with("て") || te.ends_with("で");
        // Pretending not to notice (気付かない振り 깨닫지 않는 척) is done on purpose.
        const Item* noun = n->After();
        const bool pretend = noun != nullptr && (noun->group->surface.starts_with("振り") ||
                                                 noun->group->surface.starts_with("ふり") ||
                                                 noun->group->surface.starts_with("フリ"));
        if (pretend && d->dst.starts_with("4")) {
            n->AssignEntrySurface(n->selected, "$DH@");
            continue;
        }
        if (d->dst.starts_with("4") && !d->dst.starts_with("4다") && !perfect) {
            n->AssignDirectSlot(n->DirectIndex(), "1는" + d->dst.substr(1));
        } else if (perfect && d->dst.starts_with("4데")) {
            // ていないけど is 못했는데, not 못한데.
            n->AssignDirectSlot(n->DirectIndex(), "5는" + d->dst.substr(1));
        }
    }
}

namespace {

// Whether the Korean lemma's stem ends in ㄹ (들다).
bool RieulStem(std::string_view lemma) {
    constexpr std::string_view kDa = "다";
    if (lemma.ends_with(kDa)) lemma.remove_suffix(kDa.size());
    return text::Final(DecodeLastRune(lemma).cp) == text::kFinalRieul;
}

// Whether the と of item it, with no pause after it, joins an event that followed it in the
// past sentence ending at last (振り返ると彼女がいた): the verb before it is no state of
// いる or ある, nor 言う after か or から or quoting (というと, から言うと), and no word of saying or
// thinking, no other と and no mark of supposition stands between it and the end
// (可能性があると深く理解していた, 離れすぎると信号が拾えないという欠点), and a past copula
// at the end follows only seeing or noticing.
bool FollowedEvent(Flusher& f, const Item* it, const Item* last) {
    const std::string full = output::Lemma(it->EntryText(it->selected), f.families);
    std::string_view lemma = full;
    if (const std::size_t space = lemma.rfind(' '); space != std::string_view::npos) lemma.remove_prefix(space + 1);
    if (lemma.starts_with("있") || lemma.starts_with("없")) return false;
    // A past copula ends a state the clause leads to (加えると三千の大所帯だった 더하면); only
    // seeing or noticing finds it (見ると十七時すぎだった 보니).
    std::string end(last->group->surface);
    if (last->DirectIndex() >= 0) end += std::string(last->DirectSurface());
    if ((end.ends_with("だった") || end.ends_with("であった") || end.ends_with("でした")) && !lemma.ends_with("보다") &&
        lemma != "눈치채다" && lemma != "깨닫다" && lemma != "알아차리다") {
        return false;
    }
    if (it->group->surface.starts_with("と") ||
        (it->prev != nullptr && (it->prev->group->surface == "から" || it->prev->group->surface == "か"))) {
        return false;
    }
    for (const Item* x = it->After(); x != nullptr; x = x->next) {
        if (x->selected < 0 || QuoteVerbFollows(x) || Has(x->Attr(x->selected), "UTh") || Has(x->Attr(x->selected), "UTs")) {
            return false;
        }
        if (x->DirectIndex() >= 0 && x->DirectSurface().ends_with("と")) return false;
        for (const std::string_view m : dict::ClassMembers(HDTRANS_CLASS("mark.supposed"))) {
            if (Has(x->group->surface, m) || (x->DirectIndex() >= 0 && Has(x->DirectSurface(), m))) return false;
        }
        if (x == last) break;
    }
    return true;
}

// Whether the sentence ending at last, in narration, ends in a plain present verb of an act
// (籠を見る, 歩く), not a state, a change or a potential (眠くなる, 花が咲いている, 書ける), and
// holds no adverb of habit (毎朝起きると顔を洗う).
bool NarratedPresentAct(Flusher& f, const Item* last, std::string_view end) {
    if (!f.w.settings.narrated || last->InQuote() || last->detail->cls != kDetailVerb || last->DirectIndex() < 0 ||
        last->selected < 0 || last->DetailRecord() == 4) {
        return false;
    }
    const Entry* e = last->DirectEntry(last->DirectIndex());
    if (e == nullptr || e->dst != "4다") return false;
    const char32_t c = DecodeLastRune(end).cp;
    if (std::u32string_view(U"うくぐすつぬぶむる").find(c) == std::u32string_view::npos) return false;
    const std::string lemma = output::Lemma(last->EntryText(last->selected), f.families);
    // A change (졸려지다, 되다), a state, a word with its own subject (花が咲く 꽃이 피다), or the bare
    // する of a habit (話をすると嬉しそうな顔をする).
    if (lemma.ends_with("되다") || lemma.ends_with("지다") || lemma.ends_with("있다") || lemma.ends_with("없다") ||
        Has(lemma, "이 ") || Has(lemma, "가 ") || last->group->surface.starts_with("な") || lemma == "하다") {
        return false;
    }
    for (const Item* x = f.w.chain.head; x != nullptr; x = x->next) {
        if (dict::InClass(HDTRANS_CLASS("adverb.habit"), x->group->surface) || dict::InClass(HDTRANS_CLASS("adverb.habitual"), x->group->surface) ||
            x->group->surface == "必ず") {
            return false;
        }
    }
    return true;
}

// Whether the clause of the と at item it acts on an object: a を before it, after the last comma
// or sentence mark (籠を置くと), not a happening (名前を入れると, 気に入られてしまうと) or a する.
bool ActsOnObject(Flusher& f, const Item* it) {
    if (it->group->surface.starts_with("しま") || it->group->surface.starts_with("ちゃ")) return false;
    // An attempt (選ぼうとすると) or the bare する of a noun (咳払いをすると) is no step of its own.
    if (output::Lemma(it->EntryText(it->selected), f.families) == "하다") return false;
    for (const Item* x = it->prev; x != nullptr; x = x->prev) {
        const std::string_view s = x->group->surface;
        if (s == "を") return true;
        if (s == "、" || s == "。" || s == "！" || s == "？" || s == "「" || s == "」" || s == "『" || s == "』") return false;
    }
    return false;
}

// Whether no subject or topic (が, は, も) stands between item it and last: the same subject
// goes on (籠を置くと、籠を見る), not a new one (春になると花が咲く).
bool SameSubjectAfter(const Item* it, const Item* last) {
    for (const Item* x = it->After(); x != nullptr; x = x->next) {
        const std::string_view s = x->group->surface;
        if (s == "が" || s == "は" || s == "も") return false;
        if (x == last) break;
    }
    return true;
}

}  // namespace

void PastSequence(Flusher& f) {
    const Item* last = nullptr;
    for (const Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const std::string_view s = it->group->surface;
        if (it->detail->Yougo() || !Has(" 、 。 ！ ？ ! ? … ‥ 」 』 ） よ ね な ぞ ぜ わ さ ", " " + std::string(s) + " ")) {
            last = it;
        }
    }
    if (last == nullptr) return;
    std::string end(last->group->surface);
    if (last->DirectIndex() >= 0) end += std::string(last->DirectSurface());
    // A verb whose past た is in its conjugation, not in its ending (噴火した: する + た).
    const bool pastForm = last->detail->cls == kDetailVerb && last->detail->kind == 6 && last->DirectSurface().empty();
    // Narration in the present tells its steps as the past does (籠を置くと、籠を見る 바구니를 두니, 바구니를
    // 본다), a と only, of an act on an object by the subject that goes on.
    const bool present = (!end.ends_with("た") && !pastForm) && NarratedPresentAct(f, last, end);
    if (!present && ((!end.ends_with("た") && !pastForm) || end.ends_with("よかった") || end.ends_with("良かった"))) return;
    for (Item* it = f.w.chain.head; it != nullptr && it != last; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        if (present && (it->DirectSurface() != "と" || e->dst != "2면(자)" || !SameSubjectAfter(it, last) ||
                        !ActsOnObject(f, it))) {
            continue;
        }
        const std::string_view d = it->DirectSurface();
        const bool seeing = e->dst == "2면" && d.ends_with("ば") &&
                            output::Lemma(it->EntryText(it->selected), f.families).ends_with("보다");
        // てみれば before a past is what one found on trying, also without a comma (出かけてみれば
        // 出会ったのは 나가 보니 만난 것은).
        const std::string_view stem = it->group->surface;
        const bool tried = seeing && (stem.ends_with("てみ") || stem.ends_with("でみ") ||
                                      (stem == "み" && AfterTeForm(it)));
        // A point of view (長期的に見れば, 目で見れば, 外から見れば) is no step of the story.
        const bool viewpoint = it->prev != nullptr && (it->prev->group->surface.ends_with("的に") ||
                                                      it->prev->group->surface.ends_with("目で") ||
                                                      it->prev->group->surface.ends_with("から"));
        if (tried) {
            it->AssignDirectSlot(it->DirectIndex(), "2니");
        } else if (it->After() == nullptr ||
                   (it->After()->group->surface != kJaComma && (d != "と" || e->dst != "2면(자)" || !FollowedEvent(f, it, last)))) {
            continue;
        } else if (seeing && !viewpoint) {
            it->AssignDirectSlot(it->DirectIndex(), "2니");
        } else if (e->dst != "2면(자)") {
            continue;
        } else if (d == "と" && RieulStem(output::Lemma(it->EntryText(it->selected), f.families))) {
            it->AssignDirectSlot(it->DirectIndex(), "1자");
        } else if (d == "と") {
            it->AssignDirectSlot(it->DirectIndex(), "2니");
        } else if (d.ends_with("ら")) {
            it->AssignDirectSlot(it->DirectIndex(), "5더니");
        }
    }
}


bool SentenceEnd(const Worker& w, const Item* it) {
    std::string_view after = From(it->text, it->start + it->length);
    if (after.empty() && it->tokenIndex + 1 < w.TokenCount()) after = w.Tok(it->tokenIndex + 1).text;
    return UtteranceEnd(w, it) || RuneIn(text::DecodeRune(after).cp, "！？!?");
}

bool QuoteEnd(const Worker& w, const Item* it) {
    std::string_view rest = From(it->text, it->start + it->length);
    if (rest.empty() && it->tokenIndex + 1 < w.TokenCount()) {
        if (w.Span(it->tokenIndex + 1) != 0) return false;
        rest = w.Tok(it->tokenIndex + 1).text;
    }
    return rest.empty() || RuneIn(text::DecodeRune(rest).cp, "！？!?。．.」』）)");
}

bool QuoteVerbFollows(const Item* it) {
    if (it == nullptr) return false;
    if (it->group->surface == "聞") return true;
    return dict::StartsWithMember(HDTRANS_CLASS("verb.quote-start"), it->group->surface) > 0;
}

namespace {

// Whether a verb of thinking stands within four items after it, with no predicate and no clause
// boundary in between.
bool ThinkingVerbAhead(const Item* it) {
    const Item* x = it->After();
    for (int n = 0; n < 5 && x != nullptr && x->selected >= 0; n++, x = x->next) {
        if (dict::StartsWithMember(HDTRANS_CLASS("verb.thinking-start"), x->group->surface) > 0) return n > 0;
        const char cls = At(x->Code(x->selected), 0);
        if (cls == '4' || cls == '6' || cls == '7') return false;
    }
    return false;
}

// Whether a verb of saying or thinking stands within six items from x on, with no
// predicate before it (いけないと、散々親父から言われていた).
bool QuoteVerbAhead(const Item* x) {
    for (int n = 0; n < 6 && x != nullptr && x->selected >= 0; n++, x = x->next) {
        if (QuoteVerbFollows(x)) return true;
        const char cls = At(x->Code(x->selected), 0);
        if (cls == '4' || cls == '6' || cls == '7') return false;
    }
    return false;
}

// Whether the next item is a verb of thinking or wishing (思う, 考える, 願う, 祈る, 望む).
bool WishVerbNext(const Item* x) {
    if (x == nullptr || x->selected < 0) return false;
    for (const std::string_view v : {"思", "おもう", "おもっ", "考え", "願", "祈", "望"}) {
        if (x->group->surface.starts_with(v)) return true;
    }
    return false;
}

// Whether the item is a verb of saying or thinking whose と ends the utterance.
bool SayingConditional(const Item* it) {
    const std::string_view s = it->group->surface;
    return s.ends_with("思") || s.ends_with("言") || (s == "い" && it->prev != nullptr && it->prev->group->surface.ends_with("と"));
}

// Reads 聞く after a quotation as 듣다.
void HearSense(Flusher& f, Item* it) {
    if (it != nullptr && it->group->surface.starts_with("聞")) SelectLemma(f, it, "듣다");
}

}  // namespace

void QuoteTo(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->DirectIndex() < 0) {
            if (it->group->surface == "だと" && it->prev != nullptr &&
                (!it->prev->detail->Yougo() || it->prev->DirectSurface().ends_with("の")) &&
                (QuoteEnd(f.w, it) || QuoteVerbFollows(it->After()) || ThinkingVerbAhead(it))) {
                SelectLemma(f, it, "(이)라고");
                HearSense(f, it->After());
            }
            continue;
        }
        std::string_view d = it->DirectSurface();
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        // An ending that takes the comma after と (ないと、) quotes when a verb of saying or
        // thinking follows in the clause (時間がないと、彼は言った 시간이 없다고, 그는 말했다).
        const bool paused = d.ends_with(std::string("と") + std::string(kJaComma));
        if (paused) d.remove_suffix(kJaComma.size());
        if (d.ends_with("と")) HearSense(f, it->After());
        if (!d.ends_with("と") || (!e->dst.starts_with("2면") && !e->dst.starts_with("1다면"))) continue;
        const bool end = QuoteEnd(f.w, it);
        const std::string ja = it->group->surface + std::string(d);
        if (paused) {
            if (At(it->Code(it->selected), 0) == '7' && QuoteVerbAhead(it->After())) {
                it->AssignDirectSlot(it->DirectIndex(), "1다고,");
            }
            continue;
        }
        // たらと思う keeps the condition as an unfinished wish (行けたらと思う 갈 수 있으면 하고 생각한다);
        // before a verb of saying it is a suggestion (行ったらと言われた) and stays a quote.
        if ((d == "らと" || d.ends_with("たらと") || d.ends_with("だらと")) && WishVerbNext(it->After())) {
            it->AssignDirectSlot(it->DirectIndex(), std::string(e->dst.starts_with("1다면") ? "1다면" : "2면") + " 하고");
            continue;
        }
        const bool thought = (At(it->Code(it->selected), 0) == '7' || ja.ends_with("ないと") || d.starts_with("た")) &&
                             (ThinkingVerbAhead(it) ||
                              (it->After() != nullptr && it->After()->group->surface == kJaComma && QuoteVerbAhead(it->After()->next)));
        // ないと that ends the utterance is a duty or a need (行かないと 가지 않으면, 時間がないと
        // 시간이 없으면), but the adjective ない after the topic は or も there closes a quotation
        // (毒はないと 독은 없다고, 何もないと 아무것도 없다고).
        const bool absent = (it->group->surface == "な" || it->group->surface == "無") && it->prev != nullptr &&
                            it->prev->selected >= 0 &&
                            (it->prev->group->surface.ends_with("は") || it->prev->group->surface.ends_with("も")) &&
                            ItemLemma(f, it, it->selected).ends_with("없다");
        const bool duty = ja.ends_with("ないと") && !absent;
        if ((!end && !QuoteVerbFollows(it->After()) && !thought) || (end && (duty || SayingConditional(it))) ||
            Has(d, "ます")) {
            continue;
        }
        if (e->dst.starts_with("1다면") || At(it->Code(it->selected), 0) == '7') {
            it->AssignDirectSlot(it->DirectIndex(), "1다고");
        } else if (d.starts_with("た") || d.starts_with("だ")) {
            it->AssignDirectSlot(it->DirectIndex(), "5다고");
        } else {
            it->AssignDirectSlot(it->DirectIndex(), "4다고");
        }
    }
}

namespace {

// Whether an attributive (綺麗な, 赤い, 走る, 走った) and a noun follow it, past punctuation (とても、キレイな裸).
bool ModifierFollows(const Item* it) {
    const Item* m = it->After();
    while (m != nullptr && !m->detail->Yougo() && m->selected >= 0 && At(m->Code(m->selected), 0) == '6') m = m->next;
    if (m == nullptr || m->DirectIndex() < 0) return false;
    const Entry* e = m->DirectEntry(m->DirectIndex());
    if (e == nullptr || (e->dst != "0" && e->dst != "4" && e->dst != "8")) return false;
    const Item* n = m->next;
    return n != nullptr && !n->detail->Yougo() && n->selected >= 0 && At(n->Code(n->selected), 0) == '1';
}

}  // namespace

void TerminalBeforeQuote(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* next = it->After();
        bool pause = false;
        while (next != nullptr && InSet(kCommaItems, next->group->surface)) {
            next = next->next;
            pause = true;
        }
        if (next != nullptr && next->tokenIndex > it->tokenIndex + 1 && !pause) {
            pause = true;
            for (int t = it->tokenIndex + 1; t < next->tokenIndex; t++) {
                const std::string_view left = f.w.Tok(t).text;
                pause = pause && f.w.Span(t) == 0 && !left.empty() && TrimRunes(left, "…‥、").empty();
            }
        }
        // A noun or adverb that starts with と (とても, とうとう) quotes nothing, but the clause
        // before the pause stays a sentence unless the word leads to a modifier and its noun, which
        // the clause modifies too (できあがった、とても綺麗な花 완성된, 무척 예쁜 꽃).
        if (!pause || next == nullptr || (!next->group->surface.starts_with("と") && !next->group->surface.starts_with("なんて")) ||
            next->selected < 0 || next->group->At(next->selected) == nullptr ||
            (std::string_view("PSM").find(next->group->At(next->selected)->cls) == std::string_view::npos &&
             ModifierFollows(next)) ||
            it->DirectIndex() < 0 || OwnedGroup(it->DirectGroup()) || !Has(it->EntryText(it->selected), "@")) {
            continue;
        }
        if (next->group->surface == "と" && next->selected >= 0 && ItemLemma(f, next, next->selected) == "(와)과") {
            TakeTarget(next, At(next->Code(next->selected), 0), "(이)라고");
        }
        const char cls = At(it->Code(it->selected), 0);
        const std::string_view text = it->DirectText(it->DirectIndex());
        const std::string ja = it->group->surface + std::string(it->DirectSurface());
        for (const TerminalForm& form : kTerminalForms) {
            if (form.cls != cls || form.attributive != text) continue;
            bool ends = false;
            for (int i = 0; i < form.lastCount; i++) ends = ends || LastChar(ja) == form.last[i];
            if (!ends) continue;
            for (int slot = 0; slot < it->DirectCount(); slot++) {
                if (it->detail->enabled[slot] && it->DirectText(slot) == form.terminal) {
                    it->SetDirectIndex(slot);
                    break;
                }
            }
        }
    }
}

namespace {

// Whether s ends in a concessive volitional: -(よ)うと or -(よ)うが (言われようと, 何をしようが,
// 誰だろうと). No verb has a volitional in ほう, which is the noun (どっちのほうが).
bool ConcessiveVolitional(std::string_view s) {
    for (const std::string_view p : {std::string_view("と"), std::string_view("が")}) {
        if (!s.ends_with(p)) continue;
        std::string_view v = s.substr(0, s.size() - p.size());
        if (!v.ends_with("う")) continue;
        v.remove_suffix(std::string_view("う").size());
        if (!v.empty() && RuneIn(DecodeLastRune(v).cp, "おこごそぞとどのぼもよろ")) return true;
    }
    return false;
}

// The text of item it.
std::string_view ItemText(const Item* it) {
    return std::string_view(it->text).substr(static_cast<std::size_t>(it->start), static_cast<std::size_t>(it->length));
}

// Whether a question word stands before it in its clause, with no concessive も, ても, でも,
// (よ)うと or (よ)うが and no embedded か question between.
bool WonderBefore(const Item* it) {
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        const std::string_view d = p->DirectSurface();
        if (s.empty() || AnyRuneIn(s, "。！？!?「」『』") || s.ends_with("も") || s == "か" || d.ends_with("も") ||
            p->EndingAsks() || ConcessiveVolitional(ItemText(p)) ||
            ((s == "と" || s == "が") && p->prev != nullptr &&
             ConcessiveVolitional(std::string(ItemText(p->prev)) + std::string(s)))) {
            return false;
        }
        if (p->IsQuestionWord()) return true;
    }
    return false;
}

// Whether only closing marks, an ellipsis, a comma or a question mark follow the item, or a
// final な left without a translation before them (どこに行ったんだろうな).
bool WonderEnd(const Worker& w, const Item* it) {
    const Item* n = it->After();
    if (n != nullptr && n->tokenIndex == it->tokenIndex && n->start == it->start + it->length &&
        (n->group->surface == "な" || n->group->surface == "なぁ" || n->group->surface == "なあ") && n->selected >= 0 &&
        n->group->At(n->selected) != nullptr && n->EntryText(n->selected).empty()) {
        it = n;
    }
    std::string_view rest = From(it->text, it->start + it->length);
    if (rest.empty() && it->tokenIndex + 1 < w.TokenCount()) {
        if (w.Span(it->tokenIndex + 1) != 0) return false;
        rest = w.Tok(it->tokenIndex + 1).text;
    }
    return rest.empty() || RuneIn(text::DecodeRune(rest).cp, "？?。．.」』）)…‥、～ー");
}

// The question form of a conjecture (3 것이다 -> 3까, 일 것이다 -> 일까), or "" for other targets.
std::string_view WonderForm(std::string_view dst) {
    constexpr std::pair<std::string_view, std::string_view> kForms[] = {
        {"3 것이다", "3까"},       {"1겠지", "3까"},     {"5을 것이다", "5을까"}, {"1겠지요", "3까요"},
        {"5겠지요", "5을까요"},   {"일 것이다", "일까"}, {"이겠지", "일까"},     {"(이)겠지", "일까"},
        {"(이)겠지요", "일까요"}, {"3 거예요", "3까요"},
    };
    for (const auto& [from, to] : kForms) {
        if (dst == from) return to;
    }
    return {};
}

bool WonderSurface(std::string_view s) { return s.ends_with("だろう") || s.ends_with("だろ") || s.ends_with("でしょう"); }

}  // namespace

void WonderingDarou(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const bool ending = it->DirectIndex() >= 0 && WonderSurface(it->DirectSurface());
        if ((!ending && !WonderSurface(it->group->surface)) || !WonderEnd(f.w, it) || !WonderBefore(it)) continue;
        if (ending) {
            const Entry* e = it->DirectEntry(it->DirectIndex());
            if (e == nullptr) continue;
            const std::string_view to = WonderForm(e->dst);
            if (!to.empty()) it->AssignDirectSlot(it->DirectIndex(), to);
            continue;
        }
        if (it->group->At(it->selected) == nullptr) continue;
        const std::string_view to = WonderForm(it->EntryText(it->selected));
        if (!to.empty()) it->AssignEntrySurface(it->selected, to);
    }
}

void LongVowelAfterKatakana(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->group->surface != "ー" || it->selected < 0 || ItemLemma(f, it, it->selected) != "-") continue;
        const std::string_view before = std::string_view(it->text).substr(0, static_cast<std::size_t>(it->start));
        if (before.empty()) continue;
        const char32_t r = DecodeLastRune(before).cp;
        const std::string_view after = From(it->text, it->start + static_cast<long>(std::string_view("ー").size()));
        const char32_t next = after.empty() ? U'\0' : text::DecodeRune(after).cp;
        if (r >= U'ァ' && r <= U'ヺ' &&
            ((next >= U'ぁ' && next <= U'ヺ') || text::IsKanji(next) || RuneIn(next, "、。！？」』）…"))) {
            if (it->group->At(it->selected) != nullptr) it->AssignEntrySurface(it->selected, "");
        }
    }
}

namespace {

// Whether s speaks politely; a polite word reported with って (一番ですって) does not.
bool PoliteSurface(std::string_view s) {
    if (Has(s, "ですって") || Has(s, "ますって") || Has(s, "でしたって") || Has(s, "でしょうかって")) return false;
    if (s.ends_with("いませ")) return true;
    for (const std::string_view m : dict::ClassMembers(HDTRANS_CLASS("speech.polite-grammar"))) {
        if (Has(s, m)) return true;
    }
    return false;
}

// Whether the last predicate after it in its sentence is polite, looking past the quotes that
// open after it and stopping at the end of the quote it stands in (私が許してくださいと頼む
// ことはない is not).
bool PoliteAfter(const Item* it) {
    int depth = 0;
    const Item* last = nullptr;
    for (const Item* q = it->After(); q != nullptr; q = q->next) {
        const std::string_view s = q->group->surface;
        // The text of q up to the next word, which holds the brackets no word took.
        const size_t from = std::min(static_cast<size_t>(q->start), q->text.size());
        const size_t to = q->next != nullptr && q->next->tokenIndex == q->tokenIndex
                              ? std::max(from, std::min(static_cast<size_t>(q->next->start), q->text.size()))
                              : q->text.size();
        const std::string_view span = q->text.substr(from, to - from);
        for (size_t i = 0; i < span.size();) {
            const text::Rune r = text::DecodeRune(span.substr(i));
            if (r.cp == U'「' || r.cp == U'『') depth++;
            if (r.cp == U'」' || r.cp == U'』') depth--;
            i += r.size == 0 ? 1 : r.size;
        }
        if (depth < 0) break;
        if (depth == 0 && (q->detail->Yougo() || PoliteSurface(s) || !q->DirectSurface().empty())) last = q;
    }
    return last != nullptr && (PoliteSurface(last->group->surface) || PoliteSurface(last->DirectSurface()));
}

}  // namespace

namespace {

bool OnReading(const Item* it) {
    return it != nullptr && it->selected >= 0 && Has(it->GAttr(it->selected), "On");
}

// A counter that follows a number across spaces only (2026 年 2026 년, ３　歳): the number
// it counts stands before it, so it does not start a word.
bool CounterAfterSpacedNumber(const Flusher& f, const Item* it) {
    const Item* p = it->prev;
    if (p == nullptr || it->start != 0 || !Has(it->GAttr(it->selected), "Ct") || p->tokenIndex >= it->tokenIndex) {
        return false;
    }
    const std::string_view ps = p->group->surface;
    if (ps.empty() || !text::IsDigit(DecodeLastRune(ps).cp)) return false;
    if (p->start + p->length != static_cast<int>(p->text.size())) return false;
    for (int t = p->tokenIndex + 1; t < it->tokenIndex; t++) {
        const int type = f.w.Tok(t).type;
        if (type != tokenizer::kTypeAsciiSpace && type != tokenizer::kTypeWideSpace) return false;
    }
    return p->tokenIndex + 1 < it->tokenIndex;
}

// s with the initial sound rule on its first syllable: ㄹ before ㅑ ㅕ ㅖ ㅛ ㅠ ㅣ and ㄴ before
// those vowels become ㅇ, another ㄹ becomes ㄴ (륭 융, 녀 여, 로 노).
std::string InitialSound(std::string_view s) {
    const text::Rune r = text::DecodeRune(s);
    if (r.cp < 0xAC00 || r.cp > 0xD7A3) return std::string(s);
    const char32_t index = r.cp - 0xAC00;
    char32_t lead = index / (21 * 28);
    const char32_t vowel = index / 28 % 21, tail = index % 28;
    const bool palatal = vowel == 2 || vowel == 6 || vowel == 7 || vowel == 12 || vowel == 17 || vowel == 20;
    if (lead == 5) lead = palatal ? 11 : 2;
    else if (lead == 2 && palatal) lead = 11;
    else return std::string(s);
    std::string out;
    text::AppendRune(out, 0xAC00 + (lead * 21 + vowel) * 28 + tail);
    out.append(s.substr(r.size));
    return out;
}

}  // namespace

// The Sino-Korean reading that starts a word follows the initial sound rule (隆書 륭서 →
// 융서, 林道 림도 → 임도); one that follows another Sino-Korean reading stays (興隆 흥륭).
void InitialSoundRule(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!OnReading(it)) continue;
        // A word starts after punctuation, a particle or the start of its token, but not
        // after the ・ of a word spelled out (謹・賀・新・年).
        if (it->start > 0 && std::string_view(it->text).substr(0, static_cast<size_t>(it->start)).ends_with("・")) continue;
        const Item* p = it->prev;
        if (p != nullptr && p->tokenIndex == it->tokenIndex && p->start + p->length == it->start) {
            const std::string_view ps = p->group->surface;
            const char32_t last = ps.empty() ? 0 : DecodeLastRune(ps).cp;
            const bool particle = p->selected >= 0 && At(p->Code(p->selected), 0) == '2';
            if (!particle && (text::IsLetter(last) || text::IsDigit(last))) continue;
        }
        if (CounterAfterSpacedNumber(f, it)) continue;
        const std::string target(it->EntryText(it->selected));
        if (const std::string ruled = InitialSound(target); ruled != target) {
            TakeTarget(it, At(it->Code(it->selected), 0), ruled);
        }
    }
}

namespace {

// Whether the sentence part before the number names an angle or a temperature.
bool AngleBefore(std::string_view before) {
    for (const std::string_view end : {"。", "！", "？", "!", "?", "\n"}) {
        if (const std::size_t at = before.rfind(end); at != std::string_view::npos) before.remove_prefix(at + end.size());
    }
    return std::ranges::any_of(dict::ClassMembers(HDTRANS_CLASS("noun.do-angle")),
                               [&](std::string_view w) { return before.find(w) != std::string_view::npos; });
}

bool AngleAfter(std::string_view after) {
    after.remove_prefix(dict::StartsWithMember(HDTRANS_CLASS("particle.do-angle"), after));
    return dict::StartsWithMember(HDTRANS_CLASS("word.do-angle"), after) > 0;
}

}  // namespace

void DegreeAfterNumber(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (it->group->surface != "度" || it->selected < 0 || p == nullptr || p->selected < 0 ||
            ItemLemma(f, it, it->selected) != "번" || p->tokenIndex != it->tokenIndex) {
            continue;
        }
        const std::string_view number = p->EntryText(p->selected);
        const bool digits = !number.empty() && std::all_of(number.begin(), number.end(), [](char c) {
            return (c >= '0' && c <= '9') || c == '.' || c == ',';
        });
        if (!digits || !IsDigit(number[0])) continue;
        if (number.find('.') == std::string_view::npos && (number.size() < 2 || number[0] == '0')) continue;
        const std::string_view after = From(it->text, it->start + it->length);
        if (dict::StartsWithMember(HDTRANS_CLASS("word.do-times"), after) > 0) {
            continue;
        }
        // Arabic digits (180度, ４５度) read as the degree; a kanji numeral only before a word
        // of angle or temperature or after a noun of one in the sentence, and the bare 十 never
        // (十度 열 번).
        const std::string_view source = p->group->surface;
        if (!text::IsDigit(text::DecodeRune(source).cp) &&
            (source == "十" || (!AngleAfter(after) && !AngleBefore(p->text.substr(0, static_cast<std::size_t>(std::max(p->start, 0))))))) {
            continue;
        }
        SelectLemma(f, it, "도");
    }
}

void PluralPronoun(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->After();
        if (n == nullptr || it->selected < 0 || n->selected < 0 || it->detail->Yougo() ||
            !dict::InClass(HDTRANS_CLASS("suffix.plural-pronoun"), n->group->surface) || n->tokenIndex != it->tokenIndex ||
            it->start + it->length != n->start) {
            continue;
        }
        const char cls = At(it->Code(it->selected), 0);
        const std::string suffix = ItemLemma(f, n, n->selected);
        if (cls != '1' || (suffix != "들" && suffix != "등")) continue;
        const std::string lemma = ItemLemma(f, it, it->selected);
        std::string_view target;
        if (lemma == "나") target = "우리";
        else if (lemma == "저") target = "저희";
        else if (lemma == "너") target = "너희";
        else if (lemma == "우리" || lemma == "저희" || lemma == "너희") target = lemma;
        else continue;
        // The pronoun carries the space the suffix had before a word (우리 전원); the
        // empty suffix adds none.
        // Items written as nothing between them (ボクたちの仲間 우리 동료) are passed over.
        std::string written(target);
        const Item* last = n;
        const Item* m = n->next;
        bool adjacent = false;
        while (m != nullptr && m->selected >= 0) {
            const int end = last->start + last->length;
            adjacent = (m->tokenIndex == last->tokenIndex && m->start == end) ||
                       (m->tokenIndex == last->tokenIndex + 1 && end == static_cast<int>(last->text.size()) && m->start == 0);
            if (!adjacent || !TrimSpace(m->EntryText(m->selected)).empty()) break;
            last = m;
            m = m->next;
        }
        // Not before a word that starts with a particle (じゃない (이)가 아니다: 너희가).
        if (adjacent && m != nullptr && m->selected >= 0 && At(m->Code(m->selected), 0) != '2' &&
            At(m->Code(m->selected), 0) != '6' && !TrimSpace(m->EntryText(m->selected)).starts_with("(")) {
            const char32_t r = text::DecodeRune(m->group->surface).cp;
            if (text::IsLetter(r) || text::IsDigit(r)) written += ' ';
        }
        TakeTarget(it, cls, written);
        TakeTarget(n, At(n->Code(n->selected), 0), "");
    }
}

// The first person of a polite sentence in its humble form: 나 저, 내 제, 우리 저희
// (私はそう思います 저는 그렇게 생각합니다).
void HumbleFirstPerson(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->selected < 0 || it->detail->Yougo() ||
            !dict::InClass(HDTRANS_CLASS("pronoun.humble-first-person"), it->group->surface)) {
            continue;
        }
        const std::string lemma = ItemLemma(f, it, it->selected);
        std::string_view target;
        if (lemma == "나") target = "저";
        else if (lemma == "내") target = "제";
        else if (lemma == "내 ") target = "제 ";
        else if (lemma == "우리") target = "저희";
        else if (lemma == "우리들") target = "저희들";
        else continue;
        if (!PoliteAfter(it)) continue;
        if (target == "저" && it->After() != nullptr && it->After()->selected >= 0 &&
            ItemLemma(f, it->After(), it->After()->selected).starts_with("(이)가")) {
            target = "제";
        }
        TakeTarget(it, At(it->Code(it->selected), 0), target);
    }
}

namespace {

// The Korean of a のか word: its ending, or the word itself when it has none (なのか).
std::string_view NokaText(const Item* it) {
    if (const Entry* d = it->DirectEntry(it->DirectIndex())) return d->dst;
    if (const Entry* e = it->group->At(it->selected); e != nullptr && !it->detail->Yougo()) return e->dst;
    return {};
}

bool Noka(const Item* it) {
    const std::string_view s = it->DirectGroup() != nullptr ? it->DirectSurface() : std::string_view(it->group->surface);
    return s.ends_with("のか") && !NokaText(it).empty();
}

bool PunctuationItem(const Item* it) {
    const std::string_view s = it->group->surface;
    return s == "、" || s == "," || s == "。" || s == "." || s == "…" || s == "‥" || s == "？" || s == "?" ||
           s == "！" || s == "!" || s == "」" || s == "』";
}

// The question in 지 of t (것일까 것인지, 건가 건지, 인가 인지, 는가 는지); "" when t has
// none of them at its end. t in 지 is itself.
std::string NokaInJi(std::string_view t) {
    if (t.ends_with("지")) return std::string(t);
    constexpr std::pair<std::string_view, std::string_view> kForms[] = {
        {"것일까", "것인지"}, {"건가", "건지"}, {"인가", "인지"}, {"는가", "는지"}};
    for (const auto& [from, to] : kForms) {
        if (t.ends_with(from)) return std::string(t.substr(0, t.size() - from.size())) + std::string(to);
    }
    return {};
}

void WriteNoka(Item* it, std::string_view s) {
    if (it->DirectEntry(it->DirectIndex()) != nullptr) {
        it->AssignDirectSlot(it->DirectIndex(), s);
    } else {
        it->AssignEntrySurface(it->selected, s);
    }
}

}  // namespace

void ParallelNoka(Flusher& f) {
    std::vector<Item*> run;
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!Noka(it) || (it->After() != nullptr && !PunctuationItem(it->After()))) continue;
        run.push_back(it);
    }
    if (run.size() < 2) return;
    // Every clause but the last is followed by a comma.
    for (std::size_t k = 0; k + 1 < run.size(); k++) {
        const Item* n = run[k]->After();
        if (n == nullptr || (n->group->surface != "、" && n->group->surface != ",")) return;
    }
    std::vector<std::string> forms;
    for (const Item* it : run) {
        forms.push_back(NokaInJi(NokaText(it)));
        if (forms.back().empty()) return;
    }
    for (std::size_t k = 0; k < run.size(); k++) {
        if (forms[k] != NokaText(run[k])) WriteNoka(run[k], forms[k]);
    }
}

namespace {

// The token range [open, close] of the quote around token j: open is the token with 「 or 『
// before it and close the token with 」 or 』 after it; -1 for a side the chunk lacks.
// False when j is not inside a quote.
bool QuoteAround(const Worker& w, int j, int& open, int& close) {
    open = close = -1;
    for (int k = j; k >= 0; k--) {
        const std::string_view t = w.Tok(k).text;
        if (k != j && (Has(t, "」") || Has(t, "』"))) break;
        if (Has(t, "「") || Has(t, "『")) {
            open = k;
            break;
        }
    }
    for (int k = j; k < w.TokenCount(); k++) {
        const std::string_view t = w.Tok(k).text;
        if (k != j && (Has(t, "「") || Has(t, "『"))) break;
        if (Has(t, "」") || Has(t, "』")) {
            close = k;
            break;
        }
    }
    return open >= 0 || close >= 0;
}

// Whether the quote text has a mark of old or haughty speech (ておる, のじゃ, せよ, であろう,
// 余が, 儂, わしが, くれてやる, a final じゃ or ぬ).
bool OldSpeech(std::string_view q) {
    for (const std::string_view m : {"ておる", "ており", "ておっ", "でおる", "のじゃ", "であろう", "である", "儂",
                                     "わしが", "わしは", "わしの", "わしに", "わしを", "わしも", "くれてやる",
                                     "くれてやろう", "余が", "余は", "余の", "余に", "余を", "余も"}) {
        if (Has(q, m)) return true;
    }
    // 突撃せよ, not 出せよ.
    for (std::size_t at = q.find("せよ"); at != std::string_view::npos; at = q.find("せよ", at + 1)) {
        const std::string_view head = q.substr(0, at);
        const text::Rune last = DecodeLastRune(head);
        if (last.size > 0 && text::IsKanji(last.cp) && text::IsKanji(DecodeLastRune(head.substr(0, head.size() - last.size)).cp)) {
            return true;
        }
    }
    for (const std::string_view m : {"じゃ", "ぬ"}) {
        for (std::size_t at = q.find(m); at != std::string_view::npos; at = q.find(m, at + m.size())) {
            const std::string_view rest = q.substr(at + m.size());
            const std::string_view head = q.substr(0, at);
            if (m == "じゃ" && (head.ends_with("それ") || head.ends_with("これ") || rest.starts_with("あ") ||
                                rest.starts_with("な") || rest.starts_with("ん") || rest.starts_with("ね"))) {
                continue;
            }
            if (rest.empty() || RuneIn(text::DecodeRune(rest).cp, "。！？!?、…‥」』ぞよ")) return true;
        }
    }
    return false;
}

// Whether the sentence before it names a doer other than the speaker, a kin term or a person noun
// (noun.role or noun.other-giver, not pronoun.speaker) in が, は or も (彼が, 母は), or a habit
// (adverb.habit: 毎日): its giving then tells what someone does, no promise. A time word or a name
// before the particle does not (今日も, 長鯨が: a speaker often names herself).
bool OtherGiver(const Item* it) {
    for (const Item* x = it->prev; x != nullptr; x = x->prev) {
        const std::string_view s = x->group->surface;
        if (s == "。" || s == "！" || s == "？" || s == "!" || s == "?" || s == "「" || s == "『") break;
        if (dict::InClass(HDTRANS_CLASS("adverb.habit"), s)) return true;
        if ((s == "が" || s == "は" || s == "も") && x->prev != nullptr) {
            const std::string_view doer = x->prev->group->surface;
            if (!dict::InClass(HDTRANS_CLASS("pronoun.speaker"), doer) &&
                (dict::InClass(HDTRANS_CLASS("noun.role"), doer) || dict::InClass(HDTRANS_CLASS("noun.other-giver"), doer))) {
                return true;
            }
        }
    }
    return false;
}

// Whether the sentence before it names a doer other than the speaker in が, は or も (彼が, 田中は):
// the plain verb あげる then tells what that doer gives.
bool NamedDoer(const Item* it) {
    for (const Item* x = it->prev; x != nullptr; x = x->prev) {
        const std::string_view s = x->group->surface;
        if (s == "。" || s == "！" || s == "？" || s == "!" || s == "?" || s == "「" || s == "『") break;
        if ((s == "が" || s == "は" || s == "も") && x->prev != nullptr && !dict::InClass(HDTRANS_CLASS("pronoun.speaker"), x->prev->group->surface)) {
            return true;
        }
    }
    return false;
}

}  // namespace

void OfferPromise(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Entry* e = it->group->At(it->selected);
        const Entry* d = it->DirectEntry(it->DirectIndex());
        const std::string_view surface = it->group->surface;
        const bool humble = surface.ends_with("さしあげ") || surface.ends_with("差し上げ");
        std::string_view after = From(it->text, it->start + it->length);
        if (after.empty() && it->tokenIndex + 1 < f.w.TokenCount()) after = f.w.Tok(it->tokenIndex + 1).text;
        // The sentence ends after the word. After あげる it may also end in an ellipsis or a clipping
        // っ (見せてあげる……, 見せてあげるっ！), and so may やろう; a shouted やる there stays a
        // declaration.
        const bool gives = surface.ends_with("あげ") || surface.ends_with("上げ");
        std::string_view rest = after;
        for (int next = it->tokenIndex + 1; gives;) {
            while (!rest.empty() && RuneIn(text::DecodeRune(rest).cp, "っッ♪")) rest.remove_prefix(text::DecodeRune(rest).size);
            if (!rest.empty() || next >= f.w.TokenCount()) break;
            rest = f.w.Tok(next++).text;
        }
        const bool ends = gives || it->DirectSurface() == "ろう"
                              ? rest.empty() || RuneIn(text::DecodeRune(rest).cp, "！？!?。．.」』）)…‥")
                              : QuoteEnd(f.w, it);
        const bool asks = after.starts_with("？") || after.starts_with("?") || rest.starts_with("？") || rest.starts_with("?");
        // やる said with its や dropped after a て form before a soft particle (やったるわ, 殴ったるさ) puts
        // 주다 into the ending: speech, so the speaker's promise.
        const std::string_view own = it->DirectSurface();
        if (d != nullptr && ends && !asks && own.size() == 3 && Has("わさよね", own) && it->detail != nullptr &&
            it->detail->patternKind == 8 && it->detail->kind == 0 && std::string_view(d->dst).starts_with("6 ")) {
            it->AssignDirectSlot(it->DirectIndex(), "6 줄게");
            continue;
        }
        if (e == nullptr || d == nullptr || !(e->dst.ends_with("&CJ@") || (humble && e->dst.ends_with("드&AA@"))) ||
            (!surface.ends_with("や") && !gives) || !ends) {
            continue;
        }
        const std::string_view ending = it->DirectSurface();
        // Only a present or volitional ending turns: not 教えてやったよ, 教えてさしあげられます.
        if (it->DetailRecord() != 0 || d->dst.empty() || !AnyOf("46017", d->dst[0])) continue;
        if (asks) continue;
        // The volitional of やる after a て form (褒めてやろう) is the speaker's haughty grant: 주마,
        // in narration as in speech.
        if (surface.ends_with("や") && ending == "ろう") {
            it->AssignDirectSlot(it->DirectIndex(), "1마");
            continue;
        }
        const bool polite = ending == "ます" || ending == "ますよ" || ending == "ますね" || ending == "ますわ" ||
                            (humble && ending == "ましょう");
        const bool plain = ending == "る" || ending == "よ" || ending == "ね" || ending == "わ" || ending == "さ" ||
                           ending == "ぜ" || ending == "ぞ" || ending == "ろう" || ending == "よう" || ending == "のじゃ";
        // The verb itself ends in る, ろう, よう or ます: not the request あげてね, やってよ.
        const std::string_view tail = it->text.substr(
            std::min(it->text.size(), static_cast<std::size_t>(it->start) + it->group->surface.size()));
        if (!tail.starts_with("る") && !tail.starts_with("ろう") && !tail.starts_with("よう") && !tail.starts_with("ま") &&
            !tail.starts_with("のじゃ")) {
            continue;
        }
        // A final particle marks speech outside brackets too (a line of dialogue), and so does an
        // exclamation mark after the あげる of a て form with no other doer or habit before it
        // (見せてあげる！; not 母が作ってあげる！ nor the shouted 見せてやる！), or after the plain
        // verb あげる in る or ます with no doer named at all (チョコ、あげます！; not 彼が花をあげます！; a
        // volitional is cx:grant-will's).
        // The て form may be the item before or the start of the item itself (してあげ).
        const std::string_view lead = gives ? surface.substr(0, surface.size() - std::string_view("あげ").size()) : "";
        const bool afterTe = lead.ends_with("て") || lead.ends_with("で") ||
                             (it->prev != nullptr && it->prev->selected >= 0 &&
                              (it->prev->DirectSurface().ends_with("て") || it->prev->DirectSurface().ends_with("で")));
        const bool spoken = ending == "よ" || ending == "ね" || ending == "わ" || ending == "さ" || ending == "ぜ" ||
                            ending == "ぞ" ||
                            (gives && (afterTe || ((ending == "る" || ending == "ます") && !NamedDoer(it))) && !OtherGiver(it) &&
                             dict::StartsWithMember(HDTRANS_CLASS("mark.spoken-exclamation"), rest) > 0);
        int open = 0, close = 0;
        if (!polite && !plain) continue;
        if (!QuoteAround(f.w, it->tokenIndex, open, close) && !spoken) continue;
        if (polite && humble) {
            it->AssignDirectSlot(it->DirectIndex(), "1겠습니다");
            continue;
        }
        if (humble) continue;
        if (polite) {
            if (surface.ends_with("あげ") || surface.ends_with("上げ")) {
                std::string dst = e->dst;
                dst.replace(dst.size() - std::string_view("&CJ@").size(), std::string_view::npos, "드&AA@");
                it->AssignEntrySurface(it->selected, dst);
            }
            it->AssignDirectSlot(it->DirectIndex(), "3게요");
            continue;
        }
        std::string quote;
        const int from = open >= 0 ? open : 0;
        const int to = close >= 0 ? close : f.w.TokenCount() - 1;
        for (int k = from; k <= to; k++) quote += f.w.Tok(k).text;
        it->AssignDirectSlot(it->DirectIndex(), OldSpeech(quote) || surface.ends_with("くれてや") ? "1마" : "3게");
    }
}

namespace {

// Punctuation before which an exclaimed adjective may open the utterance.
constexpr std::string_view kExclaimOpeners[] = {"「", "『", "（", "(", "…", "……", "、"};

// Whether the stem's negative lemma is a state (verbNegative).
bool StativeNegative(std::string_view lemma) {
    const std::string_view stem = lemma.substr(0, lemma.size() - std::string_view("지 않다").size());
    return stem.ends_with("있") || stem.ends_with("그렇") || stem.ends_with("얼마 되");
}

bool AdjectiveNegative(std::string_view lemma) {
    const std::string_view stem = lemma.substr(0, lemma.size() - std::string_view("지 않다").size());
    return dict::EndsWithMember(HDTRANS_CLASS("ko-adjective.stem-end"), stem) > 0;
}

}  // namespace

void ExclaimedAdjective(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->DirectIndex() < 0 || OwnedGroup(it->DirectGroup()) || At(it->Code(it->selected), 0) != '7' ||
            it->DirectText(it->DirectIndex()) != "4" || !IsOneOf(it->After(), kCommaItems)) {
            continue;
        }
        if (const Item* p = it->prev; p != nullptr && !IsOneOf(p, kExclaimOpeners)) continue;
        if (const Item* n = it->After()->next; n != nullptr && n->selected >= 0 && At(n->Code(n->selected), 0) == '7' &&
                                            !IsWord(n, it->group->surface)) {
            continue;
        }
        if (const Item* n = it->After()->next; n != nullptr && n->selected >= 0 &&
                                            (At(n->Code(n->selected), 0) == '1' || n->group->surface.starts_with("で") ||
                                             n->group->surface.starts_with("だ"))) {
            continue;
        }
        for (int slot = 0; slot < it->DirectCount(); slot++) {
            if (it->detail->enabled[slot] && it->DirectText(slot) == "1" + std::string(kKoDa)) {
                it->SetDirectIndex(slot);
                break;
            }
        }
    }
}

void VerbNegative(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->DirectIndex() < 0 || At(it->Code(it->selected), 0) != '7') continue;
        const std::string lemma = ItemLemma(f, it, it->selected);
        if (!lemma.ends_with("지 않다") || StativeNegative(lemma)) continue;
        const std::string_view surface = it->group->surface;
        if (!surface.ends_with("な")) continue;
        const std::string_view stem = surface.substr(0, surface.size() - std::string_view("な").size());
        if (stem.empty() || stem.ends_with("じゃ") || stem.ends_with("では")) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        if (stem.ends_with("く") || AdjectiveNegative(lemma)) {
            if (e->dst == "4") it->AssignDirectSlot(it->DirectIndex(), "1은");
        } else if (e->dst == "1다") {
            it->AssignDirectSlot(it->DirectIndex(), "1는다");
        } else if (e->dst == "4") {
            it->AssignDirectSlot(it->DirectIndex(), "1는");
        }
    }
}

}  // namespace hdTrans::word
