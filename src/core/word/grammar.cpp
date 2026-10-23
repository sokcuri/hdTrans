// Grammar passes of the text path: the readings of a verb's られ, and the conjugating
// targets of passive verbs that have no static family.
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "core/output/korean.h"
#include "core/output/families.h"
#include "core/output/inflect.h"
#include "core/output/particle.h"
#include "core/text/hangul.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
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

// Verbs whose られ is read as the passive whatever their objects (perception and attitude).
constexpr std::string_view kPassiveVerbs[] = {
    "恐れ", "求め", "向け", "認め", "褒め", "責め", "見つめ", "称え", "讃え", "崇め", "見捨て", "名付け", "閉じ込め",
    "捕らえ", "授け", "咎め", "苛め", "虐げ", "嫌がらせ", "撫で", "睨め", "抱え", "付け", "抱き上げ",
};

// Ichidan stems whose affirmative られた is mostly the potential.
constexpr std::string_view kAbilityVerbs[] = {
    "見つけ", "答え", "覚え", "信じ", "食べ", "耐え", "考え", "止め", "抑え", "堪え", "決め", "変え", "見分け", "避け",
    "続け", "受け止め", "乗り越え", "生き延び", "逃げ", "着",
};

// Ichidan stems of acts done to someone, whose narrative られた after an object is the passive
// (決断を命じられた, 罵声を浴びせられた, 集合をかけられた, 場所を入れ替えられて). After an object
// other verbs are the potential (書き上げられた, 新年を迎えられたこと) unless the object is the
// body or what one wears (腕を絡められ, 顔を近づけられ) or the verb describes the noun after it
// (板を敷き詰められた床).
constexpr std::string_view kObjectPassiveVerbs[] = {
    "命じ", "禁じ", "告げ", "浴びせ", "付け加え", "投げ", "割り当て", "課せ", "与え", "教え", "見せ", "封じ", "尋ね",
    "舐め", "押し付け", "押しつけ", "突きつけ", "突き付け", "かけ", "掛け", "込め", "閉め", "開け", "入れ替え", "取り替え",
    "括り付け", "縛り付け",
};

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
    constexpr std::string_view suffixes[] = {"付け", "つけ", "かけ", "掛け", "立て", "かかえ", "抱え"};
    for (const std::string_view s : suffixes) {
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

// Nouns after a verb that name the clause rather than a thing in it (していたの, していたこと,
// していたようだ, 迎えられたこと).
constexpr std::string_view kClauseNouns[] = {"の", "ん", "こと", "事", "もの", "もん", "物", "よう", "みたい", "はず", "わけ", "訳"};

// Whether item it is followed by a noun that the verb describes, not at the end of the
// utterance (楽しみにしてたさ); before selection the first candidate of the next item counts.
bool DescribedNoun(const Worker& w, const Item* it) {
    const Item* n = it->next;
    return n != nullptr && n->tokenIndex == it->tokenIndex && At(n->Code(std::max(n->selected, 0)), 0) == '1' &&
           !UtteranceEnd(w, n) && !RuneIn(text::DecodeRune(From(n->text, n->start + n->length)).cp, "、。！？!?…‥」』）)") &&
           !std::any_of(std::begin(kClauseNouns), std::end(kClauseNouns),
                        [&](std::string_view c) { return n->group->surface.starts_with(c); });
}

// The passive reading of a られた, られて or られ、 with no other cue.
bool NarrativePassive(const Item* it, std::string_view rest) {
    if (!rest.starts_with("た") && !rest.starts_with("て") && !rest.starts_with(kJaComma)) return false;
    return !InSet(kAbilityVerbs, it->group->surface);
}

// Whether the clause before item it has an object を and no に (夜伽を命じられれば, not
// 証拠を上に見せられれば).
bool ObjectWithoutRecipient(const Item* it) {
    bool object = false;
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s == "に" || s == "には") return false;
        if (s == "を") object = true;
        if (p->detail->Yougo() || s.empty() || Has("、。,「」『』！？", s)) break;
    }
    return object;
}

// The passive reading of the られ of verb item it; rest is the text after られ:
//   - a conditional of a verb of kObjectPassiveVerbs is passive after its object without a
//     recipient に;
//   - a negative, conditional or volitional form and the polite non-past are potential;
//   - a verb that takes no object is potential, unless it is listed or a compound action;
//   - ている, ていた, てる, ており are passive;
//   - going back through the clause, the nearest を (potential unless listed, or a narrative
//     form of a verb of kObjectPassiveVerbs, after the body or before a noun it describes)
//     or an agent
//     に, から, によって, により after a thing-free word decides;
//   - listed verbs and narrative forms are passive;
//   - a subject は or が that is a thing makes the reading passive, unless the verb is the
//     plain non-past form;
//   - otherwise potential.
bool PassiveReading(const Worker& w, const Item* it, std::string_view rest) {
    if ((rest.starts_with("れば") || rest.starts_with("たら")) && InSet(kObjectPassiveVerbs, it->group->surface)) {
        return ObjectWithoutRecipient(it);
    }
    constexpr std::string_view kPotentialForms[] = {"な", "ず", "ぬ", "ん", "ませ", "ます", "れば", "よう", "たら", "たり"};
    for (const std::string_view p : kPotentialForms) {
        if (rest.starts_with(p)) return false;
    }
    const bool listed = InSet(kPassiveVerbs, it->group->surface) || CompoundAction(it->group->surface);
    if (!listed && !Transitive(it)) return false;
    constexpr std::string_view kPassiveForms[] = {"てい", "てる", "てお", "てた"};
    for (const std::string_view p : kPassiveForms) {
        if (rest.starts_with(p)) return true;
    }
    const Item* subject = nullptr;
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s == "を" && !listed &&
            (!NarrativePassive(it, rest) ||
             !(InSet(kObjectPassiveVerbs, it->group->surface) || BodyObject(p->prev) || DescribedNoun(w, it)))) {
            return false;
        } else if (s == "に" || s == "から" || s == "によって" || s == "により") {
            const Item* a = p->prev;
            if (a != nullptr && !Thing(a->Attr(std::max(a->selected, 0)))) return true;
        } else if ((s == "は" || s == "が") && p->prev != nullptr && subject == nullptr) {
            subject = p->prev;
        } else if (p->detail->Yougo() || s.empty() || Has("、。,「」『』！？", s)) {
            break;
        }
    }
    if (listed || NarrativePassive(it, rest)) return true;
    return subject != nullptr && Thing(subject->Attr(std::max(subject->selected, 0))) && !rest.starts_with("る");
}

// Nouns with 하다 whose passive is the noun with 받다 (사과받다) or 당하다 (무시당하다); the
// others take 되다 (고용되다).
constexpr std::string_view kReceivedNouns[] = {
    "사과", "축하", "비난", "칭찬", "위협", "공격", "의심", "존경", "초대", "부탁", "요청", "지시", "명령",
    "질문", "추천", "소개", "지원", "용서", "허락", "인정", "환영", "주목", "보호", "치료", "교육", "지적",
    "오해", "고백", "사랑", "감사", "대접", "위로", "격려", "축복", "저주", "제안", "신뢰", "주의", "충고", "재판",
};
constexpr std::string_view kSufferedNouns[] = {
    "무시", "거절", "배신", "이용", "습격", "강요", "협박", "돌진", "매도", "납치", "살해", "폭행", "감금",
    "추방", "방해", "모욕", "조롱", "추격", "포위", "기습", "학대", "구박", "고문", "희롱",
};
// Nouns with 하다 that keep 어지다 (말하다 말해지다, 의지하다).
constexpr std::string_view kKeptNouns[] = {"말", "의지", "생각", "일"};

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
    if (InSet(kKeptNouns, noun)) return {};
    const std::string_view by = InSet(kReceivedNouns, noun) ? "받다" : InSet(kSufferedNouns, noun) ? "당하다" : "되다";
    return std::string(stem) + std::string(by);
}

// Whether verb item it ends in its attributive form (る, た) before a noun in its token that
// names no clause (と言われた下女, not と言われるのが or と言われたこと); an auxiliary いる after
// its て form ends it instead (と言われている騎士団長).
bool ModifiesNoun(const Item* it) {
    if (const Item* aux = it->next; aux != nullptr && aux->tokenIndex == it->tokenIndex && aux->detail->Yougo() &&
                                    (aux->group->surface == "い" || aux->group->surface == "お")) {
        it = aux;
    }
    const Item* n = it->next;
    const std::string_view own = it->text.substr(static_cast<std::size_t>(it->start), static_cast<std::size_t>(it->length));
    return n != nullptr && n->tokenIndex == it->tokenIndex && (own.ends_with("る") || own.ends_with("た")) &&
           At(n->Code(std::max(n->selected, 0)), 0) == '1' &&
           !std::any_of(std::begin(kClauseNouns), std::end(kClauseNouns),
                        [&](std::string_view c) { return n->group->surface.starts_with(c); });
}

// Titles and suffixes of a person the speaker honours (陛下, 王子, 先生, シンシア様).
constexpr std::string_view kHonorificTitles[] = {
    "様", "さま", "殿", "殿下", "陛下", "閣下", "猊下", "先生", "王子", "王女", "王妃", "国王", "皇帝", "皇后",
    "皇太子", "女王", "母上", "父上", "兄上", "姉上",
};

