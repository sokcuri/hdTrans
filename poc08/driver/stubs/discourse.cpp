// Stub S, Z and R until poc08/discourse lands: one utterance per line (or per top-level quote), and
// one speech level per utterance from --speech.
#include "ir/stages.h"
#include "ir/utf8.h"

namespace hd08 {

struct DiscourseData {};

namespace {

bool IsOpen(uint32_t cp) { return cp == 0x300C || cp == 0x300E; }
bool IsClose(uint32_t cp) { return cp == 0x300D || cp == 0x300F; }

uint8_t LevelFor(const Config& cfg, bool quoted) {
    switch (cfg.speech) {
    case Speech::Haera: return level::haera;
    case Speech::Hae: return level::hae;
    case Speech::Haeyo: return level::haeyo;
    case Speech::Hapsyo: return level::hapsyo;
    case Speech::Soft: case Speech::Auto: return quoted ? level::hae : level::haera;
    case Speech::Strict: break;
    }
    return level::haera;
}

}  // namespace

DiscourseData* LoadDiscourse(const Config&, std::string&) { return new DiscourseData; }
void FreeDiscourse(DiscourseData* data) { delete data; }

void StageS(const DiscourseData&, const Config&, Doc& doc, std::vector<ForcedSpan>&) {
    for (Line& line : doc.lines) {
        line.utts.clear();
        const std::string_view s = line.src;
        Utterance whole;
        whole.span = {0, static_cast<uint32_t>(s.size())};
        whole.narration = true;
        line.utts.push_back(whole);
        int depth = 0;
        size_t i = 0;
        uint32_t open = 0;
        while (i < s.size()) {
            const size_t at = i;
            const uint32_t cp = NextCodePoint(s, i);
            if (IsOpen(cp) && depth++ == 0) open = static_cast<uint32_t>(at);
            if (IsClose(cp) && depth > 0 && --depth == 0) {
                Utterance q;
                q.span = {open, static_cast<uint32_t>(i)};
                q.depth = 1;
                line.utts.push_back(q);
            }
        }
    }
}

void StageZ(const DiscourseData&, const Config&, Doc&, Line&, int) {}

void StageR(const DiscourseData&, const Config& cfg, Doc& doc, Line& line) {
    line.reg.clear();
    line.lev.clear();
    Decision d;
    d.stage = 'R';
    d.kind = Ev::Default;
    d.rule = doc.I("code:stub-register");
    d.src = {0, static_cast<uint32_t>(line.src.size())};
    const uint32_t id = line.Decide(d);
    for (uint16_t u = 0; u < line.utts.size(); u++) {
        UttLevel lv;
        lv.utt = u;
        lv.level = LevelFor(cfg, line.utts[u].depth > 0);
        lv.decision = id;
        line.lev.push_back(lv);
    }
    for (const KNode& n : line.kir) {
        if (n.kind != KK::V) continue;
        KReg r;
        r.node = n.id;
        r.level = LevelFor(cfg, false);
        r.decision = id;
        line.reg.push_back(r);
    }
}

}  // namespace hd08
