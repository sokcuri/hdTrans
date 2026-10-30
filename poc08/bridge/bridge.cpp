#include "bridge.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "driver/driver.h"
#include "driver/report.h"
#include "ir/ir_json.h"
#include "lift.h"

namespace hd08::bridge {

namespace {

// One sentence as last written: the compose stage may run a line more than once (trial passes),
// and only the last pass counts.
struct SentenceRecord {
    std::string why, stages;
    bool generated = false;
    uint64_t lifted = 0;
    std::vector<LiftMiss> misses;
};

struct DocState {
    Doc doc;                     // the side IR: one Line per line of the call
    std::vector<uint32_t> from;  // per line: where the lift's source search goes on
    std::vector<std::pair<int, int>> last;  // per line: the (chunk, sentence) lifted last; an earlier one starts a pass
    std::vector<std::map<std::pair<int, int>, SentenceRecord>> records;  // per line, by (chunk, sentence)
};

constexpr size_t kMaxStructureBytes = 4096;

struct ChunkState {
    std::vector<uint32_t> ends;  // token ends of the analyzer, sorted, in chunk bytes
    std::atomic<uint64_t>* refits = nullptr;
};

}  // namespace

struct Bridge {
    Options opt;
    Engine engine;
    LiftTable lift;
    hdTrans::hooks::Hooks hooks;
    bool analyzerReal = false, generatorReal = false, transferReal = false;
    std::atomic<uint64_t> sentences{0}, generated{0}, kept{0}, liftedItems{0}, failedItems{0}, normalized{0},
        segmentedChunks{0}, refits{0};
    std::mutex mu;  // guards the fields below
    std::string why, stages;
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
    if (b.opt.sideIr || b.opt.decide || b.opt.generate) {
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

void* BeginChunk(void* context, void*, int, int, std::string_view text) {
    auto& b = *static_cast<Bridge*>(context);
    if (!b.opt.segment) return nullptr;
    Doc d;
    Line l;
    l.src = text;
    StageN(*b.engine.analyzer, b.engine.cfg, d, l);
    StageA(*b.engine.analyzer, b.engine.cfg, d, l, {});
    auto* cs = new ChunkState;
    cs->refits = &b.refits;
    for (const Token& t : l.toks) cs->ends.push_back(t.src.end);
    std::sort(cs->ends.begin(), cs->ends.end());
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

bool Generate(void* context, void* doc, void*, const hdTrans::hooks::Sentence& s, std::string& out) {
    auto& b = *static_cast<Bridge*>(context);
    auto& ds = *static_cast<DocState*>(doc);
    if (s.line < 0 || static_cast<size_t>(s.line) >= ds.doc.lines.size()) return false;
    const auto lineIndex = static_cast<size_t>(s.line);
    // The compose stage may go over a line more than once; a sentence not after the last one starts a pass.
    const std::pair<int, int> here{s.chunk, s.index};
    if (here <= ds.last[lineIndex]) ds.from[lineIndex] = 0;
    ds.last[lineIndex] = here;
    const Line& whole = ds.doc.lines[static_cast<size_t>(s.line)];
    // The sentence as a line of its own: the line's analysis and scope, the sentence's nodes.
    Line sent;
    sent.id = whole.id;
    sent.name = std::to_string(s.line + 1) + "." + std::to_string(s.chunk) + "." + std::to_string(s.index);
    sent.src = whole.src;
    sent.norm = whole.norm;
    sent.map = whole.map;
    sent.utts = whole.utts;
    std::vector<LiftMiss> misses;
    const bool lifted = Lift(b.lift, s, ds.doc, sent, ds.from[lineIndex], misses);
    // The line's analysis within the sentence's words, so that D, R and the canary see this sentence only.
    Span span{UINT32_MAX, 0};
    for (const KNode& n : sent.kir) {
        if (n.src.end <= n.src.begin) continue;
        span.begin = std::min(span.begin, n.src.begin);
        span.end = std::max(span.end, n.src.end);
    }
    if (span.begin < span.end) CopyAnalysis(whole, span, sent);
    const Config& cfg = b.engine.cfg;
    if (b.opt.decide) StageD(*b.engine.transfer, cfg, ds.doc, sent);
    StageR(*b.engine.discourse, cfg, ds.doc, sent);
    StageW(*b.engine.generator, cfg, ds.doc, sent);
    StageG(*b.engine.generator, cfg, ds.doc, sent);
    const bool use = b.opt.generate && lifted && !sent.out.empty();
    if (use) out = sent.out;
    SentenceRecord& r = ds.records[lineIndex][{s.chunk, s.index}];
    r = SentenceRecord{};
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
    out.normalize = out.segment = out.generate = out.decide = false;
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
        else if (part == "generate") out.generate = true;
        else if (part == "decide") out.decide = true;
        else {
            err = "unknown part " + std::string(part) + " (normalize, segment, generate, decide, all, none)";
            return false;
        }
        at = comma + 1;
    }
    return true;
}

Bridge* Create(const Options& options, std::string& err) {
    auto b = std::make_unique<Bridge>();
    b->opt = options;
    if (!LoadEngine(options.cfg, b->engine, err)) return nullptr;
    if (!LoadLift(options.cfg.data, b->lift, err)) return nullptr;
    const std::string stubs = StubComponents();
    auto stub = [&](const char* name) { return (',' + stubs + ',').find(',' + std::string(name) + ',') != std::string::npos; };
    b->analyzerReal = !stub("analyzer");
    b->generatorReal = !stub("generator");
    b->transferReal = !stub("transfer");
    b->opt.normalize = options.normalize && b->analyzerReal;
    b->opt.segment = options.segment && b->analyzerReal;
    b->opt.generate = options.generate && b->generatorReal;
    b->opt.decide = options.decide && b->transferReal;
    b->hooks.context = b.get();
    b->hooks.beginDocument = BeginDocument;
    b->hooks.endDocument = EndDocument;
    if (b->opt.normalize) b->hooks.normalize = Normalize;
    if (b->opt.segment) {
        b->hooks.beginChunk = BeginChunk;
        b->hooks.endChunk = EndChunk;
        b->hooks.fits = Fits;
    }
    // The lift and the side IR run whenever a part, --why or --stages needs them.
    if (b->opt.generate || b->opt.decide || b->opt.sideIr || b->opt.lift) b->hooks.generate = Generate;
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
    return c;
}

std::string TakeWhy(Bridge* b) {
    std::lock_guard<std::mutex> lock(b->mu);
    return std::exchange(b->why, {});
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
