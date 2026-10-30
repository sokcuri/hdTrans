#include "ir_json.h"

#include <cmath>
#include <limits>
#include <type_traits>

namespace hd08 {

namespace {

// One field list per struct (Fields below) drives both the writer and the reader.

struct Writer {
    static constexpr bool kReading = false;
    const Doc& doc;
    std::string& o;
    bool first = true;
    bool all = false;  // write zero fields too (alternative tokens are read as deltas)

    void Key(const char* k) {
        if (!first) o += ',';
        first = false;
        AppendJsonString(o, k);
        o += ':';
    }
    template <class I> void Int(const char* k, I& v, long long def = 0) {
        if (!all && static_cast<long long>(v) == def) return;
        Key(k);
        o += std::to_string(static_cast<long long>(v));
    }
    void Float(const char* k, float& v) {
        if (!all && v == 0) return;
        Key(k);
        AppendJsonNumber(o, v);
    }
    void Bool(const char* k, bool& v) {
        if (!all && !v) return;
        Key(k);
        o += v ? "true" : "false";
    }
    void Char(const char* k, uint8_t& v) {
        if (!v) return;
        Key(k);
        AppendJsonString(o, std::string_view(reinterpret_cast<const char*>(&v), 1));
    }
    void Interned(const char* k, uint32_t& v) {
        if (!all && !v) return;
        Key(k);
        AppendJsonString(o, doc.S(v));
    }
    void Text(const char* k, std::string& s, bool always = false) {
        if (s.empty() && !always) return;
        Key(k);
        AppendJsonString(o, s);
    }
    template <class E> void Enum(const char* k, E& v, F f, bool always = false) {
        const auto u = static_cast<unsigned>(v);
        if (!u && !always && !all) return;
        Key(k);
        AppendJsonString(o, Name(f, u));
    }
    template <class B> void Bits(const char* k, B& v, F f) {
        if (!all && !v) return;
        Key(k);
        o += '[';
        bool firstBit = true;
        for (unsigned b = 0; b < Count(f); b++) {
            if (!(static_cast<uint64_t>(v) & (uint64_t{1} << b))) continue;
            if (!firstBit) o += ',';
            firstBit = false;
            AppendJsonString(o, Name(f, b));
        }
        o += ']';
    }
    void SpanF(const char* k, Span& s, bool always = false) {
        if (!always && !all && s.begin == 0 && s.end == 0) return;
        Key(k);
        o += '[' + std::to_string(s.begin) + ',' + std::to_string(s.end) + ']';
    }
    template <class I> void Table(const char* k, I& v, Strings Doc::*table) {
        if (!all && !v) return;
        Key(k);
        AppendJsonString(o, (doc.*table).Str(v));
    }
    template <class T> void Vec(const char* k, std::vector<T>& vec) {
        if (vec.empty()) return;
        Key(k);
        o += '[';
        for (size_t i = 0; i < vec.size(); i++) {
            if (i) o += ',';
            Object(vec[i]);
        }
        o += ']';
    }
    template <class T> void Object(T& x, bool everything = false) {
        o += '{';
        Writer sub{doc, o};
        sub.all = everything;
        Fields(sub, x);
        o += '}';
    }
    template <class T> void Sub(const char* k, T& x, bool present) {
        if (!present) return;
        Key(k);
        Object(x);
    }
    void Contrib(const char* k, std::vector<std::pair<uint32_t, float>>& v) {
        if (v.empty()) return;
        Key(k);
        o += '{';
        for (size_t i = 0; i < v.size(); i++) {
            if (i) o += ',';
            AppendJsonString(o, doc.S(v[i].first));
            o += ':';
            AppendJsonNumber(o, v[i].second);
        }
        o += '}';
    }
    template <class I> void Ints(const char* k, std::vector<I>& v) {
        if (v.empty()) return;
        Key(k);
        o += '[';
        for (size_t i = 0; i < v.size(); i++) {
            if (i) o += ',';
            o += std::to_string(static_cast<long long>(v[i]));
        }
        o += ']';
    }
    void Strs(const char* k, std::vector<uint32_t>& v) {
        if (v.empty()) return;
        Key(k);
        o += '[';
        for (size_t i = 0; i < v.size(); i++) {
            if (i) o += ',';
            AppendJsonString(o, doc.S(v[i]));
        }
        o += ']';
    }
    template <class I, size_t N> void IntArr(const char* k, I (&arr)[N]) {
        size_t n = N;
        while (n > 0 && arr[n - 1] == 0) n--;
        if (n == 0) return;
        Key(k);
        o += '[';
        for (size_t i = 0; i < n; i++) {
            if (i) o += ',';
            o += std::to_string(static_cast<long long>(arr[i]));
        }
        o += ']';
    }
};

struct Reader {
    static constexpr bool kReading = true;
    Doc& doc;
    const Json& obj;
    std::string& err;
    int pendingRel = -1;  // "ctype":"conn:<rel>"

