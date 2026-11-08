#include <algorithm>
#include <span>
#include <utility>

#include "core/dict/classes.h"
#include "core/output/pieces.h"
#include "core/pipeline/normalize.h"
#include "core/pipeline/pattern.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/spoken_forms_gen.h"

namespace hdTrans::pipeline::internal {

size_t SentenceFinalParticle(std::string_view s, std::string_view particle, bool beforeComma) {
    size_t p = 0;
    if (!Lit(s, p, particle)) return 0;
    size_t q = p;
    if (q == s.size() || OneOf(s, q, U"。｡！!？?…‥」』）)　 ー～〜") || (beforeComma && OneOf(s, q, U"、，,"))) return p;
    return 0;
}

NegativeTail KansaiNegativeTail(std::string_view rest) {
    for (const std::string_view f : {"で", "がな"}) {
        if (const size_t n = SentenceFinalParticle(rest, f); n > 0) return {n, "ないよ"};
    }
    constexpr std::string_view kDe = "で", kDemo = "でも", kDeEe = "でええ";
    if (rest.starts_with(kDeEe)) return {kDeEe.size(), "なくていい"};
    if (rest.starts_with("でいい")) return {kDe.size(), "なくて"};
    if (rest.starts_with(kDemo)) {
        size_t r = kDemo.size();
        while (OneOf(rest, r, U"…‥")) {
        }
        return {kDemo.size(), rest.substr(r).starts_with("な") ? "ないでも" : "なくても"};
    }
    return {};
}

namespace {

// Group 1 a stem of kKataStems in their order, group 2 its ending. The adjective ending
// written in katakana (マズイ) only ends a katakana word of the stem and イ alone (not
// セコイア).
bool KataStemPattern(std::string_view s, size_t pos, Match& m) {
    for (const KataStem& k : kKataStems) {
        size_t p = pos;
        if (!Lit(s, p, k.kata)) continue;
        // Before an honorific the run is a name (エラさん 엘라 씨), not a stem and its さ.
        if (dict::StartsWithMember(HDTRANS_CLASS("honorific.name"), s.substr(p)) > 0) continue;
        for (std::string_view e : kKataEndings) {
            size_t q = p;
            if (!Lit(s, q, e)) continue;
            if (e == kKataAdjectiveEnding &&
                (IsKatakanaRune(LastRune(s.substr(0, pos)).cp) || (q < s.size() && IsKatakanaRune(At(s, q).cp)))) {
                continue;
            }
            m.group[1] = Span(s, pos, p);
            m.group[2] = Span(s, p, q);
            m.end = q;
            return true;
        }
    }
    return false;
}

// ([^\s「『（、。！？]じゃ|[良よ]く)ね[ぇえーっ]*([？?]|の|か|よ|だろ|って)
bool JaneQuestion(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "ね")) return false;
        return Repeat(s, p, InSet(U"ぇえーっ"), 0, kUnbounded, [&](size_t q) {
            size_t r = q;
            if (OneOf(s, r, U"？?")) {
                m.group[2] = Span(s, q, r);
                m.end = r;
                return true;
            }
            for (std::string_view w : kJaneQuestionAfter) {
                r = q;
                if (!Lit(s, r, w)) continue;
                m.group[2] = Span(s, q, r);
                m.end = r;
                return true;
            }
            return false;
        });
    };
    size_t p = pos;
    if (NoneOf(s, p, U"\t\n\f\r 「『（、。！？") && Lit(s, p, "じゃ") && rest(p)) return true;
    p = pos;
    return OneOf(s, p, U"良よ") && Lit(s, p, "く") && rest(p);
}

// Whether the casual ねえ, ねぇ or ねー at p of s goes on as the negative ない: before ん, の, よ, や, し, ぞ, ぜ, さ, か, な, っ,
// だろ, じゃ, けど, から, です or もん (意味もねえんだよ, 亭主もねえのか, 近道なんてねーからな); its length then, else 0.
size_t NegativeNeeAt(std::string_view s, size_t p) {
    size_t q = p;
    if (!Lit(s, q, "ね")) return 0;
    const size_t run = q;
    while (q < s.size() && In(At(s, q).cp, U"ぇえー")) q += At(s, q).size;
    if (q == run) return 0;
    const std::string_view rest = s.substr(q);
    if (!rest.empty() && In(At(rest, 0).cp, U"んのよやしぞぜさかなっ")) return q - p;
    for (const std::string_view w : {"だろ", "じゃ", "けど", "から", "です", "もん"}) {
        if (rest.starts_with(w)) return q - p;
    }
    return 0;
}

// The casual negative after も, なんか or なんて, the words of there being none (spec LEX-5071): ねえ, ねぇ or ねー before a word
// of NegativeNeeAt (へったくれもねえな, 行くとこなんかねえじゃねえか, 正解なんてねぇのさ), and after a noun and も also ねえ or ねぇ
// before 。！」』 or the end (祝言もねえ。, どうしようもねぇ。). Group 1 is the word before ね. Not the particle ね after かも
// (かもねえ), しかも, a でも that opens a clause, nor the でも of a て form (読んでもねえ); at a sentence end not after the
// concessive ても (言ってもねえ。) nor with ねー, and not before a comma or an ellipsis, where it is the particle of a pause
// (私もねー、, 事件もねぇ……).
bool NoneNee(std::string_view s, size_t pos, Match& m) {
    for (const std::string_view w : {"なんか", "なんて", "も"}) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        const std::string_view head = s.substr(0, pos);
        const char32_t b = pos > 0 ? LastRune(s.substr(0, pos)).cp : 0;
        if (w == "も" && (head.ends_with("か") || pos == 0 || (!IsHiraganaClass(b) && !IsKanjiClass(b) && !IsKatakanaRune(b)))) return false;
        m.group[1] = Span(s, pos, p);
        // The particle ね after しかも, a clause-opening でも (でもねえ、), or the で of a て form with も (読んでもねえ).
        if (w == "も") {
            if (head.ends_with("しか")) return false;
            if (head.ends_with("で")) {
                const std::string_view lead = head.substr(0, head.size() - std::string_view("で").size());
                const char32_t c = lead.empty() ? 0 : LastRune(lead).cp;
                if (c == 0 || In(c, U"んい「『、。！？!? 　")) return false;
            }
        }
        if (const size_t n = NegativeNeeAt(s, p); n > 0) {
            m.group[2] = Span(s, p + n, p + n);
            m.end = p + n;
            return true;
        }
        // At a sentence end only after a noun and も, not after the て of a concession (言ってもねえ。).
        if (w != "も" || head.ends_with("て")) return false;
        size_t q = p;
        if (!Lit(s, q, "ね") || !OneOf(s, q, U"ぇえ")) return false;
        while (q < s.size() && In(At(s, q).cp, U"ぇえ")) q += At(s, q).size;
        if (q < s.size() && !In(At(s, q).cp, U"。！!」』")) return false;
        m.group[2] = Span(s, q, q);
        m.end = q;
        return true;
    }
    return false;
}

// The wish たい with は said たか before the casual ねえ (言われたかねえや 듣고 싶지 않아, 行きたかねえよ) or ない (言いたかないけど):
// たくはない. With ねえ only before a word of NegativeNeeAt other than か, な or の, where たかねえ is no question in かね after a past
// (行ったかねえ。, できたかねえ).
bool TakaNee(std::string_view s, size_t pos, Match& m) {
    const char32_t b = pos > 0 ? LastRune(s.substr(0, pos)).cp : 0;
    // After っ or ん the た is a past (あったかないか).
    if (pos == 0 || !(IsHiraganaClass(b) || IsKanjiClass(b)) || In(b, U"っん")) return false;
    size_t p = pos;
    if (!Lit(s, p, "たか")) return false;
    size_t q = p;
    if (Lit(s, q, "ない")) {
        m.end = q;
        return true;
    }
    const size_t n = NegativeNeeAt(s, p);
    if (n == 0) return false;
    if (In(At(s, p + n).cp, U"かなの")) return false;
    m.end = p + n;
    return true;
}

// ([はも])ね[ぇえー]+(だろ|でしょ|よ|って|し)
bool ParticleNee(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"はも")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ね")) return false;
    return Repeat(s, p, InSet(U"ぇえー"), 1, kUnbounded, [&](size_t q) {
        for (std::string_view w : kParticleNeeAfter) {
            size_t r = q;
            if (!Lit(s, r, w)) continue;
            m.group[2] = Span(s, q, r);
            m.end = r;
            return true;
        }
        return false;
    });
}

// A rune of first, then lit, then one of the words: groups 1 and 2.
bool CharLitWord(std::string_view s, size_t pos, Match& m, std::u32string_view first, std::string_view lit,
                 std::span<const std::string_view> words) {
    size_t p = pos;
    if (!OneOf(s, p, first)) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, lit)) return false;
    for (std::string_view w : words) {
        size_t q = p;
        if (!Lit(s, q, w)) continue;
        m.group[2] = Span(s, p, q);
        m.end = q;
        return true;
    }
    return false;
}

// ([てで])ん(じゃ|だろ|です|でしょ); the で of a て form follows ん or い (読んでんです), so a で after anything else is
// another word (おでんです), and the って after a dictionary form in る, う or つ or after だ quotes (あるってんですか, だってんだろ).
bool ContractedTeru(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (s.substr(pos).starts_with("で") && (pos == 0 || !In(LastRune(head).cp, U"んい"))) return false;
    if (head.ends_with("っ")) {
        const std::string_view lead = head.substr(0, head.size() - std::string_view("っ").size());
        // A godan verb in る, う or つ drops that kana before the っ of its て form, so one before って quotes; the つ of the
        // っつって of って言って stays (違うっつってんだろ).
        if (!lead.empty() && In(LastRune(lead).cp, U"うつるだ") && !lead.ends_with("っつ")) return false;
    }
    if (!CharLitWord(s, pos, m, U"てで", "ん", dict::ClassMembers(HDTRANS_CLASS("follower.contracted-teru")))) return false;
    // でしょうが of insistence reads better from the てん the end rows know (言ってんでしょうが).
    return !(m.group[2] == "でしょ" && s.substr(m.end).starts_with("うが"));
}

// (だ)?って(ん)(だろ|でしょ|です|だ): the quoting って and the ん of んだ, っていうんだ said short (土瓶ってんですか,
// 何ってんだ, どうしたってんだ, 魔女だってんだろ), as group 1 the word after ん and as group 2 a copula だ before って, after
// a word. Whether a word before って quotes or is a verb whose て form the っ makes is left to the replacement, which has
// the dictionary (GodanTeStem).
bool QuoteTten(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (pos == 0) return false;
    m.group[2] = {};
    // The だ of a past form after ん or い (死んだってんだ, 泳いだってんだ) is the verb's own and stays.
    if (Lit(s, p, "だ")) {
        if (In(LastRune(s.substr(0, pos)).cp, U"んい")) return false;
        m.group[2] = Span(s, pos, p);
    }
    if (!Lit(s, p, "ってん")) return false;
    for (std::string_view w : {"だろ", "でしょ", "です", "だ"}) {
        size_t q = p;
        if (!Lit(s, q, w)) continue;
        // The question だい of a man (どこにあるってんだい) has no row of its own to go to.
        if (w == "だ" && s.substr(q).starts_with("い")) return false;
        m.group[1] = Span(s, p, q);
        m.end = q;
        return true;
    }
    return false;
}

// Whether head, before the っ of って, ends with the stem of a godan verb of the dictionary whose て form that っ makes: up to
// three kanji or kana before it and う, つ or る spell the verb (言って 言う, 待って 待つ, がんばって がんばる, ビビって ビビる).
// The た of a past form after し, っ, い or a kana of the e row and the だ after ん or い quote (どうしたってんだ,
// 言ったってんです, 食べたってんだ, 死んだってんだ).
bool GodanTeStem(std::string_view head, const RunLookup& runs) {
    if (head.ends_with("た")) {
        const std::string_view past = head.substr(0, head.size() - std::string_view("た").size());
        if (!past.empty() && In(LastRune(past).cp, U"しっいえけげせてねべめれ")) return false;
    }
    if (head.ends_with("んだ") || head.ends_with("いだ")) return false;
    std::string stem;
    for (int k = 0; k < 3 && !head.empty(); k++) {
        const text::Rune r = LastRune(head);
        if (!IsKanjiClass(r.cp) && !IsHiraganaClass(r.cp) && !IsKatakanaRune(r.cp)) break;
        stem.insert(0, head.substr(head.size() - r.size));
        head.remove_suffix(r.size);
        for (std::string_view u : {"う", "つ", "る"}) {
            const std::string verb = stem + std::string(u);
            if (runs.verbForm(runs.context, verb) || runs.verbForm(runs.context, KatakanaToHiragana(verb))) return true;
        }
    }
    return false;
}

