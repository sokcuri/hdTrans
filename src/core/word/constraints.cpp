// The constraint rules and the pass that applies them to the candidates of a lookup.
#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"
#include "core/word/constraint_rules.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

constexpr ConstraintRule kRules[] = {
    {"honorific-katakana", ConstraintWord::Katakana, "", ConstraintContext::Honorific, ConstraintReject::NonPerson,
     "가타카나 단어 + 경칭(さん·くん·様 등): 사람·동물·직함 뜻이 없으면 일반 뜻을 버리고 인명 읽기 (ハル君 헐군 → 하루군)", {}},
    {"honorific-hiragana", ConstraintWord::Hiragana, "", ConstraintContext::FamiliarHonorific, ConstraintReject::NonPerson,
     "히라가나 형용사 어간과 くん, 君, ちゃん: 같은 음가의 기존 인명을 읽되 동사 활용은 보존", {}},
    {"honorific-kanji-name", ConstraintWord::Kanji, "", ConstraintContext::FamiliarHonorific,
     ConstraintReject::NonName,
     "한자 단어 + くん·君·ちゃん·たん: 사전에 인명 뜻이 있고 사람 명사 뜻이 없으면 인명 뜻만 (翼くん 날개 군 → 츠바사군)", {}},
    {"honorific-kanji-name-place", ConstraintWord::Kanji, "", ConstraintContext::Honorific,
     ConstraintReject::NonNameBesidePlace,
     "한자 단어 + 경칭: 인명 뜻 말고는 지명 뜻뿐이면 인명 뜻만 (太郎殿 타로전 → 타로님)", {}},
    {"kimi-after-katakana", ConstraintWord::Source, "君", ConstraintContext::AfterKatakana, ConstraintReject::Pronoun,
     "가타카나 바로 뒤의 君은 대명사(너·네)가 아니라 접미사 군 (バカ君が 바보네가 → 바보군이)", {}},
    {"quote-after-exclamation", ConstraintWord::Source, "と", ConstraintContext::AfterExclamation,
     ConstraintReject::NonQuote, "！·？ 바로 뒤의 と는 인용 조사 라고 (ドン！と 돈! 와 → 돈! 라고)", {}},
    {"quote-tte-after-pause", ConstraintWord::Source, "って", ConstraintContext::AfterPause,
     ConstraintReject::NonQuote, "！·？·、·… 뒤에서 명사(奴·こと·話 등)를 꾸미는 って는 인용 (休暇、って奴 휴가, 는 녀석 → 휴가, 라는 녀석)", {}},
    {"iwaku-line-opening", ConstraintWord::Source, "曰く", ConstraintContext::LineOpening, ConstraintReject::NonAdverb,
     "줄 첫머리의 曰く、는 전하는 말을 여는 부사 (曰く、彼は 왈, 그는 → 듣자 하니, 그는)", {}},
    {"iwaku-elsewhere", ConstraintWord::Source, "曰く", ConstraintContext::Elsewhere, ConstraintReject::Adverb,
     "줄 첫머리가 아닌 曰く는 부사 뜻을 버림 (先生曰く 선생님 왈)", {}},
    {"tsui-numeral-ago", ConstraintWord::Source, "つい", ConstraintContext::BeforeNumeralAgo,
     ConstraintReject::OtherTargets, "수와 前 앞의 つい는 불과 (つい3時間前 무심코 3시간 전 → 불과 3시간 전). つい笑った는 그대로", "불과"},
    {"saki-after-verb", ConstraintWord::Source, "先", ConstraintContext::AfterVerbEnding, ConstraintReject::OtherTargets,
     "동사 た·だ·る 바로 뒤의 先은 곳 (嫁いだ先 시집간 앞 → 시집간 곳). まだ先, この先는 그대로", "곳"},
    {"sakini-after-verb", ConstraintWord::Source, "先に", ConstraintContext::AfterVerbEnding,
     ConstraintReject::OtherTargets, "동사 た·だ·る 바로 뒤의 先に는 곳에 (戻した先に 되돌린 먼저 → 되돌린 곳에)", "곳에"},
    {"kou-naru", ConstraintWord::Source, "こう", ConstraintContext::BeforeNaru, ConstraintReject::OtherTargets,
     "なる 앞의 こう는 부사 이렇게 (こうなるんだ 이러한 이다 → 이렇게 되는 거야). こうなのだ는 그대로", "이렇게"},
    {"sakini-after-demonstrative", ConstraintWord::Source, "先に", ConstraintContext::AfterDemonstrative,
     ConstraintReject::OtherTargets, "この·その·あの 뒤의 先に는 앞에 (この先に 이 먼저 → 이 앞에). 先に行く는 그대로 먼저", "앞에"},
    {"tachisagari-before-agaru", ConstraintWord::Source, "一人立ち", ConstraintContext::BeforeAgaru,
     ConstraintReject::Word, "上が 앞의 一人立ち은 독립이 아니라 一人 + 立ち上がり (一人立ち上がり 독립 올라 → 혼자 일어서). 一人立ちする는 그대로 독립", {}},
};

