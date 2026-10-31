// Stub C and L until poc08/structure lands: a bunsetsu per content token with the function
// tokens after it, each heading the next; one clause per line.
#include "ir/stages.h"

namespace hd08 {

struct StructureData {};

namespace {

bool IsFunction(Pos p) {
    return p == Pos::aux || p == Pos::suf || p == Pos::p_case || p == Pos::p_topic || p == Pos::p_conj ||
           p == Pos::p_final || p == Pos::p_adv;
}

}  // namespace

StructureData* LoadStructure(const Config&, std::string&) { return new StructureData; }
void FreeStructure(StructureData* data) { delete data; }

void StageC(const StructureData&, const Config&, Doc&, Line& line, int path) {
    PathRef p = PathOf(line, path);
    p.buns.clear();
    for (uint16_t t = 0; t < p.toks.size(); t++) {
        if (p.buns.empty() || !IsFunction(p.toks[t].pos)) {
            Bunsetsu b;
            b.first = b.last = b.content = t;
            p.buns.push_back(b);
        } else {
            p.buns.back().last = t;
        }
    }
    for (size_t b = 0; b + 1 < p.buns.size(); b++) p.buns[b].head = static_cast<int16_t>(b + 1);
}

void StageL(const StructureData&, const Config&, Doc&, Line& line, int path) {
    PathRef p = PathOf(line, path);
    p.clauses.clear();
    if (p.buns.empty()) return;
    Clause c;
    c.firstBun = 0;
    c.lastBun = static_cast<uint16_t>(p.buns.size() - 1);
    c.pred = c.lastBun;
    p.clauses.push_back(c);
}

std::vector<TokAux> AuxOf(const StructureData&, const Doc&, const std::vector<Token>& toks) {
    return std::vector<TokAux>(toks.size());
}

}  // namespace hd08
