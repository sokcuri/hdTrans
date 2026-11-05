#include <algorithm>
#include <span>

#include "core/dict/classes.h"
#include "core/output/pieces.h"
#include "core/pipeline/normalize.h"
#include "core/pipeline/pattern.h"
#include "core/pipeline/runes.h"
#include "core/text/unicode.h"

namespace hdTrans::pipeline::internal {

namespace {

// group[k] = s[from:to], m.end = to; returns true.
bool Done(std::string_view s, Match& m, size_t from, size_t to, int k) {
    m.group[k] = Span(s, from, to);
    m.end = to;
    return true;
}

// One rune of set or the end of s at p, as group k; the end of the match.
bool CharOrEnd(std::string_view s, size_t p, std::u32string_view set, Match& m, int k) {
    size_t q = p;
    if (OneOf(s, q, set) || p == s.size()) return Done(s, m, p, q, k);
    return false;
}

// The character before byte pos of s, past a stress mark (CollapseTsuRuns); 0 at the start.
char32_t Before(std::string_view s, size_t pos) {
    while (pos > 0) {
        const text::Rune r = LastRune(s.substr(0, pos));
        if (r.cp != output::kStressMark) return r.cp;
        pos -= r.size;
    }
    return 0;
}

// かも(しれ|知れ)(?:ん|ね[ぇえー]+)
bool Shiren(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "かも")) return false;
    for (std::string_view w : {"しれ", "知れ"}) {
        size_t q = p;
        if (!Lit(s, q, w)) continue;
        m.group[1] = Span(s, p, q);
        size_t r = q;
        if (Lit(s, r, "ん")) {
            m.end = r;
            return true;
        }
        r = q;
        if (Lit(s, r, "ね")) {
            const bool ok = Repeat(s, r, InSet(U"ぇえー"), 1, kUnbounded, [&](size_t t) {
                m.end = t;
                return true;
            });
            if (ok) return true;
        }
    }
    return false;
}

// Whether なら after before and the kana at (く, に, ゃ, ば or て) is a conditional when a pause
// follows its ねぇ: after the く of a verb's dictionary form (行く, not なく, よく, しく) and after
// に (田中に, 君に) but for the nouns of a set phrase with ならない (気に, 話に); after ゃ, ば and
// て (なきゃ, ねば, 聞き捨て) it is the negative.
bool PausedConditional(std::string_view before, std::string_view at) {
    if (at == "く") return !before.ends_with("な") && !before.ends_with("よ") && !before.ends_with("し");
    if (at != "に") return false;
    for (std::string_view w : {"気", "話", "様", "よう", "当て", "頼り", "為", "ため", "冗談", "洒落", "使い物", "比べ物",
                               "足し", "勝負", "問題"}) {
        if (before.ends_with(w)) return false;
    }
    return true;
}

// (て|で|[一-龥々][かがさたなばまらわ]|[見来出居寝似着]|(?<=^|[もはがに]|しか|もう|まだ)い|し|(?:わか|分か|判|知|つま|たま)ん)ね[ぇえ]([なよぜぞしんかけだっ]|$)
// or (やら|[くにゃばて]なら)ね[ぇえー]([なよぜぞしんかけだっ]|$), or before a kanji or a sentence
// end or a pause; not the particle やら after a kanji, なに or どう, nor the conditional なら
// after a noun (君ならねぇ) or before a pause where PausedConditional holds (行くならねぇ、,
// 田中にならねぇ、).
bool Nee(std::string_view s, size_t pos, Match& m) {
    // ねぇ or ねえ; ねー and, with strict, any of them only before a following character
    // of the set (見てねーわ, 荒れねーから; not the particle in 見てねー！ or 夜明けねー).
    // After a て form ねえ before もん or もの is the negative too (流れてねえもん; not 待っててねえもう).
    auto tail = [&](size_t p, bool bar, bool strict, bool te = false) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "ね")) return false;
        const bool lengthened = bar && OneOf(s, p, U"ー");
        if (!lengthened && !OneOf(s, p, U"ぇえ")) return false;
        if ((lengthened || strict) && p == s.size()) return false;
        // The match itself, read again (Rematch), ends in its も.
        const bool mon = te && (s.substr(p).starts_with("もん") || s.substr(p).starts_with("もの") || s.substr(p) == "も");
        if (!mon && s.substr(p).starts_with("も")) return false;
        return CharOrEnd(s, p, mon ? U"も" : U"なよぜぞしんかけだっわの", m, 2);
    };
    size_t p = pos;
    if (Lit(s, p, "て") && tail(p, true, false, true)) return true;
    p = pos;
    if (Lit(s, p, "で") && tail(p, false, false, true)) return true;
    p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"かがさたなばまらわ") && tail(p, true, false)) return true;
    // The stem of a verb in -eru or -iru, or a potential (荒れねー, 言えねえ, 通じねえ).
    p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"えけげせぜねべめれきぎしじちびみり") && tail(p, true, true)) return true;
    for (std::string_view w : {"いけ", "くれ", "られ", "でき"}) {
        p = pos;
        if (Lit(s, p, w) && tail(p, true, true)) return true;
    }
    p = pos;
    if (OneOf(s, p, U"見来出居寝似着") && tail(p, true, false)) return true;
    // いる in kana where a line starts or after the particle も, は, が, に, しか or the adverbs もう, まだ
    // (誰もいねぇ、, ここにはいねー, もういねえよ; not the 稲 of いね), also before a mark.
    p = pos;
    if (const std::string_view before = s.substr(0, pos);
        (pos == 0 || before.ends_with("も") || before.ends_with("は") || before.ends_with("が") || before.ends_with("に") ||
         before.ends_with("しか") || before.ends_with("もう") || before.ends_with("まだ")) &&
        Lit(s, p, "い")) {
        m.group[1] = Span(s, pos, p);
        if (Lit(s, p, "ね") && OneOf(s, p, U"ぇえー") &&
            CharOrEnd(s, p, U"なよぜぞしんかけだっわの。、！？!?…‥」』）) 　", m, 2)) {
            return true;
        }
    }
    p = pos;
    if (Lit(s, p, "し") && tail(p, true, false)) return true;
    for (std::string_view w : {"わか", "分か", "判", "知", "つま", "たま"}) {
        p = pos;
        if (Lit(s, p, w) && Lit(s, p, "ん") && tail(p, true, false)) return true;
    }
    for (std::string_view w : {"やら", "なら"}) {
        p = pos;
        const std::string_view before = s.substr(0, pos);
        if (w == "やら" && (before.ends_with("どう") || before.ends_with("なに") ||
                           (pos >= 3 && IsKanjiClass(At(s, pos - 3).cp)))) {
            continue;  // the particle やら: 何やらねぇ, どうやら
        }
        if (w == "なら" && !OneOf(s, p, U"くにゃばて")) continue;  // not after a noun: 君ならねぇ
        if (!Lit(s, p, w)) continue;
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "ね") || !OneOf(s, p, U"ぇえー")) continue;
        if (CharOrEnd(s, p, U"なよぜぞしんかけだっ", m, 2)) return true;
        // Before a kanji, a sentence end or a pause (ならねえ相手, やらねぇ。, なきゃならねぇ、);
        // なら before a pause where PausedConditional holds is the conditional with the
        // particle ねぇ (行くならねぇ、俺も, 田中にならねぇ、).
        const bool conditional = w == "なら" && PausedConditional(before, s.substr(pos, 3));
        const std::u32string_view ends = conditional ? U"。！？!?」』）)\n" : U"。！？!?」』）)\n、，…‥ 　";
        if (p < s.size() && (IsKanjiClass(At(s, p).cp) || In(At(s, p).cp, ends))) return Done(s, m, p, p, 2);
    }
    return false;
}

// The casual negative ねえ, ねぇ, ねー after が and a noun of noun.casual-lacked, a thing one has or lacks (救い, 金,
// 時間, 意味), before an exclamation mark or a final よ, ぞ, し, だろ, じゃ, けど or から (救いがねぇ！ 救いがない！, 金がねえよ);
// after any other word (こいつがねぇ！, ママがねえ！, 望月さんがねえ) the ね is the particle of storytelling and stays.
bool GaNee(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "が")) return false;
    const std::string_view head = s.substr(0, pos);
    const std::size_t noun = dict::EndsWithMember(HDTRANS_CLASS("noun.casual-lacked"), head);
    if (noun == 0) return false;
    // The noun stands alone: no kanji or katakana runs into it (人間 is no 間).
    const char32_t lead = Before(s, pos - noun);
    if (lead != 0 && (IsKanjiClass(lead) || IsKatakanaRune(lead))) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ね")) return false;
    const size_t run = p;
    while (p < s.size() && In(At(s, p).cp, U"ぇえー")) p += At(s, p).size;
    if (p == run) return false;
    bool tail = false;
    for (const std::string_view t : {"！", "!", "よ", "ぞ", "し", "だろ", "じゃ", "けど", "から"}) tail = tail || s.substr(p).starts_with(t);
    if (!tail) return false;
    m.end = p;
    return true;
}

// ([っし]|[えけげせぜてでねべめれ]|[きぎちにひみりびぴ]|[見来寝着出居似煮])ちま([うっえいわお])|んじま([うっえいわお]):
// てしまう contracted, after the て form of a godan verb, する and a verb in -eru or -iru
// (言っちまう, 忘れちまう, 見ちまった, できちまった; a godan stem in -i takes no ちま)
bool Chimau(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (OneOf(s, p, U"っしえけげせぜてでねべめれきぎちにひみりびぴ見来寝着出居似煮")) {
        size_t q = p;
        if (Lit(s, q, "ちま")) {
            size_t r = q;
            if (OneOf(s, r, U"うっえいわお")) {
                m.group[1] = Span(s, pos, p);
                return Done(s, m, q, r, 2);
            }
        }
    }
    p = pos;
    if (!Lit(s, p, "んじま")) return false;
    size_t r = p;
    if (!OneOf(s, r, U"うっえいわお")) return false;
    return Done(s, m, p, r, 3);
}

// ([っし]|[えけげせぜてでねべめれ]|[見来寝着出居似煮])ちった|んじった: ちゃった said with its ゃ
// dropped (行っちった, 間違えちった, 読んじった)
bool Chitta(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (OneOf(s, p, U"っしえけげせぜてでねべめれ見来寝着出居似煮")) {
        size_t q = p;
        if (Lit(s, q, "ちった")) {
            // ちったぁ is 少しは (ちったぁ暇になる), not ちゃった.
            size_t r = q;
            if (OneOf(s, r, U"ぁあァアー")) return false;
            m.group[1] = Span(s, pos, p);
            m.end = q;
            return true;
        }
    }
    p = pos;
    if (!Lit(s, p, "んじった")) return false;
    m.end = p;
    return true;
}

// ([ァ-ヶー]|何)ってんだ: っていうんだ said short after a noun (バットってんだ, 何ってんだ)
bool NounTteNda(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (Lit(s, p, "何")) {
    } else {
        const text::Rune r = text::DecodeRune(s.substr(p));
        if (!((r.cp >= U'ァ' && r.cp <= U'ヶ') || r.cp == U'ー')) return false;
        p += r.size;
    }
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ってんだ")) return false;
    m.end = p;
    return true;
}

// あんの(に|か): あるの said short (金ならあんのに)
bool AnNo(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "あんの")) return false;
    const size_t q = p;
    if (!OneOf(s, p, U"にか")) return false;
    m.group[1] = Span(s, q, p);
    m.end = p;
    return true;
}

