// S, Z and R for the 0.8 driver (poc08/ir/stages.h).
// S: utterances (the line, and its top-level 「」『』≪≫ quotes), scenes, speakers from name lines, the cue summary
//    of each utterance, a per-scene narration flag, and a scene-scoped name registry (Mentions).
// Z: nothing yet (Clause.subj stays as L left it).
// R: the speech level of each utterance (hd8gen register model: cues, scene-scoped speaker profiles, default)
//    and the level, persona and addressee -시- of each V node. --speech strict keeps 0.7.x's meaning.
// Data: <data>/generator/data/register (cues.tsv, levels.tsv, hon.tsv, voices.tsv).
#include <map>
#include <memory>
#include <unordered_map>

#include "hd8gen/generator.h"
#include "hd8gen/register.h"
#include "ir/stages.h"
#include "ir/utf8.h"

namespace hd08 {

struct DiscourseData {
    std::shared_ptr<const hd8gen::RegisterTables> reg;
    std::shared_ptr<const hd8gen::Tables> gen;
};

DiscourseData* LoadDiscourse(const Config& cfg, std::string& err) {
    auto* d = new DiscourseData;
    d->reg = hd8gen::LoadRegisterTables(cfg.data + "/generator/data/register", err);
    if (d->reg) d->gen = hd8gen::LoadTables(cfg.data + "/generator/data/ko", err);
    if (!d->reg || !d->gen) {
        delete d;
        return nullptr;
    }
    return d;
}
void FreeDiscourse(DiscourseData* data) { delete data; }

namespace {

bool IsOpen(uint32_t cp) { return cp == 0x300C || cp == 0x300E || cp == 0x226A; }
bool IsClose(uint32_t cp) { return cp == 0x300D || cp == 0x300F || cp == 0x226B; }

// A short line without marks that is followed by a quote line names the speaker (VN scripts).
bool IsNameLine(const std::string& s) {
    size_t i = 0, n = 0;
    while (i < s.size()) {
        const uint32_t cp = NextCodePoint(s, i);
        if (cp == ' ' || cp == 0x3000) continue;
        if (IsOpen(cp) || IsClose(cp) || cp == 0x3002 || cp == 0x3001 || cp == 0xFF01 || cp == 0xFF1F || cp == 0x2026) return false;
        ++n;
    }
    return n > 0 && n <= 12;
}
bool StartsWithQuote(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const uint32_t cp = NextCodePoint(s, i);
        if (cp == ' ' || cp == 0x3000) continue;
        return IsOpen(cp);
    }
    return false;
}
bool IsSceneMark(const std::string& s) {
    size_t i = 0, n = 0, marks = 0;
    while (i < s.size()) {
        const uint32_t cp = NextCodePoint(s, i);
        if (cp == ' ' || cp == 0x3000) continue;
        ++n;
        if (cp == 0x25C6 || cp == 0x25C7 || cp == 0x25A0 || cp == 0x25A1 || cp == 0xFF0A || cp == '*' || cp == '-' || cp == 0x2015)
            ++marks;
    }
    return n == 0 || marks == n;
}

uint8_t SourceLevelOf(std::string key) {
    if (key.rfind("quotes-", 0) == 0) key = key.substr(7);  // REG-15: the line's last quote
    if (key == "pend" || key == "polite-any") return srclevel::desu_masu;
    if (key == "keigo-pend" || key == "keigo" || key == "set") return srclevel::keigo;
    if (key == "archaic") return srclevel::archaic;
    if (key == "casual" || key == "stiff" || key == "keigo-casual" || key == "auth-plain" || key == "decl-zo" ||
        key == "exclaim-ne" || key == "exclaim")
        return srclevel::casual;
    return srclevel::plain;
}

}  // namespace

