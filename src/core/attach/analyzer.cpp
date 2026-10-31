#include <algorithm>
#include <cstdio>

#include "core/attach/analyzer.h"
#include "core/attach/lattice.h"
#include "core/attach/utf8.h"

namespace hdTrans::attach::ana {

using internal::AppendUtf8;
using internal::IsHiragana;
using internal::IsKatakana;
using internal::ToKatakana;

void WorkspaceDeleter::operator()(Workspace* ws) const { delete ws; }
WorkspacePtr NewWorkspace() { return WorkspacePtr(new Workspace); }

Analyzer::Analyzer() = default;
Analyzer::~Analyzer() = default;

bool Analyzer::Load(std::string_view snapshot, std::string* err) {
    auto m = std::make_unique<Model>();
    if (!Deserialize(snapshot, m.get(), err)) return false;
    model_ = std::move(m);
    return true;
}

std::string_view Analyzer::UdName(uint16_t ud) const {
    return model_ && ud < model_->udNames.size() ? std::string_view(model_->udNames[ud]) : std::string_view();
}
std::string_view Analyzer::CtypeName(uint8_t c) const {
    return model_ && c < model_->ctypeNames.size() ? std::string_view(model_->ctypeNames[c]) : std::string_view();
}
std::string_view Analyzer::CformName(uint8_t c) const {
    return model_ && c < model_->cformNames.size() ? std::string_view(model_->cformNames[c]) : std::string_view();
}

std::string_view Analyzer::PosName(Pos p) {
    static constexpr std::string_view kNames[] = {
        "n",   "pn",  "num",  "suf",  "pref", "v1",     "v5",      "vs",     "vk",      "vz",    "adj", "na",
        "adv", "adn", "conj", "intj", "aux",  "p_case", "p_topic", "p_conj", "p_final", "p_adv", "sym", "unk"};
    auto i = static_cast<size_t>(p);
    return i < std::size(kNames) ? kNames[i] : "unk";
}

std::string_view Analyzer::FlagName(uint16_t bit) {
    switch (bit) {
        case Unknown: return "unknown";
        case Fallback: return "fallback";
        case LongUnit: return "long-unit";
        case UserArc: return "user-arc";
        case NameCand: return "name-cand";
        case Spoken: return "spoken";
        case Elongated: return "elongated";
        case SpaceBefore: return "space-before";
        case SinoReading: return "sino-reading";
        default: return "";
    }
}

std::string_view Analyzer::EditName(EditKind k) {
    switch (k) {
        case EditKind::width: return "width";
        case EditKind::join: return "join";
        case EditKind::ruby: return "ruby";
    }
    return "";
}

namespace {

bool AllKana(std::u32string_view s) {
    for (char32_t c : s)
        if (!IsHiragana(c) && !IsKatakana(c)) return false;
    return !s.empty();
}

std::string Katakana(std::u32string_view s) {
    std::string out;
    for (char32_t c : s) AppendUtf8(out, ToKatakana(c));
    return out;
}

struct Emitter {
    const Model& m;
    const Workspace& ws;
    Analysis* out;

    std::u32string_view Core(uint32_t b, uint32_t e) const { return std::u32string_view(ws.core.data() + b, e - b); }

    Span SrcOf(uint32_t b, uint32_t e) const { return {ws.nchars[ws.coreN[b]].sb, ws.nchars[ws.coreN[e - 1]].se}; }

