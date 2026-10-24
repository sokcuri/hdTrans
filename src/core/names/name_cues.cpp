#include "core/names/name_cues.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/names/chars.h"
#include "core/text/utf8.h"

namespace hdTrans::names {

namespace {

using internal::FirstRune;
using internal::InRunes;
using internal::IsHiragana;
using internal::IsKanji;
using internal::IsKatakana;
using internal::RuneBefore;

// What ends the text before an utterance (an opening bracket, a sentence end, a speaker
// label's colon) and what closes the word after a self-introduction or a call.
constexpr std::string_view kOpens = "「『（(【\n。！？!?…‥―♪」』】:：";
constexpr std::string_view kCloses = "。！？!?…‥、」』）)♪ 　";
constexpr std::string_view kNumerals = "0123456789０１２３４５６７８９一二三四五六七八九十百";

using Members = std::span<const std::string_view>;

bool StartsWithAny(std::string_view s, Members set) {
    return std::ranges::any_of(set, [&](std::string_view m) { return s.starts_with(m); });
}

bool EndsWithAny(std::string_view s, Members set) {
    return std::ranges::any_of(set, [&](std::string_view m) { return s.ends_with(m); });
}

std::string_view TrimEndSpaces(std::string_view s) {
    while (s.ends_with(" ") || s.ends_with("　")) s.remove_suffix(s.ends_with(" ") ? 1 : std::string_view("　").size());
    return s;
}

std::string_view WithoutEnd(std::string_view s, std::string_view end) {
    return s.ends_with(end) ? s.substr(0, s.size() - end.size()) : s;
}

bool LineStart(std::string_view before) {
    const std::string_view b = TrimEndSpaces(before);
    return b.empty() || b.ends_with("\n");
}

bool UtteranceStart(std::string_view before) {
    const std::string_view b = TrimEndSpaces(before);
    return b.empty() || InRunes(RuneBefore(b, b.size()).cp, kOpens);
}

bool UtteranceEnd(std::string_view after) { return after.empty() || InRunes(FirstRune(after).cp, kCloses); }

// Whether before ends in a ship type or a numbered ship (二番艦), right before the word, before
// 「 or 『 around it, or before 、 (陽炎型駆逐艦、陽炎よ); a numbered ship also before の
// (二番艦の時雨). A katakana name may also follow a person's title (聖女アヤメ).
bool TitleBefore(std::string_view before, bool katakana) {
    std::string_view b = WithoutEnd(WithoutEnd(before, "「"), "『");
    if (b.size() == before.size()) b = WithoutEnd(b, "、");
    if (EndsWithAny(b, dict::ClassMembers("name.title-ship"))) return true;
    if (katakana && b.size() == before.size() && EndsWithAny(b, dict::ClassMembers("name.title-person"))) return true;
    if (b.size() == before.size()) b = WithoutEnd(b, "の");
    if (!b.ends_with("番艦")) return false;
    b.remove_suffix(std::string_view("番艦").size());
    return !b.empty() && InRunes(RuneBefore(b, b.size()).cp, kNumerals);
}

bool CallableKey(const dict::Snapshot& snap, std::string_view key, bool kanji);

// Whether the dictionary reads key only as nouns (睦月 1월, 時雨; not 元気 of 元気、出る).
bool NounKey(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key) continue;
        const char tag = c.codes->part.Empty() ? 0 : c.codes->part.Tag();
        if (tag != 'N' && tag != 'I') return false;
    }
    return true;
}

// Whether the dictionary has a verb or an adjective in い that starts with key (行 of 行く, 許
// of 許す): the kana after it may inflect it rather than be an honorific (行くんだ, 許さん).
bool PredicateStem(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.source == key && !c.codes->part.Empty() && (c.codes->part.Tag() == 'M' || c.codes->part.Tag() == 'H')) {
            return true;
        }
    }
    return false;
}

