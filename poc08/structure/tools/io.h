#pragma once
// toks.jsonl in, structure IR JSON out (the dev tool and the tests).
#include <ostream>
#include <string>

#include "../structure.h"
#include "json_min.h"

namespace hd8io {

using hd8json::Quote;
using hd8json::Value;

inline hd8::Pos PosOf(std::string_view s) {
    static const char* const names[] = {"n", "pn", "num", "suf", "pref", "v1", "v5", "vs", "vk", "vz", "adj", "na",
                                        "adv", "adn", "conj", "intj", "aux", "p_case", "p_topic", "p_conj", "p_final",
                                        "p_adv", "sym", "unk"};
    for (size_t i = 0; i < std::size(names); ++i)
        if (s == names[i]) return static_cast<hd8::Pos>(i);
    return hd8::Pos::unk;
}

inline bool ReadLine(const Value& v, hd8::Strings& st, hd8::Line& line) {
    line.doc = std::string(v.S("doc"));
    line.id = static_cast<uint32_t>(v.N("line"));
    line.src = std::string(v.S("src"));
    line.path = hd8::Path{};
    line.decisions.clear();
    const Value* toks = v.Get("toks");
    if (toks == nullptr || toks->type != Value::Arr) return false;
    for (const auto& t : toks->arr) {
        hd8::Token k;
        const Value* src = t.Get("src");
        if (src != nullptr && src->arr.size() == 2) {
            k.src.begin = static_cast<uint32_t>(src->arr[0].num);
            k.src.end = static_cast<uint32_t>(src->arr[1].num);
        }
        k.surf = st.Intern(t.S("surf"));
        k.lemma = st.Intern(t.S("lemma"));
        k.reading = st.Intern(t.S("reading"));
        k.pos = PosOf(t.S("pos"));
        k.ud = st.Intern(t.S("ud"));
        k.ctype = st.Intern(t.S("ctype"));
        k.cform = st.Intern(t.S("cform"));
        k.cost = static_cast<float>(t.N("cost"));
        k.margin = static_cast<float>(t.N("margin"));
        line.path.toks.push_back(k);
    }
    return true;
}

inline std::string Text(const hd8::Line& line, const hd8::Path& p, int first, int last) {
    uint32_t b = p.toks[first].src.begin, e = p.toks[last].src.end;
    if (e > line.src.size() || b > e) return {};
    return line.src.substr(b, e - b);
}

inline void WriteIr(const hd8::Line& line, const hd8::Strings& st, std::ostream& out) {
    const hd8::Path& p = line.path;
    out << "{\"doc\":" << Quote(line.doc) << ",\"line\":" << line.id << ",\"src\":" << Quote(line.src) << ",\"buns\":[";
    for (size_t i = 0; i < p.buns.size(); ++i) {
        const auto& b = p.buns[i];
        if (i) out << ',';
        out << "{\"first\":" << b.first << ",\"last\":" << b.last << ",\"content\":" << b.content
            << ",\"head\":" << b.head << ",\"headMargin\":" << b.headMargin << ",\"head2\":" << b.head2
            << ",\"type\":\"" << static_cast<char>(b.type) << "\",\"kase\":\"" << hd8::Name(b.kase) << "\",\"flags\":[";
        bool first = true;
        auto flag = [&](uint8_t bit, const char* name) {
            if (!(b.flags & bit)) return;
            out << (first ? "" : ",") << '"' << name << '"';
            first = false;
        };
        flag(hd8::Topic, "topic");
        flag(hd8::CommaAfter, "commaAfter");
        flag(hd8::QuoteClose, "quoteClose");
        out << "],\"clemma\":" << Quote(st.Get(p.toks[b.content].lemma)) << ",\"csurf\":"
            << Quote(st.Get(p.toks[b.content].surf)) << ",\"contentSrc\":[" << p.toks[b.content].src.begin << ',' << p.toks[b.content].src.end
            << "],\"src\":[" << p.toks[b.first].src.begin << ',' << p.toks[b.last].src.end
            << "],\"text\":" << Quote(Text(line, p, b.first, b.last)) << '}';
    }
    out << "],\"clauses\":[";
    for (size_t i = 0; i < p.clauses.size(); ++i) {
        const auto& c = p.clauses[i];
        if (i) out << ',';
        out << "{\"pred\":" << c.pred << ",\"firstBun\":" << c.firstBun << ",\"lastBun\":" << c.lastBun
            << ",\"parent\":" << c.parent << ",\"link\":\"" << hd8::Name(c.link) << "\",\"linkSub\":\""
            << hd8::Name(c.linkSub) << "\",\"tense\":\""
            << hd8::Name(c.tense) << "\",\"aspect\":\"" << hd8::Name(c.aspect) << "\",\"pol\":\"" << hd8::Name(c.pol)
            << "\",\"negFn\":\"" << hd8::Name(c.negFn) << "\",\"voice\":\"" << hd8::Name(c.voice) << "\",\"bene\":\""
            << hd8::Name(c.bene) << "\",\"modality\":[";
        auto mods = hd8::ModalityNames(c.modality);
        for (size_t k = 0; k < mods.size(); ++k) out << (k ? "," : "") << '"' << mods[k] << '"';
        out << "],\"force\":\"" << hd8::Name(c.force) << "\",\"polite\":\"" << hd8::Name(c.polite) << "\",\"jreg\":[";
        auto jr = hd8::JregNames(c.jreg);
        for (size_t k = 0; k < jr.size(); ++k) out << (k ? "," : "") << '"' << jr[k] << '"';
        out << "],\"args\":[";
        for (size_t k = 0; k < c.args.size(); ++k) {
            const auto& a = c.args[k];
            out << (k ? "," : "") << "{\"role\":\"" << hd8::Name(a.role) << "\",\"bun\":"
                << (a.bun == 0xFFFF ? -1 : static_cast<int>(a.bun)) << ",\"lemma\":" << Quote(st.Get(a.lemma))
                << ",\"cls\":" << Quote(st.Get(a.cls)) << ",\"how\":\"" << hd8::Name(static_cast<hd8::ArgHow>(a.how))
                << "\"}";
        }
        out << "],\"consumed\":[";
        for (size_t k = 0; k < c.consumed.size(); ++k) out << (k ? "," : "") << c.consumed[k];
        out << "],\"unfolded\":[";
        for (size_t k = 0; k < c.unfolded.size(); ++k) out << (k ? "," : "") << c.unfolded[k];
        const auto& pb = p.buns[c.pred];
        const auto& lb = p.buns[c.lastBun];
        out << "],\"predText\":" << Quote(Text(line, p, pb.content, lb.last)) << ",\"predSrc\":["
            << p.toks[pb.content].src.begin << ',' << p.toks[lb.last].src.end << "]}";
    }
    out << "],\"decisions\":[";
    static const char* const ev[] = {"lexical", "cued", "default", "unmatched-form", "fallback", "dropped",
                                     "name-guess", "user", "example"};
    for (size_t i = 0; i < line.decisions.size(); ++i) {
        const auto& d = line.decisions[i];
        out << (i ? "," : "") << "{\"stage\":\"" << static_cast<char>(d.stage) << "\",\"kind\":\""
            << ev[static_cast<int>(d.kind)] << "\",\"rule\":" << Quote(st.Get(d.rule)) << ",\"src\":[" << d.src.begin
            << ',' << d.src.end << "],\"margin\":" << d.margin << ",\"because\":[";
        for (size_t k = 0; k < d.because.size(); ++k) out << (k ? "," : "") << Quote(st.Get(d.because[k]));
        out << "]}";
    }
    out << "]}\n";
}

}  // namespace hd8io