// ([てで])もう([たて]): the Kansai てしまう (やってもうた, 赤にしてもうて)
bool Moota(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"てで")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "もう")) return false;
    const size_t q = p;
    if (!OneOf(s, p, U"たて")) return false;
    m.group[2] = Span(s, q, p);
    m.end = p;
    return true;
}

// ([てで])いやが: ている before the scornful やがる, said whole (遊んでいやがる, 笑っていやがった): the
// ている the shorter てやがる also reads, not the verb 嫌がる.
bool TeIyagaru(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    // で only as the て form after ん or い (遊んでいやがる), not the particle (つもりでいやがる).
    if (Lit(s, p, "で") && !(pos > 0 && RuneIn(Before(s, pos), U"んい"))) return false;
    p = pos;
    if (!OneOf(s, p, U"てで")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "いやが")) return false;
    m.end = p;
    return true;
}

// でやが(ります|りました|る|った): the copula of a speaker who says です as でやがります and でやがる
// (良い子でやがりますな 良い子ですな, 戻るでやがりますよ 戻るんですよ), after no て form (遊んでやがる is
// ている) and no kanji that ends an ichidan stem in で (撫でやがる).
bool DeYagaru(std::string_view s, size_t pos, Match& m) {
    // An い after kana ends an adjective (したくないでやがります, いいでやがります), not the て form of a verb
    // in -gu, which follows a kanji (泳いで).
    bool adjective = false;
    if (pos > 0 && Before(s, pos) == U'い') {
        const std::string_view head = s.substr(0, pos - std::string_view("い").size());
        adjective = !head.empty() && IsHiraganaRune(Before(head, head.size()));
    }
    if (pos > 0 && !adjective && RuneIn(Before(s, pos), U"んていでっ撫茹秀出詣")) return false;
    size_t p = pos;
    if (!Lit(s, p, "でやが")) return false;
    const size_t q = p;
    // After a predicate the copula is the explanatory んです (戻るでやがります 戻るんです).
    const bool predicate = pos > 0 && RuneIn(Before(s, pos), U"るたいうくすつぬむぶぐ");
    for (const std::string_view tail : {"りました", "ります", "った", "る"}) {
        size_t r = q;
        if (Lit(s, r, tail)) {
            const bool past = tail == "りました" || tail == "った";
            m.group[1] = predicate ? (past ? "んでした" : "んです") : (past ? "でした" : "です");
            m.end = r;
            return true;
        }
    }
    return false;
}

// っつの before a mark that ends what was said or the end: the quoting って with の of insistence said
// つ (違うっつの, 見てねえっつの！ 見ていないっての！).
bool TsuNo(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "っつの")) return false;
    if (p < s.size() && !In(At(s, p).cp, U"！？!?。、」』）)…‥ー〜～")) return false;
    m.end = p;
    return true;
}

// [てで]ねっつの before a mark that ends what was said or the end, as TsuNo: the casual negative of ている
// with its vowel dropped before っつの (見てねっつの！ 見ていないっての！; not 見てねっつのは).
bool TeNeTsuNo(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"てで")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ねっつの")) return false;
    if (p < s.size() && !In(At(s, p).cp, U"！？!?。、」』）)…‥ー〜～")) return false;
    m.end = p;
    return true;
}

// っつ[ーう]
bool Tsuu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "っつ") || !OneOf(s, p, U"ーう")) return false;
    m.end = p;
    return true;
}

// ([っん])つっ([てた]): the quoting って or なんて with the て form or past of 言う said つっ
// (やめろっつった やめろって言った, なんつった？ なんて言った？). Not なんつって before no kana
// and no question mark, the joke said after a line (phrases.tsv, like なんちゃって).
bool Tsutte(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"っん")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "つっ")) return false;
    const size_t q = p;
    if (!OneOf(s, p, U"てた")) return false;
    if (Span(s, q, p) == "て" && pos >= 3 && s.substr(pos - 3, 3) == "な") {
        const char32_t next = p < s.size() ? At(s, p).cp : 0;
        if (!IsHiraganaRune(next) && !IsKatakanaRune(next) && next != U'？' && next != U'?') return false;
    }
    return Done(s, m, q, p, 2);
}

// ([うくぐすつぬぶむる])な[ぁー]*[っッ]+([^ぁ-ゖ]|$)
bool NaTsu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"うくぐすつぬぶむる")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "な")) return false;
    return Repeat(s, p, InSet(U"ぁー"), 0, kUnbounded, [&](size_t q) {
        return Repeat(s, q, InSet(U"っッ"), 1, kUnbounded, [&](size_t r) {
            size_t t = r;
            if (Take(s, t, [](char32_t c) { return !IsHiraganaClass(c); }) || r == s.size()) {
                return Done(s, m, r, t, 2);
            }
            return false;
        });
    });
}

// ([えけげてねへべめれすく])んな([よぁー]|[っッ]?[！!、。？?…‥・」]|$), or before an ideographic space
// other than after て, ね or へ (決めてんな　お前 says what is so, ねんな and へんな are also the ねん and へん
// of Kansai); not where the んな ends a word of casual.nna-word, nor with contracted forms read after
// the まへん of Kansai ません (いきまへんな).
template <bool kContracted>
bool Nna(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"えけげてねへべめれすく")) return false;
    m.group[1] = Span(s, pos, p);
    if (kContracted && m.group[1] == "へ" && s.substr(0, pos).ends_with("ま")) return false;
    if (!Lit(s, p, "んな")) return false;
    if (dict::EndsWithMember(HDTRANS_CLASS("casual.nna-word"), s.substr(0, p)) > 0) return false;
    size_t q = p;
    if (OneOf(s, q, U"よぁー")) return Done(s, m, p, q, 2);
    const bool ok = Repeat(s, p, InSet(U"っッ"), 0, 1, [&](size_t r) {
        size_t t = r;
        if (!OneOf(s, t, U"！!、。？?…‥・」") && (In(At(m.group[1], 0).cp, U"てねへ") || !OneOf(s, t, U"　"))) return false;
        return Done(s, m, p, t, 2);
    });
    if (ok) return true;
    if (p == s.size()) return Done(s, m, p, p, 2);
    return false;
}

// Engine kanji: the CJK unified ideographs, extension A and 々.
bool IsKanji(char32_t r) { return (r >= 0x4E00 && r <= 0x9FFF) || (r >= 0x3400 && r <= 0x4DBF) || r == U'々'; }

// The kanji of the verbs in す (出す, 探す, 目指す): before すん they end a verb, not a
// する noun.
constexpr std::u32string_view kSuVerbKanji =
    U"押刺指差挿出足増化貸返帰通直治回戻渡移写映隠消干話離放外許汚倒殺残越超試示探捜逃申召為成致灯燃卸下降伏冒侵犯推濁明";

// The length of two kanji at pos that make a する noun (結婚すんの), or 0. One kanji
// before す, or a second kanji of a verb in す, is a verb (出すんじゃ, 目指すんだろ).
size_t TwoKanji(std::string_view s, size_t pos) {
    size_t p = pos;
    char32_t last = 0;
    for (int n = 0; n < 2; n++) {
        if (p >= s.size()) return 0;
        const text::Rune r = text::DecodeRune(s.substr(p));
        if (!IsKanji(r.cp)) return 0;
        last = r.cp;
        p += r.size;
    }
    return In(last, kSuVerbKanji) ? 0 : p - pos;
}

// (何|なに|何を|なにを|どう|どー|KK)すん(の|だ|です|じゃ), KK two kanji
bool Sun(std::string_view s, size_t pos, Match& m) {
    const size_t kanji = TwoKanji(s, pos);
    for (std::string_view w : {"何", "なに", "何を", "なにを", "どう", "どー", "KK"}) {
        size_t p = pos;
        if (w == "KK") {
            if (kanji == 0) continue;
            p += kanji;
        } else if (!Lit(s, p, w)) {
            continue;
        }
        size_t q = p;
        if (!Lit(s, q, "すん")) continue;
        for (std::string_view e : {"の", "だ", "です", "じゃ"}) {
            size_t r = q;
            if (!Lit(s, r, e)) continue;
            m.group[1] = Span(s, pos, p);
            return Done(s, m, q, r, 2);
        }
    }
    return false;
}

// (くれ)ん(の|だ|です)
bool Run(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "くれ")) return false;
    m.group[1] = Span(s, pos, p);
    size_t q = p;
    if (!Lit(s, q, "ん")) return false;
    for (std::string_view e : {"の", "だ", "です"}) {
        size_t r = q;
        if (Lit(s, r, e)) return Done(s, m, q, r, 2);
    }
    return false;
}

// ([るたなういくすつぬむぶぐてでだ])ん[すス]([かけよねわ」』！？!?。…‥ー〜～ぉぇ、]|$): the spoken
// んです (行くんすか, 苦手なんスけど), but not ないんすか, ないんすよ that end the sentence after
// a verb, which the end rows read (書かないんすか 쓰지 않나요). Without contracted forms read, not
// after で, and not ないんすか or ないんすよ anywhere.
template <bool kContracted>
bool Nsu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, kContracted ? U"るたなういくすつぬむぶぐてでだ" : U"るたなういくすつぬむぶぐてだ")) return false;
    // なんすか starting a word is 何すか (なんすか？ 뭔데요), its own phrase.
    if (Span(s, pos, p) == "な") {
        size_t before = pos;
        while (before > 0 && (static_cast<unsigned char>(s[before - 1]) & 0xC0) == 0x80) before--;
        if (before > 0) before--;
        const char32_t c = pos == 0 ? 0 : text::DecodeRune(s.substr(before)).cp;
        if (pos == 0 || !(IsHiraganaClass(c) || IsKanjiClass(c) || (c >= U'ァ' && c <= U'ヺ') || c == U'ー')) return false;
    }
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ん") || !OneOf(s, p, U"すス")) return false;
    // The end rows of ないんすか and ないんすよ read a verb's ない at the end of a sentence; the ない
    // of a noun or a particle (問題ないんすか, 何にもないんすか) and one the sentence goes on after
    // (じゃないんすかね) are read as ないんです.
    if (size_t q = p; m.group[1] == "い" && s.substr(0, pos).ends_with("な") && OneOf(s, q, U"かよ")) {
        if (!kContracted) return false;
        const std::string_view head = s.substr(0, pos - std::string_view("な").size());
        const char32_t before = head.empty() ? 0 : LastRune(head).cp;
        const bool verb = IsHiraganaClass(before) && !In(before, U"はもがにとへやで");
        size_t r = q;
        if (verb && (q == s.size() || OneOf(s, r, U"」』！？!?。…‥ー〜～ぁぃぅぇぉっ、"))) return false;
    }
    return CharOrEnd(s, p, U"かけよねわ」』！？!?。…‥ー〜～ぉぇ、", m, 2);
}