    const Json* Find(const char* k) const { return obj.type == Json::Obj ? obj.Get(k) : nullptr; }
    // A JSON number that is an integer within the range of I.
    template <class I> bool ToInt(const char* k, const Json& j, I& out) {
        if (j.type != Json::Num || !std::isfinite(j.n) || j.n != std::floor(j.n) ||
            j.n < static_cast<double>(std::numeric_limits<I>::min()) ||
            j.n > static_cast<double>(std::numeric_limits<I>::max())) {
            Bad(k, "expected an integer in range");
            return false;
        }
        out = static_cast<I>(j.n);
        return true;
    }
    float ToFloat(const char* k, const Json& j) {
        if (j.type != Json::Num || !std::isfinite(j.n)) {
            Bad(k, "expected a finite number");
            return 0;
        }
        return static_cast<float>(j.n);
    }
    void Bad(const char* k, const char* why) {
        if (err.empty()) err = std::string(k) + ": " + why;
    }
    template <class I> void Int(const char* k, I& v, long long = 0) {
        const Json* j = Find(k);
        if (!j) return;
        ToInt(k, *j, v);
    }
    void Float(const char* k, float& v) {
        const Json* j = Find(k);
        if (!j) return;
        v = ToFloat(k, *j);
    }
    void Bool(const char* k, bool& v) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type == Json::Bool) v = j->b;
        else if (j->type == Json::Num) v = j->n != 0;
        else Bad(k, "expected a boolean");
    }
    void Char(const char* k, uint8_t& v) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Str || j->s.size() != 1) return Bad(k, "expected one letter");
        v = static_cast<uint8_t>(j->s[0]);
    }
    void Interned(const char* k, uint32_t& v) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type == Json::Str) v = doc.I(j->s);
        else if (j->type == Json::Null) v = 0;
        else Bad(k, "expected a string");
    }
    void Text(const char* k, std::string& s, bool = false) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Str) return Bad(k, "expected a string");
        s = j->s;
    }
    bool EnumValue(const char* k, const Json& j, F f, unsigned& u) {
        if (j.type == Json::Num) {
            if (!ToInt(k, j, u)) return false;
            if (u >= Count(f)) {
                Bad(k, "value out of range");
                return false;
            }
            return true;
        }
        if (j.type == Json::Str) {
            if (j.s.empty()) {
                u = 0;
                return true;
            }
            if (Parse(f, j.s, u)) return true;
            if (f == F::ctype && j.s.starts_with("conn:")) {
                unsigned r = 0;
                if (Parse(F::rel, std::string_view(j.s).substr(5), r)) {
                    pendingRel = static_cast<int>(r);
                    u = ctype::conn;
                    return true;
                }
            }
            if (err.empty()) err = std::string(k) + ": unknown value " + j.s;
            return false;
        }
        Bad(k, "expected a name");
        return false;
    }
    template <class E> void Enum(const char* k, E& v, F f, bool = false) {
        const Json* j = Find(k);
        if (!j) return;
        unsigned u = 0;
        if (EnumValue(k, *j, f, u)) v = static_cast<E>(u);
    }
    template <class B> void Bits(const char* k, B& v, F f) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type == Json::Num) {
            uint64_t bits = 0;
            if (!ToInt(k, *j, bits)) return;
            if (bits >> Count(f)) return Bad(k, "unknown bit");
            v = static_cast<B>(bits);
            return;
        }
        if (j->type != Json::Arr) return Bad(k, "expected an array of names");
        uint64_t bits = 0;
        for (const Json& e : j->a) {
            unsigned u = 0;
            if (EnumValue(k, e, f, u)) bits |= uint64_t{1} << u;
        }
        v = static_cast<B>(bits);
    }
    void SpanF(const char* k, Span& s, bool = false) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Arr || j->a.size() != 2 || j->a[0].type != Json::Num || j->a[1].type != Json::Num)
            return Bad(k, "expected [begin,end]");
        if (!ToInt(k, j->a[0], s.begin) || !ToInt(k, j->a[1], s.end)) return;
        if (s.end < s.begin) Bad(k, "span ends before it begins");
    }
    template <class I> void Table(const char* k, I& v, Strings Doc::*table) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Str) return Bad(k, "expected a name");
        const uint32_t id = (doc.*table).Intern(j->s);
        if (id > std::numeric_limits<I>::max()) return Bad(k, "too many distinct names");
        v = static_cast<I>(id);
    }
    template <class T> void Vec(const char* k, std::vector<T>& vec) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Arr) return Bad(k, "expected an array");
        vec.clear();
        for (const Json& e : j->a) {
            vec.emplace_back();
            Reader sub{doc, e, err};
            Fields(sub, vec.back());
        }
    }
    template <class T> void Sub(const char* k, T& x, bool) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Obj) return Bad(k, "expected an object");
        Reader sub{doc, *j, err};
        Fields(sub, x);
    }
    void Contrib(const char* k, std::vector<std::pair<uint32_t, float>>& v) {
        const Json* j = Find(k);
        if (!j) return;
        v.clear();
        if (j->type == Json::Obj) {
            for (const auto& [name, value] : j->o) v.emplace_back(doc.I(name), ToFloat(k, value));
        } else if (j->type == Json::Arr) {  // [[name, value], …]
            for (const Json& e : j->a)
                if (e.type == Json::Arr && e.a.size() == 2) v.emplace_back(doc.I(e.a[0].s), ToFloat(k, e.a[1]));
        } else {
            Bad(k, "expected an object of contributions");
        }
    }
    template <class I> void Ints(const char* k, std::vector<I>& v) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Arr) return Bad(k, "expected an array");
        v.clear();
        for (const Json& e : j->a) {
            I x{};
            if (!ToInt(k, e, x)) return;
            v.push_back(x);
        }
    }
    void Strs(const char* k, std::vector<uint32_t>& v) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Arr) return Bad(k, "expected an array");
        v.clear();
        for (const Json& e : j->a) {
            if (e.type != Json::Str) return Bad(k, "expected strings");
            v.push_back(doc.I(e.s));
        }
    }
    template <class I, size_t N> void IntArr(const char* k, I (&arr)[N]) {
        const Json* j = Find(k);
        if (!j) return;
        if (j->type != Json::Arr) return Bad(k, "expected an array");
        if (j->a.size() > N) return Bad(k, "too many values");
        for (size_t i = 0; i < N; i++) {
            arr[i] = I{0};
            if (i < j->a.size() && !ToInt(k, j->a[i], arr[i])) return;
        }
    }
};