    Token Make(const Arc& a) const {
        Token t;
        t.src = SrcOf(a.b, a.e);
        auto surf = Core(a.b, a.e);
        for (char32_t c : surf) AppendUtf8(t.surf, c);
        t.flags = a.flags;
        t.cost = static_cast<float>(static_cast<double>(a.cost) / static_cast<double>(m.unitCost));
        if (a.rule >= 0) t.derive = m.rules[static_cast<size_t>(a.rule)].id;
        if (a.removedBy >= 0) t.removedBy = m.constraints[static_cast<size_t>(a.removedBy)].id;
        switch (a.kind) {
            case ArcKind::lex: {
                const auto& g = m.groups[a.ref];
                const auto& pf = m.pforms[a.form];
                t.cls = pf.cls;
                t.lemma = std::string(m.S(g.lemma));
                if (a.rule >= 0 && AllKana(surf)) {
                    t.reading = Katakana(surf);
                } else if (g.readKind == 0) {
                    t.reading = Katakana(m.C(g.stem, g.stemLen));
                    t.reading += m.S(pf.read);
                } else if (g.readKind == 1) {
                    t.reading = std::string(m.S(g.read));
                    t.reading += m.S(pf.read);
                }
                break;
            }
            case ArcKind::unk:
                t.cls = a.lcls;
                t.lemma = t.surf;
                break;
            case ArcKind::longUnit: {
                const auto& l = m.longs[a.ref];
                t.cls = a.rcls;
                t.lemma = std::string(m.S(l.lemma));
                t.reading = std::string(m.S(l.read));
                if (t.reading.empty()) {
                    std::vector<uint32_t> parts;
                    SubPath(m, ws, a.b, a.e, 0, 0, &parts);
                    for (uint32_t pi : parts)
                        if (ws.arcs[pi].kind != ArcKind::longUnit) t.reading += Make(ws.arcs[pi]).reading;
                }
                if (l.paradigm != UINT32_MAX) {
                    const auto& c = m.classes[a.rcls];
                    t.ud = c.ud;
                    t.ctype = c.ctype;
                    t.cform = c.cform;
                    t.pos = m.classPos[a.rcls];
                    return t;
                }
                t.ud = l.ud;
                t.ctype = l.ctype;
                t.cform = l.cform;
                t.pos = l.pos;
                return t;
            }
            case ArcKind::stutter:
                t.cls = a.lcls;
                t.lemma = t.surf;
                t.reading = Katakana(surf);
                break;
        }
        const auto& c = m.classes[t.cls];
        t.ud = c.ud;
        t.ctype = c.ctype;
        t.cform = c.cform;
        t.pos = m.classPos[t.cls];
        return t;
    }

