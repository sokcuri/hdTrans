#include "core/output/cleanup.h"

#include <cstring>

#include "core/dict/classes.h"
#include "core/output/particle.h"
#include "core/text/hangul.h"
#include "core/text/utf8.h"

namespace hdTrans::output {

namespace {

bool IsSyllable(char32_t r) { return text::Final(r) >= 0; }

// The first character of s, 0 for an empty string.
char32_t FirstRune(std::string_view s) { return s.empty() ? 0 : text::DecodeRune(s).cp; }

// s[i], or 0 outside s.
char At(std::string_view s, size_t i) { return i < s.size() ? s[i] : 0; }

// The last character of s; U+FFFD for an empty string.
char32_t LastRune(std::string_view s) { return DecodeLastRune(TextBefore(s), s.size()).cp; }

// Copies n bytes from s[from] to s[to], to <= from.
void Move(std::string& s, size_t to, size_t from, size_t n) {
    if (to != from) std::memmove(s.data() + to, s.data() + from, n);
}

// Writes form at s[to]; form is not part of s.
void Put(std::string& s, size_t to, std::string_view form) {
    if (!form.empty()) std::memcpy(s.data() + to, form.data(), form.size());
}

// Keeps pieces[k], a text piece rewritten in place, as the next of the first w pieces:
// dropped when empty, joined to a text piece before it.
void Keep(Pieces& pieces, size_t& w, size_t k) {
    if (pieces[k].text.empty()) return;
    if (w > 0 && pieces[w - 1].span == SpanText) {
        pieces[w - 1].text += pieces[k].text;
        return;
    }
    if (w != k) pieces[w] = std::move(pieces[k]);
    w++;
}

// The alternations outside JosaTable: the pattern, the form after a final consonant
// and the form after a vowel.
struct LooseParticle {
    std::string_view pattern;
    std::string_view consonant;
    std::string_view vowel;
};
constexpr LooseParticle kLooseParticles[] = {{"(와)", "과", "와"}, {"(어)", "어", ""}, {"(아)", "아", ""}};

bool StartsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

}  // namespace

Pieces Cleanup(const std::vector<Sentence>& sentences, std::span<const std::string> gaps) {
    Pieces out;
    Pieces got;
    int chunk = sentences.empty() ? 0 : sentences.front().chunk;
    // A chunk after a phrase break that only kept a closing mark goes on from the one before (헤에?).
    bool phrase = false;
    const auto append = [&]() {
        const std::string_view head = Head(got);
        if (phrase && (StartsWith(head, "?") || StartsWith(head, "!"))) {
            TrimRight(out);
            TrimLeft(got);
            Append(out, std::move(got));
        } else {
            AppendChunk(out, std::move(got));
        }
    };
    for (const auto& s : sentences) {
        if (s.chunk != chunk) {
            append();
            got.clear();
            std::string gap;
            for (int c = chunk + 1; c <= s.chunk; c++) {
                if (static_cast<size_t>(c) < gaps.size()) gap += gaps[static_cast<size_t>(c)];
            }
            phrase = gap == kPhraseGap;
            if (phrase) gap.clear();
            if (!gap.empty()) {
                TrimRight(out);
                out.push_back({std::move(gap), SpanGap});
            }
            chunk = s.chunk;
        }
        AppendSentence(got, s.pieces);
    }
    append();
    out = CleanupText(std::move(out));
    FixSpacing(out);
    return out;
}

void AppendSentence(Pieces& result, const Pieces& sentence) {
    const std::string_view head = Head(sentence);
    if (StartsWith(head, " ")) {
        TrimRight(result);
    } else if ((StartsWith(head, "(이)라") || StartsWith(head, "(으)로")) && !result.empty() &&
               result.back().span == SpanText) {
        // A quoting (이)라 or (으)로 right after ! or ? goes with the word before them
        // (돈!으로, 쾅!이라고).
        const std::string_view t = result.back().text;
        if (t.ends_with("! ") || t.ends_with("? ")) TrimRight(result);
    }
    Append(result, sentence);
}

void AppendChunk(Pieces& out, Pieces&& got) {
    if (!out.empty() && out.back().span == SpanGap) {
        TrimLeft(got);
    } else if (!out.empty() && !got.empty()) {
        TrimRight(out);
        Add(out, " ");
        TrimLeft(got);
    }
    Append(out, std::move(got));
}

Pieces CleanupText(Pieces text) {
    internal::ResolveParticles(text);
    internal::DropLeadingCommas(text);
    return text;
}

void FixSpacing(Pieces& pieces) {
    size_t w = 0;
    for (size_t k = 0; k < pieces.size(); k++) {
        if (pieces[k].span != SpanText) {
            if (w != k) pieces[w] = std::move(pieces[k]);
            w++;
            continue;
        }
        auto& t = pieces[k].text;
        // A ruby reading follows its base without a space ({/rb}{rt}).
        const bool rubyGap = w > 0 && k + 1 < pieces.size() && pieces[w - 1].span == SpanClose &&
                             pieces[w - 1].text == "{/rb}" && pieces[k + 1].span == SpanOpen &&
                             pieces[k + 1].text == "{rt}" && t.find_first_not_of(' ') == std::string::npos;
        if (rubyGap) continue;
        internal::NarrowTilde(t);
        internal::DropSpaceBeforeComma(t);
        internal::DropSpaceBeforeClose(t);
        // The same across an opening tag: 했다 <k>―― 했다<k>――.
        if (k + 2 < pieces.size() && pieces[k + 1].span == SpanOpen && pieces[k + 2].span == SpanText &&
            StartsWith(pieces[k + 2].text, "――")) {
            const size_t end = t.find_last_not_of(' ') + 1;
            if (end > 0 && end < t.size() && IsSyllable(LastRune(std::string_view(t).substr(0, end)))) t.resize(end);
        }
        internal::NarrowWideDigits(t);
        Keep(pieces, w, k);
    }
    pieces.resize(w);
}

std::string_view StandIn(std::string_view text) {
    if (IsSyllable(LastRune(text))) return text;
    return ClassSyllable(SpanFinal(text));
}

char SpanFinal(std::string_view text) {
    const char32_t r = LastRune(text);
    if (r == kVowelMark) return 'v';
    if (r == kConsonantMark) return 'c';
    if (r == kRieulMark) return 'l';
    if (r == U'ㄹ' || r == 0x1105 || r == 0x11AF || r == 0xFFA9) return 'l';
    if ((r >= 0x314F && r <= 0x3163) || (r >= 0x3187 && r <= 0x318E) || (r >= 0x1160 && r <= 0x11A7) ||
        (r >= 0xD7B0 && r <= 0xD7C6) || (r >= 0xFFC2 && r <= 0xFFDC)) {
        return 'v';
    }
    if (const char f = ParticleClass(text); f != 0) return f;
    return 'c';
}

std::string_view ClassSyllable(char final) {
    switch (final) {
        case 'v': return "가";
        case 'l': return "갈";
    }
    return "각";
}

int JosaRow(std::string_view text) {
    const auto table = JosaTable();
    for (int row = 0; row < kJosaRows; row++) {
        if (StartsWith(text, table[row].pattern)) return row;
    }
    return -1;
}

bool IsOpening(char32_t r) {
    switch (r) {
        case U'(': case U'[': case U'{': case U'‘': case U'“': case U'〔': case U'〈': case U'《':
        case U'「': case U'『': case U'【': case U'（': case U'［': case U'｛':
            return true;
    }
    return false;
}

void RemoveGlosses(std::string& text) {
    size_t w = 0;  // text[0, w) is the output
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '(' && JosaRow(std::string_view(text).substr(i)) < 0) {
            size_t j = i + 1;
            while (j < text.size()) {
                const auto r = text::DecodeRune(std::string_view(text).substr(j));
                if (!IsSyllable(r.cp)) break;
                j += r.size;
            }
            const bool attached = IsSyllable(LastRune(std::string_view(text.data(), w))) ||
                                  (j + 1 <= text.size() && IsSyllable(FirstRune(std::string_view(text).substr(j + 1))));
            if (j > i + 1 && At(text, j) == ')' && attached) {
                i = j + 1;
                continue;
            }
        }
        const size_t n = text::DecodeRune(std::string_view(text).substr(i)).size;
        Move(text, w, i, n);
        w += n;
        i += n;
    }
    text.resize(w);
}

