// The endings and copulas of regional and old speech read as standard Japanese before the
// spoken forms (spec GRM-3950). Each family maps one kind of form, by what stands before it and
// what follows it:
//   copula     the Kansai や and the western and old じゃ after what the copula follows, before
//              the end of the sentence, a sentence particle or a conjunction (アホや だ, 家じゃ だ,
//              雨やけど だけど, 番やさかい だから, わしの家じゃのう だなあ);
//   cause      the から of western speech after a predicate or the copula (行くけん, 雨やけん,
//              無理じゃけえ, 明日やさかい);
//   end        sentence-final ずら and だべ (だろう), ばい after a predicate (よ);
//   onbin      the う of a godan verb in わ before た, て and the contracted てる, ておく
//              (言うた 言った, 思うて, 笑うとけ) and the う of an adjective before て, ちゃ, なる
//              (暑うて 暑くて, 早う 早く);
//   adjective  the か of an adjective of Kyushu before a noun, a copula or the end (よか声
//              よい声, つらかでしょう つらいでしょう);
//   progress   the ちょる of western speech after a て form said っ or ん (持っちょる 持ってる);
//   order      the imperative in い of an ichidan verb (降りい 降りろ), せえ (しろ) and the
//              negative question んかい that orders (連れてこんかい 連れてこい, 引かんかい 引け);
//   request    the Kansai や of a request or an order after a て form or an imperative (信じてや, 食えや) and
//              the sentence-final で after a past or a ている (待っとったで) (spec SPC-39);
//   denial     ておる said とり before はせん (言っとりはせん) and the negative ん before やん, やろ (知らんやん);
//   nen        the Kansai ねん and ねや of an explanation (してはんねやろ, 減っとんねや);
//   polite     でがんす and ざます of old polite speech (です);
//   honorific  the honorific and humble forms the dictionary does not read (spec LEX-4765): ておくれ inside a
//              sentence (くれ), ておいで before a copula (ておられる), てらした (ておられた), the まいる of coming
//              (くる), ていかれ after an honoured subject (て行かれ), the statement てもらえない (てくれない) and
//              お + a verb stem + です (お読みです お読みになります) and お + a verb stem + ください (お使いください
//              使ってください).
#include <algorithm>
#include <array>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/pipeline/normalize.h"
#include "core/pipeline/pattern.h"
#include "core/pipeline/runes.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline::internal {

namespace {

// Hiragana words that the copula follows like a noun: formal nouns, demonstratives of manner,
// pronouns of rough and old speech and adjectival nouns written in kana.
constexpr std::string_view kKanaNouns[] = {
    "こと", "もの", "もん", "はず", "わけ", "とこ", "ところ", "ほう", "みたい", "よう", "そう", "こう", "ああ", "どう",
    "だけ", "ばかり", "ばっかり", "くらい", "ぐらい", "まま", "ため", "つもり", "ほんま", "うそ", "ほんと", "ほんとう",
    "わし", "おれ", "おら", "あたし", "わたし", "ぼく", "うち", "わい", "あんた", "おまえ", "きみ", "さん", "ちゃん",
    "くん", "さま", "はん", "たち", "きれい", "すてき", "だめ", "いや", "すき", "きらい", "へん", "たいへん", "じょうず",
    "へた", "あほ", "あほう", "ばか", "なん", "いつ", "どこ", "だれ", "ここ", "そこ", "あそこ", "これ", "それ", "あれ"};

// Words before じゃ that make it the じゃ of では, not the copula: それじゃ (then), これじゃ.
constexpr std::string_view kJaNotCopula[] = {"それ", "これ", "あれ", "そりゃ", "こりゃ", "ありゃ", "どれ", "そんなん", "ほんじゃ"};

thread_local const RunLookup* gRuns = nullptr;

bool VerbIs(std::string_view form) { return gRuns != nullptr && gRuns->verbForm != nullptr && gRuns->verbForm(gRuns->context, form); }
// Whether a dictionary word covers s[at, end) (有難う, お早う, 苦しゅうない are words as written).
bool WordCovers(std::string_view s, size_t at, size_t end) {
    return gRuns != nullptr && gRuns->holds != nullptr && gRuns->holds(gRuns->context, s, at, end);
}

// Whether text, whole, is a key of the dictionary (初めて).
bool IchidanIs(std::string_view form) { return gRuns != nullptr && gRuns->ichidanVerb != nullptr && gRuns->ichidanVerb(gRuns->context, form); }
// Whether stem, whole, is the stem of an ichidan verb or of くる of the dictionary (見, 出来, 来, き).
bool VowelStemIs(std::string_view stem) { return gRuns != nullptr && gRuns->vowelStem != nullptr && gRuns->vowelStem(gRuns->context, stem); }

bool IsWord(std::string_view text) { return gRuns != nullptr && gRuns->word != nullptr && gRuns->word(gRuns->context, text); }

// Whether stem is an adjectival noun the Kansai copula follows: not one that also says how much or how one does something
// with やる (copula.kansai-past-object: 色々やった is did a lot).
bool NaStemIs(std::string_view stem) {
    return gRuns != nullptr && gRuns->naStem != nullptr && gRuns->naStem(gRuns->context, stem) &&
           !dict::InClass(HDTRANS_CLASS("copula.kansai-past-object"), stem);
}

// Whether key, whole, is a person's name of the name layer (直, 治).
// Whether key, whole, is a name the document has proven (spec LEX-7740).
bool DocNameIs(std::string_view key) { return gRuns != nullptr && gRuns->documentName != nullptr && gRuns->documentName(gRuns->context, key); }
bool PersonIs(std::string_view key) { return gRuns != nullptr && gRuns->personName != nullptr && gRuns->personName(gRuns->context, key); }

// Whether the さ before the ん at pos and that ん are the さん of address after a person's name (直さんの家, 治さんやろ),
// not the negative of a verb in す whose stem is the name (直さない).
bool AddressAfterName(std::string_view s, size_t pos, std::string_view stem) {
    return pos > 0 && LastRune(s.substr(0, pos)).cp == U'さ' && PersonIs(stem);
}

bool AdjectiveIs(std::string_view stem) {
    return gRuns != nullptr && gRuns->adjectiveStem != nullptr && gRuns->adjectiveStem(gRuns->context, stem);
}

// The stem of a verb or adjective in kanji before pos: the kanji run and the kana after it up to pos.
std::string_view StemBefore(std::string_view s, size_t pos) {
    size_t start = RunStartBefore(s, pos, IsHiraganaClass);
    if (start == 0 || !IsKanjiClass(LastRune(s.substr(0, start)).cp)) return {};
    start = RunStartBefore(s, start, IsKanjiClass);
    return s.substr(start, pos - start);
}

bool EndsWithAny(std::string_view head, std::span<const std::string_view> words) {
    for (const std::string_view w : words) {
        if (head.ends_with(w)) return true;
    }
    return false;
}

// Whether the copula can follow head: a word that ends in kanji, katakana, a long mark, a digit or a
// Latin letter, or one of kKanaNouns.
bool CopulaFollows(std::string_view head) {
    if (head.empty()) return false;
    const char32_t c = LastRune(head).cp;
    if (IsKanjiClass(c) || IsKatakanaRune(c) || c == U'ー' || (c >= U'0' && c <= U'9') || (c >= U'０' && c <= U'９') ||
        (c >= U'A' && c <= U'z') || (c >= U'Ａ' && c <= U'ｚ')) {
        return true;
    }
    return EndsWithAny(head, kKanaNouns);
}

// Whether a predicate ends head: the dictionary or past form of a verb, an adjective in い, the
// negative ん or ない.
bool PredicateEnds(std::string_view head) {
    if (head.empty()) return false;
    return In(LastRune(head).cp, U"うくすつぬふむゆるぐぶたいん");
}

// Whether the sentence ends at p: the end, a mark that ends it, a closing bracket, a space or a
// drawn mark.
bool EndAt(std::string_view s, size_t p) {
    size_t q = p;
    return p == s.size() || OneOf(s, q, U"。｡．！!？?…‥」』）)　 ー～〜♪、，,―—");
}

// Whether the copula family's tail at p goes on to the end of the sentence or to a sentence particle
// before it.
constexpr std::string_view kFinalParticles[] = {"なあ", "なぁ", "ねえ", "ねぇ", "のう", "ね", "な", "よ", "わ", "ぞ", "ぜ", "さ"};

size_t FinalParticleEnd(std::string_view s, size_t p) {
    if (EndAt(s, p)) return p;
    for (const std::string_view f : kFinalParticles) {
        size_t q = p;
        if (Lit(s, q, f) && EndAt(s, q)) return q;
    }
    return std::string_view::npos;
}

// Where a form is read: anywhere, only where the sentence ends after it, or only inside the sentence.
// The ending table (extra/endings.tsv) reads the forms that end a sentence it has (学生や, 学生やで,
// 書くけん, 学生じゃけえ, 学生だべ): those are rewritten only inside a sentence.
enum class Where { Any, Final, Inside };

struct Form {
    std::string_view written;
    std::string_view plain;
    Where where;
};

// The forms of the copula や (Kansai) and じゃ (western and old speech), longest first.
constexpr Form kYaForms[] = {
    {"やないかい", "じゃないか", Where::Any}, {"やないか", "じゃないか", Where::Any}, {"やさかいに", "だから", Where::Any},
    {"やさかい", "だから", Where::Any},       {"やけえ", "だから", Where::Any},       {"やけぇ", "だから", Where::Any},
    {"やけん", "だから", Where::Any},         {"やきに", "だから", Where::Any},       {"やんけ", "じゃないか", Where::Final},
    {"やから", "だから", Where::Any},       {"やけども", "だけども", Where::Inside},  {"やけど", "だけど", Where::Inside},    {"やの", "なの", Where::Final},
    {"やねぇ", "だねぇ", Where::Final},       {"やねえ", "だねえ", Where::Final},     {"やと", "だと", Where::Any},
    {"やね", "だね", Where::Final},           {"やわ", "だわ", Where::Final},         {"やぞ", "だぞ", Where::Final},
    {"やよ", "だよ", Where::Final},           {"やんね", "だよね", Where::Final},     {"やんな", "だよな", Where::Final},
};
constexpr Form kJaForms[] = {
    {"じゃけえ", "だから", Where::Inside}, {"じゃけぇ", "だから", Where::Inside}, {"じゃけん", "だから", Where::Inside},
    {"じゃきに", "だから", Where::Any}, {"じゃいな", "だい", Where::Final}, {"じゃい", "だい", Where::Final},
};
// The から of western speech after a predicate.
constexpr Form kCauseForms[] = {
    {"だけんど", "だけど", Where::Any}, {"けんど", "けど", Where::Any},
    {"さかいに", "から", Where::Any},   {"さかい", "から", Where::Any},   {"けえ", "から", Where::Inside},
    {"けぇ", "から", Where::Inside},    {"けん", "から", Where::Inside},  {"だけん", "だから", Where::Any},
};

// Whether form f at pos, ending at p, stands where it is read: a form of the end before the end or a
// sentence particle there, a conjunction before a comma, a particle or what is no kana of a longer
// word (やからな, not やけどを, the noun 火傷).
bool FormFits(std::string_view s, size_t p, const Form& f) {
    const bool final = FinalParticleEnd(s, p) != std::string_view::npos;
    // やの before a space or a comma is the noun's の (坊やの お父さん).
    if (f.plain == "なの" && p < s.size() && In(At(s, p).cp, U" 　、，,")) return false;
    if (f.where == Where::Final) return final;
    // やと goes on with what the conditional or the quote takes (雨やといい, 雨やと思う), not 雇う (やとう, やとって).
    if (f.plain == "だと") return p == s.size() || !In(At(s, p).cp, U"うわいっ");
    if (f.where == Where::Inside && final) return false;
    if (p < s.size() && IsHiraganaClass(At(s, p).cp) && !f.plain.starts_with("じゃない")) {
        size_t q = p;
        if (!final && !OneOf(s, q, U"、，,")) {
            for (const std::string_view w : {"な", "ね", "よ", "の", "も", "さ", "ど"}) {
                if (s.substr(p).starts_with(w)) return true;
            }
            return false;
        }
    }
    return true;
}

// A form of forms at pos after a head that allows (follows), with the replacement in group 1.
bool CopulaForm(std::string_view s, size_t pos, Match& m, std::span<const Form> forms, bool (*follows)(std::string_view)) {
    const std::string_view head = s.substr(0, pos);
    if (!follows(head)) return false;
    for (const Form& f : forms) {
        size_t p = pos;
        if (!Lit(s, p, f.written) || !FormFits(s, p, f)) continue;
        // なんじゃい is a call of its own the word stage reads (なんじゃい！ 뭐냐!)
        if (f.written.starts_with("じゃい") && head.ends_with("なん")) continue;
        // やと after a list of や is the と of that list (あれやこれやと).
        if (f.written == "やと") {
            size_t start = pos;
            while (start > 0 && (IsKanjiClass(LastRune(s.substr(0, start)).cp) || IsKatakanaRune(LastRune(s.substr(0, start)).cp))) {
                start -= LastRune(s.substr(0, start)).size;
            }
            if (start == pos) {
                for (const std::string_view w : kKanaNouns) {
                    if (head.ends_with(w)) start = pos - w.size();
                }
            }
            // Nor after an お that starts the noun of such a list (お菓子やお茶やと).
            const size_t from = s.substr(0, start).ends_with("お") ? start - std::string_view("お").size() : start;
            if (s.substr(0, start).ends_with("や") || s.substr(0, from).ends_with("や")) return false;
            // Nor the と after a noun in や of the dictionary (お冷やとおしぼり).
            if (start < pos && IsWord(s.substr(from, pos + std::string_view("や").size() - from))) return false;
        }
        m.group[1] = f.plain;
        m.end = p;
        return true;
    }
    return false;
}

bool YaFollows(std::string_view head) {
    // The Kansai nouns of a reason or a guess read by the spoken forms (なんや, そや) and や after
    // the particles of a list (AやB) are no copula: the head ends in a word.
    if (head.ends_with("なん") || head.ends_with("何") || head.ends_with("そ") || head.ends_with("せ")) return false;
    // Not the や of a noun (坊や, 婆や, 爺や, 姉や).
    for (const std::string_view w : {"坊", "婆", "爺", "姉", "兄", "ばあ", "じい", "ねえ"}) {
        if (head.ends_with(w)) return false;
    }
    if (CopulaFollows(head)) return true;
    // A noun of kanji and one or two kana ending in the i or e row the dictionary has (お好み焼きやね, 祭りやな, 楽しみやと), not
    // a て form or an adjective in い.
    const char32_t last = LastRune(head).cp;
    if (!In(last, U"きしちにひみりぎじびけせねへめれげべ")) return false;
    const std::string_view stem = StemBefore(head, head.size());
    size_t kana = 0;
    for (size_t at = stem.size(); at > 0 && IsHiraganaClass(LastRune(stem.substr(0, at)).cp); at -= LastRune(stem.substr(0, at)).size) ++kana;
    return !stem.empty() && kana <= 2 && IsWord(stem);
}

bool JaFollows(std::string_view head) { return CopulaFollows(head) && !EndsWithAny(head, kJaNotCopula); }

// The forms of や that also follow a verb or an adjective in Kansai speech (食うとるやないか, すごいやないかい).
// Whether s at pos starts with the written spelling of one of forms. A pattern whose every match
// starts with one tests this first: the tests on the text before pos scan back over it, and
// ReplaceAll tries the pattern at every position of the line.
bool StartsWithForm(std::string_view s, size_t pos, std::span<const Form> forms) {
    const std::string_view rest = s.substr(pos);
    for (const Form& f : forms) {
        if (rest.starts_with(f.written)) return true;
    }
    return false;
}

constexpr Form kYaAfterPredicate[] = {
    {"やないかい", "じゃないか", Where::Any}, {"やないか", "じゃないか", Where::Any}, {"やんけ", "じゃないか", Where::Final},
    {"やんか", "じゃないか", Where::Final},   {"やんね", "よね", Where::Final},       {"やんな", "よな", Where::Final},
};

// The negative ん of a godan verb with a stem in kanji before the Kansai や of agreement or a guess that ends the sentence
// (知らんやん 知らないじゃん, 分からんやろ 分からないだろ): ない and the standard form. The match starts at the ん.
bool NegativeBeforeYa(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (pos == 0) return false;
    const std::string_view head = s.substr(0, pos);
    m.group[2] = "ない";
    // The とらん, どらん of ておる after a て form said っ, い, し or ん (治っとらんやん 治っていないじゃん).
    if (Lit(s, p, "とらん") || Lit(s, p, "どらん")) {
        const bool de = s.substr(pos).starts_with("ど");
        if (!In(LastRune(head).cp, de ? U"ん" : U"っいし") || (LastRune(head).cp != U'し' && StemBefore(s, pos).empty())) return false;
        m.group[2] = de ? "でいない" : "ていない";
    } else {
        if (!Lit(s, p, "ん")) return false;
        // The a-row kana ends the negative stem of a godan verb of the dictionary (知ら 知る, 分から 分かる; not なん, したん).
        static constexpr std::u32string_view kA = U"あかがさたなばまらわ";
        static constexpr std::string_view kU[] = {"う", "く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る", "う"};
        const size_t a = kA.find(LastRune(head).cp);
        if (a == std::u32string_view::npos) return false;
        const std::string_view stem = StemBefore(s, pos - LastRune(head).size);
        if (stem.empty() || !VerbIs(Cat({stem, kU[a]})) || AddressAfterName(s, pos, stem)) return false;
    }
    static constexpr std::string_view kTails[][2] = {
        {"やないかい", "じゃないか"}, {"やないか", "じゃないか"}, {"やんけ", "じゃないか"}, {"やんか", "じゃないか"},
        {"やんね", "よね"},           {"やんな", "よな"},         {"やん", "じゃん"},       {"やろ", "だろ"},
    };
    for (const auto& t : kTails) {
        size_t q = p;
        if (!Lit(s, q, t[0]) || FinalParticleEnd(s, q) == std::string_view::npos) continue;
        m.group[1] = t[1];
        m.end = q;
        return true;
    }
    return false;
}

// The negative ん of a verb before の, のに and a formal noun (分からんのに 分からないのに, 読まんのは 読まないのは, 気に入らんやつ
// 気に入らないやつ, 言わんこと, できんやつ; not the のう that ends a sentence): ない. A godan stem in kanji the dictionary has as
// a verb with the kana of the u row (分から 分かる, 聞か 聞く); an ichidan stem in kanji or in kana of two kana or more (出来, 見,
// でき) only before a noun, as 見んの says 見るの. Not before a kanji noun, where 知らん顔 is a phrase of its own, not a word of
// the dictionary in kana (きんの), the auxiliary かねる (しかねん奴), a regional word (あげん) or a person's name that ends in the ん (ゆかりん), nor in the さん of address after a person's name of the name layer (直さんの家, 治さんの
// おかげ). The match starts at the ん.
bool RegionalWord(std::string_view s, size_t pos, Match& m);

bool NegativeBeforeNoun(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    size_t p = pos;
    if (!Lit(s, p, "ん")) return false;
    const std::string_view rest = s.substr(p);
    bool noun = false;
    for (const std::string_view t : {"やつ", "奴", "こと", "もの"}) noun = noun || rest.starts_with(t);
    // のか reads well as written (聞かんのか 듣지 않는가); のう ends the sentence (変わらんのう)
    const bool no = rest.starts_with("の") && !rest.starts_with("のか") && !rest.starts_with("のう");
    if (!noun && !no) return false;
    const std::string_view head = s.substr(0, pos);
    // the stems in kana from the longest, of two kana or more and at most five (あきらめ, かんがえ); none where a word of the
    // dictionary in kana reaches the ん. The look back is bounded, so a long run of kana costs no more per ん.
    constexpr size_t kMaxStemKana = 5;
    const auto kanaStem = [&](size_t end, bool (*is)(std::string_view)) {
        size_t b = end, n = 0;
        for (; b > 0 && n < kMaxStemKana && IsHiraganaClass(LastRune(s.substr(0, b)).cp); n++) b -= LastRune(s.substr(0, b)).size;
        for (; b < end && n >= 2; b += text::DecodeRune(s.substr(b)).size, n--) {
            if (Match w; WordCovers(s, b, p) || (RegionalWord(s, b, w) && w.end >= p) || PersonIs(s.substr(b, p - b))) return false;
            if (is(s.substr(b, end - b))) return true;
        }
        return false;
    };
    static constexpr std::u32string_view kA = U"かがさたなばまらわ";
    static constexpr std::string_view kU[] = {"く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る", "う"};
    const size_t a = kA.find(LastRune(head).cp);
    if (a != std::u32string_view::npos) {
        const size_t end = pos - LastRune(head).size;
        const std::string_view stem = StemBefore(s, end);
        if (!stem.empty() && VerbIs(Cat({stem, kU[a]})) && !AddressAfterName(s, pos, stem)) {
            m.group[1] = "ない";
            m.end = p;
            return true;
        }
    }
    // the かねん of the auxiliary かねる is read by the constructions (LEX-7865: しかねん奴 할 수도 있는 놈)
    if (head.ends_with("かね") || head.ends_with("兼ね")) return false;
    // before the の of an explanation that goes on with だ, です, で or じゃ an ichidan or potential stem in kanji takes the
    // negative too (送り込めんのだが 보낼 수 없는데, 食べられんのです): a contracted る says ん only before a bare の (見んの)
    // (spec LEX-8113)
    if (!noun) {
        const std::string_view stem = StemBefore(s, pos);
        bool explains = false;
        for (const std::string_view t : {"のだ", "のです", "ので", "のじゃ"}) explains = explains || rest.starts_with(t);
        if (stem.empty() || !explains || PersonIs(Cat({stem, "ん"}))) return false;
        // the potential or passive in られ of an ichidan stem too (食べられんのです)
        const bool rare = stem.ends_with("られ") && VowelStemIs(stem.substr(0, stem.size() - std::string_view("られ").size()));
        if (!IsHiraganaClass(LastRune(stem).cp) || !(VowelStemIs(stem) || VerbIs(Cat({stem, "る"})) || rare) || IsWord(Cat({stem, "ん"}))) return false;
        m.group[1] = "ない";
        m.end = p;
        return true;
    }
    if (const std::string_view stem = StemBefore(s, pos); (VowelStemIs(stem) && !PersonIs(Cat({stem, "ん"}))) || kanaStem(pos, VowelStemIs)) {
        m.group[1] = "ない";
        m.end = p;
        return true;
    }
    return false;
}

// The question words of QuestionBefore and the longest of them in bytes.
constexpr std::string_view kQuestionWords[] = {"どこ", "何", "なに", "なん", "誰", "だれ", "いつ", "どう", "どれ", "どっち", "どんな", "なぜ", "なんで", "どない"};
constexpr size_t kQuestionWordMax = [] {
    size_t n = 0;
    for (const std::string_view w : kQuestionWords) n = std::max(n, w.size());
    return n;
}();

// The QuestionBefore asked last during a scan: the sentence of pos starts at start, and found is the start of a question
// word in it that ends by pos (npos for none).
struct QuestionMemo {
    unsigned long long scan = 0;
    size_t pos = 0;
    size_t start = 0;
    size_t found = std::string_view::npos;
    bool asking = false;
};
thread_local QuestionMemo gQuestion;

// What makes a question word an indefinite after it (何も, 誰か, どこでも, 何とか, どうせ, どうぞ, どうしても).
constexpr std::string_view kIndefiniteAfter[] = {"も", "か", "でも", "やら", "とか", "とも", "となく", "だか", "せ", "ぞ", "しても"};

// Whether a question word comes before pos in its sentence: the や of a question is read by the word
// stage and the old-speech rows (どこや 어디야, どう行くもんや). During a scan the answer for the place asked last is
// remembered, so asking at places that only grow reads the text once instead of once per place. With asking, a word that
// an indefinite follows does not count (何も知らない, どこかで会った: spec LEX-8112).
bool QuestionBefore(std::string_view s, size_t pos, bool asking = false) {
    const unsigned long long scan = ScanOver(s);
    const bool known = scan != 0 && gQuestion.scan == scan && gQuestion.pos <= pos && gQuestion.asking == asking;
    size_t start = pos;
    while (start > 0) {
        // No sentence end between the place asked last and pos: the sentence starts where it started then.
        if (known && start == gQuestion.pos) {
            start = gQuestion.start;
            break;
        }
        const text::Rune r = LastRune(s.substr(0, start));
        if (In(r.cp, U"。！？!?」『「 　")) break;
        start -= r.size;
    }
    // A word found then is still in the sentence; else only a word that ends after the place asked last is new.
    size_t found = known && start == gQuestion.start ? gQuestion.found : std::string_view::npos;
    if (found == std::string_view::npos) {
        const size_t from = known && start == gQuestion.start ? std::max(start, gQuestion.pos - std::min(gQuestion.pos - start, kQuestionWordMax - 1)) : start;
        const std::string_view clause = s.substr(start, pos - start);
        for (const std::string_view w : kQuestionWords) {
            // Not the いつ of あいつ, こいつ, そいつ.
            for (size_t at = clause.find(w, from - start); at != std::string_view::npos; at = clause.find(w, at + w.size())) {
                if (w == "いつ" && at > 0 && In(LastRune(clause.substr(0, at)).cp, U"あこそ")) continue;
                if (asking && std::ranges::any_of(kIndefiniteAfter, [&](std::string_view f) { return s.substr(start + at + w.size()).starts_with(f); })) {
                    continue;
                }
                found = start + at;
                break;
            }
            if (found != std::string_view::npos) break;
        }
    }
    if (scan != 0) gQuestion = {scan, pos, start, found, asking};
    return found != std::string_view::npos;
}

// The length of the noun of kKanaNouns that head ends with where a word starts before it (ほんま, 田中さん, あれはそう), 0 for
// none: not the tail of a longer word in kana (たくさん).
size_t KanaNounEnd(std::string_view head) {
    for (const std::string_view w : kKanaNouns) {
        if (!head.ends_with(w)) continue;
        const std::string_view before = head.substr(0, head.size() - w.size());
        if (before.empty() || !IsHiraganaClass(LastRune(before).cp) || In(LastRune(before).cp, U"はがもの")) return w.size();
    }
    return 0;
}

// The past やった, its conditional やったら and やったん of the Kansai copula and the やし of a reason after a noun
// (ほんまやったんか 진짜였어?, 学生やったんや, 学生やし 학생이고) (spec LEX-5996). The verb やる with an object
// whose を is dropped reads as it is (宿題やった, 俺は宿題やった, 今度やったら): the past is the copula after a noun
// in kana of class copula.kansai-past-noun (ほんまやった, そうやった) or before the Kansai んか, んや and やん.
bool YaPast(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    // Not the stem of a verb in やす (冷やし, 費やし, 燃やし).
    const std::string_view stem = StemBefore(s, pos);
    // An adjectival noun in kanji that is no noun is no object of やる either (大丈夫やった 괜찮았다, 静かやったら; not
    // 一杯やった, a drink).
    const bool adjectival = !stem.empty() && NaStemIs(stem);
    if ((!YaFollows(head) && !adjectival) || head.ends_with("こう") || head.ends_with("ああ")) return false;
    size_t p = pos;
    if (s.substr(pos).starts_with("やし") && VerbIs(Cat({stem.empty() ? head.substr(head.size() - LastRune(head).size) : stem, "やす"}))) return false;
    if (Lit(s, p, "やし")) {
        size_t q = p;
        // やしな, やしね end the sentence (静かやしな); やしない is はしない (抜けやしない).
        if (FinalParticleEnd(s, p) == std::string_view::npos && !OneOf(s, q, U"、，,")) return false;
        m.group[1] = "だし";
        m.end = p;
        return true;
    }
    // The kana nouns said of something, not a person (あたしやったげる, お兄ちゃんやったぞ: a doer), an adverb (ようやった,
    // これだけやった) or a noun of what one does (これやったら). A subject before the noun does not tell the copula from
    // the verb (昨日は雨やった, 俺は宿題やった), so it decides nothing.
    const size_t n = KanaNounEnd(head);
    const bool kana = n > 0 && dict::InClass(HDTRANS_CLASS("copula.kansai-past-noun"), head.substr(head.size() - n));
    p = pos;
    if (!Lit(s, p, "やった")) return false;
    const bool kansai = s.substr(p).starts_with("んか") || s.substr(p).starts_with("んや") || s.substr(p).starts_with("やん");
    if (!kana && !adjectival && !kansai) return false;
    // What follows is a conditional, an explanation, a conjunction or a sentence particle (not やったげる, やってあげる).
    {
        bool goes = EndAt(s, p) || FinalParticleEnd(s, p) != std::string_view::npos;
        for (const std::string_view w : {"ら", "ん", "やん", "けど", "し", "から", "で", "の", "か", "って", "な", "ね", "よ", "わ", "ぞ"}) {
            goes = goes || s.substr(p).starts_with(w);
        }
        if (!goes) return false;
    }
    m.group[1] = "だった";
    m.end = p;
    // The Kansai やん after it agrees: じゃん (流れやったやん 흐름이었잖아).
    if (size_t q = p; Lit(s, q, "やん") && FinalParticleEnd(s, q) != std::string_view::npos) {
        m.group[1] = "だったじゃん";
        m.end = q;
        return true;
    }
    // The Kansai んや after it explains: のだ (そうやったんや そうだったんだ).
    if (size_t q = p; Lit(s, q, "んや") && FinalParticleEnd(s, q) != std::string_view::npos) {
        m.group[1] = "だったんだ";
        m.end = q;
    }
    return true;
}

bool YaCopula(std::string_view s, size_t pos, Match& m) {
    // Every match starts with one of its forms, やと, the やし and やった of YaPast, or やろう after an adjectival noun.
    if (const std::string_view rest = s.substr(pos); !StartsWithForm(s, pos, kYaAfterPredicate) && !StartsWithForm(s, pos, kYaForms) &&
                                                     !rest.starts_with("やと") && !rest.starts_with("やし") && !rest.starts_with("やった") &&
                                                     !rest.starts_with("やろう")) {
        return false;
    }
    const std::string_view head = s.substr(0, pos);
    if (PredicateEnds(head) && !EndsWithAny(head, kKanaNouns) && !head.ends_with("ん") && CopulaForm(s, pos, m, kYaAfterPredicate, PredicateEnds)) {
        return true;
    }
    // A doubled mimetic in katakana (ヘトヘトや) reads with や as it is.
    {
        size_t start = pos;
        while (start > 0 && IsKatakanaRune(LastRune(s.substr(0, start)).cp)) start -= LastRune(s.substr(0, start)).size;
        const std::string_view run = s.substr(start, pos - start);
        if (run.size() >= 4 * 3 && run.size() % 2 == 0 && run.substr(0, run.size() / 2) == run.substr(run.size() / 2)) return false;
    }
    // 何やと that starts the sentence and ends it is the retort 何だと (何やと！ 뭐라고!).
    for (const std::string_view w : {"何", "なん"}) {
        size_t p = pos;
        if (!head.ends_with(w) || !Lit(s, p, "やと") || !EndAt(s, p)) continue;
        const size_t start = pos - w.size();
        if (start > 0 && !In(LastRune(s.substr(0, start)).cp, U"。！？!?」「『、，, 　…")) continue;
        m.group[1] = "だと";
        m.end = p;
        return true;
    }
    if (QuestionBefore(s, pos)) return false;
    // Not the や of や否や (聞くや否や 듣자마자).
    if (s.substr(0, pos).ends_with("否") || s.substr(pos).starts_with("や否")) return false;
    // Not the や that calls someone at the start of a sentence before a comma (民や、 백성이여).
    if (s.substr(pos).starts_with("や、") || s.substr(pos).starts_with("や，")) {
        size_t start = pos;
        bool alone = true;
        while (start > 0) {
            const text::Rune r = LastRune(s.substr(0, start));
            if (In(r.cp, U"。！？!?」「『 　、")) break;
            if (IsHiraganaClass(r.cp)) alone = false;
            start -= r.size;
        }
        if (alone) return false;
    }
    if (s.substr(pos).starts_with("やけど") && (s.substr(0, pos).ends_with("大") || s.substr(0, pos).ends_with("火"))) return false;
    if (YaPast(s, pos, m)) return true;
    // After an adjectival noun in kanji やろう is the guess だろう (大丈夫やろう 괜찮겠지), not the volitional of やる.
    if (const std::string_view stem = StemBefore(s, pos); !stem.empty() && NaStemIs(stem) && s.substr(pos).starts_with("やろう")) {
        m.group[1] = "だろう";
        m.end = pos + std::string_view("やろう").size();
        return true;
    }
    // Forms the spoken forms already read: やろ, やねん.
    for (const std::string_view w : {"やろ", "やし", "やねん", "やや"}) {
        if (s.substr(pos).starts_with(w)) return false;
    }
    return CopulaForm(s, pos, m, kYaForms, YaFollows);
}

// The じゃ of western and old speech before the から of its speech (無理じゃけん 무리니까). A bare
// じゃ and its other forms are read by the word stage and the old-speech rows (どこじゃ？ 어디냐,
// 大漁じゃ 풍어야).
bool JaCopula(std::string_view s, size_t pos, Match& m) { return CopulaForm(s, pos, m, kJaForms, JaFollows); }

bool CauseParticle(std::string_view s, size_t pos, Match& m) {
    if (!StartsWithForm(s, pos, kCauseForms)) return false;  // every match starts with one of its forms
    const std::string_view head = s.substr(0, pos);
    if (s.substr(pos).starts_with("だけん") || s.substr(pos).starts_with("だけんど")) {
        if (!CopulaFollows(head)) return false;
    } else if (!PredicateEnds(head) || head.ends_with("せん") || head.ends_with("ませ")) {
        return false;
    } else if (StemBefore(s, pos).empty()) {
        // A predicate in kana alone is a verb of the dictionary (あるけん, するけん; not ぼうけん, 冒険).
        size_t start = pos;
        while (start > 0 && IsHiraganaClass(LastRune(s.substr(0, start)).cp)) start -= LastRune(s.substr(0, start)).size;
        if (!VerbIs(s.substr(start, pos - start))) return false;
    }
    // After an い that follows kana the kana are a word of their own: いけん, いけえ of いけない, and the
    // drawn imperative 出ていけぇ. An adjective in kanji takes them (寒いけん).
    if (head.ends_with("い")) {
        const std::string_view before = head.substr(0, head.size() - std::string_view("い").size());
        if (before.empty() || !IsKanjiClass(LastRune(before).cp)) return false;
    }
    for (const Form& f : kCauseForms) {
        size_t p = pos;
        if (!Lit(s, p, f.written) || !FormFits(s, p, f)) continue;
        // Not the negative ん of an ichidan verb in け (押しつけんのか, 決めつけんなよ).
        if (f.written == "けん") {
            const std::string_view stem = StemBefore(s, pos);
            if (!stem.empty() && VerbIs(std::string(stem) + "ける")) continue;
        }
        m.group[1] = f.plain;
        m.end = p;
        return true;
    }
    return false;
}

// ずら and だべ that end a sentence after a predicate or the copula's head: だろう.
bool EndConjecture(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    for (const std::string_view w : {"ずら"}) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        if (FinalParticleEnd(s, p) == std::string_view::npos) continue;
        if (w == "ずら" && !PredicateEnds(head) && !CopulaFollows(head)) continue;
        // A predicate with a kanji before it (出してやったずら, 気持ちいいずら); not いたずら.
        if (StemBefore(s, pos).empty() || head.ends_with("いた") || s.substr(pos - std::min<size_t>(pos, 6)).starts_with("いたずら")) continue;
        if (w != "ずら" && head.empty()) continue;
        m.group[1] = "だろ";
        m.end = p;
        return true;
    }
    // ばい after a predicate ends the sentence with よ (よかばい); not やばい, しょぼい.
    size_t p = pos;
    if (Lit(s, p, "ばい") && EndAt(s, p) && In(LastRune(head).cp, U"るたかんだ") && !StemBefore(s, pos).empty()) {
        m.group[1] = "よ";
        m.end = p;
        return true;
    }
    return false;
}

