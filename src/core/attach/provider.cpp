#include "core/attach/provider.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "core/attach/analyzer.h"
#include "core/attach/model.h"
#include "core/attach/structure.h"
#include "core/dict/builtin.h"
#include "core/dict/mapped_file.h"
#include "core/word/item.h"

namespace hdTrans::dict::internal {
std::span<const BuiltinTable> AttachTables();
}

namespace hdTrans::attach {

struct Data {
    ana::Analyzer analyzer;
    Strings strings;  // row ids and names of the tables
    StructureData tables;
    std::unordered_set<std::string> notVolitional;  // data/volition.tsv
    std::string path;  // the analysis.bin it was loaded from, and its stamp then
    dict::internal::FileStamp stamp;
};

struct ChunkAnalysis {
    explicit ChunkAnalysis(const Strings* base) : st(base) {}
    Strings st;  // over Data::strings
    Path path;
    std::vector<TokInfo> info;
    std::vector<std::string> zero;  // per clause: the class of its subject by data/zero.tsv ("" for none)
};

void ChunkAnalysisDeleter::operator()(ChunkAnalysis* analysis) const { delete analysis; }

namespace {

// A chunk longer than this gets no attachment (the head search is quadratic in its bunsetsu).
constexpr std::size_t kMaxChunkBytes = 4096;

std::string TableText(std::string_view name) {
    std::string text;
    for (const dict::internal::BuiltinTable& t : dict::internal::AttachTables())
        if (t.name == name)
            for (std::string_view line : t.lines) text += line;
    return text;
}

bool Overlaps(Span a, Span b) { return a.begin < b.end && b.begin < a.end; }

bool Verb(Pos p) { return p == Pos::v1 || p == Pos::v5 || p == Pos::vs || p == Pos::vk || p == Pos::vz; }

// The analysis of the chunk's text: tokens, bunsetsu and heads (C), clauses (L) and the omitted-subject
// classes (Z).
std::unique_ptr<ChunkAnalysis, ChunkAnalysisDeleter> Analyse(const Data& d, std::string_view text) {
    ana::Analysis r;
    ana::Options opt;
    opt.margins = false;
    ana::WorkspacePtr ws = ana::NewWorkspace();
    d.analyzer.Analyze(text, opt, &r, ws.get());
    std::unique_ptr<ChunkAnalysis, ChunkAnalysisDeleter> a(new ChunkAnalysis(&d.strings));
    for (const ana::Token& t : r.toks) {
        Token k;
        k.src = Span{t.src.begin, t.src.end};
        k.surf = a->st.Intern(t.surf);
        k.lemma = a->st.Intern(t.lemma);
        k.reading = a->st.Intern(t.reading);
        k.pos = static_cast<Pos>(t.pos);
        k.ud = a->st.Intern(d.analyzer.UdName(t.ud));
        k.ctype = a->st.Intern(d.analyzer.CtypeName(t.ctype));
        k.cform = a->st.Intern(d.analyzer.CformName(t.cform));
        k.flags = t.flags;
        a->path.toks.push_back(k);
    }
    a->info = Annotate(d.tables, a->st, a->path.toks);
    std::vector<Decision> decisions;
    Chunk(d.tables, a->st, a->info, a->path);
    AttachHeads(d.tables, a->st, a->info, a->path);
    BuildClauses(d.tables, a->st, a->info, a->path, decisions);
    std::string rule;
    for (std::size_t c = 0; c < a->path.clauses.size(); c++)
        a->zero.push_back(ZeroByRules(d.tables, a->st, a->info, a->path, static_cast<int>(c), false, rule));
    return a;
}

// The source span of a chain item in the chunk's text: its token's bytes when the token's text is them byte
// for byte, else its surface searched forward from the item before.
Span ItemSpan(std::string_view text, const tokenizer::Tokens& tokens, const word::Item& it, uint32_t& from) {
    if (it.length <= 0 || it.start < 0) return {from, from};
    const auto start = static_cast<std::size_t>(it.start), length = static_cast<std::size_t>(it.length);
    const std::string_view surface = start < it.text.size() ? it.text.substr(start, length) : std::string_view();
    if (it.tokenIndex >= 0 && static_cast<std::size_t>(it.tokenIndex) < tokens.size()) {
        const auto ti = static_cast<std::size_t>(it.tokenIndex);
        const tokenizer::Token& t = tokens.tokens[ti];
        if (t.sourceEnd - t.sourceBegin == it.text.size() && tokens.Raw(ti) == it.text) {
            const uint32_t b = std::min(t.sourceBegin + static_cast<uint32_t>(start), t.sourceEnd);
            const uint32_t e = std::min(b + static_cast<uint32_t>(length), t.sourceEnd);
            if (b < e && e <= text.size()) {
                from = e;
                return {b, e};
            }
        }
    }
    if (surface.empty()) return {from, from};
    const std::size_t at = text.find(surface, from);
    const std::size_t reach = std::max<std::size_t>(24, 4 * surface.size());
    if (at == std::string_view::npos || at - from > reach) return {from, from};
    from = static_cast<uint32_t>(at + surface.size());
    return {static_cast<uint32_t>(at), from};
}

// The volitionality of a clause (cl.vol, main.vol): a passive, potential or spontaneous predicate is not
// volitional; a volitional, wish or imperative form is; then data/volition.tsv, and + for any other verb or
// verbal noun.
const char* Volition(const Data& d, const ChunkAnalysis& a, const Clause& c) {
    if (c.voice == Voice::passive || c.voice == Voice::potential || c.voice == Voice::rare ||
        c.voice == Voice::spontaneous)
        return "-";
    if ((c.modality & (Volitional | Want)) != 0 || c.force == Force::imp) return "+";
    if (c.pred >= a.path.buns.size()) return "-";
    const Bunsetsu& pb = a.path.buns[c.pred];
    if (pb.content >= a.path.toks.size()) return "-";
    const Token& t = a.path.toks[pb.content];
    bool verbalNoun = false;
    if (t.pos == Pos::n && pb.content + 1u <= pb.last && pb.content + 1u < a.path.toks.size()) {
        const std::string& next = a.st.Get(a.path.toks[pb.content + 1u].lemma);
        verbalNoun = next == "為る" || next == "する";
    }
    if (!Verb(t.pos) && !verbalNoun) return "-";
    return d.notVolitional.count(a.st.Get(t.lemma)) != 0 ? "-" : "+";
}

bool Provide(void* context, const tokenizer::Tokens& tokens, const word::Chain& chain, word::Attachment& out) {
    return AttachChain(*static_cast<ChunkAttach*>(context), tokens, chain, out);
}

}  // namespace

namespace {

// Loads the analyzer snapshot and the built-in structure tables into d.
bool Fill(Data& d, std::string_view snapshot, std::string& err) {
    if (!d.analyzer.Load(snapshot, &err)) return false;
    if (!LoadStructureData(TableText("aux.tsv"), TableText("ud.tsv"), TableText("forms.tsv"), d.strings, d.tables,
                           err) ||
        !LoadHeadRules(TableText("heads.tsv"), d.tables, err) || !LoadZeroRules(TableText("zero.tsv"), d.tables, err))
        return false;
    LoadBareObject(TableText("bareobj.tsv"), d.tables);
    LoadPersons(TableText("persons.tsv"), d.tables);
    const std::string vol = TableText("volition.tsv");
    for (std::size_t p = 0; p < vol.size();) {
        std::size_t e = vol.find('\n', p);
        if (e == std::string::npos) e = vol.size();
        const std::string_view line = std::string_view(vol).substr(p, e - p);
        p = e + 1;
        const std::size_t tab = line.find('\t');
        if (line.empty() || line[0] == '#' || tab == std::string_view::npos || line.substr(0, tab) == "lemma") continue;
        if (line.substr(tab + 1, 1) == "-") d.notVolitional.emplace(line.substr(0, tab));
    }
    if (d.tables.aux.empty()) {
        err = "no aux rows";
        return false;
    }
    return true;
}

}  // namespace

std::string CompileAnalysis(const std::string& modelDir, std::string& err) {
    ana::Model m;
    if (!ana::LoadTsvDir(modelDir, &m, &err)) return {};
    ana::TrimReadings(&m);
    ana::Finalize(&m);
    return ana::Serialize(m);
}

std::shared_ptr<const Data> LoadData(std::string_view snapshot, std::string& err) {
    std::unique_ptr<Data> d(new Data);
    if (!Fill(*d, snapshot, err)) return nullptr;
    return std::shared_ptr<const Data>(d.release());
}

std::shared_ptr<const Data> LoadFolders(const std::vector<std::string>& folders,
                                        const std::shared_ptr<const Data>& previous, std::string& err) {
    err.clear();
    for (auto f = folders.rbegin(); f != folders.rend(); ++f) {
        const std::string path = *f + "/analysis.bin";
        dict::internal::FileStamp stamp;
        if (!dict::internal::ReadStamp(path, stamp)) continue;
        if (previous != nullptr && previous->path == path && previous->stamp == stamp) return previous;
        const auto mapped = dict::internal::MappedFile::Open(path);
        if (mapped == nullptr) {
            err = path + ": cannot read";
            return nullptr;
        }
        const auto bytes = mapped->Bytes();
        std::string_view data(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::unique_ptr<Data> d(new Data);
        if (!Fill(*d, data, err)) {
            err = path + ": " + err;
            return nullptr;
        }
        d->path = path;
        d->stamp = stamp;
        return std::shared_ptr<const Data>(d.release());
    }
    return nullptr;
}

word::AttachProvider ChunkAttach::Provider() {
    word::AttachProvider p;
    p.context = this;
    p.attach = &Provide;
    return p;
}

bool AttachChain(ChunkAttach& chunk, const tokenizer::Tokens& tokens, const word::Chain& chain, word::Attachment& out) {
    if (chunk.data == nullptr) return false;
    if (!chunk.tried) {
        chunk.tried = true;
        if (chunk.text.size() <= kMaxChunkBytes) chunk.analysis = Analyse(*chunk.data, chunk.text);
    }
    if (chunk.analysis == nullptr || chunk.analysis->path.clauses.empty()) return false;
    const Data& d = *chunk.data;
    const ChunkAnalysis& a = *chunk.analysis;
    const Path& path = a.path;
    std::vector<Span> spans;
    uint32_t from = 0;
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) spans.push_back(ItemSpan(chunk.text, tokens, *it, from));
    // the item whose span holds token t, -1 when none does
    auto itemOf = [&](std::size_t t) {
        if (t >= path.toks.size()) return -1;
        for (std::size_t k = 0; k < spans.size(); k++)
            if (Overlaps(spans[k], path.toks[t].src)) return static_cast<int>(k);
        return -1;
    };
    const std::size_t nb = path.buns.size();
    auto contentItem = [&](std::size_t b) { return b < nb ? itemOf(path.buns[b].content) : -1; };
    out.items.assign(spans.size(), word::AttachItem{});
    out.clauses.clear();
    // bunsetsu → the clause that owns it: the clause whose bunsetsu range holds it, inner clauses first
    std::vector<int> bunClause(nb, -1);
    for (std::size_t c = 0; c < path.clauses.size(); c++) {
        const Clause& cl = path.clauses[c];
        for (std::size_t b = cl.firstBun; b <= cl.lastBun && b < nb; b++)
            if (bunClause[b] < 0 || path.clauses[static_cast<std::size_t>(bunClause[b])].firstBun < cl.firstBun)
                bunClause[b] = static_cast<int>(c);
    }
    for (std::size_t b = 0; b < nb; b++) {
        const int it = contentItem(b);
        if (it >= 0 && out.items[static_cast<std::size_t>(it)].clause < 0)
            out.items[static_cast<std::size_t>(it)].clause = bunClause[b];
    }
    // noun modification: a bunsetsu ending in の, or an attributive word or clause, and the noun it modifies
    for (std::size_t b = 0; b < nb; b++) {
        const Bunsetsu& bu = path.buns[b];
        if (bu.head < 0 || static_cast<std::size_t>(bu.head) >= nb) continue;
        const bool no = bu.kase == Case::no;
        bool adn = false;
        for (const Clause& cl : path.clauses) adn = adn || (cl.pred == b && cl.link == Link::adn);
        if (!no && !adn && bu.type != 'A') continue;
        const int it = contentItem(b), head = contentItem(static_cast<std::size_t>(bu.head));
        if (it < 0 || head < 0 || it == head) continue;
        word::AttachItem& ii = out.items[static_cast<std::size_t>(it)];
        if (ii.modifies >= 0) continue;
        ii.modifies = head;
        ii.modKind = no ? 1 : 2;
    }
    for (std::size_t c = 0; c < path.clauses.size(); c++) {
        const Clause& cl = path.clauses[c];
        word::AttachClause& o = out.clauses.emplace_back();
        o.predItem = contentItem(cl.pred);
        o.parent = cl.parent;
        if (o.predItem >= 0) {
            word::AttachItem& p = out.items[static_cast<std::size_t>(o.predItem)];
            p.pred = true;
            p.clause = static_cast<int>(c);
        }
        const std::string& zero = a.zero[c];
        auto feature = [&](const char* name, std::string_view value) {
            if (!value.empty()) o.features.emplace_back(name, std::string(value));
        };
        feature("link", Name(cl.link));
        feature("tense", Name(cl.tense));
        feature("aspect", Name(cl.aspect));
        feature("pol", Name(cl.pol));
        feature("negfn", Name(cl.negFn));
        feature("voice", Name(cl.voice));
        feature("bene", Name(cl.bene));
        feature("force", Name(cl.force));
        feature("polite", Name(cl.polite));
        feature("vol", Volition(d, a, cl));
        feature("subj", zero.empty() ? std::string_view("none") : std::string_view(zero));
        bool subject = false;
        for (const Arg& arg : cl.args) {
            if (arg.how == static_cast<uint8_t>(ArgHow::omitted)) continue;  // Z's omitted subject is added below
            word::AttachArg& g = o.args.emplace_back();
            g.role = Name(arg.role);
            g.how = Name(static_cast<ArgHow>(arg.how));
            g.lemma = a.st.Get(arg.lemma);
            g.cls = a.st.Get(arg.cls);
            g.item = contentItem(arg.bun);
            subject = subject || arg.role == Role::subj || arg.role == Role::topic;
            // the head noun of an attributive clause is not its argument here (the noun carries mod on the
            // clause's side); an item takes its role in the nearest clause that takes it
            if (g.item < 0 || arg.how == static_cast<uint8_t>(ArgHow::relhead)) continue;
            word::AttachItem& ii = out.items[static_cast<std::size_t>(g.item)];
            if (ii.argOf >= 0) continue;
            ii.argOf = static_cast<int>(c);
            ii.headItem = o.predItem;
            ii.role = g.role;
            ii.how = g.how;
        }
        // The omitted subject: Z's class of it (a speaker, addressee or third person is @person).
        if (!subject && cl.link != Link::adn && zero != "overt") {
            word::AttachArg& g = o.args.emplace_back();
            g.role = "subj";
            g.how = "omitted";
            g.cls = zero == "speaker" || zero == "addressee" || zero == "third" ? "@person" : "";
        }
    }
    return true;
}

}  // namespace hdTrans::attach
