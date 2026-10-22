// The rewrites of Normalize, in order:
//   0. characters of the private use area escaped (EscapePrivateUse);
//   1. on a document line, the kin terms for the speaker's sex (WriteKinTerms), then
//      ruby markup removed (StripRuby);
//   2. character forms: half-width katakana, CP932 mapping differences, kanji forms
//      (NormalizeForms);
//   3. an apology or greeting cut before なさい joined (JoinNasai);
//   4. a polite ending cut off or inside joined (JoinMasu);
//   5. spoken and old-fashioned spellings (SpokenForms);
//   6. あれ, え and じゃあ at the start of a sentence (RewriteInterjections);
//   7. casual contractions (RewriteCasual);
//   8. the military time in digits (RewriteMilitaryTime);
//   9. a word cut from the copula by a pause joined (JoinPausedCopula);
//  10. a doubled を after a word written once (CollapseDoubledWo).
// Each step reads the output of the step before.
#include "core/pipeline/normalize.h"

#include "core/pipeline/pattern.h"
#include "core/pipeline/restore.h"
#include "core/pipeline/runes.h"
#include "core/tokenizer/canonical.h"

namespace hdTrans::pipeline {

using namespace internal;

std::string Normalize(std::string_view line, const LineContext& ctx) {
    std::string text = EscapePrivateUse(line);
    if (ctx.document) text = StripRuby(WriteKinTerms(text, ctx.female));
    text = JoinNasai(NormalizeForms(text));
    text = RewriteCasual(RewriteInterjections(SpokenForms(JoinMasu(text), ctx.exclamationNa, ctx.dialectForms)));
    return CollapseDoubledWo(JoinPausedCopula(RewriteMilitaryTime(text)));
}

namespace internal {

namespace {

struct CharMap {
    char32_t from;
    char32_t to;
};

// Characters mapped to the form CP932 encodes for them.
constexpr CharMap kCp932Forms[] = {
    {U'〜', U'～'}, {U'−', U'－'}, {U'‖', U'∥'}, {U'¢', U'￠'}, {U'£', U'￡'}, {U'¬', U'￢'},
};

// Kanji forms outside JIS X 0208 (the JIS X 0213 forms of web text) with the form the
// dictionary holds.
constexpr CharMap kKanjiForms[] = {
    {U'頰', U'頬'}, {U'閒', U'間'}, {U'摑', U'掴'}, {U'𠮟', U'叱'}, {U'剝', U'剥'}, {U'繫', U'繋'}, {U'噓', U'嘘'},
    {U'搔', U'掻'}, {U'鷗', U'鴎'}, {U'屛', U'屏'}, {U'餠', U'餅'}, {U'塡', U'填'}, {U'醬', U'醤'},
    {U'蔣', U'蒋'}, {U'﨑', U'崎'}, {U'髙', U'高'}, {U'德', U'徳'}, {U'淸', U'清'}, {U'靑', U'青'},
    {U'瀨', U'瀬'}, {U'吞', U'呑'}, {U'揭', U'掲'}, {U'涉', U'渉'}, {U'焰', U'焔'}, {U'狀', U'状'},
    {U'卽', U'即'}, {U'蟬', U'蝉'},
};

char32_t MapForm(char32_t c) {
    for (const CharMap& m : kCp932Forms) {
        if (m.from == c) return m.to;
    }
    for (const CharMap& m : kKanjiForms) {
        if (m.from == c) return m.to;
    }
    return c;
}

// (ごめん|ゴメン|おやすみ|おかえり|お帰り)[、，…‥・]+(なさい)
bool SplitNasai(std::string_view s, size_t pos, Match& m) {
    for (std::string_view w : {"ごめん", "ゴメン", "おやすみ", "おかえり", "お帰り"}) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        const bool ok = Repeat(s, p, InSet(U"、，…‥・"), 1, kUnbounded, [&](size_t q) {
            size_t r = q;
            if (!Lit(s, r, "なさい")) return false;
            m.group[1] = Span(s, pos, p);
            m.group[2] = Span(s, q, r);
            m.end = r;
            return true;
        });
        if (ok) return true;
    }
    return false;
}

// Whether marks is a run of pause marks a drawl span carries: …‥―─ and runs of two or
// more ・ (one ・ separates words).
bool DrawlMarks(std::string_view marks) {
    if (marks.empty()) return false;
    size_t dots = 0;
    for (size_t i = 0; i < marks.size();) {
        const text::Rune r = At(marks, i);
        if (!In(r.cp, U"…‥・―─")) return false;
        if (r.cp != U'・' && dots == 1) return false;
        dots = r.cp == U'・' ? dots + 1 : 0;
        i += r.size;
    }
    return dots != 1;
}

// The end of a polite ending that ends at r: with particles an optional かね よね わね わよ
// か ね よ わ, then ([^ぁ-ゖァ-ヺー～]|$). e is set after the particles, t after the
// character that follows.
bool EndingEnd(std::string_view s, size_t r, bool particles, size_t& e, size_t& t) {
    e = r;
    if (particles) {
        for (std::string_view w : {"かね", "よね", "わね", "わよ", "か", "ね", "よ", "わ"}) {
            if (Lit(s, e, w)) break;
        }
    }
    t = e;
    const bool tail = Take(s, t, [](char32_t c) {
        return !IsHiraganaClass(c) && !(c >= U'ァ' && c <= U'ヺ') && c != U'ー' && c != U'～';
    });
    return tail || e == s.size();
}

// ([いきぎしじちにひびみりえけげせぜてでねへべめれ])([、，…‥]+)(ます|ました|ません|ましょう)(P?)([^ぁ-ゖァ-ヺー～]|$)
// as groups 1, 4, 2, 5 and 3, with P the particles of EndingEnd after marks of DrawlMarks.
bool SplitMasu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"いきぎしじちにひびみりえけげせぜてでねへべめれ")) return false;
    return Repeat(s, p, InSet(U"、，…‥"), 1, kUnbounded, [&](size_t q) {
        for (std::string_view w : {"ます", "ました", "ません", "ましょう"}) {
            size_t r = q, e = 0, t = 0;
            if (!Lit(s, r, w) || !EndingEnd(s, r, DrawlMarks(Span(s, p, q)), e, t)) continue;
            m.group[1] = Span(s, pos, p);
            m.group[2] = Span(s, q, r);
            m.group[3] = Span(s, e, t);
            m.group[4] = Span(s, p, q);
            m.group[5] = Span(s, r, e);
            m.end = t;
            return true;
        }
        return false;
    });
}