// The う of a godan verb in わ said in western speech before the contracted ておけ, ておいて and the
// ちょる of western speech (笑うとけ 笑っとけ, 思うちょって): っ.
bool OnbinVerb(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "う")) return false;
    const std::string_view stem = StemBefore(s, pos);
    if (stem.empty() || !IsKanjiClass(LastRune(stem).cp) || !VerbIs(std::string(stem) + "う")) return false;
    // The sound stage reads うた and うて (言うた, 思うて) and the word stage the と form (言うとる).
    for (const std::string_view w : {"とけ", "といて", "といた", "ちょ"}) {
        if (s.substr(p).starts_with(w)) {
            m.group[1] = "っ";
            m.end = p;
            return true;
        }
    }
    return false;
}

// The う of an adjective said in western and old speech after a stem that ends in a kanji (暑うて
// 暑くて, 暑うちゃ, 早う来い, 早うなる) and the しゅう of one in しい (嬉しゅうて 嬉しくて,
// 美しゅうなる): く. A stem that ends in other kana takes the う of a volitional (見ようと).
bool OnbinAdjective(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    const bool shu = Lit(s, p, "ゅう");
    if (!shu && !Lit(s, p, "う")) return false;
    const std::string_view stem = StemBefore(s, pos);
    if (stem.empty() || !AdjectiveIs(stem)) return false;
    if (shu ? !stem.ends_with("し") : !IsKanjiClass(LastRune(stem).cp)) return false;
    if (VerbIs(std::string(stem) + "う") || AdjectiveIs(std::string(stem) + "う")) return false;
    // Before て, ちゃ, a form of なる, ござる or する, a verb in kanji or the end (not 危うく, 辛うじて, 疾うに).
    bool before = EndAt(s, p) || (p < s.size() && IsKanjiClass(At(s, p).cp));
    for (const std::string_view w : {"て", "ちゃ", "なる", "なっ", "なり", "なれ", "ない", "なく", "なか", "せ", "し", "す"}) {
        before = before || s.substr(p).starts_with(w);
    }
    if (!before) return false;
    // Words written so stay: 有難う, お早う, 苦しゅうない; and the polite 嬉しゅうございます.
    const size_t stemAt = pos - stem.size();
    if (WordCovers(s, stemAt, p) || (stemAt > 0 && s.substr(0, stemAt).ends_with("お"))) return false;
    m.group[1] = "く";
    m.end = p;
    return true;
}

// The か of an adjective of Kyushu speech before a noun, a copula, a sentence particle or the end
// (よか声, つらかでしょう, 煩わしか係累): い. Not before っ, ろ, れ, ら, も or the か of a question
// it starts (よかった, 高かろう, よかれ, 多かれ).
bool KyushuAdjective(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "か")) return false;
    // not かもしれない, かった, かろう...; も only in もん, もの after it (よかもん)
    if (p < s.size() && In(At(s, p).cp, U"っろれらもかけ") && !(s.substr(p).starts_with("もん") || s.substr(p).starts_with("もの"))) return false;
    std::string_view stem = StemBefore(s, pos);
    if (stem.empty()) {
        // A stem in kana after a mark, a space or the start of the sentence (よか声 よい声, つらかでしょう).
        const size_t start = RunStartBefore(s, pos, IsHiraganaClass);
        const std::string_view before = s.substr(0, start);
        if (!before.empty() && IsKatakanaRune(LastRune(before).cp)) return false;
        stem = s.substr(start, pos - start);
        // よ after a word of the dictionary, a particle that ends the kana before it or another よか (ちょうどよかところ
        // ちょうどよいところ, それはよかこと, これでよか, よかよか)
        if (const std::string_view word = stem.substr(0, stem.size() - std::min(stem.size(), std::string_view("よ").size()));
            !word.empty() && stem.ends_with("よ") && (IsWord(word) || In(LastRune(word).cp, U"はがもにでをと") || word.ends_with("よか")) &&
            !VerbIs(Cat({word, "る"}))) {
            stem = stem.substr(stem.size() - std::string_view("よ").size());
        }
        if (stem.empty() || (stem != "よ" && (stem.size() <= text::DecodeRune(stem).size || !AdjectiveIs(stem)))) return false;
        // A past or a verb form before the か of a question (きたか, したか) is no adjective stem.
        if (In(LastRune(stem).cp, U"ただてでるうくすつぬむぶぐ")) return false;
    } else if (const std::string_view word = stem.substr(0, stem.size() - std::min(stem.size(), std::string_view("よ").size()));
               stem.ends_with("よ") && !word.empty() && In(LastRune(word).cp, U"はがもにでをと")) {
        // よ after a particle that follows a kanji word (体によかもん 몸에 좋은 것)
        stem = stem.substr(stem.size() - std::string_view("よ").size());
    } else if (!AdjectiveIs(stem) || stem.ends_with("ず") || In(LastRune(stem).cp, U"ただてで")) {
        // A past before the か of a question (出たか？) and 恥ずか of 恥ずかしい cut (恥ずか死ぬ) stay.
        return false;
    }
    // Not the volitional of an ichidan verb said short (見よか 見ようか).
    if (stem.ends_with("よ") && VerbIs(std::string(stem.substr(0, stem.size() - std::string_view("よ").size())) + "る")) return false;
    // A stem of kanji alone is the noun and the か of a question or a choice (赤か青, 赤かと思ったら, 白かよ！).
    if (!IsHiraganaClass(LastRune(stem).cp) && stem != "よ") return false;
    // Not the か of くは before a negative (痛かねえ 痛くはない, 悪かねえ).
    for (const std::string_view w : {"ねえ", "ねぇ", "ねー", "ない", "ねェ", "ねエ"}) {
        if (s.substr(p).starts_with(w)) return false;
    }
    // What follows: a noun (kanji, katakana), the copula, a particle of the end or the end.
    const char32_t next = p < s.size() ? At(s, p).cp : 0;
    bool kanaNoun = false;
    for (const std::string_view w : kKanaNouns) kanaNoun = kanaNoun || (IsHiraganaClass(next) && w.size() > 3 && s.substr(p).starts_with(w));
    // よか before a noun of two or three kana of the dictionary (よかもん, よかもの, よかとこ)
    if (stem == "よ") {
        size_t q = p;
        for (int n = 1; n <= 3 && q < s.size() && IsHiraganaClass(At(s, q).cp) && !kanaNoun; n++) {
            q += text::DecodeRune(s.substr(q)).size;
            kanaNoun = n >= 2 && IsWord(s.substr(p, q - p));
        }
    }
    const bool ok = EndAt(s, p) || IsKanjiClass(next) || IsKatakanaRune(next) || kanaNoun || s.substr(p).starts_with("でしょ") ||
                    s.substr(p).starts_with("です") || s.substr(p).starts_with("ね") || s.substr(p).starts_with("よ") ||
                    s.substr(p).starts_with("ばい") || s.substr(p).starts_with("と") || s.substr(p).starts_with("の");
    if (!ok) return false;
    m.group[1] = "い";
    m.end = p;
    return true;
}

// The ちょる of western speech after a て form said with っ or ん (持っちょる 持ってる, 読んちょった
// 読んでた): てる and its forms.
bool WesternChoru(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (!head.ends_with("っ") && !head.ends_with("ん")) return false;
    const bool de = head.ends_with("ん");
    constexpr std::string_view kForms[][2] = {
        {"ちょらん", "てない"}, {"ちょった", "てた"}, {"ちょって", "てて"}, {"ちょるん", "てるん"}, {"ちょる", "てる"}, {"ちょれ", "てろ"},
    };
    for (const auto& f : kForms) {
        size_t p = pos;
        if (!Lit(s, p, f[0])) continue;
        m.group[1] = f[1];
        m.group[3] = de ? "で" : "て";
        m.end = p;
        return true;
    }
    return false;
}

// The imperative in い or ぃ of an ichidan verb ending the sentence (降りい！ 降りろ, 見い 見ろ), and
// せえ for しろ before the end or や, よ (しっかりせえや).
bool RoughImperative(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (Lit(s, p, "せえ")) {
        const std::string_view head = s.substr(0, pos);
        // Not after っ (うっせえ うるさい) or small kana, nor くだせえ.
        // しろ after a verbal noun, an adverb, に or はよ (勉強せえ, しっかりせえ, ええかげんにせえ, はよせえ); not うるせえ or 下せえ.
        const char32_t b = LastRune(head).cp;
        if (head.empty() || head.ends_with("うる") || head.ends_with("下") || head.ends_with("小") ||
            (IsKanjiClass(LastRune(head).cp) && AdjectiveIs(head.substr(head.size() - LastRune(head).size))) ||
            !((IsKanjiClass(b) || IsKatakanaRune(b) || In(b, U"りとようんに")) && b != U'ー')) {
            return false;
        }
        size_t q = p;
        if (!EndAt(s, q) && !Lit(s, q, "や") && !Lit(s, q, "よ")) return false;
        if (!EndAt(s, q) && q != p + std::string_view("や").size()) return false;
        m.group[1] = "しろ";
        m.end = p;
        return true;
    }
    // せろ of western speech for しろ in the same places, also before the って or と of a quote (勉強せろ, そがんせろって
    // 言うけん); not 失せろ, nor the imperative of a verb in せる (見せろ, 任せろ, 伏せろ, よせろ).
    if (Lit(s, p = pos, "せろ")) {
        const std::string_view head = s.substr(0, pos);
        if (head.empty()) return false;
        const char32_t b = LastRune(head).cp;
        if (!((IsKanjiClass(b) || IsKatakanaRune(b) || In(b, U"りとよんに")) && b != U'ー')) return false;
        if (b == U'よ' && !head.ends_with("はよ")) return false;  // はよせろ; not よせろ of 寄せる
        for (size_t at = pos, n = 1; n <= 3 && at > 0; n++) {
            const text::Rune r = LastRune(head.substr(0, at));
            if (!(IsHiraganaClass(r.cp) || IsKanjiClass(r.cp))) break;
            at -= r.size;
            if (VerbIs(Cat({head.substr(at), "せる"}))) return false;
        }
        size_t q = p;
        if (!EndAt(s, q) && !Lit(s, q, "や") && !Lit(s, q = p, "よ") && !Lit(s, q = p, "って") && !Lit(s, q = p, "と")) return false;
        m.group[1] = "しろ";
        m.end = p;
        return true;
    }
    p = pos;
    if (!Lit(s, p, "い")) return false;
    size_t q = p;
    if (!EndAt(s, q) && !Lit(s, q, "や")) return false;
    const std::string_view stem = StemBefore(s, pos);
    if (stem.empty() || stem.ends_with("来") || stem.ends_with("てい") || stem.ends_with("でい") || stem.ends_with("い") ||
        !VerbIs(std::string(stem) + "る")) {
        return false;
    }
    // An ichidan stem ends in a kana of the i or e row, or is one of the ichidan verbs of one kanji (見,
    // 寝, 出; not 集い, the noun); 来い is the imperative of 来る as written.
    const char32_t last = LastRune(stem).cp;
    if (!In(last, U"いきしちにひみりぎじびえけせてねへめれげぜでべ") && !(text::RuneCount(stem) == 1 && In(last, U"見居着寝似煮干射得経出"))) {
        return false;
    }
    if (AdjectiveIs(stem)) return false;
    // A set phrase of the dictionary keeps its old imperative (面を上げい 고개를 들라).
    if (WordCovers(s, pos - stem.size(), p)) return false;
    m.group[1] = "ろ";
    m.end = p;
    return true;
}