struct HonorificSuffix {
    std::string_view text;
    bool familiar;
};

constexpr HonorificSuffix kHonorifics[] = {
    {"さん", false}, {"さま", false}, {"様", false}, {"殿", false}, {"氏", false},  {"先輩", false},
    {"先生", false}, {"くん", true},  {"君", true},  {"ちゃん", true}, {"たん", true},
};

// The numerals that start a quantity (つい三時間前, つい3日前, つい数分前).
constexpr std::u32string_view kNumerals = U"0123456789０１２３４５６７８９一二三四五六七八九十百千数何半";
// The characters that end the quantity before 前 is found.
constexpr std::u32string_view kQuantityBreaks = U"、。，．！？!?…「」『』（）() 　";

constexpr std::string_view kOpeningBrackets[] = {"「", "『", "（", "(", "“", "‘", "【", "〈", "《", "［", "["};

// What continues a word rather than follows a particle (とって, ととと, とぉぉ).
constexpr std::string_view kDrawls[] = {"っ", "と", "ぉ", "ぁ", "ー", "～", "↓", "↑"};
// The nouns that a quotation in って modifies (って奴 라는 녀석).
constexpr std::string_view kQuotedNouns[] = {"奴", "やつ", "こと", "事", "もの", "物", "感じ", "話", "言葉", "のは", "のが"};

bool AllOfClass(std::string_view s, int cls) {
    if (s.empty()) return false;
    for (std::string_view t = s; !t.empty();) {
        const std::string_view c = FirstChar(t);
        if (c.empty() || NameClassOf(c) != cls) return false;
        t.remove_prefix(c.size());
    }
    return true;
}

bool HangulText(std::string_view s) {
    if (s.empty()) return false;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.cp < U'가' || r.cp > U'힣') return false;
        s.remove_prefix(r.size);
    }
    return true;
}

bool HonorificAt(std::string_view s, bool familiarOnly) {
    for (const HonorificSuffix& h : kHonorifics) {
        if ((h.familiar || !familiarOnly) && s.starts_with(h.text)) return true;
    }
    return false;
}

bool HiraganaHonorificAt(std::string_view s) {
    if (s.starts_with("ちゃんと")) return false;
    for (const std::string_view suffix : {"くん", "君", "ちゃん"}) {
        if (!s.starts_with(suffix)) continue;
        const std::string_view after = s.substr(suffix.size());
        if (after.empty() || kQuantityBreaks.find(text::DecodeRune(after).cp) != std::u32string_view::npos) return true;
        for (const std::string_view follower : {"は", "が", "を", "の", "に", "へ", "と", "も", "で", "だ", "か",
                                               "よ", "ね", "から", "まで", "って", "なら"}) {
            if (after.starts_with(follower)) return true;
        }
    }
    return false;
}

bool NameSense(const dict::Codes& c) {
    return c.sem.HasPrefix("QPN") || c.gram.HasPrefix("Wa") || c.gram.HasPrefix("Wb");
}

// A sense of a person, a group of people, an animal or a role.
bool PersonSense(const dict::Codes& c) {
    for (const std::string_view code : {"QPN", "QNP", "VHn", "VQw", "VQm", "VGn", "VGt", "VAm", "UAq", "UQf"}) {
        if (c.sem.HasCode(code)) return true;
    }
    return c.sem.HasCodeIn("QAA", "QAH") || NameSense(c);
}