// The っす of です where the sentence goes on after it or ends in a particle pair that the end rows
// of っす do not read: before よね, かね, けど, けれど, が, し, ので or のに (怖いっすよね, 事っすかね,
// 大丈夫っすけど), and after a verb before から. As group 1 the です to write: んです after a verb's
// terminal or past form (思うっすよね 思うんですよね, 首つったっすけど, あるっすから), です after a noun, an
// adjective or そう, どう, こう, よう. っす after one kana that starts a word, as in a greeting or そっす,
// stays.
bool SsuMidSentence(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "っす")) return false;
    bool goesOn = false;
    for (const std::string_view w : {"よね", "かね", "けど", "けれど", "が", "し", "ので", "のに"}) {
        if (!s.substr(p).starts_with(w)) continue;
        goesOn = true;
        // けど, けれど, よね and かね that end the sentence are read by the end rows of っす (怖いっすよね。
        // 무섭죠, 自信ないっすけど。 없는데요); よね and かね before ？ stay here.
        const std::string_view after = s.substr(p + w.size());
        const char32_t next = after.empty() ? 0 : text::DecodeRune(after).cp;
        const bool end = next == 0 || In(next, U"\n。｡…‥」』ーぇ〜～");
        if ((w == "けど" || w == "けれど") && (end || In(next, U"！？!?"))) return false;
        if ((w == "よね" || w == "かね") && end) return false;
    }
    const bool kara = s.substr(p).starts_with("から");
    if ((!goesOn && !kara) || s.substr(p).starts_with("しょ")) return false;
    const char32_t c = Before(s, pos);
    if (c == 0 || !(IsHiraganaClass(c) || IsKanjiClass(c) || IsKatakanaRune(c))) return false;
    const std::string_view head = s.substr(0, pos);
    const char32_t c2 = Before(head, head.size() - LastRune(head).size);
    if (IsHiraganaClass(c) && !(IsHiraganaClass(c2) || IsKanjiClass(c2) || IsKatakanaRune(c2))) return false;
    bool verb = In(c, U"るくぐすつぬぶむ");
    if (c == U'う') {
        verb = !(head.ends_with("そう") || head.ends_with("どう") || head.ends_with("こう") || head.ends_with("よう") ||
                 head.ends_with("とう"));
    }
    if (c == U'た') verb = IsKanjiClass(c2) || In(c2, U"っいしきぎちにひびみりえけげせぜてでねへべめれ");
    if (c == U'だ') verb = In(c2, U"んい");
    if (kara && !verb) return false;
    m.group[1] = verb ? "んです" : "です";
    m.end = p;
    return true;
}

// The す of っす said without its っ after the い of an adjective or of ない written in kana, before か
// (not から), ね, よ, けど, a mark or the end (真逆じゃないすか, ヤバいすね, いいすか？): group 1 what follows,
// written after いっす. Not after a kanji or a particle, where いす is 椅子 (車いすから, このいす、), nor the
// greeting ういす; after い only for いい standing alone (それ、いいすね; not 赤いいすね).
bool AdjectiveSu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    const char32_t c = Before(s, pos);
    if (c == 0 || c == U'う' || !(IsHiraganaClass(c) || IsKatakanaRune(c)) || In(c, U"のがはをにもとでへ")) return false;
    if (c == U'い') {
        const std::string_view head = s.substr(0, pos - LastRune(s.substr(0, pos)).size);
        const char32_t b = Before(head, head.size());
        if (b != 0 && (IsHiraganaClass(b) || IsKatakanaRune(b) || IsKanjiClass(b) || b == U'ー')) return false;
    }
    if (!Lit(s, p, "いす")) return false;
    if (s.substr(p).starts_with("けど")) return Done(s, m, p, p + std::string_view("けど").size(), 1);
    if (s.substr(p).starts_with("から")) return false;
    return CharOrEnd(s, p, U"かねよ。！？!?…‥」』、", m, 1);
}

// ッス in katakana after a kana or a kanji: the っす of です (受けるッスから, 行きましたッスよ); not after a
// small kana or an imperative in e (くたばれぇッス).
bool SsuKatakanaAfterWord(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    const char32_t c = Before(s, pos);
    if (!(IsHiraganaClass(c) || IsKanjiClass(c)) || In(c, U"ぁぃぅぇぉっゃゅょゎえけげせぜてでねへべぺめれ")) return false;
    size_t p = pos;
    if (!Lit(s, p, "ッス")) return false;
    m.end = p;
    return true;
}

// A literal pattern.
template <const char* kText>
bool Literal(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, kText)) return false;
    m.end = p;
    return true;
}

constexpr char kKya[] = "っきゃない";
constexpr char kUssai[] = "うっさい";
constexpr char kSsuKatakana[] = "っス";
constexpr char kNaiNsuKatakana[] = "ないんス";
constexpr char kMokkai[] = "もっかい";
constexpr char kSoyuKoto[] = "そゆこと";

// ([いきぎしじちにひびみりえけげせぜねへべめれ])てぇか
bool Teeka(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"いきぎしじちにひびみりえけげせぜねへべめれ")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "てぇか")) return false;
    m.end = p;
    return true;
}

// すかい before [？?、。！!」』…‥ー～] or the end, and るかい before 、: a question in かい after
// です, ます or a dictionary form, its い a softening particle (お仲間ですかい, 元気してるかい、).
// Not after a noun (佐藤くんかい) nor のかい, which the end rows read.
bool QuestionKai(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    const bool su = Lit(s, p, "す");
    if (!su && !Lit(s, p, "る")) return false;
    if (!Lit(s, p, "か")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "い")) return false;
    return CharOrEnd(s, p, su ? U"？?、。！!」』…‥ー～" : U"、", m, 2);
}

// ([こそあど]んな)ん(?=[でじだがはもをに]): the の after こんな, そんな, あんな, どんな said ん
// (こんなんで 이런 것으로, そんなんじゃ).
bool KonnaN(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"こそあど") || !Lit(s, p, "んな")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ん")) return false;
    size_t q = p;
    if (!OneOf(s, q, U"でじだがはもをに")) return false;
    m.end = p;
    return true;
}

// いっか([！!。、…‥？?」]|$)
bool Ikka(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "いっか")) return false;
    return CharOrEnd(s, p, U"！!。、…‥？?」", m, 1);
}

// とんでもね[ぇえー]
bool Tondemo(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "とんでもね") || !OneOf(s, p, U"ぇえー")) return false;
    m.end = p;
    return true;
}

// うとして([るた]): the progressive after a volitional, contracted (書こうとしてる)
bool UtoShite(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "うとして")) return false;
    const size_t q = p;
    if (!OneOf(s, p, U"るた")) return false;
    m.group[1] = Span(s, q, p);
    m.end = p;
    return true;
}

// ([まで])しょ[ー～]+
bool Mashou(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"まで")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "しょ")) return false;
    return Repeat(s, p, InSet(U"ー～"), 1, kUnbounded, [&](size_t q) {
        m.end = q;
        return true;
    });
}

// ([てで])く([。！？…‥、!?よねぞわかの」]|$); the で of a て form follows ん or い (読んでく, 泳いでく), so
// a で after anything else starts another word (でくわす, にでくわした)
bool Teku(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (s.substr(pos).starts_with("で") && Before(s, pos) != U'ん' && Before(s, pos) != U'い') return false;
    if (!OneOf(s, p, U"てで")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "く")) return false;
    return CharOrEnd(s, p, U"。！？…‥、!?よねぞわかの」", m, 2);
}

// Whether s ends a sentence with the copula じゃ (どうじゃ？, 退屈じゃのぅ): the speaker's じゃねえ is
// then the copula with ね, written だね.
bool CopulaJa(std::string_view s) {
    for (size_t at = s.find("じゃ"); at != std::string_view::npos; at = s.find("じゃ", at + 1)) {
        const std::string_view rest = s.substr(at + std::string_view("じゃ").size());
        if (rest.empty() || In(At(rest, 0).cp, U"！？!?。…‥」") || rest.starts_with("のう") || rest.starts_with("のぅ") ||
            rest.starts_with("わい")) {
            return true;
        }
    }
    return false;
}

// ([ん一-龥々ァ-ヶー])じゃね[ぇえー]+, or じゃね[ぇえー]+ before わ, よ, し, ぞ, ぜ or か where a
// clause starts (じゃねーわ！, 」じゃねえよ; not the parting じゃねー！): じゃない, or だね where the line
// ends a sentence with the copula じゃ (CopulaJa).
bool Janee(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    const char32_t before = Before(s, pos);
    const bool start = before == 0 || In(before, U"「『（(」』）)、。，！？!?…‥　 \n");
    if (!start || IsKanjiClass(At(s, p).cp) || IsKatakanaRune(At(s, p).cp) || At(s, p).cp == U'ん') {
        if (!Take(s, p, [](char32_t c) { return c == U'ん' || IsKanjiClass(c) || IsKatakanaRune(c); })) return false;
    }
    m.group[1] = Span(s, pos, p);
    const bool bare = p == pos;
    if (!Lit(s, p, "じゃね")) return false;
    return Repeat(s, p, InSet(U"ぇえー"), 1, kUnbounded, [&](size_t q) {
        if (bare && !(q < s.size() && In(At(s, q).cp, U"わよしぞぜか"))) return false;
        m.end = q;
        return true;
    });
}

// じゃん(?:かよ?[っー～]*|っ+) before a character other than hiragana or the end: the particles
// and the stress after じゃん change nothing (いいじゃんか, ないじゃんかよ, 夢じゃんっ,
// 透けてるじゃんかーっ).
bool JanKa(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "じゃん")) return false;
    const size_t head = p;
    if (Lit(s, p, "か")) {
        Lit(s, p, "よ");
        while (OneOf(s, p, U"っー～")) {
        }
    } else {
        while (Lit(s, p, "っ")) {
        }
    }
    if (p == head) return false;
    size_t q = p;
    if (Take(s, q, IsHiraganaClass)) return false;
    m.end = p;
    return true;
}

// A casual negative ねえ, ねぇ or ねー with a final よ that opens its clause, at the start of the line or after
// 「, 『, a comma or a space (ねえよ, いや、ねえよ, ねーよそんなもん): there is none, ないよ. The calling ねえ
// takes no よ (ねえ、聞いてる？).
bool LoneNee(std::string_view s, size_t pos, Match& m) {
    const char32_t b = Before(s, pos);
    if (b != 0 && !In(b, U"「『、，, 　")) return false;
    size_t p = pos;
    if (!Lit(s, p, "ね") || !OneOf(s, p, U"ぇえー")) return false;
    if (!Lit(s, p, "よ")) return false;
    // A drawn-out よ (ねえよう、 ねぇよぉ) is the call ねえ said long, not a negative.
    if (p < s.size() && In(At(s, p).cp, U"ねなぉおーう")) return false;
    m.end = p;
    return true;
}

// The call ねえ drawn out with よう or よぉ before a comma or an ellipsis at the start of the line or after 「 or 『
// (ねえよう、待ってよ, ねぇよぉ…): the call ねえ, not the negative ないよ (spec GRM-3845). ねーよー！ stays a negative.
bool DrawnCallNee(std::string_view s, size_t pos, Match& m) {
    const char32_t b = Before(s, pos);
    if (b != 0 && !In(b, U"「『")) return false;
    size_t p = pos;
    if (!Lit(s, p, "ね") || !OneOf(s, p, U"ぇえー") || !Lit(s, p, "よ")) return false;
    if (!OneOf(s, p, U"うぉお")) return false;
    while (p < s.size() && In(At(s, p).cp, U"うぉおー〜～")) p += At(s, p).size;
    if (p >= s.size() || !In(At(s, p).cp, U"、，,…")) return false;
    m.end = p;
    return true;
}

