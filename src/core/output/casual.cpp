// Casual speech: the plain sentence ends (다) that the rules chose for the predicates of
// speech, mapped to the casual style (해체) so that the tense and mood the rules chose are
// kept.
#include <algorithm>
#include <string>
#include <string_view>

#include "core/dict/classes.h"
#include "core/output/compose.h"
#include "core/output/inflect.h"
#include "core/output/text.h"
#include "core/text/hangul.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"

namespace hdTrans::output {

QuoteState QuotesAfter(QuoteState state, std::string_view text) {
    for (std::size_t at = 0; at < text.size();) {
        const text::Rune r = text::DecodeRune(text.substr(at));
        if (r.size == 0) break;
        if (r.cp == U'「' || r.cp == U'『') {
            if (state.depth++ == 0) state.count++;
        } else if ((r.cp == U'」' || r.cp == U'』') && state.depth > 0) {
            state.depth--;
        }
        at += r.size;
    }
    return state;
}

std::vector<bool> PoliteQuotes(std::string_view line, int depth) {
    std::vector<bool> polite(depth > 0 ? 2 : 1, false);
    std::size_t own = depth > 0 ? 1 : 0;  // the quote whose own text is read
    for (std::size_t at = 0; at < line.size();) {
        const text::Rune r = text::DecodeRune(line.substr(at));
        at += r.size == 0 ? 1 : r.size;
        if (r.cp == U'「' || r.cp == U'『' || r.cp == U'｢') {
            if (depth++ == 0) {
                polite.push_back(false);
                own = polite.size() - 1;
            }
            continue;
        }
        if ((r.cp == U'」' || r.cp == U'』' || r.cp == U'｣') && depth > 0) {
            if (--depth == 0) own = 0;
            continue;
        }
        if (depth > 1 || polite[own]) continue;
        for (const std::string_view end : {"요", "죠", "니다", "시오"}) {
            if (!line.substr(0, at).ends_with(end)) continue;
            std::string_view after = line.substr(at);
            for (text::Rune d = text::DecodeRune(after);
                 d.size > 0 && std::u32string_view(U"~～―ー─-♪♡♥❤").find(d.cp) != std::u32string_view::npos;
                 d = text::DecodeRune(after)) {
                after.remove_prefix(d.size);
            }
            const char32_t next = after.empty() ? 0 : text::DecodeRune(after).cp;
            // Nouns that end in 요 (필요, 중요).
            const std::string_view head = line.substr(0, at - end.size());
            bool noun = false;
            for (const std::string_view n :
                 {"필", "중", "주", "수", "강", "소", "월", "화", "목", "금", "토", "일", "동", "요"}) {
                noun = noun || (end == "요" && head.ends_with(n));
            }
            if (!noun && (next == 0 || text::IsSpace(next) ||
                          std::u32string_view(U".,!?…‥」』｣。、！？").find(next) != std::u32string_view::npos)) {
                polite[own] = true;
            }
        }
    }
    return polite;
}

namespace {

// s without its last character.
std::string_view WithoutLast(std::string_view s) {
    std::size_t at = s.size();
    while (at > 0 && (static_cast<unsigned char>(s[at - 1]) & 0xC0) == 0x80) at--;
    return s.substr(0, at > 0 ? at - 1 : 0);
}

// Whether s starts with the end of a word: nothing, a space or punctuation.
bool WordEnds(std::string_view s) {
    if (s.empty()) return true;
    const char32_t c = text::DecodeRune(s).cp;
    return text::IsSpace(c) ||
           std::u32string_view(U".,!?…‥~～―ー─-「『」』｢｣。、！？♪♡♥❤").find(c) != std::u32string_view::npos;
}

// The spoken form of the written word at the start of s (것은 건, 무엇을 뭘) and the bytes it
// replaces; {} for none. 것 forms follow a space or 그, 이, 저; 무엇 forms start a word.
std::pair<std::string_view, std::size_t> SpokenWord(std::string_view s, bool pronoun) {
    struct Form {
        std::string_view written;
        std::string_view spoken;
        bool ended;  // the word ends after it
    };
    static constexpr Form kForms[] = {
        {"것은", "건", false},       {"것을", "걸", false},       {"것으로", "걸로", false},       {"것이야", "거야", false},
        {"것이지", "거지", false},   {"것이니까", "거니까", false}, {"것이", "게", true},
        {"것", "거", true},          {"무엇인가", "뭔가", false},  {"무엇을", "뭘", false},
        {"무엇이야", "뭐야", false}, {"무엇이지요", "뭐죠", false}, {"무엇이죠", "뭐죠", false}, {"무엇이지", "뭐지", false},  {"무엇입니까", "뭡니까", false},
        {"무엇입니다", "뭡니다", false}, {"무엇이에요", "뭐예요", false}, {"무엇인지", "뭔지", false},
        {"무엇인데", "뭔데", false}, {"무엇이든", "뭐든", false},  {"무엇이라도", "뭐라도", false},
        {"무엇일까", "뭘까", false}, {"무엇이", "뭐가", true},    {"무엇", "뭐", true},
    };
    for (const Form& f : kForms) {
        if (!s.starts_with(f.written) || (f.ended && !WordEnds(s.substr(f.written.size())))) continue;
        if (pronoun && f.written.starts_with("무엇")) continue;
        // 무엇인가 asking what something is (무엇인가가 문제, 무엇인가를 몰라) is 뭔지.
        const std::string_view rest = s.substr(f.written.size());
        // 것 같다 keeps 것 even in speech (갈 것 같아).
        if (f.written == "것" && rest.starts_with(" 같")) return {};
        if (f.written == "무엇인가" && (rest.starts_with("가") || rest.starts_with(" 몰") || rest.starts_with(" 모르") ||
                                        rest.starts_with("를 몰") || rest.starts_with("를 모르"))) {
            return {};
        }
        return {f.spoken, f.written.size()};
    }
    return {};
}

// Whether s ends with the 아/어 form of a verb, maybe followed by one space (와, 보여 , 해 ):
// the form an auxiliary 주다 or 두다 follows.
bool AfterInfinitive(std::string_view s) {
    if (s.ends_with(" ")) s.remove_suffix(1);
    const std::string_view head = WithoutLast(s);
    if (head.size() == s.size()) return false;
    const auto syllable = text::Split(text::DecodeRune(s.substr(head.size())).cp);
    if (!syllable || syllable->final != 0) return false;
    constexpr int kInfinitiveVowels[] = {0, 1, 4, 6, 9, 10, 14};  // ㅏ ㅐ ㅓ ㅕ ㅘ ㅙ ㅝ
    return std::find(std::begin(kInfinitiveVowels), std::end(kInfinitiveVowels), syllable->vowel) !=
           std::end(kInfinitiveVowels);
}

// The contracted spoken form of 주, 두 and 되 before 어 and 었 at the start of s (와 주어 와
// 줘, 그만두어 그만둬, 됐어) and the bytes it replaces; {} for none; before is the text so far.
// A word 주어 or 두어 on its own is the noun 주어 (subject) or 두어 (a couple) before a
// particle or the copula; otherwise it is the verb before 서, 도, 야, 라 or 요, or after the
// 아/어 form of a verb, a particle or an adverb (보여 주어, 돈을 주어, 곁에 두어, 가만히 두어).
// 주어지다 (be given) keeps its 주어.
std::pair<std::string_view, std::size_t> SpokenSyllables(std::string_view s, std::string_view before) {
    static constexpr std::pair<std::string_view, std::string_view> kForms[] = {
        {"주어", "줘"}, {"주었", "줬"}, {"두어", "둬"}, {"두었", "뒀"}, {"되어", "돼"}, {"되었", "됐"},
    };
    for (const auto& [written, spoken] : kForms) {
        if (!s.starts_with(written)) continue;
        const std::string_view rest = s.substr(written.size());
        if (written == "주어" || written == "두어") {
            for (const std::string_view given : {"지", "진", "졌", "져"}) {
                if (rest.starts_with(given)) return {};
            }
            for (const std::string_view particle : {"가", "는", "를", "의", "이", "입", "인", "일", "임", "만", "와"}) {
                if (rest.starts_with(particle)) return {};
            }
            const std::string_view head = WithoutLast(before);
            const bool attached = !before.empty() && text::Split(text::DecodeRune(before.substr(head.size())).cp);
            bool verb = attached || AfterInfinitive(before);
            for (const std::string_view word : {"을 ", "를 ", "에 ", "에게 ", "한테 ", "히 ", "고 ", "게 ", "로 "}) {
                verb = verb || before.ends_with(word);
            }
            for (const std::string_view ending : {"서", "도", "야", "라", "요"}) {
                verb = verb || rest.starts_with(ending);
            }
            if (!verb) return {};
        }
        return {spoken, written.size()};
    }
    return {};
}

}  // namespace

std::string SpokenWords(std::string_view line, int depth, const std::vector<std::uint8_t>& styles) {
    std::string out;
    std::size_t quote = depth > 0 ? 1 : 0;
    std::size_t count = quote;
    for (std::size_t at = 0; at < line.size();) {
        const text::Rune r = text::DecodeRune(line.substr(at));
        const std::size_t size = r.size == 0 ? 1 : r.size;
        if (r.cp == U'「' || r.cp == U'『' || r.cp == U'｢') {
            if (depth++ == 0) quote = ++count;
        } else if ((r.cp == U'」' || r.cp == U'』' || r.cp == U'｣') && depth > 0) {
            if (--depth == 0) quote = 0;
        } else if (depth == 1 && quote < styles.size() && (styles[quote] & ~(kCasualSpeech | kPoliteStyle | kPoliteTalkStyle | kPoliteFormStyle)) == 0) {
            if (const auto [spoken, length] = SpokenSyllables(line.substr(at), out); length > 0) {
                out += spoken;
                at += length;
                continue;
            }
            // 무엇 is 뭐, 것 is 거 and 주어 is 줘 in any plain or polite quote: spoken Korean
            // contracts them whatever the speech level (그건, 하는 걸, 뭘, 와 줘).
            // The word starts after a space or punctuation, or after 그, 이, 저 that do (그것은).
            const std::string_view head = WithoutLast(out);
            const char32_t last = out.empty() ? 0 : text::DecodeRune(std::string_view(out).substr(head.size())).cp;
            const bool start = out.empty() || WordEnds(std::string_view(out).substr(head.size()));
            const bool pronoun = (last == U'그' || last == U'이' || last == U'저') &&
                                 (head.empty() || WordEnds(head.substr(WithoutLast(head).size())));
            if (start || pronoun) {
                const auto [spoken, length] = SpokenWord(line.substr(at), !start);
                if (length > 0) {
                    out += spoken;
                    at += length;
                    continue;
                }
            }
        }
        out += line.substr(at, size);
        at += size;
    }
    return out;
}

namespace {

using internal::First;
using internal::Selected;

struct Rewrite {
    std::string_view from;
    std::string_view to;
};

template <std::size_t N>
std::string_view Find(const Rewrite (&table)[N], std::string_view s) {
    for (const Rewrite& r : table) {
        if (r.from == s) return r.to;
    }
    return {};
}

// Whole ending texts (a form class and its Korean) and copula entries.
constexpr Rewrite kEndings[] = {{"4다", "6"}, {"1다", "6"}, {"1는다", "6"}, {"5다", "5어"}};
constexpr Rewrite kCopulas[] = {{"(이)다", "(이)야"}, {"(이었)였다", "(이었)였어"}};

// The last word of an ending text.
constexpr Rewrite kWords[] = {
    {"것이다", "거야"}, {"거다", "거야"}, {"않는다", "않아"}, {"있다", "있어"}, {"없다", "없어"},
    {"싶다", "싶어"},   {"같다", "같아"}, {"된다", "돼"},     {"모른다", "몰라"}, {"한다", "해"},
};

// The present of an action said in speech is mostly a plan (後で返す 나중에 돌려줄 거야,
// 手伝ってやる 도와 줄 거야), a promise when the speaker is its subject (俺が行く 내가 갈게);
// in 해체 it would read as an order or a request (돌려줘).
constexpr Rewrite kPlannedWords[] = {
    {"한다", "할 거야"}, {"본다", "볼 거야"}, {"둔다", "둘 거야"}, {"버린다", "버릴 거야"},
    {"간다", "갈 거야"}, {"온다", "올 거야"}, {"준다", "줄 거야"}, {"드린다", "드릴 거야"},
};

// The clause of speech before an item: the text after the last bracket, sentence end or
// comma.
std::string_view Clause(std::string_view before) {
    std::size_t start = 0;
    for (std::size_t at = 0; at < before.size();) {
        const text::Rune r = text::DecodeRune(before.substr(at));
        at += r.size == 0 ? 1 : r.size;
        if (std::u32string_view(U"「『」』。！？!?、,…‥").find(r.cp) != std::u32string_view::npos) start = at;
    }
    return before.substr(start);
}

// Whether a present verb (word, its Korean) said after clause, its Japanese, states a plan:
// an action, without a question word and without a subject other than the speaker
// (雨が降る 비가 와, 誰が行く 누가 가); the verbs of ko-verb.casual-state state.
bool Planned(std::string_view word, std::string_view clause) {
    for (const std::string_view s : dict::ClassMembers("ko-verb.casual-state")) {
        if (word.ends_with(s)) return false;
    }
    // A question word, not an indefinite one (誰も, 何か, いつも).
    for (const std::string_view q : {"何", "なに", "なんで", "なぜ", "どうして", "誰", "だれ", "どこ", "いつ", "どれ"}) {
        for (std::size_t at = clause.find(q); at != std::string_view::npos; at = clause.find(q, at + 1)) {
            const std::string_view after = clause.substr(at + q.size());
            if (!after.starts_with("も") && !after.starts_with("か") && !after.starts_with("でも")) return false;
        }
    }
    // A report (返上するという 반납한다고 해).
    for (const std::string_view r : {"とい", "と言", "って言", "ってい", "ていう"}) {
        if (clause.find(r) != std::string_view::npos) return false;
    }
    // The subject of a condition or a reason before the verb (動きがあれば拘束する) is not its own.
    for (const std::string_view c : {"れば", "えば", "けば", "せば", "てば", "めば", "べば", "げば", "たら", "なら", "から", "ので",
                                     "ても", "のに"}) {
        if (const std::size_t at = clause.rfind(c); at != std::string_view::npos) clause = clause.substr(at + c.size());
    }
    for (std::size_t at = clause.find("が"); at != std::string_view::npos; at = clause.find("が", at + 1)) {
        // が inside a verb (あがらせて, 嫌がる, 脱がす) is no particle; が before する is
        // (気がする).
        const std::string_view next = clause.substr(at + std::string_view("が").size());
        const std::string_view head = clause.substr(0, at);
        bool inside = false;
        for (const std::string_view k : {"ら", "り", "る", "れ", "ろ", "っ", "さ", "せ", "そ"}) {
            inside = inside || next.starts_with(k);
        }
        if (next.starts_with("す") || next.starts_with("し")) {
            for (const std::string_view v : {"脱", "急", "騒", "転", "揺", "焦"}) inside = inside || head.ends_with(v);
        }
        if (inside) continue;
        bool speaker = false;
        for (const std::string_view p : {"私", "俺", "僕", "わたし", "あたし", "自分", "我々", "うち", "オレ", "ボク"}) {
            speaker = speaker || head.ends_with(p) || head.ends_with(std::string(p) + "たち") ||
                      head.ends_with(std::string(p) + "ら");
        }
        if (!speaker) return false;
    }
    return true;
}

// Whether clause names the speaker as the subject (俺が, 私は), whose planned action is a
// promise (내가 할게).
bool Promised(std::string_view clause) {
    for (const std::string_view p : {"私", "俺", "僕", "わたし", "あたし", "自分", "オレ", "ボク"}) {
        for (std::size_t at = clause.find(p); at != std::string_view::npos; at = clause.find(p, at + 1)) {
            const std::string_view rest = clause.substr(at + p.size());
            if (rest.starts_with("は") || rest.starts_with("が") || rest.starts_with("も")) return true;
        }
    }
    return false;
}

char32_t LastRune(std::string_view s) {
    char32_t last = 0;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.size == 0) return last;
        last = r.cp;
        s.remove_prefix(r.size);
    }
    return last;
}

