// The constraint rules and the pass that applies them to the candidates of a lookup.
#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/char_readings.h"
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
    {"honorific-kanji-name", ConstraintWord::Kanji, "", ConstraintContext::Honorific,
     ConstraintReject::NonName,
     "한자 단어 + 경칭: 사전에 인명 뜻이 있고 사람 명사 뜻이 없으면 인명 뜻만 (翼くん 날개 군 → 츠바사군, 東雲さん 새벽 씨 → 시노노메 씨)", {}},
    {"honorific-kanji-name-place", ConstraintWord::Kanji, "", ConstraintContext::Honorific,
     ConstraintReject::NonNameBesidePlace,
     "한자 단어 + 경칭: 인명 뜻 말고는 지명 뜻뿐이면 인명 뜻만 (太郎殿 타로전 → 타로님)", {}},
    {"kimi-after-katakana", ConstraintWord::Source, "君", ConstraintContext::AfterKatakana, ConstraintReject::Pronoun,
     "가타카나 바로 뒤의 君은 대명사(너·네)가 아니라 접미사 군 (バカ君が 바보네가 → 바보군이)", {}},
    {"kimito-after-mask", ConstraintWord::Source, "君と", ConstraintContext::AfterMask, ConstraintReject::OtherTargets,
     "가린 이름(○○, △△) 바로 뒤의 君と는 너와가 아니라 군과 (△△君と 너와 → △△군과)", "군과"},
    {"kimi-after-mask", ConstraintWord::Source, "君", ConstraintContext::AfterMask, ConstraintReject::Pronoun,
     "가린 이름(○○, △△) 바로 뒤의 君은 대명사가 아니라 접미사 군 (××君が 너가 → ××군이)", {}},
    {"quote-after-exclamation", ConstraintWord::Source, "と", ConstraintContext::AfterExclamation,
     ConstraintReject::NonQuote, "！·？ 바로 뒤나 ！っ 뒤의 と는 인용 조사 라고 (ドン！と 돈! 와 → 쾅! 라고)", {}},
    {"quote-comma-after-exclamation", ConstraintWord::Source, "と、", ConstraintContext::AfterExclamation,
     ConstraintReject::NonQuote, "！·？ 바로 뒤나 ！っ 뒤의 と、도 인용 (ドン！っと、 쾅! 와 → 쾅! 라고,)", {}},
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
    {"me-ordinal-without-number", ConstraintWord::Source, "め", ConstraintContext::NotAfterQuantity,
     ConstraintReject::Target, "수 없이 홀로 선 め는 서수 째가 아님 (化け物め 괴물째 → 괴물, ごめ 째). 三回め, 5人め는 그대로 째", "째"},
    {"me-yatsu-detached", ConstraintWord::Source, "め", ConstraintContext::Detached, ConstraintReject::Target,
     "띄어 쓰였거나 문장 부호 뒤에 홀로 선 め는 녀석이 아님 (名前: め 녀석 → 메)", "녀석"},
    {"wa-won-without-number", ConstraintWord::Source, "わ", ConstraintContext::NotAfterQuantity,
     ConstraintReject::Target, "수 없이 홀로 선 わ는 원이 아님 (だったわ 이었던 원, おわっ 원)", "원"},
    {"wa-after-predicate", ConstraintWord::Source, "わ", ConstraintContext::AfterPredicate, ConstraintReject::Target,
     "술어 바로 뒤 말끝의 わ는 감탄사 와가 아님 (だったわね 이었던 와군요 → 이었군요, 知らねーわ). うわ, おわ는 그대로 와", "와"},
    {"n-after-verb", ConstraintWord::Source, "ん", ConstraintContext::AfterVerbEnding, ConstraintReject::Target,
     "동사 た·だ·る 바로 뒤의 ん은 감탄사 응이 아님 (終わったん？ 끝난 응? → 끝난?)", "응"},
    {"ita-after-te", ConstraintWord::Source, "いた", ConstraintContext::AfterTeNotAdjective, ConstraintReject::Word,
     "て·で 바로 뒤의 いた는 형용사 어미가 붙지 않으면 痛い·傷む가 아니라 いる의 과거 (捨てられていたんです 버려져 손상되어 → 버려져 있었습니다)", {}},
    {"kotoshi-suru", ConstraintWord::Source, "ことし", ConstraintContext::BeforeSuruForm, ConstraintReject::Word,
     "する의 활용이 이어지는 ことし는 今年이 아니라 こと + する (エッチなことしないで 야한 금년 없고 → 야한 짓 하지 마)", {}},
    {"o-stem-nasai", ConstraintWord::OStem, "", ConstraintContext::BeforeNasai, ConstraintReject::Word,
     "なさい 앞의 お와 동사 연용형은 명사가 아니라 お + 동사의 명령 (お待ちなさい 대기 없음 있고 → 기다려)", {}},
    {"itadake-dake", ConstraintWord::Source, "いただけ", ConstraintContext::BeforeCopula, ConstraintReject::Word,
     "だ, で, じゃ, に, の, か 앞의 いただけ는 いただける가 아니라 いた + だけ (待っていただけだ 기다려 받을 수 있고 → 기다렸을 뿐이다)", {}},
    {"sou-itta-predicate", ConstraintWord::Source, "そう言った", ConstraintContext::NotBeforeNoun,
     ConstraintReject::Word, "명사 앞이 아닌 そう言った는 연체사 그런이 아니라 そう言う의 과거 (そう言った。 그런. → 그렇게 말했다.)", {}},
    {"toiu-kotode-opening", ConstraintWord::Source, "とい", ConstraintContext::OpeningKotode, ConstraintReject::Word,
     "줄 첫머리의 ということで、는 동사 という + ことで가 아니라 연결 표현 그래서 (그렇다고 하는 것으로, → 그래서,)", {}},
    {"to-iu-kotode-opening", ConstraintWord::Source, "と言", ConstraintContext::OpeningKotode, ConstraintReject::Word,
     "줄 첫머리의 と言うことで、는 동사 と言う + ことで가 아니라 연결 표현 그래서 (이렇게 말하는 것으로, → 그래서,)", {}},
    {"de-before-aru", ConstraintWord::EndsInDe, "", ConstraintContext::BeforeAru, ConstraintReject::Word,
     "ある의 활용 앞에서 で로 끝나는 단어는 쓰지 않고 で를 계사 である에 남김 (十四歳であり 14세에 있어 → 14세이며)", {}},
    {"amari-before-predicate", ConstraintWord::Source, "余り", ConstraintContext::BeforeStem, ConstraintReject::Verbs,
     "술어가 바로 이어지는 余り는 余る의 연용형이 아니라 부사 (余り伸びなかった 남아 자라지 → 별로 자라지). 余りの, 余りに는 그대로", {}},
    {"amari-adverb", ConstraintWord::Source, "余り", ConstraintContext::BeforeStem, ConstraintReject::OtherTargets,
     "술어가 바로 이어지는 余り는 나머지가 아니라 별로 (余り好きじゃない 나머지를 → 별로)", "별로"},
    {"wake-formal-noun", ConstraintWord::Source, "わけ", ConstraintContext::NounSlot, ConstraintReject::Verbs,
     "문장 부호, 조사, 계사, ある·ない가 뒤에 오는 わけ는 동사 沸く·分ける가 아니라 형식 명사 (なわけ。 끓어라 → 것, わけあって 서로 나누어 → 사정이 있어). わけて, 二つにわけ는 그대로", {}},
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

