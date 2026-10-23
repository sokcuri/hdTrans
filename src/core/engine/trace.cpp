#include "core/engine/trace.h"

#include <string_view>
#include <utility>

#include "core/engine/rules.h"
#include "core/word/constructions.h"
#include "core/word/select.h"

namespace hdTrans::internal {
namespace {

std::string RuleLocation(char kind, int id) {
    const std::string source = RuleSource(kind, id);
    if (source.empty()) return {};
    // A local rule is the body of the dictionary row that names it, not a line of a
    // rule file; its number is the location the reader can follow.
    if (kind == 'L') return source;
    const auto filename = source.substr(0, source.rfind(':'));
    const std::string prefix = std::string(1, kind) + " " + std::to_string(id) + " ";
    const auto lines = RuleLines();
    for (const auto& file : RuleFiles()) {
        if (filename != file.name) continue;
        for (std::size_t i = 0; i < file.count; ++i) {
            if (lines[file.first + i].starts_with(prefix)) return filename + ":" + std::to_string(i + 1);
        }
    }
    return {};
}

TraceSelection Selection(const word::Selection& selection, int index, std::string_view text, bool ending) {
    using word::Provenance;
    TraceSelection out;
    out.index = index;
    out.text = text;
    out.provenance = word::ProvenanceName(selection.layer);
    out.id = selection.id;
    switch (selection.layer) {
    case Provenance::None: break;
    case Provenance::Fixed: out.layer = "fixed"; break;
    case Provenance::LocalRule:
        out.layer = ending ? "local-ending" : "local";
        out.source = RuleLocation('L', selection.id);
        break;
    case Provenance::GlobalRule:
        out.layer = ending ? "global-ending" : "global-stem";
        if (!ending) {
            for (const auto& rule : RulesProgram().globalRules) {
                if (rule.prio == selection.id && rule.number > 0 && rule.number < 1000) out.layer = "global-last";
            }
        }
        out.source = RuleLocation('G', selection.id);
        break;
    case Provenance::Single:
    case Provenance::FirstXxx:
    case Provenance::Digit: out.layer = ending ? "ending-defaults" : "defaults"; break;
    case Provenance::Sense:
    case Provenance::SenseDefault:
        out.layer = "senses";
        out.source = "senses.tsv:" + std::to_string(selection.id);
        break;
    case Provenance::Initial: out.layer = "rewrites"; break;
    case Provenance::Rewrite:
        out.layer = "rewrites";
        out.source = std::string(word::ConstructionId(selection.id));
        break;
    case Provenance::Scorer: out.layer = "scorers"; break;
    case Provenance::Misread: out.layer = "misread"; break;
    case Provenance::Cg: out.layer = "cg-lite"; break;
    }
    return out;
}

void OnLine(void* context, int line) {
    auto& collector = *static_cast<TraceCollector*>(context);
    collector.line = collector.lineBase + line;
    const auto& next = collector.downstream;
    if (next.line) next.line(next.context, line);
}

void OnResolved(void* context, int chunk, int sentence, const word::Chain& chain) {
    auto& collector = *static_cast<TraceCollector*>(context);
    for (const word::Item* item = chain.head; item; item = item->next) {
        TraceChoice out;
        out.line = collector.line;
        out.chunk = chunk;
        out.sentence = sentence;
        out.token = item->tokenIndex;
        out.start = item->start;
        if (item->group) out.surface = item->group->surface;
        out.choice = Selection(item->choice, item->selected, item->EntryText(item->selected), false);
        if (item->DirectIndex() >= 0) {
            out.ending = TraceEnding{Selection(item->directChoice, item->DirectIndex(),
                                              item->DirectText(item->DirectIndex()), true),
                                     std::string(item->DirectSurface())};
        }
        collector.result.choices.push_back(std::move(out));
    }
    const auto& next = collector.downstream;
    if (next.resolved) next.resolved(next.context, chunk, sentence, chain);
}

void OnFilterLog(void* context, std::string_view message) {
    auto& collector = *static_cast<TraceCollector*>(context);
    collector.result.logs.emplace_back(message);
    const auto& next = collector.downstream;
    if (next.filterLog) next.filterLog(next.context, message);
}

}  // namespace

StageObserver TraceObserver(TraceCollector& collector, const StageObserver& downstream) {
    collector.downstream = downstream;
    StageObserver out;
    out.context = &collector;
    out.line = OnLine;
    out.resolved = OnResolved;
    out.filterLog = OnFilterLog;
    if (downstream.text) {
        out.text = [](void* context, std::string_view tag, std::string_view text) {
            const auto& next = static_cast<TraceCollector*>(context)->downstream;
            next.text(next.context, tag, text);
        };
    }
    if (downstream.pieces) {
        out.pieces = [](void* context, std::string_view tag, int chunk, const output::Pieces& pieces) {
            const auto& next = static_cast<TraceCollector*>(context)->downstream;
            next.pieces(next.context, tag, chunk, pieces);
        };
    }
    if (downstream.tokens) {
        out.tokens = [](void* context, int chunk, const tokenizer::Tokens& tokens) {
            const auto& next = static_cast<TraceCollector*>(context)->downstream;
            next.tokens(next.context, chunk, tokens);
        };
    }
    if (downstream.analysis) {
        out.analysis = [](void* context, int chunk, const tokenizer::Tokens& tokens, const word::Analysis& analysis) {
            const auto& next = static_cast<TraceCollector*>(context)->downstream;
            next.analysis(next.context, chunk, tokens, analysis);
        };
    }
    if (downstream.composed) {
        out.composed = [](void* context, int chunk, int sentence, const output::Pieces& pieces) {
            const auto& next = static_cast<TraceCollector*>(context)->downstream;
            next.composed(next.context, chunk, sentence, pieces);
        };
    }
    if (downstream.cleaned) {
        out.cleaned = [](void* context, const output::Pieces& pieces) {
            const auto& next = static_cast<TraceCollector*>(context)->downstream;
            next.cleaned(next.context, pieces);
        };
    }
    if (downstream.restored) {
        out.restored = [](void* context, const pipeline::RestoreSteps& steps) {
            const auto& next = static_cast<TraceCollector*>(context)->downstream;
            next.restored(next.context, steps);
        };
    }
    return out;
}

}  // namespace hdTrans::internal
