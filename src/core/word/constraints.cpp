// The constraint rules and the pass that applies them to the candidates of a lookup.
#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/char_readings.h"
#include "core/dict/classes.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/unicode.h"
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
    {"cued-name", ConstraintWord::Cued, "", ConstraintContext::NotCommonSense, ConstraintReject::NonCuedName,
     "줄이나 문서의 단서(함종 뒤, 자매 호칭과 개조와 급 앞, 화자 표시, 자기소개)가 이름이라 한 단어는 인명 뜻만, 날씨나 하늘이나 바다 뜻을 살리는 서술어 앞은 제외 (駆逐艦村雨 구축함 소나기 → 무라사메, 時雨が降る는 그대로)", {}},
    {"chan-after-word", ConstraintWord::WithKanji, "", ConstraintContext::BeforeChan, ConstraintReject::Word,
     "ゃん 바로 앞에서 ち로 끝나는 한자 단어는 쓰지 않음: 그 ち는 ちゃん의 첫 글자 (夕立ちゃん 소나기 → 夕立 + ちゃん)", {}},
    {"chau-after-verb", ConstraintWord::KansaiNegative, "", ConstraintContext::NotAfterNoun, ConstraintReject::Word,
     "명사 뒤가 아닌 ちゃう, ちゃいます(관서 말 아니다)는 쓰지 않음: 동사 뒤의 ちゃう, ちゃい는 てしまう (やっちゃうね 해 버리네; マザコンちゃうわ 마더 콤플렉스 아니거든은 그대로)", {}},
    {"chau-before-kana", ConstraintWord::Source, "ちゃう", ConstraintContext::BeforeHiragana, ConstraintReject::Word,
     "히라가나가 바로 뒤따르는 ちゃう는 아니다가 아님: 그 ちゃう는 동사 활용의 일부 (肌荒れちゃうね 피부가 거칠어져 버리네)", {}},
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
    {"ku-before-rai", ConstraintWord::EndsInKu, "", ConstraintContext::BeforeRai, ConstraintReject::Word,
     "らい 앞에서 く로 끝나는 단어는 くらい를 끊음: 짧은 단어와 くらい로 (したくらい 준비등 → 한 정도, きたくらい 귀가등 → 온 정도)", {}},
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
    {"iru-form-after-te", ConstraintWord::IruForm, "", ConstraintContext::AfterTe, ConstraintReject::Word,
     "て, で 바로 뒤에서 いる의 활용형과 같은 글자로 시작하는 낱말은 그 낱말이 아니라 いる의 활용 (固まっていろよ 굳어져 색이야 → 굳어져 있어라, 何していようかな 뭐해 이상한가 → 뭐하고 있을까)", {}},
    {"iru-past-after-te", ConstraintWord::Source, "い", ConstraintContext::AfterTeBeforeTa, ConstraintReject::NonVerbs,
     "て, で 바로 뒤에서 た가 이어지는 い는 いい의 어간이 아니라 いる (待っていたって 기다려도 좋아도 → 기다리고 있어도)", {}},
    {"hashi-before-te", ConstraintWord::Source, "はし", ConstraintContext::AfterWordBeforeTe, ConstraintReject::Word,
     "앞에 말이 있고 て가 바로 이어지는 はし는 箸나 橋가 아니라 は와 する의 て형 (感じはしていた 느낌 젓가락이라고 있었다 → 느낌은 하고 있었다)", {}},
    {"teiru-before-irassharu", ConstraintWord::EndsInSource, "てい", ConstraintContext::BeforeRassharu, ConstraintReject::Word,
     "らっしゃ 앞에서 てい로 끝나는 말(座ってい 앉아 있다)은 ている가 아니라 て형과 いらっしゃる (座っていらっしゃいます 앉아 있고 있습니다 → 앉아 계십니다)", {}},
    {"deiru-before-irassharu", ConstraintWord::EndsInSource, "でい", ConstraintContext::BeforeRassharu, ConstraintReject::Word,
     "らっしゃ 앞에서 でい로 끝나는 말도 같다 (読んでいらっしゃる)", {}},
    {"tanto-after-tsu", ConstraintWord::Source, "たんと", ConstraintContext::AfterSmallTsu, ConstraintReject::Word,
     "っ 바로 뒤의 たんと는 부사 듬뿍이 아니라 과거 た + んと (行ったんと違う 간 듬뿍 다르다)", {}},
    {"kotoshi-suru", ConstraintWord::Source, "ことし", ConstraintContext::BeforeSuruForm, ConstraintReject::Word,
     "する의 활용이 이어지는 ことし는 今年이 아니라 こと + する (エッチなことしないで 야한 금년 없고 → 야한 짓 하지 마)", {}},
    {"o-stem-nasai", ConstraintWord::OStem, "", ConstraintContext::BeforeNasai, ConstraintReject::Word,
     "なさい 앞의 お와 동사 연용형은 명사가 아니라 お + 동사의 명령 (お待ちなさい 대기 없음 있고 → 기다려)", {}},
    {"itadake-dake", ConstraintWord::EndsInSource, "いただけ", ConstraintContext::BeforeCopula, ConstraintReject::Word,
     "だ, で, じゃ, に, の, か 앞에서 いただけ로 끝나는 말은 いただける가 아니라 いた + だけ (待っていただけだ 기다려 받을 수 있고 → 기다렸을 뿐이다, 油断していただけだから 방심해 주실 수 있고니까 → 방심하고 있었을 뿐이니까)", {}},
    {"itadaku-ke-after-te", ConstraintWord::Source, "いただ", ConstraintContext::AfterTeBeforeDake, ConstraintReject::Word,
     "て, で 바로 뒤에서 けだ, け〜처럼 だけ로 읽히는 け 앞의 いただ는 いただく의 명령이 아니라 いた + だけ (フリしていただけ〜 척해 주셔라 → 척하고 있었을 뿐)", {}},
    {"teitadaku-ke", ConstraintWord::EndsInSource, "ていただ", ConstraintContext::BeforeDake, ConstraintReject::Word,
     "그런 け 앞에서 ていただ로 끝나는 말도 같다 (していただけ。 해 주셔라 → 하고 있었을 뿐)", {}},
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

