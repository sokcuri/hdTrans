#include "lift.h"

#include <algorithm>

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

bool LoadLift(const std::string& dataRoot, LiftTable& out, std::string& err) {
    const std::string path = dataRoot + "/bridge/data/lift.tsv";
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
        const std::string key(col[1]), key2(col[2]);
        if (col[0] == "family") out.families[key] = {key2 == "-" ? std::string() : key2, node};
        else if (col[0] == "ending") out.endings[key + '\t' + key2] = node;
        else if (col[0] == "particle") out.particles[key] = node;
        else if (col[0] == "word" && key.size() == 1) out.words[key[0]] = node;
        else return fail("unknown kind " + std::string(col[0]));
    }
    return true;
}

bool Lift(const LiftTable& table, const hdTrans::hooks::Sentence& s, Doc& doc, Line& line, uint32_t& from,
          std::vector<LiftMiss>& misses) {
    bool whole = true;
    size_t composedAt = 0;  // where the search for the next item's stem in the composed text goes on
    const hdTrans::word::Item* chainItem = s.chain ? s.chain->head : nullptr;
    for (const hdTrans::output::ResolvedItem& it : s.items) {
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
        if (const size_t m = MarkerAt(target); m != std::string_view::npos) {
            const std::string marker(target.substr(m, 4));
            const auto f = table.families.find(marker);
            if (f != table.families.end() && Apply(f->second.second, doc, node)) {
                node.lemma = doc.I(std::string(target.substr(0, m)) + f->second.first);
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
                for (size_t k = 0; k < e->second.a.size(); k++) {
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
        node.id = static_cast<uint32_t>(line.kir.size() + 1);
        node.decision = decision;
        line.kir.push_back(node);
        for (KNode& x : extra) {
            x.id = static_cast<uint32_t>(line.kir.size() + 1);
            x.decision = decision;
            line.kir.push_back(x);
        }
    }
    return whole;
}

}  // namespace hd08::bridge
