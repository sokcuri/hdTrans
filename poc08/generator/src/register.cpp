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
    std::map<std::string, LevelRow> levels;
    std::vector<std::string> honExclude;
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
bool EndsInTail(const std::u32string& s, const TailRow& tail) {
    if (s.size() <= tail.text.size() || !EndsWith(s, tail.text)) return false;
    return tail.notAfter.find(s[s.size() - tail.text.size() - 1]) == std::u32string::npos;
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

struct Cues {
    bool strongTail = false;  // a sentence of the line ends in よ, ね, ぞ …
    bool lastStrong = false;  // the last sentence ends in one of them (its outermost tail particle)
    bool lastKa = false;      // the last sentence ends in the question particle か
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
            // a row with notafter is an auxiliary (ます): it cannot start the utterance either (ますます)
            return !r.notAfter.empty() && (at == 0 || r.notAfter.find(str[at - 1]) != std::u32string::npos);
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
    c.cend = casualEnd && !c.pend;
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
    struct Prof { double pol = 0, cas = 0; std::map<int, int> voice; };
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
            if (!lines[k].dialogue) continue;
            cues[k - i] = Scan(t, lines[k].ja);
            keys[k - i] = Key(cues[k - i]);
            if (lines[k].speaker.empty()) continue;
            Prof& p = prof[lines[k].speaker];
            const std::string& key = keys[k - i];
            if (key == "pend" || key == "keigo-pend" || key == "set") p.pol += 1;
            if (key == "casual" || key == "keigo-casual" || key == "stiff") p.cas += 1;
            const Persona v = VoiceOf(t, lines[k].ja);
            if (v != Persona::none) p.voice[static_cast<int>(v)] += 1;
        }
        for (size_t k = i; k < j; ++k) {
            RegDecision& d = out[k];
            if (!lines[k].dialogue) {
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
            const std::string& key = keys[k - i];
            d.key = key;
            d.firstPerson = !cues[k - i].first.empty();
            d.narration = key == "narr";
            auto row = t.levels.find(key);
            d.level = row != t.levels.end() ? row->second.level : Level::hae;
            if (row != t.levels.end()) d.spec = row->second.spec;
            if (row != t.levels.end() && row->second.keep) d.keep = true;
            // REG-8: a line without its own level cue speaks like the speaker's cued lines in the scene: 해요
            // when more of them are polite than plain
            if (opt.profiles && row != t.levels.end() && row->second.profile && !lines[k].speaker.empty()) {
                auto p = prof.find(lines[k].speaker);
                if (p != prof.end() && p->second.pol > p->second.cas) {
                    d.byProfile = d.level != Level::haeyo;
                    d.level = Level::haeyo;
                    if (d.byProfile) d.spec = "REG-8";
                }
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