bool AllOfClass(std::string_view s, int cls) {
    if (s.empty()) return false;
    for (std::string_view t = s; !t.empty();) {
        const std::string_view c = FirstChar(t);
        if (c.empty() || NameClassOf(c) != cls) return false;
        t.remove_prefix(c.size());
    }
    return true;
}

bool HasKanji(std::string_view s) {
    for (std::string_view t = s; !t.empty();) {
        const std::string_view c = FirstChar(t);
        if (c.empty()) return false;
        if (NameClassOf(c) == kClassKanji) return true;
        t.remove_prefix(c.size());
    }
    return false;
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
    // っちゃ is the contracted てしまう of a verb before it (ビビっちゃった), not the suffix っち.
    if (s.starts_with("っちゃ") && !s.starts_with("っちゃん")) return false;
    const auto at = [&](std::string_view h) { return s.starts_with(h); };
    if (familiarOnly) return std::ranges::any_of(dict::ClassMembers("honorific.constraint-familiar"), at);
    return std::ranges::any_of(dict::ClassMembers("honorific.constraint"), at);
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

// Whether c is a verb or adjective as long as source, or longer, that reads the word
// rather than a name before an honorific: not an adjective stem before くん that a particle,
// a break or the end follows (碧くんが: not the 碧く of 碧い).
bool PredicateBlocks(const dict::Candidate& c, std::string_view source, std::string_view after) {
    if (c.codes == nullptr || !AnyOf("478", c.codes->part.ClassDigit()) || c.source.size() < source.size()) return false;
    return !(c.codes->part.Tag() == 'H' && c.source.size() == source.size() && after.starts_with("くん") &&
             HiraganaHonorificAt(after));
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
bool WordFits(const ConstraintRule& rule, std::string_view source, std::string_view before, const dict::KeySet* cued) {
    switch (rule.word) {
    case ConstraintWord::Cued:
        return cued != nullptr && cued->find(source) != cued->end();
    case ConstraintWord::WithKanji:
        return text::RuneCount(source) >= 2 && HasKanji(source);
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
    case ConstraintWord::EndsInSource:
        return source.ends_with(rule.source);
    case ConstraintWord::EndsInDe:
        return text::RuneCount(source) >= 2 && source.ends_with("で");
    case ConstraintWord::OStem: {
        return source.starts_with("お") && text::RuneCount(source) >= 3 &&
               NameClassOf(source.substr(source.size() - std::string_view("い").size())) == kClassHiragana &&
               !dict::InClass("word.greeting-o", source);
    }
    case ConstraintWord::EndsInKu:
        return text::RuneCount(source) >= 2 && source.ends_with("く");
    case ConstraintWord::IruForm:
        return dict::InClass("word.iru-form", source);
    case ConstraintWord::KansaiNegative:
        return dict::InClass("copula.kansai-negative", source);
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

// Whether s is what may follow the だけ of いただけ: a member of copula.after-dake, or no letter
// (いただけだ, いただけなんです, いただけ〜).
bool DakeEnds(std::string_view s) {
    return s.empty() || !text::IsLetter(text::DecodeRune(s).cp) || StartsWithAny(s, dict::ClassMembers("copula.after-dake"));
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
               StartsWithAny(after, dict::ClassMembers("noun.quoted"));
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
    case ConstraintContext::AfterTeBeforeTa:
        return (preceding.ends_with("て") || preceding.ends_with("で")) && after.starts_with("た");
    case ConstraintContext::AfterTeBeforeDake:
        return (preceding.ends_with("て") || preceding.ends_with("で")) && after.starts_with("け") &&
               DakeEnds(after.substr(std::string_view("け").size()));
    case ConstraintContext::BeforeDake:
        return after.starts_with("け") && DakeEnds(after.substr(std::string_view("け").size()));
    case ConstraintContext::AfterWordBeforeTe:
        // The word before may be Korean already and spaced off (ソワソワ written 안절부절 by the sound stage).
        return after.starts_with("て") &&
               (NameClassOf(adjacent) != kClassOther || HangulText(adjacent) || adjacent == " ");
    case ConstraintContext::BeforeRassharu:
        return after.starts_with("らっしゃ");
    case ConstraintContext::BeforeNasai:
        return after.starts_with("なさい");
    case ConstraintContext::BeforeCopula:
        return DakeEnds(after);
    case ConstraintContext::NotCommonSense:
        return !StartsWithAny(after, dict::ClassMembers("name.common-after")) && !HonorificAt(after, false);
    case ConstraintContext::NotAfterNoun:  // Worker::NotAfterNoun, which looks the word before up
        return false;
    case ConstraintContext::BeforeHiragana:
        return NameClassOf(FirstChar(after)) == kClassHiragana;
    case ConstraintContext::BeforeChan:
        return source.ends_with("ち") && after.starts_with("ゃん") && !after.starts_with("ゃんと");
    case ConstraintContext::BeforeRai:
        return after.starts_with("らい");
    case ConstraintContext::AfterPredicate:
        return std::any_of(std::begin(kPredicateEnds), std::end(kPredicateEnds),
                           [&](std::string_view e) { return lead.ends_with(e); });
    case ConstraintContext::NotAfterQuantity:
        return !EndsInQuantity(lead);
    case ConstraintContext::AfterSmallTsu:
        return preceding.ends_with("っ") || preceding.ends_with("ッ");
    case ConstraintContext::AfterTeNotAdjective:
        return (preceding.ends_with("て") || preceding.ends_with("で")) &&
               !StartsWithAny(after, dict::ClassMembers("follower.ita"));
    case ConstraintContext::AfterTe:
        return preceding.ends_with("て") || preceding.ends_with("で");
    case ConstraintContext::NotBeforeNoun: {
        const int next = NameClassOf(FirstChar(after));
        return next != kClassKanji && next != kClassKatakana;
    }
    case ConstraintContext::BeforeStem: {
        const int next = NameClassOf(FirstChar(after));
        const int last = NameClassOf(before);
        if (last == kClassKanji || last == kClassKatakana) return false;
        return (next == kClassKanji ||
                (next == kClassHiragana && !StartsWithAny(after, dict::ClassMembers("follower.noun-not-stem")) &&
                 !StartsWithAny(after, dict::ClassMembers("follower.stem")) &&
                 !StartsWithAny(after, dict::ClassMembers("particle.case-before-stem")))) &&
               !StartsWithAny(after, dict::ClassMembers("suffix.continuative"));
    }
    case ConstraintContext::BeforeSuruForm:
        // した, but not たくさん: the past ends there or a particle or mark follows it.
        if (after.starts_with("たくさん")) return false;
        if (after.starts_with("た") && !after.starts_with("たく")) {
            const std::string_view rest = after.substr(std::string_view("た").size());
            const char32_t next = rest.empty() ? 0 : text::DecodeRune(rest).cp;
            return next == 0 || !(next >= U'ぁ' && next <= U'ゖ') || std::u32string_view(U"のんよねかっけしわぞぜ").find(next) != std::u32string_view::npos;
        }
        return StartsWithAny(after, dict::ClassMembers("suru.tail"));
    case ConstraintContext::BeforeAru:
        // A noun fused with で (歳で), not a particle in で (まである).
        return StartsWithAny(after, dict::ClassMembers("aru.after-de")) &&
               std::none_of(list.begin(), list.end(), [&](const dict::Candidate& c) {
                   return c.source == source && c.codes != nullptr && c.codes->part.Tag() != 'N';
               });
    case ConstraintContext::OpeningKotode:
        return (previous.empty() || previous == "…" || previous == "‥" || StartsWithAny(previous, kOpeningBrackets)) &&
               (after.starts_with("うことで、") || after.starts_with("うことで，"));
    case ConstraintContext::NounSlot: {
        if (after.starts_with("よう") || after.starts_with("させ") || after.starts_with("され")) return false;
        if (StartsWithAny(after, dict::ClassMembers("follower.noun-not-stem"))) return true;
        const int next = NameClassOf(FirstChar(after));
        return (StartsWithAny(after, dict::ClassMembers("follower.stem")) ||
                (next != kClassHiragana && next != kClassKanji)) &&
               !StartsWithAny(adjacent, dict::ClassMembers("particle.case-before-stem"));
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
                                                    const std::vector<std::size_t>& same, std::string_view source,
                                                    std::string_view after) {
    auto codes = [&](std::size_t k) -> const dict::Codes& { return *list[k].codes; };
    const auto name = std::find_if(same.begin(), same.end(), [&](std::size_t k) {
        return NameSense(codes(k)) && list[k].user == nullptr && HangulText(list[k].target) &&
               !TranslatedName(list, same, source, list[k].target);
    });
    const bool blocked =
        std::any_of(list.begin(), list.end(), [&](const dict::Candidate& c) { return PredicateBlocks(c, source, after); }) ||
        std::any_of(same.begin(), same.end(), [&](std::size_t k) { return PersonNoun(codes(k)) || list[k].user != nullptr; }) ||
        std::all_of(same.begin(), same.end(), [&](std::size_t k) { return NameSense(codes(k)); }) ||
        (rule.reject == ConstraintReject::NonNameBesidePlace && !std::all_of(same.begin(), same.end(), [&](std::size_t k) {
             return NameSense(codes(k)) || codes(k).sem.HasCode("QCT") || codes(k).sem.HasCode("QNN");
         }));
    return blocked ? same.end() : name;
}


// Whether a predicate as long as a word goes on inflecting after it: hiragana follows that
// starts no particle (but さ, which also inflects: 許さん), copula or self-introduction, and
// for an adjectival noun no honorific (朧な, 許さん; but 朧です, 朧は, 朧ちゃん).
bool InflectsAfter(std::string_view after, bool adjectival) {
    if (NameClassOf(FirstChar(after)) != kClassHiragana) return false;
    if (adjectival && StartsWithAny(after, dict::ClassMembers("honorific.name"))) return false;
    if (StartsWithAny(after, dict::ClassMembers("name.introduction-copula"))) return false;
    for (const std::string_view p : dict::ClassMembers("particle.after-name")) {
        if (p != "さ" && after.starts_with(p)) return false;
    }
    return !after.starts_with("だ") && !after.starts_with("って");
}

// Whether a and b spell one name but for the initial of the first syllable (기리시마 키리시마,
// 가가 카가), the convention of a word-initial kana; not 오이 오오이 or 이라고 이라코.
bool SpellingVariant(std::string_view a, std::string_view b) {
    if (a == b) return true;
    if (a.empty() || b.empty() || !text::SameSound(a, b)) return false;
    return a.substr(text::DecodeRune(a).size) == b.substr(text::DecodeRune(b).size);
}

struct CuedReading {
    const dict::Codes* codes = nullptr;
    std::string_view target;
    int id = 0;
};

// The name a cue gives the word source (ConstraintReject::NonCuedName); none when a person
// noun, a user entry or a verb or adjective that inflects past the word reads it.
CuedReading CuedName(const dict::Snapshot& snapshot, const std::vector<dict::Candidate>& list,
                     const std::vector<std::size_t>& same, std::string_view source, std::string_view after,
                     std::vector<dict::Record>& records) {
    const bool blocked =
        std::any_of(list.begin(), list.end(), [&](const dict::Candidate& c) {
            return c.codes != nullptr && AnyOf("478", c.codes->part.ClassDigit()) &&
                   (c.source.size() > source.size() ||
                    (c.source.size() == source.size() && InflectsAfter(after, c.codes->part.ClassDigit() == '8')));
        }) ||
        std::any_of(same.begin(), same.end(), [&](std::size_t k) {
            return PersonNoun(*list[k].codes) || (list[k].user != nullptr && !list[k].user->nameLayer);
        });
    if (blocked) return {};
    const bool kana = AllOfClass(source, kClassKatakana) || AllOfClass(source, kClassHiragana);
    const std::string reading = kana ? text::FromKana(source, text::Preset::Fan) : std::string();
    records = snapshot.NameRecords(source);
    // A kana word the dictionary already reads as a person, as one of the name's own spellings
    // or by its sound (エル 엘, フラン 프랑), is read that way; the cue replaces only words of
    // another meaning (アヤメ 붓꽃).
    if (kana && (same.empty() || std::any_of(same.begin(), same.end(), [&](std::size_t k) {
            return (PersonSense(*list[k].codes) && list[k].user == nullptr) || text::SameSound(list[k].target, reading) ||
                   std::any_of(records.begin(), records.end(), [&](const dict::Record& r) {
                       return r.codes != nullptr && r.codes->sem.HasPrefix("QPN") && r.target == list[k].target;
                   });
        }))) {
        return {};
    }
    const auto fits = [&](const dict::Codes* codes, std::string_view target) {
        return codes != nullptr && codes->sem.HasPrefix("QPN") && !TranslatedName(list, same, source, target) &&
               (kana ? text::SameSound(target, reading) : text::JapaneseSpelling(target));
    };
    // A person sense among the candidates comes first (佐渡 사도); then a person reading of the name
    // layer that sounds like the dictionary's first place name of the key (大井 오오이, 暁
    // 아카츠키). A word the dictionary reads first as a place that no person reading sounds like
    // reads as that proper noun already (鳥海 초카이, 富山 도야마).
    CuedReading found;
    for (const std::size_t k : same) {
        if (fits(list[k].codes, list[k].target)) {
            found = {list[k].codes->asPlain, list[k].target, list[k].id};
            break;
        }
    }
    std::string_view place;
    bool placeFirst = false;
    bool firstRecord = true;
    for (const dict::Candidate& c : snapshot.RetrieveRecords(source, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != source) continue;
        const bool named = c.codes->sem.HasCode("QCT") || c.codes->sem.HasCode("QNN");
        if (firstRecord) placeFirst = named;
        firstRecord = false;
        if (named) {
            place = c.target;
            break;
        }
    }
    if (found.codes == nullptr && !place.empty()) {
        for (const dict::Record& r : records) {
            if (fits(r.codes, r.target) && text::SameSound(r.target, place)) {
                found = {r.codes->asPlain, r.target, r.id};
                break;
            }
        }
    }
    if (found.codes == nullptr && !placeFirst) {
        for (const dict::Record& r : records) {
            if (fits(r.codes, r.target)) {
                found = {r.codes->asPlain, r.target, r.id};
                break;
            }
        }
    }
    // A name the default candidate already writes, or writes with another initial of its first
    // syllable, is left as the dictionary spells it (「天龍」 텐류, 霧島 기리시마), but for a name
    // before a class, which takes the name's codes (長門型 나가토급).
    if (found.codes == nullptr) return found;
    std::size_t first = same.front();
    for (const std::size_t k : same) {
        if (list[k].codes->sem.HasCode("XXX")) {
            first = k;
            break;
        }
    }
    if (SpellingVariant(list[first].target, found.target)) {
        if (!StartsWithAny(after, dict::ClassMembers("name.class-after"))) return {};
        found.target = list[first].target;
    }
    return found;
}

}  // namespace

std::span<const ConstraintRule> ConstraintRules() { return kRules; }

bool Worker::NotAfterNoun(std::string_view head) {
    if (head.empty()) return false;
    const std::string_view last = LastChar(head);
    // The nominalizer ん takes it too (行くんちゃう 가는 거 아니야).
    if (last == "ん") return false;
    if (last == "て" || last == "で" || last == "っ" || last == "ッ") return true;
    // The longest dictionary word of up to eight characters with a kanji or a katakana that ends
    // where head ends, by its first sense as the dictionary orders them (嘘 거짓말, 見 보다, バレ 들키다).
    std::string_view word = head;
    while (text::RuneCount(word) > 8) word.remove_prefix(FirstChar(word).size());
    for (; !word.empty(); word.remove_prefix(FirstChar(word).size())) {
        if (!HasKanji(word) && NameClassOf(FirstChar(word)) != kClassKatakana) continue;
        for (const dict::Candidate& c : snapshot.GetWordInfo(word, dict::LookupMode::General, {}).list) {
            if (c.source == word && c.codes != nullptr) return c.codes->part.TagIn("MHG");
        }
    }
    // A word in kana alone ends in a verb stem as often (汗かい, 荒れ); a kanji or a katakana word
    // the dictionary lacks is a noun.
    return NameClassOf(last) != kClassKatakana && !text::IsKanji(text::DecodeRune(last).cp);
}

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
            const std::string_view after = query.size() > source.size() ? From(query, static_cast<long>(source.size()))
                                                                         : std::string_view(following);
            if (source.empty() || list[i].codes == nullptr || !first || !WordFits(rule, source, before, lookupContext.cued) ||
                (rule.context == ConstraintContext::NotAfterNoun
                     ? !NotAfterNoun(head)
                     : !ContextFits(rule, list, source, after, before, previous, adjacent, preceding, lead))) {
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
                if (const auto name = NameAmong(rule, list, same, source, after); name != same.end()) {
                    nameCodes = list[*name].codes->asPlain, nameTarget = list[*name].target, nameId = list[*name].id;
                    rejected = same;
                } else if (rule.reject == ConstraintReject::NonName) {
                    // No name sense among the candidates: the name layer's person reading of
                    // the word (千里ちゃん 치사토짱, not 천리), unless a person noun, a user
                    // entry or a verb or adjective as long reads it.
                    const bool blocked =
                        std::any_of(list.begin(), list.end(), [&](const dict::Candidate& c) { return PredicateBlocks(c, source, after); }) ||
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
            case ConstraintReject::NonCuedName:
                if (const CuedReading name = CuedName(snapshot, list, same, source, after, records); name.codes != nullptr) {
                    nameCodes = name.codes, nameTarget = name.target, nameId = name.id;
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
            case ConstraintReject::NonVerbs: {
                auto verb = [&](std::size_t k) { return list[k].codes->part.Tag() == 'M'; };
                if (std::none_of(same.begin(), same.end(), verb)) break;
                for (const std::size_t k : same) {
                    if (!verb(k)) rejected.push_back(k);
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
