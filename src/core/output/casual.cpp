// Casual speech: the plain sentence ends (다) that the rules chose for the predicates of
// speech, mapped to the casual style (해체) so that the tense and mood the rules chose are
// kept.
#include <string>
#include <string_view>

#include "core/output/compose.h"
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
        if (r.cp == U'「' || r.cp == U'『') {
            if (depth++ == 0) {
                polite.push_back(false);
                own = polite.size() - 1;
            }
            continue;
        }
        if ((r.cp == U'」' || r.cp == U'』') && depth > 0) {
            if (--depth == 0) own = 0;
            continue;
        }
        if (depth > 1 || polite[own]) continue;
        for (const std::string_view end : {"요", "죠", "니다", "시오"}) {
            if (!line.substr(0, at).ends_with(end)) continue;
            std::string_view after = line.substr(at);
            for (text::Rune d = text::DecodeRune(after);
                 d.size > 0 && std::u32string_view(U"~～―ー─-♪").find(d.cp) != std::u32string_view::npos;
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
                          std::u32string_view(U".,!?…‥」』。、！？").find(next) != std::u32string_view::npos)) {
                polite[own] = true;
            }
        }
    }
    return polite;
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

// Present verbs that state, not plan, written in 해체 (모른다 몰라, 생각한다 생각해).
constexpr std::string_view kStates[] = {
    "된다",     "모른다",   "안다",     "생각한다", "좋아한다", "싫어한다", "사랑한다", "보인다", "들린다",
    "바뀐다",   "걸린다",   "끝난다",   "열린다",   "부탁한다", "느낀다",   "바란다",   "믿는다", "원한다",
    "이해한다", "감사한다", "축하한다", "않는다",   "생각난다", " 든다",    "쓰인다",   "떨린다", "고 한다",
    "야 한다",  "면 한다",
};

// Japanese verbs that state (違う, 似る; 高すぎる after an adjective), whatever their Korean.
constexpr std::string_view kStateSurfaces[] = {"違", "似", "足り", "異な", "すぎ", "過ぎ", "見え", "聞こえ"};

// Japanese nouns of declarations and verdicts, whose する and だ keep 다 (破棄する 파기한다).
constexpr std::string_view kDeclarations[] = {
    "宣言", "却下", "許可", "禁止", "破棄", "任命", "撤回", "謝罪", "通達", "布告", "命令", "追放", "承認", "認可",
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
// (雨が降る 비가 와, 誰が行く 누가 가); the verbs of kStates state.
bool Planned(std::string_view word, std::string_view clause) {
    for (const std::string_view s : kStates) {
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
// With the closing marks of the sound stage (kEmphasisClose, kLengthenMark).
constexpr std::u32string_view kSkipped = U"っッーぇぁ\uE009\uE00A";

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

// The family marker that ends a conjugating target (생각&AV@), "" for none.
std::string_view Marker(std::string_view stem) {
    if (stem.size() < 4 || !stem.ends_with("@")) return {};
    const std::string_view m = stem.substr(stem.size() - 4);
    return m[0] == '&' || m[0] == '$' || m[0] == '%' ? m : std::string_view();
}

}  // namespace

char32_t Composer::QuotedEnd(int i, int last, bool nested) const {
    const ResolvedItem& it = (*items_)[i];
    const std::string_view own = source_[it.tokenIndex];
    const std::size_t end = static_cast<std::size_t>(it.offset + it.length);
    std::string after(end < own.size() ? own.substr(end) : std::string_view());
    for (int t = it.tokenIndex + 1; t <= last; t++) after += source_[t];
    const std::size_t following = static_cast<std::size_t>(last) + 1;
    if (following < tokens_->size()) after += (*tokens_)[following].text;
    std::string_view s = after;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.size == 0) return 0;
        s.remove_prefix(r.size);
        if (kSkipped.find(r.cp) != std::u32string_view::npos || text::IsSpace(r.cp)) continue;
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
        return r.cp;
    }
    return U'\n';
}

