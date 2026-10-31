#include "hd8gen/register.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <unordered_map>

#include "hangul.h"
#include "hd8gen/generator.h"
#include "tables.h"

namespace hd8gen {

using hangul::ToU32;
using hangul::ToU8;

struct CueRow {
    std::string cue, mode;
    std::u32string text, notAfter;
};
struct LevelRow {
    std::string key, spec;
    Level level = Level::hae;
    bool profile = false;
    bool keep = false;  // keep the level the input already has (RegDecision.keep)
    enum class Unquoted { no, yes, strong } unquoted = Unquoted::no;  // a line outside quotes (REG-7)
};
struct TailRow {
    std::u32string text, notAfter;  // a sentence-final particle; characters that must not come right before it
    bool strong = false;
};
struct VoiceRow {
    std::string type;
    std::u32string key;
    Persona persona = Persona::none;
};
struct RegisterTables {
    std::vector<VoiceRow> voices;
    std::vector<CueRow> cues;
    std::vector<TailRow> tails;
    std::vector<std::u32string> quotatives;
    std::vector<std::u32string> exclaim;            // cues.tsv exclaim: a whole sentence that exclaims (みっけ)
    struct AuthRow {
        std::vector<std::vector<std::u32string>> parts;  // each part (alternatives split by |) must occur (in the line, or for a speaker row across the speaker's lines)
        bool speaker = false;               // marks the speaker in the scene, not only the line
        std::u32string notAfter;            // characters that must not come right before a part (お嬢, 令嬢)
    };
    std::vector<AuthRow> auth;  // voices.tsv auth: an authoritative voice (REG-14)
    std::map<std::string, LevelRow> levels;
    std::vector<std::string> honExclude;
    std::vector<std::pair<std::string, std::u32string>> honBack;  // hon.tsv back rows: sonkei | give, Japanese text
};

std::shared_ptr<const RegisterTables> LoadRegisterTables(const std::string& dir, std::string& err) {
    auto t = std::make_shared<RegisterTables>();
    TsvTable c;
    if (!ReadTsv(dir + "/cues.tsv", c, err)) return nullptr;
    const int cc = c.Col("cue"), cm = c.Col("mode"), ct = c.Col("text"), cn = c.Col("notafter");
    if (cc < 0 || cm < 0 || ct < 0) {
        err = "cues.tsv: missing column";
        return nullptr;
    }
    for (const auto& r : c.rows) {
        if (r[cc] == "tail") {
            t->tails.push_back({ToU32(r[ct]), cn >= 0 ? ToU32(r[cn]) : std::u32string(), r[cm] == "strong"});
        }
        else if (r[cc] == "quotative") t->quotatives.push_back(ToU32(r[ct]));
        else if (r[cc] == "exclaim") t->exclaim.push_back(ToU32(r[ct]));
        else t->cues.push_back({r[cc], r[cm], ToU32(r[ct]), cn >= 0 ? ToU32(r[cn]) : std::u32string()});
    }
    TsvTable l;
    if (!ReadTsv(dir + "/levels.tsv", l, err)) return nullptr;
    const int lk = l.Col("key"), ls = l.Col("spec"), ll = l.Col("level"), lp = l.Col("profile"), lkeep = l.Col("keep"),
              lu = l.Col("unquoted");
    if (lk < 0 || ls < 0 || ll < 0 || lp < 0 || lkeep < 0 || lu < 0) {
        err = "levels.tsv: missing column";
        return nullptr;
    }
    TsvTable vx;
    if (!ReadTsv(dir + "/voices.tsv", vx, err)) return nullptr;
    for (const auto& r : vx.rows) {
        if (r[0] == "auth") {
            RegisterTables::AuthRow a;
            for (const auto& part : Split(r[1], '+')) {
                std::vector<std::u32string> alts;
                for (const auto& alt : Split(part, '|')) alts.push_back(ToU32(alt));
                a.parts.push_back(alts);
            }
            if (r[2] != "speaker" && r[2] != "line") {
                err = "voices.tsv: auth value must be speaker or line: " + r[2];
                return nullptr;
            }
            a.speaker = r[2] == "speaker";
            if (vx.Col("notafter") >= 0) a.notAfter = ToU32(r[static_cast<size_t>(vx.Col("notafter"))]);
            t->auth.push_back(a);
            continue;
        }
        VoiceRow v;
        v.type = r[0];
        v.key = ToU32(r[1]);
        if (v.type != "suffix" && !ParsePersona(r[2], v.persona)) {
            err = "voices.tsv: bad persona " + r[2];
            return nullptr;
        }
        t->voices.push_back(v);
    }
    TsvTable hx;
    if (!ReadTsv(dir + "/hon.tsv", hx, err)) return nullptr;
    for (const auto& r : hx.rows)
        if (r[0] == "exclude") t->honExclude.push_back(r[1]);
        else if (r[0] == "back") {
            if (r.size() < 3 || (r[1] != "sonkei" && r[1] != "give")) {
                err = "hon.tsv: back rows need kind sonkei or give and a text";
                return nullptr;
            }
            t->honBack.push_back({r[1], ToU32(r[2])});
        }
    for (const auto& r : l.rows) {
        LevelRow row;
        row.key = r[lk];
        row.spec = r[ls];
        if (row.spec.empty()) {
            err = "levels.tsv: no spec id for " + row.key;
            return nullptr;
        }
        row.keep = r[lkeep] == "yes";
        if (r[lu] == "yes") row.unquoted = LevelRow::Unquoted::yes;
        else if (r[lu] == "strong") row.unquoted = LevelRow::Unquoted::strong;
        else if (r[lu] != "no") {
            err = "levels.tsv: bad unquoted " + r[lu];
            return nullptr;
        }
        if (!ParseLevel(r[ll], row.level)) {
            err = "levels.tsv: bad level " + r[ll];
            return nullptr;
        }
        row.profile = r[lp] == "yes";
        t->levels[row.key] = row;
    }
    return t;
}

namespace {

bool IsMark(char32_t c) {
    switch (c) {
        case U'。': case U'．': case U'！': case U'？': case U'!': case U'?': case U'…': case U'‥': case U'ー':
        case U'〜': case U'～': case U'っ': case U'ッ': case U'・': case U'♪': case U'♥': case U' ': case U'　':
        case U'」': case U'』': case U'）': case U')': case U']': case U'＞': case U'>': case U'、': case U'，': case 0xFF61: case 0xFF64:
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
bool EndsWith(const std::u32string& s, const std::u32string& e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}
// Whether s ends in the tail particle (not right after one of its notAfter characters: the ぞ of どうぞ).
// notAfter holds characters, or words separated by | (どう: the ぞ of どうぞ, while 参ろうぞ keeps its ぞ).
bool EndsInTail(const std::u32string& s, const TailRow& tail) {
    if (s.size() <= tail.text.size() || !EndsWith(s, tail.text)) return false;
    const std::u32string head = s.substr(0, s.size() - tail.text.size());
    {
        if (tail.notAfter.find(U'|') != std::u32string::npos) {
            size_t a = 0;
            while (a <= tail.notAfter.size()) {
                size_t b = tail.notAfter.find(U'|', a);
                if (b == std::u32string::npos) b = tail.notAfter.size();
                const std::u32string w = tail.notAfter.substr(a, b - a);
                if (!w.empty() && EndsWith(head, w)) return false;
                a = b + 1;
            }
            return true;
        }
    }
    return tail.notAfter.find(head.back()) == std::u32string::npos;
}
bool StartsAt(const std::u32string& s, size_t i, const std::u32string& e) { return s.compare(i, e.size(), e) == 0; }

struct Utt {
    std::u32string text;
    bool narr = false;
};

Utt Utterance(const std::u32string& ja) {
    Utt u;
    size_t open = std::u32string::npos;
    for (size_t i = 0; i < ja.size(); ++i)
        if (ja[i] == U'「' || ja[i] == U'『' || ja[i] == U'≪') {
            open = i;
            break;
        }
    if (open == std::u32string::npos) {
        u.text = ja;
        return u;
    }
    const char32_t closeCh = ja[open] == U'「' ? U'」' : (ja[open] == U'≪' ? U'≫' : U'』');
    const size_t close = ja.rfind(closeCh);
    if (close == std::u32string::npos || close < open) {
        u.text = ja.substr(open + 1);
        return u;
    }
    u.text = ja.substr(open + 1, close - open - 1);
    // narration after the quote: 「…」と言った
    for (size_t i = close + 1; i < ja.size(); ++i)
        if (!IsMark(ja[i]) && ja[i] != U'」' && ja[i] != U'』' && IsWordChar(ja[i])) {
            u.narr = true;
            break;
        }
    return u;
}

std::u32string LastSentence(const std::u32string& u) {
    std::vector<std::u32string> pieces;
    std::u32string cur;
    for (char32_t c : u) {
        cur.push_back(c);
        if (IsSentenceEnd(c)) {
            pieces.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) pieces.push_back(cur);
    for (auto it = pieces.rbegin(); it != pieces.rend(); ++it)
        for (char32_t c : *it)
            if (IsWordChar(c)) return *it;
    return u;
}

// The top-level quotes of a line (「了解なのよ」 「了解」 「かしこま」), each with its brackets; one entry or none
// when the line holds fewer than two.
std::vector<std::string> SplitQuotes(const std::string& ja) {
    const std::u32string s = ToU32(ja);
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    char32_t open = 0, close = 0;
    // text before the first quote may only be a speaker tag: 【name】, [name], (name), or a short bare name
    {
        size_t first = 0;
        while (first < s.size() && s[first] != U'「' && s[first] != U'『' && s[first] != U'≪') ++first;
        std::u32string pre = s.substr(0, first);
        while (!pre.empty() && (pre.back() == U' ' || pre.back() == U'　')) pre.pop_back();
        size_t lead = 0;
        while (lead < pre.size() && (pre[lead] == U' ' || pre[lead] == U'　')) ++lead;
        pre = pre.substr(lead);
        const bool bracketed = pre.size() >= 2 && pre.size() <= 26 &&
                               ((pre.front() == U'【' && pre.back() == U'】') || (pre.front() == U'[' && pre.back() == U']') ||
                                (pre.front() == U'（' && pre.back() == U'）') || (pre.front() == U'(' && pre.back() == U')'));
        bool bareName = pre.size() <= 12;
        for (char32_t ch : pre)
            if (ch == U'、' || ch == U'。' || ch == U'，' || (ch >= U'ぁ' && ch <= U'ゖ')) bareName = false;
        if (!pre.empty() && !bracketed && !bareName) return {};
    }
    for (size_t i = 0; i < s.size(); ++i) {
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
            return {};  // text between quotes (「…」と言った「…」) is narration: not a line of several quotes
        }
    }
    return out;
}

struct Cues {
    bool strongTail = false;  // a sentence of the line ends in よ, ね, ぞ …
    bool lastStrong = false;  // the last sentence ends in one of them (its outermost tail particle)
    bool lastKa = false;      // the last sentence ends in the question particle か
    bool plainDecl = false;   // the last sentence ends in a plain statement (だ, る, た, い, ん), no particle, no ？
    bool exclaim = false;     // every sentence is a bare predicate alone or has no predicate (REG-13)
    bool auth = false;        // the line has an authoritative voice cue (voices.tsv auth, REG-14)
    bool trail = false;       // the last sentence trails off after a て-form (言われて……)
    bool honSonkei = false, honGive = false;  // REG-16 (hon.tsv back rows)
    bool declZo = false;      // REG-17: a plain statement declared with ぞ or ぜ (行くぞ, やるぜ)
    bool exclaimNe = false;   // REG-18: a predicate exclaimed with な, なぁ, なあ (寒いなぁ)
    std::vector<std::pair<size_t, size_t>> authParts;  // parts of speaker auth rows found (row, part)
    bool narr = false, empty = false, set = false, keigo = false, pend = false, pany = false, arch = false, stiff = false,
         cend = false;
    std::string first;
};

Cues Scan(const RegisterTables& t, const std::string& ja) {
    Cues c;
    const Utt u = Utterance(ToU32(ja));
    c.narr = u.narr;
    bool word = false;
    for (char32_t ch : u.text)
        if (IsWordChar(ch)) word = true;
    c.empty = !word;
    // stages of the last sentence: closing marks stripped, then 0..n tail particles stripped
    std::u32string last = LastSentence(u.text);
    // closing marks, then a small vowel that lengthens the last kana (なぁ, よぉ, ねぇ), then marks again
    while (!last.empty() && IsMark(last.back())) last.pop_back();
    while (last.size() >= 2 && (last.back() == U'ぁ' || last.back() == U'ぃ' || last.back() == U'ぅ' || last.back() == U'ぇ' ||
                                last.back() == U'ぉ') && last[last.size() - 2] >= U'ぁ' && last[last.size() - 2] <= U'ゖ')
        last.pop_back();
    while (!last.empty() && IsMark(last.back())) last.pop_back();
    // an elongation mark between kana is not part of the word (でーす, ますぅ): drop it for matching
    for (size_t k = 1; k + 1 < last.size();) {
        const bool kanaL = last[k - 1] >= U'ぁ' && last[k - 1] <= U'ヺ';
        const bool kanaR = last[k + 1] >= U'ぁ' && last[k + 1] <= U'ヺ';
        if ((last[k] == U'ー' || last[k] == U'〜' || last[k] == U'～') && kanaL && kanaR) last.erase(k, 1);
        else ++k;
    }
    std::vector<std::u32string> stages{last};
    bool tailStripped = false;
    for (int guard = 0; guard < 6; ++guard) {
        const std::u32string& s = stages.back();
        const TailRow* best = nullptr;
        for (const auto& tail : t.tails)
            if ((!best || tail.text.size() > best->text.size()) && EndsInTail(s, tail)) best = &tail;
        if (!best) break;
        if (stages.size() == 1 && best->strong) c.lastStrong = true;
        if (stages.size() == 1 && best->text == U"か") c.lastKa = true;
        if (stages.size() == 1) {
            std::u32string rest = s.substr(0, s.size() - best->text.size());
            while (!rest.empty() && IsMark(rest.back())) rest.pop_back();
            const char32_t e = rest.empty() ? 0 : rest.back();
            static const std::u32string kDecl = U"だたいるうくすつぬぶむぐん";
            if ((best->text == U"ぞ" || best->text == U"ぜ") && kDecl.find(e) != std::u32string::npos) c.declZo = true;
            // な on a verb's dictionary form forbids (行くな); on い, た, だ it exclaims
            // ... but not after a wish (ほしいな, 行きたいな: 갖고 싶은데)
            // the desire たい follows a verb stem (行きたい, 見たい), not the adjectives 冷たい, 重たい, 眠たい, めでたい
            static const std::u32string kStem = U"いきぎしじちにひびみりえけげせぜてでねべめれ見来寝出着居似煮";
            static const std::vector<std::u32string> kTaiAdj = {U"冷たい", U"重たい", U"眠たい", U"めでたい", U"ありがたい",
                                                                U"つめたい", U"おもたい", U"ねむたい", U"後ろめたい", U"うしろめたい"};
            bool taiAdj = false;
            for (const auto& a : kTaiAdj)
                if (EndsWith(rest, a)) taiAdj = true;
            const bool desire = EndsWith(rest, U"たい") && !taiAdj && rest.size() >= 3 &&
                                kStem.find(rest[rest.size() - 3]) != std::u32string::npos;
            const bool wish = desire || EndsWith(rest, U"ほしい") || EndsWith(rest, U"欲しい") ||
                              EndsWith(rest, U"また");  // またな is a farewell
            if ((best->text == U"な" || best->text == U"なあ") && (e == U'い' || e == U'た' || e == U'だ') && !wish)
                c.exclaimNe = true;
        }
        std::u32string next = s.substr(0, s.size() - best->text.size());
        while (!next.empty() && IsMark(next.back())) next.pop_back();
        stages.push_back(next);
        tailStripped = true;
    }
    bool casualEnd = tailStripped;
    {
        // a strong sentence-final particle at the end of any sentence of the utterance (…だよ。…しろ。)
        std::u32string sent;
        auto check = [&](std::u32string x) {
            while (!x.empty() && IsMark(x.back())) x.pop_back();
            for (const auto& tail : t.tails)
                if (tail.strong && EndsInTail(x, tail)) c.strongTail = true;
        };
        for (char32_t ch : u.text) {
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
        auto before = [&](const std::u32string& str, size_t at) {
            // a polite auxiliary (ます, ました) cannot start the utterance (ますます), and ます beside ます is ますます
            const bool aux = r.cue == "polite" || r.cue == "polite-any";
            if (aux && r.text.rfind(U"ま", 0) == 0) {
                if (at == 0) return true;
                if (StartsAt(str, at, U"ますます") || (at >= 2 && str.compare(at - 2, 2, U"ます") == 0)) return true;
            }
            return at > 0 && !r.notAfter.empty() && r.notAfter.find(str[at - 1]) != std::u32string::npos;
        };
        if (r.mode == "end") {
            for (const auto& s : stages)
                if (EndsWith(s, r.text) && !before(s, s.size() - r.text.size())) hit = true;
        } else {
            for (size_t i = u.text.find(r.text); i != std::u32string::npos; i = u.text.find(r.text, i + 1)) {
                if (before(u.text, i)) continue;
                if (r.mode == "any-not-quoted") {
                    const size_t j = i + r.text.size();
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
        else if (r.cue.rfind("first:", 0) == 0 && c.first.empty()) c.first = r.cue.substr(6);
    }
    // the last sentence has no end cue (a name called out: 落ち着きなさい！ 森下っ！！): the nearest earlier sentence
    // with one decides
    if (!c.pend && !casualEnd) {
        std::vector<std::u32string> sents;
        std::u32string cur;
        for (char32_t ch : u.text) {
            if (IsSentenceEnd(ch)) {
                sents.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
        sents.push_back(cur);
        while (!sents.empty()) {  // drop the last sentence with a word (the one already read)
            bool w = false;
            for (char32_t ch : sents.back())
                if (IsWordChar(ch)) w = true;
            sents.pop_back();
            if (w) break;
        }
        for (auto it = sents.rbegin(); it != sents.rend() && !c.pend && !casualEnd; ++it) {
            std::u32string x = *it;
            while (!x.empty() && IsMark(x.back())) x.pop_back();
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
    {
        std::u32string raw = LastSentence(u.text);
        size_t e = raw.size();
        while (e > 0 && (raw[e - 1] == U' ' || raw[e - 1] == U'　' || raw[e - 1] == U'」' || raw[e - 1] == U'』')) --e;
        size_t dots = e;
        while (dots > 0 && (raw[dots - 1] == U'…' || raw[dots - 1] == U'‥')) --dots;
        c.trail = dots < e && dots > 0 && (raw[dots - 1] == U'て' || raw[dots - 1] == U'で');
    }
    // REG-14: a plain statement without a sentence-final particle (思えない, ファンだ, 戦ってみたかった)
    {
        std::u32string raw = LastSentence(u.text);
        while (!raw.empty() && (raw.back() == U' ' || raw.back() == U'　' || raw.back() == U'」' || raw.back() == U'』')) raw.pop_back();
        const bool question = !raw.empty() && (raw.back() == U'？' || raw.back() == U'?');
        std::u32string x = stages.front();
        while (!x.empty() && (x.back() == U'っ' || x.back() == U'ッ' || x.back() == U'ー')) x.pop_back();
        static const std::u32string kEnds = U"だたいるうくすつぬぶむぐん";
        static const std::u32string kORow = U"おこごそぞとどのほぼぽもよょろ";
        const bool volitional = x.size() >= 2 && x.back() == U'う' && kORow.find(x[x.size() - 2]) != std::u32string::npos;
        c.plainDecl = !question && !volitional && !tailStripped && !x.empty() && kEnds.find(x.back()) != std::u32string::npos;
    }
    // REG-13: an exclamation of discovery or realisation: the utterance is bare predicates alone (見つけた！, 面白い。,
    // みっけ……あれ？), with no sentence-final particle; a piece without a predicate (あれ？) may stand beside them
    {
        std::vector<std::u32string> pieces;
        std::vector<bool> asked;  // the piece ends in ？
        std::u32string cur;
        for (char32_t ch : u.text) {
            if (IsSentenceEnd(ch) || ch == U'…' || ch == U'‥' || ch == U'、' || ch == U'，') {
                if (ch == U'？' || ch == U'?') {
                    if (!pieces.empty() && cur.empty()) asked.back() = true;  // ！？ after the piece
                    else {
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
        static const std::vector<std::u32string> kInterj = {U"はい", U"おい", U"えい", U"ほい", U"あい", U"うい"};
        bool any = false, ok = true;
        for (size_t pi = 0; pi < pieces.size(); ++pi) {
            std::u32string p = pieces[pi];
            while (!p.empty() && (p.back() == U'っ' || p.back() == U'ッ' || p.back() == U'！' || p.back() == U'!')) p.pop_back();
            bool hasWord = false;
            for (char32_t ch : p)
                if (IsWordChar(ch)) hasWord = true;
            if (!hasWord) continue;
            if (p.size() > 6) {
                ok = false;
                break;
            }
            bool pred = std::find(t.exclaim.begin(), t.exclaim.end(), p) != t.exclaim.end();
            const char32_t prev = p.size() >= 2 ? p[p.size() - 2] : 0;
            const bool kanjiPrev = prev >= 0x4E00 && prev <= 0x9FFF;
            const bool honorificNoun = p[0] == U'お' || p[0] == U'ご' || EndsWith(p, U"願い");  // お願い, お帰りなさい
            // past: 見つけた, 勝った, 来た: た after an i- or e-row kana, っ or a kanji (not また, あなた)
            static const std::u32string kPastPrev = U"いきぎしじちぢにひびぴみりえけげせぜてでねへべぺめれっ";
            if (!pred && p.size() >= 2 && p.back() == U'た' && (kanjiPrev || kPastPrev.find(prev) != std::u32string::npos))
                pred = true;
            // adjective: 面白い, 怖い, 楽しい, いい, しょっぱい, すごい (not なさい, ない, お願い, りょうかい, はい)
            static const std::u32string kAdjPrev = U"しいぱごどずぶまる";
            if (!pred && p.size() >= 2 && p.back() == U'い' && !honorificNoun &&
                (kanjiPrev || kAdjPrev.find(prev) != std::u32string::npos) &&
                std::find(kInterj.begin(), kInterj.end(), p) == kInterj.end())
                pred = true;
            // a noun and だ of realisation: ホントだ, そうだ (not the explanatory んだ)
            const bool kataPrev = prev >= U'ァ' && prev <= U'ヺ';
            if (!pred && p.size() >= 3 && p.back() == U'だ' && (kanjiPrev || kataPrev || p == U"そうだ")) pred = true;
            // a verb's dictionary form when it is the whole utterance: 断る
            static const std::u32string kVerbEnd = U"うくぐすつぬぶむる";
            if (!pred && pieces.size() == 1 && p.size() >= 2 && kVerbEnd.find(p.back()) != std::u32string::npos &&
                (kanjiPrev || (prev >= U'ぁ' && prev <= U'ゖ')) && p != U"そう" && p != U"どう")
                pred = true;
            if (asked[pi]) pred = false;  // a question asks, it does not exclaim (読んでいい？, できた？)
            if (pred) any = true;
            // a piece with a particle or another ending is not an exclamation (行くよ, それは)
            else if (p.size() > 3) ok = false;
        }
        c.exclaim = ok && any;
        // musing over one's own words: a predicate, a pause, then か (信じる……か, そういうこと……か)
        std::u32string raw = LastSentence(u.text);
        bool asks = false;
        while (!raw.empty() && IsMark(raw.back()) && raw.back() != U'…' && raw.back() != U'‥') {
            if (raw.back() == U'？' || raw.back() == U'?') asks = true;
            raw.pop_back();
        }
        bool alone = true;  // the musing is the utterance's only sentence with a word
        {
            size_t sentences = 0;
            std::u32string cur2;
            for (char32_t ch : u.text) {
                cur2.push_back(ch);
                if (IsSentenceEnd(ch)) {
                    for (char32_t x : cur2)
                        if (IsWordChar(x)) { ++sentences; break; }
                    cur2.clear();
                }
            }
            for (char32_t x : cur2)
                if (IsWordChar(x)) { ++sentences; break; }
            alone = sentences <= 1;
        }
        if (!asks && alone && raw.size() >= 3 && raw.back() == U'か' &&
            (raw[raw.size() - 2] == U'…' || raw[raw.size() - 2] == U'‥'))
            c.exclaim = true;
    }
    for (const auto& [kind, text] : t.honBack)
        if (u.text.find(text) != std::u32string::npos) (kind == "give" ? c.honGive : c.honSonkei) = true;
    for (size_t a = 0; a < t.auth.size(); ++a) {
        bool all = !t.auth[a].parts.empty();
        for (size_t p = 0; p < t.auth[a].parts.size(); ++p) {
            bool found = false;
            for (const auto& alt : t.auth[a].parts[p])
                for (size_t at = u.text.find(alt); at != std::u32string::npos && !found; at = u.text.find(alt, at + 1))
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

}  // namespace

std::string CueKey(const RegisterTables& t, const std::string& ja) { return Key(Scan(t, ja)); }

std::vector<std::pair<std::string, std::string>> LevelSpecs(const RegisterTables& t) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [key, row] : t.levels) out.emplace_back(key, row.spec);
    return out;
}

bool IsHonorificSuffix(const RegisterTables& t, const std::string& word) {
    const std::u32string u = ToU32(word);
    for (const auto& v : t.voices)
        if (v.type == "suffix" && v.key == u) return true;
    return false;
}

Persona VoiceOf(const RegisterTables& t, const std::string& ja) {
    const Utt u = Utterance(ToU32(ja));
    std::u32string last = LastSentence(u.text);
    while (!last.empty() && IsMark(last.back())) last.pop_back();
    for (const auto& v : t.voices) {
        if (v.type == "final" && EndsWith(last, v.key)) return v.persona;
        if (v.type == "first" && u.text.find(v.key) != std::u32string::npos) return v.persona;
    }
    return Persona::none;
}

std::vector<RegDecision> DecideLevels(const RegisterTables& t, const std::vector<RegLine>& lines, const RegOptions& opt) {
    std::vector<RegDecision> out(lines.size());
    struct Prof {
        double pol = 0, cas = 0;
        bool auth = false;
        std::map<size_t, std::map<size_t, bool>> authParts;  // speaker auth row → parts seen in the scene
        std::map<int, int> voice;
    };
    std::unordered_map<std::string, Prof> carry;  // decayed evidence from earlier scenes
    size_t i = 0;
    while (i < lines.size()) {
        size_t j = i;
        while (j < lines.size() && lines[j].scene == lines[i].scene) ++j;
        // scene [i, j)
        std::unordered_map<std::string, Prof> prof;
        for (auto& kv : carry) {
            Prof p = kv.second;
            p.pol *= opt.decay;
            p.cas *= opt.decay;
            p.auth = false;  // REG-14 marks a speaker for one scene
            p.authParts.clear();
            prof[kv.first] = p;
        }
        std::vector<Cues> cues(j - i);
        std::vector<std::string> keys(j - i);
        // per-scene narration flag: no quote line in the scene and ≥30% of its lines end like speech → all speech
        bool sceneSpeech = false, sceneArchaic = false;  // sceneArchaic: period speech in the scene keeps the input's levels
        {
            size_t quoted = 0, speechy = 0;
            std::vector<size_t> lens;
            for (size_t k = i; k < j; ++k) {
                if (lines[k].dialogue) ++quoted;
                const Cues c = Scan(t, lines[k].ja);
                const std::string key = Key(c);
                if (key == "pend" || key == "keigo-pend" || key == "set" || key == "polite-any" || c.strongTail) ++speechy;
                lens.push_back(ToU32(lines[k].ja).size());
            }
            // captions and hooked game text: short lines; narration: long sentences
            std::sort(lens.begin(), lens.end());
            const bool shortLines = !lens.empty() && lens[lens.size() / 2] <= 20;
            sceneSpeech = quoted * 10 < (j - i) && j > i && (speechy * 10 >= (j - i) * 3 || shortLines);
            size_t archaic = 0;
            for (size_t k = i; k < j; ++k)
                if (Scan(t, lines[k].ja).arch) ++archaic;
            sceneArchaic = sceneSpeech && archaic >= 2;  // period drama captions; games keep the table (hae)
        }
        for (size_t k = i; k < j; ++k) {
            if (!lines[k].dialogue && lines[k].speaker.empty()) continue;
            cues[k - i] = Scan(t, lines[k].ja);
            keys[k - i] = Key(cues[k - i]);
            if (lines[k].speaker.empty()) continue;
            Prof& p = prof[lines[k].speaker];
            const std::string& key = keys[k - i];
            if (key == "pend" || key == "keigo-pend" || key == "set") p.pol += 1;
            if (key == "casual" || key == "keigo-casual" || key == "stiff") p.cas += 1;
            // REG-14: the speaker is marked authoritative in the scene when all parts of a speaker auth row occur in
            // the speaker's lines (わし; 私 and おまえ)
            for (const auto& [row, part] : cues[k - i].authParts) {
                p.authParts[row][part] = true;
                if (p.authParts[row].size() == t.auth[row].parts.size()) p.auth = true;
            }
            const Persona v = VoiceOf(t, lines[k].ja);
            if (v != Persona::none) p.voice[static_cast<int>(v)] += 1;
        }
        for (size_t k = i; k < j; ++k) {
            RegDecision& d = out[k];
            {
                const Cues hc = Scan(t, lines[k].ja);
                d.honSonkei = hc.honSonkei;
                d.honGive = hc.honGive;
                d.exclaimNe = hc.exclaimNe && Key(hc) == "exclaim-ne";
            }
            // STYLE §9: an unquoted line with a speaker tag is dialogue
            if (!lines[k].dialogue && lines[k].speaker.empty()) {
                // an unbracketed line is narration unless it ends like speech (inner monologue: …だよ, …です)
                const Cues c = Scan(t, lines[k].ja);
                const std::string key = Key(c);
                if (sceneSpeech) {
                    // a scene without quote lines whose lines read as speech (captions): every line is an utterance
                    auto row = t.levels.find(key);
                    d.level = row != t.levels.end() ? row->second.level : Level::hae;
                    d.keep = (row != t.levels.end() && row->second.keep) || (sceneArchaic && (key == "casual" || key == "bare"));
                    d.key = "scene-speech-" + key;
                    // REG-9: the scene is speech, each line at its row's level; period speech in it keeps the
                    // input's plain levels
                    d.spec = "REG-9";
                    continue;
                }
                // REG-7: an unquoted line that ends politely, or (for the rows marked strong) with a strong
                // sentence-final particle, is speech at its row's level; any other is narration (REG-1)
                auto row = t.levels.find(key);
                using U = LevelRow::Unquoted;
                if (row != t.levels.end() &&
                    (row->second.unquoted == U::yes || (row->second.unquoted == U::strong && c.strongTail))) {
                    d.level = row->second.level;
                    // STYLE §9: です/ます in narration, documents and notices is 합쇼 (a polite narrator), not 해요
                    if (d.level == Level::haeyo) d.level = Level::hapsyo;
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
            auto decide = [&](const Cues& c, std::string key, RegDecision& r) {
                // REG-14: a plain statement without a sentence-final particle from an authoritative voice keeps -다
                if (key == "casual" && c.plainDecl && (c.auth || authSpeaker)) key = "auth-plain";
                r.key = key;
                auto row = t.levels.find(key);
                r.level = row != t.levels.end() ? row->second.level : Level::hae;
                if (row != t.levels.end()) r.spec = row->second.spec;
                r.keep = row != t.levels.end() && row->second.keep;
                // REG-8: a line without its own level cue speaks like the speaker's cued lines in the scene: 해요
                // when more of them are polite than plain
                if (opt.profiles && row != t.levels.end() && row->second.profile && speakerProf != prof.end() &&
                    speakerProf->second.pol > speakerProf->second.cas) {
                    r.byProfile = r.level != Level::haeyo;
                    r.level = Level::haeyo;
                    if (r.byProfile) r.spec = "REG-8";
                }
            };
            decide(cues[k - i], keys[k - i], d);
            d.trail = cues[k - i].trail;
            d.firstPerson = !cues[k - i].first.empty();
            d.narration = d.key == "narr";
            // REG-15: a line of several quotes gets a level per quote; the line's own decision is its last quote's
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
            auto p = prof.find(lines[k].speaker);
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

std::vector<Reg> AssignRegister(const RegisterTables& rt, const std::vector<Node>& kir, const RegDecision& d,
                                bool firstPersonInSource, const Tables* gen) {
    std::vector<Reg> out;
    const bool polite = d.level == Level::haeyo || d.level == Level::hapsyo;
    bool subjectSeen = false;
    for (const Node& n : kir) {
        if (n.kind == Kind::P && (n.kase == "subj" || n.kase == "top")) subjectSeen = true;
        if (n.kind == Kind::Punct && (n.punct == Punct::end || n.punct == Punct::q || n.punct == Punct::excl)) subjectSeen = false;
        if (n.kind != Kind::V) continue;
        Reg r;
        r.node = n.id;
        r.level = d.narration ? Level::haera : d.level;
        r.persona = d.persona;
        // REG-16: the addressee gives (教えてくれますか → 가르쳐 주시겠어요): -시- on 주다, auxiliary or not
        if (polite && !d.narration && d.honGive && n.lemma == "주다" && n.ctype == CType::final_ &&
            (n.mood == Mood::q || n.mood == Mood::imp) && !firstPersonInSource && !subjectSeen)
            r.hon = true;
        // REG-16: a request or command to the addressee, or a question with an honorific verb
        const bool backed = n.mood == Mood::imp || (n.mood == Mood::q && d.honSonkei);
        if (polite && !d.narration && n.ctype == CType::final_ && backed && !firstPersonInSource && !subjectSeen && !n.aux) {
            std::string pos = n.vpos;
            std::string cls = n.kcls;
            if (gen && (pos.empty() || cls.empty())) {
                const PredicateInfo info = LookupPredicate(*gen, n.lemma);
                if (pos.empty()) pos = info.pos;
                if (cls.empty()) cls = info.cls;
            }
            bool excluded = false;
            for (const auto& x : rt.honExclude)
                if (x == n.lemma || (x.rfind("suffix:", 0) == 0 && n.lemma.size() > x.size() - 7 &&
                                     n.lemma.compare(n.lemma.size() - (x.size() - 7), x.size() - 7, x, 7, std::string::npos) == 0))
                    excluded = true;
            if (pos == "V" && cls != "COP" && cls != "EXIST" && !excluded) r.hon = true;
        }
        if (n.ctype == CType::final_) subjectSeen = false;
        out.push_back(r);
    }
    return out;
}

}  // namespace hd8gen
