#include "core/pipeline/restore.h"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/builtin.h"
#include "core/dict/classes.h"
#include "core/dict/tsv.h"
#include "core/pipeline/runes.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/tokenizer/canonical.h"

namespace hdTrans::pipeline {

using output::Piece;
using output::Pieces;
using output::SpanText;

namespace {

// The runes of s, an invalid byte as U+FFFD.
std::u32string Runes(std::string_view s) {
    std::u32string rs;
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        rs.push_back(r.cp);
        i += r.size;
    }
    return rs;
}

std::string Utf8(std::u32string_view rs) {
    std::string s;
    for (const char32_t r : rs) text::AppendRune(s, r);
    return s;
}

bool Contains(std::u32string_view set, char32_t r) { return set.find(r) != std::u32string_view::npos; }

std::string_view TextOf(const Piece& p) { return p.span == SpanText ? std::string_view(p.text) : std::string_view(); }

template <class F>
Pieces MapText(const Pieces& p, F f) {
    Pieces out;
    for (const auto& x : p) {
        if (x.span == SpanText) {
            output::Add(out, f(x.text));
        } else {
            out.push_back(x);
        }
    }
    return out;
}

constexpr std::u32string_view kOpenings = U"「『（(“‘【〔《〈［｛";

// Whether text holds an opening bracket followed by an ASCII space.
bool HasOpeningSpace(std::string_view text) {
    const std::u32string rs = Runes(text);
    for (size_t i = 0; i + 1 < rs.size(); i++) {
        if (Contains(kOpenings, rs[i]) && rs[i + 1] == U' ') return true;
    }
    return false;
}

// text without the ASCII spaces after its opening brackets.
std::string DropOpeningSpaces(std::string_view text) {
    std::string out;
    bool opening = false;
    for (size_t i = 0; i < text.size();) {
        const auto r = text::DecodeRune(text.substr(i));
        if (!(opening && text[i] == ' ')) {
            out.append(text.substr(i, r.size));
            opening = Contains(kOpenings, r.cp);
        }
        i += r.size;
    }
    return out;
}

// The closing brackets, as typed and in the forms the engine writes for them. A ” or ’ closes
// only a quote opened before it (“ or ” before ”, ‘ or ’ before ’), a > or ＞ only after a < or ＜.
constexpr std::u32string_view kClosings = U"」』）)】〕］]〉》｝}”’>＞";

// The closing brackets of text (kClosings), each as the index of its rune in Runes(text).
std::vector<size_t> Closings(const std::u32string& rs) {
    std::vector<size_t> out;
    bool quoted = false, singleQuoted = false;
    int angle = 0;
    for (size_t i = 0; i < rs.size(); i++) {
        const char32_t r = rs[i];
        if (r == U'“') quoted = true;
        if (r == U'‘') singleQuoted = true;
        if (r == U'<' || r == U'＜') angle++;
        if (!Contains(kClosings, r)) continue;
        if (r == U'”' || r == U'’') {
            bool& open = r == U'”' ? quoted : singleQuoted;
            open = !open;
            if (open) continue;
        }
        if (r == U'>' || r == U'＞') {
            if (angle == 0) continue;
            angle--;
        }
        out.push_back(i);
    }
    return out;
}

// Whether text holds white space before a closing bracket.
bool HasClosingSpace(std::string_view text) {
    const std::u32string rs = Runes(text);
    for (const size_t i : Closings(rs)) {
        if (i > 0 && (rs[i - 1] == U' ' || rs[i - 1] == U'　' || rs[i - 1] == U'\t')) return true;
    }
    return false;
}

// text without the ASCII spaces before its closing brackets.
std::string DropClosingSpaces(std::string_view text) {
    const std::u32string rs = Runes(text);
    const std::vector<size_t> closings = Closings(rs);
    if (closings.empty()) return std::string(text);
    std::u32string out;
    size_t from = 0;
    for (const size_t i : closings) {
        size_t start = i;
        while (start > from && rs[start - 1] == U' ') start--;
        out.append(rs, from, start - from);
        from = i;
    }
    out.append(rs, from);
    return Utf8(out);
}

// The forms the engine writes for the brackets it does not write as typed.
char32_t BracketForm(char32_t r) {
    switch (r) {
        case U'『': return U'「';
        case U'』': return U'」';
        case U'｢': return U'「';
        case U'｣': return U'」';
        case U'（': return U'(';
        case U'）': return U')';
        case U'’': return U'＇';
        case U'〝': return U'˝';
        case U'〟': return U'〃';
    }
    return 0;
}

// The typed brackets written in Korean usage rather than as typed.
char32_t KoreanQuote(char32_t r) {
    switch (r) {
        case U'〝': return U'“';
        case U'〟': return U'”';
    }
    return r;
}

Pieces RestoreLineBrackets(const Pieces& in, const Pieces& out) {
    std::map<char32_t, std::u32string> typed;  // form -> the brackets of in written in it
    bool retyped = false;
    for (const auto& p : in) {
        for (const char32_t r : Runes(TextOf(p))) {
            if (const char32_t form = BracketForm(r); form != 0) {
                typed[form].push_back(KoreanQuote(r));
                retyped = true;
            } else if (Contains(U"「」()＇˝〃", r)) {
                typed[r].push_back(r);
            }
        }
    }
    if (!retyped) return out;
    std::map<char32_t, size_t> written;
    for (const auto& p : out) {
        for (const char32_t r : Runes(TextOf(p))) {
            if (typed.count(r) != 0) written[r]++;
        }
    }
    std::map<char32_t, size_t> next;
    return MapText(out, [&](const std::string& s) {
        std::u32string rs = Runes(s);
        for (char32_t& r : rs) {
            const auto it = typed.find(r);
            if (it != typed.end() && !it->second.empty() && written[r] == it->second.size()) {
                const char32_t form = r;
                r = it->second[next[form]++];
            }
        }
        return Utf8(rs);
    });
}

bool IsSyllable(char32_t r) { return r >= text::kBase && r <= text::kLast; }

// The length in runes of a stutter separator at rs[i]: 、 ， or a run of … ‥ ― ─ — or of
// two or more ・.
size_t StutterSeparator(std::u32string_view rs, size_t i) {
    if (i >= rs.size()) return 0;
    switch (rs[i]) {
        case U'、':
        case U'，':
            return 1;
        case U'…':
        case U'‥':
        case U'―':
        case U'─':
        case U'—':
        case U'・': {
            size_t n = i;
            while (n < rs.size() && rs[n] == rs[i]) n++;
            if (rs[i] == U'・' && n - i < 2) return 0;
            return n - i;
        }
    }
    return 0;
}

bool IsDropped(char32_t r) { return r >= kDropShift + 0x3041 && r <= kDropShift + 0x30FF; }

constexpr std::u32string_view kSmallVowels = U"ァィゥェォ";
constexpr std::u32string_view kPlainVowels = U"アイウエオ";
constexpr int kMaxLengthening = 4;

// A small vowel kana as the plain one (ァ -> ア), a plain one as itself, others as 0.
char32_t PlainVowel(char32_t r) {
    if (const size_t i = kSmallVowels.find(r); i != std::u32string_view::npos) return kPlainVowels[i];
    if (Contains(kPlainVowels, r)) return r;
    return 0;
}

// The vowel row of a katakana, -1 for none.
int VowelOf(char32_t r) {
    static constexpr std::u32string_view kRows[5] = {
        U"アァカガサザタダナハバパマヤャラワ", U"イィキギシジチヂニヒビピミリ", U"ウゥクグスズツヅヌフブプムユュル",
        U"エェケゲセゼテデネヘベペメレ", U"オォコゴソゾトドノホボポモヨョロヲ",
    };
    for (int v = 0; v < 5; v++) {
        if (Contains(kRows[v], r)) return v;
    }
    return -1;
}

// The final consonant of the Hangul syllable text ends in, or -1.
int LastFinal(std::string_view text) {
    std::size_t start = text.size();
    while (start > 0 && (static_cast<unsigned char>(text[start - 1]) & 0xC0) == 0x80) start--;
    if (start == 0) return -1;
    const auto syllable = text::Split(text::DecodeRune(text.substr(start - 1)).cp);
    return syllable ? syllable->final : -1;
}

// Whether text ends in a Hangul syllable with a final consonant.
bool AfterFinalConsonant(std::string_view text) {
    const int final = LastFinal(text);
    return final > text::kFinalNone;
}

// Whether text ends in an attributive form and one space: a syllable with the final ㄴ
// or ㄹ (갈 때, 갔을 때, 하는 때).
bool AfterAttributive(std::string_view text) {
    if (!text.ends_with(' ')) return false;
    const int final = LastFinal(text.substr(0, text.size() - 1));
    return final == text::kFinalNieun || final == text::kFinalRieul;
}

// Where the pause of a drawl goes in the syllables w that end a polite predicate: before
// 습니다 습니까 읍시다 (with 습 or 읍), else before 니다 니까 시다; before 지요 까요 네요 군요;
// else before the last syllable; at the end of a one-syllable word.
size_t DrawlPoint(std::u32string_view w) {
    const size_t n = w.size();
    size_t tail = 1;
    if (w.ends_with(U"니다") || w.ends_with(U"니까") || w.ends_with(U"시다")) {
        tail = n >= 3 && (w[n - 3] == U'습' || w[n - 3] == U'읍') ? 3 : 2;
    } else if (w.ends_with(U"지요") || w.ends_with(U"까요") || w.ends_with(U"네요") || w.ends_with(U"군요")) {
        tail = 2;
    }
    return tail < n ? n - tail : n;
}

// DrawlPoint moved before the copula or 하다 the ending starts with: 입 or 합 before 니다
// 니까 (최저……입니다); with past, also 이었, 였 or 했 before 습니다 습니까 (학교……였습니다).
size_t LeadDrawlPoint(std::u32string_view w, bool past) {
    const size_t p = DrawlPoint(w);
    if (p == w.size()) return p;
    if (w[p] == U'니' && (w[p - 1] == U'입' || w[p - 1] == U'합')) return p - 1;
    if (!past || w[p] != U'습') return p;
    if (p >= 2 && w[p - 1] == U'었' && w[p - 2] == U'이') return p - 2;
    if (w[p - 1] == U'였' || w[p - 1] == U'했') return p - 1;
    return p;
}

bool IsDrawlMark(char32_t r) { return r == kDrawlMark || r == kLeadDrawlMark || r == kPastLeadDrawlMark; }

// Whether r ends a word: white space, punctuation or a closing bracket.
bool EndsWord(char32_t r) {
    return r == U' ' || r == U'\t' || r == U'\n' || Contains(U"、。，．！？!?,.:;…‥・―─—～ー」』）)］]】〕〉》\"'", r);
}

}  // namespace