// The casual negative ねえ, ねー after a word that takes ない: after わけ or 訳 (わけがない), しか or たくも
// (わけねーだろ, しかねーし, したくもねーわ), and after a noun in kanji or katakana before よ, だろ, じゃ, し, けど or ぞ,
// which follow no particle ね (需要ねーよ, 関係ねーじゃん, 魚雷ねえけど; not 綺麗ねー。, 長波ねぇ！).
bool NounNee(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    bool noun = false;
    bool word = false;
    for (const std::string_view w : {"わけが", "訳が", "わけ", "訳", "しか", "たくも", "らしく", "こと"}) {
        // ことねえ is ことない only after a past (食べたことねえ, 聞いたことねえ; not 大変なことねえ).
        if (w == "こと" && Before(s, pos) != U'た' && Before(s, pos) != U'だ') continue;
        if (s.substr(p).starts_with(w)) {
            p += w.size();
            word = true;
            break;
        }
    }
    if (!word) {
        if (Before(s, pos) != 0 && (IsKanjiClass(Before(s, pos)) || IsKatakanaRune(Before(s, pos)))) return false;
        const auto nounRune = [](char32_t c) { return IsKanjiClass(c) || IsKatakanaRune(c); };
        if (!Take(s, p, nounRune)) return false;
        while (Take(s, p, nounRune)) {
        }
        noun = true;
    }
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ね")) return false;
    const size_t run = p;
    while (p < s.size() && In(At(s, p).cp, U"ぇえー")) p += At(s, p).size;
    if (p == run) return false;
    if (noun) {
        bool tail = false;
        for (const std::string_view t : {"よ", "だろ", "じゃ", "し", "けど", "ぞ"}) tail = tail || s.substr(p).starts_with(t);
        if (!tail) return false;
    }
    m.end = p;
    return true;
}

// ([ぁ-ゖ一-龥々])([おこごそとのぼもろよ])っか(?=[なねぁあー～？?！!。、…‥」』）)]|$): the
// volitional with か cut short (行こっか, 帰ろっか, 食べよっか, しよっかな, しちゃおっかな). Not
// そっか: after kana (そっか, あそっか) it is そうか read as an answer, and after kanji the sound
// stage asks the dictionary (RewriteVolitionalSo). Not おっかない, おっかなびっくり, おっかねぇ (scary).
bool VolitionalKa(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Take(s, p, [](char32_t c) { return IsHiraganaClass(c) || IsKanjiClass(c); })) return false;
    const size_t kana = p;
    if (!OneOf(s, p, U"おこごそとのぼもろよ")) return false;
    if (Span(s, kana, p) == "そ") return false;
    const bool o = Span(s, kana, p) == "お";
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "っか")) return false;
    if (o) {
        for (const std::string_view w : {"ない", "なく", "なび", "ね"}) {
            if (s.substr(p).starts_with(w)) return false;
        }
    }
    size_t q = p;
    if (q < s.size() && !OneOf(s, q, U"なねぁあー～？?！!。、…‥」』）)")) return false;
    m.end = p;
    return true;
}

// Whether pos starts a sentence or a clause: the start of s, or after 「『（。！？!?…‥、
// or a space.
bool ClauseStart(std::string_view s, size_t pos) {
    const char32_t c = Before(s, pos);
    return c == 0 || In(c, U"「『（。！？!?…‥、 　");
}

// んで at the start of a clause before 、，？?！!…‥」: それで (んで、どうする).
bool NdeStart(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!ClauseStart(s, pos) || !Lit(s, p, "んで")) return false;
    size_t q = p;
    if (!OneOf(s, q, U"、，？?！!…‥」")) return false;
    m.end = p;
    return true;
}

// Whether a comma stands right before pos.
bool AfterComma(std::string_view s, size_t pos) { return pos > 0 && In(Before(s, pos), U"、"); }

// そ before a copula form of copula.short-so where a clause starts (spaces after its
// punctuation allowed, not after a word and a space: むらで そだった): そう said short. A form of
// copula.short-so-final only before the end of the sentence (あ、そだ。), and one of
// copula.short-so-sentence also not after a comma (大きく、そだった is the verb 育つ).
bool SoCopulaStart(std::string_view s, size_t pos, Match& m) {
    size_t start = pos;
    while (start > 0 && text::IsSpace(Before(s, start))) start -= LastRune(s.substr(0, start)).size;
    size_t p = pos;
    if (!ClauseStart(s, start) || !Lit(s, p, "そ")) return false;
    const std::string_view rest = s.substr(p);
    // Whether a form of n bytes starts rest, with end before no kana or kanji.
    auto starts = [&](std::size_t n, bool end) {
        if (n == 0) return false;
        if (!end || n == rest.size()) return true;
        const char32_t next = text::DecodeRune(rest.substr(n)).cp;
        return !IsHiraganaRune(next) && !IsKanji(next);
    };
    if (!starts(dict::StartsWithMember(HDTRANS_CLASS("copula.short-so"), rest), false) &&
        !starts(dict::StartsWithMember(HDTRANS_CLASS("copula.short-so-final"), rest), true) &&
        (AfterComma(s, start) || !starts(dict::StartsWithMember(HDTRANS_CLASS("copula.short-so-sentence"), rest), true))) {
        return false;
    }
    m.end = p;
    return true;
}

// そ alone where a clause starts, before 、。！？!?…‥」』） or the end: そう said short as an answer
// (そ、あたしのことよ, そ。). Not a stutter before a word that starts with そ or ソ, or before a kanji,
// which the sound stage reads (そ、それは, そ、空), nor a word spelled out kana by kana (そ、れ、よ、り).
bool LoneSo(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!ClauseStart(s, pos) || !Lit(s, p, "そ")) return false;
    m.end = p;
    if (p == s.size()) return true;
    if (!In(At(s, p).cp, U"、。！？!?…‥」』）")) return false;
    size_t q = p;
    while (q < s.size() && In(At(s, q).cp, U"、…‥ 　")) q += At(s, q).size;
    if (q == s.size()) return true;
    const char32_t next = At(s, q).cp;
    if (next == U'そ' || next == U'ソ' || IsKanjiClass(next)) return false;
    // A word spelled out kana by kana (そ、れ、よ、り).
    const size_t after = q + At(s, q).size;
    return !(after < s.size() && At(s, after).cp == U'、');
}

// でなければ, でないと, でなきゃ at the start of a sentence: the conditional of the copula
// without its subject, said for そうでなければ (でなければ、すぐに死ぬ 그렇지 않으면).
bool DenakerebaStart(std::string_view s, size_t pos, Match& m) {
    if (!ClauseStart(s, pos) || AfterComma(s, pos)) return false;
    for (std::string_view w : {"でなければ", "でないと", "でなきゃ"}) {
        size_t p = pos;
        if (Lit(s, p, w)) {
            m.end = pos + std::string_view("で").size();
            return true;
        }
    }
    return false;
}

// The conditional れば said りゃ after a verb stem (寝りゃ治る 寝れば, 見りゃわかる, なりゃ,
// できりゃ, 信じりゃ), and りゃ before しない for りは (わかりゃしない わかりはしない), when a
// word goes on after it. Not それは and これは said そりゃ, こりゃ, どりゃ, nor a shout
// (おりゃ, うりゃっ, せりゃッ) or the pronoun わりゃ, nor ありゃ other than after が or も
// (金がありゃ), nor てりゃ, which the endings read.
bool Rya(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "りゃ")) return false;
    const char32_t c = Before(s, pos);
    if (c == 0 || (!IsHiraganaClass(c) && !IsKanjiClass(c)) || In(c, U"そこどてでっ")) return false;
    const char32_t b = Before(s, pos - 3);  // c, a kana or kanji, is three bytes long
    if (c == U'あ' && !In(b, U"がも")) return false;
    if (In(c, U"おうとせえわ") && !IsHiraganaClass(b) && !IsKanjiClass(b)) return false;  // わりゃ: you
    // A drawn-out あ goes with it before a word (できりゃあ苦労しない).
    size_t q = p;
    while (q < s.size() && In(At(s, q).cp, U"あぁ")) q += At(s, q).size;
    if (q >= s.size() || In(At(s, q).cp, U"あぁっッー～〜")) return false;
    // A space or a comma may stand before that word (言われてみりゃ　そうだ).
    if (q == p && In(At(s, q).cp, U"　 、，")) {
        q += At(s, q).size;
        if (q >= s.size()) return false;
    }
    if (!Take(s, q, [](char32_t r) { return IsHiraganaClass(r) || IsKanjiClass(r); })) return false;
    m.group[1] = "れば";
    // The stem of an ichidan verb says no り there (食われりゃしない 食われはしない, 出られりゃせん): an e- or
    // i-row kana that ends at most three kana after a kanji, none of them a particle (しゃべりゃ, にかえりゃ), or
    // 見, 出, 寝 alone. The なけ of なければ keeps its conditional (事がなけりゃしない).
    const bool nakere = c == U'け' && b == U'な';
    bool ichidan = In(c, U"見出寝");
    if (In(c, U"えけげせぜねべめれきぎじちびみ") && !nakere) {
        size_t k = pos - 3;
        for (int n = 0; n < 2 && k >= 3 && IsHiraganaClass(Before(s, k)) && !In(Before(s, k), U"もにをがはでとのへや"); n++)
            k -= 3;
        ichidan = IsKanjiClass(Before(s, k));
    }
    for (std::string_view neg : {"しない", "しね", "しません", "せん", "せぬ", "しなかった"}) {
        if (s.substr(p).starts_with(neg) && !nakere) m.group[1] = ichidan ? "は" : "りは";
    }
    while (p < s.size() && In(At(s, p).cp, U"あぁ")) p += At(s, p).size;
    m.end = p;
    return true;
}

// しゃ[ーあ](?:ない|ね[ぇえー]*): しょうがない (しゃーない, しゃーねー, ならしゃーない). Not the
// topic は fused to a pronoun in し (あたしゃあね, わしゃあね, わっしゃあ, わたくしゃあ).
bool Shaanai(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    for (const std::string_view stem : {"あた", "わた", "わたく", "わ", "わっ", "あっ"}) {
        if (s.substr(0, pos).ends_with(stem)) return false;
    }
    if (!Lit(s, p, "しゃ") || !OneOf(s, p, U"ーあ")) return false;
    if (!Lit(s, p, "ない")) {
        if (!Lit(s, p, "ね")) return false;
        while (OneOf(s, p, U"ぇえー")) {
        }
    }
    m.end = p;
    return true;
}

// さーて at the start of a clause before と or a mark: さて (さーて、行くか).
bool Saate(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!ClauseStart(s, pos) || !Lit(s, p, "さ")) return false;
    if (!Repeat(s, p, InSet(U"ー～"), 1, kUnbounded, [&](size_t q) {
            size_t r = q;
            if (!Lit(s, r, "て")) return false;
            size_t t = r;
            if (t < s.size() && !Lit(s, t, "と") && !OneOf(s, t, U"、，？?！!…‥」。")) return false;
            m.end = r;
            return true;
        })) {
        return false;
    }
    return true;
}

// ひで[えぇー]+ before a sentence particle or a character other than hiragana: ひどい.
bool Hidee(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "ひで")) return false;
    return Repeat(s, p, InSet(U"えぇー"), 1, kUnbounded, [&](size_t q) {
        size_t r = q;
        if (r < s.size() && !OneOf(s, r, U"なよぞし") && Take(s, r, IsHiraganaClass)) return false;
        m.end = q;
        return true;
    });
}

// おいっ+ at the start of a clause before a mark: the call おい, not 置いて.
bool OiTsu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!ClauseStart(s, pos) || !Lit(s, p, "おい") || !Lit(s, p, "っ")) return false;
    while (Lit(s, p, "っ")) {
    }
    size_t q = p;
    if (q < s.size() && !OneOf(s, q, U"、，？?！!…‥」。")) return false;
    m.end = p;
    return true;
}