// Whether noun item n names someone the speaker honours.
bool HonouredPerson(const Item* n) {
    if (n == nullptr || n->detail->Yougo() || n->selected < 0) return false;
    const std::string_view s = n->group->surface;
    for (const std::string_view t : kHonorificTitles) {
        if (s.ends_with(t)) return true;
    }
    return false;
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

// The passive of a word in 言 (言う with what is said before it) has no 지다 passive in
// Korean: after a quote it is 불리다 before a noun it describes (無口と言われた下女 과묵이라고
// 불린 하녀) and otherwise QuotePassive; after an object 하다 becomes 듣다 (嫌味を言われる
// 비꼬는 말을 듣다). Other words in 言 keep their passive.
void SayPassive(Item* it) {
    const std::string_view s = it->group->surface;
    if (!s.ends_with("言") || s == "言") return;
    const bool object = s.ends_with("を言");
    const std::string_view rest = From(it->text, it->start + static_cast<long>(s.size()));
    const int count = static_cast<int>(it->group->entries.size());
    std::vector<std::string> targets;
    for (int slot = 0; slot < count; slot++) {
        const std::string_view t = it->EntryText(slot);
        if (const std::size_t q = t.find("라고"); q != std::string_view::npos) {
            std::string target = ModifiesNoun(it) ? std::string(t.substr(0, q)) + "라고 불&AA@"
                                                  : QuotePassive(it, t.substr(0, q), rest);
            targets.push_back(target.empty() ? std::string(t) : std::move(target));
        } else if (object && t.ends_with("&AV@")) {
            targets.push_back(HeardObject(t.substr(0, t.size() - std::string_view("&AV@").size())) + "&J6@");
        } else {
            return;
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
        if (p->detail->Yougo() || s.empty() || Has("、。,「」『』！？", s)) return false;
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

// The subject form of 나, 저 and 너 (before 가), or "" for other lemmas.
std::string_view SubjectPronoun(std::string_view lemma) {
    if (lemma == "나") return "내";
    if (lemma == "저") return "제";
    if (lemma == "너") return "네";
    return {};
}

// Whether the text after ではな is a contrast that goes on (大した傷ではないが), which keeps the
// topic particle.
bool Contrast(std::string_view rest) {
    constexpr std::string_view kForms[] = {"い", "かった"};
    constexpr std::string_view kParticles[] = {"が", "けど", "けれど", "ですが", "ですけど", "のですが", "のだが",
                                               "んだが", "んですが", "んだけど", "でしょうが", "だろうが"};
    for (const std::string_view form : kForms) {
        if (!rest.starts_with(form)) continue;
        std::string_view after = rest.substr(form.size());
        if (after.starts_with(kJaComma)) after.remove_prefix(kJaComma.size());
        for (const std::string_view p : kParticles) {
            if (after.starts_with(p)) return true;
        }
    }
    return false;
}

// Nouns that name a quality: the の before an adjective after them is the subject.
constexpr std::string_view kAttributeNouns[] = {
    "香り", "色", "頭", "仲", "気持ち", "運", "口", "腕", "背", "人気", "面倒見", "品", "質", "勘", "性格", "器量",
    "愛想", "要領", "手際", "物覚え", "見晴らし", "日当たり", "風通し", "味", "気前", "気立て", "育ち", "顔色",
    "顔立ち", "目つき", "目付き", "人当たり", "評判", "趣味", "体格", "態度", "機嫌", "具合", "調子",
};

// Stems before い of the predicates of existence, quantity or knowing.
constexpr std::string_view kSubjectPredicates[] = {
    "居な", "いな", "少な", "無", "知らな", "分からな", "わからな", "見えな", "聞こえな", "多", "少", "絶えな",
};

// Whether text starts like a negative ending: な, ね (ねえ), ませ, ず, ぬ, ん.
bool NegativeStart(std::string_view text) {
    constexpr std::string_view kStarts[] = {kJaNa, kJaNe, kJaMase, kJaZu, kJaNu, kJaN};
    for (const std::string_view p : kStarts) {
        if (text.starts_with(p)) return true;
    }
    return false;
}

}  // namespace

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

bool SelectLemmaPrefix(Flusher& f, Item* it, std::string_view prefix) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        if (output::Lemma(it->EntryText(slot), f.families).starts_with(prefix)) {
            it->SelectCandidate(slot, {Provenance::Rewrite});
            return true;
        }
    }
    return false;
}

void PassiveRecords(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->group->entries.empty()) continue;
        if (it->detail->record == 1) {
            WriteLexicalPassive(f, it);
            continue;
        }
        if (it->detail->record != 4) continue;
        const char k = At(it->Code(0), 2);
        if (k != '7' && k != ';' && k != '=') continue;
        std::string_view rest = From(it->text, it->start + static_cast<long>(it->group->surface.size()));
        if (!rest.starts_with("られ")) continue;
        rest.remove_prefix(std::string_view("られ").size());
        if (!PassiveReading(f.w, it, rest)) continue;
        it->detail->record = 1;
        for (Entry* e : it->group->entries) {
            if (e->gattr.ends_with("Ka")) e->gattr = e->gattr.substr(0, e->gattr.size() - 2) + "Pi";
        }
        WriteLexicalPassive(f, it);
    }
}


void OwnPassives(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->detail->record != 1 || it->lexical || it->selected < 0) continue;
        const std::string passive = KoreanPassiveLemma(output::Lemma(it->EntryText(it->selected), f.families));
        if (passive.empty()) continue;
        // A passive in 이다 is a plain verb, not the copula.
        output::Conjugated c = output::Conjugating(passive, '&', passive.ends_with("이다") ? "규칙" : "", f.families);
        if (!c.error.empty()) continue;
        it->detail->record = 0;
        it->lexical = true;
        it->AssignEntrySurface(it->selected, c.target);
    }
}

namespace {

// Godan stems of permitting, entrusting and taking away, whose subject before the object and
// (ら)れ is the one acted on (参加を許される, 王位を奪われた).
constexpr std::string_view kReceiverVerbs[] = {"許", "任", "託", "奪", "盗", "取り上げ"};

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
    return n != nullptr && !n->detail->Yougo() && n->selected >= 0 && At(n->Code(n->selected), 0) == '1' &&
           !GrammarCode(n->GAttr(n->selected), "Hi");
}

// Whether a verb part code takes an object (class 3 to 6, M431 書く; not M418 帰る).
bool TransitiveCode(std::string_view code) { return At(code, 1) >= '3' && At(code, 1) <= '6'; }

// The honorific target of the conjugating verb target text (쓰&AF@ 쓰시다); "" when it has none.
std::string HonorificTarget(Flusher& f, std::string_view text) {
    // Verbs with their own honorific word.
    constexpr std::string_view kSuppletive[][2] = {
        {"말하다", "말씀하시다"}, {"먹다", "드시다"}, {"마시다", "드시다"}, {"자다", "주무시다"}, {"있다", "계시다"},
        {"죽다", "돌아가시다"},
    };
    if (text.size() < 4 || text.back() != '@' || text[text.size() - 4] != '&') return {};
    const std::string_view head = text.substr(0, text.size() - 4);
    std::string lemma;
    const std::string plain = output::Lemma(text, f.families);
    for (const auto& [verb, honorific] : kSuppletive) {
        if (plain == verb || plain.ends_with(" " + std::string(verb))) {
            lemma = plain.substr(0, plain.size() - verb.size()) + std::string(honorific);
        }
    }
    if (lemma.empty()) {
        const std::string_view stem = LookupSource(std::string(text.substr(text.size() - 4)) + "A", f.families);
        if (stem.empty()) return {};
        lemma = std::string(head) + std::string(stem) + "시다";
    }
    output::Conjugated c = output::Conjugating(lemma, '&', "", f.families);
    return c.error.empty() ? std::move(c.target) : std::string();
}

// The honorific target of the verb whose passive stem is surface (書かれ 書 쓰&AF@ 쓰시다), its
// default record first, and the verb's own target in plain; "" when the dictionary has no such
// verb.
std::string BaseVerbTarget(Flusher& f, std::string_view surface, bool& transitive, std::string& plain) {
    // The passive endings by the conjugation kinds (the last digit of the part code) of the verbs
    // they follow; '?' is 行く.
    constexpr std::string_view kEndings[][2] = {
        {"られ", "78"}, {"され", "3"}, {"かれ", "1?"}, {"がれ", "2"}, {"たれ", "4"}, {"なれ", "<"},
        {"ばれ", "6"},  {"まれ", "5"}, {"われ", "0"},
    };
    for (const auto& [ending, kinds] : kEndings) {
        if (!surface.ends_with(ending) || surface.size() == ending.size()) continue;
        const std::string_view base = f.w.arena.Keep(std::string(surface.substr(0, surface.size() - ending.size())));
        if (InSet(kPassiveVerbs, base) || InSet(kObjectPassiveVerbs, base) || InSet(kReceiverVerbs, base)) return {};
        const Lookup found = f.w.DoLookup(base, kModeGeneral);
        const Record* verb = nullptr;
        for (const Record* r = found.records; r != nullptr; r = r->child) {
            if (r->source != base || At(r->partCode, 1) != '4' || !Has(kinds, std::string(1, At(r->partCode, 3))) ||
                !r->target.ends_with("@")) {
                continue;
            }
            if (verb == nullptr || Has(r->semantic, "XXX")) verb = r;
            if (Has(r->semantic, "XXX")) break;
        }
        if (verb == nullptr) return {};
        transitive = TransitiveCode(verb->partCode);
        plain = verb->target;
        return HonorificTarget(f, verb->target);
    }
    return {};
}