// A word whose syllable before 다 ends in ㅆ (했다, 주었다): that stem in its spoken
// contraction (줬) with 어; "" for other words.
std::string PastWord(std::string_view word) {
    if (!word.ends_with("다")) return {};
    const std::string_view stem = word.substr(0, word.size() - std::string_view("다").size());
    const auto syllable = text::Split(LastRune(stem));
    if (stem.empty() || !syllable || syllable->final != 20) return {};  // 20: ㅆ
    return internal::SpokenInfinitive(stem) + "어";
}

// The casual form of an ending text (form class and Korean), or "" when it keeps its form.
std::string CasualEnding(std::string_view ending, bool planned, bool promised) {
    if (planned && (ending == "4다" || ending == "1는다")) return promised ? "3게" : "3 거야";
    if (const std::string_view to = Find(kEndings, ending); !to.empty()) return std::string(to);
    const char c = First(ending);
    if (c < '0' || c > '9') return {};
    const std::size_t space = ending.rfind(' ');
    const std::size_t at = space == std::string_view::npos ? 1 : space + 1;
    const std::string_view word = ending.substr(at);
    std::string soft(planned ? Find(kPlannedWords, word) : std::string_view());
    if (promised && soft.ends_with(" 거야")) soft = soft.substr(0, soft.size() - std::string_view(" 거야").size()) + "게";
    if (soft.empty()) soft = Find(kWords, word);
    if (soft.empty()) soft = PastWord(word);
    if (soft.empty()) return {};
    return std::string(ending.substr(0, at)) + soft;
}