template <class V> void Fields(V& v, NormEdit& x) {
    v.SpanF("src", x.src, true);
    v.Int("from", x.from);
    v.Int("to", x.to);
    v.Enum("kind", x.kind, F::edit, true);
}

template <class V> void Fields(V& v, Token& x) {
    v.SpanF("src", x.src, true);
    v.Interned("surf", x.surf);
    v.Interned("lemma", x.lemma);
    v.Interned("reading", x.reading);
    v.Interned("lex", x.lex);
    v.Enum("pos", x.pos, F::pos, true);
    v.Table("ud", x.ud, &Doc::ud);
    v.Table("ctype", x.ctype, &Doc::ctypes);
    v.Table("cform", x.cform, &Doc::cforms);
    v.Bits("flags", x.flags, F::tokflag);
    v.Float("cost", x.cost);
    v.Float("margin", x.margin);
    v.Interned("derive", x.derive);
    v.Interned("removedBy", x.removedBy);
}

template <class V> void Fields(V& v, Utterance& x) {
    v.SpanF("span", x.span, true);
    v.Int("depth", x.depth);
    v.Int("scene", x.scene);
    v.Int("speaker", x.speaker);
    v.Int("addressee", x.addressee);
    v.Bool("narration", x.narration);
    v.Enum("sourceLevel", x.sourceLevel, F::srclevel);
}