// The respectful verb item it before the auxiliary いる of its て form in its token is the plain
// verb with 고 and the honorific of いる (書かれている 쓰고 계신다, 置かれていた 두고 계셨다): the
// auxiliary takes the honorific; false, with nothing changed, when no such auxiliary follows.
bool HonorificProgressive(Flusher& f, Item* it) {
    Item* aux = it->next;
    if (aux == nullptr || aux->tokenIndex != it->tokenIndex || !aux->detail->Yougo() || aux->group->surface != "い" ||
        aux->selected < 0 || it->DirectIndex() < 0 || ItemLemma(f, aux, aux->selected) != "있다") {
        return false;
    }
    const std::string_view ending = it->DirectText(it->DirectIndex());
    if (ending != "6" && ending != "1고") return false;
    const std::string honorific = HonorificTarget(f, aux->EntryText(aux->selected));
    if (honorific.empty()) return false;
    aux->AssignEntrySurface(aux->selected, honorific);
    // 계시다 takes the present of a verb (계신다), not the 있다 of gram Va.
    if (Entry* e = aux->group->At(aux->selected); e != nullptr) DropGrammarCode(e->gattr, "Va");
    it->AssignDirectSlot(it->DirectIndex(), "1고");
    return true;
}

}  // namespace

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
        if (record == 1 || record == 4) {
            // A verb whose (ら)れ is the passive by itself or a potential before a negative or a
            // condition keeps its reading.
            if (InSet(kPassiveVerbs, surface) || InSet(kObjectPassiveVerbs, surface) || InSet(kReceiverVerbs, surface) ||
                CompoundAction(surface)) {
                continue;
            }
            std::string_view rest = From(it->text, it->start + static_cast<long>(surface.size()));
            if (record == 4) {
                if (!rest.starts_with("られ") || InSet(kAbilityVerbs, surface)) continue;
                rest.remove_prefix(std::string_view("られ").size());
                if (rest.starts_with("な") || rest.starts_with("ず") || rest.starts_with("ぬ") || rest.starts_with("ん") ||
                    rest.starts_with("れば") || rest.starts_with("たら")) {
                    continue;
                }
            }
            if (!RespectfulReading(it, TransitiveCode(it->Code(it->selected)))) continue;
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
        // A word written as a passive or potential stem (され, 来られ, 書かれ): its honorific
        // candidate, or the honorific of the verb it is made from.
        if (record != 0 || !surface.ends_with("れ") || it->EntryText(it->selected).ends_with("&AE@")) continue;
        // 言われ read as 듣다 (言われた通り 들은 대로) after an honoured subject is the respectful
        // 言う, 말씀하시다 (陛下が言われたこと 말씀하신 것); what is said is often left out, so it
        // needs no object.
        if (surface == "言われ" && ItemLemma(f, it, it->selected) == "듣다") {
            if (const std::string target = HonorificTarget(f, "말&AV@"); !target.empty() && RespectfulReading(it, false)) {
                it->AssignEntrySurface(it->selected, target);
            }
            continue;
        }
        // Only a passive or potential Korean (쓰여지다, 결정되다, 올 수 있다), not a verb of its own
        // (生まれ 태어나다).
        const std::string chosen = ItemLemma(f, it, it->selected);
        if (!chosen.ends_with("지다") && !chosen.ends_with("되다") && !Has(chosen, "수 있") && !Has(chosen, "수 없")) {
            continue;
        }
        const int count = static_cast<int>(it->group->entries.size());
        int honorific = -1;
        for (int slot = 0; slot < count && honorific < 0; slot++) {
            if (it->EntryText(slot).ends_with("&AE@")) honorific = slot;
        }
        if (honorific >= 0) {
            // The する of a Sino noun takes an object unless the noun's する is intransitive
            // (gram Hi): 陛下が幽閉された is the passive, 国王様が崩御された the respectful form.
            if (!RespectfulReading(it, surface == "され" && SinoNounTakesObject(it->prev))) continue;
            it->SelectCandidate(honorific, {Provenance::Rewrite});
            // The object the passive made a subject is the object again (本を説明された 책을).
            for (Item* p = it->prev; p != nullptr; p = p->prev) {
                if (p->group->surface != "を") continue;
                if (p->selected >= 0 && p->EntryText(p->selected) == "(이)가") p->AssignEntrySurface(p->selected, "(을)를");
                break;
            }
            continue;
        }
        bool transitive = false;
        std::string plain;
        const std::string target = BaseVerbTarget(f, surface, transitive, plain);
        if (target.empty() || !RespectfulReading(it, transitive)) continue;
        // The state of the passive (gram Dx, 書かれている 쓰여 있다) is no state of the active.
        if (Entry* e = it->group->At(it->selected); e != nullptr) DropGrammarCode(e->gattr, "Dx");
        it->AssignEntrySurface(it->selected, HonorificProgressive(f, it) ? plain : target);
    }
}

void BenefactiveProvider(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const std::string_view surface = it->group->surface;
        if (it->selected < 0 || !it->EntryText(it->selected).ends_with("&CJ@")) continue;
        // もら after its verb, or a word that holds both (見せてもら).
        Item* start = nullptr;
        if (surface == "もら" && it->prev != nullptr && it->prev->detail->Yougo()) {
            // A causative before もらう asks leave, and its に is no giver (相談をさせてもらう).
            if (it->prev->group->surface == "させ" || it->prev->detail->record == 2) continue;
            start = it->prev->prev;
        } else if (surface.ends_with("てもら") || surface.ends_with("でもら")) {
            start = it->prev;
        } else {
            continue;
        }
        for (Item* p = start; p != nullptr; p = p->prev) {
            const std::string_view s = p->group->surface;
            if (s == "に" || s == "には") {
                Item* a = p->prev;
                const std::string ko(p->selected < 0 ? std::string_view() : p->EntryText(p->selected));
                if (a == nullptr || a->selected < 0 || !Person(a->Attr(a->selected)) || (ko != "에게" && ko != "에게는")) break;
                p->AssignEntrySurface(p->selected, ko == "에게" ? "(이)가" : "(은)는");
                // 나, 저, 너 and 누구 before 가 are 내, 제, 네 and 누.
                const std::string lemma = ItemLemma(f, a, a->selected);
                const std::string_view subject = lemma == "누구" ? std::string_view("누") : SubjectPronoun(lemma);
                if (ko == "에게" && !subject.empty()) a->AssignEntrySurface(a->selected, std::string(subject));
                break;
            }
            if (s == "は" || s == "が" || s == "から" || s.empty() || p->detail->Yougo() ||
                Has("、。,「」『』！？!?", s)) {
                break;
            }
        }
    }
}

void GoNotSay(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (p == nullptr || p->prev == nullptr || it->group->surface != "い" || !it->detail->Yougo() ||
            !output::Lemma(it->EntryText(it->selected), f.families).starts_with("말하") ||
            !Has(" に へ まで から ", " " + p->group->surface + " ") || Person(p->prev->Attr(p->prev->selected))) {
            continue;
        }
        if (SelectLemma(f, it, "가다")) continue;
        output::Conjugated c = output::Conjugating("가다", '&', "", f.families);
        if (it->group->At(it->selected) != nullptr) it->AssignEntrySurface(it->selected, c.target);
    }
}

void PositiveIke(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->group->surface != kJaIke || !it->EntryText(it->selected).starts_with(kKoAn)) continue;
        const std::string_view direct = it->DirectSurface();
        if (NegativeStart(direct) || (direct.empty() && it->next != nullptr && NegativeStart(it->next->group->surface))) {
            continue;
        }
        const int count = static_cast<int>(it->group->entries.size());
        for (int slot = 0; slot < count; slot++) {
            if (it->EntryText(slot).starts_with(kKoGalSu)) {
                it->SelectCandidate(slot, {Provenance::Rewrite});
                break;
            }
        }
    }
}

void CopulaDeAru(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (p != nullptr && it->group->surface == "であ" && !Has("と ここ そこ あそこ どこ", p->group->surface) &&
            output::Lemma(it->EntryText(it->selected), f.families).starts_with("에서 만나")) {
            SelectLemma(f, it, "이다");
        }
    }
}

void NegativeCopula(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        if (p == nullptr) continue;
        const Entry* pe = p->group->At(p->selected);
        if (p->detail->cls != kDetailInfo || pe == nullptr || pe->cls != 'N') continue;
        const std::string plemma = output::Lemma(p->EntryText(p->selected), f.families);
        if (plemma.ends_with("히") || TrimSpace(plemma) == ",") continue;
        if (it->group->surface != "ではな" || Contrast(From(it->text, it->start + static_cast<long>(it->group->surface.size())))) {
            continue;
        }
        if (!output::Lemma(it->EntryText(it->selected), f.families).starts_with("(은)는 아니") ||
            !SelectLemmaPrefix(f, it, "(이)가 아니")) {
            continue;
        }
        if (const std::string_view subject = SubjectPronoun(plemma); !subject.empty()) SelectLemma(f, p, subject);
    }
}

void SubjectBeforeGa(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        if (p == nullptr || it->selected < 0 || p->selected < 0) continue;
        // The subject form 내, 제, 네 that a rule chose for が goes back to 나, 저, 너 where the
        // が reads as an object (私が好き 내를 → 나를 좋아해).
        if (it->group->surface == "が" && output::Lemma(it->EntryText(it->selected), f.families) == "(을)를" &&
            !p->detail->Yougo() && p->group->At(p->selected) != nullptr) {
            const std::string_view own = p->EntryText(p->selected);
            const std::string_view plain = own == "내" ? "나" : own == "제" ? "저" : own == "네" ? "너" : "";
            if (!plain.empty()) p->AssignEntrySurface(p->selected, std::string(plain));
            continue;
        }
        // 俺じゃない (이)가 아니다: 내가 아니다.
        if (it->EntryText(it->selected).starts_with("(이)가 ") && !p->detail->Yougo() && p->group->At(p->selected) != nullptr) {
            const std::string_view subject = SubjectPronoun(output::Lemma(p->EntryText(p->selected), f.families));
            if (!subject.empty()) p->AssignEntrySurface(p->selected, std::string(subject));
            continue;
        }
        if (output::Lemma(it->EntryText(it->selected), f.families) != "(이)가") continue;
        if (it->group->surface == "の") {
            const Item* n = it->next;
            if (n != nullptr && n->detail->Yougo() && Has(n->group->surface, "を")) {
                SelectLemma(f, it, "의");
                continue;
            }
        }
        // 俺が, and 俺の作った with の as the subject of a clause: 내가.
        if (it->group->surface == "が" || it->group->surface == "の") {
            const std::string_view subject = SubjectPronoun(output::Lemma(p->EntryText(p->selected), f.families));
            if (!subject.empty() && !p->detail->Yougo()) {
                if (p->group->At(p->selected) != nullptr) p->AssignEntrySurface(p->selected, std::string(subject));
            }
        }
    }
}