Pieces DropAddedSpaces(const Pieces& typed, const Pieces& out) {
    const std::vector<Pieces> inLines = output::Lines(typed);
    std::vector<Pieces> outLines = output::Lines(out);
    if (inLines.size() != outLines.size()) return out;
    for (size_t i = 0; i < outLines.size(); i++) {
        Pieces& line = outLines[i];
        if (!output::Head(inLines[i]).starts_with(' ')) output::TrimLeft(line);
        bool opening = false;
        for (const auto& p : inLines[i]) opening = opening || HasOpeningSpace(TextOf(p));
        if (!opening) line = MapText(line, DropOpeningSpaces);
        bool closing = false;
        for (const auto& p : inLines[i]) closing = closing || HasClosingSpace(TextOf(p));
        if (!closing) line = MapText(line, DropClosingSpaces);
    }
    return output::JoinLines(outLines);
}

Pieces RestoreBrackets(const Pieces& typed, const Pieces& out) {
    const std::vector<Pieces> inLines = output::Lines(typed);
    std::vector<Pieces> outLines = output::Lines(out);
    if (inLines.size() != outLines.size()) return RestoreLineBrackets(typed, out);
    for (size_t i = 0; i < outLines.size(); i++) outLines[i] = RestoreLineBrackets(inLines[i], outLines[i]);
    return output::JoinLines(outLines);
}