// Whether an honorific after key makes it a name for the whole document: key starts with no
// honorific prefix (お姫様), is no word of a role (姫様) and has no sense of a person, an animal
// (蟹ちゃん) or a verb or adjective that it starts (行くんだ, 許さん).
bool HonorableKey(const dict::Snapshot& snap, std::string_view key) {
    if (key.starts_with("お") || key.starts_with("ご") || key.starts_with("御") || dict::InClass("noun.role", key) ||
        PredicateStem(snap, key)) {
        return false;
    }
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN")) continue;
        for (const std::string_view code : {"VHn", "QNP", "QAA", "QAB", "QAC", "QAD", "QAE", "QAF", "QAG", "QAH"}) {
            if (c.codes->sem.HasCode(code)) return false;
        }
    }
    return true;
}

// The text after the word: a sister's or an older brother's title or a remodel (時雨姉さん,
// 霞改二), or a phrase of naming after a word that HonorableKey allows (アヤメと名乗る; not
// 神と名乗る).
bool SuffixAfter(const dict::Snapshot& snap, std::string_view after, std::string_view key) {
    if (StartsWithAny(after, dict::ClassMembers("name.suffix-after"))) return true;
    if (StartsWithAny(after, dict::ClassMembers("name.intro-after"))) return HonorableKey(snap, key);
    return false;
}

// A class after a ship's name of two kanji or more, also after a prefix of the class (陽炎型,
// 改陽炎型, 初期陽炎型).
bool ClassAfter(std::string_view after, std::string_view key, bool kanji) {
    return kanji && text::RuneCount(key) >= 2 && StartsWithAny(after, dict::ClassMembers("name.class-after"));
}

// An honorific after a word that HonorableKey allows (筧君): a cue of its line only, since the
// name registry weighs honorifics across the lines of a document.
bool HonorificAfter(const dict::Snapshot& snap, std::string_view after, std::string_view key) {
    return StartsWithAny(after, dict::ClassMembers("honorific.candidate")) && !after.starts_with("ちゃんと") &&
           !after.starts_with("くんだり") && HonorableKey(snap, key);
}

// Whether a hiragana key starts with a verb or an adjective of the dictionary (いくよ: いく).
bool HiraganaPredicate(const dict::Snapshot& snap, std::string_view key) {
    for (std::size_t n = 0; n < key.size();) {
        n += text::DecodeRune(key.substr(n)).size;
        if (n >= std::string_view("いく").size() && PredicateStem(snap, key.substr(0, n))) return true;
    }
    return false;
}

// Whether what follows a word after a title ends a name there: a mark or the end, a bracket, a
// particle, a copula, a self-introduction, a suffix or an honorific (駆逐艦時雨、, 戦艦大和です;
// not 海防艦計3隻).
bool NameEnd(std::string_view after) {
    if (UtteranceEnd(after) || after.starts_with("「") || after.starts_with("」") || after.starts_with("』") ||
        after.starts_with("だ") || after.starts_with("改")) {
        return true;
    }
    return StartsWithAny(after, dict::ClassMembers("particle.after-name")) ||
           StartsWithAny(after, dict::ClassMembers("name.introduction-copula")) ||
           StartsWithAny(after, dict::ClassMembers("name.suffix-after")) ||
           StartsWithAny(after, dict::ClassMembers("name.class-after")) ||
           StartsWithAny(after, dict::ClassMembers("name.intro-after")) ||
           StartsWithAny(after, dict::ClassMembers("honorific.candidate"));
}

// A speaker label: the word alone at the start of the line before 「, 『 or a colon.
bool LabelAt(std::string_view before, std::string_view after) {
    if (!LineStart(before)) return false;
    if (after.starts_with(":") || after.starts_with("：")) return true;
    // A quote ends the line; text after its closing bracket makes the bracket a reading or a
    // title (光「ひかり」散「ち」らす, 新「三川艦隊」を編成せよ).
    for (const auto& [open, close] : {std::pair<std::string_view, std::string_view>{"「", "」"}, {"『", "』"}}) {
        if (!after.starts_with(open)) continue;
        const std::size_t end = after.rfind(close);
        if (end == std::string_view::npos) return true;
        const std::string_view tail = after.substr(end + close.size());
        return internal::AllRunes(tail, [](char32_t c) { return InRunes(c, kCloses) || c == U'\r'; }) || tail.empty();
    }
    return false;
}

