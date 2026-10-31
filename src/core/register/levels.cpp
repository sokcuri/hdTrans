#include "core/register/levels.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "core/register/generate.h"
#include "core/register/hangul.h"

namespace hdTrans::reg {

using hangul::ToU32;
using hangul::ToU8;

namespace {

bool IsMark(char32_t c) {
    switch (c) {
        case U'。': case U'．': case U'！': case U'？': case U'!': case U'?': case U'…': case U'‥': case U'ー':
        case U'〜': case U'～': case U'っ': case U'ッ': case U'・': case U'♪': case U'♥': case U' ': case U'　':
        case U'」': case U'』': case U'）': case U')': case U']': case U'＞': case U'>': case U'、': case U'，':
        case 0xFF61: case 0xFF64:
            return true;
        default:
            return false;
    }
}
bool IsSentenceEnd(char32_t c) { return c == U'。' || c == U'！' || c == U'？' || c == U'!' || c == U'?' || c == 0xFF61; }
bool IsWordChar(char32_t c) {
    return (c >= U'ぁ' && c <= U'ゖ') || (c >= U'ァ' && c <= U'ヺ') || (c >= 0x4E00 && c <= 0x9FFF) || (c >= U'a' && c <= U'z') ||
           (c >= U'A' && c <= U'Z') || (c >= U'ａ' && c <= U'ｚ') || (c >= U'Ａ' && c <= U'Ｚ') || (c >= U'0' && c <= U'9');
}
bool HasWord(std::u32string_view s) { return std::any_of(s.begin(), s.end(), IsWordChar); }
bool EndsWith(std::u32string_view s, std::u32string_view e) { return s.ends_with(e); }
bool StartsAt(std::u32string_view s, std::size_t i, std::u32string_view e) { return s.substr(i).starts_with(e); }

// Whether s ends in the tail particle, not right after one of its notAfter characters (the ぞ of どうぞ);
// notAfter holds characters, or words separated by | (どう: the ぞ of どうぞ, while 参ろうぞ keeps its ぞ).
bool EndsInTail(std::u32string_view s, const TailRow& tail) {
    if (s.size() <= tail.text.size() || !EndsWith(s, tail.text)) return false;
    const std::u32string_view head = s.substr(0, s.size() - tail.text.size());
    if (tail.notAfter.find(U'|') != std::u32string::npos) {
        for (std::size_t a = 0; a <= tail.notAfter.size();) {
            std::size_t b = tail.notAfter.find(U'|', a);
            if (b == std::u32string::npos) b = tail.notAfter.size();
            const std::u32string_view w = std::u32string_view(tail.notAfter).substr(a, b - a);
            if (!w.empty() && EndsWith(head, w)) return false;
            a = b + 1;
        }
        return true;
    }
    return tail.notAfter.find(head.back()) == std::u32string::npos;
}

void PopMarks(std::u32string& s) {
    while (!s.empty() && IsMark(s.back())) s.pop_back();
}

// The utterance of a line: the text of its first quote to the last closing mark of that kind.
struct Utt {
    std::u32string text;
    bool narr = false;  // narration follows the quote: 「…」と言った
};

Utt Utterance(const std::u32string& ja) {
    Utt u;
    std::size_t open = std::u32string::npos;
    for (std::size_t i = 0; i < ja.size(); ++i)
        if (ja[i] == U'「' || ja[i] == U'『' || ja[i] == U'≪') {
            open = i;
            break;
        }
    if (open == std::u32string::npos) {
        u.text = ja;
        return u;
    }
    const char32_t closeCh = ja[open] == U'「' ? U'」' : (ja[open] == U'≪' ? U'≫' : U'』');
    const std::size_t close = ja.rfind(closeCh);
    if (close == std::u32string::npos || close < open) {
        u.text = ja.substr(open + 1);
        return u;
    }
    u.text = ja.substr(open + 1, close - open - 1);
    for (std::size_t i = close + 1; i < ja.size(); ++i)
        if (!IsMark(ja[i]) && ja[i] != U'」' && ja[i] != U'』' && IsWordChar(ja[i])) {
            u.narr = true;
            break;
        }
    return u;
}

// The last sentence of u that holds a word.
std::u32string LastSentence(const std::u32string& u) {
    std::vector<std::u32string> pieces;
    std::u32string cur;
    for (const char32_t c : u) {
        cur.push_back(c);
        if (IsSentenceEnd(c)) {
            pieces.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) pieces.push_back(cur);
    for (auto it = pieces.rbegin(); it != pieces.rend(); ++it)
        if (HasWord(*it)) return *it;
    return u;
}

// The top-level quotes of a line, each with its brackets; none when the line holds fewer than two, or
// narration stands before or between them.
std::vector<std::string> SplitQuotes(const std::string& ja) {
    const std::u32string s = ToU32(ja);
    {
        // text before the first quote may only be a speaker tag: 【name】, [name], (name), or a short bare name
        std::size_t first = 0;
        while (first < s.size() && s[first] != U'「' && s[first] != U'『' && s[first] != U'≪') ++first;
        std::u32string pre = s.substr(0, first);
        while (!pre.empty() && (pre.back() == U' ' || pre.back() == U'　')) pre.pop_back();
        std::size_t lead = 0;
        while (lead < pre.size() && (pre[lead] == U' ' || pre[lead] == U'　')) ++lead;
        pre = pre.substr(lead);
        const bool bracketed = pre.size() >= 2 && pre.size() <= 26 &&
                               ((pre.front() == U'【' && pre.back() == U'】') || (pre.front() == U'[' && pre.back() == U']') ||
                                (pre.front() == U'（' && pre.back() == U'）') || (pre.front() == U'(' && pre.back() == U')'));
        bool bareName = pre.size() <= 12;
        for (const char32_t ch : pre)
            if (ch == U'、' || ch == U'。' || ch == U'，' || (ch >= U'ぁ' && ch <= U'ゖ')) bareName = false;
        if (!pre.empty() && !bracketed && !bareName) return {};
    }
    std::vector<std::string> out;
    int depth = 0;
    std::size_t start = 0;
    char32_t open = 0, close = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char32_t ch = s[i];
        if (depth == 0 && (ch == U'「' || ch == U'『' || ch == U'≪')) {
            open = ch;
            close = ch == U'「' ? U'」' : (ch == U'『' ? U'』' : U'≫');
            start = i;
            depth = 1;
        } else if (depth > 0 && ch == open) {
            ++depth;
        } else if (depth > 0 && ch == close && --depth == 0) {
            out.push_back(ToU8(s.substr(start, i - start + 1)));
        } else if (depth == 0 && IsWordChar(ch) && !out.empty()) {
            return {};  // text between quotes (「…」と言った「…」) is narration
        }
    }
    return out;
}

struct Cues {
    bool strongTail = false;  // a sentence of the line ends in よ, ね, ぞ …
    bool lastStrong = false;  // the last sentence ends in one of them (its outermost tail particle)
    bool lastKa = false;      // the last sentence ends in the question particle か
    bool plainDecl = false;   // the last sentence ends in a plain statement (だ, る, た, い, ん), no particle, no ？
    bool exclaim = false;     // REG-13: every sentence is a bare predicate alone or has no predicate
    bool auth = false;        // REG-14: the line has an authoritative voice cue
    bool trail = false;       // the last sentence trails off after a て-form (言われて……)
    bool outburst = false;    // REG-1: the cues are those of the narrator's outburst after a quote
    bool casualMark = false;  // REG-20: a casual cue: a sentence-final particle, a contraction, a casual pronoun or address
    bool trailConj = false;   // the last clause trails off after a conjunction (けど……, が……, て……)
    bool honSonkei = false, honGive = false;  // REG-16
    bool declZo = false;      // REG-17: a plain statement declared with ぞ or ぜ
    bool exclaimNe = false;   // REG-18: a predicate exclaimed with な, なぁ, なあ
    std::vector<std::pair<std::size_t, std::size_t>> authParts;  // parts of speaker auth rows found (row, part)
    bool narr = false, empty = false, set = false, keigo = false, pend = false, pany = false, arch = false, stiff = false,
         cend = false;
    std::string first;
};

Cues Scan(const RegisterTables& t, const std::string& ja) {
    Cues c;
    const Utt u = Utterance(ToU32(ja));
    c.narr = u.narr;
    // REG-1: the text after the quote is narration, unless it is itself spoken: a casual sentence-final particle at
    // its end (「…」じゃねーわ！！, the narrator's own outburst) makes the line speech. The outburst then decides the
    // line's cues, and the quote is another voice whose level stays (RegDecision::holdQuote).
    if (c.narr) {
        const std::u32string all = ToU32(ja);
        const std::size_t close = all.find_last_of(U"」』≫");
        if (close != std::u32string::npos) {
            const std::u32string afterAll = all.substr(close + 1);
            std::u32string after = LastSentence(afterAll);
            for (bool again = true; again;) {
                again = false;
                while (!after.empty() && (IsMark(after.back()) || after.back() == U'ー')) {
                    after.pop_back();
                    again = true;
                }
                if (after.size() >= 2 && std::u32string_view(U"ぁぃぅぇぉ").find(after.back()) != std::u32string_view::npos &&
                    after[after.size() - 2] >= U'ぁ' && after[after.size() - 2] <= U'ゖ') {
                    after.pop_back();
                    again = true;
                }
            }
            bool spoken = false;
            for (const auto& tail : t.tails)
                if (tail.strong && EndsInTail(after, tail)) spoken = true;
            if (spoken && afterAll.find_first_of(U"「『≪") == std::u32string::npos) {
                Cues o = Scan(t, ToU8(afterAll));
                o.outburst = true;
                return o;
            }
        }
    }
    c.empty = !HasWord(u.text);
    // the last sentence without its closing marks, then without a small vowel that lengthens the last kana
    // (なぁ, よぉ, ねぇ), then without marks again; then 0..n tail particles stripped, one stage each
    std::u32string last = LastSentence(u.text);
    PopMarks(last);
    while (last.size() >= 2 &&
           (last.back() == U'ぁ' || last.back() == U'ぃ' || last.back() == U'ぅ' || last.back() == U'ぇ' || last.back() == U'ぉ') &&
           last[last.size() - 2] >= U'ぁ' && last[last.size() - 2] <= U'ゖ')
        last.pop_back();
    PopMarks(last);
    // an elongation mark between kana is not part of the word (でーす, ますぅ)
    for (std::size_t k = 1; k + 1 < last.size();) {
        const bool kanaL = last[k - 1] >= U'ぁ' && last[k - 1] <= U'ヺ';
        const bool kanaR = last[k + 1] >= U'ぁ' && last[k + 1] <= U'ヺ';
        if ((last[k] == U'ー' || last[k] == U'〜' || last[k] == U'～') && kanaL && kanaR) last.erase(k, 1);
        else ++k;
    }
    std::vector<std::u32string> stages{last};
    bool tailStripped = false;
    for (int guard = 0; guard < 6; ++guard) {
        const std::u32string s = stages.back();
        const TailRow* best = nullptr;
        for (const auto& tail : t.tails)
            if ((!best || tail.text.size() > best->text.size()) && EndsInTail(s, tail)) best = &tail;
        if (!best) break;
        if (stages.size() == 1 && best->strong) c.lastStrong = true;
        if (stages.size() == 1 && best->text == U"か") c.lastKa = true;
        if (stages.size() == 1) {
            std::u32string rest = s.substr(0, s.size() - best->text.size());
            PopMarks(rest);
            const char32_t e = rest.empty() ? 0 : rest.back();
            constexpr std::u32string_view kDecl = U"だたいるうくすつぬぶむぐん";
            if ((best->text == U"ぞ" || best->text == U"ぜ") && kDecl.find(e) != std::u32string_view::npos) c.declZo = true;
            // な on a verb's dictionary form forbids (行くな); on い, た, だ it exclaims, but not after a wish
            // (ほしいな, 行きたいな); the desire たい follows a verb stem, not the adjectives 冷たい, 重たい …
            constexpr std::u32string_view kStem = U"いきぎしじちにひびみりえけげせぜてでねべめれ見来寝出着居似煮";
            constexpr std::u32string_view kTaiAdj[] = {U"冷たい", U"重たい", U"眠たい", U"めでたい", U"ありがたい",
                                                       U"つめたい", U"おもたい", U"ねむたい", U"後ろめたい", U"うしろめたい"};
            bool taiAdj = false;
            for (const auto& a : kTaiAdj)
                if (EndsWith(rest, a)) taiAdj = true;
            const bool desire = EndsWith(rest, U"たい") && !taiAdj && rest.size() >= 3 &&
                                kStem.find(rest[rest.size() - 3]) != std::u32string_view::npos;
            const bool wish = desire || EndsWith(rest, U"ほしい") || EndsWith(rest, U"欲しい") ||
                              EndsWith(rest, U"また");  // またな is a farewell
            if ((best->text == U"な" || best->text == U"なあ") && (e == U'い' || e == U'た' || e == U'だ') && !wish)
                c.exclaimNe = true;
        }
        std::u32string next = s.substr(0, s.size() - best->text.size());
        PopMarks(next);
        stages.push_back(next);
        tailStripped = true;
    }
    bool casualEnd = tailStripped;
    {
        // a strong sentence-final particle at the end of any sentence of the utterance (…だよ。…しろ。)
        const auto check = [&](std::u32string x) {
            PopMarks(x);
            for (const auto& tail : t.tails)
                if (tail.strong && EndsInTail(x, tail)) c.strongTail = true;
        };
        std::u32string sent;
        for (const char32_t ch : u.text) {
            if (IsSentenceEnd(ch)) {
                check(sent);
                sent.clear();
            } else {
                sent.push_back(ch);
            }
        }
        check(sent);
    }
    for (const auto& r : t.cues) {
        bool hit = false;
        const auto before = [&](std::u32string_view str, std::size_t at) {
            // a polite auxiliary (ます, ました) cannot start the utterance, and ます beside ます is ますます
            const bool aux = r.cue == "polite" || r.cue == "polite-any";
            if (aux && r.text.starts_with(U"ま")) {
                if (at == 0) return true;
                if (StartsAt(str, at, U"ますます") || (at >= 2 && str.substr(at - 2, 2) == U"ます")) return true;
            }
            return at > 0 && !r.notAfter.empty() && r.notAfter.find(str[at - 1]) != std::u32string::npos;
        };
        if (r.mode == "end") {
            for (const auto& s : stages)
                if (EndsWith(s, r.text) && !before(s, s.size() - r.text.size())) hit = true;
        } else {
            for (std::size_t i = u.text.find(r.text); i != std::u32string::npos; i = u.text.find(r.text, i + 1)) {
                if (before(u.text, i)) continue;
                const bool followed = std::any_of(r.notBefore.begin(), r.notBefore.end(), [&](const std::u32string& w) {
                    return StartsAt(u.text, i + r.text.size(), w);
                });
                if (followed) continue;
                if (r.mode == "any-not-quoted") {
                    const std::size_t j = i + r.text.size();
                    bool quoted = false;
                    for (const auto& q : t.quotatives)
                        if (StartsAt(u.text, j, q)) quoted = true;
                    if (quoted) continue;
                }
                hit = true;
                break;
            }
        }
        if (!hit) continue;
        if (r.cue == "polite") c.pend = true;
        else if (r.cue == "polite-any") c.pany = true;
        else if (r.cue == "keigo") c.keigo = true;
        else if (r.cue == "set") c.set = true;
        else if (r.cue == "archaic") c.arch = true;
        else if (r.cue == "stiff") c.stiff = true;
        else if (r.cue == "casual") casualEnd = true;
        else if (r.cue == "casual-mark") c.casualMark = true;
        else if (r.cue.starts_with("first:") && c.first.empty()) c.first = r.cue.substr(6);
    }
    // the last sentence has no end cue (a name called out: 落ち着きなさい！ 森下っ！！): the nearest earlier
    // sentence with one decides
    if (!c.pend && !casualEnd) {
        std::vector<std::u32string> sents;
        std::u32string cur;
        for (const char32_t ch : u.text) {
            if (IsSentenceEnd(ch)) {
                sents.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
        sents.push_back(cur);
        while (!sents.empty()) {  // drop the last sentence with a word (the one already read)
            const bool w = HasWord(sents.back());
            sents.pop_back();
            if (w) break;
        }
        for (auto it = sents.rbegin(); it != sents.rend() && !c.pend && !casualEnd; ++it) {
            std::u32string x = *it;
            PopMarks(x);
            if (x.empty()) continue;
            bool tail = false;
            for (const auto& tr : t.tails)
                if (EndsInTail(x, tr)) tail = true;
            for (const auto& r : t.cues) {
                if (r.mode != "end" || !EndsWith(x, r.text) || x.size() <= r.text.size()) continue;
                if (!r.notAfter.empty() && r.notAfter.find(x[x.size() - r.text.size() - 1]) != std::u32string::npos) continue;
                if (r.cue == "polite") c.pend = true;
                else if (r.cue == "casual") casualEnd = true;
            }
            if (tail) casualEnd = true;
        }
    }
    c.cend = casualEnd && !c.pend;
    if (tailStripped || c.strongTail) c.casualMark = true;
    {
        const std::u32string raw = LastSentence(u.text);
        std::size_t e = raw.size();
        while (e > 0 && (raw[e - 1] == U' ' || raw[e - 1] == U'　' || raw[e - 1] == U'」' || raw[e - 1] == U'』')) --e;
        std::size_t dots = e;
        while (dots > 0 && (raw[dots - 1] == U'…' || raw[dots - 1] == U'‥')) --dots;
        c.trail = dots < e && dots > 0 && (raw[dots - 1] == U'て' || raw[dots - 1] == U'で');
        // a clause trailing off after a conjunction (ですけど……, ますが……, まして……): no sentence end to set a level on
        const std::u32string_view head = std::u32string_view(raw).substr(0, dots);
        for (const std::u32string_view conj : {U"けど", U"けれど", U"が", U"て", U"で", U"のに", U"から", U"ので", U"し"})
            if (dots < e && EndsWith(head, conj)) c.trailConj = true;
    }
    // REG-14: a plain statement without a sentence-final particle (思えない, ファンだ, 戦ってみたかった)
    {
        std::u32string raw = LastSentence(u.text);
        while (!raw.empty() && (raw.back() == U' ' || raw.back() == U'　' || raw.back() == U'」' || raw.back() == U'』'))
            raw.pop_back();
        const bool question = !raw.empty() && (raw.back() == U'？' || raw.back() == U'?');
        std::u32string x = stages.front();
        while (!x.empty() && (x.back() == U'っ' || x.back() == U'ッ' || x.back() == U'ー')) x.pop_back();
        constexpr std::u32string_view kEnds = U"だたいるうくすつぬぶむぐん";
        constexpr std::u32string_view kORow = U"おこごそぞとどのほぼぽもよょろ";
        const bool volitional = x.size() >= 2 && x.back() == U'う' && kORow.find(x[x.size() - 2]) != std::u32string_view::npos &&
                                !EndsWith(x, U"だろう") && !EndsWith(x, U"であろう");  // conjecture, not volition
        c.plainDecl = !question && !volitional && !tailStripped && !x.empty() && kEnds.find(x.back()) != std::u32string_view::npos;
    }
    // REG-13: an exclamation of discovery or realisation: bare predicates alone (見つけた！, 面白い。, みっけ……あれ？),
    // with no sentence-final particle; a piece without a predicate (あれ？) may stand beside them
    {
        std::vector<std::u32string> pieces;
        std::vector<bool> asked;  // the piece ends in ？
        std::u32string cur;
        for (const char32_t ch : u.text) {
            if (IsSentenceEnd(ch) || ch == U'…' || ch == U'‥' || ch == U'、' || ch == U'，') {
                if (ch == U'？' || ch == U'?') {
                    if (!pieces.empty() && cur.empty()) {
                        asked.back() = true;  // ！？ after the piece
                    } else {
                        pieces.push_back(cur);
                        asked.push_back(true);
                        cur.clear();
                        continue;
                    }
                }
                if (cur.empty()) continue;
                pieces.push_back(cur);
                asked.push_back(false);
                cur.clear();
            } else if (ch != U'ー' && ch != U'〜' && ch != U'～' && ch != U' ' && ch != U'　' && ch != U'♪' && ch != U'♥') {
                cur.push_back(ch);
            }
        }
        pieces.push_back(cur);
        asked.push_back(false);
        constexpr std::u32string_view kInterj[] = {U"はい", U"おい", U"えい", U"ほい", U"あい", U"うい"};
        bool any = false, ok = true;
        for (std::size_t pi = 0; pi < pieces.size(); ++pi) {
            std::u32string p = pieces[pi];
            while (!p.empty() && (p.back() == U'っ' || p.back() == U'ッ' || p.back() == U'！' || p.back() == U'!')) p.pop_back();
            if (!HasWord(p)) continue;
            if (p.size() > 6) {
                ok = false;
                break;
            }
            bool pred = std::find(t.exclaim.begin(), t.exclaim.end(), p) != t.exclaim.end();
            const char32_t prev = p.size() >= 2 ? p[p.size() - 2] : 0;
            const bool kanjiPrev = prev >= 0x4E00 && prev <= 0x9FFF;
            const bool honorificNoun = p[0] == U'お' || p[0] == U'ご' || EndsWith(p, U"願い");  // お願い, お帰りなさい
            // past: 見つけた, 勝った, 来た: た after an i- or e-row kana, っ or a kanji (not また, あなた)
            constexpr std::u32string_view kPastPrev = U"いきぎしじちぢにひびぴみりえけげせぜてでねへべぺめれっ";
            if (!pred && p.size() >= 2 && p.back() == U'た' && (kanjiPrev || kPastPrev.find(prev) != std::u32string_view::npos))
                pred = true;
            // adjective: 面白い, 怖い, いい, しょっぱい, すごい (not なさい, ない, お願い, りょうかい, はい)
            constexpr std::u32string_view kAdjPrev = U"しいぱごどずぶまる";
            if (!pred && p.size() >= 2 && p.back() == U'い' && !honorificNoun &&
                (kanjiPrev || kAdjPrev.find(prev) != std::u32string_view::npos) &&
                std::find(std::begin(kInterj), std::end(kInterj), p) == std::end(kInterj))
                pred = true;
            // a noun and だ of realisation: ホントだ, そうだ (not the explanatory んだ)
            const bool kataPrev = prev >= U'ァ' && prev <= U'ヺ';
            if (!pred && p.size() >= 3 && p.back() == U'だ' && (kanjiPrev || kataPrev || p == U"そうだ")) pred = true;
            // a verb's dictionary form that is the whole utterance: 断る; an adverb in -く or -ぐ is no verb (早く,
            // まったく, しばらく, すぐ)
            constexpr std::u32string_view kVerbEnd = U"うくぐすつぬぶむる";
            constexpr std::u32string_view kAdvKu = U"しよかたらすき早遅強弱高低深浅長短広狭多少近遠暗明熱冷寒暑重軽固細太新古若薄厚濃全";
            const bool adverb = (p.back() == U'く' || p.back() == U'ぐ') && kAdvKu.find(prev) != std::u32string_view::npos;
            if (!pred && !adverb && pieces.size() == 1 && p.size() >= 2 && p.size() <= 4 &&
                kVerbEnd.find(p.back()) != std::u32string_view::npos &&
                (kanjiPrev || (prev >= U'ぁ' && prev <= U'ゖ')) && p != U"そう" && p != U"どう")
                pred = true;
            if (asked[pi]) pred = false;  // a question asks, it does not exclaim (読んでいい？, できた？)
            if (pred) any = true;
            else if (p.size() > 3) ok = false;  // a piece with a particle or another ending (行くよ, それは)
        }
        c.exclaim = ok && any;
        // musing over one's own words: a predicate, a pause, then か (信じる……か, そういうこと……か)
        std::u32string raw = LastSentence(u.text);
        bool asks = false;
        while (!raw.empty() && IsMark(raw.back()) && raw.back() != U'…' && raw.back() != U'‥') {
            if (raw.back() == U'？' || raw.back() == U'?') asks = true;
            raw.pop_back();
        }
        std::size_t sentences = 0;  // the musing is the utterance's only sentence with a word
        std::u32string cur2;
        for (const char32_t ch : u.text) {
            cur2.push_back(ch);
            if (IsSentenceEnd(ch)) {
                if (HasWord(cur2)) ++sentences;
                cur2.clear();
            }
        }
        if (HasWord(cur2)) ++sentences;
        if (!asks && sentences <= 1 && raw.size() >= 3 && raw.back() == U'か' &&
            (raw[raw.size() - 2] == U'…' || raw[raw.size() - 2] == U'‥'))
            c.exclaim = true;
    }
    // REG-14: a vocative with よ opening the line on a title, or on a katakana name after a rank in kanji (副将アデムよ、,
    // 王よ、): the address of a lord; casual katakana words (ダメよ、, ゴメンよ、) never follow a rank
    {
        const std::size_t v = u.text.find(U"よ、");
        if (v != std::u32string::npos && v >= 1 && v <= 12) {
            constexpr std::u32string_view kTitle = U"将王帝卿殿様師長官者君神";
            constexpr std::u32string_view kRank = U"将長官王帝卿師士佐尉督主侯爵伯臣相";
            std::size_t run = v;
            while (run > 0 && ((u.text[run - 1] >= U'ァ' && u.text[run - 1] <= U'ヺ') || u.text[run - 1] == U'ー')) --run;
            const bool titled = run < v && run > 0 && kRank.find(u.text[run - 1]) != std::u32string_view::npos;
            if (titled || kTitle.find(u.text[v - 1]) != std::u32string_view::npos) c.auth = true;
        }
    }
    for (const auto& [kind, text] : t.honBack)
        if (u.text.find(text) != std::u32string::npos) (kind == "give" ? c.honGive : c.honSonkei) = true;
    for (std::size_t a = 0; a < t.auth.size(); ++a) {
        bool all = !t.auth[a].parts.empty();
        for (std::size_t p = 0; p < t.auth[a].parts.size(); ++p) {
            bool found = false;
            for (const auto& alt : t.auth[a].parts[p])
                for (std::size_t at = u.text.find(alt); at != std::u32string::npos && !found; at = u.text.find(alt, at + 1))
                    if (at == 0 || t.auth[a].notAfter.find(u.text[at - 1]) == std::u32string::npos) found = true;
            if (!found) all = false;
            else if (t.auth[a].speaker) c.authParts.push_back({a, p});
        }
        if (all) c.auth = true;
    }
    return c;
}

std::string Key(const Cues& c) {
    if (c.narr) return "narr";
    if (c.empty) return "empty";
    if (c.set && !c.cend) return "set";
    if (c.keigo && c.pend) return "keigo-pend";
    if (c.pend) return "pend";
    if (c.keigo) return c.lastStrong || c.lastKa ? "keigo-casual" : "keigo";
    if (c.arch) return "archaic";
    if (c.pany) return "polite-any";
    if (c.stiff) return "stiff";
    if (c.cend && c.declZo) return "decl-zo";
    if (c.cend && c.exclaimNe) return "exclaim-ne";
    if (c.exclaim && !c.strongTail) return "exclaim";
    if (c.cend) return "casual";
    return "bare";
}

Persona VoiceOf(const RegisterTables& t, const std::string& ja) {
    const Utt u = Utterance(ToU32(ja));
    std::u32string last = LastSentence(u.text);
    PopMarks(last);
    for (const auto& v : t.voices) {
        if (v.type == "final" && EndsWith(last, v.key)) return v.persona;
        if (v.type == "first" && u.text.find(v.key) != std::u32string::npos) return v.persona;
    }
    return Persona::none;
}

}  // namespace

std::string CueKey(const RegisterTables& t, const std::string& ja) { return Key(Scan(t, ja)); }

std::vector<RegDecision> DecideLevels(const RegisterTables& t, const std::vector<RegLine>& lines) {
    std::vector<RegDecision> out(lines.size());
    // REG-20: prose fiction: paragraphs separated by blank lines (one line in 20 or more blank, 10 lines or more), no
    // speaker tags; game scripts have no blank lines or name their speakers
    bool prose = false;
    std::vector<bool> casualAt(lines.size(), false);
    const auto blank = [&](std::size_t k) { return lines[k].ja.find_first_not_of(" \t\r\xe3\x80\x80") == std::string::npos; };
    {
        std::size_t blanks = 0, narration = 0, dialogue = 0, named = 0;
        // a name line: a short line without sentence punctuation right before a quote line (筧 / 「おはよう」); a scene
        // mark (◇◇◇, ——, ＊) is no name
        const auto nameLine = [&](std::size_t k) {
            if (k + 1 >= lines.size() || !lines[k + 1].dialogue || lines[k].dialogue) return false;
            const std::u32string x = ToU32(lines[k].ja);
            if (x.empty() || x.size() > 12 || !HasWord(x)) return false;
            for (const char32_t ch : x)
                if (ch == U'。' || ch == U'、' || ch == U'！' || ch == U'？' || ch == U'…' || ch == U'「') return false;
            return true;
        };
        for (std::size_t k = 0; k < lines.size(); ++k) {
            if (blank(k)) ++blanks;
            else if (nameLine(k)) ++named;
            else if (!lines[k].dialogue) ++narration;
            else ++dialogue;
            if (!lines[k].speaker.empty()) ++named;
        }
        // a script names its speakers on many lines (a few bracketed headings in a book do not count); prose has 10
        // lines or more, 2 blank ones, and narration on 3 in 10 of its other lines
        const bool tagged = named * 10 >= dialogue && named > 0;
        prose = !tagged && lines.size() >= 10 && blanks >= 2 && blanks * 20 >= lines.size() &&
                narration * 10 >= (lines.size() - blanks) * 3;
        if (prose)
            for (std::size_t k = 0; k < lines.size(); ++k)
                if (lines[k].dialogue) casualAt[k] = Scan(t, lines[k].ja).casualMark;
    }
    // the line right before or after line k (blank lines skipped) is dialogue that speaks casually: an exchange
    const auto casualNeighbour = [&](std::size_t k) {
        for (const long dir : {-1L, 1L})
            for (long m = static_cast<long>(k) + dir; m >= 0 && m < static_cast<long>(lines.size()); m += dir) {
                const std::size_t n = static_cast<std::size_t>(m);
                if (blank(n)) continue;
                if (lines[n].dialogue && casualAt[n]) return true;
                break;
            }
        return false;
    };
    struct Prof {
        int pol = 0, cas = 0;
        bool auth = false;
        std::map<std::size_t, std::map<std::size_t, bool>> authParts;  // speaker auth row → parts seen in the scene
        std::map<int, int> voice;
    };
    // What a speaker's earlier scenes leave: the voices only (REG-8 and REG-14 look at one scene).
    std::unordered_map<std::string, Prof> carry;
    std::size_t i = 0;
    while (i < lines.size()) {
        std::size_t j = i;
        while (j < lines.size() && lines[j].scene == lines[i].scene) ++j;
        // the scene [i, j)
        std::unordered_map<std::string, Prof> prof;
        for (const auto& kv : carry) {
            Prof p;
            p.voice = kv.second.voice;
            prof[kv.first] = p;
        }
        std::vector<Cues> cues(j - i);
        std::vector<std::string> keys(j - i);
        // REG-9: no quote line in the scene and three tenths of its lines end like speech, or short lines: all speech
        bool sceneSpeech = false, sceneArchaic = false;  // sceneArchaic: period speech keeps the input's levels
        {
            std::size_t quoted = 0, speechy = 0, archaic = 0;
            std::vector<std::size_t> lens;
            for (std::size_t k = i; k < j; ++k) {
                if (lines[k].dialogue) ++quoted;
                const Cues c = Scan(t, lines[k].ja);
                const std::string key = Key(c);
                if (key == "pend" || key == "keigo-pend" || key == "set" || key == "polite-any" || c.strongTail) ++speechy;
                if (c.arch) ++archaic;
                lens.push_back(ToU32(lines[k].ja).size());
            }
            std::sort(lens.begin(), lens.end());
            const bool shortLines = !lens.empty() && lens[lens.size() / 2] <= 20;
            sceneSpeech = quoted * 10 < (j - i) && j > i && (speechy * 10 >= (j - i) * 3 || shortLines);
            sceneArchaic = sceneSpeech && archaic >= 2;
        }
        for (std::size_t k = i; k < j; ++k) {
            if (!lines[k].dialogue && lines[k].speaker.empty()) continue;
            cues[k - i] = Scan(t, lines[k].ja);
            keys[k - i] = Key(cues[k - i]);
            if (lines[k].speaker.empty()) continue;
            Prof& p = prof[lines[k].speaker];
            const std::string& key = keys[k - i];
            if (key == "pend" || key == "keigo-pend" || key == "set") p.pol += 1;
            if (key == "casual" || key == "keigo-casual" || key == "stiff") p.cas += 1;
            // REG-14: the speaker is authoritative in the scene when all parts of a speaker auth row occur in
            // the speaker's lines (わし; 私 and おまえ)
            for (const auto& [row, part] : cues[k - i].authParts) {
                p.authParts[row][part] = true;
                if (p.authParts[row].size() == t.auth[row].parts.size()) p.auth = true;
            }
            const Persona v = VoiceOf(t, lines[k].ja);
            if (v != Persona::none) p.voice[static_cast<int>(v)] += 1;
        }
        for (std::size_t k = i; k < j; ++k) {
            RegDecision& d = out[k];
            {
                const Cues hc = Scan(t, lines[k].ja);
                d.honSonkei = hc.honSonkei;
                d.honGive = hc.honGive;
                d.exclaimNe = hc.exclaimNe && Key(hc) == "exclaim-ne";
            }
            if (!lines[k].dialogue && lines[k].speaker.empty()) {
                const Cues c = Scan(t, lines[k].ja);
                const std::string key = Key(c);
                const auto row = t.levels.find(key);
                if (sceneSpeech) {
                    // REG-9: every line of the scene is an utterance at its row's level; period speech keeps the
                    // input's plain levels
                    d.level = row != t.levels.end() ? row->second.level : Level::hae;
                    d.keep = (row != t.levels.end() && row->second.keep) || (sceneArchaic && (key == "casual" || key == "bare"));
                    d.key = "scene-speech-" + key;
                    d.spec = "REG-9";
                    continue;
                }
                // REG-7: an unquoted line that ends politely, or (rows marked strong) with a strong sentence-final
                // particle, is speech; any other is narration (REG-1)
                using U = LevelRow::Unquoted;
                if (row != t.levels.end() &&
                    (row->second.unquoted == U::yes || (row->second.unquoted == U::strong && c.strongTail))) {
                    d.level = row->second.level;
                    if (d.level == Level::haeyo) d.level = Level::hapsyo;  // a polite narrator
                    d.key = "unbracketed-" + key;
                    d.spec = "REG-7";
                } else {
                    d.level = Level::haera;
                    d.narration = true;
                    d.key = "narration";
                    d.spec = "REG-1";
                }
                continue;
            }
            const auto speakerProf = lines[k].speaker.empty() ? prof.end() : prof.find(lines[k].speaker);
            const bool authSpeaker = speakerProf != prof.end() && speakerProf->second.auth;
            // the decision of one utterance (the line, or one of its quotes) from its cues
            const auto decide = [&](const Cues& c, std::string key, RegDecision& r) {
                // REG-14: a plain statement without a sentence-final particle from an authoritative voice keeps -다
                if (key == "casual" && c.plainDecl && (c.auth || authSpeaker)) key = "auth-plain";
                // REG-20: in prose fiction a plain statement without a casual cue keeps -다
                else if (prose && key == "casual" && c.plainDecl && !c.casualMark && !casualNeighbour(k)) key = "prose-plain";
                r.key = key;
                const auto row = t.levels.find(key);
                r.level = row != t.levels.end() ? row->second.level : Level::hae;
                if (row != t.levels.end()) r.spec = row->second.spec;
                r.keep = row != t.levels.end() && row->second.keep;
                // REG-8: a line without its own level cue speaks like the speaker's cued lines in the scene
                if (row != t.levels.end() && row->second.profile && speakerProf != prof.end() &&
                    speakerProf->second.pol > speakerProf->second.cas) {
                    r.byProfile = r.level != Level::haeyo;
                    r.level = Level::haeyo;
                    if (r.byProfile) r.spec = "REG-8";
                }
            };
            decide(cues[k - i], keys[k - i], d);
            d.trail = cues[k - i].trail;
            d.holdQuote = cues[k - i].outburst;
            d.trailConj = cues[k - i].trailConj;
            d.firstPerson = !cues[k - i].first.empty();
            d.narration = d.key == "narr";
            // REG-15: a line of several quotes gets a level per quote; the line's own is its last quote's
            const std::vector<std::string> quotes = d.narration ? std::vector<std::string>() : SplitQuotes(lines[k].ja);
            if (quotes.size() >= 2) {
                for (const std::string& qt : quotes) {
                    const Cues qc = Scan(t, qt);
                    RegDecision qd;
                    decide(qc, Key(qc), qd);
                    d.quotes.push_back({qd.level, qd.keep, qd.key, qd.spec});
                }
                d.level = d.quotes.back().level;
                d.keep = d.quotes.back().keep;
                d.key = "quotes-" + d.quotes.back().key;
                d.spec = "REG-15";
            }
            d.persona = VoiceOf(t, lines[k].ja);
            const auto p = prof.find(lines[k].speaker);
            if (d.persona == Persona::none && p != prof.end() && !lines[k].speaker.empty() && !p->second.voice.empty()) {
                int best = 0, bestN = 0;
                for (const auto& [vv, n] : p->second.voice)
                    if (n > bestN) {
                        best = vv;
                        bestN = n;
                    }
                d.persona = static_cast<Persona>(best);
            }
        }
        carry = prof;
        i = j;
    }
    return out;
}

std::vector<Reg> AssignRegister(const RegisterTables& rt, const Tables& gen, const std::vector<Node>& nodes,
                                const RegDecision& d) {
    std::vector<Reg> out;
    const bool polite = d.level == Level::haeyo || d.level == Level::hapsyo;
    bool subjectSeen = false;
    for (const Node& n : nodes) {
        if (n.kind == Kind::P && (n.kase == "subj" || n.kase == "top")) subjectSeen = true;
        if (n.kind == Kind::Punct && (n.punct == Punct::end || n.punct == Punct::q || n.punct == Punct::excl)) subjectSeen = false;
        if (n.kind != Kind::V) continue;
        Reg r;
        r.node = n.id;
        r.level = d.narration ? Level::haera : d.level;
        r.persona = d.persona;
        // REG-16: the addressee gives (教えてくれますか → 가르쳐 주시겠어요): -시- on 주다, auxiliary or not
        if (polite && !d.narration && d.honGive && n.lemma == "주다" && n.ctype == CType::final_ &&
            (n.mood == Mood::q || n.mood == Mood::imp) && !d.firstPerson && !subjectSeen)
            r.hon = true;
        // REG-16: a request or command to the addressee, or a question with an honorific verb
        const bool backed = n.mood == Mood::imp || (n.mood == Mood::q && d.honSonkei);
        if (polite && !d.narration && n.ctype == CType::final_ && backed && !d.firstPerson && !subjectSeen && !n.aux) {
            std::string pos = n.vpos;
            std::string cls = n.kcls;
            if (pos.empty() || cls.empty()) {
                const PredicateInfo info = LookupPredicate(gen, n.lemma);
                if (pos.empty()) pos = info.pos;
                if (cls.empty()) cls = info.cls;
            }
            bool excluded = false;
            for (const auto& x : rt.honExclude) {
                if (x == n.lemma) excluded = true;
                if (x.starts_with("suffix:") && n.lemma.size() > x.size() - 7 && n.lemma.ends_with(std::string_view(x).substr(7)))
                    excluded = true;
            }
            if (pos == "V" && cls != "COP" && cls != "EXIST" && !excluded) r.hon = true;
        }
        if (n.ctype == CType::final_) subjectSeen = false;
        out.push_back(r);
    }
    return out;
}

}  // namespace hdTrans::reg