// A polite ending cut inside or spelled with っ or ス: a head, a run of marks, an
// optional っ and a tail, then ([^ぁ-ゖァ-ヺー～]|$). The heads and tails are
//   ([いきぎしじちにひびみりえけげせぜてでねへべめれ一-龥々]ま) (す|ス|した|せん|しょう)
//   (で) (す|ス|しょう)
// with marks [、，…‥・―─ー～]*; した, せん and しょう only after a mark other than ー ～
// and without っ, and す only after such a mark or っ (ー ～ alone are left as they are).
// After marks of DrawlMarks the particles of EndingEnd may follow the tail. Not for the
// word いま or 今ま (今ま…す、すぐ), nor before 、 or ， and a kanji or the tail's first kana,
// a restarted word (ここで…す、すみません). Group 1 is the head, group 2 the tail (ス as す),
// group 3 the character after, group 4 the marks, group 5 the particles.
bool SplitCutPolite(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    bool masu = false;
    if (OneOf(s, p, U"いきぎしじちにひびみりえけげせぜてでねへべめれ") || Take(s, p, IsKanjiClass)) masu = Lit(s, p, "ま");
    if (masu && s.substr(pos).starts_with("今")) return false;
    if (masu && s.substr(pos).starts_with("い")) {
        const char32_t prev = LastRune(s.substr(0, pos)).cp;
        if (!IsHiraganaClass(prev) && !IsKanjiClass(prev) && !(prev >= U'ァ' && prev <= U'ヺ')) return false;
    }
    if (!masu) {
        p = pos;
        if (!Lit(s, p, "で")) return false;
    }
    const size_t cut = p;
    bool marked = false;
    while (true) {
        const text::Rune r = At(s, p);
        if (r.size == 0 || !In(r.cp, U"、，…‥・―─ー～")) break;
        if (r.cp != U'ー' && r.cp != U'～') marked = true;
        p += r.size;
    }
    const size_t marks = p;
    const bool tsu = OneOf(s, p, U"っッ");
    const bool particles = !tsu && DrawlMarks(Span(s, cut, marks));
    const std::initializer_list<std::string_view> masuTails = {"す", "ス", "した", "せん", "しょう"};
    const std::initializer_list<std::string_view> desuTails = {"す", "ス", "しょう"};
    for (std::string_view w : masu ? masuTails : desuTails) {
        size_t r = p, e = 0, t = 0;
        if (!Lit(s, r, w)) continue;
        const bool su = w == "す" || w == "ス";
        if (w == "す" ? !marked && !tsu : !su && (!marked || tsu)) continue;
        if (!EndingEnd(s, r, particles, e, t)) continue;
        if (const text::Rune comma = At(s, e); comma.size > 0 && (comma.cp == U'、' || comma.cp == U'，')) {
            const text::Rune next = At(s, t);
            const std::string_view first = su ? std::string_view("す") : w.substr(0, std::string_view("し").size());
            if (next.size > 0 && (IsKanjiClass(next.cp) || s.substr(t).starts_with(first))) continue;
        }
        m.group[1] = Span(s, pos, cut);
        m.group[2] = su ? std::string_view("す") : Span(s, p, r);
        m.group[3] = Span(s, e, t);
        m.group[4] = Span(s, cut, marks);
        m.group[5] = Span(s, r, e);
        m.end = t;
        return true;
    }
    return false;
}