// Characters that end a sentence of speech after its predicate, and those passed over
// before them (a stretched or cut-off end).
constexpr std::u32string_view kEnds = U"」』！？!?‼⁉⁈。．.…‥、，,～~〜∼―─♪☆★♥♡";
constexpr std::u32string_view kSkipped = U"っッーぇぁ";

// Whether c is passed over after a predicate: one of kSkipped or a closing mark of the sound
// stage.
bool Skipped(char32_t c) { return kSkipped.find(c) != std::u32string_view::npos || IsClosingMark(c); }

// The last character of s that is not a space or an emphasis open mark, 0 for none.
char32_t LastNonSpace(std::string_view s) {
    char32_t last = 0;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.size == 0) return last;
        if (!text::IsSpace(r.cp) && r.cp != kEmphasisOpen) last = r.cp;
        s.remove_prefix(r.size);
    }
    return last;
}

bool Hiragana(char32_t c) { return c >= U'ぁ' && c <= U'ゖ'; }

// The item before item i that writes something, i itself for none (ん in あるんか).
int Spoken(const std::vector<ResolvedItem>& items, int i) {
    for (int k = i - 1; k >= 0; k--) {
        if (items[k].directIndex >= 0 || !Selected(items[k]).text.empty()) return k;
    }
    return i;
}

// Whether the Korean of an item ends in an adnominal form (있는, 한 사람인, 4은), which a
// following copula cannot take.
bool Adnominal(const ResolvedItem& it) {
    if (it.directIndex < 0) return Selected(it).text == "인";  // the copula of な
    const std::string_view text = it.ending.text;
    if (text == "4" || text == "0") return true;
    for (const std::string_view end : {"는", "인", "은", "던"}) {
        if (text.ends_with(end)) return true;
    }
    return false;
}

// Whether clause holds a question word, not an indefinite one (誰も, 何か, いつも, どうしても,
// いくらでも), not one inside a word (あいつ, みんなに) and not one of an embedded question
// (どう使うかは).
bool QuestionWord(std::string_view clause) {
    for (const std::string_view q : {"何", "なに", "なんで", "なんの", "なんだろ", "なんな", "なぜ", "どう", "どこ", "どれ", "どの",
                                     "どんな", "どっち", "どちら", "誰", "だれ", "いつ", "いくら", "いくつ", "一体"}) {
        for (std::size_t at = clause.find(q); at != std::string_view::npos; at = clause.find(q, at + 1)) {
            const std::string_view after = clause.substr(at + q.size());
            bool other = false;
            for (const std::string_view k : {"も", "か", "でも", "だって", "しても", "せ", "ぞ"}) other = other || after.starts_with(k);
            for (const std::string_view k : {"かは", "かを", "かが", "かも", "か、", "かどうか", "かなんて", "かわか", "か分か",
                                             "か知", "ても", "でも", "うが", "ようと"}) {
                other = other || after.find(k) != std::string_view::npos;
            }
            if (Hiragana(text::DecodeRune(q).cp) && at > 0) {
                std::size_t back = at - 1;
                while (back > 0 && (static_cast<unsigned char>(clause[back]) & 0xC0) == 0x80) back--;
                const char32_t previous = text::DecodeRune(clause.substr(back)).cp;
                other = other || (Hiragana(previous) &&
                                  std::u32string_view(U"はがをにでもとへてやのよねっどらしば").find(previous) == std::u32string_view::npos);
            }
            if (!other) return true;
        }
    }
    return false;
}

// The syllable that ends s and the bytes before it; {0, s.size()} when s does not end in one.
std::pair<char32_t, std::size_t> LastSyllable(std::string_view s) {
    if (s.size() < 3) return {0, s.size()};
    const char32_t r = text::DecodeRune(s.substr(s.size() - 3)).cp;
    if (!text::Split(r)) return {0, s.size()};
    return {r, s.size() - 3};
}

// The formal question (-ㅂ니까) of text that ends in the formal statement -ㅂ니다 (7니다, 뿐입니다,
// 하셨습니다); "" for other text.
std::string FormalAsked(std::string_view text) {
    if (!text.ends_with("니다")) return {};
    const std::string_view head = text.substr(0, text.size() - std::string_view("니다").size());
    if (!head.ends_with("7") && text::Final(LastSyllable(head).first) != text::kFinalBieup) return {};
    return std::string(head) + "니까";
}