// ([にでもがは])いん(の|だ|じゃ)
bool ContractedIru(std::string_view s, size_t pos, Match& m) {
    return CharLitWord(s, pos, m, U"にでもがは", "いん", dict::ClassMembers(HDTRANS_CLASS("follower.contracted-iru")));
}

// One rune of set or the end of s at p as group 2, ending the match.
bool EndGroup(std::string_view s, size_t p, std::u32string_view set, Match& m) {
    size_t q = p;
    if (!OneOf(s, q, set) && p != s.size()) return false;
    m.group[2] = Span(s, p, q);
    m.end = q;
    return true;
}

// ([うくぐすつぬぶむる])んじゃ(ない|ね[えぇー]+)(よ|ぞ|わよ)?[ーっッぇ]*([！!」』。]|$): the prohibition said as
// a rebuke (泣くんじゃない！, 来るんじゃねぇ！, 泣くんじゃないよ, 気を抜くんじゃないぞ！, 口出しするんじゃねえッ！; spec
// MNG-6), the particle one of follower.prohibition-janai. After the てる or ている of a て form, ない tells what is not so
// (一般論を言ってるんじゃない, 言ってるんじゃないよ) and stays unless ！ or a particle of follower.prohibition-janai-teru
// follows. Not after a verb no one does on purpose (verb.observed-state, verb.janai-state: 分かるんじゃない,
// 興味あるんじゃない), a quoting verb (verb.janai-quote: って言うんじゃない) or an adverb of adverb.realization in the
// sentence (やっぱり気になるんじゃない), nor where a verb of saying corrects what was said (そういうこと言ってるんじゃない！),
// nor the honorific くん after katakana, two kanji, う, ん, ー or a small kana (田中くんじゃない！), nor a てく or でく.
bool ProhibitionJanai(std::string_view s, size_t pos, Match& m) {
    m.group[4] = {};
    m.group[5] = {};
    size_t p = pos;
    if (!OneOf(s, p, U"うくぐすつぬぶむる")) return false;
    m.group[1] = Span(s, pos, p);
    if (m.group[1] == "く") {
        const text::Rune b = LastRune(s.substr(0, pos));
        // てく, でく are ていく or てくる said short (出てくんじゃねえ), no dictionary form of their own.
        if (pos == 0 || IsKatakanaRune(b.cp) || In(b.cp, U"うんーぁぃぅぇぉゃゅょゎてで") ||
            (IsKanjiClass(b.cp) && pos > b.size && IsKanjiClass(LastRune(s.substr(0, pos - b.size)).cp))) {
            return false;
        }
    }
    const std::string_view before = s.substr(0, pos);
    const text::Rune last = pos > 0 ? LastRune(before) : text::Rune{};
    const char32_t b = last.cp;
    // うん is the answer (うんうんじゃねえよ), not a verb in う.
    if (m.group[1] == "う" && !IsKanjiClass(b) && (!IsHiraganaRune(b) || In(b, U"うん"))) return false;
    // す after two kanji, katakana or a particle is する said す (信用すんじゃねえ, 気にすんじゃねえ); after
    // one kanji it ends a verb (出すんじゃない); after other kana it may be either (落とす, 扱いする).
    const bool suru = m.group[1] == "す" && pos > 0 &&
                      (IsKatakanaRune(b) || In(b, U"にをとも") ||
                       (IsKanjiClass(b) && pos > last.size && IsKanjiClass(LastRune(s.substr(0, pos - last.size)).cp)));
    if (m.group[1] == "す" && !suru && !IsKanjiClass(b)) return false;
    if (!Lit(s, p, "んじゃ")) return false;
    if (suru) m.group[1] = "する";
    bool nee = false;
    if (!Lit(s, p, "ない")) {
        if (!Lit(s, p, "ね") || !OneOf(s, p, U"えぇー")) return false;
        while (OneOf(s, p, U"えぇー")) {
        }
        nee = true;
    }
    const bool teru = m.group[1] == "る" && pos >= 3 &&
                      (In(b, U"てで") || (b == U'い' && In(LastRune(before.substr(0, pos - last.size)).cp, U"てで")));
    // ある and いる (verb.janai-state) as words of their own: after a particle other than は (はいる is 入る),
    // a kanji, katakana or the start, not inside a kana word
    const std::size_t state = dict::EndsWithMember(HDTRANS_CLASS("verb.janai-state"), before);
    const bool stateWord = state > 0 && [&] {
        if (state == before.size()) return true;
        const char32_t c = LastRune(before.substr(0, before.size() - state)).cp;
        return !IsHiraganaRune(c) || In(c, U"がもにでとをへ");
    }();
    if (!teru && (dict::EndsWithMember(HDTRANS_CLASS("verb.observed-state"), before) > 0 || stateWord ||
                  dict::EndsWithMember(HDTRANS_CLASS("verb.janai-quote"), before) > 0)) {
        return false;
    }
    // The sentence before the verb: after an adverb of adverb.realization the んじゃない affirms what was found
    // (やっぱり気になるんじゃない 역시 신경 쓰이는 거잖아), and the てる of a verb of verb.janai-saying after a word of
    // word.janai-correction corrects what was said (そういうこと言ってるんじゃないわよっ！ 그런 거 말하는 게 아니야).
    const size_t lineStart = before.find_last_of('\n');
    std::string_view sentence = lineStart == std::string_view::npos ? before : before.substr(lineStart + 1);
    for (std::string_view mark : {"。", "！", "？", "!", "?", "「", "『"}) {
        const size_t at = sentence.rfind(mark);
        if (at != std::string_view::npos) sentence = sentence.substr(at + mark.size());
    }
    const auto inSentence = [&](std::string_view w) { return sentence.find(w) != std::string_view::npos; };
    // the speaker as topic or subject tells what the speaker does not do (俺は逃げるんじゃない。 나는 도망치는 게 아니다)
    for (const std::string_view w : dict::ClassMembers(HDTRANS_CLASS("pronoun.first-person"))) {
        for (const std::string_view mark : {"は", "が"}) {
            const std::size_t at = sentence.find(w);
            if (at != std::string_view::npos && sentence.substr(at + w.size()).starts_with(mark)) return false;
        }
    }
    if (teru && !nee) {
        std::string_view stem = before.substr(0, pos - last.size);
        if (b == U'い') stem = stem.substr(0, stem.size() - LastRune(stem).size);
        if (dict::EndsWithMember(HDTRANS_CLASS("verb.janai-saying"), stem) > 0 &&
            std::ranges::any_of(dict::ClassMembers(HDTRANS_CLASS("word.janai-correction")), inSentence)) {
            return false;
        }
    }
    // The rebuke quoted with と or って, at the end or before a verb of saying (泣くんじゃないと 울지
    // 말라고, 泣くんじゃないと言われた 울지 말라고 들었다): the quoted prohibition. Not after a てる (言ってるんじゃないと),
    // nor before 思う, where んじゃない is a guess (行くんじゃないと思う).
    if (!teru && !nee) {
        size_t r = p;
        std::string_view quote;
        if (Lit(s, r, "って")) quote = "って";
        else if (Lit(s, r, "と")) quote = "と";
        if (!quote.empty()) {
            const std::string_view rest = s.substr(r);
            bool verb = false;
            for (const std::string_view v : {"言", "いう", "いっ", "いわ", "いえ", "教え", "叱", "怒"}) verb = verb || rest.starts_with(v);
            // と closes the quote at the end or before a mark; って, which also explains what is not so (そんな
            // つもりで来るんじゃないって 오는 게 아니라고), only before ！ or a verb of saying.
            Match e;
            const bool adnominal = m.group[1] == "う" && (before.ends_with("そうい") || before.ends_with("こうい") ||
                                                          before.ends_with("ああい") || before.ends_with("どうい"));
            const bool closes = quote == "と" ? EndGroup(s, r, U"！!」』", e) : EndGroup(s, r, U"！!", e) && r < s.size();
            // ということ, というの name a fact, not a saying (書くんじゃないということだ 쓰는 게 아니라는 것이다).
            const bool fact = quote == "と" && (rest.starts_with("いうこと") || rest.starts_with("いう事") || rest.starts_with("いうの") ||
                                                rest.starts_with("いうわけ"));
            if (!adnominal && !fact && (verb || closes)) {
                m.group[3] = quote;
                m.group[2] = verb ? std::string_view() : e.group[2];
                m.end = verb ? r : e.end;
                return true;
            }
        }
    }
    size_t q = p;
    std::string_view particle;
    for (std::string_view w : dict::ClassMembers(HDTRANS_CLASS("follower.prohibition-janai"))) {
        size_t r = p;
        if (w.size() > particle.size() && Lit(s, r, w)) {
            particle = w;
            q = r;
        }
    }
    while (OneOf(s, q, U"ーっッぇ")) {
    }
    size_t r = q;
    const bool exclaimed = OneOf(s, r, U"！!");
    if (teru && !nee && !exclaimed && !dict::InClass(HDTRANS_CLASS("follower.prohibition-janai-teru"), particle)) {
        return false;
    }
    // an adverb of adverb.realization makes a plain んじゃない affirm what was found; with ！, ぞ or わよ it stays
    // a rebuke (やっぱり、無理するんじゃない！ 역시, 무리하지 마!)
    if (!exclaimed && !dict::InClass(HDTRANS_CLASS("follower.prohibition-janai-teru"), particle) &&
        std::ranges::any_of(dict::ClassMembers(HDTRANS_CLASS("adverb.realization")), inSentence)) {
        return false;
    }
    // a て form after a noun with で tells how or why one is there, not a rebuke (遊びで来てるんじゃないぞ！ 놀이로 온
    // 게 아니야): the stem of the て form, one kanji or up to two kana, right after で
    if (teru && !nee) {
        std::string_view v = before;
        if (v.ends_with("い")) v.remove_suffix(std::string_view("い").size());
        if (v.ends_with("て") || v.ends_with("で")) v.remove_suffix(std::string_view("て").size());
        for (const std::string_view e : {"っ", "ん", "い"})
            if (v.ends_with(e)) {
                v.remove_suffix(e.size());
                break;
            }
        const text::Rune stemEnd = LastRune(v);
        if (stemEnd.size > 0 && IsKanjiClass(stemEnd.cp)) {
            v.remove_suffix(stemEnd.size);
        } else {
            for (int n = 0; n < 2 && !v.empty() && IsHiraganaRune(LastRune(v).cp) && !v.ends_with("で"); n++)
                v.remove_suffix(LastRune(v).size);
        }
        // the で of a noun, not that of a て form (急いで, 読んで)
        if (v.ends_with("で") && v.size() > std::string_view("で").size() &&
            !In(LastRune(v.substr(0, v.size() - std::string_view("で").size())).cp, U"んい"))
            return false;
    }
    // The rough rebuke before a word of follower.prohibition-pause (話しかけんじゃねえこのクズども, ふざけんじゃねえぞこの野郎)
    // ends there, with a pause that is not written: group 4 set, and group 5 the particle.
    if (nee && dict::StartsWithMember(HDTRANS_CLASS("follower.prohibition-pause"), s.substr(q)) > 0) {
        m.group[2] = Span(s, q, q);
        m.group[4] = m.group[2];
        m.group[5] = Span(s, p, q);
        m.end = q;
        return true;
    }
    // After a particle the rough rebuke may also end in a question mark, or but after a てる in a comma (忘れんじゃねーぞ？,
    // なめんじゃねーぞ、公方).
    if (nee && !particle.empty()) return EndGroup(s, q, teru ? U"！!」』。？?" : U"！!」』。？?、", m);
    return EndGroup(s, q, U"！!」』。", m);
}

// (..[いたる])じゃない([！!。、…‥」』]|$)
bool AppealJanai(std::string_view s, size_t pos, Match& m) {
    auto notNewline = [](char32_t c) { return c != U'\n'; };
    size_t p = pos;
    if (!Take(s, p, notNewline) || !Take(s, p, notNewline) || !OneOf(s, p, U"いたる")) return false;
    m.group[1] = Span(s, pos, p);
    return Lit(s, p, "じゃない") && EndGroup(s, p, U"！!。、…‥」』", m);
}

// The words ending in い after which じゃない is the negative (嫌いじゃない), and てる.
bool NotAppeal(std::string_view stem) {
    for (std::string_view w : kNotAppealStems) {
        if (stem.ends_with(w)) return true;
    }
    return false;
}

// (怖|寒|…|こわ)っ+([！!？?。、…‥」』　]|$); a stem of kClippedCries only after a particle
// (足が痛っ), the cry of the dictionary elsewhere (痛っ！ 아얏).
bool ClippedAdjective(std::string_view s, size_t pos, Match& m) {
    const bool afterParticle = pos > 0 && In(LastRune(s.substr(0, pos)).cp, U"がはもにをでとへの");
    for (std::string_view w : kClippedAdjectives) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        if (!afterParticle && std::ranges::find(kClippedCries, w) != std::end(kClippedCries)) continue;
        m.group[1] = Span(s, pos, p);
        const bool ok = Repeat(s, p, InSet(U"っ"), 1, kUnbounded,
                               [&](size_t q) { return EndGroup(s, q, U"！!？?。、…‥」』　", m); });
        if (ok) return true;
    }
    return false;
}

