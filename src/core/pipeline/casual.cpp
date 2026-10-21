#include "core/pipeline/normalize.h"
#include "core/pipeline/pattern.h"

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

// (て|で|[一-龥々][かがさたなばまらわ]|[見来出居寝似着]|し|(?:わか|分か|判|知|つま|たま)ん)ね[ぇえ]([なよぜぞしんかけだっ]|$)
// or (やら|[くにゃばて]なら)ね[ぇえー]([なよぜぞしんかけだっ]|$), or before a kanji or a sentence
// end or a pause; not the particle やら after a kanji, なに or どう, nor the conditional なら
// after a noun (君ならねぇ) or before a pause where PausedConditional holds (行くならねぇ、,
// 田中にならねぇ、).
bool Nee(std::string_view s, size_t pos, Match& m) {
    auto tail = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "ね") || !OneOf(s, p, U"ぇえ")) return false;
        return CharOrEnd(s, p, U"なよぜぞしんかけだっ", m, 2);
    };
    size_t p = pos;
    if (Lit(s, p, "て") && tail(p)) return true;
    p = pos;
    if (Lit(s, p, "で") && tail(p)) return true;
    p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"かがさたなばまらわ") && tail(p)) return true;
    p = pos;
    if (OneOf(s, p, U"見来出居寝似着") && tail(p)) return true;
    p = pos;
    if (Lit(s, p, "し") && tail(p)) return true;
    for (std::string_view w : {"わか", "分か", "判", "知", "つま", "たま"}) {
        p = pos;
        if (Lit(s, p, w) && Lit(s, p, "ん") && tail(p)) return true;
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

// (っ|し)ちま([うっえいわお])|んじま([うっえいわお])
bool Chimau(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (OneOf(s, p, U"っし")) {
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

// っつ[ーう]
bool Tsuu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "っつ") || !OneOf(s, p, U"ーう")) return false;
    m.end = p;
    return true;
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

// ([えけげてねへべめれすく])んな([よぁー]|[っッ]?[！!、。？?…‥」]|$)
bool Nna(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"えけげてねへべめれすく")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "んな")) return false;
    size_t q = p;
    if (OneOf(s, q, U"よぁー")) return Done(s, m, p, q, 2);
    const bool ok = Repeat(s, p, InSet(U"っッ"), 0, 1, [&](size_t r) {
        size_t t = r;
        if (!OneOf(s, t, U"！!、。？?…‥」")) return false;
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

// ([るたなういくすつぬむぶぐてだ])ん[すス]([かけよねわ」』！？!?。…‥ー〜～ぉぇ、]|$): the spoken
// んです (行くんすか, 苦手なんスけど), but not ないんすか, ないんすよ.
bool Nsu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"るたなういくすつぬむぶぐてだ")) return false;
    // なんすか starting a word is 何すか (なんすか？ 뭔데요), its own phrase.
    if (Span(s, pos, p) == "な") {
        size_t before = pos;
        while (before > 0 && (static_cast<unsigned char>(s[before - 1]) & 0xC0) == 0x80) before--;
        if (before > 0) before--;
        const char32_t c = pos == 0 ? 0 : text::DecodeRune(s.substr(before)).cp;
        if (pos == 0 || !(IsHiraganaClass(c) || IsKanjiClass(c))) return false;
    }
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ん") || !OneOf(s, p, U"すス")) return false;
    // ないんすか and ないんすよ have their own end rows (書かないんすか 쓰지 않나요).
    size_t q = p;
    if (m.group[1] == "い" && s.substr(0, pos).ends_with("な") && OneOf(s, q, U"かよ")) return false;
    return CharOrEnd(s, p, U"かけよねわ」』！？!?。…‥ー〜～ぉぇ、", m, 2);
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

// ([いきぎしじちにひびみりえけげせぜねへべめれ])てぇか
bool Teeka(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"いきぎしじちにひびみりえけげせぜねへべめれ")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "てぇか")) return false;
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

// ([てで])く([。！？…‥、!?よねぞわかの」]|$)
bool Teku(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"てで")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "く")) return false;
    return CharOrEnd(s, p, U"。！？…‥、!?よねぞわかの」", m, 2);
}