template <class V> void Fields(V& v, Mention& x) {
    v.SpanF("src", x.src, true);
    v.Interned("entity", x.entity);
    v.Float("evidence", x.evidence);
    v.Int("decision", x.decision);
}

template <class V> void Fields(V& v, Speaker& x) {
    v.Interned("name", x.name);
    v.IntArr("firstPerson", x.firstPerson);
    v.Int("desuMasu", x.desuMasu);
    v.Int("plain", x.plain);
    v.Int("keigo", x.keigo);
    v.IntArr("finals", x.finals);
    v.Enum("persona", x.persona, F::persona);
}

template <class V> void Fields(V& v, Scene& x) {
    v.Int("firstLine", x.firstLine);
    v.Int("lastLine", x.lastLine);
    v.Int("decision", x.decision);
}

template <class V> void Fields(V& v, Bunsetsu& x) {
    v.Int("first", x.first);
    v.Int("last", x.last);
    v.Int("content", x.content);
    v.Int("head", x.head, -1);
    v.Float("headMargin", x.headMargin);
    v.Int("head2", x.head2, -1);
    v.Enum("type", x.type, F::buntype, true);
    v.Enum("kase", x.kase, F::kase_ja);
    v.Bits("flags", x.flags, F::bunflag);
}

template <class V> void Fields(V& v, Arg& x) {
    v.Enum("role", x.role, F::role, true);
    v.Int("bun", x.bun);
    v.Interned("lemma", x.lemma);
    v.Interned("cls", x.cls);
    v.Enum("how", x.how, F::how);
}

template <class V> void Fields(V& v, Referent& x) {
    v.Enum("kind", x.kind, F::refkind, true);
    v.Interned("entity", x.entity);
    v.Int("bun", x.bun, 0xFFFF);
    v.Float("score", x.score);
    v.Float("margin", x.margin);
    v.Int("decision", x.decision);
}

bool Present(const Referent& r) { return r.kind || r.entity || r.decision || r.bun != 0xFFFF; }

template <class V> void Fields(V& v, Clause& x) {
    v.Int("pred", x.pred);
    v.Int("firstBun", x.firstBun);
    v.Int("lastBun", x.lastBun);
    v.Int("parent", x.parent, -1);
    v.Enum("link", x.link, F::link);
    v.Enum("linkSub", x.linkSub, F::timesub);
    v.Enum("tense", x.tense, F::tense);
    v.Enum("aspect", x.aspect, F::jaspect);
    v.Enum("pol", x.pol, F::pol);
    v.Enum("negFn", x.negFn, F::negfn);
    v.Enum("voice", x.voice, F::voice);
    v.Enum("bene", x.bene, F::bene);
    v.Bits("modality", x.modality, F::modality);
    v.Enum("force", x.force, F::force);
    v.Enum("polite", x.polite, F::polite);
    v.Bits("jreg", x.jreg, F::jreg);
    v.Vec("args", x.args);
    v.Ints("consumed", x.consumed);
    v.Ints("unfolded", x.unfolded);
    v.Sub("subj", x.subj, Present(x.subj));
    v.Sub("beneTo", x.beneTo, Present(x.beneTo));
}

template <class V> void Fields(V& v, Chosen& x) {
    v.Int("path", x.path);
    v.Ints("heads", x.heads);
}

