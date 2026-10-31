#include "report.h"

#include <algorithm>

#include "ir/ir_json.h"
#include "ir/json.h"

namespace hd08 {

namespace {

// An evidence kind as an index of the per-kind tables; an out-of-range kind counts as fallback.
unsigned EvIndex(Ev kind) {
    const auto k = static_cast<unsigned>(kind);
    return k < Count(F::ev) ? k : static_cast<unsigned>(Ev::Fallback);
}

bool SameFlag(const Flag& a, const Flag& b) {
    return a.kind == b.kind && a.inv == b.inv && a.src.begin == b.src.begin && a.src.end == b.src.end;
}

// The decision behind a kir2 node: its origin's in kir, else its own. *other is the node's own
// decision when it differs from the one returned (a W rewrite or a later stage's).
const Decision* NodeDecision(const Line& line, const KNode& n, const Decision** other) {
    *other = nullptr;
    const KNode* origin = FindNode(line.kir, n.origin);
    const Decision* own = line.DecisionOf(n.decision);
    const Decision* from = origin ? line.DecisionOf(origin->decision) : nullptr;
    if (from && own && own != from) *other = own;
    return from ? from : own;
}

void Key(std::string& o, const char* k) {
    if (o.size() > 1) o += ',';
    AppendJsonString(o, k);
    o += ':';
}

void SpanValue(std::string& o, Span s) {
    o += '[' + std::to_string(s.begin) + ',' + std::to_string(s.end) + ']';
}

// contrib, path and flip of a decision (IR.md §7 v2).
void Explain(std::string& o, const Doc& doc, const Line& line, const Decision& d) {
    if (!d.contrib.empty()) {
        Key(o, "contrib");
        o += '{';
        for (size_t i = 0; i < d.contrib.size(); i++) {
            if (i) o += ',';
            AppendJsonString(o, doc.S(d.contrib[i].first));
            o += ':';
            AppendJsonNumber(o, d.contrib[i].second);
        }
        o += '}';
    }
    if (!line.alt.toks.empty()) {
        Key(o, "path");
        o += std::to_string(d.path);
    }
    if (d.flip) {
        Key(o, "flip");
        AppendJsonString(o, doc.S(d.flip));
    }
}

const KReg* RegOf(const Line& line, uint32_t node) {
    for (const KReg& r : line.reg)
        if (r.node == node) return &r;
    return nullptr;
}

}  // namespace

LineReport Report(const Engine& e, const Doc& doc, const Line& line) {
    LineReport r;
    r.flags = line.flags;
    std::vector<Flag> canary;
    CheckInvariants(doc, line, e.invariants, canary);
    for (const Flag& f : canary)
        if (std::none_of(r.flags.begin(), r.flags.end(), [&](const Flag& g) { return SameFlag(f, g); }))
            r.flags.push_back(f);

    for (const Decision& d : line.decisions) {
        r.evidence[EvIndex(d.kind)]++;
        if (d.stage == 'T' && d.st == st::u) r.stU++;
        if (d.nAlt > 1) {
            r.multi++;
            r.multiContrib += !d.contrib.empty();
        }
        const float risk = e.risk[EvIndex(d.kind)];
        if (risk > r.risk) {
            r.risk = risk;
            r.top = &d;
        }
    }
    for (const Flag& f : r.flags)
        if (IsRiskFlag(f.kind)) r.risk = std::max(r.risk, e.risk[static_cast<unsigned>(Ev::Fallback)]);

    std::vector<bool> traced(line.out.size(), false);
    for (const OutSpan& s : line.spans) {
        const KNode* n = FindNode(line.kir2, s.node);
        const Decision* rw = nullptr;
        if (!n || !NodeDecision(line, *n, &rw)) continue;
        for (uint32_t b = s.begin; b < s.end && b < traced.size(); b++) traced[b] = true;
    }
    for (size_t b = 0; b < line.out.size(); b++) {
        const char c = line.out[b];
        if (c == ' ' || c == '\t') continue;
        r.outBytes++;
        if (traced[b]) r.tracedBytes++;
    }

    bool risky = false;
    for (const Decision& d : line.decisions)
        if (IsRiskEvidence(d.kind)) risky = true;
    for (const Flag& f : r.flags)
        if (IsRiskFlag(f.kind)) risky = true;
    r.covered = !risky && r.stU == 0 && !line.out.empty() && r.tracedBytes == r.outBytes;
    return r;
}

std::string WhyRecords(const Engine& e, const Doc& doc, const Line& line, const LineReport& report) {
    std::string all;
    std::vector<bool> used(line.decisions.size() + 1, false);
    for (const OutSpan& s : line.spans) {
        std::string o = "{";
        Key(o, "line");
        o += std::to_string(line.id);
        Key(o, "ko");
        AppendJsonString(o, std::string_view(line.out).substr(std::min<size_t>(s.begin, line.out.size()),
                                                               s.end > s.begin ? s.end - s.begin : 0));
        Key(o, "out");
        SpanValue(o, {s.begin, s.end});
        Key(o, "node");
        o += std::to_string(s.node);
        const KNode* n = FindNode(line.kir2, s.node);
        const Decision* rw = nullptr;
        const Decision* d = n ? NodeDecision(line, *n, &rw) : nullptr;
        const Span src = d ? d->src : n ? n->src : Span{};
        Key(o, "src");
        SpanValue(o, src);
        Key(o, "ja");
        AppendJsonString(o, line.Src(src));
        // v2g: the ja/repair.tsv edits that gave a token of the span its reading.
        bool repaired = false;
        for (const Token& t : line.toks) {
            if (!(t.flags & tokflag::Repaired) || !t.repair || t.src.end <= src.begin || t.src.begin >= src.end) continue;
            if (repaired) {
                o += ',';
            } else {
                Key(o, "repair");
                o += '[';
                repaired = true;
            }
            AppendJsonString(o, doc.S(t.repair));
        }
        if (repaired) o += ']';
        Key(o, "by");
        AppendJsonString(o, d ? doc.S(d->rule) : std::string_view());
        Key(o, "ev");
        AppendJsonString(o, d ? Name(F::ev, static_cast<unsigned>(d->kind)) : std::string_view("untraced"));
        if (d) {
            used[static_cast<size_t>(d - line.decisions.data()) + 1] = true;
            if (!d->because.empty()) {
                Key(o, "because");
                o += '[';
                for (size_t i = 0; i < d->because.size(); i++) {
                    if (i) o += ',';
                    AppendJsonString(o, doc.S(d->because[i]));
                }
                o += ']';
            }
            Explain(o, doc, line, *d);
            if (d->score != 0) {
                Key(o, "score");
                AppendJsonNumber(o, d->score);
            }
            if (d->margin != 0) {
                Key(o, "margin");
                AppendJsonNumber(o, d->margin);
            }
            if (d->runnerUp) {
                Key(o, "alt");
                o += '[';
                AppendJsonString(o, doc.S(d->runnerUp));
                o += ']';
            }
            if (d->st) {
                Key(o, "st");
                AppendJsonString(o, Name(F::st, d->st));
            }
            Key(o, "risk");
            AppendJsonNumber(o, e.risk[EvIndex(d->kind)]);
        }
        if (rw) {
            used[static_cast<size_t>(rw - line.decisions.data()) + 1] = true;
            Key(o, rw->stage == 'W' ? "rw" : "also");
            AppendJsonString(o, doc.S(rw->rule));
        }
        if (n) {
            const KReg* reg = RegOf(line, n->id);
            if (!reg && n->origin) reg = RegOf(line, n->origin);
            if (reg) {
                Key(o, "level");
                AppendJsonString(o, Name(F::level, reg->level));
                if (const Decision* rd = line.DecisionOf(reg->decision)) {
                    used[reg->decision] = true;
                    Key(o, "level_by");
                    AppendJsonString(o, doc.S(rd->rule));
                }
            }
        }
        o += "}\n";
        all += o;
    }
    for (size_t i = 0; i < line.decisions.size(); i++) {
        if (used[i + 1]) continue;
        const Decision& d = line.decisions[i];
        std::string o = "{";
        Key(o, "line");
        o += std::to_string(line.id);
        Key(o, "decision");
        o += std::to_string(i + 1);
        Key(o, "stage");
        o += '"';
        o += static_cast<char>(d.stage ? d.stage : '?');
        o += '"';
        Key(o, "by");
        AppendJsonString(o, doc.S(d.rule));
        Key(o, "ev");
        AppendJsonString(o, Name(F::ev, static_cast<unsigned>(d.kind)));
        Key(o, "src");
        SpanValue(o, d.src);
        Key(o, "ja");
        AppendJsonString(o, line.Src(d.src));
        if (!d.because.empty()) {
            Key(o, "because");
            o += '[';
            for (size_t k = 0; k < d.because.size(); k++) {
                if (k) o += ',';
                AppendJsonString(o, doc.S(d.because[k]));
            }
            o += ']';
        }
        Explain(o, doc, line, d);
        if (d.score != 0) {
            Key(o, "score");
            AppendJsonNumber(o, d.score);
        }
        if (d.margin != 0) {
            Key(o, "margin");
            AppendJsonNumber(o, d.margin);
        }
        if (d.runnerUp) {
            Key(o, "alt");
            o += '[';
            AppendJsonString(o, doc.S(d.runnerUp));
            o += ']';
        }
        Key(o, "risk");
        AppendJsonNumber(o, e.risk[EvIndex(d.kind)]);
        o += "}\n";
        all += o;
    }
    for (const Flag& f : report.flags) {
        std::string o = "{";
        Key(o, "line");
        o += std::to_string(line.id);
        Key(o, "flag");
        AppendJsonString(o, Name(F::flag, f.kind));
        if (f.inv) {
            Key(o, "inv");
            o += "\"I" + std::to_string(f.inv) + '"';
        }
        Key(o, "src");
        SpanValue(o, f.src);
        Key(o, "ja");
        AppendJsonString(o, line.Src(f.src));
        if (f.node) {
            Key(o, "node");
            o += std::to_string(f.node);
        }
        o += "}\n";
        all += o;
    }
    return all;
}

std::string LineSummaryJson(const Doc& doc, const Line& line, const LineReport& r) {
    std::string o = "{";
    Key(o, "line");
    o += std::to_string(line.id);
    if (!doc.id.empty()) {
        Key(o, "doc");
        AppendJsonString(o, doc.id);
    }
    Key(o, "src");
    AppendJsonString(o, line.src);
    Key(o, "out");
    AppendJsonString(o, line.out);
    Key(o, "covered");
    o += r.covered ? "true" : "false";
    Key(o, "risk");
    AppendJsonNumber(o, r.risk);
    if (r.top) {
        Key(o, "top");
        o += "{\"by\":";
        AppendJsonString(o, doc.S(r.top->rule));
        o += ",\"ev\":";
        AppendJsonString(o, Name(F::ev, static_cast<unsigned>(r.top->kind)));
        o += ",\"src\":";
        SpanValue(o, r.top->src);
        o += '}';
    }
    Key(o, "evidence");
    o += '{';
    bool first = true;
    for (unsigned k = 0; k < Count(F::ev); k++) {
        if (!r.evidence[k]) continue;
        if (!first) o += ',';
        first = false;
        AppendJsonString(o, Name(F::ev, k));
        o += ':' + std::to_string(r.evidence[k]);
    }
    o += '}';
    if (r.stU) {
        Key(o, "st_u");
        o += std::to_string(r.stU);
    }
    if (r.multi) {
        Key(o, "contrib");
        o += '[' + std::to_string(r.multiContrib) + ',' + std::to_string(r.multi) + ']';
    }
    Key(o, "traced");
    o += '[' + std::to_string(r.tracedBytes) + ',' + std::to_string(r.outBytes) + ']';
    Key(o, "flags");
    o += '[';
    for (size_t i = 0; i < r.flags.size(); i++) {
        const Flag& f = r.flags[i];
        if (i) o += ',';
        o += "{\"kind\":";
        AppendJsonString(o, Name(F::flag, f.kind));
        if (f.inv) o += ",\"inv\":\"I" + std::to_string(f.inv) + '"';
        o += ",\"src\":";
        SpanValue(o, f.src);
        o += '}';
    }
    o += "]}";
    return o;
}

}  // namespace hd08
