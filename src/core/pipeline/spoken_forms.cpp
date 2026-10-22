#include "core/pipeline/normalize.h"
#include "core/pipeline/pattern.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/spoken_forms_gen.h"

namespace hdTrans::pipeline::internal {

namespace {

// Group 1 a stem of kKataStems in their order, group 2 its ending.
bool KataStemPattern(std::string_view s, size_t pos, Match& m) {
    for (const KataStem& k : kKataStems) {
        size_t p = pos;
        if (!Lit(s, p, k.kata)) continue;
        for (std::string_view e : kKataEndings) {
            size_t q = p;
            if (!Lit(s, q, e)) continue;
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
template <size_t N>
bool CharLitWord(std::string_view s, size_t pos, Match& m, std::u32string_view first, std::string_view lit,
                 const std::string_view (&words)[N]) {
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

constexpr std::string_view kTeruAfter[] = {"じゃ", "だろ"};
constexpr std::string_view kIruAfter[] = {"の", "だ", "じゃ"};

// ([てで])ん(じゃ|だろ)
bool ContractedTeru(std::string_view s, size_t pos, Match& m) {
    return CharLitWord(s, pos, m, U"てで", "ん", kTeruAfter);
}

// ([にでもがは])いん(の|だ|じゃ)
bool ContractedIru(std::string_view s, size_t pos, Match& m) {
    return CharLitWord(s, pos, m, U"にでもがは", "いん", kIruAfter);
}

// One rune of set or the end of s at p as group 2, ending the match.
bool EndGroup(std::string_view s, size_t p, std::u32string_view set, Match& m) {
    size_t q = p;
    if (!OneOf(s, q, set) && p != s.size()) return false;
    m.group[2] = Span(s, p, q);
    m.end = q;
    return true;
}

// ([うくぐすつぬぶむる])んじゃない([！!」』]|$)
bool ProhibitionJanai(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"うくぐすつぬぶむる")) return false;
    m.group[1] = Span(s, pos, p);
    return Lit(s, p, "んじゃない") && EndGroup(s, p, U"！!」』", m);
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

// (怖|寒|…|こわ)っ+([！!？?。、…‥」』]|$)
bool ClippedAdjective(std::string_view s, size_t pos, Match& m) {
    for (std::string_view w : kClippedAdjectives) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        m.group[1] = Span(s, pos, p);
        const bool ok = Repeat(s, p, InSet(U"っ"), 1, kUnbounded,
                               [&](size_t q) { return EndGroup(s, q, U"！!？?。、…‥」』", m); });
        if (ok) return true;
    }
    return false;
}

constexpr std::u32string_view kNegativeEnd = U"！!？?。、…‥」』ぞよわがしけっ";

// ([一-龥々][らわかがまば]|許さ|わから|しら|かまわ)ん([！!？?。、…‥」』ぞよわがしけっ]|$)
bool NegativeN(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        return Lit(s, p, "ん") && EndGroup(s, p, kNegativeEnd, m);
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"らわかがまば") && rest(p)) return true;
    for (std::string_view w : kNegativeNStems) {
        p = pos;
        if (Lit(s, p, w) && rest(p)) return true;
    }
    return false;
}

// (でき|得|足り|られ|させ|言え|見え|聞こえ)ん([！!？?。、…‥」』ぞよわがしけっ]|$)
bool NegativeE(std::string_view s, size_t pos, Match& m) {
    for (std::string_view w : kNegativeEStems) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        m.group[1] = Span(s, pos, p);
        if (Lit(s, p, "ん") && EndGroup(s, p, kNegativeEnd, m)) return true;
    }
    return false;
}

constexpr char kDekinu[] = "できぬ";

bool Dekinu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, kDekinu)) return false;
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
    for (std::string_view stem : kNegativeNeeStems) {
        p = pos;
        if (Lit(s, p, stem) && rest(p)) return true;
    }
    for (std::string_view stem : kNegativeNeeKu) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "く") && rest(p)) return true;
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
        if (Lit(s, p, "かった")) m.group[2] = Span(s, ending, p);
        else m.group[2] = {};
        m.end = p;
        return true;
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"わかがさたなまば") && rest(p)) return true;
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