constexpr std::u32string_view kNegativeEnd = U"！!？?。、…‥」』ぞよわがしけっ";

// Whether before ends with a member of casual.ikanai-after.
bool AfterIkanaiWord(std::string_view before) {
    return dict::EndsWithMember(HDTRANS_CLASS("casual.ikanai-after"), before) > 0;
}

// The ん of a negative before a tail of KansaiNegativeTail (group 2 the tail, group 3 the negative it reads; but
// for がな only where de), or else before a rune of kNegativeEnd or the end.
bool NegativeNEnd(std::string_view s, size_t p, bool de, Match& m) {
    if (!Lit(s, p, "ん")) return false;
    if (const NegativeTail t = KansaiNegativeTail(s.substr(p)); t.length > 0 && (de || s.substr(p).starts_with("がな"))) {
        m.group[2] = s.substr(p, t.length);
        m.group[3] = t.negative;
        m.end = p + t.length;
        return true;
    }
    return EndGroup(s, p, kNegativeEnd, m);
}

// ([一-龥々][まわかがばさた]?[らわかがまば]|許さ|わから|しら|かまわ|つまら|くだら|いら|たまら|いか)ん([！!？?。、…‥」』ぞよわがしけっ]|$), or before a final が
// な, or after a stem but the first before a tail in で: a godan verb in kanji may end its て form in that んで
// (浮かんで), which the contraction stage tells apart.
bool NegativeN(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p, bool de) {
        m.group[1] = Span(s, pos, p);
        return NegativeNEnd(s, p, de, m);
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"らわかがまば") && rest(p, false)) return true;
    // A godan stem with two kana after the kanji (止まらん, 終わらん, 助からん).
    p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"まわかがばさた") && OneOf(s, p, U"らわかがまば") &&
        !Span(s, pos, p).ends_with("分から") && rest(p, false)) {
        return true;
    }
    for (std::string_view w : kNegativeNStems) {
        p = pos;
        if (Lit(s, p, w) && rest(p, true)) return true;
        // The な after the kana stem's ん that ends the clause is the final particle (つまらんな つまらないな),
        // and a negative said twice reads as one (いらんいらん いらないいらない).
        p = pos;
        if (Lit(s, p, w)) {
            size_t q = p;
            if (Lit(s, q, "ん") && s.substr(q).starts_with(Cat({w, "ん"}))) {
                m.group[1] = Span(s, pos, p);
                m.group[2] = {};
                m.group[3] = {};
                m.end = q;
                return true;
            }
            q = p;
            if (Lit(s, q, "んな")) {
                size_t r = q;
                if (r == s.size() || OneOf(s, r, U"！!？?。、…‥」』ぁあー〜～")) {
                    m.group[1] = Span(s, pos, p);
                    m.group[2] = "な";
                    m.group[3] = {};
                    m.end = q;
                    return true;
                }
            }
        }
    }
    // A kana いか after casual.ikanai-after is いく (わけにはいかん), not the いかん of それはいかん.
    p = pos;
    if (Lit(s, p, "いか") && AfterIkanaiWord(s.substr(0, pos)) && rest(p, true)) return true;
    return false;
}

// (でき|得|足り|られ|させ|言え|見え|聞こえ)ん([！!？?。、…‥」』ぞよわがしけっ]|$), or before a tail of
// KansaiNegativeTail.
// The ん of a negative before the conjecture of the copula, だろ, じゃろ or でしょ, after a kana of the あ row
// (分からんだろう, 終わらんじゃろう, 要らんでしょ, 許さんだろう) is ない (모를 것이다, 끝나지 않겠지), before かった
// the な of なかった. Not a potential in え, which says the る of るんだ short (使えんだろ 쓸 수 있는 거지), nor
// a word whose negative the dictionary reads (いかん 안 돼, 済まん, かなわん), ている said とる (しとらん), やがる, the
// kana わかん of 分かるんだ, nor the kana of a noun or a past (いちばんだろう, 茶わんだろう, えらんだろう, 読んだろう,
// 何なんだろう, したんでしょう, 強盗さんでしょうか).
bool NegativeConjecture(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "ん")) return false;
    const std::string_view head = s.substr(0, pos);
    if (head.empty()) return false;
    if (!In(LastRune(head).cp, U"かまやらわがざばぱ") && !head.ends_with("許さ") && !head.ends_with("せさ")) return false;
    for (const std::string_view w : {"わか", "いか", "とら", "どら", "済ま", "すま", "かなわ", "叶わ", "ちば", "茶わ", "えら", "やが", "しきら"}) {
        if (head.ends_with(w) && !(w == "わか" && head.ends_with("分か"))) return false;
    }
    std::string_view rest = s.substr(p);
    // A past in かった goes with it (知らんかっただろ 몰랐을 것이다).
    m.group[1] = rest.starts_with("かった") ? "な" : "ない";
    if (rest.starts_with("かった")) rest.remove_prefix(std::string_view("かった").size());
    if (!rest.starts_with("だろ") && !rest.starts_with("じゃろ") && !rest.starts_with("でしょ")) return false;
    m.end = p;
    // A じゃろ before a comma, which no end row reads, is だろ (わからんじゃろ、 모르겠지,).
    if (rest.starts_with("じゃろ、") && m.group[1] == "ない") {
        m.group[1] = "ないだろ";
        m.end = p + std::string_view("じゃろ").size();
    }
    return true;
}

bool NegativeE(std::string_view s, size_t pos, Match& m) {
    for (std::string_view w : kNegativeEStems) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        m.group[1] = Span(s, pos, p);
        if (NegativeNEnd(s, p, true, m)) return true;
    }
    return false;
}

// Whether the sentence ends at p, also before a sentence-final particle or an ending of old speech.
bool OldNegativeEnds(std::string_view s, size_t p) {
    if (p == s.size()) return true;
    size_t q = p;
    if (OneOf(s, q, U"。！？!?」』ぞか")) return true;
    // わ only as the sentence particle, not the start of a word (知らぬわらわ, 知らぬわけ).
    if (q = p; Lit(s, q, "わ") && (q == s.size() || OneOf(s, q, U"。！？!?」』…‥ 　"))) return true;
    for (const std::string_view w : {"のじゃ", "のだ", "でござる", "でおじゃる"}) {
        if (q = p; Lit(s, q, w)) return true;
    }
    return false;
}

// The classical negative ぬ of the verbs whose negative the word stage reads as a word of its own
// (できない 할 수 없다, 知らない 모르다, 分からない 모르다), read as ない: できぬ anywhere, the others where
// the sentence ends (知らぬ。, not the idioms 知らぬが仏, 知らぬ間に or 見知らぬ), ならぬ there after なくては,
// まかり or nothing (ならぬ！, 戦わなくてはならぬ), not 人ならぬ (인간이 아닌), 相手にならぬ, そうはならぬ or the
// prohibition てはならぬ, which the word stage reads (해서는 안 된다).
constexpr std::string_view kOldNegatives[] = {"できぬ", "知らぬ", "分からぬ", "わからぬ", "判らぬ"};

bool OldNegative(std::string_view s, size_t pos, Match& m) {
    for (const std::string_view w : kOldNegatives) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        if (w == "知らぬ" && s.substr(0, pos).ends_with("見")) return false;
        if (w != "できぬ" && !OldNegativeEnds(s, p)) return false;
        // After が the word stage reads 分からぬ as what cannot be told (行方が分からぬ 행방을 알 수 없다).
        if (w != "できぬ" && w != "知らぬ" && s.substr(0, pos).ends_with("が")) return false;
        m.group[1] = w.substr(0, w.size() - std::string_view("ぬ").size());
        // A question in か asks whether one does not (分からぬか 모르는 건가), not a wondering かな.
        if (size_t q = p; w != "できぬ" && Lit(s, q, "か")) m.group[2] = "の";
        m.end = p;
        return true;
    }
    size_t p = pos;
    if (!Lit(s, p, "ならぬ")) return false;
    const std::string_view head = s.substr(0, pos);
    if (!head.empty() && !head.ends_with("なくては") && !head.ends_with("まかり")) {
        const char32_t before = LastRune(head).cp;
        if (!In(before, U"「『（(、。！？!?…‥ 　")) return false;
    }
    if (!OldNegativeEnds(s, p)) return false;
    m.group[1] = "なら";
    m.end = p;
    return true;
}

bool NegativeNee(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "ね")) return false;
        return Repeat(s, p, InSet(U"ぇえー"), 1, kUnbounded, [&](size_t q) {
            m.end = q;
            return true;
        });
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"らわさまばが") && rest(p)) return true;
    for (std::string_view stem : kNegativeNeeKa) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "か") && rest(p)) return true;
    }
    for (std::string_view stem : kNegativeNeeTa) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "た") && rest(p)) return true;
    }
    // A kana いか at the start, after an opening quote or after casual.ikanai-after is いく (そうはいかねえ,
    // 「いかねえよ), not いいかねえ.
    p = pos;
    if (Lit(s, p, "いか")) {
        const std::string_view before = s.substr(0, pos);
        if ((before.empty() || before.ends_with("「") || before.ends_with("『") || AfterIkanaiWord(before)) && rest(p))
            return true;
    }
    for (std::string_view stem : kNegativeNeeStems) {
        p = pos;
        if (Lit(s, p, stem) && rest(p)) return true;
    }
    for (std::string_view stem : kNegativeNeeKu) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "く") && rest(p)) return true;
    }
    // The たい of a wish after a verb's continuative, a kanji or a kana of the i or e row
    // (したくねぇ, 行きたくねえ, 食べたくねー; not まったくねぇ). The start of s is the match read again
    // (ReplaceNegativeNee).
    p = pos;
    if (const char32_t b = pos > 0 ? LastRune(s.substr(0, pos)).cp : 0;
        (b == 0 || IsKanjiClass(b) || In(b, U"いきぎしじちにひびみりえけげせぜてでねへべめれ")) && Lit(s, p, "たく") && rest(p)) {
        return true;
    }
    return false;
}

std::string ReplaceNegativeNee(const Match& m) {
    const Match g = Rematch(m, NegativeNee);
    std::string_view stem = g.group[1];
    if (stem == "わかん") stem = "わから";
    else if (stem == "分かん") stem = "分から";
    else if (stem == "たまん") stem = "たまら";
    else if (stem == "つまん") stem = "つまら";
    else if (stem == "くだん") stem = "くだら";
    return Cat({stem, "ない"});
}

bool KansaiNegative(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "へん")) return false;
        const size_t ending = p;
        m.group[5] = {};
        if (Lit(s, p, "かった")) {
            m.group[2] = Span(s, ending, p);
        } else {
            m.group[2] = {};
            // A tail of KansaiNegativeTail after it reads with the negative (負けへんで！ 負けないよ！).
            if (const NegativeTail t = KansaiNegativeTail(s.substr(p)); t.length > 0) {
                m.group[5] = t.negative;
                m.group[3] = s.substr(p, t.length);
                p += t.length;
            }
        }
        m.end = p;
        return true;
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"わかがさたなまば") && rest(p)) return true;
    for (const auto& drawn : kKansaiDrawnStems) {
        p = pos;
        if (Lit(s, p, drawn[0]) && rest(p)) return true;
    }
    // The stem of an ichidan verb, a potential or a passive: kanji and at most three kana, the last an e- or
    // i-row kana (負けへん, 離れへん, 逃れられへん, 落ちへん); not the drawn-out せえ, けえ or 来え of する or くる
    // (手加減せえへん, 来えへん). せ alone after two kanji or more is written with the ん of the old negative
    // (勉強せへん 勉強せん, 見逃せへん 見逃せん), which the contraction stage reads as する or a potential; group 4
    // marks it.
    p = pos;
    m.group[4] = {};
    int kanjiCount = 0;
    while (Take(s, p, IsKanjiClass)) kanjiCount++;
    if (kanjiCount > 0) {
        const size_t kanji = p;
        for (int n = 0; n < 3 && p < s.size() && IsHiraganaRune(At(s, p).cp) && !s.substr(p).starts_with("へん"); n++)
            p += At(s, p).size;
        const std::string_view kana = s.substr(kanji, p - kanji);
        const char32_t last = kana.empty() ? 0 : LastRune(kana).cp;
        const bool drawn = kana.ends_with("せえ") || kana.ends_with("けえ") ||
                           (kana == "え" && LastRune(s.substr(0, kanji)).cp == U'来');
        if (last != 0 && In(last, U"えけげせぜねべめれきぎじちびみ") && !drawn && rest(p)) {
            if (kana == "せ" && kanjiCount >= 2) m.group[4] = "ん";
            return true;
        }
    }
    for (std::string_view stem : kKansaiNegativeRa) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "ら") && rest(p)) return true;
    }
    p = pos;
    if (OneOf(s, p, U"てで") && rest(p)) return true;
    for (std::string_view stem : kKansaiNegativeStems) {
        p = pos;
        if (Lit(s, p, stem) && rest(p)) return true;
    }
    return false;
}