void SubjectNo(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        Item* n = it->next;
        if (p == nullptr || n == nullptr || n->next == nullptr || it->group->surface != "の" || it->selected < 0 ||
            output::Lemma(it->EntryText(it->selected), f.families) != "의" || p->selected < 0 ||
            At(p->Code(p->selected), 0) != '1' || n->selected < 0 ||
            (!InSet(kSubjectPredicates, n->group->surface) &&
             !(InSet(kAttributeNouns, p->group->surface) && At(n->Code(n->selected), 0) == '7')) ||
            n->next->selected < 0 || At(n->next->Code(n->next->selected), 0) != '1') {
            continue;
        }
        if (SelectLemma(f, it, "(이)가")) {
            const std::string_view subject = SubjectPronoun(output::Lemma(p->EntryText(p->selected), f.families));
            if (!subject.empty()) {
                if (p->group->At(p->selected) != nullptr) p->AssignEntrySurface(p->selected, std::string(subject));
            }
        }
    }
}

namespace {

// Stems of the verbs of resultState whose ている is a state, read without an object.
constexpr std::string_view kResultVerbs[] = {
    "濡れ", "座", "立", "残", "入", "生き", "落ち", "似", "疲れ", "並", "変わ", "繋が", "離れ", "揃", "空", "終わ", "整",
    "載", "止ま", "倒れ", "集ま", "壊れ", "閉ま", "浮か", "隠れ", "広が", "固ま", "戻", "浮", "減", "溜ま", "潜", "掛か",
    "混じ", "死", "太", "痩せ", "決ま", "開", "閉じ", "腫れ", "枯れ", "割れ", "汚れ", "曲が", "折れ", "破れ", "崩れ", "乾",
    "冷え", "腐", "詰ま", "埋ま", "刺さ", "慣れ", "晴れ", "曇", "凍", "外れ", "取れ", "引きこも", "引き籠も", "惚れ", "ハマ",
    "はま", "嵌ま", "突っ伏", "埋もれ", "腰を下ろ", "落ち着", "腰かけ", "腰掛け", "済", "な", "埋め尽くされ", "覚め",
    "にな", "歪", "建ち並", "立ち並", "耽", "叶", "染ま", "混ざ", "鍵がかか", "鍵が掛か", "佇", "嵌", "取り残",
    "座り込", "座りこ", "しゃがみ込", "しゃがみこ", "へたり込", "へたりこ", "屈み込", "かがみ込", "倒れ込", "倒れこ",
    "潜り込", "潜りこ", "入り込", "入りこ", "迷い込", "紛れ込", "溶け込", "溶けこ", "引っ掛か", "寄り掛か", "もたれ掛か", "老け",
};

// Whether stem is a result verb of kResultVerbs, or a compound or phrase that ends in one of
// two or more characters that starts with a kanji (根性が曲が 曲が, 折れ曲が 曲が); a single kanji
// matches only itself (目立 is no 立). 掛か hangs only after the place に (壁に掛か, 罠に掛か):
// after a verb かかる begins, approaches or assaults (取り掛かる, 差し掛かる, 襲い掛かる) and after
// が it takes time or comes as a call (時間が掛かる), none of them a state; the compounds that
// are one (引っ掛かる, 寄り掛かる) are listed.
bool ResultVerbStem(std::string_view stem) {
    // 見惚れる is gazing, not having fallen for someone.
    static constexpr std::string_view kActions[] = {"見惚れ", "見とれ"};
    constexpr std::string_view kHang = "掛か";
    if (InSet(kResultVerbs, stem)) return true;
    if (InSet(kActions, stem)) return false;
    for (const std::string_view v : kResultVerbs) {
        if (stem.size() > v.size() && stem.ends_with(v) && text::RuneCount(v) >= 2 && text::IsKanji(text::DecodeRune(v).cp) &&
            (v != kHang || stem.substr(0, stem.size() - v.size()).ends_with("に"))) {
            return true;
        }
    }
    return false;
}

// Stems of the verbs of resultState whose state reads as the Korean past (亡くなっているから
// 돌아가셨으니까, 古びている本 오래된 책).
constexpr std::string_view kPastResultVerbs[] = {"亡くな", "生き残", "古び"};

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

// Nouns before になっている that read as an ongoing state (話題になっている 화제가 되고 있다).
constexpr std::string_view kOngoingNouns[] = {"話題", "噂", "評判", "問題", "人気", "注目", "議論", "流行"};

// Verbal nouns whose されている is a state.
constexpr std::string_view kResultNouns[] = {
    "舗装", "設置", "固定", "封印", "整備", "保存", "記載", "記録", "配置", "装備", "収納", "展示", "包装", "印刷", "登録",
    "接続", "密閉", "施錠", "武装", "装飾", "彫刻", "刻印", "整頓", "固定化", "表示", "拘束", "掲載", "確保",
};

// Stems of the passive verbs whose ている is a state.
constexpr std::string_view kResultPassives[] = {
    "並べ", "置", "囲", "覆", "積", "飾", "掛け", "盛", "刻", "書", "貼", "吊", "敷", "張", "描", "記", "包", "詰め",
    "閉じ込め", "埋め", "刺", "縛", "隠", "残", "向け", "付け", "込め", "組", "建て", "植え", "収め", "閉ざ", "塗", "彫",
    "繋", "結", "束ね", "挟", "重ね", "据え", "閉め", "開け", "括り付け", "縛り付け", "取り囲", "取り巻",
};

// The adverbs of gradual change before a ている of なる (a progression).
constexpr std::string_view kGradualAdverbs[] = {
    "次第", "徐々", "どんどん", "だんだん", "段々", "ますます", "益々", "日に日に", "少しずつ", "みるみる",
};

// Stems of the verbs of noticing or remembering whose negative takes 못하다 (notYet).
constexpr std::string_view kNotYetVerbs[] = {"気づ", "気付", "気がつ", "気が付", "覚え", "憶え"};

// Whether an adverb of gradual change stands before it in its sentence.
bool GradualBefore(const Item* it) {
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        for (const std::string_view a : kGradualAdverbs) {
            if (p->group->surface.starts_with(a)) return true;
        }
    }
    return false;
}

// The particles and punctuation of seeAnd (before the 見る て they do not change).
constexpr std::string_view kSeeAndFollowers = " は も を が の に と で や か よ ね な ";

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
        if (stem == "にな" && v->prev != nullptr && InSet(kOngoingNouns, v->prev->group->surface)) continue;
        const bool nounPassive = stem == "され" && v->prev != nullptr && InSet(kResultNouns, v->prev->group->surface);
        const bool past = v->detail->record == 0 && !v->lexical && InSet(kPastResultVerbs, stem);
        const bool state = past || (v->detail->record == 0 && !v->lexical && (ResultVerbStem(stem) || nounPassive) && !object) ||
                           ((v->detail->record == 1 || v->lexical) && InSet(kResultPassives, stem)) ||
                           (v->detail->record <= 1 && GrammarCode(v->GAttr(v->selected), "Dx") &&
                            !v->EntryText(v->selected).ends_with("&HN@"));
        if (!state) continue;
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
        if (Item* n = it->next; past && rest.empty() && n != nullptr && n->group->surface == "い" && n->selected >= 0 &&
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
        const std::string auxiliary = rest.empty() && it->next != nullptr ? ItemLemma(f, it->next, it->next->selected) : "";
        if (rest.starts_with(" 있") || auxiliary.starts_with("있") || auxiliary == "계시다") {
            it->AssignDirectSlot(it->DirectIndex(), "6" + std::string(rest));
        }
    }
}

