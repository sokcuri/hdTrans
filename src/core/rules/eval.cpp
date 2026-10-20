#include <cstddef>
#include <cstdint>

#include "core/rules/rules.h"

namespace hdTrans::rules {

namespace {

Item* Word(Item* pwi, int delta) { return delta == 0 || pwi == nullptr ? pwi : pwi->GetObject(delta); }

bool HasWordOperand(Op op) {
    return op == Op::SrcEqW || op == Op::DstSetW || op == Op::AttrMinusEqW || op == Op::GobiDstSetW;
}

// The left word is looked up before the right one, and both before either is
// tested. di and udi are read from the word just before the call that takes them.
bool RunAtom(const Program& program, const Node& n, Item* pwi) {
    Item* w = Word(pwi, n.delta);
    Item* wi = HasWordOperand(n.op) ? Word(pwi, n.rightDelta) : nullptr;
    if (w == nullptr) return false;
    const std::string_view s(program.literals.data() + n.first, n.second);
    switch (n.op) {
    case Op::SrcEq: return w->CheckSrc(s);
    case Op::SrcNe: return !w->CheckSrc(s);
    case Op::SrcEqW: return w->CheckLLSrc(wi);
    case Op::DstEq: return w->CheckDst(s, w->GetCurrentCompDI());
    case Op::DstNe: return !w->CheckDst(s, w->GetCurrentCompDI());
    case Op::DstSet: return w->AssignDst(w->GetCurrentCompDI(), s);
    case Op::DstSetW: return w->AssignLLDst(wi, w->GetCurrentCompDI());
    case Op::PumsaEq: return w->CheckPumsa(s, w->GetCurrentCompDI());
    case Op::PumsaNe: return !w->CheckPumsa(s, w->GetCurrentCompDI());
    case Op::AttrEq: return w->CheckAttr(s, w->GetCurrentCompDI());
    case Op::AttrNe: return !w->CheckAttr(s, w->GetCurrentCompDI());
    case Op::AttrMinusEq: return w->MinAttrOperation(w->GetCurrentCompDI(), s);
    case Op::AttrMinusEqW: return wi != nullptr && w->CheckLLAttr(kOpMinSubAttr, wi, w->GetCurrentCompDI());
    case Op::GramEq: return w->CheckGAttr(s, w->GetCurrentCompDI());
    case Op::GramNe: return !w->CheckGAttr(s, w->GetCurrentCompDI());
    case Op::GramMinusEq: return w->MinGAttrOperation(w->GetCurrentCompDI(), s);
    case Op::GobiSrcEq: return w->CheckGobiSrc(s);
    case Op::GobiSrcNe: return !w->CheckGobiSrc(s);
    case Op::GobiDstEq: return w->CheckGobiDst(s, w->GetCurrentCompUmiDI());
    case Op::GobiDstSet: return w->AssignGobiDst(w->GetCurrentCompUmiDI(), s);
    // Takes the base candidate index, not the ending one.
    case Op::GobiDstSetW: return w->AssignLLGobiDst(wi, w->GetCurrentCompDI());
    case Op::GobiPumsaEq: return w->CheckGobiPumsa(s, w->GetCurrentCompUmiDI());
    case Op::GobiAttrEq: return w->CheckGobiAttr(s, w->GetCurrentCompUmiDI());
    case Op::GobiAttrMinusEq: return w->MinGobiAttrOperation(w->GetCurrentCompUmiDI(), s);
    case Op::GobiGramEq: return w->CheckGobiGAttr(s, w->GetCurrentCompUmiDI());
    case Op::GobiGramNe: return !w->CheckGobiGAttr(s, w->GetCurrentCompUmiDI());
    }
    return false;
}

bool Run(const Program& program, uint32_t i, Item* pwi) {
    const Node& n = program.nodes[i];
    if (n.kind == NodeKind::Atom) return RunAtom(program, n, pwi);
    bool all = true;
    uint32_t child = i + 1;
    for (uint32_t k = 0; k < n.first; k++) {
        const bool v = Run(program, child, pwi);
        if (n.kind == NodeKind::Or && v) return true;
        if (n.kind == NodeKind::And && !v) return false;
        all = all && v;
        const Node& c = program.nodes[child];
        child = c.kind == NodeKind::Atom ? child + 1 : c.second;
    }
    return n.kind != NodeKind::Or && all;
}

}  // namespace

bool RunRule(const Program& program, uint32_t root, Item* pwi) {
    if (pwi == nullptr) return Run(program, root, pwi);
    pwi->BeginRule();
    const bool result = Run(program, root, pwi);
    pwi->EndRule(result);
    return result;
}

bool RunLocal(const Program& program, Item* pwi, int ruleIndex, Tally* tally) {
    if (ruleIndex < 0 || static_cast<std::size_t>(ruleIndex) >= program.localRules.size()) return false;
    const std::size_t index = static_cast<std::size_t>(ruleIndex);
    if (pwi) pwi->SetRuleContext(false, ruleIndex);
    if (pwi) pwi->BeginRule();
    const bool result = index < program.localCode.size() && program.localCode[index]
                            ? program.localCode[index](pwi)
                            : Run(program, program.localRules[index], pwi);
    if (pwi) pwi->EndRule(result);
    if (pwi) pwi->SetRuleContext(false, -1);
    if (tally && index < tally->localRuns.size()) {
        tally->localRuns[index]++;
        if (result) tally->localFires[index]++;
    }
    return result;
}

bool RunGlobal(const Program& program, Item* pwi, bool uganRule, unsigned char pumsa, bool last, int* fired,
               Tally* tally) {
    if (fired) *fired = -1;
    const int lo = last ? 0 : 1000;
    const int hi = last ? 1000 : 5000;
    for (std::size_t i = 0; i < program.globalRules.size(); i++) {
        const GlobalRule& r = program.globalRules[i];
        if (r.uganRule != uganRule || r.pumsa != pumsa) continue;
        if (uganRule && (r.number <= lo || r.number >= hi)) continue;
        if (pwi) pwi->SetRuleContext(true, r.prio);
        if (pwi) pwi->BeginRule();
        const bool result = i < program.globalCode.size() && program.globalCode[i] ? program.globalCode[i](pwi)
                                                                                     : Run(program, r.root, pwi);
        if (pwi) pwi->EndRule(result);
        if (pwi) pwi->SetRuleContext(true, -1);
        if (tally && i < tally->globalRuns.size()) {
            tally->globalRuns[i]++;
            if (result) tally->globalFires[i]++;
        }
        if (result) {
            if (fired) *fired = r.prio;
            return true;
        }
    }
    return false;
}

}  // namespace hdTrans::rules