// Whether the やろ or じゃろ at pos can be the conjecture of the copula: not the volitional of やる
// (やろう, Kansai やろ), which follows a て or で form (貸してやろか, 読んでやろ) or a particle of an
// object, a doer or a partner (これをやろう, 一緒にやろな, 俺がやろか, ちゃんとやろな, 今度はやろ,
// これもやろ) or starts a sentence or a clause (よし、やろか), nor the やろ of 野郎 after a
// demonstrative or ばか (このやろが, バカやろって), nor after the Kansai adverbial う of an
// adjective of one kanji (景気良うやろか, 早うやろ), which describes a verb. Nor after a polite
// ます or ません, which takes no copula (なりますやろか), a Kansai negative へん the spoken forms
// could not read (気ぃつかへん) or a noun quoted by いう without と (平和いうんじゃろうか), which
// leave the predicate unread.
bool BeforeCopula(std::string_view s, size_t pos) {
    const std::string_view head = s.substr(0, pos);
    if (head.empty() || head.ends_with("へん") || head.ends_with("ます") || head.ends_with("ません")) return false;
    if (head.ends_with("いうん")) {
        const std::string_view quoted = head.substr(0, head.size() - std::string_view("いうん").size());
        if (!quoted.ends_with("と") && !quoted.ends_with("って")) return false;
    }
    for (const std::string_view w : {"この", "その", "あの", "どの", "ばか", "バカ", "馬鹿"}) {
        if (head.ends_with(w)) return false;
    }
    if (head.ends_with("う")) {
        const char32_t stem = LastRune(head.substr(0, head.size() - std::string_view("う").size())).cp;
        if (std::u32string_view(U"良早遅強高安長偉寒暑痛若古悪近遠深浅広狭重軽太細").find(stem) != std::u32string_view::npos) {
            return false;
        }
    }
    // The question word なんで (何で) and the noun こと end in the kana of a particle.
    if (head.ends_with("なんで") || head.ends_with("何で") || head.ends_with("こと")) return true;
    const char32_t before = LastRune(head).cp;
    if (In(before, U"てでをにへがとはもっゃゅょぁぃぅぇぉ")) return false;
    return IsKanjiClass(before) || IsKatakanaRune(before) || IsHiraganaClass(before) || before == U'ー';
}

// Whether a particle of kKansaiConjectureBefore that leaves the sentence open starts s at p: a
// sentence-final が is the rebuke that the end rows read (お前やろが！ 너잖아).
bool OpenParticleAt(std::string_view s, size_t p) {
    for (std::string_view w : kKansaiConjectureBefore) {
        size_t q = p;
        if (!Lit(s, q, w)) continue;
        size_t r = q;
        return w != "が" || (q != s.size() && !OneOf(s, r, U"」』）！？!?。．…‥～ーぁぃぅぇぉっ"));
    }
    return false;
}

// The conjecture of the copula (だろう) in Kansai やろ and in the じゃろ(う) of western and old
// speech, before a particle that leaves the predicate open, after what the copula follows
// (なんやろか, 増えるやろね, お前やろが書く, あるじゃろうな, もらえんじゃろうか), and そやろ, せやろ,
// どやろ at the start of a word: the analysis reads them as だろう. A sentence-final one is read
// by the end rows. Not the volitional of やる (BeforeCopula).
bool KansaiConjecture(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    // The ね of the Kansai ねん after ん is the の of んだ (乗ってんねやろな 乗ってんだろうな).
    if (s.substr(0, pos).ends_with("ん") && Lit(s, p, "ねやろ") && OpenParticleAt(s, p)) {
        m.group[1] = "だろう";
        m.end = p;
        return true;
    }
    p = pos;
    // After the polite negative ません or まへん the conjecture is でしょう (もらえまへんやろか).
    const std::string_view head = s.substr(0, pos);
    const bool politeNegative = head.ends_with("ません") || head.ends_with("まへん");
    if (OneOf(s, p, U"そせど")) {
        if (pos > 0 && IsHiraganaClass(LastRune(s.substr(0, pos)).cp)) return false;
        if (!Lit(s, p, "やろ")) return false;
    } else if (!politeNegative && !BeforeCopula(s, pos)) {
        return false;
    } else if (!Lit(s, p, "やろ")) {
        if (!Lit(s, p, "じゃろ")) return false;
        if (size_t q = p; Lit(s, q, "う") && OpenParticleAt(s, q)) p = q;
    }
    if (!OpenParticleAt(s, p)) return false;
    if (politeNegative) m.group[1] = "でしょう";
    m.end = p;
    return true;
}

std::string ReplaceKansaiConjecture(const Match& m) {
    if (!m.group[1].empty()) return std::string(m.group[1]);
    if (m.group[0].starts_with("ど")) return "どうだろう";
    if (m.group[0].starts_with("そ") || m.group[0].starts_with("せ")) return "そうだろう";
    return "だろう";
}

std::string ReplaceKansaiNegative(const Match& m) {
    const Match g = Rematch(m, KansaiNegative);
    std::string_view stem = g.group[1];
    if (stem == "あら" || stem == "あれ") stem = {};
    else if (stem == "せえ") stem = "し";
    for (const auto& drawn : kKansaiDrawnStems) {
        if (stem == drawn[0]) stem = drawn[1];
    }
    // The old negative せん keeps the tail for the contraction stage, which reads it the same.
    if (!g.group[4].empty()) return Cat({stem, g.group[4], g.group[2], g.group[3]});
    if (!g.group[5].empty()) return Cat({stem, g.group[5]});
    return Cat({stem, g.group[2].empty() ? "ない" : "なかった"});
}

// The Kansai polite negative まへん in words of their own: おまへん ありません, すんまへん and
// すいまへん すみません, あきまへん いけません, かまへん かまわない and でけしまへん できません.
// まへん after the stem of a verb is read by the word stage as ません.
bool KansaiMahen(std::string_view s, size_t pos, Match& m) {
    // The sentence-final で after it is the よ of Kansai speech (あきまへんで いけませんよ), here also
    // before a comma: a polite negative has no て form in で.
    auto finalDe = [&](size_t p) {
        const size_t de = SentenceFinalParticle(s.substr(p), "で", true);
        if (de == 0) return p;
        m.group[2] = "よ";
        return p + de;
    };
    static constexpr std::string_view kWords[][2] = {
        {"おまへん", "ありません"}, {"すんまへん", "すみません"}, {"すいまへん", "すいません"}, {"あきまへん", "いけません"},
        {"かまへん", "かまわない"}, {"でけしまへん", "できません"}};
    m.group[2] = {};
    for (const auto& w : kWords) {
        if (size_t p = pos; Lit(s, p, w[0])) {
            m.group[1] = w[1];
            // かまへんで before a comma may be a て form (かまわないで、); before the end it is the よ.
            if (w[0] == "かまへん") {
                const size_t de = SentenceFinalParticle(s.substr(p), "で", false);
                if (de > 0) m.group[2] = "よ";
                m.end = p + de;
            } else {
                m.end = finalDe(p);
            }
            return true;
        }
    }
    return false;
}

// The Kansai なんや or 何や of a question or surprise, なんだ (なんや、お前か 뭐야, 너야, なんやそれ 뭐야 그거): at the
// start of a sentence or a clause or after another なんや, before a mark, the end, なんや, a final ろ or それ, これ, あれ;
// before ねん it is なんだよ (なんやねん 뭐야). Not なんやかんや, なんやら or なんや before a verb (なんや知らんけど,
// 何だか).
// とちゃう, the Kansai ではない, after the ん of のだ, a word in kanji or katakana, それ or そう, a へん read ない or 過ぎ (多いんとちゃう？
// 많지 않아?, 彼女とちゃうやろ 여자친구가 아니잖아, 偏り過ぎとちゃう？): じゃない. Not after other kana (かもしれんと
// ちゃう is rare), nor where ちゃう goes on as a verb (とちゃうかった keeps).
bool KansaiTochau(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    const std::string_view head = s.substr(0, pos);
    const char32_t b = LastRune(head).cp;
    bool word = b == U'ん' || IsKanjiClass(b) || IsKatakanaRune(b) || head.ends_with("過ぎ") || head.ends_with("すぎ") ||
                head.ends_with("ない");
    for (const std::string_view w : {"それ", "これ", "あれ", "そう", "こう", "ああ"}) word = word || head.ends_with(w);
    if (!word) return false;
    size_t p = pos;
    if (!Lit(s, p, "とちゃう")) return false;
    const std::string_view after = s.substr(p);
    if (after.starts_with("かった") || after.starts_with("て") || after.starts_with("ちゃう")) return false;
    // After それ, これ, あれ the じゃ of じゃない would read as それじゃ (then); と違う keeps the noun (それとちゃうねん
    // 그거랑 다르거든).
    const bool demonstrative = head.ends_with("それ") || head.ends_with("これ") || head.ends_with("あれ");
    m.group[1] = demonstrative ? "と違う" : "じゃない";
    m.end = p;
    // とちゃうん asked is じゃないの (東京とちゃうん？ 도쿄 아니야?).
    if (!demonstrative && after.starts_with("ん")) {
        size_t q = p + std::string_view("ん").size();
        if (q == s.size() || OneOf(s, q, U"？?。！!…")) {
            m.group[1] = "じゃないの";
            m.end = p + std::string_view("ん").size();
        }
    }
    return true;
}

// The Kansai request not to do, the negative stem with んとって (せんとって しないで, 行かんとって 行かないで,
// 動かんとってください, 気にせんとって下さい): ないで. Only after a kana of the あ row or a せ, the し of する or the せ
// of an ichidan verb (見せんとって 見せないで, 思い出させんとって 思い出させないで).
bool KansaiNtotte(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"あかさたなはまらわがざだばぱせ")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "んとって")) return false;
    if (s.substr(p).starts_with("もら")) return false;  // ないでもらう reads apart
    m.end = p;
    return true;
}

// Whether the sentence before ends in a part that holds a question word (何, 誰, なんで, どこ, いつ, どう).
bool InterrogativeBefore(std::string_view head) {
    std::size_t start = 0;
    for (const std::string_view mark : {"。", "！", "？", "!", "?", "「", "『", "\n"}) {
        const std::size_t at = head.rfind(mark);
        if (at != std::string_view::npos) start = std::max(start, at + mark.size());
    }
    const std::string_view sentence = head.substr(start);
    for (const std::string_view w : {"何", "なに", "なん", "誰", "だれ", "どこ", "いつ", "どう", "なぜ"}) {
        if (sentence.find(w) != std::string_view::npos) return true;
    }
    return false;
}

// The Kansai ておるん said とん after the っ or し of a て form (何しとんねん 뭐 하는 거야, なっとんねん, 楽しみにしとんの):
// てるん, before ねん, の, や or か; the ねん is kept for the endings of Kansai (てんねん). Not すっとん, ぶっとん, きんとん.
bool KansaiTon(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"っし")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "とん")) return false;
    // Not すっとん, ぶっとん (素っ飛ぶ, ぶっ飛ぶ).
    if (m.group[1] == "っ" && pos > 0 && In(LastRune(s.substr(0, pos)).cp, U"すぶ")) return false;
    const std::string_view after = s.substr(p);
    if (after.starts_with("ねん")) {
        // With a question word in the sentence the ねん asks (何しとんねん 뭐 하는 거야): てるんだ.
        m.group[2] = InterrogativeBefore(s.substr(0, pos)) ? "てるんだ" : "てんねん";
        m.end = p + std::string_view("ねん").size();
        return true;
    }
    if (after.starts_with("のじゃ")) {  // old speech keeps おる (何をしとんのじゃ 뭘 하고 있는 게야)
        m.group[2] = "ておる";
        m.end = p;
        return true;
    }
    if (after.starts_with("の")) {
        m.group[2] = "てる";
        m.end = p;
        return true;
    }
    for (const std::string_view w : {"や", "か"}) {
        if (after.starts_with(w)) {
            m.group[2] = "てるん";
            m.end = p;
            return true;
        }
    }
    return false;
}

// A question word with the Kansai やねん (誰やねん 누구야, 何がやねん 뭐가, なんやねん after a word): なんだよ, or
// だよ after 何, なん, 何が. なんでやねん stays the retort that other rows read (왜 그래).
bool KansaiQuestionYanen(std::string_view s, size_t pos, Match& m) {
    for (const std::string_view w : {"何で", "なんで", "何が", "なにが", "誰", "だれ", "どこ", "いつ", "何", "なん", "なに", "どう"}) {
        if (w == "何で" || w == "なんで") {  // なんでやねん is the retort of its own (왜 그래)
            size_t q = pos;
            if (Lit(s, q, w) && Lit(s, q, "やねん")) return false;
            continue;
        }
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        if (!Lit(s, p, "やねん")) return false;
        m.group[1] = w;
        m.group[2] = (w == "何が" || w == "なにが" || w == "何" || w == "なん" || w == "なに") ? "だよ" : "なんだよ";
        m.end = p;
        return true;
    }
    return false;
}

