#include <span>
#include <vector>

#include "core/dict/classes.h"
#include "core/pipeline/normalize.h"

namespace hdTrans::pipeline::internal {

namespace {

// The words for an elder sibling whose Korean depends on the speaker: a woman says 오빠
// and 언니, a man 형님 and 누님 (the dictionary's reading).
struct KinTerm {
    std::string_view ja;
    std::string_view female;
};
constexpr KinTerm kKinTerms[] = {
    {"お兄様", "오빠"}, {"お兄さま", "오빠"}, {"兄様", "오빠"}, {"兄さま", "오빠"}, {"お兄ちゃん", "오빠"},
    {"お姉様", "언니"}, {"お姉さま", "언니"}, {"姉様", "언니"}, {"姉さま", "언니"}, {"お姉ちゃん", "언니"},
};

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

std::string ReplaceString(std::string_view s, std::string_view from, std::string_view to) {
    std::string out;
    size_t at = 0;
    for (size_t i; (i = s.find(from, at)) != std::string_view::npos; at = i + from.size()) {
        out.append(s.substr(at, i - at));
        out.append(to);
    }
    out.append(s.substr(at));
    return out;
}

}  // namespace

SpeechCues ReadSpeechCues(std::string_view line) {
    SpeechCues cues;
    const auto utterances = Utterances(line);
    for (std::string_view u : utterances) {
        if (HasAny(u, dict::ClassMembers("speech.female"))) cues.female = true;
        if (!HasAny(u, dict::ClassMembers("speech.male"))) continue;
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

std::string WriteKinTerms(std::string_view line, bool female) {
    if (!((female && !HasAny(line, dict::ClassMembers("speech.male"))) || HasAny(line, dict::ClassMembers("speech.female")))) {
        return std::string(line);
    }
    std::string out(line);
    for (const KinTerm& k : kKinTerms) out = ReplaceString(out, k.ja, k.female);
    return out;
}

}  // namespace hdTrans::pipeline::internal