// The 해요체 of a formal statement pressed with よ before ？ (見守るだけですよ？ 뿐입니다 뿐이에요?,
// 撃ちますよ？ 7니다 6요), as ですよ is (이에)예요; "" for other text.
std::string PressedPolite(std::string_view text) {
    constexpr std::string_view kCopula = "입니다", kFormal = "7니다";
    if (text.ends_with(kCopula)) return std::string(text.substr(0, text.size() - kCopula.size())) + "이에요";
    if (text.ends_with(kFormal)) return std::string(text.substr(0, text.size() - kFormal.size())) + "6요";
    return {};
}

// The 해요체 of a 해체 end, for a line that speaks politely elsewhere (무서워 무서워요, (이)야
// (이에)예요, 거야 거예요); "" where it has none.
std::string PoliteOf(std::string soft) {
    if (soft.ends_with("(이)야")) return soft.substr(0, soft.size() - std::string_view("(이)야").size()) + "(이에)예요";
    if (soft.ends_with("거야")) return soft.substr(0, soft.size() - std::string_view("야").size()) + "예요";
    if (soft.ends_with("야")) return {};
    return soft + "요";
}

// Whether a 해체 end text states a past (5어, 했어, 버렸어).
bool PastText(std::string_view text) {
    if (First(text) == '5') return true;
    const std::size_t space = text.rfind(' ');
    const std::string_view word = text.substr(space == std::string_view::npos ? 0 : space + 1);
    return (word != "있다" && !PastWord(word).empty()) || text.ends_with("었다") || text.ends_with("였다");
}

// Whether the run of end marks that starts with close and goes on with rest (？, ！？, …？)
// asks; exclaimed tells whether the run also holds an exclamation mark (！？, ？！).
bool Asks(char32_t close, std::string_view rest, bool* exclaimed) {
    *exclaimed = false;
    for (char32_t c = close;;) {
        if (c == U'？' || c == U'?' || c == U'⁉' || c == U'⁈') {
            *exclaimed = *exclaimed || c == U'⁉' || c == U'⁈' || rest.starts_with("！") || rest.starts_with("!");
            return true;
        }
        if (c == U'！' || c == U'!' || c == U'‼') *exclaimed = true;
        if (std::u32string_view(U"！!‼…‥～~〜∼♪☆★♥♡").find(c) == std::u32string_view::npos &&
            !Skipped(c) && !text::IsSpace(c)) {
            return false;
        }
        if (rest.empty()) return false;
        const text::Rune r = text::DecodeRune(rest);
        if (r.size == 0) return false;
        rest.remove_prefix(r.size);
        c = r.cp;
    }
}

// The family marker that ends a conjugating target (생각&AV@), "" for none.
std::string_view Marker(std::string_view stem) {
    if (stem.size() < 4 || !stem.ends_with("@")) return {};
    const std::string_view m = stem.substr(stem.size() - 4);
    return m[0] == '&' || m[0] == '$' || m[0] == '%' ? m : std::string_view();
}

// The agreement an ending text seeks (5잖아 몰랐잖아, 높지, 되잖아) in polite speech: a past or a 지
// with 죠 (몰랐죠, 높죠), another 잖아 with 요 (되잖아요).
void PoliteAgreement(std::string& ending) {
    constexpr std::string_view kSeek = "잖아", kSure = "지", kPolite = "죠";
    if (ending.ends_with(kSure)) {
        ending.replace(ending.size() - kSure.size(), kSure.size(), kPolite);
    } else if (ending.ends_with(kSeek)) {
        const std::string_view stem = std::string_view(ending).substr(0, ending.size() - kSeek.size());
        const auto syllable = text::Split(LastRune(stem));
        if (stem == "5" || (syllable && syllable->final == 20)) {  // the past form, or ㅆ (20)
            ending.replace(ending.size() - kSeek.size(), kSeek.size(), kPolite);
        } else {
            ending += "요";
        }
    }
}

}  // namespace

char32_t Composer::QuotedEnd(int i, int last, bool nested, bool* asked, bool* clipped, bool* exclaimed,
                            bool* drawn) const {
    if (asked != nullptr) *asked = false;
    if (drawn != nullptr) *drawn = false;
    if (exclaimed != nullptr) *exclaimed = false;
    if (clipped != nullptr) *clipped = false;
    const ResolvedItem& it = (*items_)[i];
    const std::string_view own = source_[it.tokenIndex];
    const std::size_t end = static_cast<std::size_t>(it.offset + it.length);
    std::string after(end < own.size() ? own.substr(end) : std::string_view());
    for (int t = it.tokenIndex + 1; t <= last; t++) after += source_[t];
    // The tokens after the sentence while they hold end marks only (～ then ？).
    for (std::size_t following = static_cast<std::size_t>(last) + 1; following < tokens_->size(); following++) {
        const std::string_view t = (*tokens_)[following].text;
        after += t;
        bool marks = !t.empty();
        for (std::string_view m = t; marks && !m.empty();) {
            const text::Rune r = text::DecodeRune(m);
            marks = r.size > 0 && (kEnds.find(r.cp) != std::u32string_view::npos || Skipped(r.cp) || text::IsSpace(r.cp));
            m.remove_prefix(r.size == 0 ? m.size() : r.size);
        }
        if (!marks) break;
    }
    std::string_view s = after;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.size == 0) return 0;
        s.remove_prefix(r.size);
        if (Skipped(r.cp) || text::IsSpace(r.cp)) {
            if (r.cp == kClipMark && clipped != nullptr) *clipped = true;
            if ((r.cp == kDrawnMark || r.cp == kLengthenMark || r.cp == U'ぁ' || r.cp == U'ぇ' || r.cp == U'ー') &&
                drawn != nullptr) {
                *drawn = true;
            }
            continue;
        }
        if (kEnds.find(r.cp) == std::u32string_view::npos) return 0;
        // A quote inside speech that a noun, a particle or another quote follows (『書いた』もの,
        // 『言わない』『聞かない』) is cited; a report (逃げた……とある) is no speech.
        if (nested && (r.cp == U'」' || r.cp == U'』') && !s.empty()) {
            const char32_t next = text::DecodeRune(s).cp;
            if (text::IsKanji(next) || (next >= U'ァ' && next <= U'ヺ') || next == U'「' || next == U'『' ||
                (next >= U'ぁ' && next <= U'ゖ' && !s.starts_with("と") && !s.starts_with("っ"))) {
                return 0;
            }
        }
        // A clause quoted in speech (探してほしい、と).
        if ((r.cp == U'、' || r.cp == U'，' || r.cp == U',') && (s.starts_with("と") || s.starts_with("って"))) return 0;
        if (r.cp == U'…' || r.cp == U'‥') {
            while (s.starts_with("…") || s.starts_with("‥")) s.remove_prefix(std::string_view("…").size());
            if (s.starts_with("と") || s.starts_with("って")) return 0;
        }
        bool loud = false;
        const bool asks = Asks(r.cp, s, &loud);
        if (asked != nullptr) *asked = asks;
        if (exclaimed != nullptr) *exclaimed = asks && loud;
        return r.cp;
    }
    return U'\n';
}

bool Composer::GoesOn(int i, int last) const {
    const ResolvedItem& it = (*items_)[i];
    const std::string_view own = source_[it.tokenIndex];
    const std::size_t end = static_cast<std::size_t>(it.offset + it.length);
    std::string after(end < own.size() ? own.substr(end) : std::string_view());
    for (int t = it.tokenIndex + 1; t <= last; t++) after += source_[t];
    for (std::size_t following = static_cast<std::size_t>(last) + 1; following < tokens_->size(); following++) {
        after += (*tokens_)[following].text;
    }
    std::string_view s = after;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.size == 0) return false;
        if (!Skipped(r.cp) && std::u32string_view(U"…‥～~〜∼").find(r.cp) == std::u32string_view::npos) break;
        s.remove_prefix(r.size);
    }
    if (s.empty()) return false;
    const char32_t next = text::DecodeRune(s).cp;
    return std::u32string_view(U"」』）)｣。．.！!？?‼⁉⁈\n").find(next) == std::u32string_view::npos;
}