// Whether the kana run is a predicate by itself: an adjective in い other than はい, a run
// ending in そう どう こう ありがとう おめでとう よろしく だめ, or one of those with の or ん,
// or なの なん (そうなの).
bool PredicateKana(std::string_view run) {
    for (std::string_view w : {"なの", "なん", "の", "ん"}) {
        if (run.ends_with(w) && run.size() > w.size()) return PredicateKana(run.substr(0, run.size() - w.size()));
    }
    if (run.ends_with("い")) return run != "はい";
    for (std::string_view w : {"そう", "どう", "こう", "ありがとう", "おめでとう", "よろしく", "だめ"}) {
        if (run.ends_with(w)) return true;
    }
    return false;
}

// Whether the text before ends in a word that can take a pause before the copula: a kanji,
// a katakana (or ー after one), or kana after such a word (嬉しい, 好き, 静か, 沈むん)
// that are no particle (は も に へ と で が を や ね よ から まで より って けど, の right
// after the kanji) nor a request form (ください, なさい); or kana alone that PredicateKana
// takes. Fillers and conjunctions (えっと, つまり, だから, うーん) are none of these.
bool PredicateBefore(std::string_view before) {
    const text::Rune last = LastRune(before);
    if (IsKanjiClass(last.cp) || (last.cp >= U'ァ' && last.cp <= U'ヺ')) return true;
    if (last.cp == U'ー') {
        const text::Rune prev = LastRune(before.substr(0, before.size() - last.size));
        return prev.cp >= U'ァ' && prev.cp <= U'ヺ';
    }
    if (!IsHiraganaClass(last.cp)) return false;
    size_t start = before.size();
    while (start > 0) {
        const text::Rune r = LastRune(before.substr(0, start));
        if (!IsHiraganaClass(r.cp)) break;
        start -= r.size;
    }
    const std::string_view run = before.substr(start);
    for (std::string_view w : {"は", "も", "に", "へ", "と", "で", "が", "を", "や", "ね", "よ", "から",
                               "まで", "より", "って", "けど", "ください", "なさい"}) {
        if (run.ends_with(w)) return false;
    }
    if (start > 0 && PredicateBefore(before.substr(0, start))) return run != "の";
    return PredicateKana(run);
}