// The characters that mask a name (○○君, △△さん).
constexpr std::u32string_view kMasks = U"○●◯〇◎×✕＊*■□△▲◇◆";

// The last characters of a predicate that a sentence-final particle follows; え, ー and
// the like end a drawn-out slang predicate (痛えわ, 知らねーわ).
constexpr std::string_view kPredicateEnds[] = {"た", "だ", "る", "う", "い", "す", "ん", "え", "ぇ", "ー", "エ", "ェ"};

// What continues a word rather than follows a particle (とって, ととと, とぉぉ).
constexpr std::string_view kDrawls[] = {"っ", "と", "ぉ", "ぁ", "ー", "～", "↓", "↑"};
// The rest of a form of する after its stem し (しない, してる, しちゃう, しよう), not the
// first kana of まで, なら or たくさん after 今年.
constexpr std::string_view kSuruTails[] = {"ない", "なく", "なかっ", "なけ", "なきゃ", "なくちゃ", "なさ", "ねえ",
                                           "ねー", "ます", "ませ", "まし", "て", "ちゃ", "ちま", "たら",
                                           "たり", "たい", "たく", "よう", "つつ", "ながら"};
// What follows だけ and no verb stem (いただけだ, いただけで, いただけじゃ, いただけに).
constexpr std::string_view kCopulaStarts[] = {"だ", "で", "じゃ", "に", "の", "か"};
// What follows いた as an adjective ending (見ていたい) or as 致す and 頂く (でいたします,
// ていただく).
constexpr std::string_view kAdjectiveOrHumbleTails[] = {"い", "か", "く", "け", "し", "だ"};
// What follows a noun and never a verb stem: copulas, particles, ある (わけだ, わけが, わけあって),
// and what also follows the stem of a verb in its own use (わけ、, わけない 分けない, わけは).
constexpr std::string_view kNounFollowers[] = {"だ", "で", "じゃ", "が", "の", "ね", "か", "や", "って",
                                               "なん", "なの", "なら", "あっ", "あり", "ある", "さ", "よ"};
