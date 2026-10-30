// Stub W and G until poc08/generator lands: kir2 is a copy of kir, and G writes each node's
// lemma, keeping the source spacing between nodes.
#include "ir/invariants.h"
#include "ir/stages.h"

namespace hd08 {

struct GeneratorData {
    InvariantData invariants;
};

GeneratorData* LoadGenerator(const Config& cfg, std::string& err) {
    auto* g = new GeneratorData;
    if (!LoadInvariantData(cfg.data, g->invariants, err)) {
        delete g;
        return nullptr;
    }
    return g;
}
void FreeGenerator(GeneratorData* data) { delete data; }

void StageW(const GeneratorData&, const Config&, Doc&, Line& line) {
    line.kir2 = line.kir;
    for (KNode& n : line.kir2) {
        n.origin = n.id;
        n.decision = 0;
    }
}

void StageG(const GeneratorData& g, const Config&, Doc& doc, Line& line) {
    line.out.clear();
    line.spans.clear();
    uint32_t prevEnd = 0;
    for (const KNode& n : line.kir2) {
        if (!line.out.empty() && n.src.begin > prevEnd) line.out += ' ';
        const std::string_view text = doc.S(n.lemma);
        OutSpan s;
        s.begin = static_cast<uint32_t>(line.out.size());
        line.out += text;
        s.end = static_cast<uint32_t>(line.out.size());
        s.node = n.id;
        line.spans.push_back(s);
        if (n.src.end > n.src.begin) prevEnd = n.src.end;
    }
    CheckInvariants(doc, line, g.invariants, line.flags);
}

}  // namespace hd08