template <class V> void Fields(V& v, KNode& x) {
    v.Int("id", x.id, -1);
    v.Int("origin", x.origin);
    v.Enum("kind", x.kind, F::kk, true);
    v.Interned("lemma", x.lemma);
    v.Enum("kcls", x.kcls, F::kcls);
    v.Bits("sem", x.sem, F::sem);
    v.Enum("tense", x.tense, F::tense);
    v.Enum("aspect", x.aspect, F::kaspect);
    v.Enum("mood", x.mood, F::mood);
    v.Enum("ctype", x.ctype, F::ctype);
    v.Enum("rel", x.rel, F::rel);
    v.Enum("neg", x.neg, F::kneg);
    bool aux = x.aux, dep = x.dep, passive = x.passive, causative = x.causative;
    v.Bool("aux", aux);
    v.Bool("dep", dep);
    v.Bool("passive", passive);
    v.Bool("causative", causative);
    x.aux = aux;
    x.dep = dep;
    x.passive = passive;
    x.causative = causative;
    v.Bits("ja", x.ja, F::jafact);
    v.Enum("kase", x.kase, F::kase);
    v.Enum("punct", x.punct, F::punct);
    v.Interned("jaMark", x.jaMark);
    v.SpanF("src", x.src);
    v.Int("clause", x.clause);
    v.Int("utt", x.utt);
    v.Int("decision", x.decision);
    if constexpr (V::kReading) {
        if (v.pendingRel >= 0 && v.Find("rel") == nullptr) x.rel = static_cast<uint8_t>(v.pendingRel);
    }
}

template <class V> void Fields(V& v, KReg& x) {
    v.Int("node", x.node);
    v.Enum("level", x.level, F::level, true);
    v.Enum("persona", x.persona, F::persona);
    v.Bool("hon", x.hon);
    v.Int("decision", x.decision);
}

template <class V> void Fields(V& v, UttLevel& x) {
    v.Int("utt", x.utt);
    v.Enum("level", x.level, F::level, true);
    v.Enum("persona", x.persona, F::persona);
    v.Int("decision", x.decision);
}

template <class V> void Fields(V& v, OutSpan& x) {
    v.Int("begin", x.begin, -1);
    v.Int("end", x.end, -1);
    v.Int("node", x.node);
}

template <class V> void Fields(V& v, Flag& x) {
    v.Enum("kind", x.kind, F::flag, true);
    v.SpanF("src", x.src, true);
    v.Int("node", x.node);
    v.Int("inv", x.inv);
}

template <class V> void Fields(V& v, Decision& x) {
    v.Char("stage", x.stage);
    v.Enum("kind", x.kind, F::ev, true);
    v.Interned("rule", x.rule);
    v.SpanF("src", x.src, true);
    v.Float("score", x.score);
    v.Float("margin", x.margin);
    v.Int("nAlt", x.nAlt);
    v.Interned("runnerUp", x.runnerUp);
    v.Strs("because", x.because);
    v.Contrib("contrib", x.contrib);
    v.Int("path", x.path);
    v.Interned("flip", x.flip);
    v.Enum("st", x.st, F::st);
}

void WriteToks(Writer& w, const Line& line, const std::vector<Token>& toks) {
    if (toks.empty()) return;
    w.Key("toks");
    std::string& o = w.o;
    o += '[';
    for (size_t i = 0; i < toks.size(); i++) {
        if (i) o += ',';
        Token tok = toks[i];
        o += '{';
        Writer sub{w.doc, o};
        Fields(sub, tok);
        if (tok.nAlt) {
            sub.Key("alts");
            o += '[';
            for (uint32_t k = 0; k < tok.nAlt && tok.alt + k < line.alts.size(); k++) {
                if (k) o += ',';
                Token a = line.alts[tok.alt + k];
                sub.Object(a, true);
            }
            o += ']';
        }
        o += '}';
    }
    o += ']';
}

