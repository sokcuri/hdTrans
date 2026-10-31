#include "bridge/attach.h"

#include <algorithm>

namespace hd08::bridge {
namespace {

// The item whose span holds token t, -1 when none does.
int ItemOfToken(const Line& sent, std::span<const Span> items, size_t t) {
    if (t >= sent.toks.size()) return -1;
    const Span s = sent.toks[t].src;
    for (size_t k = 0; k < items.size(); k++)
        if (Overlaps(items[k], s)) return static_cast<int>(k);
    return -1;
}

void Esc(std::string& o, std::string_view s) {
    o += '"';
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) < 0x20) {
            o += ' ';
            continue;
        }
        o += c;
    }
    o += '"';
}

}  // namespace

Attachment AttachItems(const Line& sent, std::span<const Span> itemSpans) {
    Attachment a;
    a.items.resize(itemSpans.size());
    const size_t nb = sent.buns.size();
    // bunsetsu → clause that owns it (the clause whose bunsetsu range holds it; inner clauses win)
    std::vector<int> bunClause(nb, -1);
    for (size_t c = 0; c < sent.clauses.size(); c++) {
        const Clause& cl = sent.clauses[c];
        for (size_t b = cl.firstBun; b <= cl.lastBun && b < nb; b++)
            if (bunClause[b] < 0 || sent.clauses[static_cast<size_t>(bunClause[b])].firstBun < cl.firstBun) bunClause[b] = static_cast<int>(c);
    }
    auto contentItem = [&](size_t b) { return b < nb ? ItemOfToken(sent, itemSpans, sent.buns[b].content) : -1; };
    for (size_t b = 0; b < nb; b++) {
        const int it = contentItem(b);
        if (it >= 0 && a.items[static_cast<size_t>(it)].clause < 0) a.items[static_cast<size_t>(it)].clause = bunClause[b];
    }
    // noun modification: a bunsetsu ending in の, or an attributive word or clause, and the noun it modifies
    for (size_t b = 0; b < nb; b++) {
        const Bunsetsu& bu = sent.buns[b];
        if (bu.head < 0 || static_cast<size_t>(bu.head) >= nb) continue;
        const bool no = bu.kase == Case::no;
        bool adn = false;
        for (const Clause& cl : sent.clauses)
            if (cl.pred == b && cl.link == link::adn) adn = true;
        if (!no && !adn && bu.type != buntype::A) continue;
        const int it = contentItem(b), head = contentItem(static_cast<size_t>(bu.head));
        if (it < 0 || head < 0 || it == head) continue;
        ItemAttach& ii = a.items[static_cast<size_t>(it)];
        if (ii.modifies >= 0) continue;
        ii.modifies = head;
        ii.modKind = no ? 1 : 2;
    }
    a.clauses.resize(sent.clauses.size());
    for (size_t c = 0; c < sent.clauses.size(); c++) {
        const Clause& cl = sent.clauses[c];
        ClauseAttach& ca = a.clauses[c];
        ca.clause = &cl;
        ca.parent = cl.parent;
        ca.predItem = contentItem(cl.pred);
        if (ca.predItem >= 0) {
            ItemAttach& p = a.items[static_cast<size_t>(ca.predItem)];
            p.pred = true;
            p.clause = static_cast<int>(c);
        }
        for (const Arg& arg : cl.args) {
            ItemArg ia;
            ia.role = static_cast<uint8_t>(arg.role);
            ia.how = arg.how;
            ia.kase = arg.bun < nb ? static_cast<uint8_t>(sent.buns[arg.bun].kase) : 0;
            ia.item = arg.how == how::omitted ? -1 : contentItem(arg.bun);
            ia.lemma = arg.lemma;
            ia.cls = arg.cls;
            ca.args.push_back(ia);
            // the head noun of an attributive clause is not its argument here (the noun carries mod on the clause's
            // side); an item takes its role in the clause it is an argument of
            if (ia.item < 0 || ia.how == how::relhead) continue;
            ItemAttach& ii = a.items[static_cast<size_t>(ia.item)];
            if (ii.isArg) continue;  // the first clause that takes it (the nearest)
            ii.isArg = true;
            ii.argOf = static_cast<int>(c);
            ii.headItem = ca.predItem;
            ii.role = ia.role;
            ii.how = ia.how;
            ii.kase = ia.kase;
        }
    }
    return a;
}

std::string AttachmentJson(const Doc& doc, const Line& sent, std::span<const Span> itemSpans, const Attachment& a) {
    std::string o = "{\"name\":";
    Esc(o, sent.name);
    o += ",\"src\":";
    Esc(o, sent.src);
    o += ",\"items\":[";
    for (size_t k = 0; k < a.items.size(); k++) {
        const ItemAttach& it = a.items[k];
        const Span s = k < itemSpans.size() ? itemSpans[k] : Span{};
        if (k) o += ',';
        o += "{\"i\":" + std::to_string(k) + ",\"text\":";
        Esc(o, s.end > s.begin && s.end <= sent.src.size() ? std::string_view(sent.src).substr(s.begin, s.end - s.begin)
                                                          : std::string_view());
        if (it.clause >= 0) o += ",\"clause\":" + std::to_string(it.clause);
        if (it.pred) o += ",\"pred\":true";
        if (it.modifies >= 0)
            o += ",\"mod\":{\"of\":" + std::to_string(it.modifies) + ",\"kind\":\"" + (it.modKind == 1 ? "no" : "adn") + "\"}";
        if (it.isArg) {
            o += ",\"arg\":{\"of\":" + std::to_string(it.argOf) + ",\"head\":" + std::to_string(it.headItem) + ",\"role\":";
            Esc(o, Name(F::role, it.role));
            o += ",\"how\":";
            Esc(o, Name(F::how, it.how));
            o += ",\"case\":";
            Esc(o, Name(F::kase_ja, it.kase));
            o += '}';
        }
        o += '}';
    }
    o += "],\"clauses\":[";
    for (size_t c = 0; c < a.clauses.size(); c++) {
        const ClauseAttach& ca = a.clauses[c];
        const Clause& cl = *ca.clause;
        if (c) o += ',';
        o += "{\"c\":" + std::to_string(c) + ",\"pred\":" + std::to_string(ca.predItem) + ",\"parent\":" + std::to_string(ca.parent);
        o += ",\"link\":";
        Esc(o, Name(F::link, cl.link));
        o += ",\"pol\":";
        Esc(o, Name(F::pol, cl.pol));
        o += ",\"voice\":";
        Esc(o, Name(F::voice, cl.voice));
        o += ",\"subj\":";
        Esc(o, Name(F::refkind, cl.subj.kind));
        o += ",\"args\":[";
        for (size_t k = 0; k < ca.args.size(); k++) {
            const ItemArg& ia = ca.args[k];
            if (k) o += ',';
            o += "{\"role\":";
            Esc(o, Name(F::role, ia.role));
            o += ",\"item\":" + std::to_string(ia.item) + ",\"lemma\":";
            Esc(o, doc.S(ia.lemma));
            if (ia.cls) {
                o += ",\"cls\":";
                Esc(o, doc.S(ia.cls));
            }
            o += ",\"how\":";
            Esc(o, Name(F::how, ia.how));
            o += '}';
        }
        o += "]}";
    }
    o += "]}";
    return o;
}

}  // namespace hd08::bridge
