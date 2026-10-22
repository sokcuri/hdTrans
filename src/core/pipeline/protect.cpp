#include "core/pipeline/protect.h"

#include "core/pipeline/runes.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline {

namespace internal {

namespace {

// Stands for a span around a text piece in RewriteConjunctions; text pieces never hold
// it (the normalising rewrites map U+0000..U+0003 to spaces).
constexpr std::string_view kSpanChar = "\x01";

// The conjunctions with the words that replace them, tried in this order.
struct Conjunction {
    std::string_view word;
    std::string_view form;
};

constexpr Conjunction kConjunctions[] = {
    {"ならば", "それならば"},       {"なら", "それなら"},           {"ですから", "だから"},
    {"であれば", "それなら"},       {"にも関わらず", "それでも"},   {"にも拘らず", "それでも"},
    {"にもかかわらず", "それでも"}, {"対して", "反面"},
};

bool IsSentenceStartMark(char32_t c) { return RuneIn(c, U"\n「『（。！？!?"); }
bool IsLeadBlank(char32_t c) { return RuneIn(c, U" 　―…‥"); }

// A conjunction match at pos: an optional sentence-start mark (none only at position
// 0), blanks, the conjunction and one character other than a line feed.
struct ConjunctionMatch {
    size_t end = 0;
    size_t lead = 0;  // where the blanks start
    size_t word = 0;  // where the conjunction starts
    const Conjunction* conjunction = nullptr;
    size_t next = 0;  // where the character after it starts
};

bool MatchAfterMark(std::string_view s, size_t pos, ConjunctionMatch& m) {
    m.lead = pos;
    size_t p = pos + RunOf(s.substr(pos), IsLeadBlank);
    for (const Conjunction& c : kConjunctions) {
        if (!s.substr(p).starts_with(c.word)) continue;
        const size_t q = p + c.word.size();
        if (q >= s.size()) continue;
        const text::Rune r = text::DecodeRune(s.substr(q));
        if (r.cp == U'\n') continue;
        m.word = p;
        m.conjunction = &c;
        m.next = q;
        m.end = q + r.size;
        return true;
    }
    return false;
}

bool MatchConjunction(std::string_view s, size_t pos, ConjunctionMatch& m) {
    if (pos == 0 && MatchAfterMark(s, 0, m)) return true;
    if (pos >= s.size()) return false;
    const text::Rune r = text::DecodeRune(s.substr(pos));
    return IsSentenceStartMark(r.cp) && MatchAfterMark(s, pos + r.size, m);
}

// s with each conjunction match rewritten; the scan resumes after a match.
std::string RewriteConjunctionsIn(std::string_view s) {
    std::string out;
    size_t at = 0;
    for (size_t pos = 0; pos < s.size();) {
        ConjunctionMatch m;
        if (!MatchConjunction(s, pos, m)) {
            pos += text::DecodeRune(s.substr(pos)).size;
            continue;
        }
        out.append(s.substr(at, m.word - at));
        const std::string_view next = s.substr(m.next, m.end - m.next);
        const char32_t c = text::DecodeRune(next).cp;
        if (m.conjunction->word == "なら" && c >= 0x3041 && c <= 0x309F && !s.substr(m.next).starts_with("いい")) {
            out.append(m.conjunction->word);
        } else {
            out.append(m.conjunction->form);
        }
        out.append(next);
        at = pos = m.end;
    }
    out.append(s.substr(at));
    return out;
}

}  // namespace

output::Pieces RewriteConjunctions(const output::Pieces& text) {
    output::Pieces out = text;
    for (size_t k = 0; k < out.size(); k++) {
        if (out[k].span != output::SpanText) continue;
        const std::string_view before = k > 0 ? kSpanChar : "";
        const std::string_view after = k + 1 < out.size() ? kSpanChar : "";
        const std::string s = RewriteConjunctionsIn(std::string(before) + out[k].text + std::string(after));
        out[k].text = s.substr(before.size(), s.size() - before.size() - after.size());
    }
    return out;
}

std::string_view TextOf(const output::Piece& p) {
    if (p.span != output::SpanText) return {};
    return p.text;
}

}  // namespace internal

output::Pieces MarkCornerBrackets(const output::Pieces& typed) {
    output::Pieces out;
    for (const output::Piece& p : typed) {
        if (p.span != output::SpanText) {
            out.push_back(p);
            continue;
        }
        std::string s;
        for (std::string_view t = p.text; !t.empty();) {
            if (t.starts_with("｢")) {
                s += "「";
                t.remove_prefix(std::string_view("｢").size());
            } else if (t.starts_with("｣")) {
                s += "」";
                t.remove_prefix(std::string_view("｣").size());
            } else {
                s += t[0];
                t.remove_prefix(1);
            }
        }
        output::Add(out, s);
    }
    return out;
}

output::Pieces ProtectTyped(std::string_view sounded, const LineContext& ctx, const dict::Snapshot& snap) {
    output::Pieces typed = internal::CutSpans(sounded, ctx.markers);
    typed = internal::KataSpeech(typed, ctx, snap);
    typed = internal::KataHonorifics(typed, ctx, snap);
    typed = internal::KataWordsToHiragana(typed, ctx, snap);
    typed = internal::NicknamesToHangul(typed, ctx, snap);
    return internal::RewriteConjunctions(typed);
}

output::Pieces Protect(std::string_view sounded, const LineContext& ctx, const dict::Snapshot& snap) {
    return MarkCornerBrackets(ProtectTyped(sounded, ctx, snap));
}

}  // namespace hdTrans::pipeline
