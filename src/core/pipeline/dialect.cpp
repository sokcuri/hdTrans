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
//   nen        the Kansai ねん and ねや of an explanation (してはんねやろ, 減っとんねや);
//   polite     でがんす and ざます of old polite speech (です);
//   honorific  the honorific and humble forms the dictionary does not read (spec LEX-4765): ておくれ inside a
//              sentence (くれ), ておいで before a copula (ておられる), てらした (ておられた), the まいる of coming
//              (くる), ていかれ after an honoured subject (て行かれ), the statement てもらえない (てくれない) and
//              お + a verb stem + です (お読みです お読みになります).
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
    {"やね", "だね", Where::Final},           {"やわ", "だわ", Where::Final},         {"やぞ", "だぞ", Where::Final},
    {"やよ", "だよ", Where::Final},
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
    return CopulaFollows(head);
}

bool JaFollows(std::string_view head) { return CopulaFollows(head) && !EndsWithAny(head, kJaNotCopula); }

// The forms of や that also follow a verb or an adjective in Kansai speech (食うとるやないか, すごいやないかい).
constexpr Form kYaAfterPredicate[] = {
    {"やないかい", "じゃないか", Where::Any}, {"やないか", "じゃないか", Where::Any}, {"やんけ", "じゃないか", Where::Final},
};

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
        if (clause.find(w) != std::string_view::npos) return true;
    }
    return false;
}

bool YaCopula(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (PredicateEnds(head) && !head.ends_with("ん") && CopulaForm(s, pos, m, kYaAfterPredicate, PredicateEnds)) return true;
    // A doubled mimetic in katakana (ヘトヘトや) reads with や as it is.
    {
        size_t start = pos;
        while (start > 0 && IsKatakanaRune(LastRune(s.substr(0, start)).cp)) start -= LastRune(s.substr(0, start)).size;
        const std::string_view run = s.substr(start, pos - start);
        if (run.size() >= 4 * 3 && run.size() % 2 == 0 && run.substr(0, run.size() / 2) == run.substr(run.size() / 2)) return false;
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
    // Forms the spoken forms already read: やろ, やし, やねん.
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
        // しろ after a verbal noun, an adverb or はよ (勉強せえ, しっかりせえ, はよせえ); not うるせえ or 下せえ.
        const char32_t b = LastRune(head).cp;
        if (head.empty() || head.ends_with("うる") || head.ends_with("下") || head.ends_with("小") ||
            (IsKanjiClass(LastRune(head).cp) && AdjectiveIs(head.substr(head.size() - LastRune(head).size))) ||
            !((IsKanjiClass(b) || IsKatakanaRune(b) || In(b, U"りとようん")) && b != U'ー')) {
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
    if (lead.empty() && !PredicateEnds(s.substr(0, pos))) return false;
    if (lead.empty() && s.substr(0, pos).ends_with("ん")) return false;
    struct Tail {
        std::string_view written, plain;
    };
    // The ねん itself is read by the word stage (行くねん 가거든).
    static constexpr Tail kTails[] = {{"ねや", "んだ"}};
    for (const Tail& t : kTails) {
        size_t q = p;
        if (!Lit(s, q, t.written)) continue;
        if (FinalParticleEnd(s, q) == std::string_view::npos) continue;
        m.group[1] = lead;
        m.group[2] = t.plain;
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
        if (!Lit(s, q, f[0])) continue;
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
        if (!Lit(s, q, f[0])) continue;
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
        if (!Lit(s, q, f[0])) continue;
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

// お and the stem of a dictionary verb before a copula, the state the speaker honours (お帰りですか, お立ちです,
// お過ごしで、): お and the stem before になる, a question in ですか the present のですか (お急ぎですか
// お急ぎになるのですか 서두르십니까, not the future 서두르시겠습니까). Not the nouns made of a stem (お願いです, お返しです,
// お疲れですか), nor お持ち, which reads as written.
bool HonouredStemCopula(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "お")) return false;
    const size_t stemStart = p;
    while (p < s.size() && IsKanjiClass(At(s, p).cp)) p += At(s, p).size;
    if (p == stemStart || p >= s.size()) return false;
    const char32_t k = At(s, p).cp;
    const std::u32string_view iRow = U"いきぎしちにびみり", uRow = U"うくぐすつぬぶむる";
    const std::size_t row = iRow.find(k);
    if (row == std::u32string_view::npos && std::u32string_view(U"えけげせてねべめれ").find(k) == std::u32string_view::npos) return false;
    const std::string_view kanji = s.substr(stemStart, p - stemStart);
    p += At(s, p).size;
    const std::string_view stem = s.substr(stemStart, p - stemStart);
    if (dict::InClass(HDTRANS_CLASS("noun.o-stem"), stem)) return false;
    bool verb = false;
    if (row != std::u32string_view::npos) {
        std::string godan(kanji);
        text::AppendRune(godan, uRow[row]);
        verb = VerbIs(godan) || VerbIs(Cat({stem, "る"}));
    } else {
        verb = VerbIs(Cat({stem, "る"}));
    }
    // an adjective in い (お強いですね) is no stem
    if (k == U'い' && AdjectiveIs(kanji)) return false;
    if (!verb) return false;
    // お呼びですか asks whether one has called (부르셨습니까) and お見えです says someone has come (오셨습니다);
    // the でしょうか of such a stem stays.
    const bool past = dict::InClass(HDTRANS_CLASS("stem.o-past"), stem);
    if (past && s.substr(p).starts_with("でしょう")) return false;
    static constexpr std::string_view kForms[][2] = {
        {"でしたか", "になりましたか"}, {"でした", "になりました"}, {"ですか", "になるのですか"}, {"です", "になります"},
        {"でしょう", "になるでしょう"}, {"だった", "になった"},     {"なの", "になるの"},       {"なん", "になるん"},
        {"で、", "になって、"},         {"で…", "になって…"},       {"だ", "になる"},
    };
    for (const auto& f : kForms) {
        size_t q = p;
        if (!Lit(s, q, f[0])) continue;
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
}  // namespace

std::string DialectForms(std::string_view in, const RunLookup& runs) {
    gRuns = &runs;
    std::string text(in);
    const auto group1 = [](const Match& m) { return std::string(m.group[1]); };
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
    if (text.find("ね") != std::string::npos) {
        text = ReplaceAll(text, KansaiNen, [](const Match& m) { return Cat({m.group[1], m.group[2]}); });
    }
    if (text.find("す") != std::string::npos) text = ReplaceAll(text, OldPolite, group1);
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
    gRuns = nullptr;
    return text;
}

}  // namespace hdTrans::pipeline::internal