namespace {

// Stems of the verbs of thinking and perceiving whose ている is read as the simple form
// (思っている 생각한다, 見えている 보인다); a stem ending in 思 also counts (そう思). The verbs of
// saying and going out with someone (言っていた 말했다, 付き合っている人 사귀는 사람) keep the
// progressive in the plain present and in 지, where their simple form reads as narration.
constexpr std::string_view kStateVerbs[] = {"見え", "聞こえ", "想"};
constexpr std::string_view kSayVerbs[] = {"話", "お話", "付き合"};
constexpr std::string_view kSayEnds[] = {"言", "云"};

enum class Aspect { None, State, Say };

// Whether a verb's Korean ends in a marker family of an adjective (困って 곤란&Mf@): an adjective
// takes neither 고 있다 nor 어 있다.
bool AdjectiveTarget(std::string_view target) {
    if (target.size() < 4 || !target.ends_with("@") || target[target.size() - 4] != '&') return false;
    const output::internal::KoreanFamily* family = output::internal::FindKoreanFamily(target.substr(target.size() - 4));
    return family != nullptr && family->partOfSpeech == output::internal::KoreanPartOfSpeech::Adjective &&
           family->inflectionClass != output::internal::InflectionClass::Copula;
}

Aspect SimpleAspectOf(std::string_view stem, std::string_view target) {
    if (InSet(kStateVerbs, stem) || stem.ends_with("思") || AdjectiveTarget(target)) return Aspect::State;
    if (InSet(kSayVerbs, stem) ||
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
        const std::string progressive = "1고 ";
        const std::string resultative = "6 ";
        const bool contracted = std::string_view(e->dst).starts_with(progressive) ||
                                (std::string_view(e->dst).starts_with(resultative) && AdjectiveTarget(it->EntryText(it->selected)));
        if (contracted) {
            const std::size_t cut = e->dst.starts_with(progressive) ? progressive.size() : resultative.size();
            const std::string simple = ExistentialToVerb(f, std::string_view(e->dst).substr(cut));
            if (!simple.empty() && !KeepsProgressive(aspect, simple)) it->AssignDirectSlot(it->DirectIndex(), simple);
            continue;
        }
        Item* n = it->next;
        if ((e->dst != "1고" && e->dst != "6") || (!te.ends_with("て") && !te.ends_with("で")) || n == nullptr ||
            n->group->surface != "い" || n->selected < 0 || n->DirectIndex() < 0 ||
            !ItemLemma(f, n, n->selected).starts_with("있") || n->DirectSurface().ends_with("っけ")) {
            continue;
        }
        const Entry* d = n->DirectEntry(n->DirectIndex());
        if (d == nullptr || d->dst.empty() || KeepsProgressive(aspect, d->dst)) continue;
        // An adjective target has no verb form 0 or 4 (있는, 있다); the composer reads it with its
        // いる (苛々している 초조하다).
        if (AdjectiveTarget(it->EntryText(it->selected)) && (d->dst[0] == '0' || d->dst[0] == '4')) continue;
        it->AssignDirectSlot(it->DirectIndex(), d->dst);
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
        Item* n = it->next;
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

// Korean verbs of holding, wearing, touching, posture and knowing, whose 고 있던 is the state
// the action left (抱えていた子供 안고 있던 아이, 忘れていた 잊고 있던); 던 would read as a habit.
constexpr std::string_view kHoldingVerbs[] = {
    "잡다", "쥐다", "움켜쥐다", "움켜 쥐다", "붙잡다", "안다", "껴안다", "끌어안다", "들다", "물다", "악물다", "입다", "신다",
    "끼다", "차다", "메다", "업다", "품다", "대다", "가지다", "지니다", "숙이다", "잊다", "알다", "기억하다", "달라붙다", "붙다", "앉다", "눕다", "서다", "앞두다",
};

// Whether lemma is one of kHoldingVerbs or a phrase that ends in one (이를 악물다).
bool HoldingVerb(std::string_view lemma) {
    return std::any_of(std::begin(kHoldingVerbs), std::end(kHoldingVerbs), [&](std::string_view v) {
        return lemma == v || (lemma.ends_with(v) && lemma.substr(0, lemma.size() - v.size()).ends_with(" "));
    });
}

}  // namespace

namespace {

// Stems of the verbs of writing, whose てある is what stands written (書いてある 써 있다).
constexpr std::string_view kWritingVerbs[] = {"書", "か", "描", "記", "記し", "しるし"};

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

// Whether item it ends its sentence: the end of the utterance, or ！ or ？ after it.
bool SentenceEnd(const Worker& w, const Item* it) {
    std::string_view after = From(it->text, it->start + it->length);
    if (after.empty() && it->tokenIndex + 1 < w.TokenCount()) after = w.Tok(it->tokenIndex + 1).text;
    return UtteranceEnd(w, it) || RuneIn(text::DecodeRune(after).cp, "！？!?");
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
            it->lexical || InSet(kWritingVerbs, it->group->surface) || GrammarCode(it->GAttr(it->selected), "Dx") ||
            PassiveStem(it->group->surface) || SubjectGaBefore(it) || KoreanPassive(ItemLemma(f, it, it->selected))) {
            continue;
        }
        const Entry* e = it->DirectEntry(it->DirectIndex());
        const std::string_view te = it->DirectSurface();
        Item* n = it->next;
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

void PastProgressiveAdnominal(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb || it->detail->record != 0) {
            continue;
        }
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        if (HoldingVerb(ItemLemma(f, it, it->selected))) continue;
        if ((e->dst == "1고 있었던" || e->dst == "1고 있던") && DescribedNoun(f.w, it)) {
            it->AssignDirectSlot(it->DirectIndex(), "1던");
            continue;
        }
        const std::string_view te = it->DirectSurface();
        Item* n = it->next;
        if (e->dst != "1고" || (!te.ends_with("て") && !te.ends_with("で")) || n == nullptr || n->group->surface != "い" ||
            n->selected < 0 || n->DirectIndex() < 0 || n->DirectSurface() != "た" ||
            !ItemLemma(f, n, n->selected).starts_with("있") || !DescribedNoun(f.w, n)) {
            continue;
        }
        const Entry* d = n->DirectEntry(n->DirectIndex());
        if (d == nullptr || d->dst != "8") continue;
        it->AssignDirectSlot(it->DirectIndex(), "1던");
        n->AssignEntrySurface(n->selected, "");
        n->AssignDirectSlot(n->DirectIndex(), "");
    }
}

void NotYet(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->next;
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
        if (!potential && !std::any_of(std::begin(kNotYetVerbs), std::end(kNotYetVerbs),
                                       [&](std::string_view v) { return it->group->surface.ends_with(v); })) {
            continue;
        }
        Entry* d = n->DirectEntry(n->DirectIndex());
        // A plain ない (気付かない) changes only before a terminal ending; its attributive keeps 않다.
        if (!potential && n->group->surface == "な" && (d == nullptr || d->dst.starts_with("4"))) continue;
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
        const bool remember = it->group->surface.ends_with("覚え") || it->group->surface.ends_with("憶え");
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
    }
}

void SeeAnd(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || ItemLemma(f, it, it->selected) != "보다" ||
            !it->group->surface.ends_with("見") || it->DirectSurface() != "て") {
            continue;
        }
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr || e->dst != "6" || it->prev == nullptr || it->prev->group->surface != "を") continue;
        if (UtteranceEnd(f.w, it) && !From(it->text, it->start + it->length).starts_with(kJaComma)) continue;
        if (const Item* n = it->next; n != nullptr) {
            const std::string_view next = n->group->surface;
            bool stop = next.starts_with("」") || next.starts_with("』");
            for (const std::string_view a : kTeAuxiliaries) stop = stop || next.starts_with(a);
            if (stop || Has(kSeeAndFollowers, " " + std::string(next) + " ")) continue;
        }
        it->AssignDirectSlot(it->DirectIndex(), "1고");
    }
}