void Composer::WriteCasual(int first, int last) {
    std::vector<ResolvedItem>& items = *items_;
    // The quote state at (carriedToken, carriedOffset), carried forward from item to item
    // so that a long line is read once.
    QuoteState carried = settings_.quotes;
    int carriedToken = first;
    std::size_t carriedOffset = 0;
    for (int i = 0; i < static_cast<int>(items.size()); i++) {
        ResolvedItem& it = items[i];
        if (it.tokenIndex < first || it.tokenIndex > last) continue;
        const std::string_view src = source_[it.tokenIndex];
        const std::size_t start = std::min<std::size_t>(static_cast<std::size_t>(it.offset), src.size());
        const std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(it.offset + it.length), src.size());
        const std::string_view own = src.substr(start, end - start);
        // The source text from the first token up to the item, built when a rule reads it.
        std::string beforeText;
        bool beforeBuilt = false;
        auto before = [&]() -> const std::string& {
            if (!beforeBuilt) {
                for (int t = first; t < it.tokenIndex; t++) beforeText += source_[t];
                beforeText += src.substr(0, start);
                beforeBuilt = true;
            }
            return beforeText;
        };
        if (it.tokenIndex < carriedToken || (it.tokenIndex == carriedToken && start < carriedOffset)) {
            carried = settings_.quotes;
            carriedToken = first;
            carriedOffset = 0;
        }
        for (; carriedToken < it.tokenIndex; carriedToken++, carriedOffset = 0) {
            carried = QuotesAfter(carried, std::string_view(source_[carriedToken]).substr(carriedOffset));
        }
        carried = QuotesAfter(carried, src.substr(carriedOffset, start - carriedOffset));
        carriedOffset = start;
        // Speech: inside a quote at the start and at the end of the item; not polite or old
        // speech.
        const QuoteState at = carried;
        const bool quoted = at.depth > 0 && QuotesAfter(at, own).depth > 0;
        const std::size_t quote = at.depth > 0 ? static_cast<std::size_t>(at.count) : 0;  // 0: the text outside quotes
        const std::uint8_t style = settings_.quoteStyles != nullptr && quote < settings_.quoteStyles->size()
                                       ? (*settings_.quoteStyles)[quote]
                                       : 0;
        // Polite or old speech; kCasualSpeech only marks casual speech.
        const bool styled = (style & ~kCasualSpeech) != 0;
        bool asked = false;
        bool clipped = false;
        bool loud = false;
        bool drawn = false;
        const char32_t close = QuotedEnd(i, last, quoted && at.depth > 1, &asked, &clipped, &loud, &drawn);
        const bool questioned = close != 0 && asked && !loud;  // ？ without ！
        // A plain end right before ！ or ？, or before … or ～ that ends the sentence, is spoken (spec
        // SPC-1), in a quote, and outside one unless the text there is narration.
        bool punctuated = close != 0 && (quoted || !settings_.narrated) &&
                          std::u32string_view(U"！!‼⁉⁈？?…‥～~〜∼").find(close) != std::u32string_view::npos;
        if (punctuated && std::u32string_view(U"…‥～~〜∼").find(close) != std::u32string_view::npos) {
            punctuated = !GoesOn(i, last);
        }
        // A drawn, lengthened or clipped end is spoken as well (好きだぁぁぁっ 좋아해애애앳, 疲れたー
        // 지쳤어~), not before a comma (痛いいいい、やめて 아프다아아아, 그만해).
        punctuated = punctuated || (close != 0 && (drawn || clipped) && (quoted || !settings_.narrated) &&
                                    std::u32string_view(U"、，,").find(close) == std::u32string_view::npos);
        if ((quoted || !settings_.narrated) && !styled) AskWord(i, last);
        if (close != 0 && asked &&
            AskConjecture(i, Clause(before() + std::string(own)), own, quoted && !styled)) {
            continue;
        }
        // っけ after a question word recalls with 더라 (どこに置いたっけ 어디에 두었더라), in
        // polite or old speech with 던가 (どこでしたっけ 어디서 했던가).
        if (close != 0 && own.ends_with("っけ") && it.directIndex >= 0 && it.ending.text.ends_with("나") &&
            QuestionWord(Clause(before() + std::string(own)))) {
            it.ending.text.replace(it.ending.text.size() - std::string_view("나").size(), std::string_view("나").size(),
                                   styled ? "던가" : "더라");
            continue;
        }
        // ？ after ぞ, ぜ, よ, わ, さ or ね presses a statement (結婚するつもりなんだぞ？), it
        // does not ask.
        bool pressedYo = false;
        if (asked) {
            std::size_t kept = 0;  // the end of the last character that is not a lengthening mark
            for (std::size_t pos = 0; pos < own.size();) {
                const text::Rune r = text::DecodeRune(own.substr(pos));
                pos += r.size == 0 ? 1 : r.size;
                if (!Skipped(r.cp)) kept = pos;
            }
            const std::string_view tail = own.substr(0, kept);
            for (const std::string_view p : {"ぞ", "ぜ", "よ", "わ", "さ", "ね"}) asked = asked && !tail.ends_with(p);
            pressedYo = tail.ends_with("よ");
            // A pressed statement is no question and keeps its end (なんだぞ？ 생각이다?).
            if (!asked) punctuated = false;
        }
        // でしょ is polite itself: in a quote of polite speech the agreement it seeks is polite too
        // (…なんです……知らなかったでしょ？ 몰랐죠?, 高いでしょ 높죠, ばいいでしょ 되잖아요).
        if (quoted && (style & kPoliteTalkStyle) != 0 && close != 0 && it.directIndex >= 0 &&
            it.endingSurface.ends_with("でしょ")) {
            PoliteAgreement(it.ending.text);
            continue;
        }
        // The formal -ㅂ니다 before ？ asks with -ㅂ니까 in any speech; a particle that writes nothing
        // leaves the end to the predicate before it, and after よ it is pressed (見守るだけですよ？
        // 지켜볼 뿐이에요?).
        if (questioned && (asked || pressedYo) && AskFormal(i, pressedYo)) continue;
        if (questioned && it.directIndex < 0 && it.selected >= 0 && Selected(it).text.empty() && i > 0 &&
            Spoken(items, i) != i && AskFormal(Spoken(items, i), own.starts_with("よ"))) {
            continue;
        }
        // A line that speaks politely elsewhere takes 해요체 where a mark makes a plain end spoken
        // (やめてください！怖い！ 그만해 주세요! 무서워요!).
        // Not an utterance that と or って quotes after its mark (豊作だー！とか言ってました).
        bool politeLine = punctuated && (style & kPoliteFormStyle) != 0 &&
                          (style & ~(kCasualSpeech | kPoliteStyle | kPoliteTalkStyle | kPoliteFormStyle)) == 0;
        if (politeLine) {
            const std::string_view whole = source_[it.tokenIndex];
            std::string rest(static_cast<std::size_t>(it.offset + it.length) < whole.size()
                                 ? whole.substr(static_cast<std::size_t>(it.offset + it.length))
                                 : std::string_view());
            for (int t = it.tokenIndex + 1; t < static_cast<int>(tokens_->size()) && rest.size() < 64; t++) rest += (*tokens_)[t].text;
            std::string_view r = rest;
            while (!r.empty()) {
                const text::Rune c = text::DecodeRune(r);
                if (c.size == 0 || (kEnds.find(c.cp) == std::u32string_view::npos && !Skipped(c.cp) && !text::IsSpace(c.cp))) break;
                r.remove_prefix(c.size);
            }
            politeLine = !r.starts_with("と") && !r.starts_with("って");
        }
        if ((!quoted && !punctuated) || (styled && !politeLine) || (!settings_.casual && !asked && !punctuated)) continue;
        // Marked casual quotes: their own text only, not a quote inside them (『書いた』って);
        // a question or an end before a mark is spoken in any plain quote.
        if (settings_.casualMarkedOnly && !asked && !punctuated && ((style & kCasualSpeech) == 0 || at.depth > 1)) continue;
        // である reports (優勢である) and declarations (破棄する) keep their style.
        if (close == 0 || own.find("であ") != std::string_view::npos ||
            (before().ends_with("で") && own.starts_with("あ"))) {
            continue;
        }
        // すか (行くんすか) is polite; よっか (しよっか) is no noun with か.
        if (quoted && asked && !before().ends_with("す") && !before().ends_with("ス") && !before().ends_with("っ") &&
            AskPlain(i, own)) {
            continue;
        }
        // 白紙撤回とする, 王太子妃とする: a declaration.
        bool declared = own.starts_with("する") && before().ends_with("と");
        for (const std::string_view d : dict::ClassMembers("noun.casual-declaration")) {
            declared = declared || own.starts_with(d) || before().ends_with(d) ||
                       before().ends_with(std::string(d) + "と") || before().ends_with(std::string(d) + "を");
        }
        if (declared) continue;
        const char32_t open = LastNonSpace(before());
        const bool alone = open == 0 || std::u32string_view(U"「『。！？!?…‥").find(open) != std::u32string_view::npos;
        const bool strict = !settings_.casual || settings_.casualMarkedOnly;
        // An end only a mark makes spoken: a past there keeps -다 (手に入れた！ 손에 넣었다!,
        // へたり込んでしまった…… 주저앉아버렸다……) unless it is the whole clause (わかった！ 알았어!).
        const bool markOnly = !asked && (!quoted || politeLine || (strict && ((style & kCasualSpeech) == 0 || at.depth > 1)));
        char32_t lead = 0;  // the character before the item, past the marks of the sound stage (すっっごい)
        for (std::string_view b = before(); !b.empty();) {
            const text::Rune r = text::DecodeRune(b);
            if (r.size == 0) break;
            if (!text::IsSpace(r.cp) && (r.cp < 0xE000 || r.cp > 0xF8FF)) lead = r.cp;
            b.remove_prefix(r.size);
        }
        const bool bare = lead == 0 || std::u32string_view(U"「『。！？!?…‥、，,").find(lead) != std::u32string_view::npos;
        const bool keepPast = markOnly && !bare;
        if (it.directIndex < 0) {
            // A copula alone in its clause (「だな」, しかし、だ), after a stutter (だだだ) or
            // after a particle (ユフィリアもだ) is no predicate of a sentence. A copula that a
            // construction wrote as a past verb (놀곤 했다) takes the 해체 of the past, and so does a
            // phrase a rule wrote that ends in a predicate word (そう 것 같다 것 같아, いたんだ 있었어).
            if (it.selected < 0) continue;
            // The old copula じゃ keeps -다 where only a mark would make it spoken (わしじゃ…… 나다……).
            if (it.surface == "じゃ" && strict && !asked && (!quoted || (style & kCasualSpeech) == 0)) continue;
            const std::string_view text = Selected(it).text;
            std::string to(Find(kCopulas, text));
            if (!to.empty() && (alone || open == U'、' || open == U'だ' || (i > 0 && First(Selected(items[i - 1]).code) == '2'))) {
                continue;
            }
            const std::size_t space = text.rfind(' ');
            const std::size_t wordAt = space == std::string_view::npos ? 0 : space + 1;
            if (to.empty() && (space != std::string_view::npos || !alone)) {
                std::string soft(Find(kWords, text.substr(wordAt)));
                if (soft.empty()) soft = PastWord(text.substr(wordAt));
                if (!soft.empty()) to = std::string(text.substr(0, wordAt)) + soft;
            }
            if (to.empty()) continue;
            if (keepPast && (text == "(이었)였다" || !PastWord(text.substr(wordAt)).empty())) continue;
            if (politeLine) to = PoliteOf(to);
            if (to.empty()) continue;
            it.entry.text = to;
            if (it.selected == 0) it.first.text = to;
            continue;
        }
        // The verb that ends the item: its own, or the one a folded branch puts after it
        // (読める 읽을 수 있다, Composer::Build).
        std::string stem(Selected(it).text);
        switch (it.detailRecord) {
            case 1: stem = "&AL@"; break;
            case 2: stem = "&AV@"; break;
            case 4: stem = "&HN@"; break;
            case 5: stem = "&C6@"; break;
        }
        const std::string_view marker = Marker(stem);
        if (it.ending.text.size() < 2) continue;
        // Kept: the copula verbs (이어).
        if (!marker.empty() && Lookup(marker[0], std::string(marker) + "6").ends_with("이어")) continue;
        // Strict speech keeps 너무 X다 (露骨すぎだ, 握りすぎだ), whose 해체 is no word,
        // and 待った (잠깐!), whose past would be read.
        if (strict) {
            const std::string_view surface = it.surface;
            if ((First(it.ending.text) != '5' &&
                 (surface.find("すぎ") != std::string_view::npos || surface.find("過ぎ") != std::string_view::npos)) ||
                (surface.starts_with("待") && First(it.ending.text) == '5' && close != U'？' && close != U'?')) {
                continue;
            }
        }
        const std::string present = PresentWord(it.ending.text, stem);
        // 말한다 in 해체 (말해) reads as an order; a name (虹輝と言う) or a promise keeps it.
        if (present.ends_with("말한다")) continue;
        bool stative = false;
        for (const std::string_view v : dict::ClassMembers("verb.casual-state")) {
            stative = stative || std::string_view(it.surface).starts_with(v);
        }
        bool state = false;
        for (const std::string_view s : dict::ClassMembers("ko-verb.casual-state")) {
            state = state || present.ends_with(s);
        }
        // A present action before ！, … or ～, or drawn out, keeps -ㄴ다, an exclamation (行く！ 간다!,
        // 勝つ！ 이긴다!, イくぅぅっ 간다아앗); before ？ it asks in 해체 (行く？ 가?). A verb of 형용동사
        // (好きだ 좋아한다) states.
        const internal::KoreanFamily* family = marker.empty() ? nullptr : internal::FindKoreanFamily(marker);
        const bool verbal = it.detailClass == DetailClass::Dakeiyo && family != nullptr &&
                            family->partOfSpeech == internal::KoreanPartOfSpeech::Verb;
        if (punctuated && !asked && !present.empty() && !state && !stative && !verbal) continue;
        // A present action in 해체 reads as a plan, a promise or an order; only soft speech
        // chooses one.
        if (settings_.casualMarkedOnly && !asked && !punctuated && !present.empty() && !state) continue;
        const bool planned = !present.empty() && !stative && !asked && close != U'？' && close != U'?' &&
                             Planned(present, Clause(before() + std::string(own)));
        // 주다 in 해체 (줘) reads as a request; its past (줬어) does not.
        if (!planned && !asked && marker == "&CJ@" && First(it.ending.text) != '5') continue;
        const std::string original = it.ending.text;
        if (keepPast && PastText(original)) continue;
        std::string soft = CasualEnding(original, planned, planned && Promised(Clause(before() + std::string(own))));
        if (politeLine && !soft.empty()) soft = PoliteOf(soft);
        if (soft.empty()) continue;
        // A repeated predicate (終わった終わった) takes the same end.
        for (int j = i - 1;
             j >= 0 && items[j].surface == it.surface && items[j].directIndex >= 0 && items[j].ending.text == original;
             j--) {
            items[j].ending.text = soft;
            state_[j].spoken = true;
        }
        it.ending.text = std::move(soft);
        state_[i].spoken = true;
    }
}