// The old copula じゃ before the quoting と of a question or a cry, or of a verb of saying or thinking (10両じゃと？
// 10냥이라고?, 無理じゃと言う 무리라고): だと. After a word in kanji or katakana, or a verb or adjective in る, た, い.
bool OldCopulaJato(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    const std::string_view head = s.substr(0, pos);
    const char32_t b = LastRune(head).cp;
    if (!(IsKanjiClass(b) || IsKatakanaRune(b) || In(b, U"るたい"))) return false;
    size_t p = pos;
    if (!Lit(s, p, "じゃと")) return false;
    const std::string_view after = s.substr(p);
    bool ok = after.empty();
    for (const std::string_view w : {"？", "?", "！", "!", "～", "〜", "ー", "…", "言", "いう", "いっ", "思", "おも", "申"}) ok = ok || after.starts_with(w);
    if (!ok) return false;
    m.end = p;
    return true;
}

// The Kansai そや (そうだ) where a clause starts, before な, ね, から, ねん, で, a mark or the end (そやけど stays for its own row) (そやな 그렇지,
// そやから 그러니까, そやねん 그렇다니까): そうだ, だから for そやから, そうなんだ for そやねん.
bool KansaiSoya(std::string_view s, size_t pos, Match& m) {
    if (pos > 0 && !In(LastRune(s.substr(0, pos)).cp, U"「『（(　 、，,。！？!?…‥―─～") && !s.substr(0, pos).ends_with("ああ")) {
        return false;
    }
    size_t p = pos;
    if (!Lit(s, p, "そや")) return false;
    const std::string_view after = s.substr(p);
    if (after.starts_with("から")) {
        m.group[1] = "だから";
        m.end = p + std::string_view("から").size();
        return true;
    }
    if (after.starts_with("ねん")) {
        m.group[1] = "そうなんだ";
        m.end = p + std::string_view("ねん").size();
        return true;
    }
    bool ok = after.empty();
    for (const std::string_view w : {"な", "ね", "で", "し", "。", "、", "！", "？", "!", "?", "…", " ", "　"}) ok = ok || after.starts_with(w);
    if (!ok) return false;
    m.group[1] = "そうだ";
    m.end = p;
    return true;
}

bool KansaiNanya(std::string_view s, size_t pos, Match& m) {
    if (pos > 0 && !In(LastRune(s.substr(0, pos)).cp, U"「『（(　 、，,。！？!?…‥―─～") &&
        !s.substr(0, pos).ends_with("なんや") && !s.substr(0, pos).ends_with("何や")) {
        return false;
    }
    size_t p = pos;
    if (!Lit(s, p, "なんや") && !Lit(s, p, "何や")) return false;
    m.group[1] = Span(s, pos, p - std::string_view("や").size());
    m.group[2] = {};
    if (size_t q = p; Lit(s, q, "ねん")) {
        m.group[2] = "よ";
        m.end = q;
        return true;
    }
    size_t q = p;
    const std::string_view after = s.substr(p);
    // A ろ that ends the sentence is the conjecture, なんだろ (なんやろ？ 뭘까?); KansaiConjecture reads the rest
    // (なんやろか).
    if (SentenceFinalParticle(after, "ろ") > 0 || SentenceFinalParticle(after, "ろう") > 0) {
        m.end = p;
        return true;
    }
    if (p != s.size() && !OneOf(s, q, U"、，,。！？!?…‥」』）)　 ー～") && !after.starts_with("それ") &&
        !after.starts_with("これ") && !after.starts_with("あれ") && !after.starts_with("なんや") && !after.starts_with("何や")) {
        return false;
    }
    m.end = p;
    return true;
}

// The Kansai ええ of いい (ええよ いいよ, もうええわ, 選んでもええけど, ええ感じ): before a sentence particle, the
// copula, a conjunction or a noun of ええ (kKansaiEeBefore), after a particle, a て form or a word in kanji or
// katakana or where a clause starts; a sentence-final で after it is the よ of Kansai speech (ええで いいよ). Not
// the ええ of yes (ええ、そうです), the filler ええと, a cry (ええっ, ひええ) or the drawn end of an adjective
// (こええ 怖い, つええ, はええな, 怖ええ, 強ええ; kKansaiEeAdjectiveKanji).
bool KansaiEe(std::string_view s, size_t pos, Match& m) {
    if (pos > 0) {
        const std::string_view head = s.substr(0, pos);
        const char32_t b = LastRune(head).cp;
        const bool clauseStart = In(b, U"「『（(　 、，,。！？!?…‥―─～");
        if (!clauseStart && !IsKanjiClass(b) && !IsKatakanaRune(b) && !In(b, U"でもがはらばとてのうあち")) return false;
        // After the stem of an adjective it is the adjective drawn out (怖ええよ 무서워, 強ええな).
        if (In(b, kKansaiEeAdjectiveKanji)) return false;
        // は that starts a clause is the drawn 速い (はええな).
        if (b == U'は' && (head.size() == std::string_view("は").size() ||
                          In(LastRune(head.substr(0, head.size() - std::string_view("は").size())).cp, U"「『（(　 、，,。！？!?…‥"))) {
            return false;
        }
    }
    size_t p = pos;
    if (!Lit(s, p, "ええ")) return false;
    m.group[1] = {};
    if (const size_t de = SentenceFinalParticle(s.substr(p), "で"); de > 0) {
        m.group[1] = "よ";
        m.end = p + de;
        return true;
    }
    for (const std::string_view w : kKansaiEeBefore) {
        if (s.substr(p).starts_with(w)) {
            m.end = p;
            return true;
        }
    }
    // At the end, before a mark or a space, after a particle, the ええ that ends a clause is
    // the adjective (それでええ それでいい, 知らんほうがええ, もうええ 下がれ), not the yes of a reply,
    // which opens its clause.
    if (pos > 0) {
        size_t q = p;
        const char32_t b = LastRune(s.substr(0, pos)).cp;
        // The particle has a word before it (not でええい of a shout).
        const std::string_view lead = s.substr(0, pos - std::string_view("で").size());
        const bool word = !lead.empty() && !In(LastRune(lead).cp, U"「『（(　 、，,。！？!?…‥―─～");
        const bool after = (In(b, U"でもが") && word) ||
                           (s.substr(0, pos).ends_with("もう") && !In(LastRune(s.substr(0, pos - std::string_view("う").size())).cp, U"い"));
        if (after && (p == s.size() || OneOf(s, q, U"。｡、！!？?…‥」』 　"))) {
            m.end = p;
            return true;
        }
    }
    return false;
}

// The copula じゃ of western and old speech before the が of a contrast, だ (仕方ないのじゃが…, そうじゃが、,
// 思うんじゃがな, 件じゃがの。, a sentence-initial じゃが、 だが、): only where the が ends the clause, before a
// comma, な, ね, のう, の before a mark, a closing mark or the end, or after the ん of のだ; elsewhere じゃが is the potato (じゃがいも,
// 新じゃがを, じゃがバタ, じゃがと人参, じゃがの皮, 肉じゃが).
bool OldCopulaJaga(std::string_view s, size_t pos, Match& m) {
    if (s.substr(0, pos).ends_with("肉")) return false;
    size_t p = pos;
    if (!Lit(s, p, "じゃが")) return false;
    constexpr std::u32string_view kEnds = U"、，,。｡！!？?…‥」』）)";
    size_t q = p;
    bool ends = q == s.size() || OneOf(s, q, kEnds);
    for (const std::string_view w : {"な", "ね", "のう"}) {
        q = p;
        ends = ends || Lit(s, q, w);
    }
    q = p;
    if (Lit(s, q, "の")) ends = ends || q == s.size() || OneOf(s, q, kEnds);
    // After the ん of のだ the が is the copula's before any word (そうなんじゃが判を, 来たんじゃが仕方ない).
    if (!ends && s.substr(0, pos).ends_with("ん")) {
        ends = true;
        for (const std::string_view w : {"い", "イ", "芋", "バター"}) ends = ends && !s.substr(p).starts_with(w);
    }
    if (!ends) return false;
    m.end = p;
    return true;
}

// The たく of a wish said with the う of western and old speech before its negative (行きとうない
// 行きたくない, 見とうなかった 見たくなかった, 負けとうなくて): とう after a verb's continuative, a
// hiragana of the い or え row or the one kanji of an ichidan stem (見, 寝, 居, 出, 来, 着). Not
// ありがとう or おめでとう, nor とう after another kana (さとう, もうとうない).
bool OldWishNegative(std::string_view s, size_t pos, Match& m) {
    const char32_t before = LastRune(s.substr(0, pos)).cp;
    if (!In(before, U"いきぎしじちにひびみりえけげせぜてでねべめれ見寝居出来着")) return false;
    size_t p = pos;
    if (!Lit(s, p, "とうな")) return false;
    const size_t tail = p;
    if (!Lit(s, p, "い") && !Lit(s, p, "かった") && !Lit(s, p, "く")) return false;
    m.group[1] = s.substr(tail - std::string_view("な").size(), p - tail + std::string_view("な").size());
    m.end = p;
    return true;
}

// Whether a せ after head ends the stem of an ichidan verb, not the negative stem of する: after one
// kanji (見せ, 任せ, 乗せ) or the negative stem of a causative after its kanji (待たせ, 行かせ), not
// after a noun of two kanji or more (勉強せ), a particle or an adverb in か (何とかせ, どうにかせ).
bool IchidanSe(std::string_view head) {
    const char32_t before = LastRune(head).cp;
    const std::string_view rest = head.substr(0, head.size() - LastRune(head).size);
    if (IsKanjiClass(before)) return !IsKanjiClass(LastRune(rest).cp);
    return In(before, U"かがさたなばまらわ") && IsKanjiClass(LastRune(rest).cp);
}

// The Kansai あかん (アカン) of a predicate, いけない: alone (もうあかん, 見たらあかん, 行かなきゃあかん),
// with the must of a bare な or んと after a negative stem written in full (撮らなあかん 撮らなきゃいけない,
// せなあかん しなきゃいけない, 行かんとあかん 行かないといけない), its past (あかんかった いけなかった) and a
// sentence-final で, the よ of Kansai speech (あかんで いけないよ). Not 埒があかん or ドアがあかん (it does not open), a
// word that goes on (あかんべ, あかんぼう, あかんたれ) or a katakana word (ミチョアカン).
bool KansaiAkan(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (head.ends_with("埒が") || head.ends_with("らちが") || head.ends_with("埒") || head.ends_with("らち")) return false;
    // A door, a lid or an eye before が: it does not open (ドアがあかん, 目があかん).
    for (const std::string_view opens : {"ドアが", "扉が", "戸が", "窓が", "蓋が", "ふたが", "鍵が", "目が", "瞼が", "まぶたが"}) {
        if (head.ends_with(opens)) return false;
    }
    size_t p = pos;
    // group 1 the must before あかん, written in full
    if (Lit(s, p, "せな") || Lit(s, p, "せんと")) {
        m.group[1] = s.substr(pos, p - pos) == "せな" ? "しなきゃ" : "しないと";
    } else if (const char32_t before = LastRune(head).cp;
               (IsHiraganaClass(before) || IsKanjiClass(before)) && before != U'ん' && before != U'な' &&
               (Lit(s, p, "な") || (In(before, U"かがさたなばまらわ") && Lit(s, p, "んと")))) {
        m.group[1] = s.substr(pos, p - pos) == "な" ? "なきゃ" : "ないと";
    } else {
        p = pos;
    }
    const size_t word = p;
    if (!Lit(s, p, "あかん") && !Lit(s, p, "アカン")) return false;
    const bool katakana = s.substr(word).starts_with("アカン");
    // An あかん that opens an utterance before a comma is the interjection of the phrase rows
    // (あかん、よう盛り上がらんわ 안 돼, ...).
    if (word == pos &&
        (head.empty() || !(IsHiraganaClass(LastRune(head).cp) || IsKanjiClass(LastRune(head).cp) || IsKatakanaRune(LastRune(head).cp))) &&
        (s.substr(p).starts_with("、") || s.substr(p).starts_with("，") || s.substr(p).starts_with(","))) {
        return false;
    }
    if (katakana && word == pos && IsKatakanaRune(LastRune(head).cp)) return false;
    if (p < s.size() && katakana && IsKatakanaRune(At(s, p).cp)) return false;
    size_t q = p;
    if (Lit(s, q, "かった")) {
        m.group[2] = "いけなかった";
        m.end = q;
        return true;
    }
    q = p;
    if (Lit(s, q, "で") && (q == s.size() || OneOf(s, q, U"。｡、！!？?…‥」』）　 ーっぇえ〜～"))) {
        m.group[2] = "いけないよ";
        m.end = p + std::string_view("で").size();
        return true;
    }
    q = p;
    const bool ends = q == s.size() || OneOf(s, q, U"。｡、，,！!？?…‥」』）)　 ーっッ～〜・") || Take(s, q, IsKanjiClass);
    bool follows = ends;
    for (const std::string_view w : {"ねん", "ねや", "のや", "のよ", "のん", "の", "で", "わ", "よ", "し", "から", "かな", "かも", "か",
                                     "やろ", "やん", "って", "て", "な", "ね", "けど", "けれど", "のに", "もん", "ぞ", "ぜ", "と",
                                     "ちゅう", "え", "が", "や"}) {
        q = p;
        follows = follows || Lit(s, q, w);
    }
    if (!follows) return false;
    m.group[2] = "いけない";
    m.end = p;
    return true;
}