// A word cut from the copula by a pause: ([ぁ-ゖァ-ヺー一-龥々])([…‥・―─]+)(です|でした|でしょう)
// (P?)([^ぁ-ゖァ-ヺー～]|$) as groups 1, 4, 2, 5 and 3, with P the particles of EndingEnd,
// when PredicateBefore holds for the text up to the marks.
bool SplitPausedCopula(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    const auto wordChar = [](char32_t c) {
        return IsHiraganaClass(c) || (c >= U'ァ' && c <= U'ヺ') || c == U'ー' || IsKanjiClass(c);
    };
    if (!Take(s, p, wordChar)) return false;
    const size_t word = p;
    if (!PredicateBefore(s.substr(0, word))) return false;
    while (OneOf(s, p, U"…‥・―─")) continue;
    if (!DrawlMarks(Span(s, word, p))) return false;
    for (std::string_view w : {"です", "でした", "でしょう"}) {
        size_t r = p, e = 0, t = 0;
        if (!Lit(s, r, w) || !EndingEnd(s, r, true, e, t)) continue;
        m.group[1] = Span(s, pos, word);
        m.group[2] = Span(s, p, r);
        m.group[3] = Span(s, e, t);
        m.group[4] = Span(s, word, p);
        m.group[5] = Span(s, r, e);
        m.end = t;
        return true;
    }
    return false;
}

// marks as a drawl span (mark, marks, mark) when DrawlMarks holds; nothing for other
// marks (、 ー).
std::string DrawlSpan(std::string_view marks, char32_t mark) {
    if (!DrawlMarks(marks)) return {};
    std::string out;
    text::AppendRune(out, mark);
    out += marks;
    text::AppendRune(out, mark);
    return out;
}

// The character before pos, U+0000 at the start of s.
char32_t Before(std::string_view s, size_t pos) {
    if (pos == 0) return 0;
    size_t q = pos - 1;
    while (q > 0 && (static_cast<unsigned char>(s[q]) & 0xC0) == 0x80) q--;
    return At(s, q).cp;
}

// (?<=^|[\n「『（。！？!?])([ 　―…‥]*) as group 2 (group 1 empty), then rest(end of group 2).
// The character before is not taken, so a sentence that starts right after a match
// starts its own (え？え？).
template <class Rest>
bool SentenceStart(std::string_view s, size_t pos, Match& m, Rest rest) {
    if (pos != 0 && !In(Before(s, pos), U"\n「『（。！？!?")) return false;
    size_t p = pos;
    return Repeat(s, p, InSet(U" 　―…‥"), 0, kUnbounded, [&](size_t q) {
        m.group[1] = Span(s, pos, pos);
        m.group[2] = Span(s, p, q);
        return rest(q);
    });
}

// start (あれ[～ー]*|え)([、，！？…‥―─—]); not あれ when a run of …‥―─— ends before a
// particle (あれ――は何だ, the pronoun).
bool StartInterjection(std::string_view s, size_t pos, Match& m) {
    return SentenceStart(s, pos, m, [&](size_t q) {
        for (std::string_view w : {"あれ", "え"}) {
            size_t r = q;
            if (!Lit(s, r, w)) continue;
            size_t t = r;
            if (w == "あれ") {
                while (OneOf(s, t, U"～ー")) continue;  // あれ～？
            }
            if (!OneOf(s, t, U"、，！？…‥―─—")) continue;
            size_t after = t;
            while (OneOf(s, after, U"…‥―─—")) continue;
            if (w == "あれ" && In(At(s, r).cp, U"…‥―─—") && OneOf(s, after, U"はがをもにのでと")) continue;
            m.group[3] = Span(s, q, r);
            m.group[4] = Span(s, r, t);
            m.end = t;
            return true;
        }
        return false;
    });
}