std::string ReplaceKansaiNegative(const Match& m) {
    const Match g = Rematch(m, KansaiNegative);
    std::string_view stem = g.group[1];
    if (stem == "あら" || stem == "あれ") stem = {};
    else if (stem == "せえ") stem = "し";
    return Cat({stem, g.group[2].empty() ? "ない" : "なかった"});
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
    return Lit(s, p, "んど") && rest(p);
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
            if (!OneOf(s, r, U"！!？?。…‥」』）") && q != s.size()) return false;
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
    for (std::string_view ending : kExclamationNaExcludedEndings) {
        if (g.group[1].ends_with(ending)) return std::string(m.group[0]);
    }
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

// The pronouns after which わ is the topic は spelled as it sounds (こっちわ, お前わ).
constexpr std::string_view kTopicWaWords[] = {
    "こっち", "そっち", "あっち", "どっち", "こちら", "そちら", "あちら", "お前", "おまえ", "オマエ", "てめえ",
    "あんた", "アンタ", "あなた", "貴方", "君", "きみ", "キミ", "私", "わたし", "あたし", "アタシ", "俺", "おれ",
    "オレ", "僕", "ぼく", "ボク", "これ", "それ", "あれ", "今日", "明日", "昨日", "みんな", "皆",
};

// (こっち|お前|…)わ before a mark, a space, kanji, katakana or the end: group 1 the
// pronoun (こっちわ大丈夫 こっちは). わ before hiragana stays (お前わかってる).
bool TopicWa(std::string_view s, size_t pos, Match& m) {
    if (pos > 0) {
        const char32_t before = LastRune(s.substr(0, pos)).cp;
        if (IsHiraganaClass(before) || IsKanjiClass(before) || (before >= U'ァ' && before <= U'ヺ') || before == U'ー') {
            return false;
        }
    }
    for (std::string_view w : kTopicWaWords) {
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

std::string Group1Plus(const Match& m, std::string_view mid) { return Cat({m.group[1], mid, m.group[2]}); }

}  // namespace

std::string SpokenForms(std::string_view in, bool exclamationNa, bool dialectForms) {
    std::string text(in);
    if (Contains(text, "かしら")) text = ReplaceAll(text, KashiraNe, [](const Match&) { return std::string("かしら"); });
    if (ContainsAny(text, kKataStemStarts)) {
        text = ReplaceAll(text, KataStemPattern, [](const Match& m) {
            const Match g = Rematch(m, KataStemPattern);
            std::string_view hira;
            for (const KataStem& k : kKataStems) {
                if (k.kata == g.group[1]) hira = k.hira;
            }
            return Cat({hira, g.group[2]});
        });
    }
    if (Contains(text, "ね")) {
        text = ReplaceAll(text, JaneQuestion, [](const Match& m) { return Group1Plus(m, "ない"); });
        text = ReplaceAll(text, ParticleNee, [](const Match& m) { return Group1Plus(m, "ない"); });
        if (dialectForms) text = ReplaceAll(text, NegativeNee, ReplaceNegativeNee);
    }
    if (dialectForms && Contains(text, "へん")) text = ReplaceAll(text, KansaiNegative, ReplaceKansaiNegative);
    if (dialectForms && (Contains(text, "と") || Contains(text, "ど")))
        text = ReplaceAll(text, ContractedToru, ReplaceContractedToru);
    if (Contains(text, "ん")) {
        text = ReplaceAll(text, ContractedTeru, [](const Match& m) { return Group1Plus(m, "るん"); });
        text = ReplaceAll(text, ContractedIru, [](const Match& m) {
            const Match g = Rematch(m, ContractedIru);
            if (g.group[2] == "の") return Cat({g.group[1], "いるの"});
            return Cat({g.group[1], "いるん", g.group[2]});
        });
    }
    if (Contains(text, "じゃない")) {
        text = ReplaceAll(text, ProhibitionJanai, [](const Match& m) { return Group1Plus(m, "なよ"); });
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
        text = ReplaceAll(text, NegativeN, [](const Match& m) { return Group1Plus(m, "ない"); });
        text = ReplaceAll(text, NegativeE, [](const Match& m) { return Group1Plus(m, "ない"); });
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
    return ReplaceAll(text, Dekinu, [](const Match&) { return std::string("できない"); });
}

}  // namespace hdTrans::pipeline::internal
