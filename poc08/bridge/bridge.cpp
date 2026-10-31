#include "bridge.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "driver/driver.h"
#include "hd8/stage_hints.h"
#include "bridge/attach.h"
#include "driver/report.h"
#include "ir/ir_json.h"
#include "core/word/attach.h"
#include "core/word/item.h"
#include "lift.h"
#include "rule.h"

namespace hd08::bridge {

namespace {

// One sentence as last written: the compose stage may run a line more than once (trial passes),
// and only the last pass counts.
struct SentenceRecord {
    std::string why, stages, reselections, attach;
    uint64_t points = 0, overrides = 0, reselected = 0;
    bool generated = false;
    uint64_t lifted = 0;
    std::vector<LiftMiss> misses;
};

struct DocState {
    Doc doc;                     // the side IR: one Line per line of the call
    std::vector<uint32_t> from;  // per line: where the lift's source search goes on
    std::vector<std::pair<int, int>> last;  // per line: the (chunk, sentence) lifted last; an earlier one starts a pass
    std::vector<std::map<std::pair<int, int>, SentenceRecord>> records;  // per line, by (chunk, sentence)
    std::vector<uint32_t> fromSel;  // per line: the lift cursor of reselect
    std::vector<std::pair<int, int>> lastSel;
    // per line: where the search for the next chunk's text goes on, and the chunk anchored last (attach)
    std::vector<uint32_t> chunkFrom;
    std::vector<int> lastChunk;
    bool caption = false;  // 0.8-rule: the document reads as captions, so no part changes it
};

constexpr size_t kMaxStructureBytes = 4096;

struct ChunkState {
    std::vector<uint32_t> ends;  // token ends of the analyzer, sorted, in chunk bytes
    std::atomic<uint64_t>* refits = nullptr;
    // attach: the chunk's line and where its text starts in the line's source (base), or in the line's
    // normalised text when only that holds it (viaNorm, mapped to the source by Line.map); -1 when neither does
    int line = -1;
    int64_t base = -1;
    bool viaNorm = false;
};

}  // namespace

struct Bridge {
    Options opt;
    Engine engine;
    LiftTable lift;
    std::shared_ptr<const RuleData> rule;
    hdTrans::hooks::Hooks hooks;
    bool analyzerReal = false, generatorReal = false, transferReal = false;
    std::atomic<uint64_t> sentences{0}, generated{0}, kept{0}, liftedItems{0}, failedItems{0}, normalized{0},
        segmentedChunks{0}, refits{0}, points{0}, overrides{0}, reselected{0};
    std::mutex mu;  // guards the fields below
    std::string why, stages, reselections, attachments;
    std::map<std::pair<std::string, std::string>, uint64_t> misses;
};

namespace {

void* BeginDocument(void* context, std::span<const std::string> lines) {
    auto& b = *static_cast<Bridge*>(context);
    auto* ds = new DocState;
    for (size_t i = 0; i < lines.size(); i++) {
        Line l;
        l.id = static_cast<uint32_t>(i + 1);
        l.src = lines[i];
        if (!l.src.empty() && l.src.back() == '\r') l.src.pop_back();
        ds->doc.lines.push_back(std::move(l));
    }
    ds->from.assign(lines.size(), 0);
    ds->last.assign(lines.size(), {-1, -1});
    ds->records.resize(lines.size());
    ds->fromSel.assign(lines.size(), 0);
    if (b.rule) ds->caption = CaptionDocument(*b.rule, std::vector<std::string>(lines.begin(), lines.end()));
    ds->lastSel.assign(lines.size(), {-1, -1});
    ds->chunkFrom.assign(lines.size(), 0);
    ds->lastChunk.assign(lines.size(), -1);
    if (b.opt.sideIr || b.opt.decide || b.opt.generate || b.opt.reselect || b.opt.attach) {
        // The IR alongside: analysis, scope and structure of every line, read by D, R and --why.
        const Config& cfg = b.engine.cfg;
        const std::vector<ForcedSpan> none;
        for (Line& l : ds->doc.lines) {
            StageN(*b.engine.analyzer, cfg, ds->doc, l);
            StageA(*b.engine.analyzer, cfg, ds->doc, l, none);
        }
        std::vector<ForcedSpan> forced;
        StageS(*b.engine.discourse, cfg, ds->doc, forced);
        for (Line& l : ds->doc.lines) {
            // Structure is not linear in the length of a line; a very long line keeps its tokens only.
            if (l.src.size() > kMaxStructureBytes) continue;
            const int paths = l.alt.toks.empty() ? 1 : 2;
            for (int p = 0; p < paths; p++) {
                StageC(*b.engine.structure, cfg, ds->doc, l, p);
                StageL(*b.engine.structure, cfg, ds->doc, l, p);
                StageZ(*b.engine.discourse, cfg, ds->doc, l, p);
            }
        }
    }
    return ds;
}

void EndDocument(void* context, void* doc) {
    auto& b = *static_cast<Bridge*>(context);
    auto* ds = static_cast<DocState*>(doc);
    std::string why, stages = b.opt.sideIr ? DocJson(ds->doc) + '\n' : std::string();
    std::lock_guard<std::mutex> lock(b.mu);
    for (const auto& line : ds->records) {
        for (const auto& [key, r] : line) {
            b.sentences++;
            (r.generated ? b.generated : b.kept)++;
            b.liftedItems += r.lifted;
            b.failedItems += r.misses.size();
            for (const LiftMiss& m : r.misses) b.misses[{m.kind, m.key}]++;
            why += r.why;
            stages += r.stages;
            b.points += r.points;
            b.overrides += r.overrides;
            b.reselected += r.reselected;
            b.reselections += r.reselections;
            b.attachments += r.attach;
        }
    }
    b.why += why;
    b.stages += stages;
    delete ds;
}

bool Normalize(void* context, void*, int, std::string_view text, std::string& out) {
    auto& b = *static_cast<Bridge*>(context);
    Doc d;
    Line l;
    l.src = text;
    StageN(*b.engine.analyzer, b.engine.cfg, d, l);
    if (l.norm != text) b.normalized++;
    out = std::move(l.norm);
    return true;
}

// A cut within this many units of the best path is free for 0.7.x: everything below one rank-2 count (only
// connection rules, unknown words and derived or fallback words bind).
constexpr float kTieSlackUnits = 999.0f;

// Small vowel kana, the long-vowel mark and wave dashes: the marks that lengthen a spoken word.
uint32_t CodePoint(std::string_view ch);

bool IsLengthening(std::string_view ch) {
    const uint32_t c = CodePoint(ch);
    const bool smallHira = c >= 0x3041 && c <= 0x3049 && (c - 0x3041) % 2 == 0;
    const bool smallKata = c >= 0x30A1 && c <= 0x30A9 && (c - 0x30A1) % 2 == 0;
    return smallHira || smallKata || c == 0x30FC || c == 0x301C || c == 0xFF5E;
}

uint32_t CodePoint(std::string_view ch) {
    if (ch.size() != 3) return 0;
    return (static_cast<uint32_t>(static_cast<unsigned char>(ch[0]) & 0x0F) << 12) |
           (static_cast<uint32_t>(static_cast<unsigned char>(ch[1]) & 0x3F) << 6) |
           (static_cast<uint32_t>(static_cast<unsigned char>(ch[2]) & 0x3F));
}

// Vowel of a hiragana letter (U+3041..U+3093), '-' for none.
char Vowel(uint32_t c) {
    static constexpr char kVowels[] =
        "aaiiuueeooaaiiuueeooaaiiuueeooaaii-uueeooaiueoaaaiiiuuueeeoooaiueoaauuooaiueoaaieo-";
    return c >= 0x3041 && c <= 0x3093 ? kVowels[c - 0x3041] : '-';
}

// A wave dash (U+301C, U+FF5E) right after a kana becomes the long-vowel mark U+30FC.
std::string LengtheningMarks(std::string_view text) {
    std::string out(text);
    uint32_t prev = 0;
    for (size_t i = 0; i + 3 <= out.size();) {
        const unsigned char c0 = static_cast<unsigned char>(out[i]);
        const size_t n = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xE ? 3 : 4;
        const uint32_t c = n == 3 ? CodePoint(std::string_view(out).substr(i, 3)) : 0;
        const bool kana = prev >= 0x3041 && prev <= 0x30FA;
        if ((c == 0x301C || c == 0xFF5E) && kana) {
            out[i] = static_cast<char>(0xE3);
            out[i + 1] = static_cast<char>(0x83);
            out[i + 2] = static_cast<char>(0xBC);
        } else {
            prev = c;
        }
        i += n;
    }
    return out;
}

// attach: where the chunk's text stands in its line, searched forward from the chunk before it (the first
// chunk of a pass starts over).
void AnchorChunk(DocState& ds, ChunkState& cs, int line, int chunk, std::string_view text) {
    if (line < 0 || static_cast<size_t>(line) >= ds.doc.lines.size()) return;
    const auto li = static_cast<size_t>(line);
    if (chunk <= ds.lastChunk[li]) ds.chunkFrom[li] = 0;
    ds.lastChunk[li] = chunk;
    cs.line = line;
    const Line& l = ds.doc.lines[li];
    const uint32_t from = ds.chunkFrom[li];
    size_t at = from <= l.src.size() ? l.src.find(text, from) : std::string::npos;
    if (at != std::string::npos) {
        cs.base = static_cast<int64_t>(at);
        ds.chunkFrom[li] = static_cast<uint32_t>(at + text.size());
        return;
    }
    // the normalised text (the normalize part writes the chunk from it); the search goes on from the norm byte
    // of the source cursor
    if (l.map.size() != l.norm.size() + 1) return;
    size_t nfrom = 0;
    while (nfrom < l.norm.size() && l.map[nfrom] < from) nfrom++;
    at = l.norm.find(text, nfrom);
    if (at == std::string::npos) return;
    cs.base = static_cast<int64_t>(at);
    cs.viaNorm = true;
    ds.chunkFrom[li] = l.map[std::min(at + text.size(), l.norm.size())];
}

void* BeginChunk(void* context, void* doc, int line, int chunk, std::string_view text) {
    auto& b = *static_cast<Bridge*>(context);
    auto* ds = static_cast<DocState*>(doc);
    const bool segment = b.opt.segment && !(ds != nullptr && ds->caption);
    if (!segment && !b.opt.attach) return nullptr;
    if (!segment) {
        // attach alone: no ends, so Fits (when installed) holds everywhere
        auto* cs = new ChunkState;
        cs->refits = &b.refits;
        if (ds != nullptr) AnchorChunk(*ds, *cs, line, chunk, text);
        return cs;
    }
    // 0.7.x's own normalization has written a lengthening ー after kana as a wave dash; the analyzer reads the
    // mark it was (same byte length, so the offsets stay the chunk's)
    const std::string analysed = LengtheningMarks(text);
    Doc d;
    Line l;
    l.src = analysed;
    StageN(*b.engine.analyzer, b.engine.cfg, d, l);
    StageA(*b.engine.analyzer, b.engine.cfg, d, l, {});
    auto* cs = new ChunkState;
    cs->refits = &b.refits;
    if (b.opt.attach && ds != nullptr) AnchorChunk(*ds, *cs, line, chunk, text);
    // Only the boundaries that connection rules or the rank 1-3 preferences decided bind 0.7.x; a cut that
    // the fewest-units rank or a tie-breaker chose is left to 0.7.x's own segmentation.
    AnalyzerAllowedEnds(*b.engine.analyzer, analysed, kTieSlackUnits, &cs->ends);
    for (const Token& t : l.toks) {
        cs->ends.push_back(t.src.end);
        // a lengthening at the end of a word may be cut off it (ね|ぇ, から|ぁ), so that 0.7.x keeps its
        // reading of the word itself
        uint32_t cut = t.src.end;
        while (cut > t.src.begin) {
            uint32_t at = cut - 1;
            while (at > t.src.begin && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) at--;
            if (!IsLengthening(text.substr(at, cut - at))) break;
            cut = at;
        }
        if (cut > t.src.begin && cut < t.src.end) cs->ends.push_back(cut);
        // inside an inflected word 0.7.x splits a stem key from its ending (と言 + われる): those cuts are its own
        const bool conjugated = t.pos == Pos::v1 || t.pos == Pos::v5 || t.pos == Pos::vs || t.pos == Pos::vk ||
                                t.pos == Pos::vz || t.pos == Pos::adj || t.pos == Pos::aux;
        if (conjugated)
            for (uint32_t at = t.src.begin + 1; at < t.src.end; at++)
                if ((static_cast<unsigned char>(text[at]) & 0xC0) != 0x80) cs->ends.push_back(at);
        // a final small tsu marks a clipped, emphatic ending (ないの|っ, いい|っ) unless it is a verb or adjective's
        // sound-changed form (行っ+て)
        const bool inflecting = t.pos == Pos::v1 || t.pos == Pos::v5 || t.pos == Pos::vs || t.pos == Pos::vk ||
                                t.pos == Pos::vz || t.pos == Pos::adj || t.pos == Pos::aux;
        if (!inflecting && t.src.end - t.src.begin >= 6) {
            const uint32_t last = CodePoint(text.substr(t.src.end - 3, 3));
            if (last == 0x3063 || last == 0x30C3) cs->ends.push_back(t.src.end - 3);
        }
        // a particle or auxiliary lengthened by its own vowel (ね|え, よ|お)
        const bool function = t.pos == Pos::aux || t.pos == Pos::p_case || t.pos == Pos::p_topic ||
                              t.pos == Pos::p_conj || t.pos == Pos::p_final || t.pos == Pos::p_adv;
        if (function && t.src.end - t.src.begin >= 6) {
            const uint32_t last = CodePoint(text.substr(t.src.end - 3, 3)), prev = CodePoint(text.substr(t.src.end - 6, 3));
            const bool plainVowel = last == 0x3042 || last == 0x3044 || last == 0x3046 || last == 0x3048 || last == 0x304A;
            if (plainVowel && Vowel(last) != '-' && Vowel(last) == Vowel(prev)) cs->ends.push_back(t.src.end - 3);
        }
    }
    // Inside a compound noun (a run of noun, prefix and suffix tokens) the analyzer's short units say nothing
    // about how 0.7.x should split it: 0.7.x keeps its own compound keys (生徒会 + 長 as well as 生徒 + 会長).
    auto nominal = [](Pos p) { return p == Pos::n || p == Pos::pn || p == Pos::num || p == Pos::pref || p == Pos::suf; };
    for (size_t i = 0; i < l.toks.size();) {
        size_t j = i;
        while (j < l.toks.size() && nominal(l.toks[j].pos) && (j == i || l.toks[j].src.begin == l.toks[j - 1].src.end)) j++;
        if (j > i + 1)
            for (uint32_t at = l.toks[i].src.begin + 1; at < l.toks[j - 1].src.end; at++)
                if ((static_cast<unsigned char>(text[at]) & 0xC0) != 0x80) cs->ends.push_back(at);
        i = j > i ? j : i + 1;
    }
    std::sort(cs->ends.begin(), cs->ends.end());
    cs->ends.erase(std::unique(cs->ends.begin(), cs->ends.end()), cs->ends.end());
    b.segmentedChunks++;
    return cs;
}

void EndChunk(void*, void* chunk) { delete static_cast<ChunkState*>(chunk); }

bool Fits(const void* chunk, uint32_t, uint32_t end, char) {
    const auto& cs = *static_cast<const ChunkState*>(chunk);
    if (cs.ends.empty() || end >= cs.ends.back()) return true;
    const bool fits = std::binary_search(cs.ends.begin(), cs.ends.end(), end);
    if (!fits) (*cs.refits)++;
    return fits;
}

// The tokens of whole inside span, with their bunsetsu and clauses (indexes renumbered; a clause or
// bunsetsu that reaches outside the span is left out), into sent. Path 1 is not copied.
void CopyAnalysis(const Line& whole, Span span, Line& sent) {
    std::vector<int> tok(whole.toks.size(), -1), bun(whole.buns.size(), -1), cl(whole.clauses.size(), -1);
    for (size_t i = 0; i < whole.toks.size(); i++) {
        if (!Contains(span, whole.toks[i].src)) continue;
        tok[i] = static_cast<int>(sent.toks.size());
        Token t = whole.toks[i];
        t.alt = static_cast<uint16_t>(sent.alts.size());
        for (uint16_t k = 0; k < whole.toks[i].nAlt && whole.toks[i].alt + k < whole.alts.size(); k++)
            sent.alts.push_back(whole.alts[whole.toks[i].alt + k]);
        sent.toks.push_back(t);
    }
    for (size_t i = 0; i < whole.buns.size(); i++) {
        const Bunsetsu& b = whole.buns[i];
        if (b.last >= tok.size() || tok[b.first] < 0 || tok[b.last] < 0) continue;
        bun[i] = static_cast<int>(sent.buns.size());
        sent.buns.push_back(b);
    }
    for (size_t i = 0; i < whole.buns.size(); i++) {
        if (bun[i] < 0) continue;
        Bunsetsu& b = sent.buns[static_cast<size_t>(bun[i])];
        b.first = static_cast<uint16_t>(tok[b.first]);
        b.last = static_cast<uint16_t>(tok[b.last]);
        b.content = static_cast<uint16_t>(b.content < tok.size() && tok[b.content] >= 0 ? tok[b.content] : b.first);
        b.head = b.head >= 0 && static_cast<size_t>(b.head) < bun.size() ? static_cast<int16_t>(bun[static_cast<size_t>(b.head)]) : -1;
        b.head2 = b.head2 >= 0 && static_cast<size_t>(b.head2) < bun.size() ? static_cast<int16_t>(bun[static_cast<size_t>(b.head2)]) : -1;
    }
    for (size_t i = 0; i < whole.clauses.size(); i++) {
        const Clause& c = whole.clauses[i];
        if (c.lastBun >= bun.size() || bun[c.firstBun] < 0 || bun[c.lastBun] < 0) continue;
        cl[i] = static_cast<int>(sent.clauses.size());
        sent.clauses.push_back(c);
    }
    auto mapTok = [&](std::vector<uint16_t>& v) {
        std::vector<uint16_t> out;
        for (uint16_t t : v)
            if (t < tok.size() && tok[t] >= 0) out.push_back(static_cast<uint16_t>(tok[t]));
        v = std::move(out);
    };
    for (size_t i = 0; i < whole.clauses.size(); i++) {
        if (cl[i] < 0) continue;
        Clause& c = sent.clauses[static_cast<size_t>(cl[i])];
        c.firstBun = static_cast<uint16_t>(bun[c.firstBun]);
        c.lastBun = static_cast<uint16_t>(bun[c.lastBun]);
        c.pred = static_cast<uint16_t>(c.pred < bun.size() && bun[c.pred] >= 0 ? bun[c.pred] : c.lastBun);
        c.parent = c.parent >= 0 && static_cast<size_t>(c.parent) < cl.size() ? static_cast<int16_t>(cl[static_cast<size_t>(c.parent)]) : -1;
        for (Arg& a : c.args)
            a.bun = a.bun < bun.size() && bun[a.bun] >= 0 ? static_cast<uint16_t>(bun[a.bun]) : uint16_t{0xFFFF};
        for (Referent* r : {&c.subj, &c.beneTo})
            if (r->bun != 0xFFFF) r->bun = r->bun < bun.size() && bun[r->bun] >= 0 ? static_cast<uint16_t>(bun[r->bun]) : uint16_t{0xFFFF};
        mapTok(c.consumed);
        mapTok(c.unfolded);
    }
    for (const Mention& m : whole.mentions)
        if (Overlaps(m.src, span)) sent.mentions.push_back(m);
}

// The sentence as a side line: the line's source and scope, the sentence's lifted nodes, the line's
// analysis within them.
bool SentenceLine(Bridge& b, DocState& ds, const hdTrans::hooks::Sentence& s, uint32_t& from, Line& sent,
                  std::vector<LiftMiss>& misses, std::vector<Span>* itemSpans) {
    const Line& whole = ds.doc.lines[static_cast<size_t>(s.line)];
    sent.id = whole.id;
    sent.name = std::to_string(s.line + 1) + "." + std::to_string(s.chunk) + "." + std::to_string(s.index);
    sent.src = whole.src;
    sent.norm = whole.norm;
    sent.map = whole.map;
    sent.utts = whole.utts;
    const bool lifted = Lift(b.lift, s, ds.doc, sent, from, misses, itemSpans);
    Span span{UINT32_MAX, 0};
    for (const KNode& n : sent.kir) {
        if (n.src.end <= n.src.begin) continue;
        span.begin = std::min(span.begin, n.src.begin);
        span.end = std::max(span.end, n.src.end);
    }
    if (span.begin < span.end) CopyAnalysis(whole, span, sent);
    return lifted;
}

// 0.7.x's own facts for one ending candidate of an item, as the lift records them for the selected one.
std::vector<std::string> CandidateFacts(const hdTrans::word::Item& it, std::string_view ending) {
    const std::string_view target = it.EntryText(it.selected);
    std::vector<std::string> facts{"t075.ko=" + std::string(target) + "|" + std::string(ending)};
    size_t m = target.find_first_of("&%$");
    std::string_view stem = target.substr(0, m == std::string_view::npos ? target.size() : m);
    while (!stem.empty() && stem.back() == ' ') stem.remove_suffix(1);
    if (m != std::string_view::npos && m + 4 <= target.size()) facts.push_back("t075.marker=" + std::string(target.substr(m, 4)));
    std::string_view tail = ending;
    while (!tail.empty() && (tail.front() >= '0' && tail.front() <= '9')) tail.remove_prefix(1);
    if (!stem.empty()) facts.push_back("t075.real=" + std::string(stem) + std::string(tail));
    return facts;
}

bool Reselect(void* context, void* doc, void*, const hdTrans::hooks::Sentence& s, hdTrans::word::Chain& chain) {
    auto& b = *static_cast<Bridge*>(context);
    auto& ds = *static_cast<DocState*>(doc);
    if (s.line < 0 || static_cast<size_t>(s.line) >= ds.doc.lines.size()) return false;
    const auto lineIndex = static_cast<size_t>(s.line);
    const std::pair<int, int> here{s.chunk, s.index};
    if (here <= ds.lastSel[lineIndex]) ds.fromSel[lineIndex] = 0;
    ds.lastSel[lineIndex] = here;
    Line sent;
    std::vector<LiftMiss> misses;
    std::vector<Span> spans;
    SentenceLine(b, ds, s, ds.fromSel[lineIndex], sent, misses, &spans);
    std::vector<hdTrans::word::Item*> items;
    for (hdTrans::word::Item* it = chain.head; it != nullptr; it = it->next) items.push_back(it);
    if (b.opt.attach) {
        // The whole line's analysis: a clause may reach past the sentence 0.7.x cut (the final 。 is a sentence of
        // its own), and the items find their tokens by their spans anyway.
        const Line& whole = ds.doc.lines[lineIndex];
        const Attachment att = AttachItems(whole, spans);
        Line named = whole;
        named.name = sent.name;
        ds.records[lineIndex][{s.chunk, s.index}].attach = AttachmentJson(ds.doc, named, spans, att) + '\n';
    }
    if (!b.opt.reselect) return false;
    SentenceRecord rec;
    bool changed = false;
    for (const PointDecision& p : DecidePoints(*b.engine.transfer, b.engine.cfg, ds.doc, sent)) {
        rec.points++;
        if (!p.overridden || p.family == "tame") continue;
        rec.overrides++;
        std::string jlem;
        for (uint32_t f : sent.decisions[p.decision - 1].because)
            if (ds.doc.S(f).starts_with("jlem=")) jlem = std::string(ds.doc.S(f).substr(5));
        std::string from, to;
        bool done = false;
        for (size_t k = 0; k < items.size() && k < spans.size() && !done; k++) {
            hdTrans::word::Item& it = *items[k];
            if (!Overlaps(spans[k], p.at) || it.DirectCount() == 0) continue;
            from = std::string(it.DirectText(it.DirectIndex()));
            for (int i = 0; i < it.DirectCount(); i++) {
                if (i == it.DirectIndex() || !it.DirectEnabled(i)) continue;
                const std::string_view text = it.DirectText(i);
                if (text.empty() || ClassOfFacts(*b.engine.transfer, p.family, CandidateFacts(it, text), jlem) != p.cls)
                    continue;
                it.SetDirectIndex(i, {hdTrans::word::Provenance::Scorer, -1});
                to = std::string(text);
                done = changed = true;
                break;
            }
        }
        rec.reselected += done;
        rec.reselections += std::to_string(s.line + 1) + "\t" + p.family + "\t" + p.base + "\t" + p.cls + "\t" +
                            (done ? "realised" : "unrealised") + "\t" + from + "\t" + to + "\n";
    }
    SentenceRecord& r = ds.records[lineIndex][{s.chunk, s.index}];
    r.points = rec.points;
    r.overrides = rec.overrides;
    r.reselected = rec.reselected;
    r.reselections = std::move(rec.reselections);
    return changed;
}

// The volitionality of clause c (main.vol, cl.vol): as the transfer's deciders read it (a passive, potential or
// spontaneous predicate is not volitional; a volitional, wish or imperative form is; then the predicate table,
// and + for any other verb or verbal noun).
std::string ClauseVolition(const Bridge& b, const Doc& doc, const Line& l, const Clause& c) {
    if (c.voice == voice::passive || c.voice == voice::potential || c.voice == voice::rare ||
        c.voice == voice::spontaneous)
        return "-";
    if ((c.modality & (modality::volitional | modality::want)) != 0 || c.force == force::imp) return "+";
    if (c.pred >= l.buns.size()) return "-";
    const Bunsetsu& pb = l.buns[c.pred];
    if (pb.content >= l.toks.size()) return "-";
    const Token& t = l.toks[pb.content];
    const bool verb = t.pos == Pos::v1 || t.pos == Pos::v5 || t.pos == Pos::vs || t.pos == Pos::vk || t.pos == Pos::vz;
    bool verbalNoun = false;
    if (t.pos == Pos::n && pb.content + 1u <= pb.last && pb.content + 1u < l.toks.size()) {
        const std::string_view next = doc.S(l.toks[pb.content + 1u].lemma);
        verbalNoun = next == "為る" || next == "する";
    }
    if (!verb && !verbalNoun) return "-";
    if (b.transferReal && b.engine.transfer != nullptr) {
        const std::string v = PredicateVolition(*b.engine.transfer, doc.S(t.lemma));
        if (!v.empty()) return v;
    }
    return "+";
}

// The source span of a chain item in its line: the token's source bytes when its text is them byte for byte (as
// the segment hook reads them), else its surface searched forward from the item before (as the lift does).
Span ItemSpan(const ChunkState& cs, const Line& l, const hdTrans::tokenizer::Tokens& tokens,
              const hdTrans::word::Item& it, uint32_t& from) {
    const auto toLine = [&](uint32_t at, uint32_t& out) {
        if (cs.base < 0) return false;
        const uint64_t p = static_cast<uint64_t>(cs.base) + at;
        if (cs.viaNorm) {
            if (p >= l.map.size()) return false;
            out = l.map[p];
        } else {
            if (p > l.src.size()) return false;
            out = static_cast<uint32_t>(p);
        }
        return true;
    };
    if (it.length <= 0 || it.start < 0) return {from, from};
    const std::string_view surface = static_cast<size_t>(it.start) < it.text.size()
                                         ? it.text.substr(static_cast<size_t>(it.start), static_cast<size_t>(it.length))
                                         : std::string_view();
    if (it.tokenIndex >= 0 && static_cast<size_t>(it.tokenIndex) < tokens.size()) {
        const auto ti = static_cast<size_t>(it.tokenIndex);
        const hdTrans::tokenizer::Token& t = tokens[ti];
        if (t.sourceEnd - t.sourceBegin == it.text.size() && tokens.Raw(ti) == it.text) {
            const uint32_t b0 = std::min(t.sourceBegin + static_cast<uint32_t>(it.start), t.sourceEnd);
            const uint32_t e0 = std::min(b0 + static_cast<uint32_t>(it.length), t.sourceEnd);
            uint32_t b1 = 0, e1 = 0;
            if (toLine(b0, b1) && toLine(e0, e1) && b1 < e1 && e1 <= l.src.size()) {
                from = e1;
                return {b1, e1};
            }
        }
    }
    if (surface.empty()) return {from, from};
    const size_t at = l.src.find(surface, from);
    const size_t reach = std::max<size_t>(24, 4 * surface.size());
    if (at == std::string::npos || at - from > reach) return {from, from};
    from = static_cast<uint32_t>(at + surface.size());
    return {static_cast<uint32_t>(at), from};
}

// The attach hook: C, L and Z of the whole line mapped onto the chain items, for the packs layer.
bool AttachHook(void* context, void* doc, void* chunk, const hdTrans::tokenizer::Tokens& tokens,
                const hdTrans::word::Chain& chain, hdTrans::word::Attachment& out) {
    if (doc == nullptr || chunk == nullptr) return false;
    const auto& b = *static_cast<const Bridge*>(context);
    const auto& ds = *static_cast<const DocState*>(doc);
    const auto& cs = *static_cast<const ChunkState*>(chunk);
    if (cs.line < 0 || static_cast<size_t>(cs.line) >= ds.doc.lines.size()) return false;
    const Line& whole = ds.doc.lines[static_cast<size_t>(cs.line)];
    if (whole.clauses.empty()) return false;
    std::vector<Span> spans;
    uint32_t from = 0;
    if (cs.base >= 0) {
        const uint64_t p = static_cast<uint64_t>(cs.base);
        from = cs.viaNorm ? (p < whole.map.size() ? whole.map[p] : 0) : static_cast<uint32_t>(std::min<uint64_t>(p, whole.src.size()));
    }
    for (const hdTrans::word::Item* it = chain.head; it != nullptr; it = it->next)
        spans.push_back(ItemSpan(cs, whole, tokens, *it, from));
    const Attachment att = AttachItems(whole, spans);
    const Doc& d = ds.doc;
    out.items.clear();
    out.clauses.clear();
    for (const ItemAttach& ia : att.items) {
        hdTrans::word::AttachItem& o = out.items.emplace_back();
        o.clause = ia.clause;
        o.pred = ia.pred;
        if (ia.isArg) {
            o.argOf = ia.argOf;
            o.headItem = ia.headItem;
            o.role = Name(F::role, ia.role);
            o.how = Name(F::how, ia.how);
        }
        o.modifies = ia.modifies;
        o.modKind = ia.modKind;
    }
    for (const ClauseAttach& ca : att.clauses) {
        const Clause& c = *ca.clause;
        hdTrans::word::AttachClause& o = out.clauses.emplace_back();
        o.predItem = ca.predItem;
        o.parent = ca.parent;
        auto feature = [&](const char* name, std::string_view value) {
            if (!value.empty()) o.features.emplace_back(name, std::string(value));
        };
        feature("link", Name(F::link, c.link));
        feature("tense", Name(F::tense, c.tense));
        feature("aspect", Name(F::jaspect, c.aspect));
        feature("pol", Name(F::pol, c.pol));
        feature("negfn", Name(F::negfn, c.negFn));
        feature("voice", Name(F::voice, c.voice));
        feature("bene", Name(F::bene, c.bene));
        feature("force", Name(F::force, c.force));
        feature("polite", Name(F::polite, c.polite));
        feature("vol", ClauseVolition(b, d, whole, c));
        feature("subj", Name(F::refkind, c.subj.kind));
        bool subject = false;
        for (const ItemArg& ga : ca.args) {
            hdTrans::word::AttachArg& g = o.args.emplace_back();
            g.role = Name(F::role, ga.role);
            g.how = Name(F::how, ga.how);
            g.lemma = d.S(ga.lemma);
            g.cls = d.S(ga.cls);
            g.item = ga.how == how::omitted ? -1 : ga.item;
            subject = subject || ga.role == static_cast<uint8_t>(Role::subj) || ga.role == static_cast<uint8_t>(Role::topic);
        }
        // The omitted subject the side IR leaves out of the clause's arguments: Z's class of it (a speaker,
        // addressee or third person is @person), and the entity Z named, if any.
        if (!subject && c.link != link::adn && c.subj.kind != refkind::overt) {
            hdTrans::word::AttachArg& g = o.args.emplace_back();
            g.role = "subj";
            g.how = "omitted";
            g.lemma = d.S(c.subj.entity);
            const bool person = c.subj.kind == refkind::speaker || c.subj.kind == refkind::addressee ||
                                c.subj.kind == refkind::third;
            g.cls = person ? "@person" : "";
            g.item = -1;
        }
    }
    return true;
}

bool Generate(void* context, void* doc, void*, const hdTrans::hooks::Sentence& s, std::string& out) {
    auto& b = *static_cast<Bridge*>(context);
    auto& ds = *static_cast<DocState*>(doc);
    if (s.line < 0 || static_cast<size_t>(s.line) >= ds.doc.lines.size()) return false;
    const auto lineIndex = static_cast<size_t>(s.line);
    // The compose stage may go over a line more than once; a sentence not after the last one starts a pass.
    const std::pair<int, int> here{s.chunk, s.index};
    if (here <= ds.last[lineIndex]) ds.from[lineIndex] = 0;
    ds.last[lineIndex] = here;
    // The sentence as a line of its own: the line's analysis and scope, the sentence's nodes.
    Line sent;
    std::vector<LiftMiss> misses;
    const bool lifted = SentenceLine(b, ds, s, ds.from[lineIndex], sent, misses, nullptr);
    const Config& cfg = b.engine.cfg;
    if (b.opt.decide) StageD(*b.engine.transfer, cfg, ds.doc, sent);
    StageR(*b.engine.discourse, cfg, ds.doc, sent);
    StageW(*b.engine.generator, cfg, ds.doc, sent);
    StageG(*b.engine.generator, cfg, ds.doc, sent);
    const bool use = b.opt.generate && lifted && !sent.out.empty();
    if (use) {
        // The white space 0.7.x composed around the sentence (between sentences of a line) stays.
        const size_t lead = s.composed.find_first_not_of(' ');
        const size_t trail = s.composed.find_last_not_of(' ');
        out = (lead == std::string_view::npos ? std::string() : std::string(s.composed.substr(0, lead))) + sent.out +
              (trail == std::string_view::npos ? std::string() : std::string(s.composed.substr(trail + 1)));
    }
    SentenceRecord& r = ds.records[lineIndex][{s.chunk, s.index}];
    r.why.clear();
    r.stages.clear();
    r.generated = use;
    r.lifted = sent.kir.size() - misses.size();
    r.misses = std::move(misses);
    if (b.opt.sideIr) {
        const LineReport report = Report(b.engine, ds.doc, sent);
        r.why = WhyRecords(b.engine, ds.doc, sent, report);
        r.stages = LineJson(ds.doc, sent) + '\n';
    }
    return use;
}

}  // namespace

bool ParseParts(std::string_view list, Options& out, std::string& err) {
    out.normalize = out.segment = out.generate = out.decide = out.reselect = out.rule = out.attach = false;
    if (list == "none") return true;
    if (list == "all") {
        out.normalize = out.segment = out.generate = out.decide = true;
        return true;
    }
    size_t at = 0;
    while (at <= list.size()) {
        size_t comma = list.find(',', at);
        if (comma == std::string_view::npos) comma = list.size();
        const std::string_view part = list.substr(at, comma - at);
        if (part == "normalize") out.normalize = true;
        else if (part == "segment") out.segment = true;
        else if (part == "attach") out.attach = true;
        else if (part == "generate") out.generate = true;
        else if (part == "decide") out.decide = true;
        else if (part == "reselect") out.reselect = true;
        else if (part == "rule") out.rule = out.segment = true;
        else {
            err = "unknown part " + std::string(part) + " (normalize, segment, generate, decide, reselect, rule, attach, all, none)";
            return false;
        }
        at = comma + 1;
    }
    return true;
}

Bridge* Create(const Options& options, std::string& err) {
    auto b = std::make_unique<Bridge>();
    b->opt = options;
    // Only the components a part needs are loaded: segment and normalize need the analyzer alone, so 0.8-rule
    // loads no structure, discourse or transfer table.
    const bool full = options.generate || options.decide || options.reselect || options.sideIr || options.lift ||
                      options.attach;
    if (full) {
        if (!LoadEngine(options.cfg, b->engine, err)) return nullptr;
    } else {
        b->engine.cfg = options.cfg;
        if (!(b->engine.analyzer = LoadAnalyzer(options.cfg, err))) return nullptr;
    }
    if (!LoadLift(options.cfg.data, b->lift, err)) return nullptr;
    if (options.rule && !(b->rule = LoadRule(options.cfg.data, err))) return nullptr;
    const std::string stubs = StubComponents();
    auto stub = [&](const char* name) { return (',' + stubs + ',').find(',' + std::string(name) + ',') != std::string::npos; };
    b->analyzerReal = !stub("analyzer");
    b->generatorReal = full && !stub("generator");
    b->transferReal = full && !stub("transfer");
    b->opt.normalize = options.normalize && b->analyzerReal;
    b->opt.segment = options.segment && b->analyzerReal;
    b->opt.generate = options.generate && b->generatorReal;
    b->opt.decide = options.decide && b->transferReal;
    b->opt.reselect = options.reselect && b->transferReal;
    b->hooks.context = b.get();
    b->hooks.beginDocument = BeginDocument;
    b->hooks.endDocument = EndDocument;
    if (b->opt.normalize) b->hooks.normalize = Normalize;
    if (b->opt.segment || b->opt.attach) {
        b->hooks.beginChunk = BeginChunk;
        b->hooks.endChunk = EndChunk;
    }
    if (b->opt.segment) b->hooks.fits = Fits;
    // attach: the side IR's attachment is the packs layer's provider (it reads it only when packs are loaded)
    if (b->opt.attach) b->hooks.attach = AttachHook;
    // The lift and the side IR run whenever a part, --why or --stages needs them.
    if (b->opt.generate || b->opt.decide || b->opt.sideIr || b->opt.lift) b->hooks.generate = Generate;
    if (b->opt.reselect || b->opt.attach) b->hooks.reselect = Reselect;
    return b.release();
}

void Destroy(Bridge* bridge) {
    if (!bridge) return;
    FreeEngine(bridge->engine);
    delete bridge;
}

const hdTrans::hooks::Hooks* HooksOf(const Bridge* bridge) { return &bridge->hooks; }

std::string ActiveParts(const Bridge* b) {
    std::string s;
    auto add = [&s](bool on, const char* name) {
        if (on) s += s.empty() ? name : std::string(",") + name;
    };
    add(b->opt.normalize, "normalize");
    add(b->opt.segment, "segment");
    add(b->opt.generate, "generate");
    add(b->opt.decide, "decide");
    add(b->opt.reselect, "reselect");
    add(b->opt.rule, "rule");
    add(b->opt.attach, "attach");
    return s;
}

Counts CountsOf(const Bridge* b) {
    Counts c;
    c.sentences = b->sentences;
    c.generated = b->generated;
    c.kept = b->kept;
    c.liftedItems = b->liftedItems;
    c.failedItems = b->failedItems;
    c.normalized = b->normalized;
    c.segmentedChunks = b->segmentedChunks;
    c.refits = b->refits;
    c.points = b->points;
    c.overrides = b->overrides;
    c.reselected = b->reselected;
    return c;
}

std::string TakeWhy(Bridge* b) {
    std::lock_guard<std::mutex> lock(b->mu);
    return std::exchange(b->why, {});
}

std::shared_ptr<const RuleData> RuleDataOf(const Bridge* b) { return b->rule; }

std::vector<SourceToken> AnalyzeSource(const Bridge* b, std::string_view text) {
    std::vector<SourceToken> out;
    if (b == nullptr || !b->engine.analyzer || !b->analyzerReal || text.size() > kMaxStructureBytes) return out;
    Doc d;
    Line l;
    l.src = text;
    StageN(*b->engine.analyzer, b->engine.cfg, d, l);
    StageA(*b->engine.analyzer, b->engine.cfg, d, l, {});
    for (const Token& t : l.toks) {
        SourceToken s;
        s.lemma = d.S(t.lemma);
        s.reading = d.S(t.reading);
        s.cform = d.cforms.Str(t.cform);
        s.verb = t.pos == Pos::v1 || t.pos == Pos::v5 || t.pos == Pos::vs || t.pos == Pos::vk || t.pos == Pos::vz;
        s.prefix = t.pos == Pos::pref;
        out.push_back(std::move(s));
    }
    return out;
}

std::string TakeReselections(Bridge* b) {
    std::lock_guard<std::mutex> lock(b->mu);
    return std::exchange(b->reselections, {});
}

std::string TakeAttachments(Bridge* b) {
    std::lock_guard<std::mutex> lock(b->mu);
    return std::exchange(b->attachments, {});
}

std::string TakeStages(Bridge* b) {
    std::lock_guard<std::mutex> lock(b->mu);
    return std::exchange(b->stages, {});
}

std::string LiftMisses(const Bridge* b, size_t limit) {
    std::vector<std::pair<uint64_t, std::pair<std::string, std::string>>> rows;
    {
        std::lock_guard<std::mutex> lock(const_cast<Bridge*>(b)->mu);
        for (const auto& [key, n] : b->misses) rows.push_back({n, key});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& x, const auto& y) {
        return x.first != y.first ? x.first > y.first : x.second < y.second;
    });
    std::string out;
    for (size_t i = 0; i < rows.size() && i < limit; i++)
        out += rows[i].second.first + '\t' + rows[i].second.second + '\t' + std::to_string(rows[i].first) + '\n';
    return out;
}

}  // namespace hd08::bridge