// The negative question んかい that orders, not before ？ (連れてこんかい 連れてこい, 引かんかい 引け,
// 食べんかい 食べろ): the imperative of the verb of the negative stem before ん. The match starts at
// the last kana of that stem.
bool OrderNkai(std::string_view s, size_t pos, Match& m) {
    if (pos >= s.size()) return false;
    const text::Rune last = At(s, pos);
    size_t p = pos + last.size;
    if (!Lit(s, p, "んかい")) return false;
    if (!EndAt(s, p) || (p < s.size() && In(At(s, p).cp, U"？?"))) return false;
    const std::string_view lastText = s.substr(pos, last.size);
    const std::string_view head = s.substr(0, pos);
    const std::string_view stem = StemBefore(s, pos);
    // 来る: こんかい after kana (連れてこんかい) or 来 (来んかい is read below as an ichidan stem).
    if (lastText == "こ" && (head.empty() || !IsKanjiClass(LastRune(head).cp))) {
        m.group[1] = "こい";
        m.end = p;
        return true;
    }
    static constexpr std::string_view kA[] = {"わ", "か", "が", "さ", "た", "な", "ば", "ま", "ら"};
    static constexpr std::string_view kU[] = {"う", "く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る"};
    static constexpr std::string_view kE[] = {"え", "け", "げ", "せ", "て", "ね", "べ", "め", "れ"};
    for (size_t k = 0; k < std::size(kA); k++) {
        if (lastText != kA[k] || stem.empty()) continue;
        if (!VerbIs(Cat({stem, kU[k]}))) return false;
        m.group[1] = kE[k];
        m.end = p;
        return true;
    }
    // An ichidan stem (食べんかい 食べろ) or a verbal noun with せ (勉強せんかい 勉強しろ).
    const std::string_view whole = s.substr(pos - stem.size(), stem.size() + last.size);
    if (lastText == "せ" && !stem.empty() && !VerbIs(std::string(whole) + "る")) {
        m.group[1] = "しろ";
        m.end = p;
        return true;
    }
    if (!stem.empty() && (IsKanjiClass(last.cp) || In(last.cp, U"いきしちにひみりぎじびえけせてねへめれげぜでべ")) &&
        VerbIs(std::string(whole) + "る")) {
        m.group[1] = "ろ";
        m.end = p;
        return true;
    }
    // An ichidan verb in kana where a word starts, from two kana to four (やめんかい やめろ) (spec LEX-8114).
    if (stem.empty() && In(last.cp, U"えけげせてねべめれ")) {
        size_t b = pos, n = 1;
        for (; b > 0 && n < 4 && IsHiraganaClass(LastRune(s.substr(0, b)).cp); n++) b -= LastRune(s.substr(0, b)).size;
        if (b > 0 && (IsHiraganaClass(LastRune(s.substr(0, b)).cp) || IsKanjiClass(LastRune(s.substr(0, b)).cp))) return false;
        if (n >= 2 && IchidanIs(Cat({s.substr(b, pos + last.size - b), "る"})) && !WordCovers(s, b, p)) {
            m.group[1] = "ろ";
            m.end = p;
            return true;
        }
    }
    return false;
}

// Where the sentence ends at p after drawn marks: the end, a mark that ends it, a closing bracket or a space, not a comma
// (ましてや、 and a list go on); npos where it goes on.
size_t SentenceEndAfterDrawn(std::string_view s, size_t p, std::u32string_view drawn) {
    while (OneOf(s, p, drawn)) {
    }
    size_t q = p;
    if (p == s.size() || OneOf(s, q, U"。｡．！!？?…‥」』）)　 ♪")) return p;
    return std::string_view::npos;
}

// Whether head ends with a て form: て after っ, い, し, a kanji or the stem of an ichidan verb, で after the ん or い of a
// stem in kanji (読んで, 泳いで) or ないで.
bool TeFormEnds(std::string_view head) {
    constexpr size_t kKana = std::string_view("て").size();
    if (head.size() <= kKana) return false;
    const std::string_view before = head.substr(0, head.size() - kKana);
    if (head.ends_with("て")) {
        const char32_t b = LastRune(before).cp;
        if (IsKanjiClass(b) || In(b, U"っしえけげせぜてねべめれ")) return true;
        // The といて, どいて of ておいて (置いといて, 気にせんといて, 読んどいて).
        if (before.ends_with("とい") || before.ends_with("どい")) return true;
        // An i-row kana ends the stem of an ichidan verb (借りて, 信じて) or is the い of a godan verb in く, ぐ (書いて); the
        // stem of a godan verb before the てえ of たい is none (食いてぇ, 帰りてぇ).
        const std::string_view stem = before.substr(0, before.size() - LastRune(before).size);
        if (b == U'い' && (VerbIs(Cat({stem, "く"})) || VerbIs(Cat({stem, "ぐ"})))) return true;
        return In(b, U"いきぎじちにひびみり") && VerbIs(Cat({before, "る"}));
    }
    if (!head.ends_with("で")) return false;
    if (head.ends_with("ないで")) return true;
    if (!In(LastRune(before).cp, U"んい")) return false;
    const std::string_view stem = before.substr(0, before.size() - kKana);
    return !stem.empty() && IsKanjiClass(LastRune(stem).cp);
}

// Whether the text before pos ends with an imperative: くれ, しろ after what is no kana, a particle or an adverb (勉強しろ,
// ゆっくりしろ, はよしろ), the ろ of an ichidan verb or the e-row kana of a godan verb of the dictionary (下りろ, 食え,
// 行け, そこでやれ). A kana noun the copula follows (これ, ところ) is none.
bool ImperativeEnds(std::string_view s, size_t pos) {
    const std::string_view head = s.substr(0, pos);
    if (head.empty() || EndsWithAny(head, kKanaNouns)) return false;
    if (head.ends_with("くれ")) return true;
    constexpr size_t kKana = std::string_view("ろ").size();
    if (head.ends_with("しろ")) {
        const std::string_view before = head.substr(0, head.size() - 2 * kKana);
        return before.empty() || !IsHiraganaClass(LastRune(before).cp) || In(LastRune(before).cp, U"をにともりはがよで");
    }
    static constexpr std::u32string_view kE = U"えけげせてねべめれ";
    static constexpr std::string_view kU[] = {"う", "く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る"};
    const char32_t last = LastRune(head).cp;
    const size_t e = kE.find(last);
    if (e == std::u32string_view::npos && last != U'ろ') return false;
    const std::string_view ending = last == U'ろ' ? "る" : kU[e];
    const size_t lastAt = pos - kKana;
    if (const std::string_view stem = StemBefore(s, lastAt); !stem.empty() && VerbIs(Cat({stem, ending}))) return true;
    // A verb in kana: one or two kana before the last after no kana or a particle (やれ, そこでやれ).
    size_t start = lastAt;
    for (int n = 0; n < 2 && start > 0 && IsHiraganaClass(LastRune(s.substr(0, start)).cp); n++) {
        start -= LastRune(s.substr(0, start)).size;
        const char32_t b = start > 0 ? LastRune(s.substr(0, start)).cp : 0;
        if (IsHiraganaClass(b) && !In(b, U"をでにがはもとて")) continue;
        if (VerbIs(Cat({s.substr(start, lastAt - start), ending}))) return true;
    }
    return false;
}

// The や of a Kansai request or order that ends a sentence after a て form or an imperative (信じてや, 来てぇや,
// 見んといてや, 食えや, 上がってくれや, 下りろや): よ. A drawn ぇ or え after the て falls away and drawn marks around the
// や go after the よ (信じてぇ～や 信じてよ～). ましてや and its like go on before a comma and are no end.
bool RequestYa(std::string_view s, size_t pos, Match& m) {
    // Every match is や after an optional ぇ or え and drawn marks.
    {
        size_t p = pos;
        OneOf(s, p, U"ぇえ");
        while (OneOf(s, p, U"ー～〜")) {
        }
        if (!Lit(s, p, "や")) return false;
    }
    const std::string_view head = s.substr(0, pos);
    if (head.empty() || !IsHiraganaClass(LastRune(head).cp)) return false;
    bool te = TeFormEnds(head);
    // A word of the dictionary in て that is no verb's て form is an adverb or a noun before the copula (初めてや 처음이야).
    if (te && head.ends_with("て")) {
        size_t from = pos - std::string_view("て").size();
        for (int n = 0; n < 3 && from > 0 && te; n++) {
            from -= LastRune(s.substr(0, from)).size;
            const std::string_view word = s.substr(from, pos - from);
            const std::string_view stem = word.substr(0, word.size() - std::string_view("て").size());
            te = !IsWord(word) || VerbIs(Cat({stem, "る"})) || stem.ends_with("っ") || stem.ends_with("し") || stem.ends_with("い");
        }
    }
    if (!te && !ImperativeEnds(s, pos)) return false;
    size_t p = pos;
    if (te) OneOf(s, p, U"ぇえ");
    const size_t drawn = p;
    while (OneOf(s, p, U"ー～〜")) {
    }
    const size_t drawnEnd = p;
    if (!Lit(s, p, "や")) return false;
    const size_t end = SentenceEndAfterDrawn(s, p, U"ー～〜ぁ");
    if (end == std::string_view::npos) return false;
    m.group[1] = "よ";
    // After an imperative the drawn marks fall away: the analysis reads 言えよ but not 言えよ～.
    m.group[2] = te ? s.substr(drawn, drawnEnd - drawn) : std::string_view{};
    m.group[3] = te ? s.substr(p, end - p) : std::string_view{};
    m.end = end;
    return true;
}

// Whether the text before pos ends with the past of a verb with a stem in kanji, of an adjective or a negative (言った,
// 言うた, 読んだ, 来た, 高かった, 知らんかった), the past copula (やった, だった) or a ている said in full or short (知っとる,
// 待っとった, 読んでる).
bool PastOrStateEnds(std::string_view s, size_t pos) {
    const std::string_view head = s.substr(0, pos);
    static constexpr std::string_view kStates[] = {"ていた", "ておった", "てた", "とった", "ている", "ておる", "てる", "とる",
                                                   "でいた", "でおった", "でた", "どった", "でいる", "でおる", "でる", "どる"};
    for (const std::string_view w : kStates) {
        if (!head.ends_with(w)) continue;
        const std::string_view before = head.substr(0, head.size() - w.size());
        if (before.empty()) return false;
        const char32_t b = LastRune(before).cp;
        if (w.starts_with("て") || w.starts_with("と")) return IsKanjiClass(b) || In(b, U"っいうしきぎじちにひびみりえけげせぜねべめれ");
        const std::string_view stem = before.substr(0, before.size() - LastRune(before).size);
        return In(b, U"んい") && !stem.empty() && IsKanjiClass(LastRune(stem).cp);
    }
    if (head.ends_with("やった") || head.ends_with("だった")) return true;
    if (head.ends_with("かった")) return !StemBefore(s, pos).empty() || head.ends_with("なかった");
    const bool ta = head.ends_with("た");
    if (!ta && !head.ends_with("だ")) return false;
    const size_t at = pos - std::string_view("た").size();
    const std::string_view stem = StemBefore(s, at);
    if (stem.empty()) return false;
    const char32_t last = LastRune(stem).cp;
    const std::string_view base = stem.substr(0, stem.size() - LastRune(stem).size);
    const auto verb = [&](std::string_view b, std::initializer_list<std::string_view> endings) {
        for (const std::string_view e : endings) {
            if (VerbIs(Cat({b, e}))) return true;
        }
        return false;
    };
    if (!ta) return (last == U'ん' && verb(base, {"む", "ぬ", "ぶ"})) || (last == U'い' && verb(base, {"ぐ"}));
    switch (last) {
        case U'っ': return verb(base, {"う", "つ", "る", "く"});
        case U'う': return verb(base, {"う"});
        case U'い': return verb(base, {"く"});
        case U'し': return verb(base, {"す"}) || IsKanjiClass(LastRune(base).cp) || IsKatakanaRune(LastRune(base).cp);
        default: return verb(stem, {"る"});
    }
}

// The で of Kansai speech that ends a sentence after a past or a ている (言うたで 言ったよ, 待っとったで, 知っとるで): よ.
bool FinalDe(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "で") || SentenceEndAfterDrawn(s, p, U"ー～〜") == std::string_view::npos || !PastOrStateEnds(s, pos)) return false;
    m.group[1] = "よ";
    m.end = p;
    return true;
}

// The ておる of western speech said とり or どり before the negative はせん, はしない or ゃせん (言っとりはせん, しとりゃせん,
// 読んどりはせん): ていない, with the は of a denial.
bool TeOruNegative(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (head.empty()) return false;
    size_t p = pos;
    const bool de = Lit(s, p, "ど");
    if (!de && !Lit(s, p, "と")) return false;
    const char32_t b = LastRune(head).cp;
    if (de ? b != U'ん' : !In(b, U"っいし")) return false;
    if (b != U'し' && StemBefore(s, pos).empty()) return false;
    static constexpr std::string_view kTails[][2] = {
        {"りはせんかった", "はいなかった"}, {"りはしなかった", "はいなかった"}, {"りゃせんかった", "はいなかった"},
        {"りゃしなかった", "はいなかった"}, {"りはせん", "はいない"},           {"りはしない", "はいない"},
        {"りゃせん", "はいない"},           {"りゃしない", "はいない"},
    };
    for (const auto& t : kTails) {
        size_t q = p;
        if (!Lit(s, q, t[0])) continue;
        m.group[1] = de ? "で" : "て";
        m.group[2] = t[1];
        m.end = q;
        return true;
    }
    return false;
}

// The Kansai ねん, ねや of an explanation after a predicate: んだ (行くねん 行くんだ, 減っとんねや
// 減ってるんだ). After the contracted てん, とん (ている) and はん (the honorific はる) the ん is that
// verb's る. The match starts at the ね, or at the て, と, で, ど or は before ん.
bool KansaiNen(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    std::string_view lead;
    for (const auto& [w, l] : {std::pair<std::string_view, std::string_view>{"てん", "てる"}, {"でん", "でる"}, {"とん", "とる"},
                               {"どん", "どる"}, {"はん", "はる"}}) {
        size_t q = pos;
        if (Lit(s, q, w) && s.substr(q).starts_with("ね")) {
            lead = l;
            p = q;
            break;
        }
    }
    // The や of the copula before ねん is なん (好きやねんけど 好きなんだけど). After a question word KansaiQuestionYanen reads
    // it (どっちやねん 어느 쪽이야).
    if (lead.empty() && s.substr(pos).starts_with("やねん") && YaFollows(s.substr(0, pos))) {
        lead = "な";
        p = pos + std::string_view("や").size();
    }
    if (lead.empty() && !PredicateEnds(s.substr(0, pos))) return false;
    if (lead.empty() && s.substr(0, pos).ends_with("ん")) return false;
    struct Tail {
        std::string_view written, plain;
    };
    // The ねん that ends the sentence itself is read by the word stage (行くねん 가거든, 好きやねん 좋아하거든), except after a
    // question word, where it asks, and before a particle or けど: んだ (何言うねん 何言うんだ, どこ行くねん, 何してんねん,
    // 行くねんな 行くんだな, 好きやねんけど 好きなんだけど).
    static constexpr Tail kTails[] = {{"ねや", "んだ"}, {"ねん", "んだ"}};
    for (const Tail& t : kTails) {
        size_t q = p;
        if (!Lit(s, q, t.written)) continue;
        const size_t final = FinalParticleEnd(s, q);
        const bool but = s.substr(q).starts_with("けど");
        if (final == std::string_view::npos && !but) continue;
        if (t.written == "ねん" && !but && final == q && !QuestionBefore(s, pos)) continue;
        // After とん, どん and はん the word stage reads ねん (何しとんねん 뭐하고 있어).
        if (t.written == "ねん" && !lead.empty() && lead != "な" && lead != "てる" && lead != "でる") continue;
        if (t.written == "ねや" && lead == "な") continue;
        m.group[1] = lead;
        // After a contracted ている the question asks with の (何してんねん 何してるの 뭐해).
        m.group[2] = t.written == "ねん" && final == q && !but && (lead == "てる" || lead == "でる") ? std::string_view("の") : t.plain;
        m.end = q;
        return true;
    }
    return false;
}

// The っちゅう and ちゅう of western speech that quote: という (来るっちゅうさかい 来るというから,
// 好都合ちゅうこった 好都合ということだ). ちゅう without っ only after a word in kanji or katakana
// before こと, もん, の, わけ, ん or か (not 注意, 躊躇 in kana).
bool QuoteChuu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (Lit(s, p, "っちゅう")) {
        if (pos == 0 || s.substr(0, pos).ends_with("しょ")) return false;
        // あっちゅう間 is あっという間.
        m.group[1] = s.substr(p).starts_with("間") ? "っという" : "という";
        m.end = p;
        return true;
    }
    p = pos;
    if (!Lit(s, p, "ちゅう") || pos == 0) return false;
    const char32_t b = LastRune(s.substr(0, pos)).cp;
    if ((!IsKanjiClass(b) && !IsKatakanaRune(b)) || s.substr(0, pos).ends_with("何")) return false;
    bool before = false;
    for (const std::string_view w : {"こと", "こっ", "もん", "の", "わけ", "ん", "か"}) before = before || s.substr(p).starts_with(w);
    if (!before) return false;
    m.group[1] = "という";
    m.end = p;
    return true;
}

// The こった of ことだ and the こっちゃ of ことじゃ or the Kansai ことや said short after a predicate or the な of an
// adjectival noun (知らんこった 知らないことだ, 知ったこっちゃない 知ったことじゃない, えらいこっちゃ えらいことだ,
// そんなこった): こと and the copula, where the copula ends the sentence or goes on to ない. After a particle or a kanji
// the こった is the verb 凝る (肩がこった, 凝った料理) and こっちゃ the こっちは that starts a clause (こっちゃ忙しい);
// a dictionary word that holds it reads as written (言わんこっちゃない).
bool ShortKoto(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (!PredicateEnds(head) && !head.ends_with("な")) return false;
    size_t p = pos;
    if (Lit(s, p, "こった")) {
        if (FinalParticleEnd(s, p) == std::string_view::npos || WordCovers(s, pos, p)) return false;
        m.group[1] = "ことだ";
        m.end = p;
        return true;
    }
    p = pos;
    if (!Lit(s, p, "こっちゃ") || WordCovers(s, pos, p)) return false;
    for (const std::string_view w : {"ない", "ねえ", "ねぇ", "ねー", "あらへん", "あるまい", "ありません"}) {
        if (!s.substr(p).starts_with(w)) continue;
        m.group[1] = "ことじゃ";
        m.end = p;
        return true;
    }
    if (FinalParticleEnd(s, p) == std::string_view::npos) return false;
    m.group[1] = "ことだ";
    m.end = p;
    return true;
}

// The polite speech of Osaka merchants (Senba): まひょ ましょう, だす です, でっせ ですよ, まっせ ますよ,
// でっか ですか, まっか ますか, まんがな and でんがな ますよ, ですよ; なはれ なさい and くんなはれ ください.
bool SenbaPolite(std::string_view s, size_t pos, Match& m) {
    struct Polite {
        std::string_view written, plain;
        bool afterStem;  // after the continuative of a verb (しまひょ, 来なはれ); else after what the copula follows
    };
    static constexpr Polite kForms[] = {
        {"まひょか", "ましょうか", true}, {"まひょ", "ましょう", true},  {"まっせ", "ますよ", true},
        {"まっか", "ますか", true},       {"まんがな", "ますよ", true},  {"とくんなはれ", "といてください", false},
        {"てくんなはれ", "てください", false}, {"でくんなはれ", "でください", false}, {"なはれ", "なさい", true},
        {"でっせ", "ですよ", false},      {"でっか", "ですか", false},   {"でんがな", "ですよ", false},
        {"だす", "です", false},
    };
    const std::string_view head = s.substr(0, pos);
    if (head.empty()) return false;
    for (const Polite& f : kForms) {
        size_t p = pos;
        if (!Lit(s, p, f.written)) continue;
        if (f.written == "だす") {
            // だす after どう, こう, そう before the end or か (どうだす？); a noun before it may take 出す in
            // kana (本気だす).
            if (!head.ends_with("どう") && !head.ends_with("こう") && !head.ends_with("そう")) continue;
            if (!EndAt(s, p) && !s.substr(p).starts_with("か") && FinalParticleEnd(s, p) == std::string_view::npos) continue;
        } else if (f.written == "でっか") {
            if (p < s.size() && In(At(s, p).cp, U"いくかさ")) continue;  // でっかい (big)
            // Not the volitional of an ichidan verb in で said short (茹でっか 茹でようか).
            if (!CopulaFollows(head) || (IsKanjiClass(LastRune(head).cp) && VerbIs(Cat({s.substr(pos - LastRune(head).size, LastRune(head).size), "でる"})))) continue;
        } else if (f.afterStem) {
            // A continuative ends in a kana of the i or e row or a kanji (し, あり, 来).
            const char32_t b = LastRune(head).cp;
            if (!In(b, U"いきしちにひみりぎじびえけせてねへめれげぜでべ") && !IsKanjiClass(b)) continue;
        } else if (f.written.starts_with("でっ") || f.written == "でんがな") {
            if (!CopulaFollows(head)) continue;
        }
        m.group[1] = f.plain;
        m.end = p;
        return true;
    }
    return false;
}

// The が of Tosa that makes a predicate a noun at the end of a question: のか (起こったがか？
// 起こったのか？); だに of Mikawa: だよ. The と of Kyushu (知らんとよ) is also the と of hearsay or of a
// condition elsewhere (来るとよ, いないとよ、) and stays.
bool NominalEnd(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    size_t p = pos;
    if (Lit(s, p, "がか") && EndAt(s, p) && PredicateEnds(head) && !head.ends_with("い") && !StemBefore(s, pos).empty() &&
        IsHiraganaClass(LastRune(StemBefore(s, pos)).cp)) {
        m.group[1] = "のか";
        m.end = p;
        return true;
    }
    p = pos;
    if (Lit(s, p, "だに") && EndAt(s, p) && CopulaFollows(head)) {
        m.group[1] = "だよ";
        m.end = p;
        return true;
    }
    return false;
}