std::string KoreanForms(std::string_view s) {
    std::string out;
    bool afterWord = false;  // the previous rune neither whitespace nor an opening bracket
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        switch (r.cp) {
            case U'　': out += ' '; break;
            case U'（': out += '('; break;
            case U'）': out += ')'; break;
            case U'［': out += '['; break;
            case U'］': out += ']'; break;
            case U'｛': out += '{'; break;
            case U'｝': out += '}'; break;
            case U'＜': out += '<'; break;
            case U'＞': out += '>'; break;
            case U'“':
                if (afterWord) out += ' ';
                out += "“";
                break;
            default: out.append(s.substr(i, r.size)); break;
        }
        // The class test sees the ASCII forms written above.
        const unsigned char last = static_cast<unsigned char>(out.back());
        const char32_t written = last < 0x80 ? last : r.cp;
        afterWord = !Contains(U"\t\n\f\r 「『(“‘【〔《〈[{<", written);
        i += r.size;
    }
    return out;
}

std::string KoreanCleanup(std::string_view s) {
    // after: "final" (a syllable with a final consonant before) or "attributive"
    // (AfterAttributive).
    struct Row {
        std::string from, to, after;
    };
    static const auto rows = [] {
        std::vector<Row> parsed;
        const std::string table = dict::ExtraBuiltinText("cleanup.tsv");
        dict::TsvReader reader(table, "//");
        while (reader.Next()) {
            const auto& f = reader.Fields();
            if (f.size() < 2 || f[0].empty()) continue;
            parsed.push_back({std::string(f[0]), std::string(f[1]), f.size() >= 3 ? std::string(f[2]) : std::string()});
        }
        return parsed;
    }();
    std::string out;
    out.reserve(s.size());
    while (!s.empty()) {
        bool matched = false;
        for (const auto& [from, to, after] : rows) {
            if (!s.starts_with(from) || (after == "final" && !AfterFinalConsonant(out)) ||
                (after == "attributive" && !AfterAttributive(out))) {
                continue;
            }
            out += to;
            s.remove_prefix(from.size());
            matched = true;
            break;
        }
        if (!matched) {
            out += s.front();
            s.remove_prefix(1);
        }
    }
    return out;
}

std::string WriteDropped(std::string_view s) {
    const std::u32string rs = Runes(s);
    bool any = false;
    for (const char32_t r : rs) any = any || IsDropped(r);
    if (!any) return std::string(s);
    std::string out;
    for (size_t i = 0; i < rs.size();) {
        if (!IsDropped(rs[i])) {
            text::AppendRune(out, rs[i]);
            i++;
            continue;
        }
        std::u32string kana;
        for (; i < rs.size() && IsDropped(rs[i]); i++) kana.push_back(rs[i] - kDropShift);
        out += internal::ReadKana(Utf8(kana));
    }
    return out;
}