// ([いるたうくすつぬぶむぐ])んか(?=[？?」』。…‥！!]|ね|い|よ|$): the question のか with
// の contracted after a predicate (いいんか, あるんか, 信じるんかい). A く that follows
// katakana, two kanji, う, ん, ー or a small kana, or starts the text, is the honorific くん
// (田中くんか, ユウくんかい, ゆうくんか), not the end of a verb (書くんか, いくんか). A た after a
// kanji is left to RewriteContractedN, which tells a past (来たんかい) from the negative stem of a
// godan verb (待たんか).
bool NnKa(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"いるたうくすつぬぶむぐ")) return false;
    if (Span(s, pos, p) == "た" && IsKanjiClass(Before(s, pos))) return false;
    if (Span(s, pos, p) == "く") {
        const text::Rune b = LastRune(s.substr(0, pos));
        if (pos == 0 || IsKatakanaRune(b.cp) || In(b.cp, U"うんーぁぃぅぇぉゃゅょゎ") ||
            (IsKanjiClass(b.cp) && IsKanjiClass(Before(s, pos - b.size)))) {
            return false;
        }
    }
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "んか")) return false;
    size_t q = p;
    if (q < s.size() && !OneOf(s, q, U"？?」』。…‥！!ねいよ")) return false;
    m.end = p;
    return true;
}

// なー+に at the start of a clause: なに drawn out (なーにやってんの).
bool Naani(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!ClauseStart(s, pos) || !Lit(s, p, "な")) return false;
    return Repeat(s, p, InSet(U"ー～"), 1, kUnbounded, [&](size_t q) {
        size_t r = q;
        if (!Lit(s, r, "に")) return false;
        m.end = r;
        return true;
    });
}

// あん(の|だ|でしょ|じゃ) after no hiragana or after が, も, は, に or と: ある with its る said ん
// before の, だ or です (何かあんの？ 何かあるの, 夢があんだよ). The の only before a question, か, よ,
// ね or a mark (not あんの野郎, the demonstrative), the だ not before け (あんだけ is あれだけ).
bool AruN(std::string_view s, size_t pos, Match& m) {
    if (pos > 0) {
        const char32_t b = Before(s, pos);
        if (IsHiraganaClass(b) && !In(b, U"がもはにと")) return false;
    }
    size_t p = pos;
    if (!Lit(s, p, "あん")) return false;
    const size_t q = p;
    if (Lit(s, p, "の")) {
        if (p < s.size() && !In(At(s, p).cp, U"？?かよね！!。、…‥」』　 ")) return false;
        return Done(s, m, q, p, 2);
    }
    for (const std::string_view w : {"だ", "でしょ", "じゃ"}) {
        p = q;
        if (!Lit(s, p, w)) continue;
        if (w == "だ" && p < s.size() && In(At(s, p).cp, U"けっ")) return false;
        if (w == "じゃ" && !(p < s.size() && In(At(s, p).cp, U"んなね"))) return false;
        return Done(s, m, q, p, 2);
    }
    return false;
}

// ([こそど])っから: ここから, そこから, どこから contracted.
bool KoKara(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"こそど")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "っから")) return false;
    m.end = p;
    return true;
}

// ([一-龥々](?:[っしいえけげせぜてねべめれん])?|[一-龥々][かさたなまらわがば]せ|もらっ|まかし|やっ|[にく]なっ|^し|[^ぁ-ゖ]し|[にをう]し)(と|ど)
// (く|こう|こ|いて|いた|いで|きな|きま|きたい|きたく|きたか|きゃ|き|け)([^ぁ-ゖ]|[よねぞわなも]|あげ|くれ|くだ|$), or, after a form other than き,
// から|って|しか|のに|んだ|んで, or
// けば; after the っ or ん of a godan て form in kanji (持っといで, 呼んどいたから), after the なっ of なる
// following に or く (親密になっといてまだ) and after the し of する
// following に, を or う (こうしといたら) whatever follows.
bool Toku(std::string_view s, size_t pos, Match& m) {
    auto after = [&](size_t p, bool suruTe = false) {
        m.group[1] = Span(s, pos, p);
        size_t q = p;
        if (!OneOf(s, q, U"とど")) return false;
        m.group[2] = Span(s, p, q);
        const bool godanTe = suruTe || (p - pos > 3 && IsKanjiClass(At(s, pos).cp) &&
                                        (m.group[1].ends_with("っ") || m.group[1].ends_with("ん")));
        for (std::string_view w : {"く", "こう", "こ", "いて", "いた", "いで", "きな", "きま", "きたい", "きたく", "きたか", "きゃ", "き", "け"}) {
            size_t r = q;
            if (!Lit(s, r, w)) continue;
            m.group[3] = Span(s, q, r);
            if (godanTe) return Done(s, m, r, r, 4);
            size_t t = r;
            if (Take(s, t, [](char32_t c) { return !IsHiraganaClass(c); })) return Done(s, m, r, t, 4);
            t = r;
            if (OneOf(s, t, U"よねぞわなも")) return Done(s, m, r, t, 4);
            t = r;
            if (w == "け" && Lit(s, t, "ば")) return Done(s, m, r, t, 4);
            for (std::string_view e : {"あげ", "くれ", "くだ"}) {
                t = r;
                if (Lit(s, t, e)) return Done(s, m, r, t, 4);
            }
            // from, quote, only, though and ん after a form other than the き of とき (聞いとくから, 寝とけって; not 幼いときから)
            for (std::string_view e : {"から", "って", "しか", "のに", "んだ", "んで"}) {
                t = r;
                if (w != "き" && Lit(s, t, e)) return Done(s, m, r, t, 4);
            }
            if (r == s.size()) return Done(s, m, r, r, 4);
        }
        return false;
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass)) {
        const bool ok = Repeat(s, p, InSet(U"っしいえけげせぜてねべめれん"), 0, 1, after);
        if (ok) return true;
    }
    p = pos;
    // the causative stem of a godan verb (泣かせとけば, 待たせとく)
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"かさたなまらわがば") && Lit(s, p, "せ") && after(p)) return true;
    for (std::string_view w : {"もらっ", "まかし", "やっ"}) {
        p = pos;
        if (Lit(s, p, w) && after(p)) return true;
    }
    p = pos;
    // なっとく is also 納得 in kana: as the form of おく only where the rest allows it.
    // なっとく where the sentence ends is 納得 (本当になっとく。).
    if (OneOf(s, p, U"にく") && Lit(s, p, "なっ") && ((after(p, true) && m.group[3] != "く") || after(p))) {
        return m.group[3] != "く" || (!m.group[4].empty() && IsHiraganaClass(At(m.group[4], 0).cp));
    }
    p = pos;
    if (pos == 0 && Lit(s, p, "し") && after(p)) return true;
    p = pos;
    if (OneOf(s, p, U"にをう") && Lit(s, p, "し") && after(p, true)) return true;  // ようにしとけ, こうしといたら
    p = pos;
    return Take(s, p, [](char32_t c) { return !IsHiraganaClass(c); }) && Lit(s, p, "し") && after(p);
}

// Toku where the text around allows it: やっといた that starts a clause and is cut off by a
// mark before a verb of searching or finding in its sentence is the adverb やっと before
// いた (やっといた、探したよ), not やっておいた (やっといたよ).
bool TokuInText(std::string_view s, size_t pos, Match& m) {
    constexpr std::string_view kFound = "やっといた";
    if (s.substr(pos).starts_with(kFound) && ClauseStart(s, pos)) {
        size_t p = pos + kFound.size();
        if (OneOf(s, p, U"、，！!…‥")) {
            std::string_view rest = s.substr(p);
            rest = rest.substr(0, std::min(rest.find("。"), rest.find("」")));
            for (std::string_view v : {"探", "捜", "見つけ", "見つか", "見っけ", "発見", "さがし", "みつけ", "みつか"}) {
                if (rest.find(v) != std::string_view::npos) return false;
            }
        }
    }
    return Toku(s, pos, m);
}

// ([そこあど])[ーう]ゆ[うー]
bool Souyuu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"そこあど")) return false;
    m.group[1] = Span(s, pos, p);
    if (!OneOf(s, p, U"ーう") || !Lit(s, p, "ゆ") || !OneOf(s, p, U"うー")) return false;
    m.end = p;
    return true;
}

// The る contracted to ん before の, だ or です written in full.
std::string ContractedRu(std::string_view stem, std::string_view after) {
    if (after == "の") return Cat({stem, "るの"});
    return Cat({stem, "るん", after});
}

// The forms of おく for the contracted とく.
std::string_view TokuForm(std::string_view w) {
    if (w == "く") return "おく";
    if (w == "こう" || w == "こ") return "おこう";
    if (w == "いて") return "おいて";
    if (w == "いた") return "おいた";
    if (w == "いで") return "おいで";
    if (w == "きま") return "おきま";  // 言っときます
    if (w == "きたい") return "おきたい";  // 持っときたい, not と来た
    if (w == "きたく") return "おきたく";
    if (w == "きたか") return "おきたか";
    if (w == "きゃ") return "おけば";  // 撮っときゃ
    if (w == "きな") return "おきな";
    return "おけ";  // き, け
}

// s ends with a kanji other than 見来寝着出居 (all kanji are three bytes long).
bool EndsWithStemKanji(std::string_view s) {
    if (s.size() < 3) return false;
    const text::Rune last = text::DecodeRune(s.substr(s.size() - 3));
    return last.size == 3 && IsKanji(last.cp) && !In(last.cp, U"見来寝着出居");
}

bool AdjectiveEnd(char32_t c) { return In(c, U"いくかけそ"); }
bool Bakkashi(char32_t c) { return In(c, U"のでだじや、。！？!?…‥」』　 ") || c == 0; }
bool NoKana(char32_t c) { return !IsHiraganaRune(c) && !IsKatakanaRune(c); }
bool NoKatakana(char32_t c) { return !IsKatakanaRune(c); }

// A word said with an emphatic っ or ん inside and its plain spelling, where next holds for
// the rune after it (0 at the end) and, with clause, where a clause starts before it.
struct EmphaticSpelling {
    std::string_view written, plain;
    bool (*next)(char32_t) = nullptr;
    bool clause = false;
};
constexpr EmphaticSpelling kEmphaticSpellings[] = {
    {"さっすが", "さすが"},
    {"しっかた", "しかた"},  // しっかたないなぁ
    {"すんご", "すご"},      // すんごい, すんごく
    {"あったりまえ", "あたりまえ"},
    {"あったり前", "あたり前"},
    {"くっさ", "くさ", AdjectiveEnd},  // くっさい
    {"くっさ", "臭っ", NoKana},        // うわ、くっさ。
    {"あっつ", "暑", AdjectiveEnd},  // あっついねー
    {"きっつ", "きつ", AdjectiveEnd},
    {"キッツ", "きつ", AdjectiveEnd},  // キッツい
    {"ばっかし", "ばかり", Bakkashi},  // not ばっか with する: 勉強ばっかしてる
    {"うっせ", "うるせえ", NoKana},
    {"バッカ", "バカ", NoKatakana},          // not バッカス
    {"ばっかじゃ", "バカじゃ", nullptr, true},  // not the ばかり of 学食ばっかじゃ
    {"ばっかみたい", "バカみたい", nullptr, true},
    {"大っ嫌", "大嫌"},  // 大っ嫌い
    {"大っきらい", "大嫌い"},
    {"大っ好き", "大好き"},
    {"だいっきらい", "だいきらい"},
    {"だいっすき", "だいすき"},
};

bool EmphaticAt(std::string_view s, size_t pos, Match& m) {
    for (const EmphaticSpelling& e : kEmphaticSpellings) {
        size_t p = pos;
        if (!Lit(s, p, e.written) || (e.clause && !ClauseStart(s, pos))) continue;
        if (e.next != nullptr && !e.next(p < s.size() ? At(s, p).cp : 0)) continue;
        m.group[1] = e.plain;
        m.end = p;
        return true;
    }
    return false;
}

}  // namespace

