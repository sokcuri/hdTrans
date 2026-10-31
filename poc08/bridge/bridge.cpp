#include "bridge.h"

#include "hd8/stage_repair.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "driver/driver.h"
#include "driver/report.h"
#include "ir/ir_json.h"
#include "core/word/item.h"
#include "lift.h"
#include "rule.h"

namespace hd08::bridge {

namespace {

// One sentence as last written: the compose stage may run a line more than once (trial passes),
// and only the last pass counts.
struct SentenceRecord {
    std::string why, stages, reselections;
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
    bool caption = false;  // 0.8-rule: the document reads as captions, so no part changes it
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
    std::shared_ptr<const RuleData> rule;
    hdTrans::hooks::Hooks hooks;
    bool analyzerReal = false, generatorReal = false, transferReal = false;
    std::atomic<uint64_t> sentences{0}, generated{0}, kept{0}, liftedItems{0}, failedItems{0}, normalized{0}, repaired{0},
        segmentedChunks{0}, refits{0}, points{0}, overrides{0}, reselected{0};
    std::mutex mu;  // guards the fields below
    std::string why, stages, reselections;
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
    if (b.opt.sideIr || b.opt.decide || b.opt.generate || b.opt.reselect) {
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
        }
    }
    b.why += why;
    b.stages += stages;
    delete ds;
}

bool Repair(void* context, void*, int line, std::string_view text, std::string& out) {
    auto& b = *static_cast<Bridge*>(context);
    std::string why;
    if (!RepairText(*b.engine.analyzer, text, &out, b.opt.sideIr ? &why : nullptr)) return false;
    b.repaired++;
    if (b.opt.sideIr) {
        std::lock_guard<std::mutex> lock(b.mu);
        b.why += "repair line " + std::to_string(line + 1) + ": " + std::string(text) + "\n" + why;
    }
    return true;
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

void* BeginChunk(void* context, void* doc, int, int, std::string_view text) {
    auto& b = *static_cast<Bridge*>(context);
    if (!b.opt.segment || (doc != nullptr && static_cast<DocState*>(doc)->caption)) return nullptr;
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
    out.normalize = out.segment = out.generate = out.decide = out.reselect = out.rule = out.repair = false;
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
        if (part == "repair") out.repair = true;
        else if (part == "normalize") out.normalize = true;
        else if (part == "segment") out.segment = true;
        else if (part == "generate") out.generate = true;
        else if (part == "decide") out.decide = true;
        else if (part == "reselect") out.reselect = true;
        else if (part == "rule") out.rule = out.segment = true;
        else {
            err = "unknown part " + std::string(part) +
                  " (repair, normalize, segment, generate, decide, reselect, rule, all, none)";
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
    const bool full = options.generate || options.decide || options.reselect || options.sideIr || options.lift;
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
    b->opt.repair = options.repair && b->analyzerReal && EnableRepair(b->engine.analyzer);
    if (b->opt.repair) b->hooks.repair = Repair;
    if (b->opt.segment) {
        b->hooks.beginChunk = BeginChunk;
        b->hooks.endChunk = EndChunk;
        b->hooks.fits = Fits;
    }
    // The lift and the side IR run whenever a part, --why or --stages needs them.
    if (b->opt.generate || b->opt.decide || b->opt.sideIr || b->opt.lift) b->hooks.generate = Generate;
    if (b->opt.reselect) b->hooks.reselect = Reselect;
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
    add(b->opt.repair, "repair");
    add(b->opt.normalize, "normalize");
    add(b->opt.segment, "segment");
    add(b->opt.generate, "generate");
    add(b->opt.decide, "decide");
    add(b->opt.reselect, "reselect");
    add(b->opt.rule, "rule");
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
    c.repaired = b->repaired;
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