bool ReadToks(const Json& toks, Doc& doc, Line& line, std::vector<Token>& out, std::string& err) {
    if (toks.type != Json::Arr) {
        err = "toks: expected an array";
        return false;
    }
    out.clear();
    for (const Json& t : toks.a) {
        Token tok;
        Reader r{doc, t, err};
        Fields(r, tok);
        if (const Json* alts = t.Get("alts"); alts && alts->type == Json::Arr) {
            tok.alt = static_cast<uint16_t>(line.alts.size());
            tok.nAlt = static_cast<uint16_t>(alts->a.size());
            for (const Json& a : alts->a) {
                Token alt = tok;  // an alternative names only what differs
                alt.nAlt = 0;
                alt.alt = 0;
                Reader ra{doc, a, err};
                Fields(ra, alt);
                line.alts.push_back(alt);
            }
        }
        out.push_back(tok);
    }
    return err.empty();
}

}  // namespace

std::string LineJson(const Doc& doc, const Line& constLine, uint32_t parts) {
    Line& line = const_cast<Line&>(constLine);  // the writer only reads
    std::string o = "{";
    Writer w{doc, o};
    if (parts & kPartSrc) {
        std::string id = doc.id;
        w.Text("doc", id);
        w.Text("case", line.name);
        w.Int("line", line.id);
        w.Bool("context", line.context);
        w.Text("src", line.src, true);
    }
    if (parts & kPartNorm) {
        if (line.norm != line.src) {
            w.Text("norm", line.norm, true);
            w.Ints("map", line.map);
        }
        w.Vec("edits", line.edits);
    }
    if (parts & kPartToks) {
        WriteToks(w, line, line.toks);
        if (!line.alt.toks.empty()) {
            w.Key("alt");
            o += '{';
            Writer sub{doc, o};
            WriteToks(sub, line, line.alt.toks);
            if (parts & kPartBuns) sub.Vec("buns", line.alt.buns);
            if (parts & kPartClauses) sub.Vec("clauses", line.alt.clauses);
            o += '}';
        }
    }
    if (parts & kPartScope) {
        w.Vec("utts", line.utts);
        w.Vec("mentions", line.mentions);
    }
    if (parts & kPartBuns) w.Vec("buns", line.buns);
    if (parts & kPartClauses) w.Vec("clauses", line.clauses);
    if (parts & kPartKir) {
        w.Vec("kir", line.kir);
        w.Sub("chosen", line.chosen, line.chosen.path != 0 || !line.chosen.heads.empty());
    }
    if (parts & kPartReg) {
        w.Vec("reg", line.reg);
        w.Vec("lev", line.lev);
    }
    if (parts & kPartKir2) w.Vec("kir2", line.kir2);
    if (parts & kPartOut) {
        w.Text("out", line.out, true);
        w.Vec("spans", line.spans);
    }
    if (parts & kPartFlags) w.Vec("flags", line.flags);
    if (parts & kPartDecisions) w.Vec("decisions", line.decisions);
    o += '}';
    return o;
}

std::string DocJson(const Doc& constDoc) {
    Doc& doc = const_cast<Doc&>(constDoc);
    std::string o = "{";
    Writer w{doc, o};
    w.Text("doc", doc.id, true);
    w.Bool("wrapped", doc.wrapped);
    w.Vec("scenes", doc.scenes);
    w.Vec("speakers", doc.speakers);
    o += '}';
    return o;
}