// Words of regional speech at the start of a word: ほんで それで and ほんなら それなら where a sentence
// starts, こげん, そげん, あげん, どげん (こんな…), ゆんべ 夕べ, じゃかあしい やかましい, あんさん あなた,
// はよ 早く before a verb or せ, し, the pronouns わい (わし) and おら (おれ) before a particle, and ええ
// いい right before a noun (ええ店 いい店; not the ええ of yes).
bool RegionalWord(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    const char32_t b = head.empty() ? 0 : LastRune(head).cp;
    const bool wordStart = head.empty() || !(IsHiraganaClass(b) || IsKatakanaRune(b));
    if (!wordStart) return false;
    const bool sentenceStart = head.empty() || In(b, U"。！？!?「『（( 　、…‥");
    // After a kanji the kana go on with a word or a particle (勇気はよし, 味わい, 賑わい).
    const bool afterKanji = IsKanjiClass(b);
    // an old conjunction opening a sentence (class conjunction.archaic-start, note its plain form: されば、 だから)
    if (sentenceStart) {
        const auto members = dict::ClassMembers(HDTRANS_CLASS("conjunction.archaic-start"));
        const auto notes = dict::ClassNotes(HDTRANS_CLASS("conjunction.archaic-start"));
        for (size_t i = 0; i < members.size() && i < notes.size(); i++) {
            size_t p = pos;
            if (!Lit(s, p, members[i]) || notes[i].empty() || (p < s.size() && IsHiraganaClass(At(s, p).cp))) continue;
            m.group[1] = notes[i];
            m.end = p;
            return true;
        }
    }
    // a western こ, そ, ど word in がん or げん before the copula, or before か, と or a sentence particle that ends the
    // sentence (class adverb.western-kosoado, note its plain adverb: そがんです そうです 그래요, そがんね, こげんか, そがんと？);
    // before a verb or a noun the word stage reads it (そがん言うた 그렇게, そがん人 그런)
    {
        const auto members = dict::ClassMembers(HDTRANS_CLASS("adverb.western-kosoado"));
        const auto notes = dict::ClassNotes(HDTRANS_CLASS("adverb.western-kosoado"));
        for (size_t i = 0; i < members.size() && i < notes.size(); i++) {
            size_t p = pos;
            if (!Lit(s, p, members[i]) || notes[i].empty()) continue;
            bool before = !EndAt(s, p) && FinalParticleEnd(s, p) != std::string_view::npos;  // そがんね
            for (const std::string_view w : {"です", "でしょ"}) before = before || s.substr(p).starts_with(w);
            for (const std::string_view w : {"だ", "や", "か", "と"}) {
                size_t q = p;
                if (!Lit(s, q, w)) continue;
                // the ど word of a question with か is the indefinite どうか, no question (どがんかね？ stays)
                if (w == "か" && notes[i].starts_with("ど")) continue;
                // the copula だ, や that ends the sentence or goes on as one (そがんやろ, そがんだった, そがんやけん); not や
                // of a noun (そがんやつ 그런 놈)
                bool goes = FinalParticleEnd(s, q) != std::string_view::npos;
                if (w == "だ" || w == "や") {
                    for (const std::string_view t : {"ろ", "っ", "けん", "から", "けど", "し", "か"}) goes = goes || s.substr(q).starts_with(t);
                }
                before = before || goes;
            }
            if (!before) continue;
            m.group[1] = notes[i];
            m.end = p;
            return true;
        }
    }
    struct Word {
        std::string_view written, plain;
        int where;  // 0 any word start, 1 sentence start, 2 before a particle, 3 before a verb, 4 before a noun
    };
    static constexpr Word kWords[] = {
        {"ほんなら", "それなら", 1}, {"ほんで", "それで", 1},     {"こげん", "こんな", 0}, {"そげん", "そんな", 0},
        {"あげん", "あんな", 0},     {"どげん", "どんな", 0},     {"ゆんべ", "夕べ", 0},   {"じゃかあしい", "やかましい", 0},
        {"じゃかましい", "やかましい", 0}, {"あんさん", "あなた", 0}, {"はよ", "早く", 3},    {"ええ", "いい", 4},
    };
    for (const Word& w : kWords) {
        size_t p = pos;
        if (!Lit(s, p, w.written)) continue;
        if (w.where == 1 && !sentenceStart) continue;
        if ((w.where == 2 || w.where == 3 || w.where == 4) && afterKanji) continue;
        if (w.where == 2) {
            bool particle = false;
            // Not before a comma: おら、 is the rough call (おら、座れ 이봐, 앉아라).
            for (const std::string_view q : {"の", "は", "が", "も", "や", "に", "を"}) particle = particle || s.substr(p).starts_with(q);
            if (s.substr(p).starts_with("のう")) particle = false;  // わいのう: the call and its のう
            if (!particle) continue;
        }
        if (w.where == 3) {
            const char32_t n = p < s.size() ? At(s, p).cp : 0;
            if (!(IsKanjiClass(n) || s.substr(p).starts_with("せ") || s.substr(p).starts_with("し") || s.substr(p).starts_with("、") ||
                  s.substr(p).starts_with("来") || s.substr(p).starts_with("行"))) {
                continue;
            }
        }
        if (w.where == 4 && !(p < s.size() && (IsKanjiClass(At(s, p).cp) ||
                                               (IsKatakanaRune(At(s, p).cp) && !In(At(s, p).cp, U"ーッァィゥェォャュョ"))))) {
            continue;
        }
        // Opening the line, a sentence or a quote, ええ before an adverb, a word of manner, a verb or a person
        // is the yes of an answer, written with its comma (ええ確かに, ええ大丈夫よ, ええ勿論, ええ使いましたよ), not the good one
        // of western speech before a noun (ええ子, ええ塩梅じゃのう, ええ旦那さんだなあ).
        if (w.where == 4 && (head.empty() || In(b, U"。！？!?「『（(…‥"))) {
            bool yes = false;
            for (const std::string_view n :
                 {"確", "勿論", "無論", "本当", "本気", "然", "全く", "全然", "実は", "少し", "多分", "一応", "大丈夫", "結構", "変", "平気",
                  "分か", "判", "解", "知", "存じ", "承知", "了解", "構", "喜ん", "是非", "今", "必ず", "絶対", "当然", "云", "言", "使", "差上", "差し",
                  "吃驚", "行", "来", "待", "見", "聞", "思", "私", "僕", "俺", "彼", "何", "誰", "マジ", "ホント"}) {
                yes = yes || s.substr(p).starts_with(n);
            }
            if (yes) {
                // the yes is written apart from what it answers (ええ、確かに 네, 확실히)
                m.group[1] = "ええ、";
                m.end = p;
                return true;
            }
        }
        if (w.where == 1 && p < s.size() && IsHiraganaClass(At(s, p).cp) && !s.substr(p).starts_with("ど")) continue;
        m.group[1] = w.plain;
        m.end = p;
        return true;
    }
    return false;
}

// The Kansai ええ of いい before the ん or の of an explanation or a question and before やん, やろ
// (ほんまにええんか 정말 괜찮아?, ええのに, ええやん): いい. Not a drawn-out ええ (ねええ, ええええ).
bool GoodBeforeNoda(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "ええ")) return false;
    if (pos > 0 && In(LastRune(s.substr(0, pos)).cp, U"えぇーね")) return false;
    bool follows = false;
    for (const std::string_view f : {"んか", "んや", "んやろ", "んだ", "んです", "のか", "のに", "やん", "やろ", "ねん", "んね", "んよ", "のね", "のよ"}) {
        follows = follows || s.substr(p).starts_with(f);
    }
    if (!follows) return false;
    m.group[1] = "いい";
    m.end = p;
    return true;
}

// でがんす, がんす after a noun and ざます, ざんす of old polite speech: です.
bool OldPolite(std::string_view s, size_t pos, Match& m) {
    for (const std::string_view w : {"でがんす", "でごんす", "でごわす", "ざます", "ざんす"}) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        if (FinalParticleEnd(s, p) == std::string_view::npos && !s.substr(p).starts_with("か") && !s.substr(p).starts_with("っ")) continue;
        // でごんす and でごわす of sumo and Satsuma speech say their で, so they follow kana nouns too (ごっつぁんでごんす)
        const bool de = w == "でごんす" || w == "でごわす";
        if (pos == 0 || (!de && !CopulaFollows(s.substr(0, pos)))) return false;
        // not after an adjective or a predicate in い (呼ぶがいいでごんす), which the end rows read better without です
        if (de && s.substr(0, pos).ends_with("い")) return false;
        m.group[1] = "です";
        m.end = p;
        return true;
    }
    return false;
}

// The archaic copula 也 (なり), alone or with the や of a character's speech, before the end of the sentence or a
// quoting と (悪魔也 悪魔なり, 御免也や 御免なり, 愚か也や 愚かなり, 思った也や 思ったのだ, その通り也やと その通りだと)
// (spec LEX-5990). After a verb or an adjective it is のだ; before と or a sentence particle だ. Not where the kanji before it make a word with it (拓也, 達也), nor after one kanji (涼也).
bool ArchaicNari(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (pos == 0 || !Lit(s, p, "也")) return false;
    {
        size_t q = p;
        if (Lit(s, q, "や")) p = q;
    }
    if (FinalParticleEnd(s, p) == std::string_view::npos && !s.substr(p).starts_with("と")) return false;
    const std::string_view head = s.substr(0, pos);
    const char32_t last = LastRune(head).cp;
    if (IsKanjiClass(last)) {
        size_t start = pos;
        while (start > 0 && IsKanjiClass(LastRune(s.substr(0, start)).cp)) start -= LastRune(s.substr(0, start)).size;
        const std::string_view run = s.substr(start, pos - start);
        const size_t end = pos + std::string_view("也").size();
        // A word of two or more kanji before it takes the copula (真実也) unless the run with 也 is a word (拓也, 佐藤達也).
        if (IsWord(s.substr(start, end - start))) return false;
        // One kanji with 也 is a name the dictionary lacks (涼也と, おい冬也！, 弟の冬也。, 涼也や！), never the copula,
        // though the archaic 神也 shares its surface.
        if (start + LastRune(head).size == pos) return false;
        if (!IsWord(run)) {
            for (size_t at = start; at < pos; at += text::DecodeRune(s.substr(at)).size) {
                if (IsWord(s.substr(at, end - at))) return false;
            }
            // A run that is no word takes the copula only where it ends with a word of two kanji or more (大悪魔也); else
            // its last kanji with 也 is a name (田中涼也, 佐藤冬也と申します).
            bool tail = false;
            for (size_t at = start; at + LastRune(head).size < pos; at += text::DecodeRune(s.substr(at)).size) {
                tail = tail || IsWord(s.substr(at, pos - at));
            }
            if (!tail) return false;
            // Even then a clause frames it: a modifier in kana before it, a topic's は, こそ or ぞ before it (其は大悪魔也)
            // or the 也や of a character's speech, and the end of the sentence after it.
            const char32_t lead = start > 0 ? LastRune(s.substr(0, start)).cp : 0;
            const bool framed = IsHiraganaClass(lead) && !In(lead, U"がもをにへとでや");
            if (!framed || !EndAt(s, p) || (p < s.size() && In(At(s, p).cp, U"、，,"))) {
                if (!s.substr(pos + std::string_view("也").size()).starts_with("や")) return false;
            }
        }
        m.group[1] = "だ";
    } else if (IsKatakanaRune(last) || last == U'ー' || head.ends_with("か") || EndsWithAny(head, kKanaNouns)) {
        m.group[1] = "だ";
    } else if (In(last, U"うくすつぬふむゆるぐぶたい")) {
        m.group[1] = "のだ";
    } else {
        return false;
    }
    // At the end of the sentence after a noun it is the なり the old-speech rows read (悪魔也！ 悪魔なり 악마다!).
    if (m.group[1] == "だ" && EndAt(s, p) && !(p < s.size() && In(At(s, p).cp, U"、，,"))) m.group[1] = "なり";
    m.end = p;
    return true;
}

// Whether head ends with a predicate that says the same with the plain よ as with のよ: an adjective or ない and the
// contracted ている (決まってる, 疲れてる); not a verb in its dictionary form, where よ promises (するよ 할게), nor a past.
bool PlainBeforeYo(std::string_view head) {
    return head.ends_with("い") || head.ends_with("てる") || head.ends_with("でる");
}

// The forms of a character's speech that add a particle to an ending: the ending they stand for (spec LEX-5991),
// のよさ at the end (意外なのよさ 意外なのよ, 信じてるのよさ 信じてるよ), ですぞ and でしたぞ at the end, also drawn (もちろんですぞ もちろんです
// 물론입니다, 本物ですぞっ！; not before a drawl, ですぞ〜) and the doubled です after ません, でした and before か (分かりませんです 모릅니다). The doubled ますです at the
// end stays: its 습니다요 keeps the habit.
bool CharacterTic(std::string_view s, size_t pos, Match& m) {
    static constexpr std::string_view kTics[][2] = {{"のよさ", "よ"},           {"ですぞ", "です"},         {"でしたぞ", "でした"},
                                                     {"ませんです", "ません"}, {"でしたです", "でした"}, {"ますですか", "ますか"},
                                                     {"ましたですか", "ましたか"}, {"ませんのです", "ません"}, {"ましたのです", "ました"},
                                                     {"ますのです", "ます"},     {"でしゅ", "でちゅ"},       {"でつ", "でちゅ"},
                                                     {"まちゅ", "ます"},         {"ましゅ", "ます"}};
    for (const auto& t : kTics) {
        size_t p = pos;
        if (!Lit(s, p, t[0])) continue;
        // The のです of a polite ending and the です and ます of baby talk end the same before a sentence particle, which
        // stays (いけませんのですか いけませんか, 大丈夫でつか). でしゅ and でつ are the でちゅ the ending table reads
        // (大丈夫でつ 괜찮아요); まちゅ and ましゅ are ます.
        size_t end = p;
        // the emphatic よ goes with the のです it follows (担ってまいりましたのですよ 担ってまいりました); ね and か stay
        if (t[0].ends_with("のです") && s.substr(p).starts_with("よ")) end += std::string_view("よ").size();
        const bool baby = t[0] == "でしゅ" || t[0] == "でつ" || t[0] == "まちゅ" || t[0] == "ましゅ";
        const bool particle = baby || t[0].ends_with("のです");
        if (particle) {
            while (p < s.size() && In(At(s, p).cp, U"よねかわ")) p += At(s, p).size;
            // drawn baby talk is read by the ending table (えらいでちゅねーっ 대단하네요)
            if (baby && p < s.size() && In(At(s, p).cp, U"ー～〜っッぇ")) return false;
            // baby talk after what the copula or ます follows: a word, not a particle (手でつかむ, 家でつ is rare)
            if (baby && (pos == 0 || In(LastRune(s.substr(0, pos)).cp, U"、。！？「 　"))) return false;
        }
        // ですぞ drawn or cut short (ですぞっ！, ですぞぉ, ですぞー) and before a closing 》 ends the same.
        if (t[0].ends_with("ぞ")) {
            while (p < s.size() && In(At(s, p).cp, U"っッぉォ")) p += At(s, p).size;
            // A playful wave after it (こいみたいですぞ〜) keeps the reading of the word stage.
            if (p < s.size() && In(At(s, p).cp, U"〜～")) return false;
        }
        // Before a conjunction the のです goes the same (存じませんのですが 存じませんが, しますのですけど).
        const std::string_view rest = s.substr(p);
        const bool conjunction = t[0].ends_with("のです") && p == end &&
                                 (rest.starts_with("が") || rest.starts_with("けど") || rest.starts_with("けれど"));
        if (!conjunction && !EndAt(s, p) && !(p < s.size() && In(At(s, p).cp, U"》≫〉>"))) return false;
        // のよさ follows a predicate or the な of the copula; after a noun it is の and the noun よさ (質のよさ).
        if (t[0] == "のよさ" && (pos == 0 || !In(LastRune(s.substr(0, pos)).cp, U"うくすぬむるぐぶたいな"))) return false;
        if (t[0] == "のよさ" && s.substr(0, pos).ends_with("な") &&
            !CopulaFollows(s.substr(0, pos - std::string_view("な").size())) && !s.substr(0, pos).ends_with("かな")) {
            return false;
        }
        // The plain よ only after an adjective or ない and the contracted ている (信じてるのよさ 信じてるよ); the copula な, a
        // verb in its dictionary form or a past and a question keep the の (早くするのよさ 早くするのよ, 何をするのよさ？).
        // A set phrase of the dictionary of four letters or more before it (行ってくる 다녀올게, どうって事ない) is read whole
        // and ends the sentence itself: the plain よ.
        bool phrase = false;
        for (size_t at = pos, n = 0; at > 0 && n < 8 && !phrase; ++n) {
            at -= LastRune(s.substr(0, at)).size;
            phrase = n >= 3 && IsWord(s.substr(at, pos - at));
        }
        if (t[0] == "のよさ" && !phrase && (!PlainBeforeYo(s.substr(0, pos)) || QuestionBefore(s, pos))) {
            m.group[1] = "のよ";
            m.end = p;
            return true;
        }
        m.group[1] = t[1];
        m.end = particle ? end : p;
        return true;
    }
    return false;
}

// The Kansai まう of しまう after a て form, and its まった, まいそう and imperative まえ (忘れてまう 忘れてしまう, 死んでまうで,
// 曲がってまった, 行ってまえ) (spec LEX-5994). で only as the て form after ん or い (死んでまう), not the particle (家でまう). Not the
// adverb まったく (なんてまったく). The match starts at the ま.
bool KansaiMau(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    const std::string_view head = s.substr(0, pos);
    if (head.ends_with("で")) {
        const std::string_view before = head.substr(0, head.size() - std::string_view("で").size());
        if (before.empty() || !In(LastRune(before).cp, U"んい")) return false;
    } else if (!head.ends_with("て")) {
        return false;
    }
    static constexpr std::string_view kForms[][2] = {{"まう", "しまう"}, {"まった", "しまった"}, {"まいそう", "しまいそう"}, {"まえ", "しまえ"}};
    for (const auto& [w, plain] : kForms) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        if (w == "まった" && s.substr(p).starts_with("く")) return false;
        // The imperative まえ ends the sentence after the て form of a verb (行ってまえ！ 가버려라!), not the noun 手前 or お点前 in kana.
        if (w == "まえ") {
            const std::string_view te = head.substr(0, head.size() - std::string_view("て").size());
            const char32_t c = te.empty() ? 0 : LastRune(te).cp;
            if (FinalParticleEnd(s, p) == std::string_view::npos || !(IsKanjiClass(c) || In(c, U"っいきしちにひみりえけせてねへめれげべんぎじびぃ"))) return false;
        }
        m.group[1] = plain;
        m.end = p;
        return true;
    }
    return false;
}

// The Kansai すん of する before ねん, ねや and a で that ends the sentence (勉強すんねん 勉強するねん, 案内すんで
// 案内するで, どうすんねや) (spec LEX-5992). Not the すん of a verb in kana (休んで, 進んで, 済んで before a comma).
bool KansaiSuru(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "すん")) return false;
    if (pos > 0) {
        const char32_t before = LastRune(s.substr(0, pos)).cp;
        if (IsHiraganaClass(before) && !In(before, U"をがはもにう") && !s.substr(0, pos).ends_with("どない")) return false;
        if (before == U'う' && !EndsWithAny(s.substr(0, pos), std::array<std::string_view, 3>{"どう", "こう", "そう"})) return false;
    }
    size_t q = p;
    if (Lit(s, q, "ねん") || Lit(s, q, "ねや")) {
        m.group[1] = "する";
        m.end = p;
        return true;
    }
    // After a subject's が the すんで that ends the sentence is 済んで (気がすんで 기분이 풀려, 用がすんで).
    if (s.substr(0, pos).ends_with("が")) return false;
    q = p;
    if (!Lit(s, q, "で") || !EndAt(s, q) || (q < s.size() && In(At(s, q).cp, U"、，,"))) return false;
    m.group[1] = "する";
    m.end = p;
    return true;
}

// The ん of an explanation said for の before a final よ or ね in western and old speech, after a verb or an adjective
// and in なん after a noun (そうなんね そうなのね, 誰が守るんよ 誰が守るのよ); before a よ that tells it is the plain
// predicate after an adjective, ない or てる (決まってるんよ 決まってるよ, 得意なんよ 得意だよ) (spec LEX-5993). Not the negative ん of a verb (知らんよ, 行かんね), which no predicate ends before.
bool WesternNoEnd(std::string_view s, size_t pos, Match& m) {
    // Every match is なん or ん, then よ or ね where the sentence ends.
    {
        size_t q = pos;
        if (!Lit(s, q, "なん") && !Lit(s, q, "ん")) return false;
        if (!(Lit(s, q, "よ") || Lit(s, q, "ね")) || !EndAt(s, q) || (q < s.size() && In(At(s, q).cp, U"、，,"))) return false;
    }
    size_t p = pos;
    const std::string_view head = s.substr(0, pos);
    std::string_view plain;
    if (Lit(s, p, "なん")) {
        if (!CopulaFollows(head) && !head.ends_with("か")) return false;
        plain = "なの";
    } else {
        p = pos;
        // The Kansai ええ is the adjective いい (ええんよ ええよ 좋아).
        const bool ee = head.ends_with("ええ");
        if (!Lit(s, p, "ん") || head.empty() || (!In(LastRune(head).cp, U"うくすつぬぶむるぐたい") && !ee)) return false;
        // Not the negative ん after the た of a godan verb in つ (勝たんよ 勝たないよ, 待たんね).
        if (LastRune(head).cp == U'た') {
            const std::string_view stem = StemBefore(s, pos - LastRune(head).size);
            if (!stem.empty() && VerbIs(Cat({stem, "つ"}))) return false;
        }
        // Not a word in kana of three kana or more that ends in ん (たぶん, ごめん, ぜんぜん); a shorter one is the end
        // of a verb (なったん, not たん; しまうん, not うん).
        // A run after a kanji is the kana of that word (違うん, 知らなかったん, not うん, かったん).
        size_t kana = 1;
        size_t start = pos;
        while (start > 0 && IsHiraganaClass(LastRune(s.substr(0, start)).cp)) {
            start -= LastRune(s.substr(0, start)).size;
            ++kana;
        }
        if (kana >= 3 && !(start > 0 && IsKanjiClass(LastRune(s.substr(0, start)).cp))) {
            // Every kana here is three bytes: test the tails of three kana or more (たぶん, ごめん, ぜんぜん).
            for (size_t at = start; p - at >= 3 * std::string_view("ん").size(); at += std::string_view("ん").size()) {
                if (IsWord(s.substr(at, p - at))) return false;
            }
        }
        // One kana before ん where the clause starts is a word of its own (うんよ 응), not the end of a verb.
        if (pos == LastRune(head).size || In(LastRune(head.substr(0, pos - LastRune(head).size)).cp, U"「『（(　 、，,。！？!?…‥―─～")) return false;
        // The す of the Kansai する said す (どうすんよ) is read by the word stage; a godan verb in す keeps its kanji.
        if (LastRune(head).cp == U'す' && (head.size() == LastRune(head).size || !IsKanjiClass(LastRune(head.substr(0, head.size() - LastRune(head).size)).cp))) return false;
        plain = "の";
    }
    // Not an honorific in ん (筧くんね, ゆうくんよ); one that starts with た is the past before it (行ったんよ, not たん).
    // Nor a く that ends a verb of the dictionary (行くんよ, not くん).
    if (const size_t h = dict::EndsWithMember(HDTRANS_CLASS("honorific.constraint-familiar"), s.substr(0, p));
        h > 0 && !s.substr(p - h).starts_with("た") && !(plain == "の" && VerbIs(StemBefore(s, pos)))) {
        return false;
    }
    size_t q = p;
    const bool yo = s.substr(p).starts_with("よ");
    if (!(Lit(s, q, "よ") || Lit(s, q, "ね")) || !EndAt(s, q) || (q < s.size() && In(At(s, q).cp, U"、，,"))) return false;
    // Before よ that tells, the explanation is the plain predicate (決まってるんよ 決まってるよ, 得意なんよ 得意だよ); a question
    // keeps it (誰が守るんよ 誰が守るのよ).
    if (yo && !QuestionBefore(s, pos) && (plain == "なの" || PlainBeforeYo(head) || head.ends_with("ええ"))) plain = plain == "なの" ? "だ" : "";
    m.group[1] = plain;
    m.end = p;
    return true;
}

// Honorific and humble forms read as the forms the dictionary reads (spec LEX-4765).

// Whether the verb in its て form before pos (the て or で at pos - 1 not counted) moves somewhere: its
// ておいで and ていらした are the coming of 来る (帰っておいで, 戻っていらした), not the ている they honour.
bool MotionBefore(std::string_view head, bool entering) {
    return dict::EndsWithMember(HDTRANS_CLASS("stem.te-moving"), head) > 0 ||
           (entering && dict::EndsWithMember(HDTRANS_CLASS("stem.te-entering"), head) > 0);
}

// The て or で right before pos and the text before it.
bool TeBefore(std::string_view s, size_t pos, std::string_view& head) {
    const std::string_view before = s.substr(0, pos);
    if (!before.ends_with("て") && !before.ends_with("で")) return false;
    const bool de = before.ends_with("で");
    head = before.substr(0, before.size() - std::string_view("て").size());
    if (head.empty() || EndAt(head, head.size() - LastRune(head).size)) return false;
    // で is a て form after ん or い only (読んで, 泳いで; not the copula of 上手でいらした or 全力で参ります);
    // て follows a kanji or a kana a verb stem ends in (待っ, 書い, 話し, 見せ, 待って), not a particle or another
    // kana (それをてらして 照らして).
    const char32_t last = LastRune(head).cp;
    if (de) return last == U'ん' || last == U'い';
    return IsKanjiClass(last) || In(last, U"っいしえけげせぜてねべぺめれきぎじちひびみり");
}

