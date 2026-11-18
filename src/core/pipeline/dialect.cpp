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
#include <array>
#include <span>
#include <string>
#include <string_view>

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
bool IsWord(std::string_view text) { return gRuns != nullptr && gRuns->word != nullptr && gRuns->word(gRuns->context, text); }

bool AdjectiveIs(std::string_view stem) {
    return gRuns != nullptr && gRuns->adjectiveStem != nullptr && gRuns->adjectiveStem(gRuns->context, stem);
}

// The stem of a verb or adjective in kanji before pos: the kanji run and the kana after it up to pos.
std::string_view StemBefore(std::string_view s, size_t pos) {
    size_t start = pos;
    while (start > 0 && IsHiraganaClass(LastRune(s.substr(0, start)).cp)) start -= LastRune(s.substr(0, start)).size;
    if (start == 0 || !IsKanjiClass(LastRune(s.substr(0, start)).cp)) return {};
    while (start > 0 && IsKanjiClass(LastRune(s.substr(0, start)).cp)) start -= LastRune(s.substr(0, start)).size;
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
    {"やけども", "だけども", Where::Inside},  {"やけど", "だけど", Where::Inside},    {"やの", "なの", Where::Final},
    {"やねぇ", "だねぇ", Where::Final},       {"やねえ", "だねえ", Where::Final},     {"やと", "だと", Where::Any},
    {"やね", "だね", Where::Final},           {"やわ", "だわ", Where::Final},         {"やぞ", "だぞ", Where::Final},
    {"やよ", "だよ", Where::Final},           {"やんね", "だよね", Where::Final},     {"やんな", "だよな", Where::Final},
};
constexpr Form kJaForms[] = {
    {"じゃけえ", "だから", Where::Inside}, {"じゃけぇ", "だから", Where::Inside}, {"じゃけん", "だから", Where::Inside},
    {"じゃきに", "だから", Where::Any},
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
        if (stem.empty() || !VerbIs(Cat({stem, kU[a]}))) return false;
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

// Whether a question word comes before pos in its sentence: the や of a question is read by the word
// stage and the old-speech rows (どこや 어디야, どう行くもんや).
bool QuestionBefore(std::string_view s, size_t pos) {
    size_t start = pos;
    while (start > 0) {
        const text::Rune r = LastRune(s.substr(0, start));
        if (In(r.cp, U"。！？!?」『「 　")) break;
        start -= r.size;
    }
    const std::string_view clause = s.substr(start, pos - start);
    for (const std::string_view w : {"どこ", "何", "なに", "なん", "誰", "だれ", "いつ", "どう", "どれ", "どっち", "どんな", "なぜ", "なんで", "どない"}) {
        // Not the いつ of あいつ, こいつ, そいつ.
        for (size_t at = clause.find(w); at != std::string_view::npos; at = clause.find(w, at + w.size())) {
            if (w == "いつ" && at > 0 && In(LastRune(clause.substr(0, at)).cp, U"あこそ")) continue;
            return true;
        }
    }
    return false;
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
    if (!YaFollows(head) || head.ends_with("こう") || head.ends_with("ああ")) return false;
    size_t p = pos;
    // Not the stem of a verb in やす (冷やし, 費やし, 燃やし).
    const std::string_view stem = StemBefore(s, pos);
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
    if (!kana && !kansai) return false;
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
    // Every match starts with one of its forms, やと, or the やし and やった of YaPast.
    if (const std::string_view rest = s.substr(pos); !StartsWithForm(s, pos, kYaAfterPredicate) && !StartsWithForm(s, pos, kYaForms) &&
                                                     !rest.starts_with("やと") && !rest.starts_with("やし") && !rest.starts_with("やった")) {
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
    if (p < s.size() && In(At(s, p).cp, U"っろれらもかけ")) return false;
    std::string_view stem = StemBefore(s, pos);
    if (stem.empty()) {
        // A stem in kana after a mark, a space or the start of the sentence (よか声 よい声, つらかでしょう).
        size_t start = pos;
        while (start > 0 && IsHiraganaClass(LastRune(s.substr(0, start)).cp)) start -= LastRune(s.substr(0, start)).size;
        const std::string_view before = s.substr(0, start);
        if (!before.empty() && IsKatakanaRune(LastRune(before).cp)) return false;
        stem = s.substr(start, pos - start);
        if (stem.empty() || (stem != "よ" && (text::RuneCount(stem) < 2 || !AdjectiveIs(stem)))) return false;
        // A past or a verb form before the か of a question (きたか, したか) is no adjective stem.
        if (In(LastRune(stem).cp, U"ただてでるうくすつぬむぶぐ")) return false;
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
    const bool ok = EndAt(s, p) || IsKanjiClass(next) || IsKatakanaRune(next) || s.substr(p).starts_with("でしょ") ||
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
    struct Word {
        std::string_view written, plain;
        int where;  // 0 any word start, 1 sentence start, 2 before a particle, 3 before a verb, 4 before a noun
    };
    static constexpr Word kWords[] = {
        {"ほんなら", "それなら", 1}, {"ほんで", "それで", 1},     {"こげん", "こんな", 0}, {"そげん", "そんな", 0},
        {"あげん", "あんな", 0},     {"どげん", "どんな", 0},     {"ゆんべ", "夕べ", 0},   {"じゃかあしい", "やかましい", 0},
        {"じゃかましい", "やかましい", 0}, {"あんさん", "あなた", 0}, {"はよ", "早く", 3},    {"わい", "わし", 2},
        {"おら", "俺", 2},         {"ええ", "いい", 4},
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
    for (const std::string_view f : {"んか", "んや", "んやろ", "んだ", "んです", "のか", "のに", "やん", "やろ", "ねん"}) {
        follows = follows || s.substr(p).starts_with(f);
    }
    if (!follows) return false;
    m.group[1] = "いい";
    m.end = p;
    return true;
}

// でがんす, がんす after a noun and ざます, ざんす of old polite speech: です.
bool OldPolite(std::string_view s, size_t pos, Match& m) {
    for (const std::string_view w : {"でがんす", "ざます", "ざんす"}) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        if (FinalParticleEnd(s, p) == std::string_view::npos && !s.substr(p).starts_with("か") && !s.substr(p).starts_with("っ")) continue;
        if (pos == 0 || !CopulaFollows(s.substr(0, pos))) return false;
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
                                                     {"ませんです", "ません"}, {"でしたです", "でした"}, {"ますですか", "ますか"}};
    for (const auto& t : kTics) {
        size_t p = pos;
        if (!Lit(s, p, t[0])) continue;
        // ですぞ drawn or cut short (ですぞっ！, ですぞぉ, ですぞー) and before a closing 》 ends the same.
        if (t[0].ends_with("ぞ")) {
            while (p < s.size() && In(At(s, p).cp, U"っッぉォ")) p += At(s, p).size;
            // A playful wave after it (こいみたいですぞ〜) keeps the reading of the word stage.
            if (p < s.size() && In(At(s, p).cp, U"〜～")) return false;
        }
        if (!EndAt(s, p) && !(p < s.size() && In(At(s, p).cp, U"》≫〉>"))) return false;
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
        m.end = p;
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

std::string DialectForms(std::string_view in, const RunLookup& runs) {
    gRuns = &runs;
    std::string text(in);
    const auto group1 = [](const Match& m) { return std::string(m.group[1]); };
    if (text.find("てま") != std::string::npos || text.find("でま") != std::string::npos) text = ReplaceAll(text, KansaiMau, group1);
    if (text.find("すん") != std::string::npos) text = ReplaceAll(text, KansaiSuru, group1);
    if (text.find("んや") != std::string::npos) {
        text = ReplaceAll(text, NegativeBeforeYa, [](const Match& m) { return Cat({m.group[2], m.group[1]}); });
    }
    if (text.find("や") != std::string::npos) text = ReplaceAll(text, YaCopula, group1);
    if (text.find("じゃ") != std::string::npos) text = ReplaceAll(text, JaCopula, group1);
    if (text.find("け") != std::string::npos || text.find("さかい") != std::string::npos) {
        text = ReplaceAll(text, CauseParticle, group1);
    }
    if (text.find("ずら") != std::string::npos || text.find("だべ") != std::string::npos || text.find("ばい") != std::string::npos) {
        text = ReplaceAll(text, EndConjecture, group1);
    }
    if (text.find("う") != std::string::npos) {
        text = ReplaceAll(text, OnbinVerb, group1);
        text = ReplaceAll(text, OnbinAdjective, group1);
    }
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
    if (text.find("せえ") != std::string::npos || text.find("い") != std::string::npos) text = ReplaceAll(text, RoughImperative, group1);
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
        text.find("すです") != std::string::npos || text.find("んです") != std::string::npos || text.find("たです") != std::string::npos) {
        text = ReplaceAll(text, CharacterTic, group1);
    }
    if (text.find("んよ") != std::string::npos || text.find("んね") != std::string::npos) text = ReplaceAll(text, WesternNoEnd, group1);
    if (text.find("ちゅう") != std::string::npos) text = ReplaceAll(text, QuoteChuu, group1);
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
