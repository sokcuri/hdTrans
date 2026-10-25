#include "core/pipeline/segment.h"

#include "core/dict/classes.h"
#include "core/pipeline/runes.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline {

namespace internal {

namespace {

constexpr int kChunkBefore = 40;
constexpr int kChunkAfter = 10;

// Endings that end a clause only after one of the given characters.
struct ConjunctiveAfter {
    std::string_view ending;
    std::u32string_view before;
};

constexpr ConjunctiveAfter kConjunctiveAfter[] = {
    {"で", U"んい"},        // 読んで, 泳いで, ないで (not the particle で)
    {"し", U"いるだすうく"},  // 行くし, いいし, だし
    {"が", U"たいるうすだん"},  // 思ったが, ですが, ませんが
};

bool IsSentenceEnd(char32_t r) { return RuneIn(r, U"。．！？!?.\n\r"); }

// A space, an ideographic space or a tab.
bool IsBlank(char32_t r) { return r == U' ' || r == U'　' || r == U'\t'; }

std::string_view TrimLeftRunes(std::string_view s, std::u32string_view set) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (!RuneIn(r.cp, set)) break;
        s.remove_prefix(r.size);
    }
    return s;
}

// The characters other than white space of text from piece k, byte from, before the
// first sentence end; a span counts as one.
int CharsBeforeEnd(const output::Pieces& text, size_t k, size_t from) {
    int n = 0;
    for (; k < text.size(); k++, from = 0) {
        if (text[k].span != output::SpanText) {
            n++;
            continue;
        }
        for (std::string_view s = std::string_view(text[k].text).substr(from); !s.empty();) {
            const text::Rune r = text::DecodeRune(s);
            if (IsSentenceEnd(r.cp)) return n;
            if (!text::IsSpace(r.cp)) n++;
            s.remove_prefix(r.size);
        }
    }
    return n;
}

}  // namespace

bool KanaSpace(std::string_view s, size_t i) {
    // The run of spaces is read only after a hiragana, so that a long run costs one pass.
    const char32_t last = LastRune(s.substr(0, i)).cp;
    if (!IsHiraganaRune(last)) return false;
    std::string_view rest = s.substr(i);
    while (!rest.empty() && rest[0] == ' ') rest.remove_prefix(1);
    const char32_t first = text::DecodeRune(rest).cp;
    if (!IsJapaneseRune(first)) return false;
    return RuneIn(last, U"ねよさわぞぜ") ||
           (IsKanjiRune(first) && !RuneIn(last, U"いただるうくすつぬむぶぐなのはがをにでともへやか"));
}

bool Conjunctive(std::string_view s) {
    for (const std::string_view e : dict::ClassMembers(HDTRANS_CLASS("ending.conjunctive"))) {
        if (s.ends_with(e)) return true;
    }
    for (const ConjunctiveAfter& p : kConjunctiveAfter) {
        if (s.ends_with(p.ending) && RuneIn(LastRune(s.substr(0, s.size() - p.ending.size())).cp, p.before)) return true;
    }
    return false;
}

size_t ColumnGap(std::string_view s, size_t i, bool lineBefore, bool lineAfter) {
    // The checks on the characters around i come before the run is read, so that only the
    // first blank of a run reads it and a long run costs one pass.
    if (i >= s.size() || !IsBlank(text::DecodeRune(s.substr(i)).cp)) return i;
    auto lineEnd = [](char32_t r) { return r == U'\n' || r == U'\r'; };
    const text::Rune last = LastRune(s.substr(0, i));
    if (i == 0 ? !lineBefore : (IsBlank(last.cp) || lineEnd(last.cp))) return i;
    const size_t blanks = RunOf(s.substr(i), IsBlank);
    const std::string_view run = s.substr(i, blanks);
    if (run.find('\t') == std::string_view::npos && LastRune(run).size == blanks) return i;
    const size_t end = i + blanks;
    const text::Rune next = text::DecodeRune(s.substr(end));
    if (end == s.size() ? !lineAfter : lineEnd(next.cp)) return i;
    // A single kanji on each side, each standing alone: a two-character name padded to the
    // width of three (林  涛).
    const auto alone = [&](size_t at, bool more) {
        if (at == 0 || at == s.size()) return !more;
        const char32_t r = at < i ? LastRune(s.substr(0, at)).cp : text::DecodeRune(s.substr(at)).cp;
        return IsBlank(r) || lineEnd(r);
    };
    if (IsKanjiRune(last.cp) && IsKanjiRune(next.cp) && alone(i - last.size, lineBefore) &&
        alone(end + next.size, lineAfter)) {
        return i;
    }
    return end;
}