void StageS(const DiscourseData& d, const Config&, Doc& doc, std::vector<ForcedSpan>&) {
    doc.scenes.clear();
    doc.speakers.clear();
    std::unordered_map<std::string, uint32_t> speakerIds;
    // scenes: cut at blank lines and scene marks
    Scene cur;
    bool open = false;
    for (uint32_t i = 0; i < doc.lines.size(); ++i) {
        const bool cut = IsSceneMark(doc.lines[i].src);
        if (cut) {
            if (open) doc.scenes.push_back(cur);
            open = false;
            continue;
        }
        if (!open) {
            cur = Scene{};
            cur.firstLine = i;
            open = true;
        }
        cur.lastLine = i;
    }
    if (open) doc.scenes.push_back(cur);
    std::vector<uint32_t> sceneOf(doc.lines.size(), 0);
    for (uint32_t s = 0; s < doc.scenes.size(); ++s)
        for (uint32_t i = doc.scenes[s].firstLine; i <= doc.scenes[s].lastLine; ++i) sceneOf[i] = s + 1;
    // per-scene narration flag: a scene with a quote line has narration outside its quotes; a scene without one
    // is all speech (captions, game hooks) unless its lines read like narration
    std::vector<bool> sceneNarrated(doc.scenes.size() + 1, true);
    for (uint32_t s = 0; s < doc.scenes.size(); ++s) {
        size_t quoted = 0, speechy = 0, n = 0;
        for (uint32_t i = doc.scenes[s].firstLine; i <= doc.scenes[s].lastLine; ++i) {
            const std::string& src = doc.lines[i].src;
            ++n;
            if (StartsWithQuote(src)) ++quoted;
            const std::string key = hd8gen::CueKey(*d.reg, src);
            if (key == "pend" || key == "keigo-pend" || key == "set") ++speechy;
        }
        sceneNarrated[s + 1] = quoted > 0 || n == 0 || speechy * 10 < n * 3;
    }
    for (uint32_t i = 0; i < doc.lines.size(); ++i) {
        Line& line = doc.lines[i];
        line.utts.clear();
        uint32_t speaker = 0;
        if (i > 0 && IsNameLine(doc.lines[i - 1].src) && StartsWithQuote(line.src)) {
            std::string name;
            for (char c : doc.lines[i - 1].src)
                if (c != ' ') name.push_back(c);
            auto it = speakerIds.find(name);
            if (it == speakerIds.end()) {
                Speaker sp;
                sp.name = doc.I(name);
                doc.speakers.push_back(sp);
                it = speakerIds.emplace(name, static_cast<uint32_t>(doc.speakers.size())).first;
            }
            speaker = it->second;
        }
        const std::string key = hd8gen::CueKey(*d.reg, line.src);
        Utterance whole;
        whole.span = {0, static_cast<uint32_t>(line.src.size())};
        whole.scene = sceneOf[i];
        whole.narration = sceneNarrated[sceneOf[i]];
        whole.sourceLevel = SourceLevelOf(key);
        line.utts.push_back(whole);
        int depth = 0;
        size_t k = 0;
        uint32_t openAt = 0;
        while (k < line.src.size()) {
            const size_t at = k;
            const uint32_t cp = NextCodePoint(line.src, k);
            if (IsOpen(cp) && depth++ == 0) openAt = static_cast<uint32_t>(at);
            if (IsClose(cp) && depth > 0 && --depth == 0) {
                Utterance q;
                q.span = {openAt, static_cast<uint32_t>(k)};
                q.depth = 1;
                q.scene = sceneOf[i];
                q.speaker = speaker;
                q.sourceLevel = whole.sourceLevel;
                line.utts.push_back(q);
            }
        }
        if (speaker) {
            Speaker& sp = doc.speakers[speaker - 1];
            if (whole.sourceLevel == srclevel::desu_masu) ++sp.desuMasu;
            else if (whole.sourceLevel == srclevel::keigo) ++sp.keigo;
            else ++sp.plain;
        }
    }
    // the name registry: a word before an honorific suffix, or a speaker name, is a name within its scene; every
    // occurrence of it in the scene becomes a Mention (scope: the scene, never the whole document)
    for (uint32_t s = 0; s < doc.scenes.size(); ++s) {
        std::map<std::string, int> evidence;
        for (uint32_t i = doc.scenes[s].firstLine; i <= doc.scenes[s].lastLine; ++i) {
            const Line& line = doc.lines[i];
            for (size_t t = 0; t + 1 < line.toks.size(); ++t) {
                const std::string_view next = doc.S(line.toks[t + 1].surf);
                if (hd8gen::IsHonorificSuffix(*d.reg, std::string(next)) && line.toks[t].pos != Pos::sym)
                    evidence[std::string(doc.S(line.toks[t].surf))] += 3;
                if (line.toks[t].flags & tokflag::NameCand) evidence[std::string(doc.S(line.toks[t].surf))] += 1;
            }
            if (i > 0 && IsNameLine(doc.lines[i - 1].src)) evidence[doc.lines[i - 1].src] += 5;
        }
        for (uint32_t i = doc.scenes[s].firstLine; i <= doc.scenes[s].lastLine; ++i) {
            Line& line = doc.lines[i];
            line.mentions.clear();
            for (const auto& [name, ev] : evidence) {
                if (ev < 3 || name.empty()) continue;
                for (size_t p = line.src.find(name); p != std::string::npos; p = line.src.find(name, p + name.size())) {
                    Mention m;
                    m.src = {static_cast<uint32_t>(p), static_cast<uint32_t>(p + name.size())};
                    m.entity = doc.I(name);
                    m.evidence = static_cast<float>(ev);
                    Decision dec;
                    dec.stage = 'S';
                    dec.kind = ev >= 5 ? Ev::Cued : Ev::NameGuess;
                    dec.rule = doc.I("code:scene-name-registry");
                    dec.src = m.src;
                    dec.score = m.evidence;
                    m.decision = line.Decide(dec);
                    line.mentions.push_back(m);
                }
            }
        }
    }
}

