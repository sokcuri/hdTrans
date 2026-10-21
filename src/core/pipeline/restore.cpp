#include "core/pipeline/restore.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/builtin.h"
#include "core/dict/tsv.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"

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
            out.push_back(rs[j] - (rs[j] - text::kBase) % text::kFinalsCount);
        } else {
            const size_t share = (marks * word + marks + rest - 1) / (marks + rest);
            out += rs.substr(j, std::max<size_t>(1, std::min(share, word - 1)));
        }
        out += sep;
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

RestoreSteps RestoreAll(const Pieces& cleaned, const Pieces& typed, bool keepBrackets, bool literalCleanup) {
    RestoreSteps st;
    st.spaced = DropAddedSpaces(typed, cleaned);
    st.bracketed = keepBrackets ? RestoreBrackets(typed, st.spaced) : st.spaced;
    st.korean = MapText(st.bracketed, [literalCleanup](std::string_view s) {
        const std::string korean = KoreanForms(s);
        return literalCleanup ? KoreanCleanup(korean) : korean;
    });
    st.line = DropParticleMarks(WriteStutters(WriteDropped(output::String(st.korean))));
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