// A call to action after the speaker's own name at the start of an utterance (時雨、出撃するよ),
// not after a word that starts a verb or an adjective (楽、行きます); NameCues takes it for two
// characters or more only (松、了解 is the tree too).
bool ActionAt(const dict::Snapshot& snap, std::string_view key, std::string_view before, std::string_view after) {
    return UtteranceStart(before) && after.starts_with("、") && !PredicateStem(snap, key) && NounKey(snap, key) &&
           StartsWithAny(after.substr(std::string_view("、").size()), dict::ClassMembers("name.action-after"));
}

// A self-introduction: the word and a copula or よ make the first sentence of the line (睦月です。,
// 陽炎よ。, 「聖女アヤメよ、); a hiragana name may also introduce itself later (だから…まるゆです).
// Later in a line the word answers or exclaims (……あっ！夢です！, ……季節です).
bool IntroductionAt(std::string_view before, std::string_view after, bool hiragana) {
    if (!(hiragana ? UtteranceStart(before) : LineStart(WithoutEnd(WithoutEnd(TrimEndSpaces(before), "「"), "『")))) return false;
    for (const std::string_view copula : dict::ClassMembers("name.introduction-copula")) {
        if (after.starts_with(copula) && UtteranceEnd(after.substr(copula.size()))) return true;
    }
    return false;
}

// Whether a call of key at the start or the end of an utterance (アヤメ、, 、エナ」) names
// someone: the dictionary has no sense of a time or an adverb of it, and for kanji only noun
// senses, where a word of time, an adverb or an adjectival noun (明日、, 正直、, 元気、) starts
// a sentence. A kana name may also be spelled like an adjectival noun (まどか).
bool CallableKey(const dict::Snapshot& snap, std::string_view key, bool kanji) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key) continue;
        const char tag = c.codes->part.Empty() ? 0 : c.codes->part.Tag();
        if (tag == 'E' || c.codes->sem.HasCode("UMe") || (kanji && tag != 'N' && tag != 'I')) return false;
    }
    return true;
}

// Whether the dictionary reads key first as a place name: a call of it may list places
// (…富山、名古屋は).
bool PlaceKey(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.source == key) return c.codes->sem.HasCode("QCT") || c.codes->sem.HasCode("QNN");
    }
    return false;
}

// A call: the word alone after 、 at the end of an utterance (大丈夫か、エナ). At the start an
// utterance names its topic as often as the one it calls (宇宙、そこは, チョコ、あげます).
bool CallAt(std::string_view before, std::string_view after) {
    std::string_view b = TrimEndSpaces(before);
    // At the end: before a closing bracket, an exclamation or a question, or the end (大丈夫か、エナ」;
    // not 、スルー。).
    const bool closes = after.empty() || after.starts_with("」") || after.starts_with("』") || after.starts_with("！") ||
                        after.starts_with("？") || after.starts_with("!") || after.starts_with("?") ||
                        after.starts_with("…");
    if (!(b.ends_with("、") || b.ends_with("，")) || !closes) return false;
    b = WithoutEnd(WithoutEnd(b, "、"), "，");
    // The call follows a clause of its own that ends in hiragana (大丈夫か、エナ), not an
    // interjection (げ、マジ) or a list (ワン、ツー).
    if (!IsHiragana(RuneBefore(b, b.size()).cp)) return false;
    std::size_t runes = 0;
    while (!b.empty() && !InRunes(RuneBefore(b, b.size()).cp, kOpens) && !InRunes(RuneBefore(b, b.size()).cp, "、 　")) {
        b.remove_suffix(RuneBefore(b, b.size()).size);
        runes++;
    }
    return runes >= 3;
}

