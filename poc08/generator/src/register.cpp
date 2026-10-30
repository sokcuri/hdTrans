#include "hd8gen/register.h"

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
    std::u32string text;
};
struct LevelRow {
    std::string key;
    Level level = Level::hae;
    bool profile = false;
};
struct RegisterTables {
    std::vector<CueRow> cues;
    std::vector<std::u32string> tails, strongTails, quotatives;
    std::map<std::string, LevelRow> levels;
    std::vector<std::string> honExclude;
};

std::shared_ptr<const RegisterTables> LoadRegisterTables(const std::string& dir, std::string& err) {
    auto t = std::make_shared<RegisterTables>();
    TsvTable c;
    if (!ReadTsv(dir + "/cues.tsv", c, err)) return nullptr;
    const int cc = c.Col("cue"), cm = c.Col("mode"), ct = c.Col("text");
    if (cc < 0 || cm < 0 || ct < 0) {
        err = "cues.tsv: missing column";
        return nullptr;
    }
    for (const auto& r : c.rows) {
        if (r[cc] == "tail") {
            t->tails.push_back(ToU32(r[ct]));
            if (r[cm] == "strong") t->strongTails.push_back(ToU32(r[ct]));
        }
        else if (r[cc] == "quotative") t->quotatives.push_back(ToU32(r[ct]));
        else t->cues.push_back({r[cc], r[cm], ToU32(r[ct])});
    }
    TsvTable l;
    if (!ReadTsv(dir + "/levels.tsv", l, err)) return nullptr;
    const int lk = l.Col("key"), ll = l.Col("level"), lp = l.Col("profile");
    if (lk < 0 || ll < 0 || lp < 0) {
        err = "levels.tsv: missing column";
        return nullptr;
    }
    TsvTable hx;
    if (!ReadTsv(dir + "/hon.tsv", hx, err)) return nullptr;
    for (const auto& r : hx.rows)
        if (r[0] == "exclude") t->honExclude.push_back(r[1]);
    for (const auto& r : l.rows) {
        LevelRow row;
        row.key = r[lk];
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
        case U'」': case U'』': case U'）': case U')': case U']': case U'＞': case U'>': case U'、': case U'，':
            return true;
        default:
            return false;
    }
}
bool IsSentenceEnd(char32_t c) { return c == U'。' || c == U'！' || c == U'？' || c == U'!' || c == U'?'; }
bool IsWordChar(char32_t c) {
    return (c >= U'ぁ' && c <= U'ゖ') || (c >= U'ァ' && c <= U'ヺ') || (c >= 0x4E00 && c <= 0x9FFF) || (c >= U'a' && c <= U'z') ||
           (c >= U'A' && c <= U'Z') || (c >= U'ａ' && c <= U'ｚ') || (c >= U'Ａ' && c <= U'Ｚ') || (c >= U'0' && c <= U'9');
}
bool EndsWith(const std::u32string& s, const std::u32string& e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
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
        if (ja[i] == U'「' || ja[i] == U'『') {
            open = i;
            break;
        }
    if (open == std::u32string::npos) {
        u.text = ja;
        return u;
    }
    const char32_t closeCh = ja[open] == U'「' ? U'」' : U'』';
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

struct Cues {
    bool strongTail = false;  // the line ends in よ, ね, ぞ …
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
    while (!last.empty() && IsMark(last.back())) last.pop_back();
    std::vector<std::u32string> stages{last};
    bool tailStripped = false;
    for (int guard = 0; guard < 6; ++guard) {
        const std::u32string& s = stages.back();
        size_t best = 0;
        for (const auto& tail : t.tails)
            if (tail.size() > best && tail.size() < s.size() && EndsWith(s, tail)) best = tail.size();
        if (!best) break;
        std::u32string next = s.substr(0, s.size() - best);
        while (!next.empty() && IsMark(next.back())) next.pop_back();
        stages.push_back(next);
        tailStripped = true;
    }
    bool casualEnd = tailStripped;
    if (tailStripped)
        for (const auto& st : t.strongTails)
            if (EndsWith(stages[0], st) && stages[0].size() > st.size()) c.strongTail = true;
    for (const auto& r : t.cues) {
        bool hit = false;
        if (r.mode == "end") {
            for (const auto& s : stages)
                if (EndsWith(s, r.text)) hit = true;
        } else {
            for (size_t i = u.text.find(r.text); i != std::u32string::npos; i = u.text.find(r.text, i + 1)) {
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
    c.cend = casualEnd && !c.pend;
    return c;
}

std::string Key(const Cues& c) {
    if (c.narr) return "narr";
    if (c.empty) return "empty";
    if (c.set && !c.cend) return "set";
    if (c.keigo && c.pend) return "keigo-pend";
    if (c.pend) return "pend";
    if (c.keigo) return "keigo";
    if (c.arch) return "archaic";
    if (c.pany) return "polite-any";
    if (c.stiff) return "stiff";
    if (c.cend) return "casual";
    return "bare";
}

}  // namespace

std::string CueKey(const RegisterTables& t, const std::string& ja) { return Key(Scan(t, ja)); }

std::vector<RegDecision> DecideLevels(const RegisterTables& t, const std::vector<RegLine>& lines, const RegOptions& opt) {
    std::vector<RegDecision> out(lines.size());
    struct Prof { double pol = 0, cas = 0; int washi = 0; };
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
            prof[kv.first] = p;
        }
        std::vector<Cues> cues(j - i);
        std::vector<std::string> keys(j - i);
        for (size_t k = i; k < j; ++k) {
            if (!lines[k].dialogue) continue;
            cues[k - i] = Scan(t, lines[k].ja);
            keys[k - i] = Key(cues[k - i]);
            if (lines[k].speaker.empty()) continue;
            Prof& p = prof[lines[k].speaker];
            const std::string& key = keys[k - i];
            if (key == "pend" || key == "keigo-pend" || key == "set") p.pol += 1;
            if (key == "casual" || key == "stiff") p.cas += 1;
            if (cues[k - i].first == "washi") p.washi += 1;
        }
        for (size_t k = i; k < j; ++k) {
            RegDecision& d = out[k];
            if (!lines[k].dialogue) {
                // an unbracketed line is narration unless it ends like speech (inner monologue: …だよ, …です)
                const Cues c = Scan(t, lines[k].ja);
                const std::string key = Key(c);
                if (key == "pend" || key == "keigo-pend") {
                    d.level = key == "pend" ? Level::haeyo : Level::hapsyo;
                    d.key = "unbracketed-" + key;
                } else if (key == "casual" && c.strongTail) {
                    d.level = Level::hae;
                    d.key = "unbracketed-casual";
                } else {
                    d.level = Level::haera;
                    d.narration = true;
                    d.key = "narration";
                }
                continue;
            }
            const std::string& key = keys[k - i];
            d.key = key;
            d.firstPerson = !cues[k - i].first.empty();
            d.narration = key == "narr";
            auto row = t.levels.find(key);
            d.level = row != t.levels.end() ? row->second.level : Level::hae;
            if (opt.profiles && row != t.levels.end() && row->second.profile && !lines[k].speaker.empty()) {
                auto p = prof.find(lines[k].speaker);
                if (p != prof.end() && p->second.pol + p->second.cas > 0) {
                    const double share = p->second.pol / (p->second.pol + p->second.cas);
                    const Level was = d.level;
                    if (share > opt.threshold) d.level = Level::haeyo;
                    else if (key == "bare" || key == "empty") d.level = Level::hae;
                    d.byProfile = d.level != was;
                }
            }
            auto p = prof.find(lines[k].speaker);
            if (p != prof.end() && p->second.washi > 0 && (key == "archaic" || cues[k - i].arch)) d.persona = Persona::oldMale;
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
        if (polite && !d.narration && n.ctype == CType::final_ && (n.mood == Mood::q || n.mood == Mood::imp) &&
            !firstPersonInSource && !subjectSeen && !n.aux) {
            std::string pos = n.vpos;
            std::string cls = n.kcls;
            if (gen && (pos.empty() || cls.empty())) {
                const PredicateInfo info = LookupPredicate(*gen, n.lemma);
                if (pos.empty()) pos = info.pos;
                if (cls.empty()) cls = info.cls;
            }
            bool excluded = false;
            for (const auto& x : rt.honExclude)
                if (x == n.lemma) excluded = true;
            if (pos == "V" && cls != "COP" && cls != "EXIST" && !excluded) r.hon = true;
        }
        if (n.ctype == CType::final_) subjectSeen = false;
        out.push_back(r);
    }
    return out;
}

}  // namespace hd8gen