std::string WriteStutters(std::string_view s) {
    const std::u32string rs = Runes(s);
    if (!Contains(rs, kStutterMark) && !Contains(rs, kStutterRest)) return std::string(s);
    std::u32string out;
    for (size_t i = 0; i < rs.size(); i++) {
        if (rs[i] == kStutterRest) continue;  // only after a mark, where the run below takes it
        if (rs[i] != kStutterMark) {
            out.push_back(rs[i]);
            continue;
        }
        size_t trimmed = out.size();
        while (trimmed > 0 && out[trimmed - 1] == U' ') trimmed--;
        if (trimmed == 0 || Contains(U"「『（(“‘〈《【[\n", out[trimmed - 1])) out.resize(trimmed);
        size_t marks = 1, rest = 0;
        while (i + marks < rs.size() && rs[i + marks] == kStutterMark) marks++;
        while (i + marks + rest < rs.size() && rs[i + marks + rest] == kStutterRest) rest++;
        i += marks + rest - 1;
        const size_t k = StutterSeparator(rs, i + 1);
        std::u32string sep = rs.substr(i + 1, k);
        if (sep == U"、" || sep == U"，") sep = U", ";
        i += k;
        size_t j = i + 1;
        while (j < rs.size() && (rs[j] == U' ' || rs[j] == kStutterMark || rs[j] == kStutterRest ||
                                 StutterSeparator(rs, j) > 0)) {
            j++;
        }
        size_t word = 0;
        while (j + word < rs.size() && IsSyllable(rs[j + word])) word++;
        if (word == 0) continue;
        if (rest == 0) {
            // Marks without separators between them stand for a syllable said again each.
            for (size_t m = 0; m < marks; m++) out.push_back(rs[j] - (rs[j] - text::kBase) % text::kFinalsCount);
        } else {
            const size_t share = (marks * word + marks + rest - 1) / (marks + rest);
            out += rs.substr(j, std::max<size_t>(1, std::min(share, word - 1)));
        }
        out += sep;
    }
    return Utf8(out);
}

std::string WriteDrawls(std::string_view s) {
    const std::u32string rs = Runes(s);
    if (std::none_of(rs.begin(), rs.end(), IsDrawlMark)) return std::string(s);
    std::u32string out;
    for (size_t i = 0; i < rs.size(); i++) {
        if (!IsDrawlMark(rs[i])) {
            out.push_back(rs[i]);
            continue;
        }
        const char32_t mark = rs[i];
        size_t close = i + 1;
        while (close < rs.size() && Contains(U"…‥・―─", rs[close])) close++;
        if (close == i + 1 || close == rs.size() || rs[close] != mark) {
            out.push_back(mark);
            continue;
        }
        std::u32string marks;
        for (size_t k = i + 1; k < close; k++) {
            const auto form = tokenizer::CanonicalForm(rs[k]);
            if (form) {
                marks += Runes(*form);
            } else {
                marks.push_back(rs[k]);
            }
        }
        i = close;
        size_t spaces = 0;  // spaced from the word as if a word followed the span
        while (spaces < out.size() && out[out.size() - 1 - spaces] == U' ') spaces++;
        const size_t end = out.size() - spaces;
        size_t start = end;
        while (start > 0 && IsSyllable(out[start - 1])) start--;
        if (start == end) continue;
        if (close + 1 == rs.size() || EndsWord(rs[close + 1])) out.resize(end);
        const std::u32string_view word = std::u32string_view(out).substr(start, end - start);
        out.insert(start + (mark == kDrawlMark ? DrawlPoint(word) : LeadDrawlPoint(word, mark == kPastLeadDrawlMark)),
                   marks);
    }
    return Utf8(out);
}

namespace {

constexpr char32_t kPrivateFirst = 0xE000;
constexpr char32_t kPrivateLast = 0xF8FF;
constexpr char32_t kPrivateEscape = 0xF0000;

template <class F>
std::string MapRunes(std::string_view s, F f) {
    std::string out;
    out.reserve(s.size());
    for (std::string_view rest = s; !rest.empty();) {
        const text::Rune r = text::DecodeRune(rest);
        if (r.cp == text::kReplacement && r.size == 1) {
            out += rest.front();
        } else {
            text::AppendRune(out, f(r.cp));
        }
        rest.remove_prefix(r.size);
    }
    return out;
}

}  // namespace

std::string EscapePrivateUse(std::string_view s) {
    return MapRunes(s, [](char32_t r) {
        return r >= kPrivateFirst && r <= kPrivateLast ? r - kPrivateFirst + kPrivateEscape : r;
    });
}

std::string UnescapePrivateUse(std::string_view s) {
    return MapRunes(s, [](char32_t r) {
        return r >= kPrivateEscape && r <= kPrivateEscape + (kPrivateLast - kPrivateFirst) ? r - kPrivateEscape + kPrivateFirst
                                                                                             : r;
    });
}

std::string WriteEmphasis(std::string_view s) {
    const std::u32string rs = Runes(s);
    if (!Contains(rs, kEmphasisOpen) && !Contains(rs, kEmphasisClose)) return std::string(s);
    std::u32string out;
    bool inside = false, joined = false;  // joined: the last character written is a syllable of the word
    for (const char32_t r : rs) {
        if (r == kEmphasisOpen || r == kEmphasisClose) {
            inside = r == kEmphasisOpen;
            joined = false;
            continue;
        }
        if (inside && IsSyllable(r) && joined) {
            while (out.back() == U' ') out.pop_back();
            out += U'·';
        }
        out.push_back(r);
        if (r != U' ') joined = inside && IsSyllable(r);
    }
    return Utf8(out);
}

namespace {

// The vowel that draws out a vowel (ㅑ ㅏ, ㅙ ㅔ, ㅝ ㅓ), by jamo index.
constexpr int kLongVowel[21] = {0, 1, 0, 1, 4, 5, 4, 5, 8, 0, 5, 5, 8, 13, 4, 5, 20, 13, 18, 20, 20};

// Whether a syllable with the final f is drawn out by WriteStress: none or one that can be held
// (ㄴ ㄹ ㅁ ㅇ).
bool StressFinal(int f) { return f == 0 || f == 4 || f == 8 || f == 16 || f == 21; }

}  // namespace