// start え[ぇえー]+([…‥]*[？?])
bool StartEe(std::string_view s, size_t pos, Match& m) {
    return SentenceStart(s, pos, m, [&](size_t q) {
        size_t r = q;
        if (!Lit(s, r, "え")) return false;
        return Repeat(s, r, InSet(U"ぇえー"), 1, kUnbounded, [&](size_t t) {
            return Repeat(s, t, InSet(U"…‥"), 0, kUnbounded, [&](size_t u) {
                size_t v = u;
                if (!OneOf(s, v, U"？?")) return false;
                m.group[3] = Span(s, t, v);
                m.end = v;
                return true;
            });
        });
    });
}

// […‥―─—] え[ぇえー]* ([…‥]*[？?]): え after a pause inside an utterance that asks
// (へ……え？, ……て、…え？) as group 1 and the question as group 2.
bool PauseEe(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"…‥―─—")) return false;
    const size_t at = p;
    if (!Lit(s, p, "え")) return false;
    return Repeat(s, p, InSet(U"ぇえー"), 0, kUnbounded, [&](size_t t) {
        return Repeat(s, t, InSet(U"…‥"), 0, kUnbounded, [&](size_t u) {
            size_t v = u;
            if (!OneOf(s, v, U"？?")) return false;
            m.group[1] = Span(s, pos, at);
            m.group[2] = Span(s, t, v);
            m.end = v;
            return true;
        });
    });
}

// start じゃあ([^、，！？…ーねなぁっ。」』）\n])
bool StartJaa(std::string_view s, size_t pos, Match& m) {
    return SentenceStart(s, pos, m, [&](size_t q) {
        size_t r = q;
        if (!Lit(s, r, "じゃあ")) return false;
        size_t t = r;
        if (!NoneOf(s, t, U"、，！？…ーねなぁっ。」』）\n")) return false;
        m.group[3] = Span(s, r, t);
        m.end = t;
        return true;
    });
}

constexpr std::string_view kMilitaryWords[] = {"マル", "ヒト", "フタ", "ニー", "サン", "ヨン", "ゴー",
                                               "ゴ",   "ロク", "ナナ", "ハチ", "キュー", "キュウ"};
constexpr std::string_view kMilitaryKana[] = {"まる", "ひと", "ふた", "さん", "よん",
                                              "ごー", "ろく", "なな", "はち", "きゅう"};

// The digit words in the order the digits are read back, longest first.
struct MilitaryDigit {
    std::string_view word;
    char digit;
};
constexpr MilitaryDigit kMilitaryDigits[] = {
    {"キュウ", '9'}, {"キュ", '9'}, {"マル", '0'}, {"ヒト", '1'}, {"フタ", '2'}, {"ニ", '2'}, {"サン", '3'},
    {"ヨン", '4'},   {"ゴ", '5'},   {"ロク", '6'}, {"ナナ", '7'}, {"ハチ", '8'},
};

template <size_t N>
bool AnyWord(std::string_view s, size_t& pos, const std::string_view (&words)[N]) {
    for (std::string_view w : words) {
        if (Lit(s, pos, w)) return true;
    }
    return false;
}

