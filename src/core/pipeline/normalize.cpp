// The rewrites of Normalize, in order:
//   1. on a document line, the kin terms for the speaker's sex (WriteKinTerms), then
//      ruby markup removed (StripRuby);
//   2. character forms: half-width katakana, CP932 mapping differences, kanji forms
//      (NormalizeForms);
//   3. an apology or greeting cut before なさい joined (JoinNasai);
//   4. a verb stem cut before ます joined (JoinMasu);
//   5. spoken and old-fashioned spellings (SpokenForms);
//   6. あれ, え and じゃあ at the start of a sentence (RewriteInterjections);
//   7. casual contractions (RewriteCasual);
//   8. the military time in digits (RewriteMilitaryTime).
// Each step reads the output of the step before.
#include "core/pipeline/normalize.h"

#include "core/pipeline/pattern.h"
#include "core/tokenizer/canonical.h"

namespace hdTrans::pipeline {

using namespace internal;

std::string Normalize(std::string_view line, const LineContext& ctx) {
    std::string text(line);
    if (ctx.document) text = StripRuby(WriteKinTerms(text, ctx.female));
    text = JoinNasai(NormalizeForms(text));
    text = RewriteCasual(RewriteInterjections(SpokenForms(JoinMasu(text), ctx.exclamationNa, ctx.dialectForms)));
    return RewriteMilitaryTime(text);
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
    {U'頰', U'頬'}, {U'摑', U'掴'}, {U'𠮟', U'叱'}, {U'剝', U'剥'}, {U'繫', U'繋'}, {U'噓', U'嘘'},
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

// ([いきぎしじちにひびみりえけげせぜてでねへべめれ])[、，…‥]+(ます|ました|ません)([^ぁ-ゖァ-ヺー～]|$)
bool SplitMasu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"いきぎしじちにひびみりえけげせぜてでねへべめれ")) return false;
    return Repeat(s, p, InSet(U"、，…‥"), 1, kUnbounded, [&](size_t q) {
        for (std::string_view w : {"ます", "ました", "ません"}) {
            size_t r = q;
            if (!Lit(s, r, w)) continue;
            size_t t = r;
            const bool tail = Take(s, t, [](char32_t c) {
                return !IsHiraganaClass(c) && !(c >= U'ァ' && c <= U'ヺ') && c != U'ー' && c != U'～';
            });
            if (!tail && r != s.size()) continue;
            m.group[1] = Span(s, pos, p);
            m.group[2] = Span(s, q, r);
            m.group[3] = Span(s, r, t);
            m.end = t;
            return true;
        }
        return false;
    });
}

// A polite ending cut inside or spelled with っ or ス: a head, a run of marks, an
// optional っ and a tail, then ([^ぁ-ゖァ-ヺー～]|$). The heads and tails are
//   ([いきぎしじちにひびみりえけげせぜてでねへべめれ一-龥々]ま) (す|ス|した|せん)
//   (で) (す|ス|しょう)
// with marks [、，…‥・―─ー～]*; した, せん and しょう only after a mark other than ー ～
// and without っ, and す only after such a mark or っ (ー ～ alone are left as they are).
// Group 1 is the head, group 2 the tail (ス as す), group 3 the character after.
bool SplitCutPolite(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    bool masu = false;
    if (OneOf(s, p, U"いきぎしじちにひびみりえけげせぜてでねへべめれ") || Take(s, p, IsKanjiClass)) masu = Lit(s, p, "ま");
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
    const bool tsu = OneOf(s, p, U"っッ");
    const std::initializer_list<std::string_view> masuTails = {"す", "ス", "した", "せん"};
    const std::initializer_list<std::string_view> desuTails = {"す", "ス", "しょう"};
    for (std::string_view w : masu ? masuTails : desuTails) {
        size_t r = p;
        if (!Lit(s, r, w)) continue;
        const bool su = w == "す" || w == "ス";
        if (w == "す" ? !marked && !tsu : !su && (!marked || tsu)) continue;
        size_t t = r;
        const bool tail = Take(s, t, [](char32_t c) {
            return !IsHiraganaClass(c) && !(c >= U'ァ' && c <= U'ヺ') && c != U'ー' && c != U'～';
        });
        if (!tail && r != s.size()) continue;
        m.group[1] = Span(s, pos, cut);
        m.group[2] = su ? std::string_view("す") : Span(s, p, r);
        m.group[3] = Span(s, r, t);
        m.end = t;
        return true;
    }
    return false;
}