std::string WriteStress(std::string_view s) {
    const std::u32string rs = Runes(s);
    if (!Contains(rs, kStressMark)) return std::string(s);
    std::u32string out;
    for (size_t i = 0; i < rs.size(); i++) {
        if (rs[i] != kStressMark) {
            out.push_back(rs[i]);
            continue;
        }
        const auto first = i + 1 < rs.size() ? text::Split(rs[i + 1]) : std::nullopt;
        // The drawn syllables are vowels; without a final, a syllable that starts with ㅇ after them
        // would run into them (매우).
        const auto second = i + 2 < rs.size() ? text::Split(rs[i + 2]) : std::nullopt;
        if (!first || !StressFinal(first->final) || (first->final == 0 && second && second->initial == 11)) continue;
        const int vowel = kLongVowel[first->vowel];
        out.push_back(text::Join(first->initial, first->vowel, 0));
        out.push_back(text::Join(11, vowel, 0));
        out.push_back(text::Join(11, vowel, first->final));
        i++;
    }
    return Utf8(out);
}

std::string WriteLengthening(std::string_view s) {
    const std::u32string rs = Runes(s);
    if (!Contains(rs, kLengthenMark) && !Contains(rs, kDrawnMark) && !Contains(rs, output::kClipMark) &&
        !Contains(rs, output::kCryMark) && !Contains(rs, output::kClippedCryMark)) {
        return std::string(s);
    }
    std::u32string out;
    int lengthenings = 0;  // the lengthenings right before r
    for (const char32_t r : rs) {
        if (const auto last = out.size() >= 2 && out.back() == U'~' ? text::Split(out[out.size() - 2]) : std::nullopt;
            r == kDrawnMark && last) {
            // After a lengthening the drawn syllable goes after the ~ when the syllable before has a final
            // consonant (짱~ 짜~앙, 군~ 구~운), and right after it without the ~ when it has none (씨~ 씨이).
            out.pop_back();
            out.back() = text::Join(last->initial, last->vowel, 0);
            if (last->final != 0) out.push_back(U'~');
            out.push_back(text::Join(11, kLongVowel[last->vowel], last->final));
        } else if (r == kDrawnMark) {
            if (out.empty() || out.back() < 0xAC00 || out.back() > 0xD7A3) continue;
            const int syllable = static_cast<int>(out.back() - 0xAC00), final = syllable % 28;
            out.back() -= static_cast<char32_t>(final);
            out.push_back(static_cast<char32_t>(0xAC00 + (11 * 21 + kLongVowel[syllable % 588 / 28]) * 28 + final));
        } else if (r == output::kClipMark) {
            if (!out.empty() && text::Final(out.back()) == 0) out.back() += static_cast<char32_t>(text::kFinalSiot);
        } else if (output::IsCryMark(r)) {
            continue;
        } else if (r != kLengthenMark) {
            out.push_back(r);
        } else if (out.empty() || (out.back() != U'~' && out.back() != U'～')) {
            out.push_back(U'~');
        } else if (lengthenings == 1) {
            out.push_back(U'~');  // two lengthenings in a row (くぅぅん 군~~)
        }
        lengthenings = r == kLengthenMark ? lengthenings + 1 : 0;
    }
    return Utf8(out);
}

std::string DropParticleMarks(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::string_view rest = s; !rest.empty();) {
        const text::Rune r = text::DecodeRune(rest);
        if (r.cp != output::kVowelMark && r.cp != output::kConsonantMark && r.cp != output::kRieulMark) {
            out += rest.substr(0, r.size);
        }
        rest.remove_prefix(r.size);
    }
    return out;
}

// line without a gloss in parentheses that only repeats the word before it, as a kana gloss
// of a reading or a name reads once translated (저(저) 저, 미카(미카) 미카).
std::string DropEchoedGlosses(std::string_view line) {
    std::u32string rs = Runes(line);
    auto hangul = [](char32_t r) { return r >= U'가' && r <= U'힣'; };
    for (std::size_t i = 1; i < rs.size(); i++) {
        if (rs[i] != U'(' || !hangul(rs[i - 1])) continue;
        std::size_t j = i + 1;
        while (j < rs.size() && hangul(rs[j])) j++;
        if (j == i + 1 || j >= rs.size() || rs[j] != U')') continue;
        const std::size_t n = j - i - 1;
        std::size_t word = i;  // the start of the Hangul run before (
        while (word > 0 && hangul(rs[word - 1])) word--;
        if (i - word < n || rs.compare(i - n, n, rs, i + 1, n) != 0) continue;
        rs.erase(i, n + 2);
    }
    return Utf8(rs);
}