constexpr std::string_view kStemFollowers[] = {"ない", "無い", "なく", "なかっ", "は", "も", "に"};
// The polite ending and the auxiliaries and suffixes that a verb's continuative runs into as
// one verb form (余ります, 余り過ぎる, 余り始める, 余りがち, 余りそうだ).
constexpr std::string_view kContinuativeSuffixes[] = {"ます", "まし", "ませ", "過ぎ", "すぎ", "始め", "はじめ",
                                                      "続け", "つづけ", "がち", "気味", "つつ", "そうだ",
                                                      "そうな", "そうです"};
// The forms of ある after the で of である (であり, である, であった, であれば, であろう).
constexpr std::string_view kAruForms[] = {"ある", "あり", "あっ", "あれ", "あろ"};
// The case particles after which a verb stem in kana goes on (二つにわけ、, パンをわけない).
constexpr std::string_view kObjectParticles[] = {"を", "に", "へ", "で"};
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
    if (s.starts_with("くんだり")) return false;  // 千里くんだり: all the way to a place
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
    case ConstraintWord::EndsInDe:
        return text::RuneCount(source) >= 2 && source.ends_with("で");
    case ConstraintWord::OStem: {
        // The greetings お帰りなさい and お休みなさい are no orders.
        constexpr std::string_view kGreetings[] = {"お帰り", "おかえり", "お休み", "おやすみ"};
        return source.starts_with("お") && text::RuneCount(source) >= 3 &&
               NameClassOf(source.substr(source.size() - std::string_view("い").size())) == kClassHiragana &&
               std::find(std::begin(kGreetings), std::end(kGreetings), source) == std::end(kGreetings);
    }
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

// Whether one of the last three characters of s is a numeral.
bool EndsInQuantity(std::string_view s) {
    for (int n = 0; n < 3 && !s.empty(); n++) {
        const text::Rune r = DecodeLastRune(s);
        if (kNumerals.find(r.cp) != std::u32string_view::npos) return true;
        s.remove_suffix(r.size);
    }
    return false;
}

bool StartsWithAny(std::string_view s, std::span<const std::string_view> list) {
    return std::any_of(list.begin(), list.end(), [s](std::string_view p) { return s.starts_with(p); });
}