bool PersonNoun(const dict::Codes& c) {
    for (const std::string_view code : {"VHn", "VQw", "VQm", "VGn", "UAq", "UQf"}) {
        if (c.sem.HasCode(code)) return true;
    }
    return false;
}

// Whether the word source, after the character before, is the kind of word rule is for.
bool WordFits(const ConstraintRule& rule, std::string_view source, std::string_view before) {
    switch (rule.word) {
    case ConstraintWord::Katakana:
        return text::RuneCount(source) >= 2 && AllOfClass(source, kClassKatakana) &&
               NameClassOf(before) != kClassKatakana;
    case ConstraintWord::Hiragana:
        return text::RuneCount(source) >= 2 && AllOfClass(source, kClassHiragana) &&
               NameClassOf(before) != kClassHiragana && NameClassOf(before) != kClassKanji;
    case ConstraintWord::Kanji:
        return AllOfClass(source, kClassKanji) && NameClassOf(before) != kClassKanji && FindHonorificPrefix(before) == -1;
    case ConstraintWord::Source:
        return source == rule.source;
    }
    return false;
}

// Whether s starts with a numeral and has 前 within eight characters, before any break.
bool NumeralAgo(std::string_view s) {
    for (int n = 0; n < 9 && !s.empty(); n++) {
        const text::Rune r = text::DecodeRune(s);
        if (n == 0 && kNumerals.find(r.cp) == std::u32string_view::npos) return false;
        if (r.cp == U'前') return n > 0;
        if (kQuantityBreaks.find(r.cp) != std::u32string_view::npos) return false;
        s.remove_prefix(r.size);
    }
    return false;
}

bool StartsWithAny(std::string_view s, std::span<const std::string_view> list) {
    return std::any_of(list.begin(), list.end(), [s](std::string_view p) { return s.starts_with(p); });
}

// Whether the context of rule holds for the word source of the candidates list: after is
// the text after it, before the character before it in its token, previous the character
// before it past spaces and token ends, preceding the text right before it (its token's
// head, else the token before).
bool ContextFits(const ConstraintRule& rule, const std::vector<dict::Candidate>& list, std::string_view source,
                 std::string_view after, std::string_view before, std::string_view previous,
                 std::string_view preceding) {
    const bool exclamation = previous == "！" || previous == "？" || previous == "!" || previous == "?";
    switch (rule.context) {
    case ConstraintContext::Honorific:
    case ConstraintContext::FamiliarHonorific:
        return HonorificAt(after, rule.context == ConstraintContext::FamiliarHonorific) &&
               (rule.word != ConstraintWord::Hiragana || HiraganaHonorificAt(after)) &&
               std::none_of(list.begin(), list.end(), [&](const dict::Candidate& c) { return c.source.size() > source.size(); });
    case ConstraintContext::AfterKatakana:
        return NameClassOf(before) == kClassKatakana;
    case ConstraintContext::AfterExclamation:
        return exclamation && !StartsWithAny(after, kDrawls);
    case ConstraintContext::AfterPause:
        return (exclamation || previous == "、" || previous == "…" || previous == "‥" || previous == ",") &&
               StartsWithAny(after, kQuotedNouns);
    case ConstraintContext::BeforeNumeralAgo:
        return NumeralAgo(after);
    case ConstraintContext::AfterVerbEnding:
        return (preceding.ends_with("た") || preceding.ends_with("だ") || preceding.ends_with("る")) &&
               !preceding.ends_with("まだ");
    case ConstraintContext::AfterDemonstrative:
        return preceding.ends_with("この") || preceding.ends_with("その") || preceding.ends_with("あの");
    case ConstraintContext::BeforeNaru:
        return after.starts_with("なる") && !after.starts_with("なるべく") && !after.starts_with("なるほど");
    case ConstraintContext::BeforeAgaru:
        return after.starts_with("上が");
    case ConstraintContext::LineOpening:
    case ConstraintContext::Elsewhere:
        return ((previous.empty() || StartsWithAny(previous, kOpeningBrackets)) &&
                (after.starts_with("、") || after.starts_with("，") || after.starts_with(","))) ==
               (rule.context == ConstraintContext::LineOpening);
    }
    return false;
}