// Whether the dictionary has no common word of key, only names and places.
bool OnlyNames(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key) continue;
        if (!c.codes->sem.HasPrefix("QPN") && !c.codes->sem.HasPrefix("QCT") && !c.codes->sem.HasPrefix("QNN")) return false;
    }
    return true;
}

// Whether a single character may be a speaker label: no verb or adjective starts with it and no
// sense of it is a prefix (潮, 朧; not 続 of 続： or 新 of 新「).
bool LabelNoun(const dict::Snapshot& snap, std::string_view key) {
    if (PredicateStem(snap, key)) return false;
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.source == key && c.codes->gram.Contains("Z6")) return false;
    }
    return true;
}

// Whether a weak cue may name key: a single character with a sense of weather, the sky or a
// time answers as that word (「春は？」「霞です」, 暁よ).
bool WeakKey(const dict::Snapshot& snap, std::string_view key) {
    if (text::RuneCount(key) >= 2) return true;
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key) continue;
        for (const std::string_view code : {"VFr", "VSs", "UMe"}) {
            if (c.codes->sem.HasCode(code)) return false;
        }
    }
    return true;
}

// Whether a strong cue of key may hold through the document: key has two characters or more,
// is no hiragana nor a katakana spelling of a native word, and the dictionary reads it as no adjective,
// adverb, verb or word of time (元気, いつか), nor as weather, the sky or a place (朝日が目に
// しみる, 対馬に渡る), senses a later line may mean.
bool Memorable(const dict::Snapshot& snap, std::string_view key) {
    if (text::RuneCount(key) < 2 || !CallableKey(snap, key, true) || internal::AllRunes(key, IsHiragana)) return false;
    // A katakana spelling of a native word (ザラ, ガリガリ) is that word in other lines.
    if (internal::AllRunes(key, IsKatakana) && snap.DictionaryWord(internal::ToHiragana(key))) return false;
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN")) continue;
        for (const std::string_view code : {"VFr", "VSs", "QCT", "VPl", "QNN"}) {
            if (c.codes->sem.HasCode(code)) return false;
        }
    }
    return true;
}

struct Bracketed {
    std::size_t begin, end;  // the word between 「 and 」
};

// The words of a list of names in brackets: two or more 「」 next to each other, one of
// whose words is a name the dictionary has no common word for (「鈴谷」「村雨」「夕立」).
std::vector<std::string_view> ListedNames(const dict::Snapshot& snap, std::string_view text) {
    std::vector<std::string_view> out;
    std::vector<Bracketed> run;
    auto flush = [&] {
        if (run.size() >= 2) {
            bool name = false;
            for (const Bracketed& b : run) {
                const std::string_view w = text.substr(b.begin, b.end - b.begin);
                const std::vector<std::size_t> keys = snap.PersonKeysAt(w);
                name = name || (!keys.empty() && keys.back() == w.size() && OnlyNames(snap, w));
            }
            for (const Bracketed& b : run) {
                if (name) out.push_back(text.substr(b.begin, b.end - b.begin));
            }
        }
        run.clear();
    };
    std::size_t at = 0;
    while (true) {
        const std::size_t open = text.find("「", at);
        if (open == std::string_view::npos) break;
        if (!run.empty() && open != run.back().end + std::string_view("」").size()) flush();
        const std::size_t begin = open + std::string_view("「").size();
        const std::size_t close = text.find("」", begin);
        if (close == std::string_view::npos) break;
        const std::string_view w = text.substr(begin, close - begin);
        if (w.empty() || text::RuneCount(w) > 8 || w.find("「") != std::string_view::npos) {
            flush();
        } else {
            run.push_back({begin, close});
        }
        at = close;
    }
    flush();
    return out;
}

}  // namespace

