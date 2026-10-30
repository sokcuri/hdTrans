#include "lift.h"

#include <algorithm>

#include "core/tokenizer/token.h"
#include "core/word/item.h"
#include "ir/file.h"
#include "ir/utf8.h"
#include "ir/ir_json.h"

namespace hd08::bridge {

namespace {

// The marker family in a 0.7.x target: [&%$] two letters or digits then '@'; npos for none.
size_t MarkerAt(std::string_view t) {
    for (size_t i = 0; i + 3 < t.size(); i++) {
        const char c = t[i];
        if (c != '&' && c != '%' && c != '$') continue;
        auto alnum = [](char x) { return (x >= 'A' && x <= 'Z') || (x >= 'a' && x <= 'z') || (x >= '0' && x <= '9'); };
        if (alnum(t[i + 1]) && alnum(t[i + 2]) && t[i + 3] == '@') return i;
    }
    return std::string_view::npos;
}

std::string FirstChar(std::string_view s) {
    size_t i = 0;
    if (!s.empty()) NextCodePoint(s, i);
    return std::string(s.substr(0, i));
}

// 0.7.x's reading of (ら)れ, as the voice decider's class names.
const char* VoiceName(hdTrans::word::Voice v) {
    using V = hdTrans::word::Voice;
    switch (v) {
    case V::Passive: return "PASSIVE";
    case V::Potential: return "POTENTIAL";
    case V::Respectful: return "HONORIFIC";
    case V::Spontaneous: return "SPONTANEOUS";
    case V::None: break;
    }
    return "NONE";
}

hd08::Ev EvidenceOf(hdTrans::word::Provenance p) {
    using P = hdTrans::word::Provenance;
    switch (p) {
    case P::LocalRule: case P::GlobalRule: case P::Sense: case P::Rewrite: case P::Scorer: case P::Cg:
        return Ev::Cued;
    case P::Single: case P::Fixed: return Ev::Lexical;
    default: return Ev::Default;
    }
}

// Applies the fields an object names to node (ReadNode leaves the others).
bool Apply(const Json& object, Doc& doc, KNode& node) {
    std::string err;
    return object.type == Json::Obj && ReadNode(object, doc, node, err);
}

}  // namespace

namespace {
bool LoadLiftFile(const std::string& path, LiftTable& out, std::string& err) {
    std::string text;
    if (!ReadWholeFile(path, text)) return true;
    size_t start = 0;
    int lineNo = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string_view row(text.data() + start, end - start);
        start = end + 1;
        lineNo++;
        if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        if (row.empty() || row[0] == '#') continue;
        std::vector<std::string_view> col;
        for (size_t at = 0;;) {
            const size_t tab = row.find('\t', at);
            col.push_back(row.substr(at, tab == std::string_view::npos ? std::string_view::npos : tab - at));
            if (tab == std::string_view::npos) break;
            at = tab + 1;
        }
        const auto fail = [&](const std::string& why) {
            err = path + ":" + std::to_string(lineNo) + ": " + why;
            return false;
        };
        if (col.size() < 4) return fail("expected kind, key, key2, node");
        Json node;
        std::string jsonErr;
        if (!ParseJson(col[3], node, jsonErr)) return fail(jsonErr);
        // Every node must read as IR (names, ranges), so that a bad row fails here, not on some line.
        {
            Doc scratch;
            KNode k;
            const auto check = [&](const Json& o) { return o.type == Json::Obj && ReadNode(o, scratch, k, jsonErr); };
            bool ok = node.type == Json::Arr ? !node.a.empty() : check(node);
            if (node.type == Json::Arr)
                for (const Json& o : node.a) ok = ok && check(o);
            if (!ok) return fail("not an IR node: " + jsonErr);
        }
        const std::string key(col[1]), key2(col[2]);
        if (col[0] == "family") out.families[key] = {key2 == "-" ? std::string() : key2, node};
        else if (col[0] == "ending") out.endings[key + '\t' + key2] = node;
        else if (col[0] == "particle") out.particles[key] = node;
        else if (col[0] == "word" && key.size() == 1) out.words[key[0]] = node;
        else if (col[0] == "attach") out.attach.insert(key);
        else return fail("unknown kind " + std::string(col[0]));
    }
    return true;
}
}  // namespace