// line with the literal 무슨 소리를 하다 of a retort (何言ってるの, 何を言ってるんですか; the
// dictionary reads it so only for the て form of 言う) written as the idiom 무슨 소리야 or
// 무슨 소리예요, where it ends a clause: before a mark, a closing bracket or the end.
std::string RetortIdioms(std::string_view line) {
    struct Idiom {
        std::string_view literal, idiom;
    };
    static constexpr Idiom kIdioms[] = {
        {"무슨 소리를 하고 있는 거야", "무슨 소리야"}, {"무슨 소리를 하는 거야", "무슨 소리야"},
        {"무슨 소리를 하는거야", "무슨 소리야"},       {"무슨 소리를 하고 있어", "무슨 소리야"},
        {"무슨 소리를 하는 건데", "무슨 소리야"},      {"무슨 소리를 해", "무슨 소리야"},
        {"무슨 소리를 하고 있습니까", "무슨 소리예요"}, {"무슨 소리를 하는 겁니까", "무슨 소리예요"},
        {"무슨 소리를 하는 거예요", "무슨 소리예요"},  {"무슨 소리를 하고 있어요", "무슨 소리예요"},
        {"무슨 소리를 합니까", "무슨 소리예요"},       {"무슨 소리를 해요", "무슨 소리예요"},
        {"무슨 소리를 하고 있습니다", "무슨 소리예요"},
    };
    std::string out(line);
    for (std::size_t at = 0; (at = out.find("무슨 소리를 ", at)) != std::string::npos;) {
        bool done = false;
        for (const Idiom& i : kIdioms) {
            if (out.compare(at, i.literal.size(), i.literal) != 0) continue;
            const std::size_t end = at + i.literal.size();
            const char32_t next = end < out.size() ? text::DecodeRune(std::string_view(out).substr(end)).cp : 0;
            if (next != 0 && !Contains(U"?!.,…‥~～―」』)\"'", next)) continue;
            out.replace(at, i.literal.size(), i.idiom);
            at += i.idiom.size();
            done = true;
            break;
        }
        if (!done) at += std::string_view("무슨 소리를 ").size();
    }
    return out;
}

namespace {

// The runes of s with their byte offsets.
struct RuneAt {
    char32_t cp;
    std::size_t at;
};
std::vector<RuneAt> RunesAt(std::string_view s) {
    std::vector<RuneAt> rs;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        rs.push_back({r.cp, i});
        i += r.size == 0 ? 1 : r.size;
    }
    return rs;
}

// A run of laughing letters in rs: [start, end).
struct LaughRun {
    std::size_t start, end;
};

// The maximal runs of letters of laughs in rs.
std::vector<LaughRun> LaughRuns(const std::vector<RuneAt>& rs, std::u32string_view laughs) {
    std::vector<LaughRun> runs;
    for (std::size_t i = 0; i < rs.size();) {
        if (!Contains(laughs, rs[i].cp)) {
            i++;
            continue;
        }
        std::size_t end = i;
        while (end < rs.size() && Contains(laughs, rs[end].cp)) end++;
        runs.push_back({i, end});
        i = end;
    }
    return runs;
}

// Fullwidth forms (Ａ ｗ ０).
bool Wide(char32_t r) { return r >= 0xFF01 && r <= 0xFF5E; }

// A letter or digit of a Latin, Greek or Cyrillic word, of either width.
bool LatinRune(char32_t r) { return (r < 0x2000 || Wide(r)) && (text::IsLetter(r) || text::IsDigit(r)); }

// The marks of addresses, handles and code that join a letter next to them into one token.
constexpr std::u32string_view kAddressMarks = U"/@_:=#&／＠＿＃";

// What a run of laughing letters writes.
enum class Laugh : std::uint8_t { None, Run, Bracketed };

// What each run of src writes, read from the typed text around it.
std::vector<Laugh> TypedLaughs(const std::vector<RuneAt>& src, const std::vector<LaughRun>& runs) {
    const auto size = static_cast<std::ptrdiff_t>(src.size());
    auto at = [&](std::ptrdiff_t i) -> char32_t { return i < 0 || i >= size ? 0 : src[static_cast<std::size_t>(i)].cp; };
    auto space = [](char32_t r) { return r == U' ' || r == U'　'; };
    auto dot = [](char32_t r) { return r == U'.' || r == U'．'; };
    auto particle = [](char32_t r) {
        std::string one;
        text::AppendRune(one, r);
        return dict::InClass("particle.after-letter", one);
    };
    // Whether the rune at i, on the side step of a run of width wide, joins the run into a word
    // or an address: a Latin letter or digit of the same width, a mark of kAddressMarks, or dots
    // with such a letter beyond them (www.example.com, No...way).
    auto touches = [&](std::ptrdiff_t i, std::ptrdiff_t step, bool wide) {
        const char32_t r = at(i);
        if (LatinRune(r)) return Wide(r) == wide;
        if (dot(r)) {
            while (dot(at(i))) i += step;
            return LatinRune(at(i));
        }
        return Contains(kAddressMarks, r);
    };
    std::vector<Laugh> laughs;
    for (const LaughRun& run : runs) {
        const auto s = static_cast<std::ptrdiff_t>(run.start);
        const auto e = static_cast<std::ptrdiff_t>(run.end);
        const char32_t p = at(s - 1);
        const char32_t n = at(e);
        std::ptrdiff_t q = s - 1;
        while (q >= 0 && space(at(q))) q--;
        const bool opens = q < 0;
        const bool single = e - s == 1;
        auto noun = [](char32_t r) { return internal::IsKanjiRune(r) || internal::IsKatakanaRune(r); };
        Laugh laugh = Laugh::Run;
        if (touches(s - 1, -1, Wide(at(s))) || touches(e, 1, Wide(at(s))) || (q < s - 1 && LatinRune(at(q)))) {
            laugh = Laugh::None;
        } else if ((p == U'(' || p == U'（') && (n == U')' || n == U'）')) {
            std::ptrdiff_t b = s - 2;
            while (b >= 0 && space(at(b))) b--;
            const char32_t a = at(e + 1);
            const bool closes = a == 0 || space(a) ||
                                !(internal::IsJapaneseRune(a) || LatinRune(a) || IsSyllable(a) || Contains(U"、，,", a));
            laugh = !LatinRune(at(b)) && closes ? Laugh::Bracketed : Laugh::None;
        } else if ((opens && (internal::IsHiraganaRune(n) || (single && noun(n)))) || (particle(p) && particle(n)) ||
                   (single && (p == 0 || space(p) || noun(p) || particle(p)) && (particle(n) || (particle(p) && noun(n))))) {
            laugh = Laugh::None;
        }
        laughs.push_back(laugh);
    }
    return laughs;
}