// The person reading of the name layer for the katakana word source that replaces its
// senses same, or null when one of them is a person or sounds like the kana.
const dict::Record* PersonReading(const dict::Snapshot& snapshot, const std::vector<dict::Candidate>& list,
                                  const std::vector<std::size_t>& same, std::string_view source,
                                  std::vector<dict::Record>& records) {
    if (std::any_of(same.begin(), same.end(),
                    [&](std::size_t k) { return PersonSense(*list[k].codes) || list[k].user != nullptr; })) {
        return nullptr;
    }
    const std::string reading = text::FromKana(source, text::Preset::Fan);
    if (reading.empty() ||
        std::any_of(same.begin(), same.end(), [&](std::size_t k) { return text::SameSound(list[k].target, reading); })) {
        return nullptr;
    }
    records = snapshot.NameRecords(source);
    for (const dict::Record& r : records) {
        if (r.codes != nullptr && r.codes->sem.HasPrefix("QPN") && text::SameSound(r.target, reading)) return &r;
    }
    return nullptr;
}

// The name sense among same that replaces the others, or same.end() when there is none or
// a person noun, a verb or adjective as long as source, or (beside places) another sense
// than a place is among the candidates.
std::vector<std::size_t>::const_iterator NameAmong(const ConstraintRule& rule, const std::vector<dict::Candidate>& list,
                                                    const std::vector<std::size_t>& same, std::string_view source) {
    auto codes = [&](std::size_t k) -> const dict::Codes& { return *list[k].codes; };
    const auto name = std::find_if(same.begin(), same.end(), [&](std::size_t k) {
        return NameSense(codes(k)) && list[k].user == nullptr && HangulText(list[k].target);
    });
    const bool blocked =
        std::any_of(list.begin(), list.end(), [&](const dict::Candidate& c) {
            return c.codes != nullptr && AnyOf("478", c.codes->part.ClassDigit()) && c.source.size() >= source.size();
        }) ||
        std::any_of(same.begin(), same.end(), [&](std::size_t k) { return PersonNoun(codes(k)) || list[k].user != nullptr; }) ||
        std::all_of(same.begin(), same.end(), [&](std::size_t k) { return NameSense(codes(k)); }) ||
        (rule.reject == ConstraintReject::NonNameBesidePlace && !std::all_of(same.begin(), same.end(), [&](std::size_t k) {
             return NameSense(codes(k)) || codes(k).sem.HasCode("QCT") || codes(k).sem.HasCode("QNN");
         }));
    return blocked ? same.end() : name;
}

}  // namespace

std::span<const ConstraintRule> ConstraintRules() { return kRules; }

