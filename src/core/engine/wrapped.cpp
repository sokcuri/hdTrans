#include "core/engine/wrapped.h"

#include <algorithm>

#include "core/dict/classes.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"

namespace hdTrans::internal {

namespace {

constexpr std::string_view kBlanks[] = {" ", "\t", "\r", "　"};

std::string_view Trim(std::string_view s) {
    for (bool again = true; again;) {
        again = false;
        for (const std::string_view b : kBlanks) {
            if (s.starts_with(b)) {
                s.remove_prefix(b.size());
                again = true;
            }
            if (s.ends_with(b)) {
                s.remove_suffix(b.size());
                again = true;
            }
        }
    }
    return s;
}

char32_t FirstRune(std::string_view s) { return s.empty() ? 0 : text::DecodeRune(s).cp; }

char32_t LastRune(std::string_view s) {
    std::size_t at = s.size();
    while (at > 0 && (static_cast<unsigned char>(s[at - 1]) & 0xC0) == 0x80) at--;
    return at == 0 ? 0 : text::DecodeRune(s.substr(at - 1)).cp;
}

bool In(char32_t r, std::u32string_view set) { return r != 0 && set.find(r) != std::u32string_view::npos; }

constexpr std::u32string_view kCommas = U"、，,";
constexpr std::u32string_view kOpeners = U"「『（(【〈《［[〔｛{＜<“‘";
constexpr std::u32string_view kItemMarks = U"・●○◎■□◆◇▼▽▲△▶▷★☆※→⇒♪#＃-－*＊•‣";
constexpr std::u32string_view kNumberEnds = U".．)）、:：";
constexpr std::u32string_view kTrailingMarks = U",.!?、。，．！？…‥)）」』】〉》］";

// A speaker label before ： or : (太郎：, Ａ:): a short run without spaces or marks before it.
bool Labelled(std::string_view s) {
    std::size_t runes = 0;
    for (std::size_t at = 0; at < s.size() && runes <= 12; runes++) {
        const text::Rune r = text::DecodeRune(s.substr(at));
        if (r.cp == U'：' || r.cp == U':') return runes > 0;
        if (text::IsSpace(r.cp) || In(r.cp, U"、。！？!?「」『』…")) return false;
        at += r.size;
    }
    return false;
}

// Whether s starts an item of its own: a bracket, a list or heading mark, a number of a list
// (1. , ２）, ①) or a speaker label.
bool StartsItem(std::string_view s) {
    const char32_t r = FirstRune(s);
    if (In(r, kOpeners) || In(r, kItemMarks) || (r >= U'①' && r <= U'⑳')) return true;
    std::size_t at = 0;
    while (at < s.size() && text::IsDigit(text::DecodeRune(s.substr(at)).cp)) at += text::DecodeRune(s.substr(at)).size;
    if (at > 0 && In(FirstRune(s.substr(at)), kNumberEnds)) return true;
    return Labelled(s);
}

// The Korean of an ending without its form code and a bracketed alternative (2면(자) 면).
std::string_view KoreanEnding(std::string_view k) {
    if (!k.empty() && static_cast<unsigned char>(k.front()) < 0x80) k.remove_prefix(1);
    if (const std::size_t open = k.find('('); open != std::string_view::npos) k = k.substr(0, open);
    while (!k.empty() && k.back() == ' ') k.remove_suffix(1);
    return k;
}

bool Polite(std::string_view e) {
    for (const std::string_view p : {"ます", "ました", "ません", "です", "でした", "ましょう"}) {
        if (e.ends_with(p)) return true;
    }
    return false;
}

// Whether the line cannot end with word w, whatever follows.
bool OpenEnd(const WrapWord& w, const WrapEdges& edges) {
    const char c = w.code.empty() ? '\0' : w.code.front();
    if (w.detail == output::DetailClass::None) {
        if (c == '2') return edges.words > 1 && dict::InClass(HDTRANS_CLASS("wrap.open-particle"), w.surface);
        if (dict::InClass(HDTRANS_CLASS("wrap.open-word"), w.surface)) return true;
        // An adverb in に or と (一緒に, 随分と), not an interjection or a conjunction, after words of
        // its clause but no predicate, which an adverb said after its predicate would follow
        // (美味しいよ、本当に).
        if (c == '3' && w.code.size() > 1 && In(static_cast<char32_t>(w.code[1]), U"015") &&
            (w.surface.ends_with("に") || w.surface.ends_with("と"))) {
            return edges.words > 1 && !edges.predicate;
        }
        return c == '0';  // an attributive word (その, 大きな)
    }
    const std::string_view e = w.ending;
    for (const std::string_view s : {"ば", "たら", "だら", "ながら", "つつ", "ずに", "と", "とは"}) {
        if (e.ends_with(s)) return true;
    }
    if (w.detail == output::DetailClass::Dakeiyo && (e == "な" || e == "で")) return true;
    if (w.detail == output::DetailClass::Yougo && w.kind == 10 && text::RuneCount(e) == 1) return true;
    // The Korean the engine chose is a connective ending (읽고, 즐거워서, 하면서, 먹어도).
    const std::string_view k = KoreanEnding(w.koEnding);
    return LastRune(k) != 0 && In(LastRune(k), U"고서며면도러록");
}

// Whether next starts with a word that needs w before it: an auxiliary after a て form (読んで /
// いる), a noun or particle that needs a predicate (行かない / ことにした).
// te says that the line ends in a て form, by its last word or by its text (始めて read alone as an
// adverb).
bool BoundStart(const WrapWord& w, bool te, const WrapWord& n) {
    const std::string_view e = w.ending;
    if (te || e.ends_with("て") || e.ends_with("で")) {
        if (n.detail != output::DetailClass::None && dict::StartsWithMember(HDTRANS_CLASS("aux.te-start"), n.surface) > 0) {
            return true;
        }
        if (dict::InClass(HDTRANS_CLASS("wrap.te-word"), n.surface)) return true;
        // ても and ては broken before their particle (して / もいい).
        if (n.code.starts_with('2') && (n.surface == "も" || n.surface == "は")) return true;
    }
    if (w.detail == output::DetailClass::None || Polite(e)) return false;
    if (dict::StartsWithMember(HDTRANS_CLASS("aux.bound-pause"), n.surface) > 0) return true;
    if (n.surface == "ところで" || n.surface == "ところが") return false;
    for (const std::string_view b : dict::ClassMembers(HDTRANS_CLASS("wrap.bound-start"))) {
        if (!std::string_view(n.surface).starts_with(b)) continue;
        const std::string_view rest = std::string_view(n.surface).substr(b.size());
        if (rest.empty() || In(FirstRune(rest), U"にがをはもでのだかとなじ")) return true;
    }
    return false;
}

WrapWord WordOf(const output::ResolvedItem& it) {
    WrapWord w;
    w.surface = it.surface;
    w.code = !it.entry.code.empty() ? it.entry.code : it.first.code;
    w.detail = it.detailClass;
    w.kind = it.detailKind;
    w.ending = it.endingSurface;
    w.koEnding = it.ending.text;
    return w;
}

}  // namespace

void AddWrapEdges(const std::vector<output::ResolvedItem>& items, WrapEdges& edges) {
    for (const output::ResolvedItem& it : items) {
        const std::string_view code = !it.entry.code.empty() ? it.entry.code : it.first.code;
        if (it.surface.empty() || code.starts_with('6')) continue;
        if (!text::IsLetter(FirstRune(it.surface))) continue;
        if (!edges.first) edges.first = WordOf(it);
        if (edges.last && edges.last->detail != output::DetailClass::None) edges.predicate = true;
        edges.last = WordOf(it);
        edges.words++;
    }
}

bool GoesOn(std::string_view line, const WrapEdges& edges, std::string_view next, const WrapEdges& nextEdges) {
    line = Trim(line);
    next = Trim(next);
    if (line.empty() || next.empty() || line.find(kBreakMark) != std::string_view::npos ||
        next.find(kBreakMark) != std::string_view::npos || StartsItem(next)) {
        return false;
    }
    const char32_t end = LastRune(line);
    if (In(end, kCommas)) return true;
    if (!text::IsLetter(end) || !edges.last) return false;
    if (OpenEnd(*edges.last, edges)) return true;
    const bool te = line.ends_with("て") || line.ends_with("で");
    return nextEdges.first && BoundStart(*edges.last, te, *nextEdges.first);
}

std::vector<std::string> SplitAtBreaks(std::string_view translated, std::size_t count) {
    std::vector<std::string> out;
    std::size_t from = 0;
    for (std::size_t at = translated.find(kBreakMark); at != std::string_view::npos;
         at = translated.find(kBreakMark, from)) {
        out.emplace_back(translated.substr(from, at - from));
        from = at + kBreakMark.size();
    }
    out.emplace_back(translated.substr(from));
    if (out.size() != count) return {};
    for (std::size_t i = 0; i < out.size(); i++) {
        std::string& piece = out[i];
        if (i + 1 < out.size()) piece.erase(piece.find_last_not_of(' ') + 1);
        if (i > 0) piece.erase(0, std::min(piece.size(), piece.find_first_not_of(' ')));
        // Marks that close what the line before wrote go back to it (비쳐 있어 / , 곧바로).
        std::size_t marks = 0;
        while (marks < piece.size() && In(FirstRune(std::string_view(piece).substr(marks)), kTrailingMarks)) {
            marks += text::DecodeRune(std::string_view(piece).substr(marks)).size;
        }
        if (i > 0 && marks > 0 && marks < piece.size()) {
            out[i - 1] += piece.substr(0, marks);
            piece.erase(0, marks);
            piece.erase(0, std::min(piece.size(), piece.find_first_not_of(' ')));
        }
        if (piece.empty()) return {};
    }
    return out;
}

}  // namespace hdTrans::internal
