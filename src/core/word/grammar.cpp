// Grammar passes of the text path: the readings of a verb's られ, and the conjugating
// targets of passive verbs that have no static family.
#include <algorithm>
#include <string>
#include <string_view>

#include "core/output/korean.h"
#include "core/output/families.h"
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
    {"捨て", "버려지다:규칙"}, {"育て", "길러지다:규칙"},
};

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

// The passive reading of a られた or られて with no other cue.
bool NarrativePassive(const Item* it, std::string_view rest) {
    if (!rest.starts_with("た") && !rest.starts_with("て")) return false;
    return !InSet(kAbilityVerbs, it->group->surface);
}

// The passive reading of the られ of verb item it; rest is the text after られ:
//   - a negative, conditional or volitional form and the polite non-past are potential;
//   - a verb that takes no object is potential, unless it is listed or a compound action;
//   - ている, ていた, てる, ており are passive;
//   - going back through the clause, the nearest を (potential unless listed) or an agent
//     に, から, によって, により after a thing-free word decides;
//   - listed verbs and narrative forms are passive;
//   - a subject は or が that is a thing makes the reading passive, unless the verb is the
//     plain non-past form;
//   - otherwise potential.
bool PassiveReading(const Item* it, std::string_view rest) {
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
        if (s == "を" && !listed) {
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

// Writes a passive verb item of kPassiveLemmas as its Korean passive in the active voice.
void WriteLexicalPassive(Flusher& f, Item* it) {
    std::string lexical;
    if (!PassiveTarget(f, it->group->surface, lexical)) return;
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
        if (!PassiveReading(it, rest)) continue;
        it->detail->record = 1;
        for (Entry* e : it->group->entries) {
            if (e->gattr.ends_with("Ka")) e->gattr = e->gattr.substr(0, e->gattr.size() - 2) + "Pi";
        }
        WriteLexicalPassive(f, it);
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
        if (p == nullptr || it->selected < 0 || p->selected < 0 ||
            output::Lemma(it->EntryText(it->selected), f.families) != "(이)가") {
            continue;
        }
        if (it->group->surface == "が") {
            const std::string_view subject = SubjectPronoun(output::Lemma(p->EntryText(p->selected), f.families));
            if (!subject.empty() && !p->detail->Yougo()) {
                if (p->group->At(p->selected) != nullptr) p->AssignEntrySurface(p->selected, std::string(subject));
            }
        } else if (it->group->surface == "の") {
            const Item* n = it->next;
            if (n != nullptr && n->detail->Yougo() && Has(n->group->surface, "を")) SelectLemma(f, it, "의");
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
};

// Verbal nouns whose されている is a state.
constexpr std::string_view kResultNouns[] = {
    "舗装", "設置", "固定", "封印", "整備", "保存", "記載", "記録", "配置", "装備", "収納", "展示", "包装", "印刷", "登録",
    "接続", "密閉", "施錠", "武装", "装飾", "彫刻", "刻印", "整頓", "固定化",
};

// Stems of the passive verbs whose ている is a state.
constexpr std::string_view kResultPassives[] = {
    "並べ", "置", "囲", "覆", "積", "飾", "掛け", "盛", "刻", "書", "貼", "吊", "敷", "張", "描", "記", "包", "詰め",
    "閉じ込め", "埋め", "刺", "縛", "隠", "残", "向け", "付け", "込め", "組", "建て", "植え", "収め", "閉ざ", "塗", "彫",
    "繋", "結", "束ね", "挟", "重ね", "据え", "閉め", "開け", "括り付け", "縛り付け",
};

// The adverbs of gradual change before a ている of なる (a progression).
constexpr std::string_view kGradualAdverbs[] = {
    "次第", "徐々", "どんどん", "だんだん", "段々", "ますます", "益々", "日に日に", "少しずつ", "みるみる",
};

// Stems of the verbs of noticing or remembering that take 못하다 (notYet).
constexpr std::string_view kNotYetVerbs[] = {"気づ", "気付", "気がつ", "気が付", "覚え"};

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

}  // namespace

void ResultState(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0) continue;
        const std::string_view stem = it->group->surface;
        const char c1 = At(it->Code(it->selected), 1);
        const bool object = c1 >= '3' && c1 <= '6';
        if (stem == "な" && GradualBefore(it)) continue;
        const bool nounPassive = stem == "され" && it->prev != nullptr && InSet(kResultNouns, it->prev->group->surface);
        const bool state = (it->detail->record == 0 && !it->lexical && (InSet(kResultVerbs, stem) || nounPassive) && !object) ||
                           ((it->detail->record == 1 || it->lexical) && InSet(kResultPassives, stem));
        if (!state) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string lemma = output::Lemma(e->dst, f.families);
        if (!lemma.starts_with("1고")) continue;
        const std::string_view rest = std::string_view(lemma).substr(std::string_view("1고").size());
        if (rest.starts_with(" 있") || (rest.empty() && it->next != nullptr && ItemLemma(f, it->next, it->next->selected).starts_with("있"))) {
            it->AssignDirectSlot(it->DirectIndex(), "6" + std::string(rest));
        }
    }
}