bool LoadLift(const std::string& dataRoot, LiftTable& out, std::string& err) {
    for (const char* name : {"lift.tsv", "lift-endings.tsv"})
        if (!LoadLiftFile(dataRoot + "/bridge/data/" + name, out, err)) return false;
    // The marks the generator knows, by kind (its punct.tsv: kind, source mark, Korean mark, attach).
    std::string text;
    if (ReadWholeFile(dataRoot + "/generator/data/ko/punct.tsv", text)) {
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            const std::string_view row(text.data() + start, end - start);
            start = end + 1;
            if (row.empty() || row[0] == '#') continue;
            const size_t t1 = row.find('\t');
            if (t1 == std::string_view::npos) continue;
            const size_t t2 = row.find('\t', t1 + 1);
            const std::string_view kind = row.substr(0, t1), mark = row.substr(t1 + 1, t2 - t1 - 1);
            unsigned v = 0;
            if (mark != "-" && Parse(F::punct, kind, v)) out.marks.emplace(std::string(mark), static_cast<uint8_t>(v));
        }
    }
    return true;
}

bool Lift(const LiftTable& table, const hdTrans::hooks::Sentence& s, Doc& doc, Line& line, uint32_t& from,
          std::vector<LiftMiss>& misses) {
    bool whole = true;
    size_t composedAt = 0;  // where the search for the next item's stem in the composed text goes on

    // The tokens of the sentence that no item reads (marks, symbols, white space): 0.7.x writes them from
    // the token text, so the lift writes them as Punct nodes (marks the generator knows, runs of one mark
    // kept together) and Raw nodes (anything else), in source order between the items. A token that items
    // read is theirs (the word stage may have read it as a text other than its canonical form).
    int curTok = s.firstToken;
    bool spaced = false;
    bool glueNext = false;  // text the generator does not know (：, ＠) keeps the source's spacing after it
    std::vector<bool> read(s.tokens ? s.tokens->size() : 0, false);
    for (const hdTrans::output::ResolvedItem& it : s.items)
        if (it.tokenIndex >= 0 && static_cast<size_t>(it.tokenIndex) < read.size()) read[static_cast<size_t>(it.tokenIndex)] = true;
    const auto addNode = [&](KNode n) {
        n.id = static_cast<uint32_t>(line.kir.size() + 1);
        n.src = {from, from};
        line.kir.push_back(n);
    };
    const auto gap = [&](std::string_view text) {
        size_t i = 0;
        std::string raw;
        const auto flushRaw = [&] {
            if (raw.empty()) return;
            KNode n;
            n.kind = KK::Raw;
            n.lemma = doc.I(raw);
            n.glue = !spaced;
            addNode(n);
            raw.clear();
            spaced = false;
            glueNext = true;
        };
        while (i < text.size()) {
            const size_t at = i;
            const uint32_t cp = NextCodePoint(text, i);
            const std::string ch(text.substr(at, i - at));
            if (cp == ' ' || cp == 0x3000 || cp == '\t') {
                flushRaw();
                spaced = true;
                glueNext = false;
                continue;
            }
            const auto m = table.marks.find(ch);
            if (m == table.marks.end()) {
                raw += ch;
                continue;
            }
            flushRaw();
            size_t end = i;  // a run of the same mark is one node (……)
            while (end < text.size() && text.substr(end, ch.size()) == ch) end += ch.size();
            KNode n;
            n.kind = KK::Punct;
            n.punct = m->second;
            n.jaMark = doc.I(text.substr(at, end - at));
            n.glue = !spaced;
            addNode(n);
            spaced = false;
            glueNext = false;
            i = end;
        }
        flushRaw();
    };
    // The unread tokens before token tok (and tok itself when through).
    const auto gapsUntil = [&](int tok, bool through) {
        if (s.tokens == nullptr) return;
        for (; curTok < tok || (through && curTok == tok); curTok++) {
            if (curTok > s.lastToken || static_cast<size_t>(curTok) >= s.tokens->size()) break;
            // Marks, brackets and symbols only: words (Japanese runs, numbers, letters, spans) are always
            // read by items, also where an item's token index names the token its reading started in.
            using namespace hdTrans::tokenizer;
            const int type = s.tokens->TypeAfter(static_cast<size_t>(curTok));
            const bool mark = type == kTypeAsciiPunct || type == kTypeAsciiClose || type == kTypeAsciiOpen ||
                              type == kTypeAsciiSymbol || type == kTypeSentenceEnd || type == kTypeBracket ||
                              type == kTypeSymbol || type == kTypeAsciiSpace || type == kTypeWideSpace;
            if (read[static_cast<size_t>(curTok)] || !mark) continue;
            gap(s.tokens->Canonical(static_cast<size_t>(curTok)));
        }
    };
    // The last item that conjugates: a final ending belongs there only (anywhere else it is a reading the
    // lift cannot place).
    size_t lastPredicate = s.items.size();
    for (size_t k = 0; k < s.items.size(); k++)
        if (s.items[k].directIndex >= 0 && !s.items[k].ending.text.empty()) lastPredicate = k;
    size_t itemIndex = 0;
    const hdTrans::word::Item* chainItem = s.chain ? s.chain->head : nullptr;
    for (const hdTrans::output::ResolvedItem& it : s.items) {
        const bool lastConjugating = itemIndex++ == lastPredicate;
        gapsUntil(it.tokenIndex, false);
        spaced = false;
        const hdTrans::word::Item* ci = chainItem;
        if (chainItem) chainItem = chainItem->next;

        // The item's source text in the line, searched forward: its key, then the ending it read
        // (length counts both).
        // A text that 0.7.x changed (normalization, sound) is not found; a short one is only taken
        // close by, so that a miss does not push the later words past their places.
        Span src{from, from};
        if (!it.surface.empty()) {
            const size_t at = line.src.find(it.surface, from);
            const size_t reach = std::max<size_t>(24, 4 * it.surface.size());
            if (at != std::string::npos && at - from <= reach) {
                const size_t length = std::max(it.surface.size(), static_cast<size_t>(std::max(it.length, 0)));
                src = {static_cast<uint32_t>(at), static_cast<uint32_t>(std::min(at + length, line.src.size()))};
                from = src.end;
            }
        }
        Decision d;
        d.stage = 'T';
        d.src = src;
        if (ci != nullptr) {
            d.kind = EvidenceOf(ci->choice.layer);
            d.rule = doc.I("075:" + std::string(hdTrans::word::ProvenanceName(ci->choice.layer)) + ":" +
                           std::to_string(ci->choice.id));
        } else {
            d.kind = Ev::Default;
            d.rule = doc.I("075:item");
        }
        const std::string_view target = it.entry.text;
        // What 0.7.x decided here, for the deciders stacked on it (StageD reads these facts).
        if (ci != nullptr && ci->detail != nullptr && ci->detail->Yougo())
            d.because.push_back(doc.I(std::string("t075.voice=") + VoiceName(ci->detail->voice)));
        d.because.push_back(doc.I("t075.ko=" + std::string(target) + "|" + it.ending.text));
        // How 0.7.x wrote the word: its marker family, and the composed word that starts with its
        // stem (to the next space, at most 8 syllables after the stem).
        {
            const size_t m = MarkerAt(target);
            if (m != std::string_view::npos) d.because.push_back(doc.I("t075.marker=" + std::string(target.substr(m, 4))));
            std::string_view stem = target.substr(0, std::min(target.size(), m));
            while (!stem.empty() && stem.back() == ' ') stem.remove_suffix(1);
            if (!stem.empty()) {
                const size_t at = s.composed.find(stem, composedAt);
                if (at != std::string_view::npos) {
                    composedAt = at + stem.size();
                    const std::string_view rest = s.composed.substr(composedAt);
                    size_t end = 0;
                    for (int k = 0; k < 8 && end < rest.size(); k++) {
                        size_t next = end;
                        if (NextCodePoint(rest, next) == ' ' && k > 0) break;
                        end = next;
                    }
                    d.because.push_back(doc.I("t075.real=" + std::string(stem) + std::string(rest.substr(0, end))));
                }
            }
        }
        if (target.empty() && it.ending.text.empty()) {
            line.Decide(d);  // 0.7.x writes nothing for this word
            continue;
        }
        KNode node;
        node.src = src;
        bool lifted = true;
        const char digit = it.entry.code.empty() ? '1' : it.entry.code[0];
        const auto mk = table.marks.find(FirstChar(it.surface));
        if (mk != table.marks.end() && FirstChar(it.surface).size() == it.surface.size()) {
            node.kind = KK::Punct;
            node.punct = mk->second;
            node.jaMark = doc.I(it.surface);
        } else if (const size_t m = MarkerAt(target); m != std::string_view::npos) {
            const std::string marker(target.substr(m, 4));
            const auto f = table.families.find(marker);
            // A stem that still holds a particle alternation ((이)가 아니다) is a phrase, not one word.
            if (target.substr(0, m).find('(') != std::string_view::npos) {
                misses.push_back({"phrase", std::string(target)});
                lifted = false;
            } else if (f != table.families.end() && Apply(f->second.second, doc, node)) {
                node.lemma = doc.I(std::string(target.substr(0, m)) + f->second.first);
                // A target that is only the marker of a verbalizing family (attach rows: 하다, 되다…)
                // conjugates the noun before it (整理 + する: 정리하다), which 0.7.x writes without a space.
                if (m == 0 && table.attach.count(f->second.first)) node.glue = true;
            } else {
                misses.push_back({"family", marker});
                lifted = false;
            }
        } else if (digit == '2') {
            const auto p = table.particles.find(std::string(target));
            if (p != table.particles.end() && Apply(p->second, doc, node)) {
                if (node.kind == KK::P && node.kase == kase::LEX) node.lemma = doc.I(target);
            } else {
                misses.push_back({"particle", std::string(target)});
                lifted = false;
            }
        } else {
            const auto w = table.words.find(digit);
            if (w != table.words.end() && Apply(w->second, doc, node)) {
                node.lemma = doc.I(target);
            } else {
                misses.push_back({"word", std::string(1, digit)});
                lifted = false;
            }
        }
        std::vector<KNode> extra;
        if (lifted && it.directIndex >= 0 && !it.ending.text.empty()) {
            auto e = table.endings.find(it.endingSurface + '\t' + it.ending.text);
            if (e == table.endings.end()) e = table.endings.find("*\t" + it.ending.text);
            if (e == table.endings.end()) {
                misses.push_back({"ending", it.endingSurface + '\t' + it.ending.text});
                lifted = false;
            } else if (e->second.type == Json::Arr) {
                // The first object sets the word's own node (a verb or adjective); a noun takes the ending as
                // the nodes that follow (a copula and what it carries) and no features of its own.
                if (node.kind != KK::V && !e->second.a.empty() && !e->second.a[0].o.empty()) {
                    misses.push_back({"ending-host", it.endingSurface + '\t' + it.ending.text});
                    lifted = false;
                }
                bool final = false;
                for (const Json& o : e->second.a)
                    if (const Json* ct = o.Get("ctype"); ct && ct->type == Json::Str && ct->s == "final") final = true;
                if (lifted && final && !lastConjugating) {
                    misses.push_back({"ending-final-mid", it.endingSurface + '\t' + it.ending.text});
                    lifted = false;
                }
                // A row decomposed on a noun host (a copula carries the ending) read on a verb: the copula's
                // features are the verb's own.
                size_t skip = 0;
                if (node.kind == KK::V && e->second.a.size() >= 2 && e->second.a[0].o.empty()) {
                    const Json& cop = e->second.a[1];
                    const Json* glue = cop.Get("glue");
                    if (glue && glue->type == Json::Bool && glue->b) {
                        KNode features;
                        Apply(cop, doc, features);
                        node.tense = features.tense;
                        node.aspect = features.aspect;
                        node.mood = features.mood;
                        node.ctype = features.ctype;
                        node.rel = features.rel;
                        node.neg = features.neg;
                        node.modal = features.modal;
                        node.eomi = features.eomi;
                        skip = 2;
                    }
                }
                for (size_t k = skip; lifted && k < e->second.a.size(); k++) {
                    if (k == 0) {
                        Apply(e->second.a[0], doc, node);
                        continue;
                    }
                    KNode x;
                    x.src = src;
                    Apply(e->second.a[k], doc, x);
                    extra.push_back(x);
                }
            } else {
                Apply(e->second, doc, node);
            }
        }
        if (!lifted) {
            whole = false;
            node = KNode{};
            node.kind = KK::Raw;
            node.src = src;
            // The carried text without 0.7.x's marker families.
            std::string carried(target);
            for (size_t m; (m = MarkerAt(carried)) != std::string::npos;) carried.erase(m, 4);
            node.lemma = doc.I(carried);
            d.kind = Ev::Fallback;
            d.because.push_back(doc.I(d.rule ? std::string(doc.S(d.rule)) : std::string()));
            d.rule = doc.I("code:lift");
        }
        const uint32_t decision = line.Decide(d);
        if (glueNext) node.glue = true;
        glueNext = false;
        node.id = static_cast<uint32_t>(line.kir.size() + 1);
        node.decision = decision;
        line.kir.push_back(node);
        for (KNode& x : extra) {
            x.id = static_cast<uint32_t>(line.kir.size() + 1);
            x.decision = decision;
            line.kir.push_back(x);
        }
    }
    gapsUntil(s.lastToken, true);
    return whole;
}

}  // namespace hd08::bridge
