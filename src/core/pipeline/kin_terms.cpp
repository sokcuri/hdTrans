#include <span>
#include <vector>

#include "core/dict/classes.h"
#include "core/pipeline/normalize.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline::internal {

namespace {

// The words for an elder sibling whose Korean depends on the speaker: a woman says 오빠
// and 언니, a man 형님 and 누님 (the dictionary's reading). お兄ちゃん is 오빠 in the dictionary
// and 형 in a line of a man's speech (kMaleKinTerms).
struct KinTerm {
    std::string_view ja;
    std::string_view ko;
};
constexpr KinTerm kKinTerms[] = {
    {"お兄様", "오빠"}, {"お兄さま", "오빠"}, {"兄様", "오빠"}, {"兄さま", "오빠"}, {"お兄ちゃん", "오빠"},
    {"お姉様", "언니"}, {"お姉さま", "언니"}, {"姉様", "언니"}, {"姉さま", "언니"}, {"お姉ちゃん", "언니"},
};

// Written as the word the dictionary reads 형, so that the line is still segmented as Japanese.
constexpr KinTerm kMaleKinTerms[] = {{"お兄ちゃん", "兄ちゃん"}, {"おにいちゃん", "にいちゃん"}};

bool HasAny(std::string_view s, std::span<const std::string_view> list) {
    for (std::string_view m : list) {
        if (s.find(m) != std::string_view::npos) return true;
    }
    return false;
}

bool Contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

struct Quote {
    std::string_view open;
    std::string_view close;
};
constexpr Quote kQuotes[] = {{"「", "」"}, {"『", "』"}};

// The text inside 「」 and then inside 『』 of line; an unclosed quote runs to the end.
std::vector<std::string_view> Utterances(std::string_view line) {
    std::vector<std::string_view> out;
    for (const Quote& q : kQuotes) {
        const std::string_view open = q.open, close = q.close;
        std::string_view rest = line;
        for (;;) {
            const size_t at = rest.find(open);
            if (at == std::string_view::npos) break;
            const std::string_view after = rest.substr(at + open.size());
            const size_t end = after.find(close);
            if (end == std::string_view::npos) {
                out.push_back(after);
                break;
            }
            out.push_back(after.substr(0, end));
            rest = after.substr(end + close.size());
        }
    }
    return out;
}

// The start of the kanji before s[i], or npos when s[i] follows no kanji.
size_t KanjiBefore(std::string_view s, size_t i) {
    size_t start = i;
    while (start > 0 && (static_cast<unsigned char>(s[start - 1]) & 0xC0) == 0x80) start--;
    if (start == 0) return std::string_view::npos;
    start--;
    return text::IsKanji(text::DecodeRune(s.substr(start, i - start)).cp) ? start : std::string_view::npos;
}

// s with each from replaced by to, but not where a dictionary word holds the kanji before it and
// its first character (義姉様, 従兄様): that word is read as the dictionary has it.
std::string ReplaceWord(std::string_view s, std::string_view from, std::string_view to, WordHoldsRun holds,
                        const void* context) {
    std::string out;
    size_t at = 0;
    const size_t first = text::DecodeRune(from).size;
    for (size_t i = s.find(from); i != std::string_view::npos; i = s.find(from, i + from.size())) {
        const size_t before = KanjiBefore(s, i);
        if (holds != nullptr && before != std::string_view::npos && holds(context, s, before, i + first)) continue;
        out.append(s.substr(at, i - at));
        out.append(to);
        at = i + from.size();
    }
    out.append(s.substr(at));
    return out;
}

}  // namespace

SpeechCues ReadSpeechCues(std::string_view line) {
    SpeechCues cues;
    cues.quoted = Contains(line, "「");
    const auto utterances = Utterances(line);
    for (std::string_view u : utterances) {
        if (HasAny(u, dict::ClassMembers(HDTRANS_CLASS("speech.female")))) cues.female = true;
        if (!HasAny(u, dict::ClassMembers(HDTRANS_CLASS("speech.male")))) continue;
        for (const KinTerm& k : kKinTerms) {
            if (Contains(u, k.ja)) cues.maleKin = true;
        }
    }
    return cues;
}

bool FemaleSpeech(std::span<const std::string> lines) {
    bool female = false;
    for (const std::string& line : lines) {
        const SpeechCues cues = ReadSpeechCues(line);
        if (cues.maleKin) return false;
        female = female || cues.female;
    }
    return female;
}

std::string WriteKinTerms(std::string_view line, bool female, WordHoldsRun holds, const void* context) {
    const bool male = HasAny(line, dict::ClassMembers(HDTRANS_CLASS("speech.male")));
    const bool woman = HasAny(line, dict::ClassMembers(HDTRANS_CLASS("speech.female")));
    std::string out(line);
    if ((female && !male) || woman) {
        for (const KinTerm& k : kKinTerms) out = ReplaceWord(out, k.ja, k.ko, holds, context);
    } else if (male) {
        for (const KinTerm& k : kMaleKinTerms) out = ReplaceWord(out, k.ja, k.ko, holds, context);
    }
    return out;
}

}  // namespace hdTrans::pipeline::internal
