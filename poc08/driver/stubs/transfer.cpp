// Stub T until poc08/transfer lands: every token becomes a Raw node carrying its source text,
// with fallback evidence.
#include "ir/stages.h"

namespace hd08 {

struct TransferData {};

TransferData* LoadTransfer(const Config&, std::string&) { return new TransferData; }
void FreeTransfer(TransferData* data) { delete data; }

void StageD(const TransferData&, const Config&, Doc&, Line&) {}

void StageT(const TransferData&, const Config&, Doc& doc, Line& line) {
    line.kir.clear();
    const uint32_t rule = doc.I("code:stub-transfer");
    for (const Token& t : line.toks) {
        Decision d;
        d.stage = 'T';
        d.kind = Ev::Fallback;
        d.rule = rule;
        d.src = t.src;
        KNode n;
        n.id = static_cast<uint32_t>(line.kir.size() + 1);
        n.kind = KK::Raw;
        n.lemma = doc.I(line.Src(t.src));
        n.src = t.src;
        n.decision = line.Decide(d);
        line.kir.push_back(n);
    }
}

}  // namespace hd08