// (^|[\n「『（。！？!?])([ 　―…‥]*) as groups 1 and 2, then rest(end of group 2).
template <class Rest>
bool SentenceStart(std::string_view s, size_t pos, Match& m, Rest rest) {
    for (int alt = 0; alt < 2; alt++) {
        size_t p = pos;
        if (alt == 0 ? pos != 0 : !OneOf(s, p, U"\n「『（。！？!?")) continue;
        const bool ok = Repeat(s, p, InSet(U" 　―…‥"), 0, kUnbounded, [&](size_t q) {
            m.group[1] = Span(s, pos, p);
            m.group[2] = Span(s, p, q);
            return rest(q);
        });
        if (ok) return true;
    }
    return false;
}

// start (あれ|え)([、，！？…‥―─—])
bool StartInterjection(std::string_view s, size_t pos, Match& m) {
    return SentenceStart(s, pos, m, [&](size_t q) {
        for (std::string_view w : {"あれ", "え"}) {
            size_t r = q;
            if (!Lit(s, r, w)) continue;
            size_t t = r;
            if (!OneOf(s, t, U"、，！？…‥―─—")) continue;
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

constexpr std::string_view kMilitaryWords[] = {"マル", "ヒト", "フタ", "サン", "ヨン", "ゴー",
                                               "ゴ",   "ロク", "ナナ", "ハチ", "キュー", "キュウ"};
constexpr std::string_view kMilitaryKana[] = {"まる", "ひと", "ふた", "さん", "よん",
                                              "ごー", "ろく", "なな", "はち", "きゅう"};

// The digit words in the order the digits are read back, longest first.
struct MilitaryDigit {
    std::string_view word;
    char digit;
};
constexpr MilitaryDigit kMilitaryDigits[] = {
    {"キュウ", '9'}, {"キュー", '9'}, {"マル", '0'}, {"ヒト", '1'}, {"フタ", '2'}, {"サン", '3'},
    {"ヨン", '4'},   {"ゴー", '5'},   {"ロク", '6'}, {"ナナ", '7'}, {"ハチ", '8'}, {"ゴ", '5'},
};

template <size_t N>
bool AnyWord(std::string_view s, size_t& pos, const std::string_view (&words)[N]) {
    for (std::string_view w : words) {
        if (Lit(s, pos, w)) return true;
    }
    return false;
}

// W([、・ ]?W){3} with W a katakana digit word, or four hiragana digit words. A word
// never begins with a separator or ー, so the first word that fits is the only one
// that lets the rest match.
bool MilitaryTime(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (AnyWord(s, p, kMilitaryWords)) {
        int n = 1;
        for (; n < 4; n++) {
            size_t q = p;
            OneOf(s, q, U"、・ ");
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
        } else if (c <= 3) {
            c = U' ';
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
    const auto join = [](const Match& m) { return Cat({m.group[1], m.group[2], m.group[3]}); };
    return ReplaceAll(ReplaceAll(text, SplitMasu, join), SplitCutPolite, join);
}

std::string RewriteInterjections(std::string_view text) {
    std::string s = ReplaceAll(text, StartInterjection, [](const Match& m) {
        return Cat({m.group[1], m.group[2], m.group[3], "っ", m.group[4]});
    });
    s = ReplaceAll(s, StartEe, [](const Match& m) { return Cat({m.group[1], m.group[2], "えっ", m.group[3]}); });
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
        if (m.end < text.size() && IsKanaOrMark(At(text, m.end).cp)) continue;
        std::string words;
        for (size_t i = 0; i < m.group[0].size();) {
            const text::Rune r = At(m.group[0], i);
            i += r.size;
            if (r.cp == U'、' || r.cp == U'・' || r.cp == U' ') continue;
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

}  // namespace internal

}  // namespace hdTrans::pipeline