// ([ん一-龥々])じゃね[ぇえー]+
bool Janee(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Take(s, p, [](char32_t c) { return c == U'ん' || IsKanjiClass(c); })) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "じゃね")) return false;
    return Repeat(s, p, InSet(U"ぇえー"), 1, kUnbounded, [&](size_t q) {
        m.end = q;
        return true;
    });
}

// ([一-龥々](?:[っしいえけげせぜてねべめれん])?|もらっ|まかし|^し|[^ぁ-ゖ]し)(と|ど)
// (く|こう|こ|いて|いた|きな|き|け)([^ぁ-ゖ]|[よねぞわなも]|あげ|くれ|くだ|$)
bool Toku(std::string_view s, size_t pos, Match& m) {
    auto after = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        size_t q = p;
        if (!OneOf(s, q, U"とど")) return false;
        m.group[2] = Span(s, p, q);
        for (std::string_view w : {"く", "こう", "こ", "いて", "いた", "きな", "き", "け"}) {
            size_t r = q;
            if (!Lit(s, r, w)) continue;
            m.group[3] = Span(s, q, r);
            size_t t = r;
            if (Take(s, t, [](char32_t c) { return !IsHiraganaClass(c); })) return Done(s, m, r, t, 4);
            t = r;
            if (OneOf(s, t, U"よねぞわなも")) return Done(s, m, r, t, 4);
            for (std::string_view e : {"あげ", "くれ", "くだ"}) {
                t = r;
                if (Lit(s, t, e)) return Done(s, m, r, t, 4);
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
    for (std::string_view w : {"もらっ", "まかし"}) {
        p = pos;
        if (Lit(s, p, w) && after(p)) return true;
    }
    p = pos;
    if (pos == 0 && Lit(s, p, "し") && after(p)) return true;
    p = pos;
    return Take(s, p, [](char32_t c) { return !IsHiraganaClass(c); }) && Lit(s, p, "し") && after(p);
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
    if (w == "きな") return "おきな";
    return "おけ";  // き, け
}

// s ends with a kanji other than 見来寝着出居 (all kanji are three bytes long).
bool EndsWithStemKanji(std::string_view s) {
    if (s.size() < 3) return false;
    const text::Rune last = text::DecodeRune(s.substr(s.size() - 3));
    return last.size == 3 && IsKanji(last.cp) && !In(last.cp, U"見来寝着出居");
}

}  // namespace

std::string RewriteCasual(std::string_view in) {
    std::string text = ReplaceAll(in, Shiren, [](const Match& m) { return Cat({"かも", m.group[1], "ない"}); });
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
    text = ReplaceAll(text, Chimau, [](const Match& m) {
        const Match g = Rematch(m, Chimau);
        if (!g.group[1].empty()) return Cat({g.group[1], "ちゃ", g.group[2]});
        return Cat({"んじゃ", g.group[3]});
    });
    text = ReplaceAll(text, Tsuu, [](const Match&) { return std::string("という"); });
    text = ReplaceAll(text, NaTsu, [](const Match& m) { return Cat({m.group[1], "な", m.group[2]}); });
    text = ReplaceAll(text, Nna, [](const Match& m) { return Cat({m.group[1], "るな", m.group[2]}); });
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
    text = ReplaceAll(text, Literal<kNaiNsuKatakana>, [](const Match&) { return std::string("ないんす"); });
    text = ReplaceAll(text, Nsu, [](const Match& m) { return Cat({m.group[1], "んです", m.group[2]}); });
    text = ReplaceAll(text, Ikka, [](const Match& m) { return Cat({"いいか", m.group[1]}); });
    text = ReplaceAll(text, Tondemo, [](const Match&) { return std::string("とんでもない"); });
    text = ReplaceAll(text, Mashou, [](const Match& m) { return Cat({m.group[1], "しょう"}); });
    text = ReplaceAll(text, UtoShite, [](const Match& m) { return Cat({"うとしてい", m.group[1]}); });
    text = ReplaceAll(text, Teku, [](const Match& m) { return Cat({m.group[1], "いく", m.group[2]}); });
    text = ReplaceAll(text, Janee, [](const Match& m) { return Cat({m.group[1], "じゃない"}); });
    text = ReplaceAll(text, Toku, [](const Match& m) {
        const Match g = Rematch(m, Toku);
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