// The kana before a casual negative ねぇ, ねえ or ねー that a verb of the dictionary reads as its negative stem
// (VerbNee in RewriteCasual): group 1 the stem kana, group 2 the follower.
bool VerbNee(std::string_view s, size_t pos, Match& m, const RunLookup& runs) {
    // わ and the ichidan stems in い or e are left to Nee, which reads them after a kanji: in kana they are
    // mostly the particles わ and ね or a て form (いいわねぇ, 怖いねぇ, 聞かせてねぇ, それねー); え stays, the
    // potential of a verb in う (もらえねぇか).
    static constexpr std::u32string_view kARow = U"かがさたなばまら";
    static constexpr std::u32string_view kURow = U"くぐすつぬぶむる";
    static constexpr std::u32string_view kStemRow = U"え";
    // An ichidan stem in i or e right after the kanji of its verb (通じねえ, 起きねえ, 忘れねえ), where Nee
    // wants a following kana; not after an adnominal い, な, の, the quoting って or the honorific お or ご, where
    // the kanji and kana are a noun (いい感じねえ, って感じねぇ, お疲れねえ).
    static constexpr std::u32string_view kKanjiStemRow = U"きぎじちびみりけげせぜべめれ";
    size_t p = pos;
    const text::Rune k = text::DecodeRune(s.substr(p));
    if (k.size == 0 || !IsHiraganaRune(k.cp)) return false;
    const size_t a = kARow.find(k.cp);
    bool ichidan = kStemRow.find(k.cp) != std::u32string_view::npos;
    if (!ichidan && kKanjiStemRow.find(k.cp) != std::u32string_view::npos && pos > 0 && IsKanjiClass(Before(s, pos))) {
        const std::string_view head = s.substr(0, pos);
        const char32_t c = Before(head, head.size() - LastRune(head).size);
        // お or ご before the kanji makes it a noun (お疲れねえ, お別れねぇ, お任せねえ: the particle ねえ).
        ichidan = !In(c, U"いなのておご");
    }
    // The せ of a causative after the a-row kana of its verb (させねえ, 行かせねえ, 知らせねえ), not the ませ of
    // the polite.
    const bool causative = !ichidan && k.cp == U'せ' && pos > 0 && In(Before(s, pos), U"かがさたなばらわ");
    ichidan = ichidan || causative;
    if (a == std::u32string_view::npos && !ichidan) return false;
    p += k.size;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ね")) return false;
    const bool bar = OneOf(s, p, U"ー");
    if (!bar && !OneOf(s, p, U"ぇえ")) return false;
    if (s.substr(p).starts_with("も")) return false;
    if (!CharOrEnd(s, p, bar ? U"なよぜぞしかけだっわの" : U"なよぜぞしかけだっわの。、！？!?…‥」』）) 　", m, 2)) return false;
    // The particle ね after から, なら, で or the question か (ですからねえ, 君ならねぇ, 東京でねえ, ですかねぇ).
    const std::string_view head = s.substr(0, pos);
    if (k.cp == U'で' || (k.cp == U'ら' && (head.ends_with("か") || head.ends_with("な"))) ||
        (k.cp == U'か' && (head.ends_with("す") || head.ends_with("ま") || head.ends_with("の")))) {
        return false;
    }
    // The particle やら after a kanji, なに or どう (何やらねぇ、, どうやらねえ), as in Nee.
    if (k.cp == U'ら' && head.ends_with("や")) {
        const std::string_view lead = head.substr(0, head.size() - std::string_view("や").size());
        if (lead.ends_with("なに") || lead.ends_with("どう") || (!lead.empty() && IsKanjiClass(Before(lead, lead.size())))) return false;
    }
    // An ichidan stem after its kanji (kKanjiStemRow): the kanji, the stem and る must spell a verb whole.
    if (causative) return true;
    if (a == std::u32string_view::npos && kStemRow.find(k.cp) == std::u32string_view::npos) {
        if (runs.verbForm == nullptr) return false;
        // Before a space the casual negative may end a caption phrase that another starts after (許せねぇ 美人なだけに),
        // which a rewritten ない would join as a modifier; Nee keeps that case.
        if (m.group[2] == " " || m.group[2] == "　") return false;
        const std::string_view kanji = head.substr(head.size() - LastRune(head).size);
        const std::string stem = std::string(s.substr(pos, k.size)) + "る";
        // 好き before ねえ is the adjectival noun with the particle (好きねえ), which 着る would read as a verb.
        if (kanji == "好" && k.cp == U'き') return false;
        // A verb whole (通じる), or a potential or a longer verb the dictionary has (許せねえ 許せる).
        if (runs.verbForm(runs.context, std::string(kanji) + stem)) return true;
        // A potential in e (許せねえ, 動けねえ) the dictionary has as a longer form; not a stem in i, which may be
        // the old imperative of a godan verb (飲みねえ 飲みなさい) that 見る would read as ichidan.
        return std::u32string_view(U"けげせぜべめれ").find(k.cp) != std::u32string_view::npos &&
               runs.dictionaryVerb(runs.context, std::string(head) + stem);
    }
    std::string form(head);
    if (a != std::u32string_view::npos) {
        text::AppendRune(form, kURow[a]);
    } else {
        form += std::string(s.substr(pos, k.size));
        form += "る";
    }
    return runs.dictionaryVerb(runs.context, form);
}

// The lookups of the RewriteCasual call that VerbNeeAt reads, set around its scan.
thread_local const RunLookup* verbNeeRuns = nullptr;

bool VerbNeeAt(std::string_view s, size_t pos, Match& m) { return verbNeeRuns != nullptr && VerbNee(s, pos, m, *verbNeeRuns); }

std::string RewriteEmphaticSpellings(std::string_view in) {
    return ReplaceAll(in, EmphaticAt, [](const Match& m) { return std::string(m.group[1]); });
}

// Slurred and colloquial spellings of standard words, read as the standard spelling (SpokenSpelling): from,
// to, the characters one of which must stand before from (empty: any), whether the start of the text also
// does, and the characters that may not follow from.
struct Respelling {
    std::string_view from, to;
    std::u32string_view before;
    std::u32string_view notAfter;
    bool atStart = true;
};
constexpr Respelling kRespellings[] = {
    {"ちったぁ", "ちっとは", U"", U""},          // ちったぁ教育の必要が ちっとは
    {"ちったあ", "ちっとは", U"", U""},
    {"ぐれぇ", "ぐらい", U"", U""},              // 捻り潰すぐれぇ簡単 ぐらい (not くれぇ, the drawn くれ)
    {"ぐれえ", "ぐらい", U"", U""},
    {"ばっか", "ばかり", U"たてだで", U"りじ", false},  // 新調したばっか ばかり; not ばっかじゃない (ばか)
    {"あーゆー", "ああいう", U"", U""},          // あーゆーいい子 ああいう, as こーゆー and そーゆー
    {"つうか", "つーか", U"、「『 　", U""},    // つうか、世間セマ (not the 通過 of 通過)
    {"ましぇ", "ませ", U"", U""},                // お答えできましぇん ません
    {"どぉーせ", "どうせ", U"", U""},            // どぉーせラブラブ どうせ
    {"どぉせ", "どうせ", U"", U""},
    {"良さげ", "良さそう", U"", U""},            // 心地良さげに 기분 좋은 듯이: the げ of appearance is そう (spec LEX-3843)
    {"有りそう", "ありそう", U"", U""},          // 何か有りそうだし 뭔가 있을 것 같고, not 有り 괜찮다
};

bool SpokenSpelling(std::string_view s, size_t pos, Match& m) {
    for (const Respelling& r : kRespellings) {
        if (!s.substr(pos).starts_with(r.from)) continue;
        if (!r.before.empty()) {
            const char32_t b = Before(s, pos);
            if (b == 0 ? !r.atStart : !In(b, r.before)) continue;
        }
        const size_t end = pos + r.from.size();
        if (end < s.size() && In(At(s, end).cp, r.notAfter)) continue;
        m.group[1] = r.to;
        m.end = end;
        return true;
    }
    return false;
}

// 仕切れ after a verbal noun in kanji (処理仕切れない, 説明仕切れず): the しきれ of a task one cannot finish, 다 V하지
// 못하다 (spec LEX-3843), not 仕切る, to partition, which takes its object with を (部屋を仕切れない).
// なさげ after a noun in kanji or katakana (興味なさげに, 自信なさげ): なさそう, its appearance (spec LEX-3843); not after
// kana (なにもなさげ), where it is not split the same way.
bool KanjiNasage(std::string_view s, size_t pos, Match& m) {
    if (pos == 0 || !(IsKanjiClass(Before(s, pos)) || IsKatakanaRune(Before(s, pos)))) return false;
    size_t p = pos;
    if (!Lit(s, p, "なさげ")) return false;
    m.group[1] = "なさそう";
    m.end = p;
    return true;
}

bool KanjiShikire(std::string_view s, size_t pos, Match& m) {
    if (pos == 0 || !IsKanjiClass(Before(s, pos))) return false;
    size_t p = pos;
    if (!Lit(s, p, "仕切れ")) return false;
    if (p >= s.size() || !In(At(s, p).cp, U"なずませ")) return false;
    m.group[1] = "しきれ";
    m.end = p;
    return true;
}

// The いく of a て form said く before a noun or a demonstrative (染まってく坂道, まきこまれてくその): ていく, going on
// (spec GRM-3850). Not てくれ, てくる, てくだ, nor before ん (てくんない てくれない).
bool TeKuNoun(std::string_view s, size_t pos, Match& m) {
    const char32_t b = Before(s, pos);
    if (b == 0 || !(b == U'っ' || b == U'ん' || In(b, U"いきぎしちにびみりえけげせてねべめれじ"))) return false;
    size_t p = pos;
    if (!OneOf(s, p, U"てで")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "く") || p >= s.size()) return false;
    const char32_t n = At(s, p).cp;
    const std::string_view rest = s.substr(p);
    const bool noun = IsKanjiClass(n) || IsKatakanaRune(n) || rest.starts_with("その") || rest.starts_with("この") || rest.starts_with("あの");
    if (!noun || rest.starts_with("下さ")) return false;  // 聞いてく下さい: the ください of a request, misspelled
    m.end = p;
    return true;
}

// なきゃ said with a copula for "must" (行かなきゃだし, しなきゃだよ): なきゃいけない, the obligation; not
// なきゃだめ, which reads whole.
bool NakyaDa(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "なきゃだ")) return false;
    if (p < s.size() && !In(At(s, p).cp, U"しよねな。、！？!?…‥」』）) 　")) return false;
    m.end = p;
    return true;
}

// The て form with こねえ, こねー, こねぇ, the casual negative of くる (かかってこねーのか): てこない.
bool TeKoNee(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"てで")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "こね") || !OneOf(s, p, U"ぇえー")) return false;
    m.end = p;
    return true;
}

// Whether s holds a kanji.
bool HasKanjiText(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        if (IsKanjiClass(r.cp)) return true;
        i += std::max<size_t>(1, r.size);
    }
    return false;
}