// Text path, mode 0. For each rule and each source of the candidates that the rule's word
// and context fit, the rejected candidates leave the list; a name made from a person
// reading takes the place of the first of them.
void Worker::ApplyConstraints(dict::Candidates& found, std::string_view query) {
    const std::string_view surface = transformSurface;
    if (!surface.ends_with(query)) return;
    const std::string_view head = surface.substr(0, surface.size() - query.size());
    const std::string_view before = LastChar(head);
    std::string_view previous = LastChar(TrimRunes(head, " 　"));
    for (int j = current - 1; previous.empty() && j >= 0; j--) {
        const std::string_view t = TrimRunes(Tok(j).text, " 　");
        if (!t.empty()) previous = LastChar(t);
        else if (Tok(j).text.empty()) break;
    }
    if (previous.empty()) previous = settings.before;
    // The text after the query, in the tokens that follow when the query ends its token.
    std::string following;
    for (int j = current + 1; j < TokenCount() && following.size() < 32; j++) following += Tok(j).text;
    const std::string_view preceding = !head.empty() || current <= 0 ? head : Tok(current - 1).text;
    std::vector<dict::Candidate>& list = found.list;
    for (const ConstraintRule& rule : kRules) {
        std::size_t i = 0;
        while (i < list.size()) {
            const std::string_view source = list[i].source;
            const bool first = std::none_of(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(i),
                                            [&](const dict::Candidate& c) { return c.source == source; });
            if (source.empty() || list[i].codes == nullptr || !first || !WordFits(rule, source, before) ||
                !ContextFits(rule, list, source,
                             query.size() > source.size() ? From(query, static_cast<long>(source.size()))
                                                          : std::string_view(following),
                             before, previous, preceding)) {
                i++;
                continue;
            }
            std::vector<std::size_t> same;
            for (std::size_t k = i; k < list.size(); k++) {
                if (list[k].source == source && list[k].codes != nullptr) same.push_back(k);
            }
            std::vector<std::size_t> rejected;
            const dict::Codes* nameCodes = nullptr;
            std::string_view nameTarget;
            int nameId = 0;
            std::vector<dict::Record> records;
            switch (rule.reject) {
            case ConstraintReject::NonPerson:
                if (rule.word == ConstraintWord::Hiragana) {
                    if (std::none_of(same.begin(), same.end(), [&](std::size_t k) {
                            return list[k].codes->part.Tag() == 'H';
                        })) {
                        break;
                    }
                    const bool verb = std::any_of(list.begin(), list.end(), [&](const dict::Candidate& c) {
                        if (c.codes == nullptr || c.codes->part.Tag() != 'M' || c.codes->partText.size() < 4 ||
                            c.source.size() < source.size()) {
                            return false;
                        }
                        int branch = 0, length = 0;
                        return AnalyseVerbTail(c.source, c.codes->partText[3], query.substr(c.source.size()), branch, length) &&
                               length > 0;
                    });
                    if (verb) break;
                }
                if (const dict::Record* r = PersonReading(snapshot, list, same, source, records)) {
                    nameCodes = r->codes->asPlain, nameTarget = r->target, nameId = r->id;
                    rejected = same;
                }
                break;
            case ConstraintReject::NonName:
            case ConstraintReject::NonNameBesidePlace:
                if (const auto name = NameAmong(rule, list, same, source); name != same.end()) {
                    nameCodes = list[*name].codes->asPlain, nameTarget = list[*name].target, nameId = list[*name].id;
                    rejected = same;
                }
                break;
            case ConstraintReject::Pronoun:
                for (const std::size_t k : same) {
                    const dict::PartCode p = list[k].codes->part;
                    if (p.Tag() == 'N' && p.ClassDigit() == '1' && p.Sub() == '2') rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            case ConstraintReject::NonAdverb:
                for (const std::size_t k : same) {
                    if (list[k].codes->part.Tag() != 'E') rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            case ConstraintReject::Adverb:
                for (const std::size_t k : same) {
                    if (list[k].codes->part.Tag() == 'E') rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            case ConstraintReject::OtherTargets:
                for (const std::size_t k : same) {
                    if (list[k].target != rule.target) rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            case ConstraintReject::Word: {
                // Only when a shorter source can take the word's place; otherwise every
                // sense would go and the rule would cancel itself.
                const bool shorter = std::any_of(list.begin(), list.end(), [&](const dict::Candidate& c) {
                    return c.source.size() < source.size() && source.starts_with(c.source);
                });
                if (shorter) rejected = same;
                break;
            }
            case ConstraintReject::NonQuote:
                for (const std::size_t k : same) {
                    const std::string_view t = list[k].target;
                    if (!Has(t, "라고") && !Has(t, "다고") && !Has(t, "라는") && !Has(t, "다는")) rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            }
            if (rejected.empty()) {
                i++;
                continue;
            }
            for (auto k = rejected.rbegin(); k != rejected.rend(); ++k) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(*k));
            }
            found.count -= static_cast<int>(rejected.size());
            if (nameCodes == nullptr) continue;  // list[i] is the next source now
            if (arena.namesUsed == arena.names.size()) arena.names.emplace_back();
            std::vector<dict::UserEntry>& kept = arena.names[arena.namesUsed++];
            kept.clear();
            dict::UserEntry& e = kept.emplace_back();
            e.key.assign(source);
            e.target.assign(nameTarget);
            e.codes = nameCodes;
            e.file = -2;
            e.line = nameId;
            e.name = true;
            e.nameLayer = true;
            dict::Candidate name;
            name.source = e.key;
            name.target = e.target;
            name.codes = e.codes;
            name.user = &e;
            list.insert(list.begin() + static_cast<std::ptrdiff_t>(rejected.front()), name);
            found.count++;
            i++;
        }
    }
}

}  // namespace hdTrans::word