// The contracted てる of a て form before the んだし or のだし of a reason (寝てるんだし, 断ってるんだし):
// ている, whose 있으니까 the dictionary reads there, where the contracted form split into 자고 있어 해.
bool ContractedTeiru(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    size_t p = pos;
    std::string_view to;
    if (Lit(s, p, "てる")) {
        to = "ている";
    } else if (Lit(s, p = pos, "でる")) {
        to = "でいる";
    } else {
        return false;
    }
    const std::string_view head = s.substr(0, pos);
    const char32_t last = LastRune(head).cp;
    const bool de = to.starts_with("で");
    if (de ? !(last == U'ん' || last == U'い') : !(IsKanjiClass(last) || In(last, U"っいしえけげせぜてねべぺめれきぎじちひびみり"))) return false;
    // an ichidan verb whose stem ends in て or で (捨てる, 勝てる, 育てる, 茹でる) is no contraction: the verb's
    // own stem before the て (its kanji and the kana after them, or its kana alone) and the てる make a verb
    std::string_view stem = StemBefore(s, pos);
    if (stem.empty()) {
        size_t start = pos;
        while (start > 0 && IsHiraganaClass(LastRune(s.substr(0, start)).cp)) start -= LastRune(s.substr(0, start)).size;
        stem = s.substr(start, pos - start);
    }
    if (!stem.empty() && VerbIs(Cat({stem, s.substr(pos, p - pos)}))) return false;
    // nor the potential in てる of a godan verb in つ right after its kanji (勝てる of 勝つ, 待てる of 待つ)
    if (!stem.empty() && IsKanjiClass(LastRune(stem).cp) && !de && VerbIs(Cat({stem, "つ"}))) return false;
    bool follows = false;
    for (const std::string_view f : {"んだし", "のだし"}) {
        // not the ですから of a reason (ついてるんですから)
        follows = follows || (s.substr(p).starts_with(f) && !s.substr(p + f.size()).starts_with("ら"));
    }
    if (!follows) return false;
    m.group[1] = to;
    m.end = p;
    return true;
}

// The 言っておくれでない that forbids a saying (つまらない事を言っておくれでない): 言わないでくれ, whose
// 말하지 말아 줘 the dictionary reads; the て form of 言う (stem.te-saying) has its negative in わ.
bool SayingForbidden(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "っておくれでない") || s.substr(p).starts_with("か")) return false;
    if (dict::EndsWithMember(HDTRANS_CLASS("stem.te-saying"), Cat({s.substr(0, pos), "っ"})) == 0) return false;
    m.group[1] = "わないでくれ";
    m.end = p;
    return true;
}

// The おくれ of an old request after a て form (待っておくれ, 貸しておくれでないか): くれ. Not おくれる (be late).
bool OldRequest(std::string_view s, size_t pos, Match& m) {
    std::string_view head;
    size_t p = pos;
    if (!Lit(s, p, "おくれ") || !TeBefore(s, pos, head)) return false;
    size_t q = p;
    if (Lit(s, q, "でない")) {
        // おくれでないか asks for it (貸しておくれでないか 빌려 주지 않겠는가); おくれでない alone forbids it,
        // the くれるな of an old request (泣いておくれでない 울지 말아 다오)
        const bool asks = s.substr(q).starts_with("か");
        // 言っておくれでない is read by SayingForbidden
        if (!asks && dict::EndsWithMember(HDTRANS_CLASS("stem.te-saying"), head) > 0) return false;
        m.group[1] = asks ? "くれない" : "くれるな";
        m.end = q;
        return true;
    }
    // After a negative ないで it is the くれ of a request not to act, wherever it stands (言わないでおくれ 말하지 말아 줘;
    // spec LEX-5050), but for the おくれなさい of the ending table and the 遅れ of a verb that goes on (寝ないでおくれた).
    if (head.ends_with("ない") && !s.substr(p).starts_with("なさ") && !In(p < s.size() ? At(s, p).cp : 0, U"たてそるま")) {
        m.group[1] = "くれ";
        m.end = p;
        return true;
    }
    // At the end of a sentence or a quote the ending table reads it (待っておくれ。 기다려 다오); inside one,
    // before a comma, a space or な, ね, さ (not the おくれなさい of the ending table), it is read here.
    size_t r = p;
    if (s.substr(r).starts_with("よ")) r += std::string_view("よ").size();
    const char32_t next = r < s.size() ? At(s, r).cp : 0;
    if (!In(next, U"、，, 　なねさ") || s.substr(r).starts_with("なさ")) return false;
    m.group[1] = "くれ";
    m.end = p;
    return true;
}

// The ておいで of a state the speaker honours (持っておいでです, 覚えておいでですか, 陣取っておいでで、, 考えておいでになる)
// before a copula or になる: ておられる. Not after a verb that moves (帰っておいでです is coming back), nor the
// order to come (出ておいで, 見ておいでなさい).
bool HonouredState(std::string_view s, size_t pos, Match& m) {
    std::string_view head;
    size_t p = pos;
    if ((!Lit(s, p, "おいで") && !Lit(s, p, "お出で")) || !TeBefore(s, pos, head) || MotionBefore(head, true)) return false;
    // ておいで after a verb of fetching is going and coming back (買っておいでになった 사 오셨다), and 喜んで is
    // the adverb (喜んでおいででしょう).
    if (dict::EndsWithMember(HDTRANS_CLASS("stem.te-fetching"), head) > 0) return false;
    static constexpr std::string_view kForms[][2] = {
        {"ですか", "おられますか"},   {"でした", "おられました"}, {"です", "おられます"},     {"でしょう", "おられるでしょう"},
        {"だった", "おられた"},       {"なの", "おられるの"},     {"なん", "おられるん"},     {"なさいます", "おられます"},
        {"なさった", "おられた"},     {"なさる", "おられる"},     {"になります", "おられます"}, {"になりました", "おられました"},
        {"になった", "おられた"},     {"になって", "おられて"},   {"になる", "おられる"},     {"で、", "おられて、"},
        {"だ", "おられる"},           {"で", "おられて"},
    };
    for (const auto& f : kForms) {
        size_t q = p;
        // a word of the dictionary that starts before the お and covers the form reads it (いかがお過ごしでしょうか)
        if (!Lit(s, q, f[0]) || (pos > 0 && WordCovers(s, pos - LastRune(s.substr(0, pos)).size, q))) continue;
        if (f[0] == "だ" && !EndAt(s, q) && FinalParticleEnd(s, q) == std::string_view::npos) continue;
        if (f[0] == "で" && !EndAt(s, q)) continue;
        m.group[1] = f[1];
        m.end = q;
        return true;
    }
    return false;
}

// おいで and お出で of いらっしゃる standing alone before a copula (毎年おいでですよ, ここにお出でです): いらっしゃる.
bool HonouredComing(std::string_view s, size_t pos, Match& m) {
    std::string_view head;
    if (TeBefore(s, pos, head)) return false;
    size_t p = pos;
    if (!Lit(s, p, "おいで") && !Lit(s, p, "お出で")) return false;
    static constexpr std::string_view kForms[][2] = {{"ですか", "いらっしゃいますか"}, {"でした", "いらっしゃいました"}, {"です", "いらっしゃいます"},
                                                       {"でしょう", "いらっしゃるでしょう"}, {"だった", "いらっしゃった"}, {"なの", "いらっしゃるの"}};
    for (const auto& f : kForms) {
        size_t q = p;
        // a word of the dictionary that starts before the お and covers the form reads it (いかがお過ごしでしょうか)
        if (!Lit(s, q, f[0]) || (pos > 0 && WordCovers(s, pos - LastRune(s.substr(0, pos)).size, q))) continue;
        m.group[1] = f[1];
        m.end = q;
        return true;
    }
    return false;
}

// The (い)らして and (い)らした of いらっしゃる after a て form of a state (見ていらした, 入ってらして): ておられ.
bool HonouredStay(std::string_view s, size_t pos, Match& m) {
    std::string_view head;
    size_t p = pos;
    // only the contracted らし (見てらした); the full ていらした is also the てきた of an honoured doer (仲良くして
    // いらした 지내 오셨다), which the dictionary reads
    if (!Lit(s, p, "らし")) return false;
    if (!TeBefore(s, pos, head) || MotionBefore(head, false)) return false;
    if (p >= s.size() || !In(At(s, p).cp, U"てた")) return false;
    // the old spelling ぢや after it does not read after ておられた
    if (s.substr(p + At(s, p).size).starts_with("ぢ")) return false;
    // いらしてください and いらしてね ask someone to come or to stay (後でいらしてください).
    for (const std::string_view ask : {"てください", "て下さい", "てくださ", "て下さ", "てね", "てちょうだい"}) {
        if (s.substr(p).starts_with(ask)) return false;
    }
    m.group[1] = "おられ";
    m.end = p;
    return true;
}

// The まいる of humble speech after a て form (寄ってまいります, なってまいりました): くる. 行ってまいります
// is its own greeting.
bool HumbleCome(std::string_view s, size_t pos, Match& m) {
    std::string_view head;
    if (!TeBefore(s, pos, head) || dict::EndsWithMember(HDTRANS_CLASS("stem.te-mairu-own"), head) > 0) return false;
    // A coming to the speaker's side said humbly (寄ってまいります 다가옵니다, 迫ってまいりました); in the rest,
    // where the dictionary reads てまいります as the going on of ていく (頑張って参りましょう 힘내 갑시다), it stays.
    if (dict::EndsWithMember(HDTRANS_CLASS("stem.te-coming-humble"), head) == 0) return false;
    static constexpr std::string_view kForms[][2] = {{"まいり", "き"}, {"まいる", "くる"}, {"まいっ", "き"}, {"参り", "き"}, {"参る", "くる"}, {"参っ", "き"}};
    for (const auto& f : kForms) {
        size_t p = pos;
        if (!Lit(s, p, f[0])) continue;
        if (s.substr(p).starts_with("ましょ") || s.substr(p).starts_with("ませう")) return false;
        m.group[1] = f[1];
        m.end = p;
        return true;
    }
    return false;
}

// The ていかれ in kana of the respectful (ら)れ of 行く (帰っていかれる): て行かれ, which the voice of the word
// stage reads (先生が帰って行かれた 돌아가셨다) where the kana word いかれ reads 갈 수 있다.
bool HonouredGoing(std::string_view s, size_t pos, Match& m) {
    std::string_view head;
    size_t p = pos;
    if (!Lit(s, p, "いかれ") || !TeBefore(s, pos, head)) return false;
    if (p < s.size() && In(At(s, p).cp, U"なずぬん")) return false;
    // only with an honoured subject before it in the sentence (先生が帰っていかれる); without one the kana
    // いかれ is as likely the passive (連れていかれる) or the potential
    std::string_view sentence = head;
    for (const std::string_view stop : {"。", "！", "？", "「", "」"}) {
        const size_t at = sentence.rfind(stop);
        if (at != std::string_view::npos) sentence = sentence.substr(at + stop.size());
    }
    bool honoured = false;
    for (const std::string_view particle : {"が", "は", "も"}) {
        for (size_t at = sentence.find(particle); !honoured && at != std::string_view::npos; at = sentence.find(particle, at + particle.size())) {
            honoured = dict::EndsWithMember(HDTRANS_CLASS("honorific.honoured-person"), sentence.substr(0, at)) > 0;
        }
    }
    if (!honoured) return false;
    m.group[1] = "行かれ";
    m.end = p;
    return true;
}

// The てもらえない of a statement (見せてもらえなかった, 愛してもらえない): てくれない, what the other did not do
// for one (보여 주지 않았다). A question or a request keeps it (待ってもらえないか, 席を外してもらえませんか).
bool UngivenFavour(std::string_view s, size_t pos, Match& m) {
    std::string_view head;
    size_t p = pos;
    if (!Lit(s, p, "もらえ") || !TeBefore(s, pos, head)) return false;
    // 聞いてくれない reads its 聞く as asking (물어 주다), not the listening that 聞いてもらえない is; the verbs
    // whose Korean takes 받다 (理解してもらえない 이해받지 못하다, 認めて 인정받다, 許して 허락받다) and わかって
    // (알아주다) are read by the dictionary.
    if (dict::EndsWithMember(HDTRANS_CLASS("stem.te-favour-read"), head) > 0) return false;
    // the て form of する (理解して, お供えして, して): a godan verb in す (貸して, 返して) is no する
    if (head.ends_with("し")) {
        const std::string_view stem = head.substr(0, head.size() - std::string_view("し").size());
        std::string_view word = stem;
        while (!word.empty() && !EndAt(word, word.size() - LastRune(word).size) && !In(LastRune(word).cp, U"をがはにもとへのや")) {
            word.remove_suffix(LastRune(word).size);
        }
        const std::string_view verbStem = stem.substr(word.size());
        if (verbStem.empty() || !VerbIs(Cat({verbStem, "す"}))) return false;
    }
    static constexpr std::string_view kForms[][2] = {{"ませんでした", "くれませんでした"}, {"ません", "くれません"}, {"なかった", "くれなかった"},
                                                       {"なくて", "くれなくて"}, {"なく", "くれなく"}, {"ない", "くれない"}, {"ず", "くれず"}};
    for (const auto& f : kForms) {
        size_t q = p;
        // a word of the dictionary that starts at the last kana of the て form and covers the form reads it
        if (!Lit(s, q, f[0]) || (pos > 0 && WordCovers(s, pos - LastRune(s.substr(0, pos)).size, q))) continue;
        const std::string_view rest = s.substr(q);
        for (const std::string_view ask : {"か", "？", "?", "だろうか", "でしょうか", "かな", "かしら", "かね", "の？", "の?"}) {
            if (rest.starts_with(ask)) return false;
        }
        if (f[0] == "ません" && !EndAt(s, q) && FinalParticleEnd(s, q) == std::string_view::npos) return false;
        // with the giver in に in its clause (親に構ってもらえない) the に would need to become the subject
        std::string_view clause = head;
        for (const std::string_view stop : {"、", "。", "！", "？", " ", "　", "「"}) {
            const size_t at = clause.rfind(stop);
            if (at != std::string_view::npos) clause = clause.substr(at + stop.size());
        }
        if (clause.find("に") != std::string_view::npos) return false;
        // ない right before a closing bracket may be a request said without か (置いてもらえない」)
        if (f[0] == "ない" && (rest.starts_with("」") || rest.starts_with("』"))) return false;
        m.group[1] = f[1];
        m.end = q;
        return true;
    }
    return false;
}

// The verb stem after the お at pos: base, up to two kana of okurigana (お分かり, お決まり, お出かけ) and the i-row
// or e-row kana that ends the stem of a verb of the dictionary. p is set after the stem, row to the i-row index of
// its last kana (npos for the e-row of an ichidan stem), base to the stem before that kana, godan to whether the verb is
// a godan verb.
bool HonouredStem(std::string_view s, size_t pos, size_t& p, std::string_view& base, std::string_view& stem, bool& godan) {
    p = pos;
    if (!Lit(s, p, "お")) return false;
    const size_t stemStart = p;
    while (p < s.size() && IsKanjiClass(At(s, p).cp)) p += At(s, p).size;
    if (p == stemStart || p >= s.size()) return false;
    const std::u32string_view iRow = U"いきぎしちにびみり", uRow = U"うくぐすつぬぶむる", eRow = U"えけげせてねべめれ";
    for (int kana = 0; kana < 2 && p + At(s, p).size < s.size() && IsHiraganaClass(At(s, p).cp) &&
                       iRow.find(At(s, p).cp) == std::u32string_view::npos && eRow.find(At(s, p).cp) == std::u32string_view::npos;
         kana++) {
        p += At(s, p).size;
    }
    const char32_t k = At(s, p).cp;
    const std::size_t row = iRow.find(k);
    if (row == std::u32string_view::npos && eRow.find(k) == std::u32string_view::npos) return false;
    base = s.substr(stemStart, p - stemStart);
    p += At(s, p).size;
    stem = s.substr(stemStart, p - stemStart);
    // an adjective in い (お強いですね) is no stem
    if (k == U'い' && AdjectiveIs(base)) return false;
    godan = false;
    if (VerbIs(Cat({stem, "る"}))) return true;
    if (row == std::u32string_view::npos) return false;
    std::string dictionary(base);
    text::AppendRune(dictionary, uRow[row]);
    godan = VerbIs(dictionary);
    return godan;
}

// お and the stem of a dictionary verb before a copula, the state the speaker honours (お帰りですか, お立ちです,
// お過ごしで、): お and the stem before になる, a question in ですか the present のですか (お急ぎですか
// お急ぎになるのですか 서두르십니까, not the future 서두르시겠습니까). Not the nouns made of a stem (お願いです, お返しです,
// お疲れですか), nor お持ち, which reads as written.
bool HonouredStemCopula(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    std::string_view base, stem;
    bool godan = false;
    if (!HonouredStem(s, pos, p, base, stem, godan)) return false;
    if (dict::InClass(HDTRANS_CLASS("noun.o-stem"), stem)) return false;
    // お呼びですか asks whether one has called (부르셨습니까) and お見えです says someone has come (오셨습니다);
    // the でしょうか of such a stem stays.
    const bool past = dict::InClass(HDTRANS_CLASS("stem.o-past"), stem);
    if (past && s.substr(p).starts_with("でしょう")) return false;
    static constexpr std::string_view kForms[][2] = {
        {"でしたか", "になりましたか"}, {"でした", "になりました"}, {"ですか", "になるのですか"}, {"です", "になります"},
        {"でしょう", "になるでしょう"}, {"だった", "になった"},     {"なの", "になるの"},       {"なん", "になるん"},
        {"で、", "になって、"},         {"で…", "になって…"},       {"だと", "になると"},     {"だ", "になる"},
    };
    for (const auto& f : kForms) {
        size_t q = p;
        // a word of the dictionary that starts before the お and covers the form reads it (いかがお過ごしでしょうか)
        if (!Lit(s, q, f[0]) || (pos > 0 && WordCovers(s, pos - LastRune(s.substr(0, pos)).size, q))) continue;
        if (f[0] == "だ" && !EndAt(s, q) && FinalParticleEnd(s, q) == std::string_view::npos && !s.substr(q).starts_with("から") &&
            !s.substr(q).starts_with("が") && !s.substr(q).starts_with("けど")) {
            continue;
        }
        m.group[1] = stem;
        m.group[2] = past && f[0] == "ですか"                                                        ? std::string_view("になりましたか")
                     : f[0] == "です" && dict::InClass(HDTRANS_CLASS("stem.o-arrived"), stem) ? std::string_view("になりました")
                                                                                              : f[1];
        m.end = q;
        return true;
    }
    return false;
}

// The old request くださいまし, 下さいまし is ください, 下さい (待って下さいまし 기다려 주세요), but for the まして, ました of
// ます after ください (くださいました, くださいましょう) and the ないでくださいまし of the ending table.
bool OldRequestMashi(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    const bool kana = Lit(s, p, "くださいまし");
    if (!kana && !Lit(s, p, "下さいまし")) return false;
    if ((p < s.size() && In(At(s, p).cp, U"てたょ")) || s.substr(0, pos).ends_with("ないで")) return false;
    m.group[1] = kana ? "ください" : "下さい";
    m.end = p;
    return true;
}

// お and a verb stem before ください, 下さい or くださいませ, 下さいませ ask the listener to do it: the て form of the
// verb (お使いください 使ってください 써 주세요, お座りください 座ってください 앉아 주세요; spec LEX-5051), so
// that a noun made of the stem (お通り 대로, お使い 심부름) does not take the request. Not where a word of the dictionary
// covers the request (the phrase rows お休みください 쉬십시오, お通りください 지나가세요).
bool HonouredStemRequest(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    std::string_view base, stem;
    bool godan = false;
    // only where a word of the dictionary reads the stem, a noun that would take the request (お通り 대로, お使い 심부름)
    // a stutter of the お before it (お、お降りください) copies the word as written
    if (!HonouredStem(s, pos, p, base, stem, godan) || s.substr(0, pos).ends_with("お、") || s.substr(0, pos).ends_with("お…")) {
        return false;
    }
    const size_t stemAt = pos + std::string_view("お").size();
    if (!WordCovers(s, stemAt, p)) return false;
    // nor a causative in せ after an a-row kana (お聞かせください), whose て form the dictionary reads apart
    if (stem.ends_with("せ") && stem.size() > std::string_view("せ").size()) {
        const char32_t before = LastRune(stem.substr(0, stem.size() - std::string_view("せ").size())).cp;
        if (In(before, U"かがさざただなばまらわ")) return false;
    }
    static constexpr std::string_view kRequests[][2] = {
        {"くださいませ", "くださいませ"}, {"下さいませ", "下さいませ"}, {"ください", "ください"}, {"下さい", "下さい"},
    };
    for (const auto& r : kRequests) {
        size_t q = p;
        if (!Lit(s, q, r[0]) || WordCovers(s, pos, q)) continue;
        m.group[1] = godan ? base : stem;
        m.group[2] = "て";
        if (godan) {
            // the て form of a godan verb by the last kana of its stem
            static constexpr std::pair<char32_t, std::string_view> kTe[] = {
                {U'い', "って"}, {U'ち', "って"}, {U'り', "って"}, {U'き', "いて"}, {U'ぎ', "いで"},
                {U'し', "して"}, {U'に', "んで"}, {U'び', "んで"}, {U'み', "んで"},
            };
            const char32_t last = LastRune(stem).cp;
            for (const auto& [kana, te] : kTe) {
                if (kana == last) m.group[2] = te;
            }
            if (base.ends_with("行") && last == U'き') m.group[2] = "って";
        }
        // Not where the て form is a word of its own (改めて, an adverb), nor where an idiom of the object before を and
        // the verb would take it (力をお貸しください, 心をお許しください: 力を貸, 心を許).
        // WordCovers looks the given text up itself, so it takes these built strings: the て form alone, and the text
        // before the お with the verb, where a key that starts before the を (力を貸, 心を許) reaches the verb.
        const std::string te = Cat({m.group[1], m.group[2]});
        if (WordCovers(te, 0, te.size())) return false;
        if (s.substr(0, pos).ends_with("を")) {
            const std::string idiom = Cat({s.substr(0, pos), base});
            if (WordCovers(idiom, pos - std::string_view("を").size(), idiom.size())) return false;
        }
        m.group[3] = r[1];
        m.end = q;
        return true;
    }
    return false;
}
}  // namespace

// The Kansai ちゃう of denial, 違う, where nothing stands before it that a verb stem could end in: the start of a
// sentence, a mark, a space or the particle は, が or も (それはちゃう 그건 아니야, それ、ちゃうやん, 俺がちゃう
// ねん), and after the の of a noun, where it is the negative copula じゃない (俺のちゃうで 내 거 아니야; after ん the
// dictionary reads it, 行くんちゃう？). Its forms: ちゃう, the past ちゃうかった (違った), the polite ちゃいます, ちゃいました, ちゃいません (違います);
// before the end, a sentence particle, と of a thought or what Kansai speech says after it (ちゃうやん, ちゃうか, ちゃうん
// や), and the で of the end said for よ (それはちゃうで 違うよ). At a mark the dictionary reads ちゃうちゃう, ちゃうで and
// ちゃうねん (아니야). After a verb stem ちゃう is てしまう (食べちゃう) and is read elsewhere.
bool KansaiChau(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    bool mark = head.empty();
    bool copula = false;
    if (!head.empty()) {
        const char32_t b = LastRune(head).cp;
        mark = In(b, U"。｡！!？?「『（(、，, 　…‥―ー");
        // は, が or も after more kana: not the を-less stem of a verb (おもちゃう is no word, but take no chance with kana
        // before a particle that ends in a kana a stem ends in: only は, が, も).
        const bool particle = In(b, U"はがも") && head.size() > LastRune(head).size;
        // the の of a noun after a word (俺の)
        copula = b == U'の' && head.size() > LastRune(head).size;
        if (!mark && !particle && !copula) return false;
    }
    if (s.substr(pos).starts_with("ちゃうちゃう")) return false;
    size_t p = pos;
    std::string_view plain;
    if (Lit(s, p, "ちゃいます")) {
        plain = copula ? "じゃないです" : "違います";
    } else if (Lit(s, p = pos, "ちゃいました")) {
        plain = copula ? "じゃなかったです" : "違いました";
    } else if (Lit(s, p = pos, "ちゃいません")) {
        plain = copula ? "じゃないです" : "違いません";
    } else if (Lit(s, p = pos, "ちゃうかった")) {
        plain = copula ? "じゃなかった" : "違った";
    } else if (Lit(s, p = pos, "ちゃう")) {
        plain = copula ? "じゃない" : "違う";
    } else {
        return false;
    }
    if (mark && plain == "違う" && (s.substr(p).starts_with("で") || s.substr(p).starts_with("ねん"))) return false;
    // the で of the end after it: よ (ちゃうで 아니야, ちゃうでな)
    if (size_t q = p; Lit(s, q, "で") && FinalParticleEnd(s, q) != std::string_view::npos) {
        for (const auto& [form, withYo] : {std::pair<std::string_view, std::string_view>{"違う", "違うよ"},
                                           {"じゃない", "じゃないよ"},
                                           {"違った", "違ったよ"},
                                           {"じゃなかった", "じゃなかったよ"}}) {
            if (plain != form) continue;
            m.group[1] = withYo;
            m.end = q;
            return true;
        }
    }
    bool goes = EndAt(s, p) || FinalParticleEnd(s, p) != std::string_view::npos;
    for (const std::string_view w : {"やん", "やろ", "ねん", "か", "ん", "の", "って", "と", "よ", "ちゃう", "し", "けど", "から"}) {
        goes = goes || s.substr(p).starts_with(w);
    }
    if (!goes) return false;
    m.group[1] = plain;
    m.end = p;
    return true;
}

