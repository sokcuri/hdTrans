// StageC and StageL (ir/stages.h) over the structure library, which works on its own view of the IR
// (ir_view.h); values cross by their JSON names.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ir/stages.h"
#include "structure.h"

namespace hd08 {

struct StructureData {
    hd8::Strings strings;  // row ids and names of the tables, read-only after load
    hd8::StructureData tables;
};

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream in(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);  // UTF-8 path
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Local {
    hd8::Strings st;
    hd8::Path path;
    std::vector<hd8::TokInfo> info;
    std::vector<hd8::Decision> decisions;
};

void ToLocal(const StructureData& s, const Doc& doc, const std::vector<Token>& toks, Local& out) {
    out.st = s.strings;
    out.path.toks.clear();
    for (const Token& t : toks) {
        hd8::Token k;
        k.src = hd8::Span{t.src.begin, t.src.end};
        k.surf = out.st.Intern(doc.S(t.surf));
        k.lemma = out.st.Intern(doc.S(t.lemma));
        k.reading = out.st.Intern(doc.S(t.reading));
        k.pos = static_cast<hd8::Pos>(t.pos);
        k.ud = out.st.Intern(doc.ud.Str(t.ud));
        k.ctype = out.st.Intern(doc.ctypes.Str(t.ctype));
        k.cform = out.st.Intern(doc.cforms.Str(t.cform));
        k.flags = t.flags;
        out.path.toks.push_back(k);
    }
    out.info = hd8::Annotate(s.tables, out.st, out.path.toks);
}

uint8_t Value(F field, const char* name) {
    unsigned v = 0;
    return Parse(field, name, v) ? static_cast<uint8_t>(v) : 0;
}

void AddDecisions(Doc& doc, Line& line, const Local& l, int path) {
    for (const auto& d : l.decisions) {
        Decision o;
        o.stage = d.stage;
        o.kind = static_cast<Ev>(d.kind);
        o.rule = doc.I(l.st.Get(d.rule));
        o.src = Span{d.src.begin, d.src.end};
        o.score = d.score;
        o.margin = d.margin;
        o.nAlt = d.nAlt;
        for (uint32_t b : d.because) o.because.push_back(doc.I(l.st.Get(b)));
        o.path = static_cast<uint8_t>(path);
        line.Decide(std::move(o));
    }
}

}  // namespace

StructureData* LoadStructure(const Config& cfg, std::string& err) {
    auto* data = new StructureData;
    std::string dir = cfg.data + "/structure/data/";
    if (!hd8::LoadStructureData(ReadFile(dir + "aux.tsv"), ReadFile(dir + "ud.tsv"), ReadFile(dir + "forms.tsv"),
                                ReadFile(dir + "head.tsv"), data->strings, data->tables, err)) {
        delete data;
        return nullptr;
    }
    if (data->tables.aux.empty()) {
        err = "structure: no aux rows in " + dir;
        delete data;
        return nullptr;
    }
    return data;
}

void FreeStructure(StructureData* data) { delete data; }

void StageC(const StructureData& s, const Config&, Doc& doc, Line& line, int path) {
    PathRef p = PathOf(line, path);
    Local l;
    ToLocal(s, doc, p.toks, l);
    hd8::Chunk(s.tables, l.st, l.info, l.path);
    hd8::AttachHeads(s.tables, l.st, l.info, l.path, l.decisions, l.st);
    p.buns.clear();
    for (const auto& b : l.path.buns) {
        Bunsetsu o;
        o.first = b.first;
        o.last = b.last;
        o.content = b.content;
        o.head = b.head;
        o.headMargin = b.headMargin;
        o.head2 = b.head2;
        char t[2] = {static_cast<char>(b.type), 0};
        o.type = Value(F::buntype, t);
        o.kase = static_cast<Case>(b.kase);
        o.flags = b.flags;
        p.buns.push_back(o);
    }
    AddDecisions(doc, line, l, path);
}

void StageL(const StructureData& s, const Config&, Doc& doc, Line& line, int path) {
    PathRef p = PathOf(line, path);
    Local l;
    ToLocal(s, doc, p.toks, l);
    for (const auto& b : p.buns) {
        hd8::Bunsetsu o;
        o.first = b.first;
        o.last = b.last;
        o.content = b.content;
        o.head = b.head;
        o.headMargin = b.headMargin;
        o.head2 = b.head2;
        o.type = static_cast<uint8_t>(Name(F::buntype, b.type).empty() ? 'N' : Name(F::buntype, b.type)[0]);
        o.kase = static_cast<hd8::Case>(b.kase);
        o.flags = b.flags;
        l.path.buns.push_back(o);
    }
    hd8::BuildClauses(s.tables, l.st, l.info, l.path, l.decisions);
    p.clauses.clear();
    for (const auto& c : l.path.clauses) {
        Clause o;
        o.pred = c.pred;
        o.firstBun = c.firstBun;
        o.lastBun = c.lastBun;
        o.parent = c.parent;
        unsigned v = 0;
        if (Parse(F::link, hd8::Name(c.link), v)) {
            o.link = static_cast<uint8_t>(v);
        } else {
            // A link value that IR v2c lacks: kept as a fact until the enum has it.
            hd8::Decision d;
            d.stage = 'L';
            d.kind = hd8::Ev::Lexical;
            d.rule = l.st.Intern("code:BuildClauses");
            d.src = hd8::Span{l.path.toks[l.path.buns[c.pred].first].src.begin,
                              l.path.toks[l.path.buns[c.lastBun].last].src.end};
            std::string fact = std::string("link=") + hd8::Name(c.link);
            if (c.linkSub != hd8::LinkSub::none) fact += std::string(":") + hd8::Name(c.linkSub);
            d.because.push_back(l.st.Intern(fact));
            l.decisions.push_back(std::move(d));
        }
        o.tense = Value(F::tense, hd8::Name(c.tense));
        o.aspect = Value(F::jaspect, hd8::Name(c.aspect));
        o.pol = Value(F::pol, hd8::Name(c.pol));
        o.negFn = Value(F::negfn, hd8::Name(c.negFn));
        o.voice = Value(F::voice, hd8::Name(c.voice));
        o.bene = Value(F::bene, hd8::Name(c.bene));
        o.modality = c.modality;  // same bit order as HD08_MODALITY
        o.force = Value(F::force, hd8::Name(c.force));
        o.polite = Value(F::polite, hd8::Name(c.polite));
        o.jreg = c.jreg;  // same bit order as HD08_JREG
        for (const auto& a : c.args) {
            Arg x;
            x.role = static_cast<Role>(a.role);
            x.bun = a.bun;
            x.lemma = doc.I(l.st.Get(a.lemma));
            x.cls = a.cls ? doc.I(l.st.Get(a.cls)) : 0;
            x.how = a.how;
            o.args.push_back(x);
        }
        o.consumed = c.consumed;
        o.unfolded = c.unfolded;
        p.clauses.push_back(std::move(o));
    }
    AddDecisions(doc, line, l, path);
}

}  // namespace hd08