std::string Composer::PresentWord(std::string_view ending, std::string_view stem) const {
    const std::string_view marker = Marker(stem);
    const std::string_view head = stem.substr(0, stem.size() - marker.size());
    std::string word;
    if (ending == "4다" && !marker.empty()) {
        word = std::string(head) + std::string(Lookup(marker[0], std::string(marker) + "4")) + "다";
    } else if (ending == "1는다" && !marker.empty()) {
        word = std::string(head) + std::string(Lookup(marker[0], std::string(marker) + "1")) + "는다";
    } else if (ending.size() > 1) {
        word = std::string(ending.substr(1));
    }
    if (!word.ends_with("다")) return {};
    const char32_t before = LastRune(std::string_view(word).substr(0, word.size() - std::string_view("다").size()));
    const auto syllable = text::Split(before);
    if (before != U'는' && !(syllable && syllable->final == 4)) return {};  // 4: ㄴ
    return word;
}

// だろう or であろう before ？: the guess (것이다) asks for agreement, in casual speech
// without 겠 (行っただろう？ 갔지?, 学生だろう？ 학생이지?) and elsewhere with it (갔겠지?);
// after a question word it wonders (どうなったんだろう？ 어떻게 됐을까?).
bool Composer::AskConjecture(int i, std::string_view clause, std::string_view own, bool casual) {
    // だろうか asks by itself (気のせいだろうか 기분 탓일까).
    if ((own.find("だろ") == std::string_view::npos && own.find("であろ") == std::string_view::npos) ||
        own.find("ろうか") != std::string_view::npos) {
        return false;
    }
    std::vector<ResolvedItem>& items = *items_;
    ResolvedItem& it = items[i];
    const bool wonder = QuestionWord(clause);
    if (it.directIndex < 0) {
        const std::string_view text = Selected(it).text;
        if (it.selected < 0 || (text != "일 것이다" && (text != "일까" || wonder))) return false;
        // Alone in its clause (「だろう？」) it agrees with what was said (그렇지?); after an
        // adnominal (오르고 있는, 한 사람인) it takes 거.
        const bool alone = clause == own;
        std::string_view to = wonder ? "일까" : alone ? (casual ? "그렇지" : "그렇겠지") : casual ? "(이)지" : "(이)겠지";
        if (!alone && Adnominal(items[Spoken(items, i)])) to = wonder ? "걸까" : casual ? "거지" : "것이겠지";
        it.entry.text = to;
        if (it.selected == 0) it.first.text = to;
        return true;
    }
    std::string& ending = it.ending.text;
    const std::string_view guess = "을 것이다";
    std::string to;
    if (ending.ends_with(guess)) {
        const std::string_view head = std::string_view(ending).substr(0, ending.size() - guess.size());
        if (head != "5" && !head.ends_with("있") && !head.ends_with("없")) return false;
        to = std::string(head) + (wonder ? "을까" : casual ? "지" : "겠지");
    } else if (ending == "3 것이다") {
        to = wonder ? "3까" : casual && Stative(Marker(Selected(it).text)) ? "1지" : "1겠지";
    } else if (ending == "1겠지" && wonder) {
        to = "3까";
    } else if (ending == "3까" && casual && !wonder) {
        to = "1지";
    } else {
        return false;
    }
    ending = std::move(to);
    return true;
}