// The Kansai しもうた, しもた of しまう and もろうた, もろた of もらう after a て form, and their て
// forms (行ってしもうた, 来てもろて): しまった, しまって, もらった, もらって.
bool KansaiShimota(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"てで")) return false;
    m.group[1] = Span(s, pos, p);
    for (const std::string_view stem : {"しも", "もろ"}) {
        size_t q = p;
        if (!Lit(s, q, stem)) continue;
        size_t r = q;
        Lit(s, r, "う");
        if (size_t t = r; Lit(s, t, "た") || Lit(s, t, "て")) {
            m.group[2] = stem == "しも" ? "しまっ" : "もらっ";
            m.group[3] = Span(s, r, t);
            m.end = t;
            return true;
        }
    }
    return false;
}

// The Kyoto どす of the copula after the ん of のです, and its past どした (やったんどす,
// 覚えててくれはったんどすか): です, でした, where the copula ends the predicate: before the end, a
// mark, a space or a sentence particle (え, か, な, ね, よ, わ, けど; not か after どした, nor って).
// Elsewhere なんど is 何度 and す, した the verb (なんどすれば, なんどしたか). Not after the もん of
// もの or the さん or はん of address (野暮っちゅうもんどす, おめでとうさんどす), whose ん is no の.
bool KyotoDosu(std::string_view s, size_t pos, Match& m) {
    const std::string_view head = s.substr(0, pos);
    if (head.ends_with("も") || head.ends_with("さ") || head.ends_with("は")) return false;
    size_t p = pos;
    if (!Lit(s, p, "んど")) return false;
    for (const auto& [kyoto, standard] : {std::pair<std::string_view, std::string_view>{"す", "んです"}, {"した", "んでした"}}) {
        size_t q = p;
        if (!Lit(s, q, kyoto)) continue;
        size_t r = q;
        const bool ends = q == s.size() || OneOf(s, r, U"。｡、！!？?…‥」』）) 　＞>～ー");
        bool particle = false;
        for (const std::string_view w : {"え", "か", "な", "ね", "よ", "わ", "けど"}) {
            size_t t = q;
            particle = particle || (Lit(s, t, w) && !(kyoto == "した" && w == "か"));
        }
        if (!ends && !particle) return false;
        m.group[1] = standard;
        m.end = q;
        return true;
    }
    return false;
}

bool ContractedToru(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        for (std::string_view ending : kToruEndings) {
            size_t q = p;
            if (!Lit(s, q, ending)) continue;
            const size_t end = q;
            if (!OneOf(s, q, U"！!？?。、…‥」』ぞよわがけでなのやっん") && end != s.size()) continue;
            m.group[2] = Span(s, p, end);
            m.group[3] = Span(s, end, q);
            m.end = q;
            return true;
        }
        return false;
    };
    size_t p = pos;
    if (OneOf(s, p, U"いっし") && Lit(s, p, "と") && rest(p)) return true;
    p = pos;
    if (Lit(s, p, "んど") && rest(p)) return true;
    // The と form of くれる, whose くれ the word stage reads inside the ending row of a て form, where
    // it cannot take the と form (入れてくれとった 入れてくれていた).
    p = pos;
    return Lit(s, p, "くれと") && rest(p);
}

std::string ReplaceContractedToru(const Match& m) {
    const Match g = Rematch(m, ContractedToru);
    const bool voiced = g.group[1].ends_with("ど");
    const std::string_view contracted = voiced ? "ど" : "と";
    const std::string_view stem = g.group[1].substr(0, g.group[1].size() - contracted.size());
    std::string_view ending;
    if (g.group[2] == "る") ending = "いる";
    else if (g.group[2] == "った") ending = "いた";
    else if (g.group[2] == "らん") ending = "いない";
    else if (g.group[2] == "れ") ending = "いろ";
    return Cat({stem, voiced ? "で" : "て", ending, g.group[3]});
}

bool ExclamationNa(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "な")) return false;
        return Repeat(s, p, InSet(U"ぁあー～"), 0, kUnbounded, [&](size_t q) {
            size_t r = q;
            if (!OneOf(s, r, U"！!？?。、…‥」』）") && q != s.size()) return false;
            m.group[2] = Span(s, p, q);
            m.group[3] = Span(s, q, r);
            m.end = r;
            return true;
        });
    };
    constexpr std::u32string_view excluded = U"\t\n\f\r 、。！？!?…‥「」『』";
    auto afterRun = [&](size_t p, size_t min, size_t max) {
        // Try shorter rune runs before longer ones.
        for (size_t count = 0;; ++count) {
            if (count >= min) {
                size_t q = p;
                if (OneOf(s, q, U"うくぐすつぬぶむる") && rest(q)) return true;
            }
            if (count == max || !NoneOf(s, p, excluded)) return false;
        }
    };
    size_t p = pos;
    if (NoneOf(s, p, U"\t\n\f\r 、。！？!?…‥「」『』（") && Lit(s, p, "が") && afterRun(p, 1, 8)) return true;
    p = pos;
    if (Lit(s, p, "よく") && afterRun(p, 0, 4)) return true;
    for (std::string_view stem : kExclamationNaStems) {
        p = pos;
        if (Lit(s, p, stem) && rest(p)) return true;
    }
    return false;
}

std::string ReplaceExclamationNa(const Match& m) {
    const Match g = Rematch(m, ExclamationNa);
    if (dict::EndsWithMember(HDTRANS_CLASS("ending.no-exclamation-na"), g.group[1]) > 0) return std::string(m.group[0]);
    size_t trim = 0;
    while (OneOf(g.group[2], trim, U"ぁあ")) {}
    return Cat({g.group[1], "なあ", g.group[2].substr(trim), g.group[3]});
}

struct SlangAdjective {
    std::string_view stem;   // the spelling before the drawn-out え
    std::string_view plain;  // the dictionary form
    bool afterWord;          // also right after kana or kanji (なんかすげえ); a short stem
                             // is not, so that 書いてー and 見ようぜえ stay
    bool alone = false;      // also standing alone, where the interjection rows read it
                             // (すげえ！ 대박, うるせえ。 시끄러)
};

// Adjectives spelled with a drawn-out え (すげえ すごい, うるせー うるさい); a stem that
// starts another comes after it.
constexpr SlangAdjective kSlangAdjectives[] = {
    {"めんどくせ", "面倒くさい", true, true}, {"すっげ", "すごい", true}, {"すげ", "すごい", true},
    {"うるせ", "うるさい", true},             {"うっせ", "うるさい", true}, {"かっけ", "カッコいい", true, true},
    {"あぶね", "危ない", true, true},         {"危ね", "危ない", true, true}, {"やべ", "やばい", true},
    {"こえ", "怖い", true, true},             {"怖", "怖い", true, true},   {"ちげ", "違う", true, true},
    {"パネ", "ハンパない", true, true},       {"ぱね", "ハンパない", false, true}, {"いて", "痛い", false, true},
    {"うめ", "美味い", false, true},          {"でけ", "でかい", false, true}, {"つえ", "強い", false, true},
    {"よえ", "弱い", false, true},            {"くせ", "臭い", false, true}, {"うぜ", "うざい", false, true},
    {"きめ", "きもい", false, true},          {"はえ", "早い", false, true},
};

// What may follow a slang adjective: a mark, a space, the end, or a particle.
constexpr std::u32string_view kSlangMarks = U"ｗw～〜 　！!？?。、…‥」』）)";
constexpr std::u32string_view kSlangParticles = U"よなねぞぜしっとだじかけの";

// Whether s continues at p with the stem of a slang adjective (すげえすげえ).
bool SlangStemAt(std::string_view s, size_t p) {
    for (const SlangAdjective& a : kSlangAdjectives) {
        if (s.substr(p).starts_with(a.stem)) return true;
    }
    return false;
}

// (すげ|うるせ|…)[えぇーェエ]+ before a particle, a mark, another of them or the end; a
// sentence-final わ after it goes as well (すげえわ すごい, こええよ 怖いよ). Group 1 the
// plain form. 違いねえ (ちげえね) stays.
bool SlangAdjectivePattern(std::string_view s, size_t pos, Match& m) {
    const char32_t before = pos == 0 ? 0 : LastRune(s.substr(0, pos)).cp;
    const bool afterWord = IsHiraganaClass(before) || IsKanjiClass(before) || before == U'ー';
    const bool opens = pos == 0 || In(before, U" 　、。！!？?…‥「『（(");
    for (const SlangAdjective& a : kSlangAdjectives) {
        size_t p = pos;
        if (!Lit(s, p, a.stem) || (afterWord && !a.afterWord)) continue;
        const bool ok = Repeat(s, p, InSet(U"えぇーェエ"), 1, kUnbounded, [&](size_t q) {
            if (a.stem == "ちげ" && s.substr(q).starts_with("ね")) return false;
            if (SlangStemAt(s, q)) {
                m.end = q;
                return true;
            }
            size_t r = q;
            if (Lit(s, r, "わ")) {
                while (Lit(s, r, "ー") || Lit(s, r, "ぁ") || Lit(s, r, "～")) {}
                size_t t = r;
                if (r == s.size() || !Take(s, t, IsHiraganaClass)) {
                    m.end = r;
                    return true;
                }
            }
            r = q;
            if (q != s.size() && !OneOf(s, r, kSlangMarks) && !OneOf(s, r, kSlangParticles)) return false;
            r = q;
            if (opens && !a.alone && (q == s.size() || OneOf(s, r, kSlangMarks))) return false;
            m.end = q;
            return true;
        });
        if (ok) {
            m.group[1] = a.plain;
            return true;
        }
    }
    return false;
}

// (こっち|お前|…)わ before a mark, a space, kanji, katakana or the end: group 1 the
// pronoun (こっちわ大丈夫 こっちは). わ before hiragana stays (お前わかってる).
// The Kansai やし of a noun before a mark or the end is the だし that lists a reason (雨やし、 雨だし、);
// not やし after kana (しやしない, やしの木 after a particle).
bool KansaiYashi(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    const char32_t before = LastRune(s.substr(0, pos)).cp;
    if (!IsKanjiClass(before) && !(before >= U'ァ' && before <= U'ヺ') && before != U'ー') return false;
    // Not the stem of a verb in やす (費やし、 増やし、 冷やし、 生やし).
    if (In(before, U"費増冷生燃肥癒絶殖")) return false;
    size_t p = pos;
    if (!Lit(s, p, "やし")) return false;
    size_t q = p;
    if (p != s.size() && !OneOf(s, q, U"、。！？!?…‥」』 ")) return false;
    m.end = p;
    return true;
}

// The なん of ならない said short after a must (やらなきゃなんない, 行かなくちゃなんねえ) is なら.
bool MustNannai(std::string_view s, size_t pos, Match& m) {
    for (const std::string_view must : {"なきゃ", "なくちゃ", "なけりゃ", "なけれ"}) {
        size_t p = pos;
        if (!Lit(s, p, must) || !Lit(s, p, "なん")) continue;
        for (const std::string_view neg : {"ない", "ねえ", "ねー", "ねぇ"}) {
            size_t q = p;
            if (!Lit(s, q, neg)) continue;
            m.group[1] = must;
            m.end = q;
            return true;
        }
    }
    return false;
}