// Whether the context of rule holds for the word source of the candidates list: after is
// the text after it, before the character before it in its token, previous the character
// before it past spaces and token ends, adjacent the character right before it (in its
// token or the token before), preceding the text right before it (its token's head, else
// the token before), lead the token before and the head.
bool ContextFits(const ConstraintRule& rule, const std::vector<dict::Candidate>& list, std::string_view source,
                 std::string_view after, std::string_view before, std::string_view previous,
                 std::string_view adjacent, std::string_view preceding, std::string_view lead) {
    const bool exclamation = previous == "！" || previous == "？" || previous == "!" || previous == "?" ||
                             (previous == "っ" && (lead.ends_with("！っ") || lead.ends_with("!っ")));
    switch (rule.context) {
    case ConstraintContext::Honorific:
    case ConstraintContext::FamiliarHonorific:
        return HonorificAt(after, rule.context == ConstraintContext::FamiliarHonorific) &&
               (rule.word != ConstraintWord::Hiragana || HiraganaHonorificAt(after)) &&
               std::none_of(list.begin(), list.end(), [&](const dict::Candidate& c) { return c.source.size() > source.size(); });
    case ConstraintContext::AfterKatakana:
        return NameClassOf(before) == kClassKatakana;
    case ConstraintContext::AfterMask:
        return !previous.empty() && kMasks.find(text::DecodeRune(previous).cp) != std::u32string_view::npos;
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
    case ConstraintContext::BeforeNasai:
        return after.starts_with("なさい");
    case ConstraintContext::BeforeCopula:
        return StartsWithAny(after, kCopulaStarts);
    case ConstraintContext::AfterPredicate:
        return std::any_of(std::begin(kPredicateEnds), std::end(kPredicateEnds),
                           [&](std::string_view e) { return lead.ends_with(e); });
    case ConstraintContext::NotAfterQuantity:
        return !EndsInQuantity(lead);
    case ConstraintContext::AfterTeNotAdjective:
        return (preceding.ends_with("て") || preceding.ends_with("で")) &&
               !StartsWithAny(after, kAdjectiveOrHumbleTails);
    case ConstraintContext::NotBeforeNoun: {
        const int next = NameClassOf(FirstChar(after));
        return next != kClassKanji && next != kClassKatakana;
    }
    case ConstraintContext::BeforeStem: {
        const int next = NameClassOf(FirstChar(after));
        const int last = NameClassOf(before);
        if (last == kClassKanji || last == kClassKatakana) return false;
        return (next == kClassKanji || (next == kClassHiragana && !StartsWithAny(after, kNounFollowers) &&
                                       !StartsWithAny(after, kStemFollowers) && !StartsWithAny(after, kObjectParticles))) &&
               !StartsWithAny(after, kContinuativeSuffixes);
    }
    case ConstraintContext::BeforeSuruForm:
        // した, but not たくさん: the past ends there or a particle or mark follows it.
        if (after.starts_with("たくさん")) return false;
        if (after.starts_with("た") && !after.starts_with("たく")) {
            const std::string_view rest = after.substr(std::string_view("た").size());
            const char32_t next = rest.empty() ? 0 : text::DecodeRune(rest).cp;
            return next == 0 || !(next >= U'ぁ' && next <= U'ゖ') || std::u32string_view(U"のんよねかっけしわぞぜ").find(next) != std::u32string_view::npos;
        }
        return StartsWithAny(after, kSuruTails);
    case ConstraintContext::BeforeAru:
        // A noun fused with で (歳で), not a particle in で (まである).
        return StartsWithAny(after, kAruForms) &&
               std::none_of(list.begin(), list.end(), [&](const dict::Candidate& c) {
                   return c.source == source && c.codes != nullptr && c.codes->part.Tag() != 'N';
               });
    case ConstraintContext::OpeningKotode:
        return (previous.empty() || previous == "…" || previous == "‥" || StartsWithAny(previous, kOpeningBrackets)) &&
               (after.starts_with("うことで、") || after.starts_with("うことで，"));
    case ConstraintContext::NounSlot: {
        if (after.starts_with("よう") || after.starts_with("させ") || after.starts_with("され")) return false;
        if (StartsWithAny(after, kNounFollowers)) return true;
        const int next = NameClassOf(FirstChar(after));
        return (StartsWithAny(after, kStemFollowers) || (next != kClassHiragana && next != kClassKanji)) &&
               !StartsWithAny(adjacent, kObjectParticles);
    }
    case ConstraintContext::Detached:
        return adjacent.empty() || adjacent == " " || adjacent == "　" || adjacent == ":" || adjacent == "：" ||
               kQuantityBreaks.find(text::DecodeRune(adjacent).cp) != std::u32string_view::npos;
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

// Whether target, a name reading of the kanji word source of two kanji or more that also has
// a sense of no name or place among same, is a Korean word rather than a reading: the Korean
// of such a sense (太陽 태양), or a word that starts with the Sino-Korean reading of the
// first two kanji (情報屋 정보제공자). A name of no such word reads as it is (摩耶 마야).
bool TranslatedName(const std::vector<dict::Candidate>& list, const std::vector<std::size_t>& same,
                    std::string_view source, std::string_view target) {
    if (text::RuneCount(source) < 2) return false;
    bool word = false;
    for (const std::size_t k : same) {
        const dict::Codes& c = *list[k].codes;
        if (NameSense(c) || c.sem.HasCode("QCT") || c.sem.HasCode("VPl")) continue;
        if (list[k].target == target) return true;
        word = true;
    }
    if (!word) return false;
    std::string sino;
    for (int n = 0; n < 2; n++) {
        const text::Rune r = text::DecodeRune(source);
        const std::optional<char32_t> written = dict::Written(r.cp);
        if (!written) return false;
        text::AppendRune(sino, *written);
        source.remove_prefix(r.size);
    }
    return target.starts_with(sino);
}

// The name sense among same that replaces the others, or same.end() when there is none or
// a person noun, a verb or adjective as long as source, or (beside places) another sense
// than a place is among the candidates.
std::vector<std::size_t>::const_iterator NameAmong(const ConstraintRule& rule, const std::vector<dict::Candidate>& list,
                                                    const std::vector<std::size_t>& same, std::string_view source) {
    auto codes = [&](std::size_t k) -> const dict::Codes& { return *list[k].codes; };
    const auto name = std::find_if(same.begin(), same.end(), [&](std::size_t k) {
        return NameSense(codes(k)) && list[k].user == nullptr && HangulText(list[k].target) &&
               !TranslatedName(list, same, source, list[k].target);
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
    const std::string_view adjacent = !head.empty() ? before : LastChar(current > 0 ? preceding : settings.before);
    std::string lead(current > 0 ? Tok(current - 1).text : std::string_view());
    lead += head;
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
                             before, previous, adjacent, preceding, lead)) {
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
                } else if (rule.reject == ConstraintReject::NonName) {
                    // No name sense among the candidates: the name layer's person reading of
                    // the word (千里ちゃん 치사토짱, not 천리), unless a person noun, a user
                    // entry or a verb or adjective as long reads it.
                    const bool blocked =
                        std::any_of(list.begin(), list.end(), [&](const dict::Candidate& c) {
                            return c.codes != nullptr && AnyOf("478", c.codes->part.ClassDigit()) && c.source.size() >= source.size();
                        }) ||
                        std::any_of(same.begin(), same.end(), [&](std::size_t k) {
                            return PersonNoun(*list[k].codes) || PersonSense(*list[k].codes) || list[k].user != nullptr;
                        });
                    if (blocked) break;
                    records = snapshot.NameRecords(source);
                    for (const dict::Record& r : records) {
                        if (r.codes == nullptr || !r.codes->sem.HasPrefix("QPN") || !HangulText(r.target) ||
                            TranslatedName(list, same, source, r.target)) {
                            continue;
                        }
                        nameCodes = r.codes->asPlain, nameTarget = r.target, nameId = r.id;
                        rejected = same;
                        break;
                    }
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
            case ConstraintReject::Target:
                for (const std::size_t k : same) {
                    if (list[k].target == rule.target) rejected.push_back(k);
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
            case ConstraintReject::Verbs: {
                auto verb = [&](std::size_t k) { return list[k].codes->part.Tag() == 'M'; };
                if (std::all_of(same.begin(), same.end(), verb)) break;
                for (std::size_t k = 0; k < list.size(); k++) {
                    if (list[k].codes != nullptr && verb(k) && source.starts_with(list[k].source)) rejected.push_back(k);
                }
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