// A plain question in casual speech: か after a noun in 해체 (暗号か？ 암호인가 암호야?), a
// predicate before か in 해체 (大丈夫か？ 괜찮은가 괜찮아?; あるか？ 있을까 있어? when it
// states, not after う: 行けないだろうか 갈 수 없을까), ないか after a verb as an invitation
// (行かないか？ 가지 않겠는가 가지 않을래?) and ってこと as what is meant (行くってこと？
// 간다는 것 간다는 거야?, 見てるってこと？ 보고 있다고 일 보고 있다는 거야?).
bool Composer::AskPlain(int i, std::string_view own) {
    std::vector<ResolvedItem>& items = *items_;
    ResolvedItem& it = items[i];
    auto told = [&](int k) {
        if (k < 1 || items[k].directIndex >= 0 || items[k].selected < 0) return false;
        const std::string_view thing = Selected(items[k]).text;
        std::string& said = items[k - 1].ending.text;
        if ((thing != "것" && thing != "일") || items[k - 1].directIndex < 0) return false;
        if (said.ends_with("다고")) said = said.substr(0, said.size() - std::string_view("고").size()) + "는";
        return said.ends_with("다는");
    };
    if (it.directIndex < 0 && told(i)) {
        it.entry.text = "거야";
        if (it.selected == 0) it.first.text = it.entry.text;
        return true;
    }
    if (it.directIndex < 0 && it.selected >= 0 && Selected(it).text == "(이)다" && told(i - 1)) {
        items[i - 1].entry.text = "거";
        if (items[i - 1].selected == 0) items[i - 1].first.text = "거";
        return false;
    }
    if (it.directIndex >= 0 && it.ending.text.ends_with("다는 것")) {
        it.ending.text.replace(it.ending.text.size() - std::string_view("것").size(), std::string_view("것").size(), "거야");
        return true;
    }
    if (it.directIndex < 0) {
        if (it.selected < 0 || Selected(it).text != "인가" || i == 0) return false;
        // A past adnominal before it (元気だったか 건강했던인가) asks with 어 (건강했어).
        ResolvedItem& previous = items[Spoken(items, i)];
        const char before = First(Selected(previous).code);
        if (previous.directIndex >= 0 && previous.ending.text == "5던") {
            previous.ending.text = "5어";
            state_[Spoken(items, i)].spoken = true;
            it.entry.text = "";
        } else if (Selected(previous).text.ends_with("야")) {  // 용건이야인가 (用だか)
            return false;
        } else if (Adnominal(previous)) {  // 있는 인가 (あるんか)
            it.entry.text = "거야";
        } else if (previous.directIndex < 0 && (before == '1' || before == '9') && !Selected(previous).text.empty() &&
                   !Selected(previous).text.ends_with(" ")) {  // a noun or a phrase (何か用か 무슨 볼일이야)
            it.entry.text = "(이)야";
        } else {
            return false;
        }
        if (it.selected == 0) it.first.text = it.entry.text;
        return true;
    }
    const std::string_view marker = Marker(Selected(it).text);
    std::string& ending = it.ending.text;
    std::string to;
    if (ending == "4가" && marker != "$DH@") {
        to = "6";
    } else if (ending == "1지 않는가" || ending == "1지 않는가 ") {
        to = "1지 않아";
    } else if (ending == "1겠는가" && marker == "$DH@") {
        to = "3래";
    } else if (ending == "3까" && own.ends_with("か") && !own.ends_with("うか") && Stative(marker) && marker != "$DH@" &&
               !Selected(it).text.starts_with("안 ")) {
        // Not after a verb's ない, which may invite (来ないか 오지 않을까), nor 안 될까, which is no
        // 안 돼 (ことにならないか).
        to = "6";
    } else {
        return false;
    }
    ending = std::move(to);
    state_[i].spoken = true;
    return true;
}