// What each run of rs writes, read from the output alone: a run that no Latin letter, digit or
// address mark touches laughs after a Hangul syllable, the Korean laughter or a mark of before,
// past spaces, or at the start of the line unless a Hangul syllable follows it (w를 입력).
std::vector<Laugh> OutputLaughs(const std::vector<RuneAt>& rs, const std::vector<LaughRun>& runs,
                                std::u32string_view before, char32_t korean) {
    auto touches = [](char32_t r) { return LatinRune(r) || r == U'.' || Contains(kAddressMarks, r); };
    std::vector<Laugh> laughs;
    for (const LaughRun& run : runs) {
        std::size_t prev = run.start;
        while (prev > 0 && rs[prev - 1].cp == U' ') prev--;
        const bool opens = prev == 0;
        const char32_t p = opens ? 0 : rs[prev - 1].cp;
        const char32_t n = run.end < rs.size() ? rs[run.end].cp : 0;
        const bool touched = (run.start > 0 && touches(rs[run.start - 1].cp)) || touches(n);
        const bool after = opens || IsSyllable(p) || p == korean || Contains(before, p);
        laughs.push_back(!touched && after && !(opens && IsSyllable(n)) ? Laugh::Run : Laugh::None);
    }
    return laughs;
}

}  // namespace

std::string WriteLaughter(std::string_view line, std::string_view source) {
    bool any = false;
    for (const std::string_view m : dict::ClassMembers("mark.laughter")) any = any || line.find(m) != std::string_view::npos;
    if (!any) return std::string(line);
    std::u32string laughs, before;
    for (const std::string_view m : dict::ClassMembers("mark.laughter")) laughs += text::DecodeRune(m).cp;
    for (const std::string_view m : dict::ClassMembers("mark.before-laughter")) before += text::DecodeRune(m).cp;
    const std::string_view korean = dict::ClassMembers("ko.laughter").front();
    const char32_t koreanRune = text::DecodeRune(korean).cp;
    const std::string bracketed = "(" + std::string(dict::ClassMembers("ko.bracketed-laughter").front()) + ")";
    const std::vector<RuneAt> rs = RunesAt(line);
    const std::vector<LaughRun> runs = LaughRuns(rs, laughs);
    const std::vector<RuneAt> src = RunesAt(source);
    const std::vector<LaughRun> typed = LaughRuns(src, laughs);
    bool aligned = runs.size() == typed.size();
    for (std::size_t k = 0; aligned && k < runs.size(); k++)
        aligned = runs[k].end - runs[k].start == typed[k].end - typed[k].start;
    std::vector<Laugh> kinds = aligned ? TypedLaughs(src, typed) : OutputLaughs(rs, runs, before, koreanRune);
    // A run that the translation put against a Latin letter its typed run does not touch stays
    // (くぁwせdrftgy 크아wdrftgy).
    auto letter = [](const std::vector<RuneAt>& v, std::size_t i, bool left) {
        if (left && i == 0) return false;
        const std::size_t at = left ? i - 1 : i;
        return at < v.size() && LatinRune(v[at].cp) && !text::IsDigit(v[at].cp);
    };
    for (std::size_t k = 0; aligned && k < runs.size(); k++) {
        if ((letter(rs, runs[k].start, true) && !letter(src, typed[k].start, true)) ||
            (letter(rs, runs[k].end, false) && !letter(src, typed[k].end, false)))
            kinds[k] = Laugh::None;
    }
    auto byteAt = [&](std::size_t i) { return i < rs.size() ? rs[i].at : line.size(); };
    std::string out;
    std::size_t copied = 0;  // runes of line written to out
    bool changed = false;
    for (std::size_t k = 0; k < runs.size(); k++) {
        if (kinds[k] == Laugh::None) continue;
        std::size_t start = runs[k].start;
        std::size_t end = runs[k].end;
        if (kinds[k] == Laugh::Bracketed && start > 0 && end < rs.size() && rs[start - 1].cp == U'(' &&
            rs[end].cp == U')') {
            out.append(line, byteAt(copied), byteAt(start - 1) - byteAt(copied));
            out += bracketed;
            copied = end + 1;
            changed = true;
            continue;
        }
        std::size_t count = end - start;
        // Right after a word written as Korean laughter, past spaces, the run joins it (草ｗ ㅋㅋ ㅋ).
        std::size_t laughEnd = start;
        while (laughEnd > copied && rs[laughEnd - 1].cp == U' ') laughEnd--;
        std::size_t laughStart = laughEnd;
        while (laughStart > copied && rs[laughStart - 1].cp == koreanRune) laughStart--;
        if (laughStart < laughEnd) {
            count = std::max(count, laughEnd - laughStart);
            start = laughStart;
        }
        if (aligned) {
            const LaughRun& t = typed[k];
            // Bracketed laughter right after the run joins it (ｗｗ（笑） ㅋㅋ).
            if (t.end < src.size() && (src[t.end].cp == U'(' || src[t.end].cp == U'（')) {
                std::size_t j = end;
                while (j < rs.size() && rs[j].cp == U' ') j++;
                if (line.substr(byteAt(j)).starts_with(bracketed)) {
                    const std::size_t stop = byteAt(j) + bracketed.size();
                    while (j < rs.size() && rs[j].at < stop) j++;
                    end = j;
                }
            }
        }
        out.append(line, byteAt(copied), byteAt(start) - byteAt(copied));
        for (std::size_t n = 0; n < count; n++) out += korean;
        copied = end;
        changed = true;
    }
    if (!changed) return std::string(line);
    out.append(line, byteAt(copied), std::string_view::npos);
    return out;
}