void StageZ(const DiscourseData&, const Config&, Doc&, Line&, int) {}

namespace {

hd8gen::Level Forced(Speech s) {
    switch (s) {
        case Speech::Haera: return hd8gen::Level::haera;
        case Speech::Hae: return hd8gen::Level::hae;
        case Speech::Haeyo: return hd8gen::Level::haeyo;
        case Speech::Hapsyo: return hd8gen::Level::hapsyo;
        default: return hd8gen::Level::none;
    }
}

uint8_t ToIrLevel(hd8gen::Level l) {
    unsigned v = 0;
    Parse(F::level, hd8gen::Name(l == hd8gen::Level::none ? hd8gen::Level::haera : l), v);
    return static_cast<uint8_t>(v);
}

}  // namespace

void StageR(const DiscourseData& d, const Config& cfg, Doc& doc, Line& line) {
    line.reg.clear();
    line.lev.clear();
    // the scene of this line (read-only document state from S)
    uint32_t idx = 0;
    for (uint32_t i = 0; i < doc.lines.size(); ++i)
        if (&doc.lines[i] == &line) idx = i;
    const uint32_t scene = line.utts.empty() ? 0 : line.utts[0].scene;
    uint32_t first = idx, last = idx;
    if (scene && scene <= doc.scenes.size()) {
        first = doc.scenes[scene - 1].firstLine;
        last = doc.scenes[scene - 1].lastLine;
    }
    std::vector<hd8gen::RegLine> lines;
    for (uint32_t i = first; i <= last && i < doc.lines.size(); ++i) {
        const Line& l = doc.lines[i];
        hd8gen::RegLine r;
        r.ja = l.src;
        r.scene = 1;
        r.dialogue = l.utts.size() > 1 || (!l.utts.empty() && !l.utts[0].narration);
        for (const Utterance& u : l.utts)
            if (u.speaker && u.speaker <= doc.speakers.size()) r.speaker = std::string(doc.S(doc.speakers[u.speaker - 1].name));
        lines.push_back(r);
    }
    hd8gen::RegOptions ro;
    ro.profiles = cfg.speech == Speech::Auto;
    const std::vector<hd8gen::RegDecision> ds = hd8gen::DecideLevels(*d.reg, lines, ro);
    hd8gen::RegDecision dec = idx - first < ds.size() ? ds[idx - first] : hd8gen::RegDecision{};
    if (cfg.speech == Speech::Strict) {
        // 0.7.x meaning: plain speech is -다, です/ます is 합쇼
        const std::string k = dec.key.rfind("quotes-", 0) == 0 ? dec.key.substr(7) : dec.key;
        dec.level = (k == "pend" || k == "keigo-pend" || k == "set" || k == "keigo" || k == "keigo-casual" || k == "polite-any")
                        ? hd8gen::Level::hapsyo
                        : hd8gen::Level::haera;
    }
    const hd8gen::Level forced = Forced(cfg.speech);
    if (forced != hd8gen::Level::none && !dec.narration) dec.level = forced;
    Decision rd;
    rd.stage = 'R';
    const bool noCue = dec.key == "bare" || dec.key == "empty" || dec.key == "quotes-bare" || dec.key == "quotes-empty";
    rd.kind = dec.byProfile ? Ev::Cued : (noCue ? Ev::Default : Ev::Cued);
    rd.rule = doc.I("code:register/" + dec.key);
    rd.src = {0, static_cast<uint32_t>(line.src.size())};
    const uint32_t id = line.Decide(rd);
    for (uint16_t u = 0; u < line.utts.size(); ++u) {
        UttLevel lv;
        lv.utt = u;
        const bool quoted = line.utts[u].depth > 0 || !line.utts[u].narration;
        lv.level = ToIrLevel(quoted && !dec.narration ? dec.level : hd8gen::Level::haera);
        unsigned p = 0;
        Parse(F::persona, hd8gen::Name(dec.persona), p);
        lv.persona = static_cast<uint8_t>(p);
        lv.decision = id;
        line.lev.push_back(lv);
    }
    // V nodes: the level of their utterance, and the addressee -시- (hd8gen::AssignRegister)
    std::vector<hd8gen::Node> nodes;
    for (const KNode& k : line.kir) {
        hd8gen::Node n;
        n.id = k.id;
        n.kind = static_cast<hd8gen::Kind>(static_cast<uint8_t>(k.kind));
        n.lemma = std::string(doc.S(k.lemma));
        unsigned m = 0;
        if (Parse(F::mood, Name(F::mood, k.mood), m)) n.mood = static_cast<hd8gen::Mood>(m);
        n.ctype = k.ctype == ctype::final ? hd8gen::CType::final_ : hd8gen::CType::conn;
        n.aux = k.aux;
        n.utt = k.utt;
        if (k.kind == KK::P) n.kase = std::string(Name(F::kase, k.kase));
        if (k.kind == KK::Punct) {
            unsigned pu = 0;
            if (Parse(F::punct, Name(F::punct, k.punct), pu)) n.punct = static_cast<hd8gen::Punct>(pu + 1);
        }
        nodes.push_back(n);
    }
    // REG-16: Z's constraint rules back the addressee as subject (an omitted subject classed addressee by a rule,
    // not by default): a polite question of that clause may take -시- as with an honorific verb
    for (const Clause& c : line.clauses)
        if (c.subj.kind == static_cast<uint8_t>(refkind::addressee) && c.subj.decision > 0 &&
            c.subj.decision <= line.decisions.size() && line.decisions[c.subj.decision - 1].kind != Ev::Default)
            dec.honSonkei = true;
    const std::vector<hd8gen::Reg> regs = hd8gen::AssignRegister(*d.reg, nodes, dec, dec.firstPerson, d.gen.get());
    for (const auto& g : regs) {
        KReg r;
        r.node = g.node;
        uint8_t utt = 0;
        for (const KNode& k : line.kir)
            if (k.id == g.node) utt = static_cast<uint8_t>(k.utt);
        const bool narr = utt < line.utts.size() && line.utts[utt].depth == 0 && line.utts[utt].narration;
        r.level = ToIrLevel(narr || dec.narration ? hd8gen::Level::haera : g.level);
        unsigned p = 0;
        Parse(F::persona, hd8gen::Name(g.persona), p);
        r.persona = static_cast<uint8_t>(p);
        r.hon = g.hon && cfg.speech != Speech::Strict;
        r.decision = id;
        line.reg.push_back(r);
    }
}

}  // namespace hd08