// W([、・ ー]?W){3} with W a katakana digit word, or four hiragana digit words. A word
// never begins with a separator or ー, so the first word that fits is the only one
// that lets the rest match.
bool MilitaryTime(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (AnyWord(s, p, kMilitaryWords)) {
        int n = 1;
        for (; n < 4; n++) {
            size_t q = p;
            OneOf(s, q, U"、・ ー");  // ヒトマルーマルマル draws a word out
            if (!AnyWord(s, q, kMilitaryWords)) break;
            p = q;
        }
        if (n == 4) {
            m.end = p;
            return true;
        }
    }
    p = pos;
    for (int n = 0; n < 4; n++) {
        if (!AnyWord(s, p, kMilitaryKana)) return false;
    }
    m.end = p;
    return true;
}

bool IsKanaOrMark(char32_t c) {
    return (c >= U'ぁ' && c <= U'ゖ') || (c >= U'ァ' && c <= U'ヶ') || c == U'ー' || c == U'～';
}

// The end of a katakana time of day (hour 00 to 29 read as マル, ヒト or フタ first, not
// マルマルマルマル) before hiragana, or after a run of ー～ and small ゥッ that the time takes
// in, before anything but katakana: ヒトサンマルマルです, フタサンマルマルー！. 0 otherwise.
size_t TimeEnd(std::string_view text, const Match& m) {
    const std::string_view w = m.group[0];
    if (!w.starts_with("マル") && !w.starts_with("ヒト") && !w.starts_with("フタ")) return 0;
    if (w == "マルマルマルマル") return 0;
    size_t p = m.end;
    while (p < text.size() && In(At(text, p).cp, U"ー～ゥッ")) p += At(text, p).size;
    const char32_t c = p < text.size() ? At(text, p).cp : 0;
    if (p == m.end) return c >= U'ぁ' && c <= U'ゖ' ? p : 0;
    return (c >= U'ァ' && c <= U'ヶ') || (c >= U'一' && c <= U'鿿') ? 0 : p;
}

}  // namespace

std::string NormalizeForms(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const text::Rune r = At(text, i);
        i += r.size;
        if (r.cp == text::kReplacement && r.size == 1) {
            out.push_back(text[i - 1]);
            continue;
        }
        char32_t c = r.cp;
        if (c >= 0xFF61 && c <= 0xFF9F && c != U'｢' && c != U'｣') {
            c = tokenizer::WidenHalfWidth(c);
            const text::Rune mark = At(text, i);
            if (mark.size > 0 && mark.cp == U'ﾞ' && In(c, U"カキクケコサシスセソタチツテトハヒフヘホ")) {
                c++;
                i += mark.size;
            } else if (mark.size > 0 && mark.cp == U'ﾞ' && c == U'ウ') {
                c = U'ヴ';
                i += mark.size;
            } else if (mark.size > 0 && mark.cp == U'ﾟ' && In(c, U"ハヒフヘホ")) {
                c += 2;
                i += mark.size;
            }
        } else if (c == U'〆' && (i == text.size() || In(At(text, i).cp, U"のはをがにでも」』、。！？…"))) {
            out += "締め";  // 〆の一杯, 〆はラーメン: the noun 締め (the finish)
            continue;
        } else if (c == U'〆' && i < text.size() &&
                   (In(At(text, i).cp, U"切めるり") || (At(text, i).cp >= U'一' && At(text, i).cp <= U'鿿'))) {
            c = U'締';  // 〆切, 〆る, 〆日: the kanji 締 written short
        } else if (c == U'〆' && i < text.size() && At(text, i).cp >= U'ぁ' && At(text, i).cp <= U'ゖ') {
            out += "しめ";  // 〆た: the kana しめ written short
            continue;
        } else if (c <= 3) {
            c = U' ';
        } else if (c == U'‼' || c == U'⁉' || c == U'⁈') {
            text::AppendRune(out, c == U'⁈' ? U'？' : U'！');  // the marks CP932 has: ‼ ！！, ⁉ ！？
            c = c == U'‼' ? U'！' : c == U'⁉' ? U'？' : U'！';
            if (const text::Rune vs = At(text, i); vs.cp == 0xFE0E || vs.cp == 0xFE0F) i += vs.size;  // ‼︎
        } else {
            c = MapForm(c);
        }
        text::AppendRune(out, c);
    }
    return out;
}