// The drawn じゃあ of では after a noun before the negative (人間じゃあない 인간이 아니다, 人間じゃあなくなって,
// 本気じゃあねえ, 嘘じゃあありません): じゃ, which the word stage reads with it. Not the じゃあ of then, which opens a
// sentence or follows それ (それじゃあな), nor the farewell じゃあな.
bool DrawnJaNegative(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    // after a noun, or after a predicate where じゃあない asks for agreement (いいじゃあないですか, 悪くはないじゃあないか,
    // いいんじゃあないかな)
    if (!JaFollows(head) && (!PredicateEnds(head) || EndsWithAny(head, kJaNotCopula))) return false;
    size_t p = pos;
    if (!Lit(s, p, "じゃあ")) return false;
    bool negative = false;
    for (const std::string_view w : {"ない", "なく", "なかっ", "なけれ", "ねえ", "ねぇ", "ねー", "ありま", "あるまい"}) {
        negative = negative || s.substr(p).starts_with(w);
    }
    if (!negative) return false;
    m.group[1] = "じゃ";
    m.end = p;
    return true;
}

// The negative of the contracted てしまう said ちゃわ, じゃわ, ちまわ or じまわ before ない and its forms (落ちちゃわないのか
// 떨어져 버리지 않아?, 死んじゃわない, 忘れちまわないうちに): てしまわ, でしまわ after a verb stem. Not ちゃわん (茶碗).
bool ContractedShimawaNai(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (head.empty()) return false;
    const char32_t b = LastRune(head).cp;
    if (!(IsKanjiClass(b) || IsHiraganaClass(b))) return false;
    for (const auto& [written, plain] : {std::pair<std::string_view, std::string_view>{"ちゃわな", "てしまわな"},
                                         {"じゃわな", "でしまわな"},
                                         {"ちまわな", "てしまわな"},
                                         {"じまわな", "でしまわな"}}) {
        size_t p = pos;
        if (!Lit(s, p, written)) continue;
        // で after ん or い only (死んじゃわない, 泳いじゃわない); て after っ, a kanji or the stem of a verb
        if (plain.starts_with("で") ? !In(b, U"んい") : In(b, U"んをはがもにへとやの")) return false;
        m.group[1] = plain;
        m.end = p;
        return true;
    }
    return false;
}

// The question くね of an adjective in い, its negative ない cut short (ヤバくね？ 위험하지 않아?, 寒くね?, すごくね？): くない,
// before a question mark only (早くね！ is 빨리, 응). The stem is a kanji stem or a katakana or kana run of two or more that
// the dictionary has as the stem of an adjective, or the negative stem of a verb in な (足りなくね？ 부족하지 않아?,
// わからなくね？).
static bool NegativeStemIs(std::string_view stem) {
    if (!stem.ends_with("な")) return false;
    const std::string_view v = stem.substr(0, stem.size() - std::string_view("な").size());
    if (v.empty()) return false;
    // a kanji run before the stem may hold a noun (時間足りな): the stem is its shortest end that makes a verb
    if (IsKanjiClass(text::DecodeRune(v).cp) && IsKanjiClass(text::DecodeRune(v.substr(text::DecodeRune(v).size)).cp)) {
        if (NegativeStemIs(stem.substr(text::DecodeRune(v).size))) return true;
    }
    if (VerbIs(Cat({v, "る"}))) return true;  // 足りな 足りる
    // the a row of a godan verb before ない (分からな 分かる, 行かな 行く, 言わな 言う)
    const text::Rune last = LastRune(v);
    const std::u32string_view a = U"かがさたなばまらわ", u = U"くぐすつぬぶむるう";
    const size_t at = a.find(last.cp);
    if (at == std::u32string_view::npos) return false;
    std::string godan(v.substr(0, v.size() - last.size));
    text::AppendRune(godan, u[at]);
    return VerbIs(godan);
}

// Whether the sentence before at names a topic or a subject: a particle は, が or も after a kanji or katakana or at the
// end of a piece between commas, or a piece before a comma that does not end like an adverb or a phrase in で, て
// (あいつ、優しくね？, これ、マジで楽しくね？, 雨が降ってて、; not そっと、, みんなで, 急いで、, ゆっくり、).
static bool TopicBefore(std::string_view s, size_t at) {
    size_t start = at;
    while (start > 0 && !In(LastRune(s.substr(0, start)).cp, U"。｡！!？?「『（(\n")) start -= LastRune(s.substr(0, start)).size;
    const std::string_view clause = s.substr(start, at - start);
    size_t from = 0;
    while (from < clause.size()) {
        size_t comma = std::string_view::npos;
        for (const std::string_view c : {"、", "，", ","}) comma = std::min(comma, clause.find(c, from));
        const std::string_view piece = clause.substr(from, comma == std::string_view::npos ? std::string_view::npos : comma - from);
        for (size_t i = 0; i < piece.size();) {
            const text::Rune r = text::DecodeRune(piece.substr(i));
            const std::string_view rest = piece.substr(i + r.size);
            if ((IsKanjiClass(r.cp) || IsKatakanaRune(r.cp)) && !rest.empty() && In(text::DecodeRune(rest).cp, U"はがも")) return true;
            i += r.size;
        }
        const size_t runes = text::RuneCount(piece);
        if (runes >= 2 && In(LastRune(piece).cp, U"はがも")) return true;
        if (comma == std::string_view::npos) break;
        if (runes > 0) {
            bool adverb = piece.ends_with("っと") || piece.ends_with("んと");
            if (!adverb) adverb = In(LastRune(piece).cp, U"りくにでてらず");
            if (!adverb) return true;
        }
        from = comma + text::DecodeRune(clause.substr(comma)).size;
    }
    return false;
}

bool AdjectiveKune(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "くね")) return false;
    {
        size_t q = p;
        Lit(s, q, "ー");
        if (!OneOf(s, q, U"？?")) return false;
    }
    const std::string_view head = s.substr(0, pos);
    bool adjective = false;
    std::string_view found;
    if (const std::string_view stem = StemBefore(s, pos); !stem.empty()) {
        adjective = AdjectiveIs(stem) || NegativeStemIs(stem);
        found = stem;
    } else {
        // a kana run before くね: its last two to four characters
        size_t at = pos;
        for (int n = 1; n <= 4 && at > 0 && !adjective; n++) {
            const text::Rune r = LastRune(head.substr(0, at));
            if (!(IsHiraganaClass(r.cp) || IsKatakanaRune(r.cp))) break;
            at -= r.size;
            if (n >= 2 && (AdjectiveIs(head.substr(at)) || NegativeStemIs(head.substr(at)))) {
                adjective = true;
                found = head.substr(at);
            }
        }
    }
    // not a く form of a greeting said with ね (宜しくね？, よろしくね？), nor, with no topic or subject before it in the
    // sentence, a く form of a request (そっと、優しくね？, みんなで楽しくね？; class adjective.ku-request) or a く form that goes
    // on from a request in the て form of a verb before a comma (急いで、早くね？ 서둘러, 빨리 해); with one it is the cut
    // negative (あいつ、優しくね？ 다정하지 않아?, これ、マジで楽しくね？, 雨が降ってて、寒くね？)
    if (adjective && dict::InClass(HDTRANS_CLASS("adjective.ku-greeting"), Cat({found, "く"}))) return false;
    const bool topic = TopicBefore(s, pos - found.size());
    if (adjective && !topic && dict::InClass(HDTRANS_CLASS("adjective.ku-request"), Cat({found, "く"}))) return false;
    if (const std::string_view before = s.substr(0, pos - found.size());
        (before.ends_with("て、") || before.ends_with("で、")) && !before.ends_with("くて、") && !NegativeStemIs(found) && !topic) {
        return false;
    }
    if (!adjective) return false;
    m.group[1] = "くない";
    m.end = p;
    return true;
}

// The Kansai でけ of できる (でけん できん, でけへん できへん, でけた できた, でけます): でき, where it starts a word: after a
// mark, the particle しか, は, が, も, を, に or と, or the なん of そんなん. After a kanji or katakana で is the particle
// (金髪でけんか, 耳元でけたたましい). Not でけえ, でけぇ (でかい, big).
bool KansaiDekiru(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "でけ")) return false;
    bool form = false;
    for (const std::string_view w : {"ん", "へん", "ひん", "た", "て", "る", "ま"}) form = form || s.substr(p).starts_with(w);
    if (!form) return false;
    // not でけんか of 喧嘩 (でけんかった is the past) nor でけたたましい
    if ((s.substr(p).starts_with("んか") && !s.substr(p).starts_with("んかっ")) || s.substr(p).starts_with("たた")) return false;
    const std::string_view head = s.substr(0, pos);
    if (!head.empty() && !In(LastRune(head).cp, U"。｡！!？?「『（(、，, 　…‥")) {
        bool particle = false;
        for (const std::string_view w : {"しか", "は", "が", "も", "を", "に", "と", "なん"}) particle = particle || head.ends_with(w);
        if (!particle) return false;
    }
    m.group[1] = "でき";
    m.end = p;
    return true;
}

// The Kansai やろ of a guess after the contracted ている or ていた (言うてるやろ 言ってるだろ 말하고 있잖아, 知ってたやろ？):
// だろ, which the word stage reads on the verb; read as it is, やろ took the contracted form for a noun.
bool ContractedTeiruYaro(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    bool contracted = false;
    for (const std::string_view w : {"てる", "でる", "てた", "でた", "とる", "どる", "とった", "どった"}) contracted = contracted || head.ends_with(w);
    if (!contracted) return false;
    size_t p = pos;
    if (!Lit(s, p, "やろ")) return false;
    m.group[1] = "だろ";
    m.end = p;
    return true;
}

// The ておられん of western speech said っとられん or んどられん (もうやっとられん 더는 못 해먹겠다, 読んどられへん):
// ってられ, んでられ, the potential of the contracted ている before its negative ん, へん, ひん or ない; and とられん after
// the stem of an ichidan verb or the い of a godan verb in く (見とられん 見てられない, 書いとられへん), before ん, へん or ひん
// only (見とられない may be 看取られない).
bool ToruPotentialNegative(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    const std::string_view head = s.substr(0, pos);
    size_t p = pos;
    std::string_view plain;
    bool stemTo = false;
    if (Lit(s, p, "っとられ")) {
        plain = "ってられ";
    } else if (Lit(s, p = pos, "んどられ")) {
        plain = "んでられ";
    } else if (Lit(s, p = pos, "とられ")) {
        const std::string_view stem = StemBefore(s, pos);
        if (stem.empty()) return false;
        // an ichidan verb, not a godan verb in る (足とられん is 足取られん)
        const bool ichidan = IchidanIs(Cat({stem, "る"}));
        const bool onbin = stem.ends_with("い") && VerbIs(Cat({stem.substr(0, stem.size() - std::string_view("い").size()), "く"}));
        if (!ichidan && !onbin) return false;
        plain = "てられ";
        stemTo = true;
    } else {
        return false;
    }
    bool negative = false;
    for (const std::string_view w : {"ん", "へん", "ひん", "ない", "なかっ"}) negative = negative || s.substr(p).starts_with(w);
    if (stemTo && (s.substr(p).starts_with("ない") || s.substr(p).starts_with("なかっ"))) negative = false;
    if (!negative) return false;
    // After kana the kana before っ make a godan verb of the dictionary with う, つ or る (もうやっとられん やる, ほっとられ
    // へん ほる, もっとられん もつ); the adverbs in っと (ちょっと, ずっと, じっと) make none.
    // Not where the word before っ with っとる is itself a verb of the dictionary (のっとられない 乗っ取られない,
    // 乗っとられん).
    if (plain.starts_with("っ")) {
        size_t at = head.size();
        for (int n = 1; n <= 4 && at > 0; n++) {
            const text::Rune r = LastRune(head.substr(0, at));
            if (!IsHiraganaClass(r.cp) && !IsKanjiClass(r.cp)) break;
            at -= r.size;
            if (VerbIs(Cat({head.substr(at), "っとる"})) || VerbIs(Cat({head.substr(at), "っ取る"}))) return false;
        }
    }
    if (plain.starts_with("っ") && IsHiraganaClass(LastRune(head).cp)) {
        bool verb = false;
        size_t at = head.size();
        for (int n = 1; n <= 3 && at > 0 && !verb; n++) {
            const text::Rune r = LastRune(head.substr(0, at));
            if (!IsHiraganaClass(r.cp)) break;
            at -= r.size;
            for (const std::string_view u : {"う", "つ", "る"}) verb = verb || VerbIs(Cat({head.substr(at), u}));
        }
        if (!verb) return false;
    }
    // the Kansai へん, ひん after it: the whole negative (読んどられへん 読んでられない); ん after a stem too (見とられん)
    size_t q = p;
    const bool kansai = Lit(s, q, "へん") || Lit(s, q = p, "ひん") || (stemTo && Lit(s, q = p, "ん"));
    m.group[1] = plain;
    m.end = p;
    if (kansai) {
        m.group[1] = plain.starts_with("っ") ? "ってられない" : plain.starts_with("ん") ? "んでられない" : "てられない";
        m.end = q;
    }
    return true;
}

// The Kansai たれ of an order to do something for someone, the imperative of the contracted てやる (迎え入れたれ 맞이해 줘라,
// 貸したれや, 言うたれ, 見たれよ): てやれ, before the end, や, よ, な or a comma. The stem is a verb of the dictionary:
// an ichidan stem (入れ of 入れる), the し of a verb in す or a noun of する, a godan stem said い or っ (書い, 言っ), or a godan
// verb in う said whole (言う). Not the nouns in ったれ (くそったれ, 甘ったれ), whose stem is no verb, nor 来たれ.
bool KansaiTare(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "たれ")) return false;
    {
        size_t q = p;
        // not before a comma, where たれ is the stem of a passive that goes on (打たれ、)
        const bool ends = (EndAt(s, p) && !OneOf(s, q, U"、，,")) || Lit(s, q = p, "や") || Lit(s, q = p, "よ") || Lit(s, q = p, "な");
        if (!ends) return false;
        // たれな, たれよ end the sentence there (迎え入れたれな), not the noun たれ before more (垂れ流し)
        if (!EndAt(s, p) && !EndAt(s, q) && FinalParticleEnd(s, p) == std::string_view::npos) return false;
    }
    const std::string_view head = s.substr(0, pos);
    if (head.empty() || head.ends_with("来")) return false;
    const std::string_view stem = StemBefore(s, pos);
    if (stem.empty()) {
        // a godan stem in kana said with っ (やったれ やってやれ): the kana before っ make a verb with う, つ or る, and no
        // word of the dictionary ends there in ったれ (しみったれ, くそったれ)
        if (!head.ends_with("っ")) return false;
        const std::string_view k = head.substr(0, head.size() - std::string_view("っ").size());
        bool verb = false;
        size_t at = k.size();
        for (int n = 1; n <= 3 && at > 0; n++) {
            const text::Rune r = LastRune(k.substr(0, at));
            if (!IsHiraganaClass(r.cp)) break;
            at -= r.size;
            if (IsWord(Cat({k.substr(at), "ったれ"}))) return false;
            for (const std::string_view u : {"う", "つ", "る"}) verb = verb || VerbIs(Cat({k.substr(at), u}));
        }
        if (!verb) return false;
    } else {
        // the passive or imperative of a godan verb in つ or たる (開け放たれ, 打たれ, 当たれ) after its kanji
        if (VerbIs(Cat({stem, "つ"})) || VerbIs(Cat({stem, "たる"})) || VerbIs(Cat({stem, "たれる"}))) return false;
        const auto cut = [&](std::string_view last) { return stem.substr(0, stem.size() - last.size()); };
        bool verb = false;
        if (stem.ends_with("っ")) {
            for (const std::string_view u : {"う", "つ", "る"}) verb = verb || (!cut("っ").empty() && VerbIs(Cat({cut("っ"), u})));
        } else if (stem.ends_with("う")) {
            verb = VerbIs(stem);  // 言うたれ, 手伝うたれ: the う before た of a godan verb in う, said whole
        } else if (stem.ends_with("い")) {
            verb = !cut("い").empty() && VerbIs(Cat({cut("い"), "く"}));
        } else if (stem.ends_with("し")) {
            verb = !cut("し").empty() && (VerbIs(Cat({cut("し"), "す"})) || IsKanjiClass(LastRune(cut("し")).cp));
        }
        verb = verb || VerbIs(Cat({stem, "る"}));
        if (!verb) return false;
    }
    m.group[1] = "てやれ";
    m.end = p;
    // the な after the order softens it like よ (迎え入れたれな 맞이해 줘라); read as it is, てやれな is a potential
    if (size_t q = p; Lit(s, q, "な") && EndAt(s, q)) {
        m.group[1] = "てやれよ";
        m.end = q;
    }
    return true;
}

// The なか of Kyushu speech, ない, after じゃ or では (急ぎじゃなかけん 급한 게 아니니까, 白じゃなかと？; after く it is 中,
// ざわめくなか),
// before the から of its speech (けん, けえ), a sentence particle, ばい (よ), けど, し or the end: ない, with けん read as から here, where the
// い before it would leave it to いけん.
bool KyushuNaka(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (!(head.ends_with("じゃ") || head.ends_with("では"))) return false;
    size_t p = pos;
    if (!Lit(s, p, "なか")) return false;
    // what stands before じゃ: a kanji, katakana, ん, の or a kana run that ends in a word of the dictionary (それじゃなか,
    // そうじゃなか); not a kana name the dictionary does not hold, which keeps its reading (ちゃこじゃなか)
    {
        const std::string_view before = head.substr(0, head.size() - std::string_view("じゃ").size());
        const char32_t b = before.empty() ? 0 : LastRune(before).cp;
        bool word = b == 0 || !IsHiraganaClass(b) || In(b, U"んの") || !StemBefore(s, before.size()).empty();  // 急ぎじゃなか
        for (size_t at = before.size(), n = 1; !word && n <= 6 && at > 0; n++) {
            const text::Rune r = LastRune(before.substr(0, at));
            if (!IsHiraganaClass(r.cp)) break;
            at -= r.size;
            word = n >= 2 && IsWord(before.substr(at));
        }
        if (!word) return false;
    }
    for (const std::string_view w : {"けん", "けえ", "けぇ"}) {
        size_t q = p;
        if (Lit(s, q, w)) {
            m.group[1] = "ないから";
            m.end = q;
            return true;
        }
    }
    // ばい of the end: よ (そうじゃなかばい 그렇지 않아)
    if (size_t q = p; Lit(s, q, "ばい") && EndAt(s, q)) {
        m.group[1] = "ないよ";
        m.end = q;
        return true;
    }
    bool goes = FinalParticleEnd(s, p) != std::string_view::npos || s.substr(p).starts_with("と");
    // a clause that goes on with けど, けれど or し (そうじゃなかけど 그런 건 아니지만, 不自然じゃなかし)
    // not から: なかから is mostly 中から (これではなかから出られない)
    for (const std::string_view w : {"けど", "けれど", "し"}) goes = goes || s.substr(p).starts_with(w);
    // the か of a question (そうじゃなかか 그렇지 않아?)
    if (size_t q = p; Lit(s, q, "か") && FinalParticleEnd(s, q) != std::string_view::npos) goes = true;
    if (!goes) return false;
    m.group[1] = "ない";
    m.end = p;
    return true;
}

// The おらす of Kyushu speech, the honoured いる (ろくろ部屋におらすけん 있으니까, 先生はおらすと？): おられる, and
// with the けん of its speech おられるから, where a word starts: after a mark, a kanji, katakana or a particle.
bool KyushuOrasu(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (!head.empty()) {
        const char32_t b = LastRune(head).cp;
        if (IsHiraganaClass(b) && !In(b, U"にでがはもと")) return false;
    }
    size_t p = pos;
    if (!Lit(s, p, "おらす")) return false;
    for (const std::string_view w : {"けん", "けえ", "けぇ"}) {
        size_t q = p;
        if (Lit(s, q, w)) {
            m.group[1] = "おられるから";
            m.end = q;
            return true;
        }
    }
    if (size_t q = p; Lit(s, q, "ばい") && EndAt(s, q)) {
        m.group[1] = "おられるよ";
        m.end = q;
        return true;
    }
    if (FinalParticleEnd(s, p) == std::string_view::npos && !s.substr(p).starts_with("と")) return false;
    m.group[1] = "おられる";
    m.end = p;
    return true;
}

// The こない of 来ない written in kana before a に that begins a construction after a negative (こないに決まってる 오지 않을
// 게 뻔하다, こないにしても; class tail.negative-ni): 来ない, so the word stage does not read the Kansai こないに (이렇게).
bool NegativeNiKonai(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (!head.empty() && IsHiraganaClass(LastRune(head).cp) && !In(LastRune(head).cp, U"はがもをにでとへやの")) return false;
    size_t p = pos;
    if (!Lit(s, p, "こない")) return false;
    bool tail = false;
    for (const std::string_view t : dict::ClassMembers(HDTRANS_CLASS("tail.negative-ni"))) tail = tail || s.substr(p).starts_with(t);
    if (!tail) return false;
    m.group[1] = "来ない";
    m.end = p;
    return true;
}

// The ときゃ of spoken speech, ときは with は fused (あんときゃ 그때는, そんときゃ, 子供のときゃ, やるときゃやる, 若いときゃ,
// 元気がないときゃ): ときは. Not the ときゃ of ておけば said short after a て stem: っ, し, the い of a godan verb in く or
// the stem of an ichidan verb (やっときゃ, しときゃ, 置いときゃ, 見ときゃ, あげときゃ).
bool FusedTokiwa(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (head.empty()) return false;
    size_t p = pos;
    if (!Lit(s, p, "ときゃ")) return false;
    const char32_t b = LastRune(head).cp;
    if (In(b, U"っし")) return false;
    // the kana run before pos, its last one to three characters, or the kanji stem before it
    std::vector<std::string_view> stems;
    if (const std::string_view stem = StemBefore(s, pos); !stem.empty()) stems.push_back(stem);
    for (size_t at = pos, n = 1; n <= 3 && at > 0; n++) {
        const text::Rune r = LastRune(head.substr(0, at));
        if (!IsHiraganaClass(r.cp)) break;
        at -= r.size;
        stems.push_back(head.substr(at));
    }
    if (b == U'い') {
        // an adjective or ない (若いときゃ, ないときゃ); else the い of a verb in く (置いときゃ)
        bool adjective = head.ends_with("ない");
        for (const std::string_view w : stems) {
            adjective = adjective || (w.size() > std::string_view("い").size() && AdjectiveIs(w.substr(0, w.size() - std::string_view("い").size())));
        }
        if (!adjective) return false;
    } else if (IsKanjiClass(b) || In(b, U"きぎじちにひびみりえけげせぜてでねへべめれ")) {
        for (const std::string_view w : stems) {
            if (VerbIs(Cat({w, "る"}))) return false;
        }
    }
    // not where the は would make a verb of the dictionary with the kana after it (やるときゃやる: は and やる read as 流行る)
    for (size_t q = p, n = 1; n <= 3 && q < s.size() && IsHiraganaClass(At(s, q).cp); n++) {
        q += text::DecodeRune(s.substr(q)).size;
        if (VerbIs(Cat({"は", s.substr(p, q - p)}))) return false;
    }
    m.group[1] = "ときは";
    m.end = p;
    return true;
}