size_t EscapeGap(std::string_view s, size_t i, bool lineBefore, bool lineAfter) {
    // Only the first blank of a run reads the run, so that a long run costs one pass.
    if (i >= s.size() || (s[i] != '\\' && !IsBlank(text::DecodeRune(s.substr(i)).cp))) return i;
    const text::Rune last = LastRune(s.substr(0, i));
    if (i == 0 ? !lineBefore : (last.cp == U'\n' || last.cp == U'\r')) return i;
    if (s[i] != '\\' && IsBlank(last.cp)) return i;
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    const auto escape = [&](size_t at) {
        return at + 1 < s.size() && s[at] == '\\' && (s[at + 1] == 'n' || s[at + 1] == 'r' || s[at + 1] == 't') &&
               (at + 2 == s.size() || !lower(s[at + 2]));
    };
    size_t at = i + RunOf(s.substr(i), IsBlank);
    // An escape, not a backslash in Latin text (C:\temp, \\n) or before a Latin word (\notag).
    const char before = at > 0 ? s[at - 1] : ' ';
    if (lower(before) || (before >= 'A' && before <= 'Z') || (before >= '0' && before <= '9') || before == '\\') return i;
    if (!escape(at)) return i;
    // A run of escapes (\r\n) with the blanks between them.
    while (escape(at)) at += 2 + RunOf(s.substr(at + 2), IsBlank);
    if (at == s.size() ? !lineAfter : (s[at] == '\n' || s[at] == '\r')) return i;
    return at;
}

bool BetweenWords(std::string_view s, size_t i) {
    auto word = [](char32_t r) { return r != text::kReplacement && !IsBlank(r) && r != U'\n' && r != U'\r'; };
    const text::Rune last = LastRune(s.substr(0, i));
    if (!word(last.cp)) return false;  // before the run is read: one pass per run
    const char32_t before = LastRune(s.substr(0, i - last.size)).cp;
    const std::string_view rest = TrimLeftRunes(s.substr(i), U" 　\t");
    const text::Rune first = text::DecodeRune(rest);
    const char32_t next = text::DecodeRune(rest.substr(first.size)).cp;
    return word(last.cp) && word(first.cp) && (word(before) || word(next));
}

}  // namespace internal

std::vector<Chunk> Segment(const output::Pieces& pieces, std::vector<std::string>* gaps) {
    using namespace internal;
    std::vector<Chunk> parts;
    std::vector<std::string> gapsBefore;
    Chunk part;
    std::string gap;  // the column separator before part
    int count = 0;
    for (size_t k = 0; k < pieces.size(); k++) {
        const output::Piece& p = pieces[k];
        if (p.span != output::SpanText) {
            part.push_back(p);
            continue;
        }
        const std::string_view t = p.text;
        size_t start = 0;
        for (size_t i = 0; i < t.size();) {
            const text::Rune r = text::DecodeRune(t.substr(i));
            size_t gapEnd = ColumnGap(t, i, k > 0, k + 1 < pieces.size());
            if (gapEnd == i) gapEnd = EscapeGap(t, i, k > 0, k + 1 < pieces.size());
            if (gapEnd != i) {
                output::Add(part, t.substr(start, i - start));
                if (!part.empty()) {
                    parts.push_back(std::move(part));
                    gapsBefore.push_back(std::move(gap));
                    part.clear();
                    gap.clear();
                }
                gap += t.substr(i, gapEnd - i);
                start = i = gapEnd;
                count = 0;
                continue;
            }
            if ((r.cp == U'　' || (r.cp == U' ' && KanaSpace(t, i))) && i > start && BetweenWords(t, i)) {
                output::Add(part, t.substr(start, i - start));
                parts.push_back(std::move(part));
                gapsBefore.push_back(std::move(gap));
                part.clear();
                gap.clear();
                start = i;
                count = 0;
            } else if (IsSentenceEnd(r.cp)) {
                count = 0;
            } else if (r.cp == U'、') {
                const size_t end = i + r.size;
                if (count >= kChunkBefore && Conjunctive(t.substr(0, i)) && CharsBeforeEnd(pieces, k, end) >= kChunkAfter) {
                    output::Add(part, t.substr(start, end - start));
                    parts.push_back(std::move(part));
                    gapsBefore.push_back(std::move(gap));
                    part.clear();
                    gap.clear();
                    start = end;
                    count = 0;
                }
            } else if (IsJapaneseRune(r.cp)) {
                count++;
            }
            i += r.size;
        }
        output::Add(part, t.substr(start));
    }
    parts.push_back(std::move(part));
    gapsBefore.push_back(std::move(gap));
    for (size_t i = 1; i < parts.size(); i++) {
        Chunk& c = parts[i];
        if (c.empty() || c[0].span != output::SpanText) continue;
        const std::string_view rest = TrimLeftRunes(c[0].text, U" 　\t");
        if (!rest.empty()) {
            c[0].text = std::string(rest);
        } else {
            c.erase(c.begin());
        }
    }
    if (gaps != nullptr) *gaps = std::move(gapsBefore);
    return parts;
}

}  // namespace hdTrans::pipeline