RestoreSteps RestoreAll(const Pieces& cleaned, const Pieces& typed, bool keepBrackets, bool literalCleanup) {
    RestoreSteps st;
    st.spaced = DropAddedSpaces(typed, cleaned);
    st.bracketed = keepBrackets ? RestoreBrackets(typed, st.spaced) : st.spaced;
    st.korean = MapText(st.bracketed, [literalCleanup](std::string_view s) {
        const std::string korean = KoreanForms(s);
        return literalCleanup ? KoreanCleanup(korean) : korean;
    });
    st.line = UnescapePrivateUse(WriteLaughter(
        DropEchoedGlosses(DropParticleMarks(
            WriteLengthening(WriteEmphasis(WriteDrawls(WriteStutters(WriteDropped(WriteStress(output::String(st.korean))))))))),
        output::String(typed)));
    if (literalCleanup) st.line = RetortIdioms(st.line);
    return st;
}

std::string Restore(const Pieces& cleaned, const Pieces& typed, bool keepBrackets) {
    return RestoreAll(cleaned, typed, keepBrackets).line;
}

namespace internal {

std::string ReadKana(std::string_view kana) {
    std::u32string k = Runes(kana);
    for (char32_t& r : k) {
        if (r >= U'ぁ' && r <= U'ゖ') r += 0x60;
    }
    std::u32string rs;
    int vowel = -1, added = 0;  // vowel row of the last syllable; syllables it gained
    for (char32_t r : k) {
        if (const char32_t p = PlainVowel(r); r == U'ー' || (p != 0 && r != p)) {
            if (vowel >= 0 && (r == U'ー' || VowelOf(p) == vowel)) {
                if (added < kMaxLengthening) {
                    rs.push_back(kPlainVowels[static_cast<size_t>(vowel)]);
                    added++;
                }
                continue;
            } else if (r != U'ー' && (vowel < 0 || Contains(U"アイエオ", rs.back()))) {
                r = p;
            } else if (vowel >= 0) {
                rs.push_back(r);
                vowel = VowelOf(p);
                continue;
            } else {
                continue;
            }
        }
        if (const size_t i = std::u32string_view(U"ャュョ").find(r); i != std::u32string_view::npos && vowel < 0) {
            r = U"ヤユヨ"[i];
        }
        rs.push_back(r);
        added = 0;
        if (r == U'ッ' || r == U'ン') {
            vowel = -1;
        } else {
            vowel = r == U'ヴ' ? 2 : VowelOf(r);
        }
    }
    size_t lead = 0;
    while (lead < rs.size() && rs[lead] == U'ン') lead++;
    if (rs.find_first_not_of(U'ッ', lead) == std::u32string::npos) rs.resize(lead);
    std::string rest;
    if (lead < rs.size()) {
        rest = text::FromKana(Utf8(rs.substr(lead)), text::Preset::Fan);
        if (rest.empty()) {
            if (!TransliterateKatakana(Utf8(k), rest) || rest.empty()) return std::string(kana);
            return rest;
        }
    }
    std::string out;
    for (size_t i = 0; i < lead; i++) {
        int final = text::kFinalIeung;
        if (i == lead - 1 && rest.empty() && lead < k.size() && k.back() == U'ッ') {
            final = 19;  // ㅅ: んっ 읏
        } else if (i == lead - 1 && !rest.empty()) {
            switch (text::Split(text::DecodeRune(rest).cp)->initial) {
                case text::kInitialGiyeok:
                case text::kInitialKieuk:
                case text::kInitialHieuh:
                case 11:  // ㅇ
                    break;
                case text::kInitialMieum:
                case text::kInitialBieup:
                case 17:  // ㅍ
                    final = text::kFinalMieum;
                    break;
                default:
                    final = text::kFinalNieun;
                    break;
            }
        }
        text::AppendRune(out, text::Join(11, 18, final));
    }
    return out + rest;
}

}  // namespace internal

}  // namespace hdTrans::pipeline