// The contracted てしまう said ちゃう, じゃう, ちまう or じまう before the ん of んだ, んです or んで (消えちゃうんだ 사라져 버리는
// 거야, 死んじゃうんだよ, バレちまうんだ, できちゃうんです): てしまう, でしまう after a verb stem, which the word stage reads
// before ん as it reads 食べてしまうんだ. で after ん or い only (読んじゃうんだ, 泳いじゃうんだ); て after っ, a kanji, katakana
// or the stem of a verb. Not the Kansai ちゃう of denial after a mark, a particle or ん (それはちゃうん？, 行くんちゃうん): KansaiChau
// reads it first. Before んだから and んじゃ the word stage reads ちゃう well as it is.
static bool ContractedShimauN(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (head.empty()) return false;
    const char32_t b = LastRune(head).cp;
    if (!(IsKanjiClass(b) || IsHiraganaClass(b) || IsKatakanaRune(b))) return false;
    for (const auto& [written, plain] : {std::pair<std::string_view, std::string_view>{"ちゃうん", "てしまうん"},
                                         {"じゃうん", "でしまうん"},
                                         {"ちまうん", "てしまうん"},
                                         {"じまうん", "でしまうん"}}) {
        size_t p = pos;
        if (!Lit(s, p, written)) continue;
        if (plain.starts_with("で") ? !In(b, U"んい") : In(b, U"んをはがもにへとやの")) return false;
        // ていってしまう said てっちゃう (置いてっちまうんだ): the って of the て form would quote
        if (head.ends_with("てっ") || head.ends_with("でっ")) return false;
        // する after a kana that makes a word with し or して (もしちゃう, しようとしちゃう: もし, として)
        if (b == U'し' && head.size() > LastRune(head).size) {
            const std::string_view before = head.substr(0, head.size() - LastRune(head).size);
            const std::string_view kana = before.substr(before.size() - LastRune(before).size);
            if (IsHiraganaClass(LastRune(before).cp) && (IsWord(Cat({kana, "し"})) || IsWord(Cat({kana, "して"})))) return false;
        }
        // んだから and んじゃ the word stage reads after ちゃう (退治しちゃうんだから, 殺されちゃうんじゃない？)
        if (s.substr(p).starts_with("だから") || s.substr(p).starts_with("じゃ")) return false;
        m.group[1] = plain;
        m.end = p;
        return true;
    }
    return false;
}

// The request くれない said くんね before a question mark after a て form (消してくんね？ 지워 주지 않을래?, 貸してくんね?),
// also before the か of a question (持ってくんねぇか？, 教えてくんねか？), and drawn out before the end, a mark, か or a
// sentence particle (持ってくんねえ。, 貸してくんねえかな): くれない. Bare くんね without the question mark may be くるね
// said short (行ってくんね 다녀올게), so it stays; くんない is read elsewhere.
static bool RequestKunne(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (!head.ends_with("て") && !head.ends_with("で")) return false;
    size_t p = pos;
    if (!Lit(s, p, "くんね")) return false;
    size_t q = p;
    if (OneOf(s, q, U"？?")) {
        m.group[1] = "くれない";
        m.end = p;
        return true;
    }
    q = p;
    const bool drawn = OneOf(s, q, U"えぇ");
    const size_t end = q;
    // drawn out also before the end, a mark or a sentence particle (持ってくんねえ。, 貸してくんねぇかな)
    size_t r = q;
    const bool question = Lit(s, r, "か") && OneOf(s, r, U"？?");
    r = q;
    const bool after = EndAt(s, q) || s.substr(q).starts_with("か") || OneOf(s, r, U"。、！!…‥」』よなぞ");
    if (!question && !(drawn && after)) return false;
    m.group[1] = "くれない";
    m.end = end;
    return true;
}

// A verb of the dictionary whose stem (the 連用形) ends head at a kana of the i or e row, read from two or three characters
// back, longest first: a godan verb in the u row of that kana or an ichidan verb in る (戻り 戻る, 笑い 笑う, おり おる, 降り
// 降る, 見え 見える), and する after a kanji noun (失礼し する). Sets plain to its dictionary form, te to its て form (戻って,
// 笑って, 行って, 見えて, して) and start to where the stem starts in head; false when no verb of the dictionary ends there.
// A stem of three characters does not start at a particle kana after a kanji or katakana noun (足元にすり: すり, not にすり).
// A run of one or two kanji that ends head, with no kanji before it, is the whole stem where the dictionary has it as the
// stem of an ichidan verb or of 来る (見 見る, 寝 寝る, 出来 出来る, 来 来る; not 将来), and so is き after a て form, the
// stem of くる (持ってき 持ってくる); these set vowel. し alone after a particle kana (not と, as in としよる) or where a word starts is
// the stem of する (そんなことはしやせんでした).
struct StemVerbForm {
    std::string plain, te;
    size_t start = 0;
    std::string_view teEnd;  // what the て form writes after the stem: for its last kana where keepLast is false (って)
    bool keepLast = false;   // the stem's last kana stays (見え+て, 失礼し+て)
    bool vowel = false;      // a stem of one or two kanji, or き after a て form
};

// The continuative of a godan verb by its last kana: that kana, the dictionary form's, and what the て form writes.
static constexpr std::array<std::array<std::string_view, 3>, 9> kGodan = {{{"い", "う", "って"},
                                                                          {"ち", "つ", "って"},
                                                                          {"り", "る", "って"},
                                                                          {"き", "く", "いて"},
                                                                          {"ぎ", "ぐ", "いで"},
                                                                          {"し", "す", "して"},
                                                                          {"み", "む", "んで"},
                                                                          {"び", "ぶ", "んで"},
                                                                          {"に", "ぬ", "んで"}}};

static bool StemVerb(std::string_view head, StemVerbForm& v) {
    if (head.empty()) return false;
    const auto vowelStem = [&](size_t start) {
        const std::string_view stem = head.substr(start);
        if (!VowelStemIs(stem)) return false;
        v.te = Cat({stem, "て"});
        v.teEnd = "て";
        v.keepLast = true;
        v.vowel = true;
        v.plain = Cat({stem, "る"});
        v.start = start;
        return true;
    };
    if (IsKanjiClass(LastRune(head).cp)) {
        size_t at = head.size();
        for (int k = 0; k < 3 && at > 0 && IsKanjiClass(LastRune(head.substr(0, at)).cp); k++) at -= LastRune(head.substr(0, at)).size;
        if (at > 0 && IsKanjiClass(LastRune(head.substr(0, at)).cp)) return false;
        if (head.size() - at > 2 * std::string_view("来").size()) return false;
        if (vowelStem(at)) return true;
        // the last kanji alone where the two are no word of the dictionary (何見よる: 見; not 将来, 以来)
        const size_t last = head.size() - LastRune(head).size;
        return last > at && !IsWord(head.substr(at)) && vowelStem(last);
    }
    if (!IsHiraganaClass(LastRune(head).cp)) return false;
    const size_t lastSize = LastRune(head).size;
    const std::string_view kana = head.substr(head.size() - lastSize);
    if (kana == "き" && (head.substr(0, head.size() - lastSize).ends_with("て") || head.substr(0, head.size() - lastSize).ends_with("で")) &&
        vowelStem(head.size() - lastSize)) {
        return true;
    }
    std::array<size_t, 2> starts{};
    size_t n = 0;
    for (size_t at = head.size() - lastSize; n < starts.size() && at > 0;) {
        const text::Rune r = LastRune(head.substr(0, at));
        if (!IsKanjiClass(r.cp) && !IsHiraganaClass(r.cp)) break;
        at -= r.size;
        starts[n++] = at;
    }
    for (size_t i = n; i-- > 0;) {
        const std::string_view stem = head.substr(starts[i]);
        const std::string_view lead = stem.substr(0, stem.size() - lastSize);
        if (i > 0 && starts[i] > 0 && In(text::DecodeRune(stem).cp, U"をはがもにへとでの")) {
            const char32_t before = LastRune(head.substr(0, starts[i])).cp;
            if (IsKanjiClass(before) || IsKatakanaRune(before)) continue;
        }
        for (const auto& g : kGodan) {
            if (kana != g[0]) continue;
            if (std::string plain = Cat({lead, g[1]}); VerbIs(plain)) {
                const bool iku = plain.ends_with("行く") || plain == "いく";
                v.teEnd = iku ? std::string_view("って") : g[2];
                v.te = Cat({lead, v.teEnd});
                v.plain = std::move(plain);
                v.start = starts[i];
                return true;
            }
        }
        if (std::string plain = Cat({stem, "る"}); IchidanIs(plain)) {
            v.te = Cat({stem, "て"});
            v.teEnd = "て";
            v.keepLast = true;
            v.plain = std::move(plain);
            v.start = starts[i];
            return true;
        }
        if (kana == "し" && IsKanjiClass(LastRune(lead).cp)) {
            v.te = Cat({stem, "て"});
            v.teEnd = "て";
            v.keepLast = true;
            v.plain = Cat({lead, "する"});
            v.start = starts[i];
            return true;
        }
    }
    if (kana == "し") {
        const size_t at = head.size() - lastSize;
        const char32_t before = at == 0 ? U'\0' : LastRune(head.substr(0, at)).cp;
        if (at == 0 || In(before, U"をはがもにへ") || !(IsKanjiClass(before) || IsHiraganaClass(before) || IsKatakanaRune(before))) {
            v.te = "して";
            v.teEnd = "て";
            v.keepLast = true;
            v.vowel = true;
            v.plain = "する";
            v.start = at;
            return true;
        }
    }
    return false;
}

// Whether one of words starts s at p.
static bool StartsWithAny(std::string_view s, size_t p, std::initializer_list<std::string_view> words) {
    for (const std::string_view w : words) {
        if (s.substr(p).starts_with(w)) return true;
    }
    return false;
}

// Whether the last n hiragana of head spell the continuative of a verb of the dictionary (いただき, もらい).
static bool KanaStemEnds(std::string_view head, int n) {
    size_t at = head.size();
    for (int k = 0; k < n; k++) {
        if (at == 0 || !IsHiraganaClass(LastRune(head.substr(0, at)).cp)) return false;
        at -= LastRune(head.substr(0, at)).size;
    }
    const std::string_view stem = head.substr(at);
    const std::string_view last = head.substr(head.size() - LastRune(head).size);
    for (const auto& g : kGodan) {
        if (last == g[0] && VerbIs(Cat({stem.substr(0, stem.size() - last.size()), g[1]}))) return true;
    }
    return IchidanIs(Cat({stem, "る"}));
}

// The polite ます of rough old speech said やす after the stem of a verb (急いで戻りやした 서둘러 돌아왔습니다, ありやせん, わかり
// やした, 行きやしょう): ました for やした, ましょう for やしょう, and before the end, a mark, a sentence particle or a
// conjunction ません for やせん and ます for やす. Not the やすい of ease (飲みやすい, 歩きやすく, the Kyushu 読みやすか), nor
// 冷やした or 増やす, whose や follows a kanji, nor a stem that is no verb of the dictionary (傷をいやした), nor bare やす
// after a stem in kana, which spells a name (ゆきやす, のりやす); before ね or よ a kana verb stem of three or more kana
// takes it (もらいやすね).
// After a て form it is the ます of ています said short (先に行ってやすね 先に行ってますね; spec LEX-7870).
static bool RoughPoliteYasu(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (head.empty() || !(In(LastRune(head).cp, U"いきぎしちにびみりえけげせてでねべめれ") || IsKanjiClass(LastRune(head).cp))) return false;
    struct YasuForm {
        std::string_view written, plain;
        bool final;
    };
    static constexpr YasuForm kForms[] = {
        {"やした", "ました", false}, {"やしょう", "ましょう", false}, {"やせん", "ません", true}, {"やす", "ます", true}};
    for (const YasuForm& f : kForms) {
        size_t p = pos;
        if (!Lit(s, p, f.written)) continue;
        // the past ませんでした (しやせんでした)
        if (f.final && !EndAt(s, p) && FinalParticleEnd(s, p) == std::string_view::npos &&
            !StartsWithAny(s, p, {"か", "が", "けど", "から", "ので", "ぜ", "ぞ"}) && !(f.written == "やせん" && s.substr(p).starts_with("でした"))) {
            return false;
        }
        // やすか only as a question (ありやすか？, 行きやすかい): the Kyushu adjective in か of ease (読みやすか, 読みやすかった)
        // is not the polite ます
        if (f.written == "やす" && s.substr(p).starts_with("か")) {
            size_t r = p + std::string_view("か").size();
            if (!OneOf(s, r, U"？?い")) return false;
        }
        const bool te = TeFormEnds(head);
        StemVerbForm v;
        if (!te && !StemVerb(head, v)) return false;
        // bare やす only after a stem with a kanji (戻りやすか？) or the き of 来る: a kana stem with やす spells a name (ゆきやす,
        // のりやす); before ね or よ after a verb stem of three kana or more it is the polite ending (いただきやすね, もらいやすね;
        // not ゆきやすね, はのりやすね)
        if (f.written == "やす" && !te && !IsKanjiClass(text::DecodeRune(head.substr(v.start)).cp) && !(v.vowel && head.substr(v.start) == "き") &&
            !(StartsWithAny(s, p, {"ね", "よ"}) && (KanaStemEnds(head, 3) || KanaStemEnds(head, 4) || KanaStemEnds(head, 5)))) {
            return false;
        }
        m.group[1] = f.plain;
        m.end = p;
        return true;
    }
    return false;
}

// The western よる of an action going on after the stem of a verb (渋りよった 渋っていた, 雨が降りよる 降っている, 助かりよんよ
// 助かっているのよ, 何しよるん 何しているの): ている, ていた for よった, and ているの for よん before よ, with the verb in
// its て form; a stem in kana takes three characters. よる goes before the end, a mark, a sentence particle, ん, の or a conjunction. Not a verb of 寄る the
// dictionary has (駆けよった 駆け寄った, 立ちよる, 擦りよる, 言いよる), nor the volitional よう. The match starts at the last
// kana of the stem, which the て form rewrites (渋り 渋って).
static bool WesternYoru(std::string_view s, size_t pos, Match& m) {
    if (pos >= s.size() || !(IsHiraganaClass(At(s, pos).cp) || IsKanjiClass(At(s, pos).cp))) return false;
    const size_t at = pos + At(s, pos).size;
    size_t p = at;
    if (!Lit(s, p, "よ")) return false;
    std::string_view tail;
    size_t q = p;
    bool yon = false;  // よん, which is no form of 寄る
    if (Lit(s, q, "った")) {
        tail = "いた";
    } else if (q = p; Lit(s, q, "る")) {
        if (!EndAt(s, q) && FinalParticleEnd(s, q) == std::string_view::npos &&
            !StartsWithAny(s, q, {"ん", "の", "か", "が", "けど", "けん", "から", "じゃ", "で", "ぞ", "わ"})) {
            return false;
        }
        tail = "いる";
        // the ん of の that ends the sentence after it (何しよるん 何しているの)
        if (size_t r = q; Lit(s, r, "ん") && EndAt(s, r)) {
            tail = "いるの";
            q = r;
        }
    } else if (q = p; Lit(s, q, "ん")) {
        // before よ only: よんだ, よんで are the past and the て form of 呼ぶ, 読む (わたしよんだ？), and よん before a mark
        // may be より or よ said short (それよん、)
        if (size_t r = q; !OneOf(s, r, U"よ")) return false;
        tail = "いるの";
        yon = true;
    } else {
        return false;
    }
    const std::string_view head = s.substr(0, at);
    StemVerbForm v;
    if (!StemVerb(head, v)) return false;
    const std::string_view stem = head.substr(v.start);
    // a stem in kana of two characters may be a word of its own (それ, あれ: 逸れる, 荒れる), so a kana stem takes three
    if (!v.vowel && !IsKanjiClass(text::DecodeRune(stem).cp) && stem.size() < 3 * std::string_view("あ").size()) return false;
    // 寄る after the stem or after its last two characters (駆け寄る, 足元にすりよった: すり寄る), or a key of the dictionary
    // over them and よる as written (言いよる); not before よん
    for (size_t b = v.start; !yon && b < head.size();) {
        const std::string_view tailStem = head.substr(b);
        if (b > v.start && text::RuneCount(tailStem) < 2) break;
        if (VerbIs(Cat({tailStem, "寄る"})) || VerbIs(Cat({tailStem, "よる"})) || IsWord(Cat({tailStem, "よる"}))) return false;
        b += text::DecodeRune(tailStem).size;
    }
    m.group[1] = v.keepLast ? Span(s, pos, at) : std::string_view();
    m.group[2] = v.teEnd;
    m.group[3] = tail;
    m.end = q;
    return true;
}

// The ません of a polite apology or denial said before した with no で (すんませんした, 申し訳ありませんした, すいませんっした):
// ませんでした, before the end, a mark, a stretch, a sentence particle, か or a conjunction (spec LEX-8100).
static bool MasenShita(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "ません")) return false;
    Lit(s, p, "っ");
    if (!Lit(s, p, "した")) return false;
    if (!EndAt(s, p) && FinalParticleEnd(s, p) == std::string_view::npos &&
        !StartsWithAny(s, p, {"か", "ぁ", "あ", "っ", "ね", "よ", "けど", "が", "から", "ので"})) {
        return false;
    }
    // Before a conjunction, not after a word that ends in ません or ませんでした (すみません, 申し訳ありません, すんませんでした:
    // an apology), which the word stage reads as a whole that takes no conjunction (すいませんしたから stays).
    if (pos > 0 && StartsWithAny(s, p, {"けど", "が", "から", "ので"})) {
        const size_t last = pos - LastRune(s.substr(0, pos)).size;
        if (WordCovers(s, last, pos + std::string_view("ません").size())) return false;
        // WordCovers looks back a bounded run of letters, so the built text keeps only the 32 letters before ません.
        size_t from = pos;
        for (int n = 0; n < 32 && from > 0; n++) from -= LastRune(s.substr(0, from)).size;
        const std::string standard = Cat({s.substr(from, pos - from), "ませんでした"});
        if (WordCovers(standard, last - from, standard.size())) return false;
    }
    m.group[1] = "ませんでした";
    m.end = p;
    return true;
}

// Whether a word starts at pos for a spoken form of a word: the line or a quote opens there, a mark, a space, a digit or a
// Latin letter stands before it, or one of the particles は, が, も, に, を, て after what came before (spec LEX-8101).
static bool SpokenWordStart(std::string_view s, size_t pos) {
    if (pos == 0) return true;
    const char32_t b = LastRune(s.substr(0, pos)).cp;
    if (!(IsHiraganaClass(b) || IsKatakanaRune(b) || IsKanjiClass(b))) return true;
    return In(b, U"はがもにをて");
}

// The member of class c that s starts with at pos and its note (the plain form); false for none or no note. The look-up
// goes by the members' sizes, so it does not grow with the rows of the class.
static bool ClassFormAt(dict::ClassId c, std::string_view s, size_t pos, std::string_view& written, std::string_view& plain) {
    const size_t n = dict::StartsWithMember(c, s.substr(pos));
    if (n == 0) return false;
    written = s.substr(pos, n);
    const auto members = dict::ClassMembers(c);
    const auto notes = dict::ClassNotes(c);
    for (size_t i = 0; i < members.size() && i < notes.size(); i++) {
        if (members[i] != written) continue;
        plain = notes[i];
        return !plain.empty();
    }
    return false;
}

// A spoken or regional predicate of class dialect.predicate-word where a word starts, before the end, a mark, a sentence
// particle, か, って, から, けど, の, ん, です or べ (わぁった わかった, いがったな よかったな); a regional pronoun of class
// dialect.pronoun-before-particle before a particle or ら (おどれは お前は, わいの わしの; not おどれ！, the order to dance);
// a regional adverb of class dialect.adverb-before-verb before a verb in kanji (ようも抜かした よくも); and not where a word
// of the dictionary or a person's name reaches past the form (spec LEX-8101).
static bool SpokenClassWord(std::string_view s, size_t pos, Match& m) {
    // the first characters of the members of the three classes, gathered once: most places start none (the scan stays one
    // test per place, whatever the rows)
    static const std::u32string kFirsts = [] {
        std::u32string firsts;
        for (const dict::ClassId c : {HDTRANS_CLASS("dialect.predicate-word"), HDTRANS_CLASS("dialect.pronoun-before-particle"),
                                      HDTRANS_CLASS("dialect.adverb-before-verb")}) {
            for (const std::string_view w : dict::ClassMembers(c)) {
                const char32_t r = text::DecodeRune(w).cp;
                if (firsts.find(r) == std::u32string::npos) firsts += r;
            }
        }
        return firsts;
    }();
    if (pos >= s.size() || kFirsts.find(At(s, pos).cp) == std::u32string::npos) return false;
    // a predicate said again starts a word too (わぁったわあった)
    const bool start = SpokenWordStart(s, pos) || dict::EndsWithMember(HDTRANS_CLASS("dialect.predicate-word"), s.substr(0, pos)) != 0;
    // the pronouns and adverbs only after no kana or after を (はわいの is Hawaii's, どうしようもない stays), and after a kanji
    // only where no word of the dictionary takes the kanji with the kana (味わい)
    const char32_t b = pos == 0 ? 0 : LastRune(s.substr(0, pos)).cp;
    const bool bare = pos == 0 || (!IsHiraganaClass(b) && !IsKanjiClass(b)) || b == U'を' ||
                      (IsKanjiClass(b) && !WordCovers(s, pos - LastRune(s.substr(0, pos)).size, pos + At(s, pos).size));
    if (!start && !bare) return false;
    std::string_view written, plain;
    size_t p = 0;
    bool fits = false;
    if (start && ClassFormAt(HDTRANS_CLASS("dialect.predicate-word"), s, pos, written, plain)) {
        p = pos + written.size();
        fits = EndAt(s, p) || FinalParticleEnd(s, p) != std::string_view::npos ||
               StartsWithAny(s, p, {"か", "って", "から", "けど", "の", "ん", "です", "べ"}) ||
               dict::StartsWithMember(HDTRANS_CLASS("dialect.predicate-word"), s.substr(p)) != 0;
    } else if (bare && ClassFormAt(HDTRANS_CLASS("dialect.pronoun-before-particle"), s, pos, written, plain)) {
        p = pos + written.size();
        fits = StartsWithAny(s, p, {"の", "は", "が", "も", "や", "に", "を", "ら"}) && !s.substr(p).starts_with("のう");
    } else if (bare && ClassFormAt(HDTRANS_CLASS("dialect.adverb-before-verb"), s, pos, written, plain)) {
        p = pos + written.size();
        fits = p < s.size() && IsKanjiClass(At(s, p).cp);
    }
    if (!fits) return false;
    const size_t next = p < s.size() ? p + At(s, p).size : p;
    if (WordCovers(s, pos, next) || PersonIs(written) || DocNameIs(written)) return false;
    m.group[1] = plain;
    m.end = p;
    return true;
}

// The さ said in the causative of a godan verb before させ (呼ばさせていただく 呼ばせていただく, 読まさせて): the
// causative せ after the verb's stem in the a row. Not where the stem with that kana and す is a verb (動かさせる, 生かさせる
// are the causative of 動かす, 生かす), nor a stem of no verb of the dictionary (spec LEX-8102).
static bool SaInsertion(std::string_view s, size_t pos, Match& m) {
    static constexpr std::u32string_view kA = U"かがさたなばまらわ";
    static constexpr std::string_view kU[] = {"く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る", "う"};
    if (pos >= s.size()) return false;
    const text::Rune a = At(s, pos);
    const size_t k = kA.find(a.cp);
    if (k == std::u32string_view::npos) return false;
    size_t p = pos + a.size;
    if (!Lit(s, p, "さ") || !s.substr(p).starts_with("せ")) return false;
    const std::string_view stem = StemBefore(s, pos);
    if (stem.empty() || !IsKanjiClass(LastRune(stem).cp)) return false;
    const std::string_view kana = s.substr(pos, a.size);
    if (!VerbIs(Cat({stem, kU[k]})) || VerbIs(Cat({stem, kana, "す"}))) return false;
    m.group[1] = kana;
    m.end = p;
    return true;
}