// The る (or るの) of a verb said ん before の, だ, じゃ, です or なら (喋んの, 馬鹿にすんの, 過ぎんだよ, 待たせんのよ):
// the verb in its dictionary form. Only where the ん can be no other: a godan verb in る of one kanji before の
// (喋んの; not 死んだ, 飲んじゃ, the past and the て form of godan verbs in む, ぶ, ぬ), a verb of the dictionary read
// from up to three characters back before の or なら (stemVerb below), and する said す. The ん after an ichidan stem
// before のだ, のか or だろ (疲れんだろ, 出来んのか) is as often the old or western negative, so it stays.
// The lookups of the RewriteSlurred call that RuContraction reads, set around its scan. Each thread sets its own:
// a shared pointer would let one thread read the lookups of another call after it has returned.
thread_local const RunLookup* ruContractionRuns = nullptr;
bool RuContraction(std::string_view s, size_t pos, Match& m) {
    const RunLookup* runs = ruContractionRuns;
    if (runs == nullptr || runs->verbForm == nullptr || runs->dictionaryVerb == nullptr || pos == 0) return false;
    size_t p = pos;
    if (!Lit(s, p, "ん")) return false;
    const std::string_view after = s.substr(p);
    const bool no = after.starts_with("の") || after.starts_with("なら");
    if (!no && !after.starts_with("だ") && !after.starts_with("じゃ") && !after.starts_with("です")) return false;
    const std::string_view head = s.substr(0, pos);
    const text::Rune last = LastRune(head);
    const std::string_view rest = head.substr(0, head.size() - last.size);
    const char32_t before = rest.empty() ? 0 : LastRune(rest).cp;
    bool verb = false;
    // A verb of the dictionary whose stem ends right before the ん, read from up to three characters back (いんの いるの,
    // 攻めんなら, 清めんのよ, 分かんの, 帰ってくんの くるの, あんの; spec GRM-3960): before の or なら the ん is る whatever
    // the verb, an ichidan one too. A kana stem of one character counts only for いる, ある and the くる of a て form.
    // A question or a casual の after it (の？, のよ, のね, なら); のだ, のです, のじゃ and のか are not enough for an
    // ichidan stem.
    const bool question = after.starts_with("の？") || after.starts_with("の?") || after.starts_with("のか") || after.starts_with("のよ");
    const bool casual = after.starts_with("の？") || after.starts_with("の?") || after.starts_with("のよ") || after.starts_with("のね") ||
                        after.starts_with("なら");
    const auto stemVerb = [&](bool allowKana) {
        std::string_view tail = head;
        std::string stem;
        for (int k = 0; k < 3 && !tail.empty(); k++) {
            const text::Rune r = LastRune(tail);
            if (!IsHiraganaRune(r.cp) && !IsKanjiClass(r.cp) && !IsKatakanaRune(r.cp)) break;
            stem.insert(0, std::string(tail.substr(tail.size() - r.size)));
            tail.remove_suffix(r.size);
            // A stem in hiragana alone counts only as いる, ある or the くる of a て form (いんの, あんの, てくんの).
            if (!HasKanjiText(stem) && !IsKatakanaRune(text::DecodeRune(stem).cp)) {
                const char32_t lead = tail.empty() ? 0 : LastRune(tail).cp;
                const bool word = lead == 0 || IsKanjiClass(lead) || In(lead, U"にでがはもへ、。 　「…") || tail.ends_with("とか") ||
                                  tail.ends_with("なんか");
                const bool ok = allowKana && ((stem == "い" && word) || (stem == "あ" && word && question) ||
                                              (stem == "く" && (tail.ends_with("て") || tail.ends_with("で"))) ||
                                              (stem == "な" && (tail.ends_with("に") || tail.ends_with("く")) && casual));
                if (!ok) continue;
            }
            // A stem starts a word: not the 伝わ of 手伝わんのだ, the negative of 手伝う, where a word of the dictionary
            // (手伝) holds the kanji before it with its first (責任取れんの stays a stem, as no word holds 任取).
            if (HasKanjiText(stem) && !tail.empty() && IsKanjiClass(LastRune(tail).cp) && runs->holds != nullptr &&
                runs->holds(runs->context, s, tail.size() - LastRune(tail).size, tail.size() + text::DecodeRune(stem).size)) {
                continue;
            }
            // The い of an adjective before なら (可愛いんなら) is no いる.
            if (stem == "い" && !after.starts_with("の") && !tail.empty() && !In(LastRune(tail).cp, U"にがもはでへ")) return false;
            // The さん of a name (森下さんの) is no ん of 下さる; なんの before a noun is 何の (人生になんの意味が).
            if (stem.ends_with("さ") || (stem.ends_with("な") && !casual)) return false;
            // An ichidan or potential stem (e- or i-row kana before the ん) is as often the old negative (足りんのだ, 言えんのだ):
            // only a question or a casual の, のよ, のね or なら takes it as る (清めんのよ, 攻めんなら, いんの？).
            const char32_t end = LastRune(stem).cp;
            if ((In(end, U"いきぎしちにびみりえけげせてねべめれ見居着寝似煮干射得経来出") && stem != "い") && !casual &&
                !IsKatakanaRune(text::DecodeRune(stem).cp)) {
                return false;
            }
            if (runs->verbForm(runs->context, stem + "る")) return true;
        }
        return false;
    };
    if (no && stemVerb(true)) {
        verb = true;
    } else if (!no && last.cp == U'い' && (In(before, U"にがもは") || rest.ends_with("ほど"))) {
        // いる said い before んだ, んじゃ or んです after a particle (どこにいんだよ, 腐るほどいんだよ); not the い of an
        // adjective (多いんだ, いいんだ).
        verb = true;
    } else if (IsKanjiClass(last.cp) && no) {
        // A godan verb in る of one kanji (喋んの, 帰んの); not an ichidan one (見んの, 居んの, 出来んのか), whose ん is
        // as often the old or western negative.
        verb = !In(last.cp, U"見居着寝似煮干射得経来出") && runs->verbForm(runs->context, std::string(head.substr(rest.size())) + "る");
    } else if (last.cp == U'す') {
        // する said す after を, に, も, は or どう, そう, こう, 何 (馬鹿にすんの, どうすんの), before の, なら, じゃ or だろ; not
        // すんだ, the past of 済む (落ちずにすんだ), nor the す of a verb (落とすんだ, 脱がすんだ).
        const bool when = no || after.starts_with("じゃ") || after.starts_with("だろ");
        const bool word = In(before, U"をにもは何") || rest.ends_with("どう") || rest.ends_with("そう") || rest.ends_with("こう");
        verb = when && word;
    }
    if (!verb) return false;
    // ん stands for る before の and なら (するの, するなら), and for るん before だ, じゃ and です (過ぎるんだ).
    m.group[1] = no ? "る" : "るん";
    m.end = p;
    return true;
}

// Contractions of spoken verbs, each read as its standard spelling (spec GRM-3960):
// - なんない, なんなく, なんねぇ after に, く, と or よう: ならない (話になんねぇ 이야기가 안 된다, うまくなんない);
// - お帰んなさい and the like, a godan verb in る with んなさい: りなさい (a bare 帰んな may be るな or りな, so it stays);
// - てっから, てっけど, and でっから after ん or い: てるから (出してっから 내고 있으니까);
// - ねんだ after the a-row stem of a godan verb: ないんだ (知らねんだ);
// - てんじゃん: てるじゃん;
// - どうすっか, 何すっか: するか;
// - だってのか: だというのか (魔女だってのか 마녀라는 거야);
// - やがんだ, やがんの: やがるんだ, やがるの (何しやがんだ).
bool SpokenVerbContraction(std::string_view s, size_t pos, Match& m) {
    const RunLookup* runs = ruContractionRuns;
    const std::string_view rest = s.substr(pos);
    const std::string_view head = s.substr(0, pos);
    // ならない
    for (const std::string_view w : {"なんない", "なんなく", "なんなか", "なんねぇ", "なんねえ", "なんねー"}) {
        if (!rest.starts_with(w)) continue;
        if (!(head.ends_with("に") || head.ends_with("く") || head.ends_with("と") || head.ends_with("よう"))) return false;
        // なんねえ is the whole negative ならない (仕事になんねえじゃねぇか).
        m.group[1] = w.starts_with("なんな") ? std::string_view("ならな") : std::string_view("ならない");
        m.end = pos + (w.starts_with("なんな") ? std::string_view("なんな").size() : w.size());
        return true;
    }
    // りなさい said んなさい (お帰んなさい); a bare んな may be the prohibition るな or the request りな (帰んな！), so it stays
    if (rest.starts_with("んな") && pos > 0 && runs != nullptr && runs->verbForm != nullptr) {
        std::string_view tail = head;
        std::string stem;
        for (int k = 0; k < 3 && !tail.empty(); k++) {
            const text::Rune r = LastRune(tail);
            if (!IsHiraganaRune(r.cp) && !IsKanjiClass(r.cp)) break;
            stem.insert(0, std::string(tail.substr(tail.size() - r.size)));
            tail.remove_suffix(r.size);
            if (!HasKanjiText(stem) || !runs->verbForm(runs->context, stem + "る")) continue;
            const size_t after = pos + std::string_view("んな").size();
            if (s.substr(after).starts_with("さい")) {
                m.group[1] = "りな";
                m.end = after;
                return true;
            }
            return false;
        }
        return false;
    }
    // てるから
    for (const std::string_view w : {"てっから", "てっけど", "てっけ", "でっから", "でっけど"}) {
        if (!rest.starts_with(w) || pos == 0) continue;
        const char32_t b = LastRune(head).cp;
        if (!(b == U'っ' || b == U'ん' || IsHiraganaRune(b) || IsKanjiClass(b))) return false;
        if (w.starts_with("で") && b != U'ん' && b != U'い') return false;  // 読んでっから; not せっかくでっけど
        if (head.ends_with("連れ")) return false;                          // 連れてっから is 連れてくから
        const bool kara = w.ends_with("から");
        m.group[1] = w.starts_with("て") ? (kara ? "てるから" : "てるけ") : (kara ? "でるから" : "でるけ");
        m.end = pos + (kara ? w.size() : std::string_view("てっけ").size());
        return true;
    }
    // a negative ないんだ said ねんだ after the a-row stem of a godan verb (知らねんだ 모르는 거야)
    if ((rest.starts_with("ねんだ") || rest.starts_with("ねんの") || rest.starts_with("ねんじゃ") || rest.starts_with("ねんです")) && pos > 0 &&
        In(LastRune(head).cp, U"かがさたなばまらわ") && HasKanjiText(head.substr(head.size() >= 6 ? head.size() - 6 : 0))) {
        m.group[1] = "ないん";
        m.end = pos + std::string_view("ねん").size();
        return true;
    }
    // てるじゃん said てんじゃん (恋してんじゃん 사랑하고 있잖아); てんじゃねえ stays the prohibition.
    // After ちゃっ, じゃっ it stays (分かっちゃってんじゃん reads better as the plain past).
    if ((rest.starts_with("てんじゃん") || rest.starts_with("でんじゃん")) && pos > 0 && !head.ends_with("ちゃっ") && !head.ends_with("じゃっ")) {
        m.group[1] = rest.starts_with("て") ? "てる" : "でる";
        m.end = pos + std::string_view("てん").size();
        return true;
    }
    // するか
    if ((rest.starts_with("すっか") || rest.starts_with("すっけ")) && (head.ends_with("どう") || head.ends_with("何") || head.ends_with("なに"))) {
        m.group[1] = rest.starts_with("すっか") ? "するか" : "するけ";
        m.end = pos + std::string_view("すっか").size();
        return true;
    }
    // というの
    if (rest.starts_with("ってのか") && !rest.starts_with("ってのかい") && head.ends_with("だ")) {
        m.group[1] = "というのか";
        m.end = pos + std::string_view("ってのか").size();
        return true;
    }
    // やがるん, やがるの
    // やがんだろ stays: やがるんだろ reads no better.
    if ((rest.starts_with("やがんだ") || rest.starts_with("やがんの") || rest.starts_with("やがんです")) && !rest.starts_with("やがんだろ") && pos > 0) {
        m.group[1] = rest.starts_with("やがんの") ? "やがるの" : "やがるんだ";
        m.end = pos + (rest.starts_with("やがんの") ? std::string_view("やがんの").size() : std::string_view("やがんだ").size());
        if (rest.starts_with("やがんです")) {
            m.group[1] = "やがるんです";
            m.end = pos + std::string_view("やがんです").size();
        }
        // The question い of やがんだい reads as the plain question (何見てやがんだい 뭘 보고 있는 거야).
        if (rest.starts_with("やがんだい")) m.end = pos + std::string_view("やがんだい").size();
        return true;
    }
    return false;
}