void SqueezeSpaces(std::string& text) {
    size_t w = 0;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != ' ' || i == 0 || text[i - 1] != ' ') text[w++] = text[i];
    }
    text.resize(w);
}

namespace internal {

void ResolveParticles(Pieces& pieces) {
    size_t w = 0;  // pieces[0, w) is the output
    for (size_t k = 0; k < pieces.size(); k++) {
        if (pieces[k].span != SpanText) {
            if (w != k) pieces[w] = std::move(pieces[k]);
            w++;
            continue;
        }
        std::string& t = pieces[k].text;
        size_t j = 0;  // t[0, j) is the output of this piece
        for (size_t i = 0; i < t.size();) {
            if (t[i] == '(') {
                const TextBefore read(std::span<const Piece>(pieces.data(), w), std::string_view(t.data(), j));
                const std::string_view rest = std::string_view(t).substr(i);
                const char final = ParticleClass(read);
                std::string_view form, pattern;
                if (const int row = JosaRow(rest); row >= 0) {
                    pattern = JosaTable()[row].pattern;
                    form = ParticleForm(row, final);
                } else {
                    for (const auto& p : kLooseParticles) {
                        if (StartsWith(rest, p.pattern) && AfterSyllable(read)) {
                            pattern = p.pattern;
                            form = final != 'c' && final != 'l' ? p.vowel : p.consonant;
                            break;
                        }
                    }
                }
                if (!pattern.empty()) {
                    Put(t, j, form);
                    j += form.size();
                    i += pattern.size();
                    if (form.empty() && j > 0 && t[j - 1] == ' ' && At(t, i) == ' ') i++;
                    continue;
                }
            }
            const size_t n = text::DecodeRune(std::string_view(t).substr(i)).size;
            Move(t, j, i, n);
            j += n;
            i += n;
        }
        t.resize(j);
        Keep(pieces, w, k);
    }
    pieces.resize(w);
}

bool AfterSyllable(const TextBefore& text) {
    size_t end = text.Size();
    if (end > 0 && text.At(end - 1) == ' ') end--;
    return IsSyllable(DecodeLastRune(text, end).cp);
}

void DropLeadingCommas(Pieces& pieces) {
    bool lineStart = true;
    size_t w = 0;
    for (size_t k = 0; k < pieces.size(); k++) {
        if (pieces[k].span != SpanText) {
            if (pieces[k].span == SpanWord) lineStart = false;
            if (w != k) pieces[w] = std::move(pieces[k]);
            w++;
            continue;
        }
        std::string& t = pieces[k].text;
        size_t j = 0;
        for (size_t i = 0; i < t.size();) {
            const char c = t[i];
            if (lineStart && c == ',') {
                for (i++; i < t.size() && t[i] == ' '; i++) {
                }
                continue;
            }
            const auto r = text::DecodeRune(std::string_view(t).substr(i));
            if (c == '\n') {
                lineStart = true;
            } else if (c != ' ' && !IsOpening(r.cp)) {
                lineStart = false;
            }
            Move(t, j, i, r.size);
            j += r.size;
            i += r.size;
        }
        t.resize(j);
        Keep(pieces, w, k);
    }
    pieces.resize(w);
}

void NarrowTilde(std::string& text) {
    constexpr std::string_view kTildeOperator = "∼";
    size_t w = 0;
    for (size_t i = 0; i < text.size();) {
        if (StartsWith(std::string_view(text).substr(i), kTildeOperator)) {
            text[w++] = '~';
            i += kTildeOperator.size();
        } else {
            text[w++] = text[i++];
        }
    }
    text.resize(w);
}

void DropSpaceBeforeComma(std::string& text) {
    size_t w = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == ' ') {
            size_t e = i;
            while (e < text.size() && text[e] == ' ') e++;
            if (At(text, e) != ',') {
                Move(text, w, i, e - i);
                w += e - i;
            }
            i = e;
        } else {
            text[w++] = text[i++];
        }
    }
    text.resize(w);
}

