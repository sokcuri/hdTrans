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
    std::vector<std::map<std::pair<int, int>, SentenceRecord>> records;  // per line, by (chunk, sentence)
};

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

bool Fits(const void* chunk, uint32_t, uint32_t end) {
    const auto& cs = *static_cast<const ChunkState*>(chunk);
    if (cs.ends.empty() || end >= cs.ends.back()) return true;
    const bool fits = std::binary_search(cs.ends.begin(), cs.ends.end(), end);
    if (!fits) (*cs.refits)++;
    return fits;
}

bool Generate(void* context, void* doc, void*, const hdTrans::hooks::Sentence& s, std::string& out) {
    auto& b = *static_cast<Bridge*>(context);
    auto& ds = *static_cast<DocState*>(doc);
    if (s.line < 0 || static_cast<size_t>(s.line) >= ds.doc.lines.size()) return false;
    const auto lineIndex = static_cast<size_t>(s.line);
    if (s.chunk == 0 && s.index == 0) ds.from[lineIndex] = 0;  // a pass over the line starts again
    const Line& whole = ds.doc.lines[static_cast<size_t>(s.line)];
    // The sentence as a line of its own: the line's analysis and scope, the sentence's nodes.
    Line sent;
    sent.id = whole.id;
    sent.name = std::to_string(s.line + 1) + "." + std::to_string(s.chunk) + "." + std::to_string(s.index);
    sent.src = whole.src;
    sent.norm = whole.norm;
    sent.map = whole.map;
    sent.toks = whole.toks;
    sent.alts = whole.alts;
    sent.utts = whole.utts;
    sent.mentions = whole.mentions;
    sent.buns = whole.buns;
    sent.clauses = whole.clauses;
    sent.alt = whole.alt;
    std::vector<LiftMiss> misses;
    const bool lifted = Lift(b.lift, s, ds.doc, sent, ds.from[lineIndex], misses);
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