namespace {

// Whether the Korean lemma's stem ends in ㄹ (들다).
bool RieulStem(std::string_view lemma) {
    constexpr std::string_view kDa = "다";
    if (lemma.ends_with(kDa)) lemma.remove_suffix(kDa.size());
    return text::Final(DecodeLastRune(lemma).cp) == text::kFinalRieul;
}

bool QuoteVerbFollows(const Item* it);

// Marks of a main clause that is a supposition, hearsay, a degree or a habit rather than an
// event that followed (しまいそうだった, 崩すほど重かった, 使えるらしく, という欠点).
constexpr std::string_view kSupposedMarks[] = {"そう", "ほど", "らし", "よう", "はず", "かも", "という", "って", "べき", "もの", "こと"};

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
    for (const Item* x = it->next; x != nullptr; x = x->next) {
        if (x->selected < 0 || QuoteVerbFollows(x) || Has(x->Attr(x->selected), "UTh") || Has(x->Attr(x->selected), "UTs")) {
            return false;
        }
        if (x->DirectIndex() >= 0 && x->DirectSurface().ends_with("と")) return false;
        for (const std::string_view m : kSupposedMarks) {
            if (Has(x->group->surface, m) || (x->DirectIndex() >= 0 && Has(x->DirectSurface(), m))) return false;
        }
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
    if ((!end.ends_with("た") && !pastForm) || end.ends_with("よかった") || end.ends_with("良かった")) return;
    for (Item* it = f.w.chain.head; it != nullptr && it != last; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string_view d = it->DirectSurface();
        const bool seeing = e->dst == "2면" && d.ends_with("ば") &&
                            output::Lemma(it->EntryText(it->selected), f.families).ends_with("보다");
        // てみれば before a past is what one found on trying, also without a comma (出かけてみれば
        // 出会ったのは 나가 보니 만난 것은).
        const std::string_view stem = it->group->surface;
        const bool tried = seeing && (stem.ends_with("てみ") || stem.ends_with("でみ") ||
                                      (stem == "み" && it->prev != nullptr && it->prev->detail->Yougo() &&
                                       (it->prev->DirectSurface().ends_with(kJaTe) || it->prev->DirectSurface().ends_with(kJaDe))));
        // A point of view (長期的に見れば, 目で見れば, 外から見れば) is no step of the story.
        const bool viewpoint = it->prev != nullptr && (it->prev->group->surface.ends_with("的に") ||
                                                      it->prev->group->surface.ends_with("目で") ||
                                                      it->prev->group->surface.ends_with("から"));
        if (tried) {
            it->AssignDirectSlot(it->DirectIndex(), "2니");
        } else if (it->next == nullptr ||
                   (it->next->group->surface != kJaComma && (d != "と" || e->dst != "2면(자)" || !FollowedEvent(f, it, last)))) {
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


namespace {

// Stems that start a verb of saying or thinking (quoteVerbFollows).
constexpr std::string_view kQuoteVerbs[] = {
    "言", "いう", "いっ", "いわ", "思", "おもう", "おもっ", "おもわ", "おもえ", "願", "問", "叫", "告げ", "伝え", "答え",
    "考え", "信じ", "誓", "頼", "祈", "申", "呟", "囁", "怒鳴", "尋ね", "訴え", "嘆", "喚", "直訴", "宣言", "主張", "断言",
    "説明", "報告", "約束", "告白", "要求", "提案", "確信", "前置き", "念じ", "望", "命じ", "言い聞かせ", "感じ",
};

// Surfaces that start a verb of thinking (thinkingVerbAhead).
constexpr std::string_view kThinkingVerbs[] = {"思", "おもっ", "おもう", "感じ", "考え", "信じ", "確信"};

// Whether an item's text, as far as the chain goes, ends the utterance: the item is followed by
// the end of the text or by closing punctuation, a pause or a span boundary.
bool QuoteEnd(const Worker& w, const Item* it) {
    std::string_view rest = From(it->text, it->start + it->length);
    if (rest.empty() && it->tokenIndex + 1 < w.TokenCount()) {
        if (w.Span(it->tokenIndex + 1) != 0) return false;
        rest = w.Tok(it->tokenIndex + 1).text;
    }
    return rest.empty() || RuneIn(text::DecodeRune(rest).cp, "！？!?。．.」』）)");
}

// Whether the item starts with a verb of saying or thinking, or is 聞く.
bool QuoteVerbFollows(const Item* it) {
    if (it == nullptr) return false;
    if (it->group->surface == "聞") return true;
    for (const std::string_view v : kQuoteVerbs) {
        if (it->group->surface.starts_with(v)) return true;
    }
    return false;
}

// Whether a verb of thinking stands within four items after it, with no predicate and no clause
// boundary in between.
bool ThinkingVerbAhead(const Item* it) {
    const Item* x = it->next;
    for (int n = 0; n < 5 && x != nullptr && x->selected >= 0; n++, x = x->next) {
        for (const std::string_view v : kThinkingVerbs) {
            if (x->group->surface.starts_with(v)) return n > 0;
        }
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
                (QuoteEnd(f.w, it) || QuoteVerbFollows(it->next) || ThinkingVerbAhead(it))) {
                SelectLemma(f, it, "(이)라고");
                HearSense(f, it->next);
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
        if (d.ends_with("と")) HearSense(f, it->next);
        if (!d.ends_with("と") || (!e->dst.starts_with("2면") && !e->dst.starts_with("1다면"))) continue;
        const bool end = QuoteEnd(f.w, it);
        const std::string ja = it->group->surface + std::string(d);
        if (paused) {
            if (At(it->Code(it->selected), 0) == '7' && QuoteVerbAhead(it->next)) {
                it->AssignDirectSlot(it->DirectIndex(), "1다고,");
            }
            continue;
        }
        // たらと思う keeps the condition as an unfinished wish (行けたらと思う 갈 수 있으면 하고 생각한다);
        // before a verb of saying it is a suggestion (行ったらと言われた) and stays a quote.
        if ((d == "らと" || d.ends_with("たらと") || d.ends_with("だらと")) && WishVerbNext(it->next)) {
            it->AssignDirectSlot(it->DirectIndex(), std::string(e->dst.starts_with("1다면") ? "1다면" : "2면") + " 하고");
            continue;
        }
        const bool thought = (At(it->Code(it->selected), 0) == '7' || ja.ends_with("ないと") || d.starts_with("た")) &&
                             (ThinkingVerbAhead(it) ||
                              (it->next != nullptr && it->next->group->surface == kJaComma && QuoteVerbAhead(it->next->next)));
        // ないと that ends the utterance is a duty or a need (行かないと 가지 않으면, 時間がないと
        // 시간이 없으면), but the adjective ない after the topic は or も there closes a quotation
        // (毒はないと 독은 없다고, 何もないと 아무것도 없다고).
        const bool absent = (it->group->surface == "な" || it->group->surface == "無") && it->prev != nullptr &&
                            it->prev->selected >= 0 &&
                            (it->prev->group->surface.ends_with("は") || it->prev->group->surface.ends_with("も")) &&
                            ItemLemma(f, it, it->selected).ends_with("없다");
        const bool duty = ja.ends_with("ないと") && !absent;
        if ((!end && !QuoteVerbFollows(it->next) && !thought) || (end && (duty || SayingConditional(it))) ||
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

void NodaTo(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* n = it->next;
        if (n == nullptr || it->DirectIndex() < 0 || n->selected < 0 || !n->group->surface.starts_with("と")) continue;
        const std::string lemma = ItemLemma(f, n, n->selected);
        if (!lemma.starts_with("(이)라고") && !lemma.starts_with("라고")) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e != nullptr && e->dst.ends_with(" 것이다")) {
            it->AssignDirectSlot(it->DirectIndex(), std::string_view(e->dst).substr(0, e->dst.size() - std::string_view("다").size()));
        }
    }
}

void TerminalBeforeQuote(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* next = it->next;
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
        if (!pause || next == nullptr || (!next->group->surface.starts_with("と") && !next->group->surface.starts_with("なんて")) ||
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

// Question words that questionCopula reads.
constexpr std::string_view kQuestionWords[] = {"何", "なに", "誰", "だれ", "何故", "なぜ", "どこ", "いつ", "どれ"};

// Adverbs of degree that the 程 of hodoDegree follows.
constexpr std::string_view kDegreeTails[] = {"に", "の", "だ", "で", "まで"};

}  // namespace

void HodoDegree(Flusher& f) {
    bool conditional = false;
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->DirectIndex() < 0) continue;
        const std::string_view d = it->DirectSurface();
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (d.ends_with("ば")) conditional = true;
        const bool ok = d.ends_with("に");
        const std::string_view stem = ok ? d.substr(0, d.size() - std::string_view("に").size()) : d;
        if (conditional || (!stem.ends_with("程") && !stem.ends_with("ほど")) || e == nullptr) continue;
        const bool ni = it->next != nullptr && it->next->group->surface == "に";
        bool degree = ok;
        if (!degree && it->next != nullptr) {
            for (const std::string_view p : kDegreeTails) degree = degree || it->next->group->surface.starts_with(p);
        }
        if (!degree || (!Has(e->dst, "수록") && !(Has(e->dst, "만큼") && ni))) continue;
        it->AssignDirectSlot(it->DirectIndex(), ok ? "3 정도로" : "3 정도");
        if (!ok && ni) SelectLemma(f, it->next, "(으)로");
    }
}

void QuestionCopula(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        if (p == nullptr || it->group->surface != "だ" || ItemLemma(f, it, it->selected) != "(이)다" ||
            !InSet(kQuestionWords, p->group->surface)) {
            continue;
        }
        if (!QuoteEnd(f.w, it) && !From(it->text, it->start + it->length).starts_with(kJaComma)) continue;
        std::string word = ItemLemma(f, p, p->selected);
        if (word == "무엇") {
            if (p->group->At(p->selected) != nullptr) p->AssignEntrySurface(p->selected, "뭐");
            word = "뭐";
        }
        Entry* e = it->group->At(it->selected);
        if (e == nullptr) continue;
        it->AssignEntrySurface(it->selected, "냐");
        const char particle = output::ParticleClass(word);
        if (particle == 'c' || particle == 'l') it->AssignEntrySurface(it->selected, "이냐");
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
    const Item* n = it->next;
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

void GenitiveBetweenNames(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        const Item* n = it->next;
        if (p != nullptr && n != nullptr && it->group->surface == "の" && ItemLemma(f, it, it->selected).empty() &&
            p->Attr(p->selected).starts_with("QPN") && n->Attr(n->selected).starts_with("QPN")) {
            SelectLemma(f, it, "의");
        }
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

void ReturnTo(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        const Item* n = it->next;
        if (p != nullptr && n != nullptr && it->group->surface == "に" && ItemLemma(f, it, it->selected) == "하러" &&
            n->group->surface.starts_with("戻")) {
            SelectLemma(f, it, "(으)로");
        }
    }
}


void EvenBeforePassive(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* n = it->next;
        if (n == nullptr || it->prev == nullptr || it->group->surface != "でも" ||
            output::Lemma(it->EntryText(it->selected), f.families) != "에서도" || !n->detail->Yougo() ||
            Has(it->prev->Attr(it->prev->selected), "VPl")) {
            continue;
        }
        const std::string_view s = Slice(n->text, n->start, n->length);
        if (Has(s, "られ") || s.starts_with("され")) SelectLemma(f, it, "(이)라도");
    }
}

namespace {

// Words that start the words after から that keep (으)로부터 (kara source).
constexpr std::string_view kKeepFrom[] = {"なる", "成る", "守", "護", "逃", "救", "庇", "隠", "解放", "避", "離", "身を"};

// Words that end a word of time (kara source).
constexpr std::string_view kTimeWords[] = {
    "時", "頃", "日", "朝", "昼", "夜", "晩", "昔", "年", "月", "週", "次", "最初", "初め", "はじめ", "以前", "以後",
    "直前", "直後", "ずっと前", "少し前", "当初", "瞬間", "今",
};

// Nouns of position and situation (deScene, karaSource).
constexpr std::string_view kSceneWords[] = {
    "後ろ", "前", "横", "隣", "上", "下", "中", "外", "内", "奥", "側", "傍", "近く", "遠く", "真ん中", "隅", "裏", "表",
    "状況", "場", "場面", "現場", "世界", "夢", "環境", "空間",
};

// Verbs that take what they write or call something with と (comitativeTo, nounTo).
constexpr std::string_view kNamingVerbs[] = {"呼ぶ", "書く", "名乗る", "記す", "名付ける", "称する", "読む"};

// Verbs and verbal nouns of supposing or describing that take what they suppose with と.
constexpr std::string_view kSupposingWords[] = {"仮定", "表現", "判断", "定義", "解釈", "想定", "認識", "見なす", "称する", "形容", "断定"};

// Verbs and nouns of doing something with someone or comparing with something, whose partner takes と.
constexpr std::string_view kComitativeWords[] = {
    "合う", "戦う", "闘う", "会う", "出会う", "話す", "付き合う", "遊ぶ", "争う", "比べる", "似る", "違う", "異なる",
    "別れる", "交わる", "混ざる", "混じる", "並ぶ", "組む", "重なる", "ぶつかる", "離れる", "暮らす", "過ごす", "踊る",
    "知り合う", "見比べる", "釣り合う", "渡り合う", "向き合う", "張り合う", "話し合う", "語り合う", "競う", "揉める",
    "すれ違う", "等しい", "結婚", "喧嘩", "対決", "対戦", "勝負", "協力", "握手", "交換", "一致", "再会", "合流", "共闘",
    "衝突", "交代", "契約", "相談", "会話", "交際", "同居", "比較", "対立", "接触", "連携", "婚約", "和解", "仲直り",
    "同盟", "取引", "交渉", "決闘", "同じ", "同様", "一緒", "共",
};

// Whether s starts with one of the words.
template <std::size_t N>
bool StartsWithAny(std::string_view s, const std::string_view (&words)[N]) {
    for (const std::string_view w : words) {
        if (s.starts_with(w)) return true;
    }
    return false;
}

// Whether s ends with one of the words.
template <std::size_t N>
bool EndsWithAny(std::string_view s, const std::string_view (&words)[N]) {
    for (const std::string_view w : words) {
        if (s.ends_with(w)) return true;
    }
    return false;
}

// Whether s has only digits (ASCII or full-width) and kanji numerals, and is not empty.
bool NumberSurface(std::string_view s) {
    if (s.empty()) return false;
    for (std::string_view rest = s; !rest.empty(); rest.remove_prefix(text::DecodeRune(rest).size)) {
        const char32_t r = text::DecodeRune(rest).cp;
        const bool digit = (r >= U'0' && r <= U'9') || (r >= U'０' && r <= U'９');
        if (!digit && !RuneIn(r, "〇一二三四五六七八九十百千万億")) return false;
    }
    return true;
}

// The adverb-like noun targets (無理やり 억지로, 精一杯 힘껏): ending in 껏, 으로 or 지로.
bool AdverbTarget(std::string_view ko) {
    return ko.ends_with("껏") || ko.ends_with("으로") || ko.ends_with("지로");
}

}  // namespace

void KaraSource(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->group->surface.starts_with("から") || it->selected < 0) continue;
        Entry* e = it->group->At(it->selected);
        if (e == nullptr || !e->dst.starts_with("(으)로부터")) continue;
        const Item* n = it->next;
        if (n != nullptr && (n->group->surface == "が" || StartsWithAny(n->group->surface, kKeepFrom))) continue;
        const std::string_view before = std::string_view(it->text).substr(0, static_cast<std::size_t>(it->start));
        const char32_t last = DecodeLastRune(before).cp;
        const Item* p = it->prev;
        const std::string_view rest = std::string_view("(으)로부터");
        if (EndsWithAny(before, kTimeWords) || text::IsDigit(last) || RuneIn(last, "番号")) {
            it->AssignEntrySurface(it->selected, "부터" + e->dst.substr(rest.size()));
        } else if (p != nullptr && p->selected >= 0 && !p->detail->Yougo() && At(p->Code(p->selected), 0) == '1' &&
                   (Has(p->Attr(p->selected), "VPl") || Has(p->Attr(p->selected), "VBp") || InSet(kSceneWords, p->group->surface))) {
            it->AssignEntrySurface(it->selected, "에서" + e->dst.substr(rest.size()));
        }
    }
}

void NiAfterAdverb(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (p == nullptr || it->group->surface != "に" || it->selected < 0 || p->selected < 0 ||
            ItemLemma(f, it, it->selected) != "에") {
            continue;
        }
        const Item* n = it->next;
        const bool afterVerb = p->detail->Yougo() && p->DirectIndex() >= 0 && p->DirectEntry(p->DirectIndex()) != nullptr &&
                               p->DirectEntry(p->DirectIndex())->dst == "6" && n != nullptr && !InSet(kCommaItems, n->group->surface) &&
                               !InSet(kSentenceEndItems, n->group->surface) && !InSet(kClosingQuoteItems, n->group->surface) &&
                               n->group->surface != "。";
        const bool adverb = !p->detail->Yougo() && AdverbTarget(ItemLemma(f, p, p->selected));
        if (p->group->surface == "くらい" || p->group->surface == "ぐらい") {
            SelectLemma(f, it, "(으)로");
        } else if (afterVerb || adverb) {
            SelectLemma(f, it, "");
        }
    }
}

// Pronouns whose genitive takes the short form: 나의 -> 내, 저의 -> 제, 너의 -> 네.
constexpr std::string_view kFirstPersonPronouns[] = {"私", "僕", "俺", "わたし", "わたくし", "あたし", "アタシ", "ぼく", "おれ", "ボク", "オレ"};
constexpr std::string_view kSecondPersonPronouns[] = {"君", "お前", "おまえ", "きみ"};

// A pronoun and the genitive の right before a word (a letter or digit, in the same token
// or at the start of the next) read as the short possessive (내 책); before punctuation,
// a particle or a gap they stay 나의.
void ShortPossessive(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->next;
        if (n == nullptr || n->group->surface != "の" || it->selected < 0 || n->selected < 0 || it->detail->Yougo() ||
            ItemLemma(f, n, n->selected) != "의") {
            continue;
        }
        const Item* m = n->next;
        const int end = n->start + n->length;
        const bool adjacent =
            m != nullptr && ((m->tokenIndex == n->tokenIndex && m->start == end) ||
                             (m->tokenIndex == n->tokenIndex + 1 && end == static_cast<int>(n->text.size()) && m->start == 0));
        if (!adjacent || m->selected < 0 || At(m->Code(m->selected), 0) == '2' || At(m->Code(m->selected), 0) == '6') continue;
        if (const char32_t r = text::DecodeRune(m->group->surface).cp; !text::IsLetter(r) && !text::IsDigit(r)) continue;
        const std::string lemma = ItemLemma(f, it, it->selected);
        const std::string_view s = it->group->surface;
        // A phrase that opens with a clause (知らない間に 모르는 사이에) takes the pronoun as its
        // subject: 내가 모르는 사이에.
        const std::string_view head = m->EntryText(m->selected).substr(0, m->EntryText(m->selected).find(' '));
        bool kana = false;  // 知らない間に, not 笑顔 웃는 얼굴
        for (std::string_view t = m->group->surface; !t.empty() && !kana;) {
            const text::Rune r = text::DecodeRune(t);
            kana = r.cp >= U'ぁ' && r.cp <= U'ゖ';
            t.remove_prefix(r.size);
        }
        if (kana && head.size() < m->EntryText(m->selected).size() && (head.ends_with("는") || head.ends_with("던")) &&
            !SubjectPronoun(lemma).empty() &&
            (InSet(kFirstPersonPronouns, s) || InSet(kSecondPersonPronouns, s))) {
            TakeTarget(n, At(n->Code(n->selected), 0), "(이)가");
            if (it->group->At(it->selected) != nullptr) it->AssignEntrySurface(it->selected, std::string(SubjectPronoun(lemma)));
            continue;
        }
        std::string_view target;
        if ((lemma == "나" || lemma == "저") && InSet(kFirstPersonPronouns, s)) {
            target = lemma == "나" ? "내 " : "제 ";
        } else if (lemma == "너" && InSet(kSecondPersonPronouns, s)) {
            target = "네 ";
        } else {
            continue;
        }
        // The pronoun carries the space; the empty の adds none.
        TakeTarget(n, At(n->Code(n->selected), 0), "");
        TakeTarget(it, At(it->Code(it->selected), 0), target);
    }
}

namespace {

constexpr std::string_view kPoliteMarks[] = {"です", "ます", "ません", "ました", "ましょ", "まして", "でした", "でしょうか",
                                             "ござい", "ください", "下さい"};

// Whether s speaks politely; a polite word reported with って (一番ですって) does not.
bool PoliteSurface(std::string_view s) {
    if (Has(s, "ですって") || Has(s, "ますって") || Has(s, "でしたって") || Has(s, "でしょうかって")) return false;
    if (s.ends_with("いませ")) return true;
    for (const std::string_view m : kPoliteMarks) {
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
    for (const Item* q = it->next; q != nullptr; q = q->next) {
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

constexpr std::string_view kHumblePronouns[] = {"私", "わたし", "わたくし", "あたし", "僕", "ぼく",
                                                "私たち", "私達", "わたしたち", "僕たち", "僕達", "僕ら"};

}  // namespace

namespace {

bool OnReading(const Item* it) {
    return it != nullptr && it->selected >= 0 && Has(it->GAttr(it->selected), "On");
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
        const std::string target(it->EntryText(it->selected));
        if (const std::string ruled = InitialSound(target); ruled != target) {
            TakeTarget(it, At(it->Code(it->selected), 0), ruled);
        }
    }
}

// ちゃん right after a katakana noun as 짱 where it was left empty (ナッちゃん 낫짱); the
// empty ちゃん of a kin word (お姉ちゃん 언니) stays.
void ChanAfterKatakana(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (p == nullptr || it->selected < 0 || it->group->surface != "ちゃん" || !TrimSpace(it->EntryText(it->selected)).empty() ||
            p->start + p->length != it->start || p->tokenIndex != it->tokenIndex || p->group->surface.empty()) {
            continue;
        }
        bool katakana = true;
        for (size_t i = 0; i < p->group->surface.size();) {
            const text::Rune r = text::DecodeRune(p->group->surface.substr(i));
            katakana = katakana && ((r.cp >= U'ァ' && r.cp <= U'ヶ') || r.cp == U'ー');
            i += r.size == 0 ? 1 : r.size;
        }
        if (katakana && p->selected >= 0 && At(p->Code(p->selected), 0) == '1') SelectLemma(f, it, "짱");
    }
}

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
        if (const Item* n = it->next; n != nullptr && n->group->surface.starts_with("目")) continue;
        SelectLemma(f, it, "도");
    }
}

void PluralPronoun(Flusher& f) {
    static constexpr std::string_view kPluralSuffixes[] = {"たち", "達", "ら", "ども"};
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->next;
        if (n == nullptr || it->selected < 0 || n->selected < 0 || it->detail->Yougo() ||
            !InSet(kPluralSuffixes, n->group->surface) || n->tokenIndex != it->tokenIndex ||
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
        if (it->selected < 0 || it->detail->Yougo() || !InSet(kHumblePronouns, it->group->surface)) continue;
        const std::string lemma = ItemLemma(f, it, it->selected);
        std::string_view target;
        if (lemma == "나") target = "저";
        else if (lemma == "내") target = "제";
        else if (lemma == "내 ") target = "제 ";
        else if (lemma == "우리") target = "저희";
        else if (lemma == "우리들") target = "저희들";
        else continue;
        if (!PoliteAfter(it)) continue;
        if (target == "저" && it->next != nullptr && it->next->selected >= 0 &&
            ItemLemma(f, it->next, it->next->selected).starts_with("(이)가")) {
            target = "제";
        }
        TakeTarget(it, At(it->Code(it->selected), 0), target);
    }
}

void NoAfterNa(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (p == nullptr || it->group->surface != "の" || it->selected < 0 || p->selected < 0 || p->detail->Yougo() ||
            !p->group->surface.ends_with("な") || !ItemLemma(f, p, p->selected).ends_with("한") ||
            ItemLemma(f, it, it->selected) != "의") {
            continue;
        }
        SelectLemma(f, it, "것");
    }
}