// The やだ of いやだ said short (絶対やだよ 絶対いやだよ, こんなのやだ, もうやだ, ウェルダンやだ) after kanji or
// katakana, or after の, と, も or もう; not the や ending a word in kana before the copula (ひやひやだ, ろうやだ,
// がんばりやだ). A やだ that starts the text or follows a mark or a drawn kana is the phrase row (やだ！ 싫어,
// やだ何それ, もーやだ) unless plain kana go on after it (やだもう, やだから, やだよ雀).
bool ShortIyada(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "やだ")) return false;
    const std::string_view head = s.substr(0, pos);
    const char32_t before = head.empty() ? 0 : LastRune(head).cp;
    if (IsHiraganaClass(before) && !head.ends_with("の") && !head.ends_with("と") && !head.ends_with("も") &&
        !head.ends_with("もう")) {
        return false;
    }
    if (!IsHiraganaClass(before) && !IsKanjiClass(before) && !(before >= U'ァ' && before <= U'ヺ')) {
        const std::string_view rest = s.substr(p);
        if (rest.empty() || !IsHiraganaClass(LastRune(rest.substr(0, text::DecodeRune(rest).size)).cp)) return false;
        if (rest.starts_with("や") || rest.starts_with("ぁ") || rest.starts_with("もぅ") || rest.starts_with("もー")) {
            return false;
        }
        // Sentence particles up to the end, a mark or the やだ said again (やだね。, やだなぁ, やだっやだっ).
        size_t q = p;
        while (q < s.size() && OneOf(s, q, U"よねなぁあーぇえ～っ")) {}
        size_t r = q;
        if (q == s.size() || s.substr(q).starts_with("や") || OneOf(s, r, U"、。｡！？!?…‥」』 　・")) return false;
    }
    m.end = p;
    return true;
}

// The Kobe とう of ている after し, っ or 見 (何しとう？ 何してる？, 行っとうよ, 見とう), before the end, a mark or
// よ, ん, や, ねん, て; not the old wish before な or ござ (見とうない, 話しとうございます).
bool KobeTou(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "し") && !Lit(s, p, "っ") && !Lit(s, p, "見")) return false;
    m.group[1] = Span(s, pos, p);
    m.group[2] = {};
    if (!Lit(s, p, "とう")) return false;
    if (p != s.size()) {
        const std::string_view after = s.substr(p);
        bool ok = false;
        for (const std::string_view f : {"よ", "ん", "や", "ねん", "て", "？", "?", "！", "!", "。", "、", "…", "」", "』", "ー", "わ"}) {
            ok = ok || after.starts_with(f);
        }
        if (!ok) return false;
    }
    m.end = p;
    return true;
}

bool TopicWa(std::string_view s, size_t pos, Match& m) {
    if (pos > 0) {
        const char32_t before = LastRune(s.substr(0, pos)).cp;
        if (IsHiraganaClass(before) || IsKanjiClass(before) || (before >= U'ァ' && before <= U'ヺ') || before == U'ー') {
            return false;
        }
    }
    for (std::string_view w : dict::ClassMembers(HDTRANS_CLASS("pronoun.topic-wa"))) {
        size_t p = pos;
        if (!Lit(s, p, w) || !Lit(s, p, "わ")) continue;
        size_t q = p;
        if (p != s.size() && (Take(s, q, IsHiraganaClass) || OneOf(s, q, U"ーぁぃぅぇぉっ"))) continue;
        m.group[1] = Span(s, pos, p - std::string_view("わ").size());
        m.end = p;
        return true;
    }
    return false;
}

// A word, then ([たるうすつむぐぬだな])ん(か)?[？?]: the spoken の of a question after a
// predicate (落ちたん？ 落ちたの？, 言うんか？ 言うのか？). Group 1 the predicate's last
// kana, group 2 か. うん？, ううん？ and ふうん？ stay, and so do くん and ぶん (コウジくん？,
// たぶん？) and たん after katakana (ミクたん？); すん after どう or 何 is する (どうするの？).
bool QuestionN(std::string_view s, size_t pos, Match& m) {
    if (pos == 0) return false;
    const char32_t before = LastRune(s.substr(0, pos)).cp;
    if (!IsHiraganaClass(before) && !IsKanjiClass(before) && !(before >= U'ァ' && before <= U'ヺ')) return false;
    size_t p = pos;
    if (!OneOf(s, p, U"たるうすつむぐぬだな")) return false;
    // うん, ううん and ふうん answer; ミクたん is a name with たん.
    if (Span(s, pos, p) == "う" && (before == U'う' || before == U'ぅ' || before == U'ふ')) return false;
    if (Span(s, pos, p) == "た" && ((before >= U'ァ' && before <= U'ヺ') || before == U'ー')) return false;
    // …しちょるん？ and …てくれてるん？ stay.
    if (Span(s, pos, p) == "る" && (before == U'ょ' || s.substr(0, pos).ends_with("くれて"))) return false;
    m.group[1] = Span(s, pos, p);
    // どうすん？ and 何すん？ are する (どうするの？).
    if (m.group[1] == "す" && (before == U'う' || before == U'に' || before == U'何')) m.group[1] = "する";
    if (!Lit(s, p, "ん")) return false;
    const size_t k = p;
    Lit(s, p, "か");
    m.group[2] = Span(s, k, p);
    size_t q = p;
    if (!OneOf(s, q, U"？?")) return false;
    m.end = p;
    return true;
}

// (なんて|何て|なんという|何という)こと(だ)? standing as an exclamation: before a mark or
// the end, after a mark, a space or the start (なんてこと！, 「なんということだ」). It is
// written as なんてこった, the interjection 이럴 수가.
bool WhatAThing(std::string_view s, size_t pos, Match& m) {
    if (pos > 0 && !In(LastRune(s.substr(0, pos)).cp, U" 　、。！!？?…‥「『（(―ー")) return false;
    for (std::string_view lead : {"なんて", "何て", "なんという", "何という"}) {
        size_t p = pos;
        if (!Lit(s, p, lead) || !Lit(s, p, "こと")) continue;
        Lit(s, p, "だ");
        size_t q = p;
        if (p != s.size() && !OneOf(s, q, U"！!？?。、…‥」』）)ー～〜 　")) continue;
        m.end = p;
        return true;
    }
    return false;
}

bool ContainsAny(std::string_view s, std::u32string_view set) {
    for (size_t i = 0; i < s.size();) {
        const text::Rune r = At(s, i);
        if (In(r.cp, set)) return true;
        i += r.size;
    }
    return false;
}

bool Contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// かしら(?:ね[ぇえー～]*|ん) before a character other than hiragana or the end: the ね or ん
// after the sentence-final かしら adds nothing (どうしてかしらね, あるかしらん).
bool KashiraNe(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "かしら")) return false;
    if (Lit(s, p, "ね")) {
        while (OneOf(s, p, U"ぇえー～")) {
        }
    } else if (!Lit(s, p, "ん")) {
        return false;
    }
    size_t q = p;
    if (Take(s, q, IsHiraganaClass)) return false;
    m.end = p;
    return true;
}

// ワケ standing alone, after no katakana and before no katakana or kanji: the noun わけ
// written in katakana (やられるワケにはいかない, そういうワケだ, ワケがわからない).
bool KataWake(std::string_view s, size_t pos, Match& m) {
    if (pos > 0 && IsKatakanaRune(LastRune(s.substr(0, pos)).cp)) return false;
    size_t p = pos;
    if (!Lit(s, p, "ワケ")) return false;
    if (p < s.size()) {
        const char32_t after = At(s, p).cp;
        if (IsKatakanaRune(after) || IsKanjiRune(after)) return false;
    }
    m.end = p;
    return true;
}

// Katakana ホレ outside a katakana run. Before an ending of 惚れる (た, て, る, ない, ます, ちゃ, ず,
// させ, られ, そう, っぽ) or in 惚れ薬, 惚れ込む, 惚れ直す it is that verb, written 惚れ (ホレた女, ホレてる,
// ホレ薬). Before another kanji, a mark or the end it is the interjection ほれ (ホレ見ろ, ホレ、これ). Before
// other kana it stays (ホレおばあさん).
bool KataHore(std::string_view s, size_t pos, Match& m) {
    if (pos > 0 && IsKatakanaRune(LastRune(s.substr(0, pos)).cp)) return false;
    size_t p = pos;
    if (!Lit(s, p, "ホレ")) return false;
    m.group[1] = "ほれ";
    if (p < s.size()) {
        const char32_t after = At(s, p).cp;
        if (IsKatakanaRune(after)) return false;
        if (In(after, U"薬込直")) {
            m.group[1] = "惚れ";
        } else if (IsHiraganaRune(after)) {
            static constexpr std::string_view kEndings[] = {"た", "て", "る", "ない", "なく", "なかっ", "ま",
                                                            "ちゃ", "ず", "させ", "られ", "そう", "っぽ"};
            const auto rest = s.substr(p);
            if (std::none_of(std::begin(kEndings), std::end(kEndings),
                             [rest](std::string_view e) { return rest.starts_with(e); }))
                return false;
            m.group[1] = "惚れ";
        }
    }
    m.end = p;
    return true;
}

// Katakana ハズ outside a katakana run before の, だ, で, が, も, は, な, じゃ, ない, ねえ, a mark or the end is
// the はず of expectation (やったハズの宿題 했을 터인 숙제, そんなハズない); ハズレ and ハズした (外す) stay.
bool KataHazu(std::string_view s, size_t pos, Match& m) {
    if (pos > 0 && IsKatakanaRune(LastRune(s.substr(0, pos)).cp)) return false;
    size_t p = pos;
    if (!Lit(s, p, "ハズ")) return false;
    if (p < s.size()) {
        const char32_t after = At(s, p).cp;
        if (IsKatakanaRune(after) || IsKanjiRune(after)) return false;
        if (IsHiraganaRune(after)) {
            const auto rest = s.substr(p);
            bool ok = false;
            for (const std::string_view w : {"の", "だ", "で", "が", "も", "は", "な", "じゃ", "ねえ", "ねぇ"}) ok = ok || rest.starts_with(w);
            if (!ok) return false;
        }
    }
    m.end = p;
    return true;
}

// The old and rough ください said くだせぇ or くだせえ (任せてくだせぇ 맡겨 주세요).
bool RoughKudasai(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "くだせ") && !Lit(s, p, "下せ")) return false;
    if (!Lit(s, p, "ぇ") && !Lit(s, p, "え")) return false;
    m.end = p;
    return true;
}

// ちゃった, ちまった (じゃった, じまった after the ん or い of a て form) before らしい: てしまった, so that
// らしい reads as the guess it is and not as たら + しい (食べちゃったらしい 먹어버린 것 같다,
// 死んじまったらしくて 죽어버린 것 같아서, 持ってっちゃったらしい 가져가버린 것 같다). The copula じゃった
// (老婆じゃったらしい) and やっちゃった stay.
bool ChattaRashii(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    const std::string_view head = s.substr(0, pos);
    if ((head.ends_with("て") || head.ends_with("で")) && (Lit(s, p, "っちゃった") || Lit(s, p, "っちまった"))) {
        // ていく said てっ (持ってっちゃった 持っていってしまった).
        m.group[1] = "いってしまった";
    } else if (Lit(s, p, "ちゃった") || Lit(s, p, "ちまった")) {
        // やっちゃった, the slip of 저지르다, has its own reading.
        if (head.ends_with("やっ")) return false;
        m.group[1] = "てしまった";
    } else if (Lit(s, p, "じゃった") || Lit(s, p, "じまった")) {
        if (pos == 0 || !In(LastRune(head).cp, U"んい")) return false;
        m.group[1] = "でしまった";
    } else {
        return false;
    }
    if (!s.substr(p).starts_with("らし")) return false;
    m.end = p;
    return true;
}

// 判 or 解 for わか in わかる: after no kanji (評判らしい, 了解っす stay) and before ら, り, る,
// った, って or, for 判, れ (判らない, 解った, 判れば).
bool KanjiWakaru(std::string_view s, size_t pos, Match& m) {
    if (pos > 0 && IsKanjiRune(LastRune(s.substr(0, pos)).cp)) return false;
    size_t p = pos;
    const bool han = Lit(s, p, "判");
    if (!han && !Lit(s, p, "解")) return false;
    size_t q = p;
    if (!OneOf(s, q, han ? U"らりるれ" : U"らりる")) {
        q = p;
        if (!Lit(s, q, "っ") || !OneOf(s, q, U"たて")) return false;
    }
    m.end = p;
    return true;
}

std::string Group1Plus(const Match& m, std::string_view mid) { return Cat({m.group[1], mid, m.group[2]}); }

}  // namespace