std::string RewriteSlurred(std::string_view in, const RunLookup& runs) {
    std::string text = ReplaceAll(in, SpokenSpelling, [](const Match& m) { return std::string(m.group[1]); });
    text = ReplaceAll(text, NakyaDa, [](const Match&) { return std::string("なきゃいけない"); });
    text = ReplaceAll(text, KanjiShikire, [](const Match& m) { return std::string(m.group[1]); });
    text = ReplaceAll(text, KanjiNasage, [](const Match& m) { return std::string(m.group[1]); });
    text = ReplaceAll(text, TeKuNoun, [](const Match& m) { return Cat({m.group[1], "いく"}); });
    text = ReplaceAll(text, TeKoNee, [](const Match& m) { return Cat({m.group[1], "こない"}); });
    // Set for this scan only and put back after it, also when the scan throws.
    struct Scope {
        const RunLookup* saved;
        explicit Scope(const RunLookup* r) : saved(ruContractionRuns) { ruContractionRuns = r; }
        ~Scope() { ruContractionRuns = saved; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    } scope(&runs);
    text = ReplaceAll(text, RuContraction, [](const Match& m) { return std::string(m.group[1]); });
    return ReplaceAll(text, SpokenVerbContraction, [](const Match& m) { return std::string(m.group[1]); });
}

std::string RewriteCasual(std::string_view in, const RunLookup& runs, bool contracted) {
    std::string text = ReplaceAll(in, Shiren, [](const Match& m) { return Cat({"かも", m.group[1], "ない"}); });
    text = ReplaceAll(text, TeNeTsuNo, [](const Match& m) { return Cat({m.group[1], "いないっての"}); });
    text = ReplaceAll(text, Nee, [](const Match& m) {
        const Match g = Rematch(m, Nee);
        std::string stem(g.group[1]);
        if (stem == "て" || stem == "で") {
            if (g.group[2].empty()) return std::string(m.group[0]);
            stem += "い";
        } else if (stem.ends_with("ん")) {
            stem.resize(stem.size() - std::string_view("ん").size());
            stem += "ら";
        }
        return Cat({stem, "ない", g.group[2]});
    });
    // A negative stem in ra, ka, ma... after more kana than Nee takes, or an ichidan or potential stem, before
    // ねぇ, ねえ or ねー: a verb of the dictionary in its dictionary form (見当たらねぇ 見当たらない,
    // 止まらねえ, 放っておかねぇ, もらえねぇか). Not the particle after から or なら (ですからねえ).
    if (runs.dictionaryVerb != nullptr) {
        verbNeeRuns = &runs;
        text = ReplaceAll(text, VerbNeeAt, [](const Match& m) { return Cat({m.group[1], "ない", m.group[2]}); });
        verbNeeRuns = nullptr;
    }
    text = ReplaceAll(text, NounNee, [](const Match& m) {
        const bool wake = m.group[1] == "わけ" || m.group[1] == "訳";  // わけがない, not 訳ない (easy)
        return Cat({m.group[1], wake ? "がない" : "ない"});
    });
    text = ReplaceAll(text, GaNee, [](const Match& m) { return Cat({m.group[1], "ない"}); });
    text = ReplaceAll(text, DrawnCallNee, [](const Match&) { return std::string("ねえ"); });
    text = ReplaceAll(text, LoneNee, [](const Match&) { return std::string("ないよ"); });
    text = ReplaceAll(text, Chimau, [](const Match& m) {
        const Match g = Rematch(m, Chimau);
        if (!g.group[1].empty()) return Cat({g.group[1], "ちゃ", g.group[2]});
        return Cat({"んじゃ", g.group[3]});
    });
    text = ReplaceAll(text, Chitta, [](const Match& m) {
        const Match g = Rematch(m, Chitta);
        if (!g.group[1].empty()) return Cat({g.group[1], "ちゃった"});
        return std::string("んじゃった");
    });
    text = ReplaceAll(text, NounTteNda, [](const Match& m) {
        const Match g = Rematch(m, NounTteNda);
        return Cat({g.group[1], "っていうんだ"});
    });
    // The copula でやがる first, so that the でいやがる of ている is not taken for it.
    text = ReplaceAll(text, DeYagaru, [](const Match& m) { return std::string(m.group[1]); });
    text = ReplaceAll(text, TeIyagaru, [](const Match& m) { return Cat({m.group[1], "やが"}); });
    text = ReplaceAll(text, AnNo, [](const Match& m) {
        const Match g = Rematch(m, AnNo);
        return Cat({"あるの", g.group[1]});
    });
    text = ReplaceAll(text, Moota, [](const Match& m) {
        const Match g = Rematch(m, Moota);
        return Cat({g.group[1], "しま", g.group[2] == "た" ? "った" : "って"});
    });
    text = ReplaceAll(text, Tsutte, [](const Match& m) {
        return Cat({m.group[1] == "ん" ? "んて" : "って", "言っ", m.group[2]});
    });
    text = ReplaceAll(text, TsuNo, [](const Match&) { return std::string("っての"); });
    text = ReplaceAll(text, Tsuu, [](const Match&) { return std::string("という"); });
    // The prohibition spelled with ん is read first, so that its emphasis っ goes as that of
    // るな does (すんなッ！ するな！).
    text = ReplaceAll(text, contracted ? Nna<true> : Nna<false>, [](const Match& m) { return Cat({m.group[1], "るな", m.group[2]}); });
    text = ReplaceAll(text, NaTsu, [](const Match& m) { return Cat({m.group[1], "な", m.group[2]}); });
    text = ReplaceAll(text, Sun, [](const Match& m) {
        const Match g = Rematch(m, Sun);
        return ContractedRu(Cat({g.group[1], "す"}), g.group[2]);
    });
    text = ReplaceAll(text, Run, [](const Match& m) {
        const Match g = Rematch(m, Run);
        return ContractedRu(g.group[1], g.group[2]);
    });
    text = ReplaceAll(text, Literal<kKya>, [](const Match&) { return std::string("しかない"); });
    text = ReplaceAll(text, Teeka, [](const Match& m) { return Cat({m.group[1], "たいか"}); });
    text = ReplaceAll(text, Literal<kUssai>, [](const Match&) { return std::string("うるさい"); });
    text = ReplaceAll(text, Literal<kSsuKatakana>, [](const Match&) { return std::string("っす"); });
    text = ReplaceAll(text, SsuKatakanaAfterWord, [](const Match&) { return std::string("っす"); });
    text = ReplaceAll(text, Literal<kNaiNsuKatakana>, [](const Match&) { return std::string("ないんす"); });
    text = ReplaceAll(text, AdjectiveSu, [](const Match& m) { return Cat({"いっす", m.group[1]}); });
    text = ReplaceAll(text, SsuMidSentence, [](const Match& m) { return std::string(m.group[1]); });
    // てんす and でんす are the contracted ている before it (言ってんすか 言ってるんですか).
    text = ReplaceAll(text, contracted ? Nsu<true> : Nsu<false>, [contracted](const Match& m) {
        const bool te = contracted && (m.group[1] == "て" || m.group[1] == "で");
        return Cat({m.group[1], te ? "るんです" : "んです", m.group[2]});
    });
    text = ReplaceAll(text, KonnaN, [](const Match& m) { return Cat({m.group[1], "の"}); });
    text = ReplaceAll(text, QuestionKai, [](const Match& m) { return Cat({m.group[1], m.group[2]}); });
    text = ReplaceAll(text, Ikka, [](const Match& m) { return Cat({"いいか", m.group[1]}); });
    text = ReplaceAll(text, Tondemo, [](const Match&) { return std::string("とんでもない"); });
    text = ReplaceAll(text, Mashou, [](const Match& m) { return Cat({m.group[1], "しょう"}); });
    text = ReplaceAll(text, UtoShite, [](const Match& m) { return Cat({"うとしてい", m.group[1]}); });
    text = ReplaceAll(text, Teku, [](const Match& m) { return Cat({m.group[1], "いく", m.group[2]}); });
    const bool copulaJa = CopulaJa(text);
    text = ReplaceAll(text, Janee, [&](const Match& m) { return Cat({m.group[1], copulaJa ? "だね" : "じゃない"}); });
    text = ReplaceAll(text, VolitionalKa, [](const Match& m) { return Cat({m.group[1], "うか"}); });
    text = ReplaceAll(text, NdeStart, [](const Match&) { return std::string("それで"); });
    text = ReplaceAll(text, Rya, [](const Match& m) { return std::string(m.group[1]); });
    text = ReplaceAll(text, DenakerebaStart, [](const Match&) { return std::string("そうで"); });
    text = ReplaceAll(text, SoCopulaStart, [](const Match&) { return std::string("そう"); });
    text = ReplaceAll(text, LoneSo, [](const Match&) { return std::string("そう"); });
    text = ReplaceAll(text, Shaanai, [](const Match&) { return std::string("しょうがない"); });
    text = ReplaceAll(text, Saate, [](const Match&) { return std::string("さて"); });
    text = ReplaceAll(text, Hidee, [](const Match&) { return std::string("ひどい"); });
    text = ReplaceAll(text, OiTsu, [](const Match&) { return std::string("おい"); });
    text = ReplaceAll(text, Literal<kSoyuKoto>, [](const Match&) { return std::string("そういうこと"); });
    text = ReplaceAll(text, NnKa, [](const Match& m) { return Cat({m.group[1], "のか"}); });
    text = ReplaceAll(text, Naani, [](const Match&) { return std::string("なに"); });
    text = ReplaceAll(text, KoKara, [](const Match& m) { return Cat({m.group[1], "こから"}); });
    text = ReplaceAll(text, AruN, [](const Match& m) { return ContractedRu("あ", m.group[2]); });
    text = ReplaceAll(text, Literal<kMokkai>, [](const Match&) { return std::string("もう一回"); });
    text = ReplaceAll(text, JanKa, [](const Match&) { return std::string("じゃん"); });
    text = ReplaceAll(text, TokuInText, [&](const Match& m) {
        const Match g = Rematch(m, Toku);
        const size_t formEnd = static_cast<size_t>(g.group[3].data() + g.group[3].size() - text.data());
        if (runs.holds != nullptr && runs.holds(runs.context, text, m.begin, formEnd)) return std::string(m.group[0]);
        const std::string_view stem = g.group[1];
        std::string_view te = "て";
        if (g.group[2] == "ど") {
            if (!stem.ends_with("ん")) return std::string(m.group[0]);
            te = "で";
        } else if (stem.ends_with("ん")) {
            return std::string(m.group[0]);
        } else {
            if (EndsWithStemKanji(stem) || ((stem.ends_with("任せ") || stem.ends_with("まかせ")) && g.group[3] == "け")) {
                return std::string(m.group[0]);  // 私とこ, 彼とき; 任せとけ is its own phrase
            }
        }
        return Cat({stem, te, TokuForm(g.group[3]), g.group[4]});
    });
    return ReplaceAll(text, Souyuu, [](const Match& m) { return Cat({m.group[1], "ういう"}); });
}

}  // namespace hdTrans::pipeline::internal