void DropSpaceBeforeClose(std::string& text) {
    constexpr std::string_view kDashes = "――";
    size_t w = 0;
    for (size_t i = 0; i < text.size();) {
        const auto r = text::DecodeRune(std::string_view(text).substr(i));
        const bool mark = r.cp == U'?' || r.cp == U'!';
        if (mark || IsSyllable(r.cp)) {
            size_t e = i + r.size;
            while (e < text.size() && text[e] == ' ') e++;
            if (e > i + r.size) {
                const std::string_view rest = std::string_view(text).substr(e);
                std::string_view tail;
                if (mark) {
                    for (const auto c : dict::ClassMembers(HDTRANS_CLASS("mark.close-after-space"))) {
                        if (StartsWith(rest, c)) tail = c;
                    }
                } else if (StartsWith(rest, kDashes)) {
                    tail = kDashes;
                } else {
                    // A closing corner bracket takes no space after a syllable (가겠다 」 → 가겠다」),
                    // also where a word between them wrote nothing.
                    for (const std::string_view c : {"」", "』"}) {
                        if (StartsWith(rest, c)) tail = c;
                    }
                }
                if (!tail.empty()) {
                    Move(text, w, i, r.size);
                    w += r.size;
                    Move(text, w, e, tail.size());
                    w += tail.size();
                    i = e + tail.size();
                    continue;
                }
            }
        }
        Move(text, w, i, r.size);
        w += r.size;
        i += r.size;
    }
    text.resize(w);
}

void NarrowWideDigits(std::string& text) {
    // ０..９ are EF BC 90..99.
    size_t w = 0;
    for (size_t i = 0; i < text.size();) {
        if (i + 2 < text.size() && static_cast<unsigned char>(text[i]) == 0xEF &&
            static_cast<unsigned char>(text[i + 1]) == 0xBC && static_cast<unsigned char>(text[i + 2]) >= 0x90 &&
            static_cast<unsigned char>(text[i + 2]) <= 0x99) {
            text[w++] = static_cast<char>('0' + (static_cast<unsigned char>(text[i + 2]) - 0x90));
            i += 3;
        } else {
            text[w++] = text[i++];
        }
    }
    text.resize(w);
}

}  // namespace internal

}  // namespace hdTrans::output