std::string SpokenForms(std::string_view in, bool exclamationNa, bool dialectForms, const RunLookup& runs) {
    std::string text(in);
    if (Contains(text, "かしら")) text = ReplaceAll(text, KashiraNe, [](const Match&) { return std::string("かしら"); });
    if (Contains(text, "ワケ")) text = ReplaceAll(text, KataWake, [](const Match&) { return std::string("わけ"); });
    if (Contains(text, "ったらし")) text = ReplaceAll(text, ChattaRashii, [](const Match& m) { return std::string(m.group[1]); });
    if (Contains(text, "ホレ")) text = ReplaceAll(text, KataHore, [](const Match& m) { return std::string(m.group[1]); });
    if (Contains(text, "ハズ")) text = ReplaceAll(text, KataHazu, [](const Match&) { return std::string("はず"); });
    if (Contains(text, "せぇ") || Contains(text, "せえ")) text = ReplaceAll(text, RoughKudasai, [](const Match&) { return std::string("ください"); });
    if (Contains(text, "判") || Contains(text, "解")) {
        text = ReplaceAll(text, KanjiWakaru, [](const Match&) { return std::string("わか"); });
    }
    if (ContainsAny(text, kKataStemStarts)) {
        text = ReplaceAll(text, KataStemPattern, [](const Match& m) {
            const Match g = Rematch(m, KataStemPattern);
            std::string_view hira;
            for (const KataStem& k : kKataStems) {
                if (k.kata == g.group[1]) hira = k.hira;
            }
            return Cat({hira, g.group[2] == kKataAdjectiveEnding ? std::string_view("い") : g.group[2]});
        });
    }
    if (Contains(text, "たか")) text = ReplaceAll(text, TakaNee, [](const Match&) { return std::string("たくはない"); });
    if (Contains(text, "ね")) {
        text = ReplaceAll(text, JaneQuestion, [](const Match& m) { return Group1Plus(m, "ない"); });
        text = ReplaceAll(text, ParticleNee, [](const Match& m) { return Group1Plus(m, "ない"); });
        text = ReplaceAll(text, NoneNee, [](const Match& m) { return Group1Plus(m, "ない"); });
        if (dialectForms) text = ReplaceAll(text, NegativeNee, ReplaceNegativeNee);
    }
    if (dialectForms && Contains(text, "まへん")) {
        text = ReplaceAll(text, KansaiMahen, [](const Match& m) { return Cat({m.group[1], m.group[2]}); });
    }
    if (dialectForms && Contains(text, "へん")) text = ReplaceAll(text, KansaiNegative, ReplaceKansaiNegative);
    if (dialectForms && Contains(text, "とう")) {
        text = ReplaceAll(text, KobeTou, [](const Match& m) { return Group1Plus(m, "てる"); });
    }
    if (dialectForms && Contains(text, "やし")) {
        text = ReplaceAll(text, KansaiYashi, [](const Match&) { return std::string("だし"); });
    }
    if (Contains(text, "やだ")) text = ReplaceAll(text, ShortIyada, [](const Match&) { return std::string("いやだ"); });
    if (Contains(text, "なん")) {
        text = ReplaceAll(text, MustNannai, [](const Match& m) { return Cat({m.group[1], "ならない"}); });
    }
    if (dialectForms && Contains(text, "じゃが")) {
        text = ReplaceAll(text, OldCopulaJaga, [](const Match&) { return std::string("だが"); });
    }
    if (dialectForms && Contains(text, "とうな")) {
        text = ReplaceAll(text, OldWishNegative, [](const Match& m) { return Cat({"たく", m.group[1]}); });
    }
    if (dialectForms && (Contains(text, "あかん") || Contains(text, "アカン"))) {
        const std::string source = text;
        text = ReplaceAll(text, KansaiAkan, [&](const Match& m) {
            std::string_view must = m.group[1];
            if (must.starts_with("し") && IchidanSe(std::string_view(source).substr(0, m.begin))) {
                must = must == "しなきゃ" ? "せなきゃ" : "せないと";
            }
            return Cat({must, m.group[2]});
        });
    }
    if (dialectForms && Contains(text, "とちゃう")) {
        text = ReplaceAll(text, KansaiTochau, [](const Match& m) { return std::string(m.group[1]); });
    }
    if (dialectForms && Contains(text, "そや")) {
        text = ReplaceAll(text, KansaiSoya, [](const Match& m) { return std::string(m.group[1]); });
    }
    if (dialectForms && Contains(text, "じゃと")) {
        text = ReplaceAll(text, OldCopulaJato, [](const Match&) { return std::string("だと"); });
    }
    if (dialectForms && Contains(text, "やねん")) {
        text = ReplaceAll(text, KansaiQuestionYanen, [](const Match& m) { return Cat({m.group[1], m.group[2]}); });
    }
    if (dialectForms && Contains(text, "とん")) {
        text = ReplaceAll(text, KansaiTon, [](const Match& m) { return Cat({m.group[1], m.group[2]}); });
    }
    if (dialectForms && Contains(text, "んとって")) {
        const std::string source = text;
        text = ReplaceAll(text, KansaiNtotte, [&](const Match& m) {
            if (m.group[1] != "せ") {
                // The negative stem of a verb of the dictionary (動か 動く, 買わ 買う; not たくさんとって).
                static constexpr std::string_view kA[] = {"あ", "か", "さ", "た", "な", "は", "ま", "ら", "わ", "が", "ざ", "だ", "ば", "ぱ"};
                static constexpr std::string_view kU[] = {"う", "く", "す", "つ", "ぬ", "ふ", "む", "る", "う", "ぐ", "ず", "づ", "ぶ", "ぷ"};
                std::string_view u;
                for (std::size_t k = 0; k < std::size(kA); k++) {
                    if (m.group[1] == kA[k]) u = kU[k];
                }
                const std::string dict = Cat({std::string_view(source).substr(0, m.begin), u});
                // A kanji right before the stem kana (動か, 行か, 突っ込ま), not kana that only end like one (たくさん).
                const bool kanji = m.begin > 0 && IsKanjiClass(LastRune(std::string_view(source).substr(0, m.begin)).cp);
                if (!kanji || (runs.verb != nullptr && !runs.verb(runs.context, dict))) return std::string(m.group[0]);
                return Cat({m.group[1], "ないで"});
            }
            // The せ of an ichidan verb: after one kanji alone (見せ, 任せ) or after the あ row kana of a
            // causative or such a verb (思い出させ, 合わせ, 怒らせ, 喜ばせ, 待たせ);
            // after two kanji, katakana, a particle or a kana noun it is する (心配せ, 気にせ, いじわるせ).
            const std::string_view head = std::string_view(source).substr(0, m.begin);
            const text::Rune b = LastRune(head);
            const bool oneKanji = IsKanjiClass(b.cp) && (head.size() == b.size || !IsKanjiClass(LastRune(head.substr(0, head.size() - b.size)).cp));
            const bool ichidan = oneKanji || In(b.cp, U"かさたなまらわば");
            return std::string(ichidan ? "せないで" : "しないで");
        });
    }
    if (dialectForms && Contains(text, "ええ")) {
        text = ReplaceAll(text, KansaiEe, [](const Match& m) { return Cat({"いい", m.group[1]}); });
    }
    if (dialectForms && Contains(text, "や") && (Contains(text, "なんや") || Contains(text, "何や"))) {
        text = ReplaceAll(text, KansaiNanya, [](const Match& m) { return Cat({m.group[1], "だ", m.group[2]}); });
    }
    if (dialectForms && (Contains(text, "やろ") || Contains(text, "じゃろ"))) {
        text = ReplaceAll(text, KansaiConjecture, ReplaceKansaiConjecture);
    }
    if (dialectForms && (Contains(text, "しも") || Contains(text, "もろ"))) {
        text = ReplaceAll(text, KansaiShimota, [](const Match& m) { return Cat({m.group[1], m.group[2], m.group[3]}); });
    }
    if (dialectForms && Contains(text, "んど")) {
        text = ReplaceAll(text, KyotoDosu, [](const Match& m) { return std::string(m.group[1]); });
    }
    if (dialectForms && (Contains(text, "と") || Contains(text, "ど")))
        text = ReplaceAll(text, ContractedToru, ReplaceContractedToru);
    if (Contains(text, "ん")) {
        if (runs.verbForm != nullptr && Contains(text, "ってん")) {
            // After a word that is no godan verb the って quotes, っていうんだ said short: N(이)라는 거야 (何ってんだ), and
            // before だろ, でしょ or です the ってこと that reads the same (土瓶ってんですか N라는 것입니까, 魔女だってんだろ
            // 마녀라는 거지); after one it is the て form of ている, left to ContractedTeru (言ってんだろ).
            const std::string_view source = text;
            text = ReplaceAll(text, QuoteTten, [&](const Match& m) {
                // A key of the dictionary that spells the whole or ends in ってん keeps its row (何だってんだ 뭐라는 거야,
                // がってんだ 合点だ).
                if (runs.word != nullptr) {
                    std::string_view head = source.substr(0, m.begin);
                    std::string tail;
                    for (int k = 0; k < 3 && !head.empty(); k++) {
                        const text::Rune r = LastRune(head);
                        tail.insert(0, head.substr(head.size() - r.size));
                        head.remove_suffix(r.size);
                        if (runs.word(runs.context, tail + std::string(m.group[0])) ||
                            runs.word(runs.context, tail + std::string(m.group[2]) + "ってん")) {
                            return std::string(m.group[0]);
                        }
                    }
                }
                // N(이)라는 거야 says what the copula said (魔女だってんだろ 마녀라는 거지).
                if (m.group[2].empty() && GodanTeStem(source.substr(0, m.begin), runs)) return std::string(m.group[0]);
                if (m.group[1] == "だ") return std::string("っていうんだ");
                return Cat({"ってこと", m.group[1]});
            });
        }
        text = ReplaceAll(text, ContractedTeru, [](const Match& m) { return Group1Plus(m, "るん"); });
        text = ReplaceAll(text, ContractedIru, [](const Match& m) {
            const Match g = Rematch(m, ContractedIru);
            if (g.group[2] == "の") return Cat({g.group[1], "いるの"});
            return Cat({g.group[1], "いるん", g.group[2]});
        });
    }
    if (Contains(text, "じゃない") || Contains(text, "じゃね")) {
        // The rebuke needs a verb of the dictionary before んじゃない, not kana that only end like one (the
        // name and honorific of おーしまくんじゃない); a する read from す and a て form of てる are verbs.
        const std::string_view source = text;
        text = ReplaceAll(text, ProhibitionJanai, [&](const Match& m) {
            if (runs.verb != nullptr && m.group[1] != "する") {
                // group 1 is the last kana of the verb, at the start of the match
                const std::string_view verb = source.substr(0, m.begin + m.group[1].size());
                const std::string_view stem = verb.substr(0, m.begin);
                const bool teru = m.group[1] == "る" && (stem.ends_with("て") || stem.ends_with("で") ||
                                                         stem.ends_with("てい") || stem.ends_with("でい"));
                // A verb spelled in katakana is the verb in hiragana (ナメるんじゃねえ なめる).
                const std::string kana = KatakanaToHiragana(verb);
                if (!teru && !runs.verb(runs.context, verb) && (kana == verb || !runs.verb(runs.context, kana))) {
                    return std::string(m.group[0]);
                }
            }
            if (!m.group[3].empty()) return Cat({m.group[1], "な", m.group[3], m.group[2]});
            if (m.group[4].data() != nullptr) {
                std::string paused = Cat({m.group[1], m.group[5].empty() ? "な、" : "なよ、"});
                text::AppendRune(paused, output::kSilentComma);
                return paused;
            }
            return Group1Plus(m, "なよ");
        });
        text = ReplaceAll(text, AppealJanai, [](const Match& m) {
            const Match g = Rematch(m, AppealJanai);
            if (NotAppeal(g.group[1])) return std::string(m.group[0]);
            return Group1Plus(g, "じゃん");
        });
    }
    if (Contains(text, "っ")) {
        text = ReplaceAll(text, ClippedAdjective, [](const Match& m) { return Group1Plus(m, "い"); });
    }
    if (Contains(text, "ん")) {
        // A tail of KansaiNegativeTail after the negative reads with it in dialect forms (知らんがな 知らないよ).
        const auto negative = [&](const Match& m) {
            if (m.group[3].empty()) return Group1Plus(m, "ない");
            if (dialectForms) return Cat({m.group[1], m.group[3]});
            if (m.group[2] == "がな") return Cat({m.group[1], "ないがな"});
            return std::string(m.group[0]);
        };
        text = ReplaceAll(text, NegativeN, negative);
        text = ReplaceAll(text, NegativeE, negative);
        text = ReplaceAll(text, NegativeConjecture, [](const Match& m) { return std::string(m.group[1]); });
    }
    if (dialectForms && Contains(text, "こと")) {
        text = ReplaceAll(text, WhatAThing, [](const Match&) { return std::string("なんてこった"); });
    }
    if (dialectForms && Contains(text, "ん")) {
        text = ReplaceAll(text, QuestionN, [](const Match& m) { return Cat({m.group[1], "の", m.group[2]}); });
    }
    if (dialectForms && Contains(text, "わ")) {
        text = ReplaceAll(text, TopicWa, [](const Match& m) { return Cat({m.group[1], "は"}); });
    }
    if (dialectForms && ContainsAny(text, U"えぇーェエ")) {
        text = ReplaceAll(text, SlangAdjectivePattern, [](const Match& m) { return std::string(m.group[1]); });
    }
    if (exclamationNa && Contains(text, "な")) text = ReplaceAll(text, ExclamationNa, ReplaceExclamationNa);
    return ReplaceAll(text, OldNegative, [](const Match& m) { return Cat({m.group[1], "ない", m.group[2]}); });
}

}  // namespace hdTrans::pipeline::internal