// さして of the causative of する after a verbal noun before いただく, もらう, くれる or ください (ご一緒さしていただく
// ご一緒させていただく, 勉強さしてもらう): させて. After kana it is a verb of its own (目薬をさしていただく) (spec LEX-8102).
static bool SashiteCausative(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    const char32_t b = LastRune(s.substr(0, pos)).cp;
    if (!IsKanjiClass(b) && !(IsKatakanaRune(b) && b != U'ー')) return false;
    size_t p = pos;
    if (!Lit(s, p, "さして")) return false;
    if (!StartsWithAny(s, p, {"いただ", "頂", "もら", "貰", "くれ", "くだ", "下さ", "おく", "お"})) return false;
    if (s.substr(p).starts_with("お") && !StartsWithAny(s, p, {"おく", "おい"})) return false;
    // Not the さ of a verb of the dictionary written with the kanji (指さして 가리켜).
    if (WordCovers(s, pos - LastRune(s.substr(0, pos)).size, pos + std::string_view("さし").size())) return false;
    m.group[1] = "させて";
    m.end = p;
    return true;
}

// The っと of eastern speech for the ると of an ichidan verb in kanji (決めっと 決めると, 見せっと): before what does
// not go on with the contracted ておく, ている or a word in と (決めっとけ, 決めっとる, 決めっとこ stay) (spec LEX-8103).
static bool IchidanTto(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    size_t p = pos;
    if (!Lit(s, p, "っと")) return false;
    const std::string_view stem = StemBefore(s, pos);
    if (stem.empty() || !IsHiraganaClass(LastRune(stem).cp) || !In(LastRune(stem).cp, U"いきぎしじちにひびみりえけげせぜてでねべめれ")) {
        return false;
    }
    if (!VowelStemIs(stem)) return false;
    if (p < s.size() && In(At(s, p).cp, U"かきくけこいたっるれらろりんーおぉ〜～")) return false;
    const size_t stemAt = pos - stem.size();
    if (WordCovers(s, stemAt, p) || PersonIs(stem)) return false;
    m.group[1] = "ると";
    m.end = p;
    return true;
}

// The ん of western speech for the の that makes a verb a noun before が (入るんがそんなに嫌か 入るのが, 言うんが): の
// after a dictionary or past form of a verb of the dictionary. Not the ん of a word (考える, みかんが) (spec LEX-8104).
static bool NominalNga(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    size_t p = pos;
    if (!Lit(s, p, "ん") || !s.substr(p).starts_with("が")) return false;
    if (!In(LastRune(s.substr(0, pos)).cp, U"るうくすつむぶぐぬた")) return false;
    const size_t after = p + std::string_view("が").size();
    if (after < s.size() && In(At(s, after).cp, U"えぇ")) return false;
    const std::string_view stem = StemBefore(s, pos);
    if (stem.empty() || !VerbIs(stem)) return false;
    if (WordCovers(s, pos - stem.size(), after)) return false;
    m.group[1] = "の";
    m.end = p;
    return true;
}

// The こたあ of こと said with は before a negative or a comma (気にするこたあない 気にすることはない, そんなこたぁねえ,
// ってこたぁ、) (spec LEX-8105).
static bool KotaaKotoWa(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    size_t p = pos;
    if (!Lit(s, p, "こた")) return false;
    if (!OneOf(s, p, U"あぁー")) return false;
    // after そんな, こんな, あんな, どんな any word may follow (そんなこたぁ知らん)
    if (!StartsWithAny(s, p, {"ない", "無", "ねえ", "ねぇ", "ねー", "なかっ", "あり", "ある", "、", "ね", "な"}) && !s.substr(0, pos).ends_with("んな")) return false;
    const char32_t b = LastRune(s.substr(0, pos)).cp;
    if (IsKatakanaRune(b) || WordCovers(s, pos, p)) return false;
    m.group[1] = "ことは";
    // with the rough negative ねえ it is ことはない (困るこたぁねえ)
    for (const std::string_view w : {"ねえ", "ねぇ", "ねー"}) {
        size_t q = p;
        if (!Lit(s, q, w)) continue;
        m.group[1] = "ことはない";
        p = q;
        break;
    }
    m.end = p;
    return true;
}

// The いけん of western speech for いけない after は, ちゃ, じゃ, ば or も, before the end, a mark, a sentence particle or
// の (渡ってはいけんぞ、 いけないぞ, 見ちゃいけんよ). Not 意見 or 拝見 in kana, which go on with a particle or する
// (spec LEX-8106).
static bool WesternIken(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (!(head.ends_with("は") || head.ends_with("ちゃ") || head.ends_with("じゃ") || head.ends_with("ば") || head.ends_with("も"))) return false;
    // the particle after a word (てはいけん, 見ちゃいけん); はいけん alone where a word starts is 拝見 (では、はいけん)
    for (const std::string_view q : {"は", "ちゃ", "じゃ", "ば", "も"}) {
        if (!head.ends_with(q)) continue;
        const std::string_view lead = head.substr(0, head.size() - q.size());
        if (lead.empty() || !(IsHiraganaClass(LastRune(lead).cp) || IsKanjiClass(LastRune(lead).cp))) return false;
    }
    size_t p = pos;
    if (!Lit(s, p, "いけん")) return false;
    if (!EndAt(s, p) && FinalParticleEnd(s, p) == std::string_view::npos && !StartsWithAny(s, p, {"の", "ぞ", "ぜ", "わ", "が", "けん"})) return false;
    m.group[1] = "いけない";
    m.end = p;
    return true;
}

// ありゃんせん, 知りゃんせん of old speech for the polite negative after the stem of a verb in the い row: ません
// (spec LEX-8106).
static bool YanSen(std::string_view s, size_t pos, Match& m) {
    if (pos == 0 || !In(LastRune(s.substr(0, pos)).cp, U"りきしちにびみぎい")) return false;
    size_t p = pos;
    if (!Lit(s, p, "ゃんせん")) return false;
    m.group[1] = "ません";
    m.end = p;
    return true;
}

// The rude さらす of western speech after the し of する or of a verb in す (何しさらすんじゃ 何しやがるんだ, 邪魔しさらして):
// やがる in its form (さらした やがった, さらせ やがれ). After を it is the verb 晒す (肌をさらす) (spec LEX-8107).
static bool RudeSarasu(std::string_view s, size_t pos, Match& m) {
    if (!s.substr(0, pos).ends_with("し")) return false;
    struct SarasuForm {
        std::string_view written, plain;
    };
    static constexpr SarasuForm kForms[] = {{"さらした", "やがった"}, {"さらして", "やがって"}, {"さらす", "やがる"},
                                            {"さらせ", "やがれ"},     {"さらさ", "やがら"}};
    for (const SarasuForm& f : kForms) {
        size_t p = pos;
        if (!Lit(s, p, f.written)) continue;
        m.group[1] = f.plain;
        m.end = p;
        return true;
    }
    return false;
}

// The う of an adjective of ease or difficulty after the stem of a verb (描きやすうて 描きやすくて, 読みにくうなる,
// 言いづろうて): く, before て, ちゃ, a form of なる or ない (spec LEX-8108).
static bool EaseOnbin(std::string_view s, size_t pos, Match& m) {
    struct EaseForm {
        std::string_view written, plain;
    };
    static constexpr EaseForm kForms[] = {{"やすう", "やすく"}, {"にくう", "にくく"}, {"づろう", "づらく"}, {"づらう", "づらく"}};
    const std::string_view head = s.substr(0, pos);
    for (const EaseForm& f : kForms) {
        size_t p = pos;
        if (!Lit(s, p, f.written)) continue;
        StemVerbForm v;
        if (head.empty() || !StemVerb(head, v)) return false;
        if (!StartsWithAny(s, p, {"て", "ちゃ", "なる", "なっ", "なり", "なれ", "ない", "なく", "なか", "ござ"})) return false;
        m.group[1] = f.plain;
        m.end = p;
        return true;
    }
    return false;
}

// The もろう of Kansai and western speech for もらっ before た or て (国からもろうて もらって, 教えてもろた もらった,
// もろぅて): after a て form, a particle or where a word starts. Not もろて before を (諸手) (spec LEX-8109).
static bool KansaiMorau(std::string_view s, size_t pos, Match& m) {
    if (pos > 0) {
        const char32_t b = LastRune(s.substr(0, pos)).cp;
        if ((IsHiraganaClass(b) && !In(b, U"てでをにがもは")) || IsKatakanaRune(b) || IsKanjiClass(b)) return false;
    }
    size_t p = pos;
    if (!Lit(s, p, "もろ")) return false;
    OneOf(s, p, U"うぅ");
    if (!(s.substr(p).starts_with("て") || s.substr(p).starts_with("た"))) return false;
    const size_t after = p + std::string_view("て").size();
    if (after < s.size() && In(At(s, after).cp, U"をはがのも")) return false;
    if (WordCovers(s, pos, after) || DocNameIs("もろ")) return false;
    m.group[1] = "もらっ";
    m.end = p;
    return true;
}

// The ちゃる of western speech for てやる after the stem of a verb, its い or っ before a kanji, or し (沈めちゃる 沈めて
// やる, 書いちゃる, しちゃる してやる, 見せちゃろう 見せてやろう, 書いちゃらん): てやる, てやろう, てやらん. Not ちゃった
// or ちゃう, the てしまう of standard speech (spec LEX-8110).
static bool WesternCharu(std::string_view s, size_t pos, Match& m) {
    struct CharuForm {
        std::string_view written, plain;
    };
    static constexpr CharuForm kForms[] = {{"ちゃろう", "てやろう"}, {"ちゃらん", "てやらん"}, {"ちゃる", "てやる"}};
    const std::string_view head = s.substr(0, pos);
    if (head.empty()) return false;
    for (const CharuForm& f : kForms) {
        size_t p = pos;
        if (!Lit(s, p, f.written)) continue;
        if (f.written == "ちゃる" && p < s.size() && In(At(s, p).cp, U"いうえ")) return false;
        // ちゃらんない is ていられない (聞いちゃらんない)
        if (f.written == "ちゃらん" && p < s.size() && In(At(s, p).cp, U"なね")) return false;
        const std::string_view rest = head.substr(0, head.size() - LastRune(head).size);
        StemVerbForm v;
        const bool onbin = In(LastRune(head).cp, U"いっ") && !rest.empty() && IsKanjiClass(LastRune(rest).cp);
        if (!onbin && !StemVerb(head, v)) return false;
        if (WordCovers(s, pos - LastRune(head).size, p)) return false;
        m.group[1] = f.plain;
        m.end = p;
        return true;
    }
    return false;
}

// The ある of Kansai and western speech said あん before the ん of an explanation (あんですよ あるんですよ, 置いてあんだ
// 置いてあるんだ): after a て form, に, も or where a word starts; not あんだけ, the あれだけ (spec LEX-8111).
static bool AruN(std::string_view s, size_t pos, Match& m) {
    if (pos > 0) {
        const char32_t b = LastRune(s.substr(0, pos)).cp;
        // not after は or が, where あん may be a name (私はあんです)
        if ((IsHiraganaClass(b) && !In(b, U"てでにも")) || IsKatakanaRune(b) || IsKanjiClass(b)) return false;
    }
    size_t p = pos;
    if (!Lit(s, p, "あん")) return false;
    if (!StartsWithAny(s, p, {"です", "でしょ", "だろ", "だよ", "だな", "だね", "だけど", "だから", "だって", "だ。", "だ、", "だ！", "だ？"})) {
        if (!(s.substr(p).starts_with("だ") && EndAt(s, p + std::string_view("だ").size()))) return false;
    }
    if (WordCovers(s, pos, p + At(s, p).size) || PersonIs("あん") || DocNameIs("あん")) return false;
    m.group[1] = "あるん";
    m.end = p;
    return true;
}

// The じゃ of western and old speech for だ after the ん of an explanation that ends a sentence with a question word
// before it, with its drawn あ or before a sentence particle (何するんじゃ 何するんだ, 何するんじゃあ, どこ触っとるんじゃ):
// without a question word a bare んじゃ may be the cut んじゃない of a guess (脱走してきたんじゃ？), and an indefinite is no
// question word (何も知らないんじゃ？, どこかで会ったんじゃ？); the end rows read those. Not before a comma or what goes on
// (行くんじゃ、, 行くんじゃない), nor after そ, ほ (そんじゃ, ほんじゃ: then) (spec LEX-8112).
static bool NJaEnd(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (!head.ends_with("ん")) return false;
    const std::string_view before = head.substr(0, head.size() - std::string_view("ん").size());
    if (before.empty() || !In(LastRune(before).cp, U"るうくすつむぶぐぬたい")) return false;
    size_t p = pos;
    if (!Lit(s, p, "じゃ")) return false;
    size_t q = p;
    while (q < s.size() && In(At(s, q).cp, U"あぁー")) q += At(s, q).size;
    bool ends = q == s.size() || In(At(s, q).cp, U"。｡！!？?」』）)　 ♪");
    for (const std::string_view f : {"よ", "ぞ", "のう", "わ"}) {
        size_t r = q;
        if (Lit(s, r, f) && (r == s.size() || In(At(s, r).cp, U"。｡！!？?」』）)　 ♪ぁあーっ"))) ends = true;
    }
    if (!ends || !QuestionBefore(s, pos, true)) return false;
    // The drawn あ goes with じゃ (何するんじゃあ 何するんだ).
    m.group[1] = "だ";
    m.end = q;
    return true;
}

// The じゃ of western and old speech after a question word that asks by itself (class word.wh-ask) before a comma
// (おどれは何じゃ、いつから 何だ、): だ. After other words a じゃ before a comma is では (家じゃ、何もできない)
// (spec LEX-8112).
static bool WhJaComma(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "じゃ")) return false;
    if (p >= s.size() || !In(At(s, p).cp, U"、，,")) return false;
    const std::string_view head = s.substr(0, pos);
    bool wh = false;
    for (const std::string_view w : dict::ClassMembers(HDTRANS_CLASS("word.wh-ask"))) {
        // only one in kanji: the kana なんじゃ、 is a filler the word stage reads (なんじゃ、その… 뭐냐, 그…)
        wh = wh || (head.ends_with(w) && IsKanjiClass(text::DecodeRune(w).cp));
    }
    if (!wh) return false;
    m.group[1] = "だ";
    m.end = p;
    return true;
}

std::string DialectForms(std::string_view in, const RunLookup& runs) {
    gRuns = &runs;
    std::string text(in);
    const auto group1 = [](const Match& m) { return std::string(m.group[1]); };
    if (text.find("ちゃろ") != std::string::npos || text.find("ちゃる") != std::string::npos || text.find("ちゃらん") != std::string::npos) {
        text = ReplaceAll(text, WesternCharu, group1);
    }
    if (text.find("ちゃ") != std::string::npos) text = ReplaceAll(text, KansaiChau, group1);
    if (text.find("ません") != std::string::npos) text = ReplaceAll(text, MasenShita, group1);
    if (text.find("させ") != std::string::npos) text = ReplaceAll(text, SaInsertion, group1);
    if (text.find("さして") != std::string::npos) text = ReplaceAll(text, SashiteCausative, group1);
    if (text.find("っと") != std::string::npos) text = ReplaceAll(text, IchidanTto, group1);
    if (text.find("んが") != std::string::npos) text = ReplaceAll(text, NominalNga, group1);
    if (text.find("こた") != std::string::npos) text = ReplaceAll(text, KotaaKotoWa, group1);
    if (text.find("いけん") != std::string::npos) text = ReplaceAll(text, WesternIken, group1);
    if (text.find("ゃんせん") != std::string::npos) text = ReplaceAll(text, YanSen, group1);
    if (text.find("しさら") != std::string::npos) text = ReplaceAll(text, RudeSarasu, group1);
    if (text.find("う") != std::string::npos) text = ReplaceAll(text, EaseOnbin, group1);
    if (text.find("もろ") != std::string::npos) text = ReplaceAll(text, KansaiMorau, group1);
    if (text.find("あん") != std::string::npos) text = ReplaceAll(text, AruN, group1);
    if (text.find("じゃ、") != std::string::npos) text = ReplaceAll(text, WhJaComma, group1);
    if (text.find("んじゃ") != std::string::npos) text = ReplaceAll(text, NJaEnd, group1);
    text = ReplaceAll(text, SpokenClassWord, group1);
    if (text.find("ときゃ") != std::string::npos) text = ReplaceAll(text, FusedTokiwa, group1);
    if (text.find("こないに") != std::string::npos) text = ReplaceAll(text, NegativeNiKonai, group1);
    if (text.find("じゃあ") != std::string::npos) text = ReplaceAll(text, DrawnJaNegative, group1);
    if (text.find("うん") != std::string::npos) text = ReplaceAll(text, ContractedShimauN, group1);
    if (text.find("くんね") != std::string::npos) text = ReplaceAll(text, RequestKunne, group1);
    if (text.find("やし") != std::string::npos || text.find("やす") != std::string::npos || text.find("やせ") != std::string::npos) {
        text = ReplaceAll(text, RoughPoliteYasu, group1);
    }
    if (text.find("よ") != std::string::npos) {
        text = ReplaceAll(text, WesternYoru, [](const Match& m) { return Cat({m.group[1], m.group[2], m.group[3]}); });
    }
    if (text.find("わな") != std::string::npos) text = ReplaceAll(text, ContractedShimawaNai, group1);
    if (text.find("くね") != std::string::npos) text = ReplaceAll(text, AdjectiveKune, group1);
    if (text.find("でけ") != std::string::npos) text = ReplaceAll(text, KansaiDekiru, group1);
    if (text.find("てま") != std::string::npos || text.find("でま") != std::string::npos) text = ReplaceAll(text, KansaiMau, group1);
    if (text.find("すん") != std::string::npos) text = ReplaceAll(text, KansaiSuru, group1);
    if (text.find("んや") != std::string::npos) {
        text = ReplaceAll(text, NegativeBeforeYa, [](const Match& m) { return Cat({m.group[2], m.group[1]}); });
    }
    if (text.find("ん") != std::string::npos) text = ReplaceAll(text, NegativeBeforeNoun, group1);
    if (text.find("や") != std::string::npos) text = ReplaceAll(text, YaCopula, group1);
    if (text.find("じゃ") != std::string::npos) text = ReplaceAll(text, JaCopula, group1);
    if (text.find("け") != std::string::npos || text.find("さかい") != std::string::npos) {
        text = ReplaceAll(text, CauseParticle, group1);
    }
    if (text.find("ずら") != std::string::npos || text.find("だべ") != std::string::npos || text.find("ばい") != std::string::npos) {
        text = ReplaceAll(text, EndConjecture, group1);
    }
    if (text.find("たれ") != std::string::npos) text = ReplaceAll(text, KansaiTare, group1);
    if (text.find("う") != std::string::npos) {
        text = ReplaceAll(text, OnbinVerb, group1);
        text = ReplaceAll(text, OnbinAdjective, group1);
    }
    if (text.find("やろ") != std::string::npos) text = ReplaceAll(text, ContractedTeiruYaro, group1);
    if (text.find("られ") != std::string::npos) text = ReplaceAll(text, ToruPotentialNegative, group1);
    if (text.find("なか") != std::string::npos) text = ReplaceAll(text, KyushuNaka, group1);
    if (text.find("おらす") != std::string::npos) text = ReplaceAll(text, KyushuOrasu, group1);
    if (text.find("か") != std::string::npos) text = ReplaceAll(text, KyushuAdjective, group1);
    if (text.find("ちょ") != std::string::npos) {
        text = ReplaceAll(text, WesternChoru, [](const Match& m) {
            std::string plain(m.group[1]);
            if (m.group[3] == "で") plain.replace(0, std::string_view("て").size(), "で");
            return plain;
        });
    }
    if (text.find("んかい") != std::string::npos) {
        // The match starts at the stem's last kana: an imperative in ろ keeps it (食べ ろ), the others replace it.
        text = ReplaceAll(text, OrderNkai, [](const Match& m) {
            const std::string_view first = m.group[0].substr(0, text::DecodeRune(m.group[0]).size);
            if (m.group[1] == "ろ") return Cat({first, "ろ"});
            return std::string(m.group[1]);
        });
    }
    if (text.find("せえ") != std::string::npos || text.find("せろ") != std::string::npos || text.find("い") != std::string::npos) {
        text = ReplaceAll(text, RoughImperative, group1);
    }
    if (text.find("や") != std::string::npos) {
        text = ReplaceAll(text, RequestYa, [](const Match& m) { return Cat({m.group[1], m.group[2], m.group[3]}); });
    }
    if (text.find("で") != std::string::npos) text = ReplaceAll(text, FinalDe, group1);
    if (text.find("りはし") != std::string::npos || text.find("りはせ") != std::string::npos || text.find("りゃし") != std::string::npos ||
        text.find("りゃせ") != std::string::npos) {
        text = ReplaceAll(text, TeOruNegative, [](const Match& m) { return Cat({m.group[1], m.group[2]}); });
    }
    if (text.find("ね") != std::string::npos) {
        text = ReplaceAll(text, KansaiNen, [](const Match& m) { return Cat({m.group[1], m.group[2]}); });
    }
    if (text.find("す") != std::string::npos) text = ReplaceAll(text, OldPolite, group1);
    if (text.find("也") != std::string::npos) text = ReplaceAll(text, ArchaicNari, group1);
    if (text.find("のよさ") != std::string::npos || text.find("したぞ") != std::string::npos || text.find("すぞ") != std::string::npos ||
        text.find("すです") != std::string::npos || text.find("んです") != std::string::npos || text.find("たです") != std::string::npos ||
        text.find("のです") != std::string::npos || text.find("でしゅ") != std::string::npos ||
        text.find("でつ") != std::string::npos || text.find("まちゅ") != std::string::npos || text.find("ましゅ") != std::string::npos) {
        text = ReplaceAll(text, CharacterTic, group1);
    }
    if (text.find("んよ") != std::string::npos || text.find("んね") != std::string::npos) text = ReplaceAll(text, WesternNoEnd, group1);
    if (text.find("ちゅう") != std::string::npos) text = ReplaceAll(text, QuoteChuu, group1);
    if (text.find("こっ") != std::string::npos) text = ReplaceAll(text, ShortKoto, group1);
    text = ReplaceAll(text, SenbaPolite, group1);
    text = ReplaceAll(text, NominalEnd, group1);
    text = ReplaceAll(text, RegionalWord, group1);
    if (text.find("ええ") != std::string::npos) text = ReplaceAll(text, GoodBeforeNoda, group1);
    if (text.find("おくれでない") != std::string::npos) text = ReplaceAll(text, SayingForbidden, group1);
    if (text.find("る") != std::string::npos || text.find("た") != std::string::npos) text = ReplaceAll(text, ContractedTeiru, group1);
    if (text.find("おくれ") != std::string::npos) text = ReplaceAll(text, OldRequest, group1);
    if (text.find("おいで") != std::string::npos || text.find("お出で") != std::string::npos) text = ReplaceAll(text, HonouredState, group1);
    if (text.find("おいで") != std::string::npos || text.find("お出で") != std::string::npos) text = ReplaceAll(text, HonouredComing, group1);
    if (text.find("らし") != std::string::npos) text = ReplaceAll(text, HonouredStay, group1);
    if (text.find("まい") != std::string::npos || text.find("参") != std::string::npos) text = ReplaceAll(text, HumbleCome, group1);
    if (text.find("いかれ") != std::string::npos) text = ReplaceAll(text, HonouredGoing, group1);
    if (text.find("もらえ") != std::string::npos) text = ReplaceAll(text, UngivenFavour, group1);
    if (text.find("お") != std::string::npos) text = ReplaceAll(text, HonouredStemCopula, [](const Match& m) { return Cat({"お", m.group[1], m.group[2]}); });
    if (text.find("さいまし") != std::string::npos) text = ReplaceAll(text, OldRequestMashi, group1);
    if (text.find("お") != std::string::npos && (text.find("ください") != std::string::npos || text.find("下さい") != std::string::npos)) {
        text = ReplaceAll(text, HonouredStemRequest, [](const Match& m) { return Cat({m.group[1], m.group[2], m.group[3]}); });
    }
    gRuns = nullptr;
    return text;
}

}  // namespace hdTrans::pipeline::internal