std::string JoinNasai(std::string_view text) {
    return ReplaceAll(text, SplitNasai, [](const Match& m) { return Cat({m.group[1], m.group[2]}); });
}

std::string JoinMasu(std::string_view text) {
    const auto join = [](const Match& m) {
        return Cat({m.group[1], m.group[2], m.group[5], DrawlSpan(m.group[4], kDrawlMark), m.group[3]});
    };
    return ReplaceAll(ReplaceAll(text, SplitMasu, join), SplitCutPolite, join);
}

std::string JoinPausedCopula(std::string_view text) {
    return ReplaceAll(text, SplitPausedCopula, [](const Match& m) {
        const char32_t mark = m.group[2] == "でした" ? kPastLeadDrawlMark : kLeadDrawlMark;
        return Cat({m.group[1], m.group[2], m.group[5], DrawlSpan(m.group[4], mark), m.group[3]});
    });
}

std::string RewriteInterjections(std::string_view text) {
    std::string s = ReplaceAll(text, StartInterjection, [](const Match& m) {
        return Cat({m.group[1], m.group[2], m.group[3], "っ", m.group[4]});
    });
    s = ReplaceAll(s, StartEe, [](const Match& m) { return Cat({m.group[1], m.group[2], "えっ", m.group[3]}); });
    s = ReplaceAll(s, PauseEe, [](const Match& m) { return Cat({m.group[1], "えっ", m.group[2]}); });
    return ReplaceAll(s, StartJaa, [](const Match& m) {
        return Cat({m.group[1], m.group[2], "じゃあ、", m.group[3]});
    });
}

std::string RewriteMilitaryTime(std::string_view text) {
    if (text.find("マル") == std::string_view::npos && text.find("まる") == std::string_view::npos) {
        return std::string(text);
    }
    std::string out;
    size_t at = 0;
    Match m;
    for (size_t from = 0; Find(text, from, MilitaryTime, m); from = m.end) {
        if (m.end < text.size() && IsKanaOrMark(At(text, m.end).cp)) {
            const size_t end = TimeEnd(text, m);
            if (end == 0) continue;
            m.end = end;
        }
        std::string words;
        for (size_t i = 0; i < m.group[0].size();) {
            const text::Rune r = At(m.group[0], i);
            i += r.size;
            if (r.cp == U'、' || r.cp == U'・' || r.cp == U' ' || r.cp == U'ー') continue;
            text::AppendRune(words, r.cp >= U'ぁ' && r.cp <= U'ゖ' ? r.cp + 0x60 : r.cp);
        }
        out.append(text.substr(at, m.begin - at));
        for (std::string_view w = words; !w.empty();) {
            const MilitaryDigit* digit = nullptr;
            for (const MilitaryDigit& d : kMilitaryDigits) {
                if (w.starts_with(d.word)) {
                    digit = &d;
                    break;
                }
            }
            if (digit == nullptr) break;  // unreachable: the match is made of digit words
            out.push_back(digit->digit);
            w.remove_prefix(digit->word.size());
        }
        at = m.end;
    }
    out.append(text.substr(at));
    return out;
}

std::string CollapseDoubledWo(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    char32_t before = 0, last = 0;  // the last two characters written
    for (size_t i = 0; i < text.size();) {
        const text::Rune r = text::DecodeRune(text.substr(i));
        if (r.cp != U'を' || last != U'を' || before == U'を' || !IsJapaneseRune(before)) {
            out.append(text.substr(i, r.size));
            before = last;
            last = r.cp;
        }
        i += r.size;
    }
    return out;
}

}  // namespace internal

}  // namespace hdTrans::pipeline
