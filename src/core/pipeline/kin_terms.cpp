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

// The words the dictionary reads as a man says them, in a woman's speech: written as the words it
// reads 오빠 and 언니 (spec LEX-4619). Kana spellings come before the ones they hold.
constexpr KinTerm kSisterKinTerms[] = {
    {"お兄さん", "お兄ちゃん"}, {"おにいさん", "お兄ちゃん"}, {"にいさん", "お兄ちゃん"}, {"兄さん", "お兄ちゃん"},
    {"兄貴", "お兄ちゃん"},
    {"姉貴", "お姉ちゃん"},
};
// The words the dictionary reads as a woman says them, in a man's speech: 누나, and お兄様 written as
// 兄さん, which the dictionary reads 형님 (spec LEX-4619).
constexpr KinTerm kBrotherKinTerms[] = {
    {"お姉ちゃん", "누나"}, {"おねえちゃん", "누나"}, {"お姉さん", "누나"}, {"おねえさん", "누나"},
    {"姉さん", "누나"}, {"ねえさん", "누나"}, {"姉ちゃん", "누나"}, {"ねえちゃん", "누나"},
    {"お兄様", "兄さん"}, {"お兄さま", "兄さん"}, {"兄様", "兄さん"}, {"兄さま", "兄さん"},
};
// Words that a rough man's speech holds, which a woman's のよ or わね does not outweigh for the
// words above (いかねえのよ, だがよ).
constexpr std::string_view kRoughSpeech[] = {"ねえ", "ねぇ", "だろ", "かよ", "だがよ", "てめえ", "てめぇ"};
// More signs of a younger sister speaking that the words above take: the speaker naming herself the
// sister (この妹, 妹として).
constexpr std::string_view kSisterSpeech[] = {"この妹", "可愛い妹", "かわいい妹", "妹として", "妹の私", "妹のわたし"};
// The speech of a man that the words above need: 俺 and ぜ (僕 and ぞ are also a girl's).
constexpr std::string_view kPlainMaleSpeech[] = {"俺", "だぜ", "るぜ"};

bool HasAny(std::string_view s, std::span<const std::string_view> list) {
    for (std::string_view m : list) {
        if (s.find(m) != std::string_view::npos) return true;
    }
    return false;
}

bool Contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// Whether s holds a member of list as a speech cue: のよ in のよう (like) is no cue.
bool HasCue(std::string_view s, std::span<const std::string_view> list) {
    for (std::string_view m : list) {
        for (size_t at = s.find(m); at != std::string_view::npos; at = s.find(m, at + 1)) {
            if (m == "のよ" && s.substr(at + m.size()).starts_with("う")) continue;
            return true;
        }
    }
    return false;
}

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

// s with each bare from replaced by to where it stands alone: no kanji before it (実兄) unless from
// starts with お, nor お before a bare 兄 (お兄), and no kanji or kana of a longer word after it (兄弟, お兄様, お兄さん, 兄ちゃん).
std::string ReplaceBare(std::string_view s, std::string_view from, std::string_view to) {
    std::string out;
    size_t at = 0;
    for (size_t i = s.find(from); i != std::string_view::npos; i = s.find(from, i + from.size())) {
        const bool kanjiBefore = !from.starts_with("お") && (KanjiBefore(s, i) != std::string_view::npos ||
                                                            s.substr(0, i).ends_with("お"));
        const std::string_view after = s.substr(i + from.size());
        const char32_t next = after.empty() ? 0 : text::DecodeRune(after).cp;
        const bool joined = text::IsKanji(next) || next == U'さ' || next == U'ち' || next == U'ぃ' || next == U'ぇ' ||
                            next == U'ー' || next == U'ら' || next == U'た' || next == U'っ' || next == U'貴';
        if (kanjiBefore || joined) continue;
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
    const bool womanCue = HasCue(line, dict::ClassMembers(HDTRANS_CLASS("speech.female"))) || HasAny(line, kSisterSpeech);
    const bool rough = HasAny(line, kRoughSpeech);
    std::string out(line);
    if ((female && !male) || woman) {
        for (const KinTerm& k : kKinTerms) out = ReplaceWord(out, k.ja, k.ko, holds, context);
    } else if (male) {
        for (const KinTerm& k : kMaleKinTerms) out = ReplaceWord(out, k.ja, k.ko, holds, context);
    }
    // お兄 alone is a sister's word: お兄ちゃん, 오빠, unless the line is a man's speech.
    if (!male) out = ReplaceBare(out, "お兄", "お兄ちゃん");
    // The words the dictionary reads for the other sex switch only on a clear cue (spec LEX-4619):
    // a woman's speech with no man's or rough speech, or 俺 or ぜ with no woman's speech.
    if ((female || womanCue) && !male && !rough) {
        for (const KinTerm& k : kSisterKinTerms) out = ReplaceWord(out, k.ja, k.ko, holds, context);
        out = ReplaceBare(out, "兄", "お兄ちゃん");
    } else if (HasAny(line, kPlainMaleSpeech) && !woman) {
        for (const KinTerm& k : kBrotherKinTerms) out = ReplaceWord(out, k.ja, k.ko, holds, context);
        out = ReplaceBare(out, "姉", "누나");
    }
    return out;
}

}  // namespace hdTrans::pipeline::internal