void NotYet(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->next;
        if (n == nullptr || !it->detail->Yougo() || it->DirectIndex() < 0) continue;
        const Entry* direct = it->DirectEntry(it->DirectIndex());
        if (direct == nullptr || direct->dst != std::string("1") + std::string(kKoJi) || n->group->surface != "いな" ||
            ItemLemma(f, n, n->selected) != "않다" || !std::any_of(std::begin(kNotYetVerbs), std::end(kNotYetVerbs),
                                                                    [&](std::string_view v) { return it->group->surface.ends_with(v); })) {
            continue;
        }
        output::Conjugated target = output::Conjugating("못하다", '$', "", f.families);
        if (n->group->At(n->selected) != nullptr) n->AssignEntrySurface(n->selected, target.target);
        Entry* d = n->DirectEntry(n->DirectIndex());
        if (d == nullptr) continue;
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
    if (!end.ends_with("た") || end.ends_with("よかった") || end.ends_with("良かった")) return;
    for (Item* it = f.w.chain.head; it != nullptr && it != last; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string_view d = it->DirectSurface();
        if (it->next == nullptr || it->next->group->surface != kJaComma) {
            continue;
        } else if (e->dst == "2면" && d.ends_with("ば") && output::Lemma(it->EntryText(it->selected), f.families).ends_with("보다")) {
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
        const std::string_view d = it->DirectSurface();
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        if (d.ends_with("と")) HearSense(f, it->next);
        if (!d.ends_with("と") || (!e->dst.starts_with("2면") && !e->dst.starts_with("1다면"))) continue;
        const bool end = QuoteEnd(f.w, it);
        const std::string ja = it->group->surface + std::string(d);
        // たらと思う keeps the condition as an unfinished wish (行けたらと思う 갈 수 있으면 하고 생각한다);
        // before a verb of saying it is a suggestion (行ったらと言われた) and stays a quote.
        if ((d == "らと" || d.ends_with("たらと") || d.ends_with("だらと")) && WishVerbNext(it->next)) {
            it->AssignDirectSlot(it->DirectIndex(), std::string(e->dst.starts_with("1다면") ? "1다면" : "2면") + " 하고");
            continue;
        }
        const bool thought = (At(it->Code(it->selected), 0) == '7' || ja.ends_with("ないと") || d.starts_with("た")) &&
                             ThinkingVerbAhead(it);
        if ((!end && !QuoteVerbFollows(it->next) && !thought) || (end && (ja.ends_with("ないと") || SayingConditional(it))) ||
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

// Punctuation before which an exclaimed adjective may open the utterance.
constexpr std::string_view kExclaimOpeners[] = {"「", "『", "（", "(", "…", "……", "、"};

// Whether the stem's negative lemma is a state (verbNegative).
bool StativeNegative(std::string_view lemma) {
    const std::string_view stem = lemma.substr(0, lemma.size() - std::string_view("지 않다").size());
    return stem.ends_with("있") || stem.ends_with("그렇") || stem.ends_with("얼마 되");
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
        if (stem.ends_with("く")) {
            if (e->dst == "4") it->AssignDirectSlot(it->DirectIndex(), "1은");
        } else if (e->dst == "1다") {
            it->AssignDirectSlot(it->DirectIndex(), "1는다");
        } else if (e->dst == "4") {
            it->AssignDirectSlot(it->DirectIndex(), "1는");
        }
    }
}

}  // namespace hdTrans::word