void Composer::WriteCasual(int first, int last) {
    std::vector<ResolvedItem>& items = *items_;
    for (int i = 0; i < static_cast<int>(items.size()); i++) {
        ResolvedItem& it = items[i];
        if (it.tokenIndex < first || it.tokenIndex > last) continue;
        const std::string_view src = source_[it.tokenIndex];
        const std::size_t start = std::min<std::size_t>(static_cast<std::size_t>(it.offset), src.size());
        const std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(it.offset + it.length), src.size());
        const std::string_view own = src.substr(start, end - start);
        std::string before;
        for (int t = first; t < it.tokenIndex; t++) before += source_[t];
        before += src.substr(0, start);
        // Speech: inside a quote at the start and at the end of the item; not polite or old
        // speech.
        const QuoteState at = QuotesAfter(settings_.quotes, before);
        if (at.depth == 0 || QuotesAfter(at, own).depth == 0) continue;
        const std::size_t quote = static_cast<std::size_t>(at.count);
        const std::uint8_t style = settings_.quoteStyles != nullptr && quote < settings_.quoteStyles->size()
                                       ? (*settings_.quoteStyles)[quote]
                                       : 0;
        if ((style & ~kCasualSpeech) != 0) continue;
        // Marked casual quotes: their own text only, not a quote inside them (『書いた』って).
        if (settings_.casualMarkedOnly && ((style & kCasualSpeech) == 0 || at.depth > 1)) continue;
        const char32_t close = QuotedEnd(i, last, at.depth > 1);
        // である reports (優勢である) and declarations (破棄する) keep their style.
        if (close == 0 || own.find("であ") != std::string_view::npos ||
            (before.ends_with("で") && own.starts_with("あ"))) {
            continue;
        }
        // 白紙撤回とする, 王太子妃とする: a declaration.
        bool declared = own.starts_with("する") && before.ends_with("と");
        for (const std::string_view d : kDeclarations) {
            declared = declared || own.starts_with(d) || before.ends_with(d) ||
                       before.ends_with(std::string(d) + "と") || before.ends_with(std::string(d) + "を");
        }
        if (declared) continue;
        const char32_t open = LastNonSpace(before);
        const bool alone = open == 0 || std::u32string_view(U"「『。！？!?…‥").find(open) != std::u32string_view::npos;
        const bool exclaimed = close == U'！' || close == U'!';
        if (it.directIndex < 0) {
            // A copula alone in its clause (「だな」, しかし、だ), after a stutter (だだだ) or
            // after a particle (ユフィリアもだ) is no predicate of a sentence.
            const std::string_view to = Find(kCopulas, Selected(it).text);
            if (to.empty() || it.selected < 0 || alone || open == U'、' || open == U'だ' ||
                (i > 0 && First(Selected(items[i - 1]).code) == '2')) {
                continue;
            }
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
        // Kept: the copula verbs (이어); a past predicate alone in its sentence before ！ or an
        // ellipsis, an exclamation (助かった…… 살아났다……); a planned action before ！ (来る！
        // 온다!).
        if (!marker.empty() && Lookup(marker[0], std::string(marker) + "6").ends_with("이어")) continue;
        if (alone && First(it.ending.text) == '5' && (exclaimed || close == U'…' || close == U'‥')) continue;
        // Marked casual quotes keep 너무 X다 (露骨すぎだ, 握りすぎだ), whose 해체 is no word,
        // and 待った (잠깐!), whose past would be read.
        if (settings_.casualMarkedOnly) {
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
        for (const std::string_view v : kStateSurfaces) {
            stative = stative || std::string_view(it.surface).starts_with(v);
        }
        bool state = false;
        for (const std::string_view s : kStates) state = state || present.ends_with(s);
        // A present action in 해체 reads as a plan, a promise or an order; only soft speech
        // chooses one.
        if (settings_.casualMarkedOnly && !present.empty() && !state) continue;
        const bool planned = !present.empty() && !stative && close != U'？' && close != U'?' &&
                             Planned(present, Clause(before + std::string(own)));
        // 주다 in 해체 (줘) reads as a request.
        if ((planned && exclaimed) || (!planned && marker == "&CJ@")) continue;
        const std::string original = it.ending.text;
        std::string soft = CasualEnding(original, planned, planned && Promised(Clause(before + std::string(own))));
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

}  // namespace hdTrans::output