// The formal statement -ㅂ니다 before ？ asks with -ㅂ니까, pressed with よ in 해요체 (spec SPC-16:
// 뿐입니다 뿐입니까, 7니다 7니까, 見守るだけですよ？ 뿐이에요?); the proposal -ㅂ시다 asks with -ㄹ까요
// (参りましょう？ 갈까요?). Whether the item changed.
bool Composer::AskFormal(int i, bool pressed) {
    ResolvedItem& it = (*items_)[i];
    const auto asked = [&](std::string_view text) {
        // The military であります stays formal (死んで欲しくないでありますよ？ 원하지 않습니다?).
        if (pressed) return it.endingSurface.find("であり") != std::string::npos ? std::string() : PressedPolite(text);
        std::string to = FormalAsked(text);
        if (to.empty() && text == "9시다") to = "3까요";
        return to;
    };
    if (it.directIndex < 0) {
        if (it.selected < 0) return false;
        const std::string to = asked(Selected(it).text);
        if (to.empty()) return false;
        it.entry.text = to;
        if (it.selected == 0) it.first.text = to;
        return true;
    }
    std::string& ending = it.ending.text;
    const std::size_t kept = ending.find_last_not_of(' ') + 1;  // a trailing space stays (1지 않았습니다 )
    const std::string to = asked(std::string_view(ending).substr(0, kept));
    if (to.empty()) return false;
    ending = to + ending.substr(kept);
    return true;
}

// Whether the family of marker states: an adjective's, or a verb's whose terminal and
// first rows agree (있다).
bool Composer::Stative(std::string_view marker) const {
    if (marker.empty()) return false;
    if (marker[0] != '&') return true;
    const std::string m(marker);
    const std::string_view terminal = Lookup('&', m + "4");
    return !terminal.empty() && terminal == Lookup('&', m + "1");
}

// 무엇 and 어째서 in a casual question are spoken 뭐 and 왜 (何を書いてるの？ 뭘 쓰고 있는
// 거야?, 何かあった？ 뭔가 있었어?, なんで来たの？ 왜 온 거야?).
void Composer::AskWord(int i, int last) {
    std::vector<ResolvedItem>& items = *items_;
    ResolvedItem& it = items[i];
    if (it.selected < 0) return;
    const std::string_view own = Selected(it).text;
    std::string to;
    if (own == "무엇") {
        to = "뭐";
    } else if (own == "무엇인가") {
        to = "뭔가";
    } else if (own == "어째서") {
        to = "왜";
    } else {
        // A verb after it (何がある 무엇이 &HN@).
        for (const auto& [from, spoken] : {std::pair<std::string_view, std::string_view>{"무엇이 ", "뭐가 "},
                                           {"무엇을 ", "뭘 "}, {"무엇인가 ", "뭔가 "}, {"무엇 ", "뭐 "}, {"어째서 ", "왜 "}}) {
            if (own.starts_with(from)) to = std::string(spoken) + std::string(own.substr(from.size()));
        }
        if (to.empty()) return;
    }
    // The sentence the item is in ends in ？.
    const std::string_view src = source_[it.tokenIndex];
    const std::size_t end = static_cast<std::size_t>(it.offset + it.length);
    std::string after(end < src.size() ? src.substr(end) : std::string_view());
    for (int t = it.tokenIndex + 1; t <= last; t++) after += source_[t];
    const std::size_t following = static_cast<std::size_t>(last) + 1;
    if (following < tokens_->size()) after += (*tokens_)[following].text;
    bool asks = false;
    for (std::string_view s = after; !s.empty();) {
        const text::Rune r = text::DecodeRune(s);
        if (r.size == 0) return;
        s.remove_prefix(r.size);
        if (r.cp == U'？' || r.cp == U'?' || r.cp == U'⁉' || r.cp == U'⁈') {
            asks = true;
            break;
        }
        if (r.cp == U'！' || r.cp == U'!') {
            asks = s.starts_with("？") || s.starts_with("?");
            break;
        }
        if (std::u32string_view(U"。．」』「『\n").find(r.cp) != std::u32string_view::npos) break;
    }
    if (!asks) return;
    const int n = static_cast<int>(items.size());
    const auto entry = [&](int k) {
        return k < n && items[k].directIndex < 0 && items[k].selected >= 0 ? Selected(items[k]).text : std::string_view();
    };
    // 왜 takes no copula or particle (なんでだ 어째서야, なんでって 어째서는, not 왜야, 왜는).
    if (to == "왜" && First(i + 1 < n && items[i + 1].selected >= 0 ? Selected(items[i + 1]).code : "") == '2' &&
        (entry(i + 1).starts_with("(") || internal::HasSyllable(entry(i + 1)))) {
        return;
    }
    if (to == "뭐") {
        // 뭐 takes a particle, a verb (何描いてる 뭐 그려) or ends the question (何？ 뭐?); a noun
        // after it would join it (何あれ 뭐저것), and so would a noun before it (ごはん何 밥뭐);
        // 뭐가 before a copula is no question (何がだ 뭐가야).
        const std::string_view particle = entry(i + 1);
        bool ends = i + 1 >= n;
        if (!ends && items[i + 1].tokenIndex != it.tokenIndex) {
            const std::string_view next = source_[items[i + 1].tokenIndex];
            ends = !next.empty() && kEnds.find(text::DecodeRune(next).cp) != std::u32string_view::npos;
        }
        const bool verb = i + 1 < n && items[i + 1].directIndex >= 0;
        if (!ends && !verb && particle != "(을)를" && particle != "(이)가" && particle != "(으)로" && particle != "에") return;
        if ((i > 0 && items[i - 1].directIndex < 0 && First(Selected(items[i - 1]).code) == '1' &&
             !Selected(items[i - 1]).text.empty()) ||
            (particle == "(이)가" && entry(i + 2).starts_with("(이)"))) {
            return;
        }
        // 뭘 for 무엇을; the space of を goes before the word after it, none before an end mark
        // (何を……？ 뭘……?), and not before a comma, whose particle after it reads the one before
        // (何を、とは 뭐를, 이란).
        if (particle == "(을)를") {
            const ResolvedItem& p = items[i + 1];
            const std::string_view ps = source_[p.tokenIndex];
            const std::size_t at = static_cast<std::size_t>(p.offset + p.length);
            std::string rest(at < ps.size() ? ps.substr(at) : std::string_view());
            for (int t = p.tokenIndex + 1; t <= last && rest.empty(); t++) rest = source_[t];
            const char32_t next = rest.empty() ? 0 : text::DecodeRune(rest).cp;
            if (next != U'、' && next != U'，' && next != U',') {
                to = "뭘";
                const bool word = next != 0 && kEnds.find(next) == std::u32string_view::npos;
                items[i + 1].entry.text = word ? " " : "";
                if (items[i + 1].selected == 0) items[i + 1].first.text = items[i + 1].entry.text;
            }
        }
    }
    it.entry.text = to;
    if (it.selected == 0) it.first.text = to;
}

}  // namespace hdTrans::output