void DeScene(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (p == nullptr || it->group->surface != "で" || it->selected < 0 || p->selected < 0 || p->detail->Yougo() ||
            ItemLemma(f, it, it->selected) != "(으)로") {
            continue;
        }
        if (p->group->surface == "一瞬" || p->group->surface == "瞬時") {
            SelectLemma(f, it, "에");
        } else if (InSet(kSceneWords, p->group->surface)) {
            SelectLemma(f, it, "에서");
        }
    }
}

void YounaBeforeModifier(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* n = it->next;
        if (n == nullptr || it->group->surface != "のような" || it->selected < 0 ||
            ItemLemma(f, it, it->selected) != "(와)과 같이" || !n->detail->Yougo()) {
            continue;
        }
        const std::string_view d = n->DirectSurface();
        const Item* after = n->next;
        if (d == "な" || (d == "い" && after != nullptr && !after->detail->Yougo() && after->selected >= 0 &&
                          At(after->Code(after->selected), 0) == '1')) {
            SelectLemma(f, it, "(와)과 같은");
        }
    }
}

void JiBeforeJiman(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->next;
        if (n == nullptr || n->selected < 0 || it->DirectIndex() < 0 ||
            (!n->group->surface.starts_with("けど") && !n->group->surface.starts_with("けれど")) ||
            !ItemLemma(f, n, n->selected).ends_with("지만")) {
            continue;
        }
        const Entry* direct = it->DirectEntry(it->DirectIndex());
        if (direct != nullptr && direct->dst.ends_with("겠지")) {
            if (n->group->At(n->selected) != nullptr) n->AssignEntrySurface(n->selected, "만");
        }
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
        if (!Noka(it) || (it->next != nullptr && !PunctuationItem(it->next))) continue;
        run.push_back(it);
    }
    if (run.size() < 2) return;
    // Every clause but the last is followed by a comma.
    for (std::size_t k = 0; k + 1 < run.size(); k++) {
        const std::string_view s = run[k]->next->group->surface;
        if (s != "、" && s != ",") return;
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

}  // namespace