std::vector<NameCue> NameCues(const dict::Snapshot& snap, std::string_view text) {
    std::vector<NameCue> out;
    auto add = [&](std::string_view key, bool strong) {
        strong = strong && Memorable(snap, key);
        const auto found = std::find_if(out.begin(), out.end(), [&](const NameCue& c) { return c.key == key; });
        if (found == out.end()) out.push_back({std::string(key), strong});
        else found->strong = found->strong || strong;
    };
    for (const std::string_view w : ListedNames(snap, text)) {
        const std::vector<std::size_t> keys = snap.PersonKeysAt(w);
        if (!keys.empty() && keys.back() == w.size() && WeakKey(snap, w)) add(w, false);
    }
    std::size_t covered = 0;
    for (std::size_t i = 0; i < text.size();) {
        const text::Rune r = text::DecodeRune(text.substr(i));
        const std::size_t step = r.size == 0 ? 1 : r.size;
        if (i < covered || !(IsKanji(r.cp) || IsKatakana(r.cp) || IsHiragana(r.cp)) || InRunes(r.cp, kNumerals)) {
            i += step;
            continue;
        }
        const std::string_view rest = text.substr(i);
        const std::vector<std::size_t> keys = snap.PersonKeysAt(rest);
        const std::size_t word = snap.WordAt(rest);
        const std::size_t n = keys.empty() ? 0 : keys.back();
        // A longer word of the dictionary here reads the text, unless it ends in the ち of a ちゃん
        // (夕立ち before ゃん).
        if (n == 0 || (word > n && !(rest.substr(0, word).ends_with("ち") && rest.substr(word).starts_with("ゃん")))) {
            covered = std::max(covered, i + word);
            i += step;
            continue;
        }
        covered = i + n;
        const std::string_view key = rest.substr(0, n);
        // A word of a role, a trade or a speaker's role is no name (神, 姫, 鍛冶屋, 店員).
        if (dict::InClass("noun.role", key) || dict::InClass("noun.label-role", key) || key.ends_with("屋")) {
            i += step;
            continue;
        }
        const std::string_view before = text.substr(0, i);
        const std::string_view after = rest.substr(n);
        const char32_t last = RuneBefore(before, before.size()).cp;
        const char32_t next = FirstRune(after).cp;
        const bool kanji = internal::AllRunes(key, IsKanji);
        const bool katakana = internal::AllRunes(key, IsKatakana);
        const bool hiragana = internal::AllRunes(key, IsHiragana);
        // A kana name is a whole run of its script, a hiragana one of three kana or more.
        if ((katakana && (IsKatakana(last) || IsKatakana(next))) ||
            (hiragana && (IsHiragana(last) || text::RuneCount(key) < 3 || HiraganaPredicate(snap, key)))) {
            i += step;
            continue;
        }
        // A word right after a number is a counter (32駆改二).
        if (InRunes(last, kNumerals)) {
            i += step;
            continue;
        }
        // A suffix names the whole kanji run before it (漆葉さん is no 葉さん).
        const bool runStart = !kanji || !IsKanji(last);
        // A single character after a title stands alone before a mark (駆逐艦、杉！; not 潜水艦等に).
        const bool single = text::RuneCount(key) < 2;
        const bool alone = UtteranceEnd(after) || after.starts_with("「") || after.starts_with("」") || after.starts_with("』");
        if ((TitleBefore(before, katakana) && NameEnd(after) && CallableKey(snap, key, false) && (!single || alone)) ||
            (runStart && SuffixAfter(snap, after, key) && CallableKey(snap, key, false)) || ClassAfter(after, key, kanji) ||
            (LabelAt(before, after) && (!single || LabelNoun(snap, key))) || (!single && ActionAt(snap, key, before, after))) {
            add(key, true);
        } else if ((runStart && HonorificAfter(snap, after, key)) ||
                   (WeakKey(snap, key) && (IntroductionAt(before, after, hiragana) ||
                                          ((katakana || (kanji && text::RuneCount(key) >= 2)) &&
                                           CallAt(before, after) && CallableKey(snap, key, kanji) &&
                                           !PlaceKey(snap, key))))) {
            add(key, false);
        }
        i += step;
    }
    return out;
}

}  // namespace hdTrans::names