bool ReadLine(const Json& record, Doc& doc, Line& line, std::string& err) {
    if (record.type != Json::Obj) {
        err = "a record must be an object";
        return false;
    }
    Reader r{doc, record, err};
    r.Text("case", line.name);
    r.Int("line", line.id);
    r.Bool("context", line.context);
    r.Text("src", line.src);
    r.Text("norm", line.norm);
    if (!record.Get("norm")) line.norm = line.src;  // writers omit norm and map when norm equals src
    if (line.map.empty() && !record.Get("map") && line.norm == line.src && !line.src.empty()) {
        line.map.resize(line.src.size() + 1);
        for (uint32_t i = 0; i <= line.src.size(); i++) line.map[i] = i;
    }
    r.Ints("map", line.map);
    r.Vec("edits", line.edits);
    if (const Json* toks = record.Get("toks")) {
        line.alts.clear();
        ReadToks(*toks, doc, line, line.toks, err);
    }
    if (const Json* alt = record.Get("alt"); alt && alt->type == Json::Obj) {
        if (const Json* toks = alt->Get("toks")) ReadToks(*toks, doc, line, line.alt.toks, err);
        Reader ra{doc, *alt, err};
        ra.Vec("buns", line.alt.buns);
        ra.Vec("clauses", line.alt.clauses);
    }
    r.Vec("utts", line.utts);
    r.Vec("mentions", line.mentions);
    r.Vec("buns", line.buns);
    r.Vec("clauses", line.clauses);
    r.Vec("kir", line.kir);
    r.Sub("chosen", line.chosen, true);
    r.Vec("reg", line.reg);
    r.Vec("lev", line.lev);
    r.Vec("kir2", line.kir2);
    r.Text("out", line.out);
    r.Vec("spans", line.spans);
    r.Vec("flags", line.flags);
    r.Vec("decisions", line.decisions);
    return err.empty();
}

bool ReadDoc(const Json& record, Doc& doc, std::string& err) {
    Reader r{doc, record, err};
    r.Text("doc", doc.id);
    r.Bool("wrapped", doc.wrapped);
    r.Vec("scenes", doc.scenes);
    r.Vec("speakers", doc.speakers);
    return err.empty();
}

bool ReadJsonl(std::string_view text, std::vector<Doc>& docs, std::string& err) {
    size_t start = 0;
    size_t lineNo = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view row = text.substr(start, end - start);
        start = end + 1;
        lineNo++;
        if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        if (row.find_first_not_of(" \t") == std::string_view::npos) continue;
        Json j;
        std::string e;
        if (!ParseJson(row, j, e)) {
            err = "line " + std::to_string(lineNo) + ": " + e;
            return false;
        }
        const Json* docKey = j.Get("doc");
        const std::string docId = docKey && docKey->type == Json::Str ? docKey->s : std::string();
        const bool isLine = j.Get("line") || j.Get("src") || j.Get("toks") || j.Get("kir") || j.Get("kir2") ||
                            j.Get("case");
        // A document record starts a new document unless the current one has no lines yet.
        if (docs.empty() || (docKey && docs.back().id != docId) || (!isLine && !docs.back().lines.empty())) {
            docs.emplace_back();
            docs.back().id = docId;
        }
        Doc& doc = docs.back();
        if (!isLine) {
            if (!ReadDoc(j, doc, e)) {
                err = "line " + std::to_string(lineNo) + ": " + e;
                return false;
            }
            continue;
        }
        Line line;
        if (!ReadLine(j, doc, line, e)) {
            err = "line " + std::to_string(lineNo) + ": " + e;
            return false;
        }
        if (!j.Get("line") && !line.context) {  // ids default to the position among non-context lines
            uint32_t n = 1;
            for (const Line& l : doc.lines) n += !l.context;
            line.id = n;
        }
        doc.lines.push_back(std::move(line));
    }
    return true;
}

std::string NodeJson(const Doc& doc, const KNode& node) {
    std::string o;
    Writer w{doc, o};
    KNode copy = node;
    w.Object(copy);
    return o;
}

bool ReadNode(const Json& value, Doc& doc, KNode& node, std::string& err) {
    Reader r{doc, value, err};
    Fields(r, node);
    return err.empty();
}

std::string TokenJson(const Doc& doc, const Line&, const Token& tok) {
    std::string o;
    Writer w{doc, o};
    Token copy = tok;
    w.Object(copy);
    return o;
}

std::string ClauseJson(const Doc& doc, const Clause& clause) {
    std::string o;
    Writer w{doc, o};
    Clause copy = clause;
    w.Object(copy);
    return o;
}

std::string DecisionJson(const Doc& doc, const Decision& decision) {
    std::string o;
    Writer w{doc, o};
    Decision copy = decision;
    w.Object(copy);
    return o;
}

}  // namespace hd08