void OfferPromise(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Entry* e = it->group->At(it->selected);
        const Entry* d = it->DirectEntry(it->DirectIndex());
        const std::string_view surface = it->group->surface;
        const bool humble = surface.ends_with("さしあげ") || surface.ends_with("差し上げ");
        if (e == nullptr || d == nullptr || !(e->dst.ends_with("&CJ@") || (humble && e->dst.ends_with("드&AA@"))) ||
            (!surface.ends_with("や") && !surface.ends_with("あげ") && !surface.ends_with("上げ")) || !QuoteEnd(f.w, it)) {
            continue;
        }
        const std::string_view ending = it->DirectSurface();
        // Only a present or volitional ending turns: not 教えてやったよ, 教えてさしあげられます.
        if (it->DetailRecord() != 0 || d->dst.empty() || !AnyOf("46017", d->dst[0])) continue;
        std::string_view after = From(it->text, it->start + it->length);
        if (after.empty() && it->tokenIndex + 1 < f.w.TokenCount()) after = f.w.Tok(it->tokenIndex + 1).text;
        if (after.starts_with("？") || after.starts_with("?")) continue;
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
        // A final particle marks speech outside brackets too (a line of dialogue).
        const bool spoken = ending == "よ" || ending == "ね" || ending == "わ" || ending == "さ" || ending == "ぜ" ||
                            ending == "ぞ";
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

void ParticleInNext(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* n = it->next;
        if (n == nullptr || it->selected < 0 || n->selected < 0 || it->detail->Yougo()) continue;
        Entry* e = it->group->At(it->selected);
        if (e == nullptr || e->cls != 'P') continue;
        const std::string lemma = ItemLemma(f, it, it->selected);
        if (!lemma.empty() && ItemLemma(f, n, n->selected).starts_with(lemma + " ")) it->AssignEntrySurface(it->selected, "");
    }
}

void ComitativeTo(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        Item* n = it->next;
        if (p == nullptr || n == nullptr || it->selected < 0 || p->selected < 0 || n->selected < 0) continue;
        std::string_view bracket;
        if (it->group->surface == "と") {
            if (!NumberSurface(p->group->surface)) continue;
        } else if (it->group->surface == "」と" || it->group->surface == "』と") {
            if (At(p->Code(p->selected), 0) != '1' || p->prev == nullptr || !Has("「『", p->prev->group->surface)) continue;
            bracket = "」";
        } else {
            continue;
        }
        const std::string lemma = ItemLemma(f, it, it->selected);
        const std::string_view bare = bracket.empty() || !lemma.starts_with(bracket) ? std::string_view(lemma)
                                                                                      : std::string_view(lemma).substr(bracket.size());
        if (bare != "(으)로" && bare != "(이)라고" && bare != "라고") continue;
        const bool quote = QuoteVerbFollows(n) || IsAny(n, kNamingVerbs);
        const std::string after = n->next != nullptr ? n->next->group->surface : std::string();
        bool partner = !quote && At(n->Code(n->selected), 0) == '1';
        if (bracket.empty()) {
            const bool sound = !after.empty() && RuneIn(text::DecodeRune(after).cp, "しすさせ");
            partner = partner && after != "は" && after != "が" && (after.empty() || !sound);
        } else {
            partner = partner && Has(" を の も や と ", " " + after + " ");
        }
        const char cls = At(it->Code(it->selected), 0);
        if (IsAny(n, kComitativeWords) || partner) {
            TakeTarget(it, cls, std::string(bracket) + "(와)과");
        } else if (quote && bracket.empty()) {
            TakeTarget(it, cls, "(이)라고");
        }
    }
}

void NounTo(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        const Item* n = it->next;
        if (p == nullptr || n == nullptr || it->group->surface != "と" || it->selected < 0 || p->selected < 0 ||
            n->selected < 0 || p->detail->Yougo() || At(p->Code(p->selected), 0) != '1') {
            continue;
        }
        const char cls = At(it->Code(it->selected), 0);
        const std::string lemma = ItemLemma(f, it, it->selected);
        if ((lemma == "(으)로" || lemma == "(이)라고" || lemma == "라고") && IsAny(n, kComitativeWords)) {
            TakeTarget(it, cls, "(와)과");
        } else if (lemma == "(와)과" && (IsAny(n, kNamingVerbs) || IsAny(n, kSupposingWords))) {
            TakeTarget(it, cls, "(이)라고");
        }
    }
}

namespace {

// Whether nothing but spaces stands before the item on its line, or a sentence end does.
bool OpensLine(const Worker& w, const Item* it) {
    std::string_view before = it->text.substr(0, static_cast<std::size_t>(std::max(it->start, 0)));
    for (int t = it->tokenIndex;;) {
        before = TrimSpace(before);
        if (!before.empty()) return RuneIn(DecodeLastRune(before).cp, "。．.！？!?");
        if (--t < 0) return true;
        before = w.Tok(t).text;
    }
}

}  // namespace

void LineInitialTo(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* n = it->next;
        if (it->prev != nullptr || n == nullptr || it->group->surface != "と" || it->selected < 0 || it->Fixed() ||
            n->selected < 0 || !OpensLine(f.w, it)) {
            continue;
        }
        const std::string_view next = n->group->surface;
        // Before a word of company, sameness or difference the と is the comitative going on
        // from the line before (と一緒に行こう 와 함께 가자, と同じだ 와 같다).
        constexpr std::string_view kPartnerWords[] = {"一緒", "いっしょ", "共に", "ともに", "同じ", "おなじ", "違",
                                                      "ちが",  "比べ",     "くらべ", "異な", "似"};
        if (std::any_of(std::begin(kPartnerWords), std::end(kPartnerWords), [&](std::string_view w) { return next.starts_with(w); })) {
            continue;
        }
        if (next.starts_with("…") || next.starts_with("‥") || next.starts_with("と") ||
            (At(n->Code(n->selected), 0) == '4' && (next.starts_with("な") || next.starts_with("す") || next.starts_with("し")))) {
            continue;
        }
        const std::string lemma = ItemLemma(f, it, it->selected);
        if (lemma == "(와)과" || lemma == "(으)로") TakeTarget(it, At(it->Code(it->selected), 0), "(이)라고");
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

// Final syllables of Korean adjective stems: their 지 않다 is the negative of an adjective
// (기분이 좋지 않다, 좋지 않은), not of a verb (않는다, 않는).
constexpr std::string_view kAdjectiveStemEnds[] = {
    "좋", "같", "싶", "나쁘", "많", "싫", "아프", "어렵", "두렵", "밉", "넓", "탐탁", "점잖",
};

bool AdjectiveNegative(std::string_view lemma) {
    const std::string_view stem = lemma.substr(0, lemma.size() - std::string_view("지 않다").size());
    return std::any_of(std::begin(kAdjectiveStemEnds), std::end(kAdjectiveStemEnds),
                       [&](std::string_view e) { return stem.ends_with(e); });
}

}  // namespace

void ExclaimedAdjective(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->DirectIndex() < 0 || OwnedGroup(it->DirectGroup()) || At(it->Code(it->selected), 0) != '7' ||
            it->DirectText(it->DirectIndex()) != "4" || !IsOneOf(it->next, kCommaItems)) {
            continue;
        }
        if (const Item* p = it->prev; p != nullptr && !IsOneOf(p, kExclaimOpeners)) continue;
        if (const Item* n = it->next->next; n != nullptr && n->selected >= 0 && At(n->Code(n->selected), 0) == '7' &&
                                            !IsWord(n, it->group->surface)) {
            continue;
        }
        if (const Item* n = it->next->next; n != nullptr && n->selected >= 0 &&
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