    // Path → tokens with spaces; joined arcs record a join edit.
    // tokPath (optional) receives the path index of each token.
    void Emit(const std::vector<uint32_t>& path, std::vector<Token>* toks, bool edits, const Options& opt,
              Workspace* mws, std::vector<uint32_t>* tokPath = nullptr) const {
        toks->clear();
        if (tokPath) tokPath->clear();
        for (size_t pi = 0; pi < path.size(); ++pi) {
            const Arc& a = ws.arcs[path[pi]];
            bool spaced = a.b > 0 && ws.spaceBefore[a.b];
            if (a.kind == ArcKind::longUnit && opt.splitLongUnits) {
                uint16_t sc = pi == 0 ? 0 : ws.arcs[path[pi - 1]].rcls;
                uint16_t ec = pi + 1 < path.size() ? ws.arcs[path[pi + 1]].lcls : 0;
                std::vector<uint32_t> parts;
                SubPath(m, *mws, a.b, a.e, sc, ec, &parts);
                for (size_t k = 0; k < parts.size(); ++k) {
                    const Arc& p = ws.arcs[parts[k]];
                    Token t = Make(p);
                    if (k == 0 && spaced) t.flags |= SpaceBefore;
                    toks->push_back(std::move(t));
                    if (tokPath) tokPath->push_back(static_cast<uint32_t>(pi));
                }
            } else {
                Token t = Make(a);
                if (spaced) t.flags |= SpaceBefore;
                if (a.joined && edits) {
                    NormEdit e;
                    e.src = t.src;
                    for (uint32_t x = ws.coreN[a.b]; x <= ws.coreN[a.e - 1]; ++x) AppendUtf8(e.from, ws.nchars[x].c);
                    e.to = t.surf;
                    e.kind = EditKind::join;
                    out->edits.push_back(std::move(e));
                }
                toks->push_back(std::move(t));
                if (tokPath) tokPath->push_back(static_cast<uint32_t>(pi));
            }
        }
    }
};

}  // namespace

void Analyzer::Analyze(std::string_view src, const Options& opt, Analysis* out, Workspace* wsIn) const {
    WorkspacePtr own;
    Workspace* ws = wsIn;
    if (!ws) {
        own = NewWorkspace();
        ws = own.get();
    }
    out->norm.clear();
    out->map.clear();
    out->toks.clear();
    out->alts.clear();
    out->alt.clear();
    out->altMargin = 0;
    if (!model_) return;
    const Model& m = *model_;
    internal::Normalize(src, &ws->nchars, &ws->edits);
    out->edits = ws->edits;
    for (auto& nc : ws->nchars) {
        size_t before = out->norm.size();
        AppendUtf8(out->norm, nc.c);
        for (size_t k = before; k < out->norm.size(); ++k) out->map.push_back(nc.sb);
    }
    out->map.push_back(static_cast<uint32_t>(src.size()));

    BuildLattice(m, ws, opt);
    RunViterbi(m, ws, opt.margins);
    Emitter em{m, *ws, out};
    std::vector<uint32_t> tokPath;
    em.Emit(ws->path, &out->toks, true, opt, ws, &tokPath);
    if (!opt.margins || ws->path.empty()) return;

    // margins, same-span alternatives and path 1
    const auto& A = ws->arcs;
    // costs are in units of one token (ja/prefs.tsv pf.units); a margin below one unit was decided by a class
    // preference only, a margin of 0 is a tie
    const double unit = static_cast<double>(m.unitCost);
    auto units = [unit](int64_t c) { return static_cast<float>(static_cast<double>(c) / unit); };
    int64_t tau = static_cast<int64_t>(static_cast<double>(opt.altTau) * unit);
    int64_t minMargin = kInf;
    int32_t minArc = -1;
    size_t ti = 0;
    for (size_t pi = 0; pi < ws->path.size(); ++pi) {
        const Arc& x = A[ws->path[pi]];
        int64_t margin = kInf;
        int32_t arg = -1;
        while (ti < tokPath.size() && tokPath[ti] < pi) ++ti;
        Token* tok = ti < tokPath.size() && tokPath[ti] == pi ? &out->toks[ti] : nullptr;
        uint16_t altFirst = static_cast<uint16_t>(out->alts.size());
        for (uint32_t yi = 0; yi < A.size(); ++yi) {
            const Arc& y = A[yi];
            if (yi == ws->path[pi] || y.b >= x.e || y.e <= x.b) continue;
            if (y.removedBy >= 0) {
                if (y.b == x.b && y.e == x.e && tok) {
                    Token t = em.Make(y);
                    t.margin = -1;
                    out->alts.push_back(std::move(t));
                }
                continue;
            }
            if (y.fwd >= kInf || y.bwd >= kInf) continue;
            int64_t pc = y.fwd + y.bwd - ws->best;
            if (pc < margin) {
                margin = pc;
                arg = static_cast<int32_t>(yi);
            }
            if (y.b == x.b && y.e == x.e && pc < tau && tok) {
                Token t = em.Make(y);
                t.margin = units(pc);
                out->alts.push_back(std::move(t));
            }
        }
        if (tok) {
            tok->alt = altFirst;
            tok->nAlt = static_cast<uint16_t>(out->alts.size() - altFirst);
            for (size_t k = ti; k < tokPath.size() && tokPath[k] == pi; ++k)
                out->toks[k].margin = margin >= kInf ? 1e9f : units(margin);
        }
        if (margin < minMargin) {
            minMargin = margin;
            minArc = arg;
        }
    }
    if (minArc >= 0 && minMargin < tau) {
        std::vector<uint32_t> p1;
        for (int32_t a = minArc; a >= 0; a = A[static_cast<size_t>(a)].bp) p1.push_back(static_cast<uint32_t>(a));
        std::reverse(p1.begin(), p1.end());
        for (int32_t a = A[static_cast<size_t>(minArc)].np; a >= 0; a = A[static_cast<size_t>(a)].np)
            p1.push_back(static_cast<uint32_t>(a));
        em.Emit(p1, &out->alt, false, opt, ws);
        out->altMargin = units(minMargin);
    }
}

// ---- segmentation hints ----------------------------------------------------------------------------
}  // namespace hdTrans::attach::ana
