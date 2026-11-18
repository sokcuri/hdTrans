#include "core/names/name_cues.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/dict/classes.h"
#include "core/names/chars.h"
#include "core/names/labels.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/word/conjugation.h"
#include "core/word/kana_tables.h"

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
    if (dict::EndsWithMember(HDTRANS_CLASS("name.title-ship"), b) > 0) return true;
    if (katakana && b.size() == before.size() && dict::EndsWithMember(HDTRANS_CLASS("name.title-person"), b) > 0) return true;
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
bool HonorableKey(const dict::Snapshot& snap, std::string_view key, bool stemOk = false) {
    if (key.starts_with("お") || key.starts_with("ご") || key.starts_with("御") || dict::InClass(HDTRANS_CLASS("noun.role"), key) ||
        (!stemOk && PredicateStem(snap, key))) {
        return false;
    }
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        // A name row is no person noun, also one whose codes put VHn first (力 치카라 VHnQPN Wb).
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN") || c.codes->sem.HasCode("QPN") ||
            c.codes->gram.HasPrefix("Wa") || c.codes->gram.HasPrefix("Wb")) {
            continue;
        }
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
    if (dict::StartsWithMember(HDTRANS_CLASS("name.suffix-after"), after) > 0) return true;
    if (dict::StartsWithMember(HDTRANS_CLASS("name.intro-after"), after) > 0) return HonorableKey(snap, key);
    return false;
}

// A class after a ship's name of two kanji or more, also after a prefix of the class (陽炎型,
// 改陽炎型, 初期陽炎型).
bool ClassAfter(std::string_view after, std::string_view key, bool kanji) {
    return kanji && text::RuneCount(key) >= 2 && dict::StartsWithMember(HDTRANS_CLASS("name.class-after"), after) > 0;
}

// An honorific after a word that HonorableKey allows (筧君): a cue of its line only, since the
// name registry weighs honorifics across the lines of a document.
// Whether an honorific starts after, not ちゃんと or くんだり.
bool HonorificStart(std::string_view after) {
    return dict::StartsWithMember(HDTRANS_CLASS("honorific.candidate"), after) > 0 && !after.starts_with("ちゃんと") &&
           !after.starts_with("くんだり");
}

bool HonorificAfter(const dict::Snapshot& snap, std::string_view after, std::string_view key) {
    // A verb stem (力 of 力む) takes くん, 君, 様 or ちゃん as a name (力くん); さん after it may be a negative
    // (許さん), and a longer word of the dictionary there (行くんだ) the caller has already read.
    const bool stemOk = !after.starts_with("さん") && !after.starts_with("さま");
    return HonorificStart(after) && HonorableKey(snap, key, stemOk);
}

// A title after a word that HonorableKey allows (葉月先生, 筧先輩), or a word of a family after a
// kanji name of two characters or more (園崎本家, 古手家, 竜宮家): like an honorific, it shows a person
// to the document (spec LEX-4826).
bool TitleAfter(const dict::Snapshot& snap, std::string_view after, std::string_view key) {
    return (after.starts_with("先生") || after.starts_with("先輩") ||
            (IsKanji(FirstRune(key).cp) && text::RuneCount(key) >= 2 &&
             (dict::StartsWithMember(HDTRANS_CLASS("name.family-after"), after) > 0 ||
              dict::StartsWithMember(HDTRANS_CLASS("name.rank-after"), after) > 0))) &&
           HonorableKey(snap, key);
}

// Whether a hiragana key starts with a verb or an adjective of the dictionary (いくよ: いく).
bool HiraganaPredicate(const dict::Snapshot& snap, std::string_view key) {
    for (std::size_t n = 0; n < key.size();) {
        n += text::DecodeRune(key.substr(n)).size;
        if (n >= std::string_view("いく").size() && PredicateStem(snap, key.substr(0, n))) return true;
    }
    return false;
}

// Whether a hiragana key may be a name that introduces itself or is called: no word of the
// dictionary (いつか、, ですの) and no word or plain predicate closed by a final particle (ここね、,
// きみか、, いくよ、).
bool HiraganaCall(const dict::Snapshot& snap, std::string_view key) {
    if (snap.DictionaryWord(key)) return false;
    const text::Rune last = RuneBefore(key, key.size());
    const std::string_view rest = key.substr(0, key.size() - last.size);
    return !(InRunes(last.cp, "かよぞぜわねなさ") &&
             (snap.DictionaryWord(rest) || InRunes(RuneBefore(rest, rest.size()).cp, "うくぐすつぬぶむるい")));
}

// Whether the dictionary has s as a common word, a sense that is no name or place and no verb or
// adjective stem (この; not かが 加賀 or みが).
bool CommonKanaWord(const dict::Snapshot& snap, std::string_view s) {
    bool common = false;
    for (const dict::Candidate& c : snap.RetrieveRecords(s, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != s || c.codes->part.Empty()) continue;
        if (c.codes->part.Tag() == 'M' || c.codes->part.Tag() == 'H') continue;
        if (c.codes->part.Tag() != 'N' || (!c.codes->sem.HasPrefix("QPN") && !c.codes->sem.HasCode("QCT") &&
                                            !c.codes->sem.HasCode("QNN") && !c.codes->sem.HasCode("VPl"))) {
            common = true;
        }
    }
    return common;
}

// Whether the hiragana s reads as dictionary words of two kana or more and one-kana particles
// (ここの, はまだ, いくよ), with a final particle only at the end (last).
bool KanaWords(const dict::Snapshot& snap, std::string_view s, bool last) {
    if (s.empty()) return true;
    const text::Rune first = text::DecodeRune(s);
    if (InRunes(first.cp, "はがをにともへでやの") && first.size < s.size() && KanaWords(snap, s.substr(first.size), last)) return true;
    if (last && first.size == s.size() && InRunes(first.cp, "はがをにともへでやのよねなわさぞぜ")) return true;
    for (std::size_t n = first.size; n < s.size();) {
        n += text::DecodeRune(s.substr(n)).size;
        if (snap.DictionaryWord(s.substr(0, n)) && KanaWords(snap, s.substr(n), last)) return true;
    }
    return false;
}

// Whether a hiragana key of the name layer stands alone as a name (spec LEX-4821): a whole run
// of hiragana that no dictionary word spells and that reads as no dictionary words and particles
// (KanaWords: なしこ, こなつ; not ここの, はまだ, いくよ), after the start, a mark,
// kanji or katakana, and before the end, a mark, kanji, katakana or a one-kana particle
// (、つぐみが朝食の席に着く, あいかの方は). A key that starts a verb or an adjective (つぐみ)
// needs a mark, kanji or katakana after its particle too (つぐみが、; not つぐみでありさえ).
bool HiraganaAlone(const dict::Snapshot& snap, std::string_view key, std::string_view after, bool predicate) {
    if (text::RuneCount(key) < 3 || snap.DictionaryWord(key) || KanaWords(snap, key, true)) return false;
    // A person row that writes a title (けいおん 케이온!) names a work, which stands alone only in
    // marks (spec LEX-4).
    for (const dict::Record& r : snap.NameRecords(key)) {
        if (r.codes != nullptr && r.codes->sem.HasPrefix("QPN") && r.target.find_first_of("!?！？") != std::string_view::npos) return false;
    }
    // A drawn interjection or a stammer is no name (まぁこ of まぁこの, そそそ).
    if (internal::AllRunes(key, [&](char32_t c) { return c == FirstRune(key).cp; })) return false;
    for (std::size_t i = 0; i < key.size();) {
        const text::Rune r = text::DecodeRune(key.substr(i));
        if (InRunes(r.cp, "ぁぃぅぇぉ")) return false;
        i += r.size;
    }
    // The run of hiragana reads on as words across the end of the key: words up to a common word that
    // starts in the key and ends after it (よりこの: より この; not つぐみが: みが is a verb stem).
    std::size_t run = 0;
    while (run < after.size() && IsHiragana(text::DecodeRune(after.substr(run)).cp)) run += text::DecodeRune(after.substr(run)).size;
    for (std::size_t p = text::DecodeRune(key).size; p < key.size(); p += text::DecodeRune(key.substr(p)).size) {
        if (!KanaWords(snap, key.substr(0, p), false)) continue;
        for (std::size_t e = 0; e < run;) {
            e += text::DecodeRune(after.substr(e)).size;
            if (CommonKanaWord(snap, std::string(key.substr(p)) + std::string(after.substr(0, e)))) return false;
        }
    }
    // A key that ends like a final particle needs a particle after it: before a mark it may be
    // speech (いくよー！; あかねが is a name).
    const bool final = InRunes(RuneBefore(key, key.size()).cp, "よねなわさぞぜか");
    const char32_t next = FirstRune(after).cp;
    if (after.empty() || !IsHiragana(next)) return !final;
    if (!InRunes(next, "がはもをにとのやでへ")) return false;
    // The particle starts no verb or adjective (まるゆとまったり: 止まったり; but つぐみには, あいかの方は).
    for (std::size_t n = FirstRune(after).size, k = 0; n < after.size() && k < 6; k++) {
        n += text::DecodeRune(after.substr(n)).size;
        if (PredicateStem(snap, after.substr(0, n))) return false;
    }
    const std::string_view rest = after.substr(FirstRune(after).size);
    return !predicate || rest.empty() || !IsHiragana(FirstRune(rest).cp);
}

// Whether what follows a word after a title ends a name there: a mark or the end, a bracket, a
// particle, a copula, a self-introduction, a suffix or an honorific (駆逐艦時雨、, 戦艦大和です;
// not 海防艦計3隻).
bool NameEnd(std::string_view after) {
    if (UtteranceEnd(after) || after.starts_with("「") || after.starts_with("」") || after.starts_with("』") ||
        after.starts_with("だ") || after.starts_with("改")) {
        return true;
    }
    return dict::StartsWithMember(HDTRANS_CLASS("particle.after-name"), after) > 0 ||
           dict::StartsWithMember(HDTRANS_CLASS("name.introduction-copula"), after) > 0 ||
           dict::StartsWithMember(HDTRANS_CLASS("name.suffix-after"), after) > 0 ||
           dict::StartsWithMember(HDTRANS_CLASS("name.class-after"), after) > 0 ||
           dict::StartsWithMember(HDTRANS_CLASS("name.intro-after"), after) > 0 ||
           dict::StartsWithMember(HDTRANS_CLASS("honorific.candidate"), after) > 0;
}

// Whether a role in round brackets follows the word, which opens a caption: a role of noun.role,
// noun.label-role or suffix.label-role or a noun of a person (大泉(俳優)).
bool RoleAfterLabel(const dict::Snapshot& snap, std::string_view before, std::string_view after) {
    if (!internal::CaptionLead(before) || !(after.starts_with("(") || after.starts_with("（"))) return false;
    std::string_view inner = after.substr(after.starts_with("(") ? 1 : std::string_view("（").size());
    inner = inner.substr(0, 4 * static_cast<std::size_t>(internal::kMaxCaptionGroup));
    const std::size_t close = inner.find(after.starts_with("(") ? ")" : "）");
    if (close == std::string_view::npos) return false;
    const std::string_view role = inner.substr(0, close);
    if (dict::InClass(HDTRANS_CLASS("noun.role"), role) || dict::InClass(HDTRANS_CLASS("noun.label-role"), role) ||
        dict::EndsWithMember(HDTRANS_CLASS("suffix.label-role"), role) > 0) {
        return true;
    }
    for (const dict::Candidate& c : snap.RetrieveRecords(role, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.source == role && c.codes->sem.HasCode("VHn")) return true;
    }
    return false;
}

// Whether the word is a speaker label that a colon or ≫ closes, after caption marks only
// (アヤメ：, ⚟アヤメ：, ≫アヤメ：, アヤメ≫).
// The colon must open words of a speech, not a value: a field of a status window (力：35, 敏捷：Ｂ+,
// 力：Ｂ　敏捷：Ａ) is no speaker (StatusFieldAt; spec LEX-5175).

bool ClosedLabelAt(std::string_view before, std::string_view after) {
    if (!internal::CaptionLead(before)) return false;
    if (after.starts_with("≫")) return true;
    return (after.starts_with(":") || after.starts_with("：")) && !StatusFieldAt(after);
}

// A speaker label: the word alone at the start of the line before 「, 『, a colon, ≫ or a role in
// round brackets, after caption marks only (⚟アヤメ：, ≫アヤメ：, 》アヤメ：), or a part of a label in
// round brackets there ((アヤメ), ⚟（アヤメ）).
bool LabelAt(const dict::Snapshot& snap, std::string_view before, std::string_view after) {
    if (internal::BracketLabelAt(before, after) || internal::NameBoxAt(before, after) || ClosedLabelAt(before, after) ||
        RoleAfterLabel(snap, before, after)) {
        return true;
    }
    if (!internal::CaptionLead(before)) return false;
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
           dict::StartsWithMember(HDTRANS_CLASS("name.action-after"), after.substr(std::string_view("、").size())) > 0;
}

// A self-introduction: the word and a copula or よ make the first sentence of the line (睦月です。,
// 陽炎よ。, 「聖女アヤメよ、); a hiragana name may also introduce itself later (だから…まるゆです).
// Later in a line the word answers or exclaims (……あっ！夢です！, ……季節です).
bool IntroductionAt(std::string_view before, std::string_view after, bool hiragana) {
    if (!(hiragana ? UtteranceStart(before) : LineStart(WithoutEnd(WithoutEnd(TrimEndSpaces(before), "「"), "『")))) return false;
    for (const std::string_view copula : dict::ClassMembers(HDTRANS_CLASS("name.introduction-copula"))) {
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

// What ends a clause for PersonActAt: a mark, a bracket, a space or the end of a line.
constexpr std::string_view kClauseEnds = "、。！？!?…‥」』）)「『（(\n 　";
// How far into a clause PersonActIn looks, in characters.
constexpr std::size_t kClauseReach = 24;

// The clause before the word, back to its start or to the end of a condition, a cause or a
// quotation in it (何か言いたげに、口をパクパクさせる; 黙っていれば見目麗しい姫としか思えない: the last
// part only).
std::string_view ClauseBefore(std::string_view s) {
    std::size_t i = s.size();
    while (i > 0) {
        const text::Rune r = RuneBefore(s, i);
        if (InRunes(r.cp, kClauseEnds)) break;
        i -= r.size;
    }
    std::string_view clause = s.substr(i);
    for (std::size_t j = 0; j < clause.size();) {
        if (const std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.person-clause-stop"), clause.substr(j)); n > 0) {
            clause = clause.substr(j + n);
            j = 0;
            continue;
        }
        j += text::DecodeRune(clause.substr(j)).size;
    }
    return clause;
}

// Whether a member of name.person-part starts s and a particle or a mark follows it (顔を, 手が,
// 顔。; not 手紙, 手ほどき).
bool PersonPartAt(std::string_view s) {
    const std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.person-part"), s);
    if (n == 0) return false;
    const std::string_view rest = s.substr(n);
    return rest.empty() || InRunes(FirstRune(rest).cp, "がをにはもでとのへや、。！？!?…」』") ;
}

// The kana that end a verb stem or a past before a noun of one kanji, which then ends a word of
// the stem (効き目, 見た目, 聞き手), not a part of the body.
constexpr std::string_view kStemEnds = "きしちひみりぎびいけせめれげべえた";

// How the word stands before the clause that PersonActIn reads.
enum class Role {
    Subject,  // with が
    Topic,    // with は or も
    // With は or も on a kanji word that has a person's name row and no common sense of time, a
    // statement, a name, a thought or money (梨花は言った; not 昼間も言った, 言葉は聞いた, 本名は知らない):
    // a verb of saying or thinking makes it a person too (name.person-speak; spec LEX-4830).
    NamedTopic,
    Other,    // called with 、, or a sentence end after the clause
    // As Other, but set off by 、 after another 、 of the sentence, where the word may be one of a list
    // (りんご、みかん、バナナを買った): only a verb that takes no object.
    Aside,
    // As Topic, but only for what only a person does: no verb of saying or handling after an object (a
    // common word of one kanji: 樹は笑った, not 車はじゃり道の上をがりがり言わせて).
    Act,
};

// Whether the clause at the start of s holds what only a person does: a verb of name.person-act;
// a part of the body of the person's own, after no の, kanji or verb stem, before を, or before が after a
// topic (身をよじる, 顔が赤い; not その手を断ち切る); a verb of name.person-say after a subject or
// after an object in を; or a verb of name.person-transitive after an object in を (小太刀はスイッチを
// 入れた), going on as no wish or passive (見たい, 知られ). The clause ends at a mark, a bracket, a
// member of name.person-clause-stop and, with clause, at the next subject a kanji or katakana word
// with が or は opens (泉がある場所で彼女は).
// How far after a verb a simile ending may come (泣いているようだ), in characters.
constexpr std::size_t kSimileReach = 8;

// Whether a simile ending (よう, みたい) follows soon after a verb, before a clause end.
bool SimileAfter(std::string_view s) {
    for (std::size_t i = 0, runes = 0; i < s.size() && runes < kSimileReach; runes++) {
        const std::string_view rest = s.substr(i);
        if (rest.starts_with("よう") || rest.starts_with("みたい")) return true;
        const text::Rune r = text::DecodeRune(rest);
        if (InRunes(r.cp, kClauseEnds)) return false;
        i += r.size == 0 ? 1 : r.size;
    }
    return false;
}

// A form of a verb of name.person-verb and what it asks of the clause.
struct PersonVerb {
    // A verb that takes an object, which may stand as the topic (礼は受け取ろう, 量は食べられない): after
    // は, も or 、 it is a cue only after an object in を in its clause.
    bool transitive = false;
    // A verb of saying: as the verbs of name.person-speak, a cue after a named topic with no object.
    bool say = false;
    // What may follow the form: anything (語った, 語りながら), a mark or a kanji (a stem: 語り、, 見つめ返す),
    // or the end of an utterance (a command: 黙れ！, not the potential 黙れる).
    enum class Follow { Any, Stem, Command } follow = Follow::Any;
    // The dictionary form or the past, which a noun after it makes a relative clause (仕事に出かける母親).
    bool final = false;
    // A verb of moving on one's own (近づく, 逃げる, 走り出す), which a vehicle or an animal does too: no
    // cue for a key with such a common sense nor for a common word of one kanji (車が近づいてくる).
    bool move = false;
};

// The forms of the verbs of name.person-verb, by text, and their passives (笑われ, 呆れられ). Each row
// is a lemma with its kind in the note (v5 godan, v1 ichidan, vs a verb in する) and the flags t
// (transitive), say and move; the one conjugation table (word::Conjugation) makes its active forms
// only: the dictionary form, the past, the te form, ちゃ, the conditional, the volitional, the
// causative, the stem before ます, ながら, そう, つつ or がち, the negative stem before ない, ず, ん or
// ぬ, and the commands. No passive or potential stem is a form, so 見る gives no 見え or 見られ
// (姿が見えない).
struct PersonVerbForms {
    std::unordered_map<std::string, PersonVerb> forms;
    std::unordered_map<std::string, bool> passives;
    std::vector<std::size_t> sizes;  // the sizes of forms in bytes, largest first
    std::vector<std::size_t> passiveSizes;
};

// The conjugation kind of lemma for the kind in its note and its flags; -1 for a note that does not
// parse: no kind, an unknown flag, or a lemma that does not end as its kind does (PersonVerbProblems).
int PersonVerbKind(std::string_view lemma, std::string_view note, PersonVerb& v) {
    const std::size_t space = note.find(' ');
    const std::string_view type = note.substr(0, space);
    int kind = -1;
    if (type == "v1") kind = 7;
    if (type == "vs") kind = 9;
    if (type == "v5") {
        constexpr std::string_view kEnds[] = {"う", "く", "ぐ", "す", "つ", "む", "ぶ", "", "る", "", "", "", "ぬ"};
        for (int k = 0; k < static_cast<int>(std::size(kEnds)) && kind < 0; k++) {
            if (!kEnds[k].empty() && lemma.ends_with(kEnds[k])) kind = k;
        }
        if (lemma.ends_with("行く") || lemma.ends_with("いく")) kind = word::kKindIku;
    }
    const std::string_view ending = kind < 0 ? std::string_view() : word::Conjugation(kind, 0);
    if (ending.empty() || !lemma.ends_with(ending) || lemma.size() == ending.size()) return -1;
    v = PersonVerb();
    std::string_view flags = space == std::string_view::npos ? std::string_view() : note.substr(space + 1);
    while (!flags.empty()) {
        const std::size_t end = flags.find(' ');
        const std::string_view flag = flags.substr(0, end);
        if (flag == "t") {
            v.transitive = true;
        } else if (flag == "say") {
            v.say = true;
        } else if (flag == "move") {
            v.move = true;
        } else {
            return -1;
        }
        flags = end == std::string_view::npos ? std::string_view() : flags.substr(end + 1);
    }
    return kind;
}

const PersonVerbForms& PersonVerbTable() {
    static const PersonVerbForms table = [] {
        PersonVerbForms t;
        const auto members = dict::ClassMembers(HDTRANS_CLASS("name.person-verb"));
        const auto notes = dict::ClassNotes(HDTRANS_CLASS("name.person-verb"));
        for (std::size_t k = 0; k < members.size() && k < notes.size(); k++) {
            const std::string_view lemma = members[k];
            PersonVerb v;
            const int kind = PersonVerbKind(lemma, notes[k], v);
            // A row that does not parse is no verb here; the unit test of PersonVerbProblems fails on it.
            if (kind < 0) continue;
            const std::string stem(lemma.substr(0, lemma.size() - word::Conjugation(kind, 0).size()));
            const auto add = [&](std::string form, PersonVerb::Follow follow, bool final = false) {
                if (form.find('(') != std::string::npos || form.find("XX") != std::string::npos) return;
                PersonVerb f = v;
                f.follow = follow;
                f.final = final;
                const auto [it, fresh] = t.forms.emplace(std::move(form), f);
                if (!fresh) {
                    // Two lemmas of one form: the looser of the two.
                    it->second.transitive = it->second.transitive && f.transitive;
                    it->second.say = it->second.say || f.say;
                    it->second.move = it->second.move || f.move;
                    it->second.follow = std::min(it->second.follow, f.follow);
                    it->second.final = it->second.final && f.final;
                }
            };
            const auto cell = [&](int branch) { return std::string(word::Conjugation(kind, branch)); };
            for (const int branch : {0, 3, 5, 6, 7, 17}) {
                if (!cell(branch).empty() || branch == 0) add(stem + cell(branch), PersonVerb::Follow::Any, branch == 0 || branch == 6);
            }
            // The causative: one who has someone do it is a person too (腕を引いて立ち上がらせる). It takes an
            // object, so after は it needs one in を (not ボケはてめーだよ…笑わせんな).
            const bool transitive = v.transitive;
            v.transitive = true;
            add(kind == 9 ? stem + "させ" : stem + cell(2), PersonVerb::Follow::Any);
            v.transitive = transitive;
            const std::string masu = stem + cell(10);
            for (const std::string_view tail : {"ま", "なが", "そう", "つつ", "がち"}) add(masu + std::string(tail), PersonVerb::Follow::Any);
            if (text::RuneCount(masu) >= 2) add(masu, PersonVerb::Follow::Stem);
            const std::string neg = stem + cell(8);
            add(neg + "な", PersonVerb::Follow::Any);
            add(stem + cell(9), PersonVerb::Follow::Any);
            if (kind != 9) {
                add(neg + "ん", PersonVerb::Follow::Any);
                add(neg + "ぬ", PersonVerb::Follow::Any);
            }
            for (const int branch : {11, 12}) {
                if (!cell(branch).empty()) add(stem + cell(branch), PersonVerb::Follow::Command);
            }
            const std::string passive = kind == 9 ? stem + "され" : stem + cell(1);
            if (passive.find('(') == std::string::npos && passive.size() > stem.size()) t.passives.emplace(passive, true);
        }
        for (const auto& [form, v] : t.forms) t.sizes.push_back(form.size());
        for (const auto& [form, v] : t.passives) t.passiveSizes.push_back(form.size());
        for (auto* sizes : {&t.sizes, &t.passiveSizes}) {
            std::sort(sizes->begin(), sizes->end(), std::greater<>());
            sizes->erase(std::unique(sizes->begin(), sizes->end()), sizes->end());
        }
        return t;
    }();
    return table;
}

// The byte length of the noun of a person that starts s (母親, 少女), 0 for none: the longest word of kanji or
// katakana there that has a common sense of a person.
std::size_t PersonNounLength(const dict::Snapshot& snap, std::string_view s) {
    std::size_t end = 0;
    for (std::size_t runes = 0; end < s.size() && runes < 4; runes++) {
        const text::Rune r = text::DecodeRune(s.substr(end));
        if (!IsKanji(r.cp) && !IsKatakana(r.cp)) break;
        end += r.size;
    }
    for (std::size_t n = end; n > 0; n -= RuneBefore(s, n).size) {
        const std::string_view word = s.substr(0, n);
        bool found = false;
        for (const dict::Candidate& c : snap.RetrieveRecords(word, dict::LookupMode::General)) {
            if (c.codes == nullptr || c.source != word || c.codes->sem.HasPrefix("QPN")) continue;
            if (c.codes->sem.HasCode("VHn")) return n;
            found = true;
        }
        // Only the longest word: 様子 is no 様.
        if (found) return 0;
    }
    return 0;
}

bool PersonNounAt(const dict::Snapshot& snap, std::string_view s) { return PersonNounLength(snap, s) > 0; }

// What a form of a command may stand before: the end of the utterance (黙れ！, 黙れよ, 黙れっ).
constexpr std::string_view kCommandEnds = "、。！？!?…‥」』）)\n 　ーっよ〜";

// The longest form of a verb of name.person-verb that s starts with, where its ending allows (a
// wish such as 見たい or a state in てある is no act: its object takes が; nor is an act asked of another
// in てほしい), and its size in bytes; 0 for none.
std::size_t PersonVerbAt(std::string_view s, PersonVerb& verb) {
    const PersonVerbForms& t = PersonVerbTable();
    for (const std::size_t size : t.sizes) {
        if (size > s.size()) continue;
        const auto it = t.forms.find(std::string(s.substr(0, size)));
        if (it == t.forms.end()) continue;
        const std::string_view tail = s.substr(size);
        const char32_t next = tail.empty() ? 0 : FirstRune(tail).cp;
        switch (it->second.follow) {
            case PersonVerb::Follow::Any:
                if (it->first.ends_with("た")) {
                    bool wish = false;
                    for (const std::string_view w : {"い", "く", "かっ", "かろ", "がる", "がっ", "がら", "がり", "けれ", "さ"}) wish = wish || tail.starts_with(w);
                    if (wish) continue;
                }
                break;
            case PersonVerb::Follow::Stem:
                if (next != U'、' && !IsKanji(next)) continue;
                break;
            case PersonVerb::Follow::Command:
                if (next != 0 && !InRunes(next, kCommandEnds)) continue;
                break;
        }
        // てほしい asks the act of another (見て欲しけりゃ…しとけボケ).
        if ((it->first.ends_with("て") || it->first.ends_with("で")) && dict::StartsWithMember(HDTRANS_CLASS("ending.te-wish"), tail) > 0) continue;
        // てある makes the object the subject (名前が書いてある).
        if (it->second.transitive && (it->first.ends_with("て") || it->first.ends_with("で")) && tail.starts_with("あ") &&
            !tail.starts_with("あげ")) {
            continue;
        }
        verb = it->second;
        return size;
    }
    return 0;
}

// Whether s starts with the passive of a verb of name.person-verb (笑われ, 呆れられ).
bool PersonVerbPassiveAt(std::string_view s) {
    const PersonVerbForms& t = PersonVerbTable();
    for (const std::size_t size : t.passiveSizes) {
        if (size <= s.size() && t.passives.contains(std::string(s.substr(0, size)))) return true;
    }
    return false;
}

// With mover, the key has a common sense of a vehicle or an animal (MoverKey): a verb of moving on one's own
// is no cue for it, nor for a common word of one kanji (Role::Act).
bool PersonActIn(std::string_view s, Role role, bool clause, const dict::Snapshot* snap = nullptr, bool mover = false) {
    bool object = false;
    bool subject = false;
    std::size_t runes = 0;
    for (std::size_t i = 0; i < s.size() && runes < kClauseReach; runes++) {
        const std::string_view rest = s.substr(i);
        const char32_t prev = i == 0 ? 0 : RuneBefore(s, i).cp;
        if (const std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.body-part"), rest);
            n > 0 && role != Role::Act && !IsKanji(prev) && prev != U'の' && !(text::RuneCount(rest.substr(0, n)) == 1 && InRunes(prev, kStemEnds)) &&
            (rest.substr(n).starts_with("を") || ((role == Role::Topic || role == Role::NamedTopic) && rest.substr(n).starts_with("が")))) {
            return true;
        }
        // A verb starts a word: not after a kanji (世話して is no 話し).
        const bool wordStart = !IsKanji(prev) || !IsKanji(FirstRune(rest).cp);
        if (const std::size_t act = wordStart ? dict::StartsWithMember(HDTRANS_CLASS("name.person-act"), rest) : 0; act > 0) {
            // For a common word of one kanji, an act in a simile (囁くように揺れた, 泣いているようだった) is
            // no act of its own.
            if (role != Role::Act || !SimileAfter(rest.substr(act))) return true;
        }
        PersonVerb verb;
        if (const std::size_t n = wordStart ? PersonVerbAt(rest, verb) : 0; n > 0) {
            const std::string_view tail = rest.substr(n);
            // After は, も or 、 a verb that takes an object may have the word as its object (礼は受け取ろう),
            // unless an object in を stands before it; a named topic may say with none (高峰は語った).
            // Before the word at a sentence end, the word is the object only with a subject in the clause (俺が
            // 見つめる千里; not 涙声で尋ねる千里).
            const bool objectTopic = verb.transitive && role != Role::Subject && !object && !(verb.say && role == Role::NamedTopic) &&
                                     (clause || subject);
            // A topic is no subject of a relative clause on a noun of a person, its own subject (昼間は仕事に出かける
            // 母親; not 千里は驚く様子もなく).
            const bool relative = role != Role::Subject && verb.final && snap != nullptr && PersonNounAt(*snap, tail);
            // A stem right after X、 lists nouns (風呂、着替え、洗濯物).
            const bool listed = (role == Role::Other || role == Role::Aside) && i == 0 && verb.follow == PersonVerb::Follow::Stem;
            const bool moved = verb.move && (mover || role == Role::Act);
            if (!objectTopic && !relative && !listed && !moved && !((role == Role::Act || role == Role::Aside) && verb.transitive) &&
                !(role == Role::Act && SimileAfter(tail))) {
                return true;
            }
        }
        object = object || (i > 0 && rest.starts_with("を"));
        subject = subject || (i > 0 && rest.starts_with("が"));
        if (wordStart && role == Role::NamedTopic && dict::StartsWithMember(HDTRANS_CLASS("name.person-speak"), rest) > 0) return true;
        for (const bool say : {true, false}) {
            if (!wordStart || role == Role::Act) break;
            const std::size_t n = dict::StartsWithMember(say ? HDTRANS_CLASS("name.person-say") : HDTRANS_CLASS("name.person-transitive"), rest);
            if (n == 0 || !(object || (say && role == Role::Subject))) continue;
            const std::string_view member = rest.substr(0, n);
            const std::string_view tail = rest.substr(n);
            const bool wish = member.ends_with("た") &&
                              (tail.starts_with("い") || tail.starts_with("く") || tail.starts_with("か") || tail.starts_with("が"));
            const bool passive = member.ends_with("ら") && tail.starts_with("れ");
            if (!wish && !passive) return true;
        }
        const text::Rune r = text::DecodeRune(rest);
        // After Xが a comma that closes an adverb in に keeps the clause (凪がこれ見よがしに、俺と腕を組む).
        if (r.cp == U'、' && role == Role::Subject && prev == U'に') {
            i += r.size;
            continue;
        }
        // A simile in ように goes on to the act it describes (からかうように笑った).
        if (InRunes(r.cp, kClauseEnds) ||
            (dict::StartsWithMember(HDTRANS_CLASS("name.person-clause-stop"), rest) > 0 && !(role == Role::Act && rest.starts_with("ように")))) {
            return false;
        }
        if (clause && i > 0 && (r.cp == U'が' || r.cp == U'は') && (IsKanji(prev) || IsKatakana(prev))) return false;
        i += r.size == 0 ? 1 : r.size;
    }
    return false;
}

// Whether s starts with a request in て that calls on the one named before it (教えてよ, 見てください).
bool RequestAt(std::string_view s) {
    std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.person-transitive"), s);
    if (n == 0) n = dict::StartsWithMember(HDTRANS_CLASS("name.person-say"), s);
    std::string_view tail = s.substr(n);
    if (n == 0 || !(s.substr(0, n).ends_with("て") || tail.starts_with("て"))) return false;
    if (tail.starts_with("て")) tail.remove_prefix(std::string_view("て").size());
    return tail.empty() || tail.starts_with("よ") || tail.starts_with("くだ") || tail.starts_with("くれ") || UtteranceEnd(tail);
}

// Whether the word is called at the end of an utterance after a sentence of two characters or more
// that a final particle closes and that does not hold the word (イクよ、皐月っ！, 待ってね、玉藻; not
// な、中っ, みんな、 or 夢だよ、夢): drawn out
// or not, a mark or the end follows it.
bool FinalCallAt(std::string_view key, std::string_view before, std::string_view after) {
    std::string_view b = TrimEndSpaces(before);
    if (!b.ends_with("、")) return false;
    b.remove_suffix(std::string_view("、").size());
    const text::Rune particle = RuneBefore(b, b.size());
    if (!InRunes(particle.cp, "よねなぞぜわ")) return false;
    const std::string_view head = b.substr(0, b.size() - particle.size);
    // A word said again after the sentence repeats it (夢だよ、夢).
    if (ClauseBefore(head).find(key) != std::string_view::npos) return false;
    if (head.empty() || InRunes(RuneBefore(head, head.size()).cp, "んン") || InRunes(RuneBefore(head, head.size()).cp, kOpens) ||
        InRunes(RuneBefore(head, head.size()).cp, "、 　")) {
        return false;
    }
    while (after.starts_with("っ") || after.starts_with("ー") || after.starts_with("～") || after.starts_with("〜")) {
        after.remove_prefix(FirstRune(after).size);
    }
    return after.empty() || after.starts_with("！") || after.starts_with("？") || after.starts_with("!") || after.starts_with("?") ||
           after.starts_with("」") || after.starts_with("…");
}

// Whether the name layer reads key as a person (善也, 清香).
bool LayerPersonKey(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Record& r : snap.NameRecords(key)) {
        if (r.codes != nullptr && r.codes->sem.HasPrefix("QPN")) return true;
    }
    return false;
}

// Whether key is called alone: the whole of an utterance, at its start or after a call word and a space
// (おい, ねえ, なあ, じゃあな, またな, じゃあね: じゃあな 匡｡, おい 航太…), and before a question or
// exclamation mark, an ellipsis or the end (航太？); not before more words (隆 この間の…).
bool AloneCallAt(std::string_view before, std::string_view after, bool common) {
    std::string_view b = TrimEndSpaces(before);
    const bool spaced = b.size() < before.size();
    // A word the dictionary also reads as a common word, alone at the start, may be said back as a
    // question (「対立？) or mused on (夢…): only a call word before it calls it.
    bool start = !common && (b.empty() || InRunes(RuneBefore(b, b.size()).cp, "「『（(【\n"));
    if (!start && spaced) {
        std::size_t k = b.size();
        while (k > 0 && !InRunes(RuneBefore(b, k).cp, "「『（(【 　。！？!?…\n")) k -= RuneBefore(b, k).size;
        const std::string_view word = b.substr(k);
        start = dict::InClass(HDTRANS_CLASS("name.call-word"), word);
    }
    if (!start) return false;
    while (after.starts_with("っ") || after.starts_with("ー") || after.starts_with("～") || after.starts_with("〜")) {
        after.remove_prefix(FirstRune(after).size);
    }
    return after.empty() || after.starts_with("！") || after.starts_with("？") || after.starts_with("!") || after.starts_with("?") ||
           after.starts_with("…") || after.starts_with("‥") || after.starts_with("｡") || after.starts_with("。");
}

// Whether key is a single kanji that the name layer reads as a person (南 미나미, 栞 시오리, 司 츠카사) and no side
// or position of name.side-word (左, 前), whose
// person cues hold in their line (spec LEX-4832); a single character is else no name a cue shows.
bool SingleNameKey(const dict::Snapshot& snap, std::string_view key) {
    if (text::RuneCount(key) != 1 || !internal::AllRunes(key, IsKanji) || dict::InClass(HDTRANS_CLASS("name.side-word"), key)) return false;
    for (const dict::Record& r : snap.NameRecords(key)) {
        if (r.codes != nullptr && r.codes->sem.HasPrefix("QPN")) return true;
    }
    return false;
}

// Whether key, one kanji, stands in a lenticular box before speech as a heading or a word: it is of
// noun.bracket-note (【完】), or has a common sense and no person's reading in the name layer or is of
// noun.box-heading (【新】「春の新作」).
// A key with a person's reading names the speaker in a box of one, in its line only (【凪】「うん」, 【幸】「ねえ」;
// spec LEX-5941, LEX-6060).
bool HeadingBoxAt(const dict::Snapshot& snap, std::string_view key, std::string_view before, std::string_view after) {
    return text::RuneCount(key) < 2 && internal::NameBoxAt(before, after) && !internal::BracketLabelAt(before, after) &&
           ((!NameOnlyKey(snap, key) && (!SingleNameKey(snap, key) || dict::InClass(HDTRANS_CLASS("noun.box-heading"), key))) ||
            dict::InClass(HDTRANS_CLASS("noun.bracket-note"), key));
}

// Whether a person cue of key may be a figure of speech (瞳が見つめる, 心が泣く, 花が笑う, 夢が叫ぶ,
// 未来が微笑む): key is a single character that is no SingleNameKey, a member of name.personified-word, or a word the dictionary
// reads as a part of the body, a colour, a feeling, weather, the sky or a place.
bool FigureKey(const dict::Snapshot& snap, std::string_view key) {
    const bool single = text::RuneCount(key) < 2;
    if ((single && !SingleNameKey(snap, key)) || dict::InClass(HDTRANS_CLASS("name.personified-word"), key)) return true;
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        // The senses of a proper noun, a person or a place (千里 치사토, 高峰 타카미네), are no common word.
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN") || c.codes->sem.HasCode("QCT") ||
            c.codes->sem.HasCode("QNN")) {
            continue;
        }
        for (const std::string_view code : {"VBp", "UCl", "UFl", "VFr", "VSs"}) {
            if (c.codes->sem.HasCode(code)) return true;
        }
        // A single kanji is a figure as a plant, an animal or a time too (花が笑う, 雀が鳴く), but not as a
        // direction (南が言った), which a place sense of it does not undo.
        if (single) {
            // A plant that the name layer has as a person is a person doing what only people do (樹はからかう
            // ように笑った, 桜が言った; spec LEX-5175); 花 and the like are name.personified-word.
            for (const std::string_view code : {"QAL", "VPt", "QAA", "QAB", "UMe"}) {
                if (code == "VPt" && SingleNameKey(snap, key)) continue;
                if (c.codes->sem.HasCode(code)) return true;
            }
            if (c.codes->sem.HasCode("VPl") && !c.codes->sem.HasCode("UDr")) return true;
        } else if (c.codes->sem.HasCode("VPl")) {
            return true;
        }
    }
    return false;
}

// Whether key has a common sense of a vehicle or an animal (車 차, 小舟 작은 배), which moves on its own as a
// person does (PersonVerb::move).
bool MoverKey(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN") || c.codes->sem.HasCode("QCT") ||
            c.codes->sem.HasCode("QNN")) {
            continue;
        }
        for (const std::string_view code : {"VTp", "QAL", "QAA", "QAB"}) {
            if (c.codes->sem.HasCode(code)) return true;
        }
    }
    return false;
}

// Whether a verb of saying or thinking after Xは makes the kanji word X a person (Role::NamedTopic): X has a
// person's name row and no common sense of time, a statement, a name, a thought or money, which is what
// is said or when (昼間も言った, 言葉は聞いた, 本名は, 利子は).
bool SpeakingTopic(const dict::Snapshot& snap, std::string_view key) {
    if (!internal::AllRunes(key, IsKanji)) return false;
    bool row = false;
    for (const dict::Record& r : snap.NameRecords(key)) row = row || (r.codes != nullptr && r.codes->sem.HasPrefix("QPN"));
    if (!row) return false;
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN")) continue;
        for (const std::string_view code : {"UMe", "UTs", "UQf", "UTh", "VMo"}) {
            if (c.codes->sem.HasCode(code)) return false;
        }
    }
    return true;
}

// A predicate of a person (spec LEX-4820): the word with の before a part of the body or the face
// (皐月の顔), as the subject of が, は or も, or set off by 、 before a clause, of what only a person
// does (玉藻がはにかむ, 小太刀が黙然と腕を組んだ, 弥生、怒ってない), called with a request (ゴーヤー、教えて)
// or, with two characters or more, after a final particle (イクよ、皐月っ！; not だったね、海), or
// closing a sentence after a clause of what a person does (口をパクパクさせる玉藻。). The key has two
// characters or more, its common word names no person or animal and is no word a figure of speech
// makes a person (FigureKey), and no verb starts with it (HonorableKey).
// Whether the clause at the start of s ends in a verb of saying or thinking (name.person-speak) that no
// causative follows (言わせ).
bool SingleSayer(std::string_view s) {
    std::size_t runes = 0;
    for (std::size_t i = 0; i < s.size() && runes < kClauseReach; runes++) {
        const std::string_view rest = s.substr(i);
        const std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.person-speak"), rest);
        if (n > 0) return !rest.substr(n).starts_with("せ") && !rest.substr(n).starts_with("さ");
        const text::Rune r = text::DecodeRune(rest);
        if (InRunes(r.cp, kClauseEnds) || dict::StartsWithMember(HDTRANS_CLASS("name.person-clause-stop"), rest) > 0) return false;
        i += r.size == 0 ? 1 : r.size;
    }
    return false;
}

// Whether katakana key's common word is an animal and no person (アリ 개미, サイ 코뿔소), which a person
// name of the name layer also spells.
bool KatakanaAnimalKey(const dict::Snapshot& snap, std::string_view key) {
    if (text::RuneCount(key) < 2 || !internal::AllRunes(key, IsKatakana)) return false;
    bool animal = false;
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN")) continue;
        if (c.codes->sem.HasCode("VHn") || c.codes->sem.HasCode("QNP")) return false;
        for (const std::string_view code : {"QAA", "QAB", "QAC", "QAD", "QAE", "QAF", "QAG", "QAH"}) animal = animal || c.codes->sem.HasCode(code);
    }
    return animal;
}

// How far after a person act a passive られ may come (呆れられる), in bytes.
constexpr std::size_t kPassiveReach = 12;

// Whether after makes the word one that speaks (樹の言うとおり: の and name.person-speak) or one whose act
// befalls someone (樹に呆れられる: に, a person act and られ).
bool SpeakerOrAgentAt(std::string_view after) {
    if (after.starts_with("の")) {
        return dict::StartsWithMember(HDTRANS_CLASS("name.person-speak"), after.substr(std::string_view("の").size())) > 0;
    }
    if (!after.starts_with("に")) return false;
    const std::string_view rest = after.substr(std::string_view("に").size());
    const std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.person-act"), rest);
    return (n > 0 && rest.substr(n, kPassiveReach).find("られ") != std::string_view::npos) || PersonVerbPassiveAt(rest);
}

// Whether the common word key reads as an adverb (大方 대부분), which sets off a clause with 、 as no
// call does (大方、味方を見捨てて逃げたんだろう).
bool AdverbKey(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.source == key && c.codes->partText.starts_with("E")) return true;
    }
    return false;
}

// Whether s ends in a command or a request (戻れ, 落ち着け, 来い, 発言しろ, 手伝ってくれ, 待ってください), before
// よ, っ, ー or 〜: a command of the one conjugation table after a verb stem of the dictionary that starts with a
// kanji, しろ or せよ, 来い or こい after て, or a request in てくれ, てください, てちょうだい or なさい.
// With negative set, a negative request counts too (行かないで); with te set, a request in a bare te form (そこをどいて,
// 待って, 読んで), but no adjective's くて.
bool CommandEndIn(const dict::Snapshot& snap, std::string_view s, bool negative = false, bool te = false) {
    for (bool more = true; more;) {
        more = false;
        for (const std::string_view t : {"よ", "っ", "ー", "〜", "～"}) {
            if (s.ends_with(t)) {
                s.remove_suffix(t.size());
                more = true;
            }
        }
    }
    for (const std::string_view t : dict::ClassMembers(HDTRANS_CLASS("ending.command-request"))) {
        if (s.ends_with(t) && s.size() > t.size()) return true;
    }
    if (s.ends_with("てこい") || s.ends_with("でこい")) return true;
    // A negative request (行かないで, 忘れないで).
    if (negative && s.ends_with("ないで") && s.size() > std::string_view("ないで").size()) return true;
    if (te && s.size() > std::string_view("て").size() * 2) {
        if (s.ends_with("んで") || s.ends_with("いで")) return true;
        if (s.ends_with("て")) {
            const char32_t prev = RuneBefore(s, s.size() - std::string_view("て").size()).cp;
            if (prev != U'く' && (IsHiragana(prev) || IsKanji(prev))) return true;
        }
    }
    // The command ending is the last one or two kana; the stem goes back to a kanji (戻れ, 落ち着け: 着).
    std::size_t q = s.size();
    for (int endRunes = 1; endRunes <= 2 && q > 0 && IsHiragana(RuneBefore(s, q).cp); endRunes++) {
        q -= RuneBefore(s, q).size;
        const std::string_view ending = s.substr(q);
        std::size_t j = q;
        for (int stemRunes = 1; stemRunes <= 4 && j > 0; stemRunes++) {
            const char32_t c = RuneBefore(s, j).cp;
            if (!IsKanji(c) && !IsHiragana(c)) break;
            j -= RuneBefore(s, j).size;
            if (!IsKanji(c)) continue;
            const std::string_view stem = s.substr(j, q - j);
            for (const dict::Candidate& r : snap.RetrieveRecords(stem, dict::LookupMode::General)) {
                if (r.codes == nullptr || r.source != stem || r.codes->partText.size() < 4 || !r.codes->partText.starts_with("M4")) continue;
                const int kind = r.codes->partText[3] - '0';
                for (const int branch : {11, 12}) {
                    if (word::Conjugation(kind, branch) == ending) return true;
                }
            }
        }
    }
    return false;
}

// What closes an utterance after a clause: a mark of its end or a closing bracket.
constexpr std::string_view kUtteranceCloses = "」』！？!?。…‥\n";

// Whether s, after X、 at the start of an utterance, is one clause up to the end of the utterance that calls on
// X: a command or a request (小太刀、いい加減仕事に戻れ, 高峰、ちょっと手伝ってくれ), or, where question is set, a
// question (筧、今まで読んだ本の数は？, 凪、お茶でも飲むか？; spec LEX-4820). A question with no object of its own
// may ask about X (スイカ、食べる？, スイカ、俺も食べる？); 何 before a verb of doing is its object with を
// dropped (高峰、何してるんだ？). A bare te form asks with te set (千里、そこをどいて), for a key not of hiragana,
// which may be a phrase of its own (ここね、待って), nor a katakana word of the dictionary, which may be what is
// offered (チョコ、よかったら召し上がって). Otherwise only a word of time, which nobody eats or calls, is called
// there (睦月、呼んだ？), with time set.
bool CalledClauseAt(const dict::Snapshot& snap, std::string_view s, bool question, bool time = false, bool te = false) {
    std::size_t i = 0;
    for (std::size_t runes = 0; i < s.size() && runes < kClauseReach; runes++) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        if (InRunes(r.cp, kClauseEnds)) break;
        i += r.size == 0 ? 1 : r.size;
    }
    if (i == 0 || (i < s.size() && !InRunes(FirstRune(s.substr(i)).cp, kUtteranceCloses))) return false;
    const std::string_view clause = s.substr(0, i);
    const std::string_view close = s.substr(i);
    if (question) {
        if (!(close.starts_with("？") || close.starts_with("?")) || !IsHiragana(RuneBefore(clause, clause.size()).cp)) return false;
        // An object in を or でも, or a noun asked about in は (本の数は？).
        if (time || clause.ends_with("は") || clause.find("を") != std::string_view::npos) return true;
        for (const std::string_view what : dict::ClassMembers(HDTRANS_CLASS("name.what-doing"))) {
            const std::size_t at = clause.find(what);
            if (at != std::string_view::npos && !clause.substr(at + what.size()).starts_with("ら")) return true;
        }
        const std::size_t demo = clause.find("でも");
        return demo != std::string_view::npos && demo > 0;
    }
    return CommandEndIn(snap, clause, te, te);
}

// Whether key has a common sense of a time the dictionary has as a common noun (昼間 낮, 今日 오늘), which a question
// after 、 asks about (昼間、何してた？); a month (睦月 1월) is a proper noun of time and no such word.
bool CommonTimeKey(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN")) continue;
        if (c.codes->sem.HasCode("UMe") && c.codes->partText.size() >= 3 && c.codes->partText[2] == '1') return true;
    }
    return false;
}

// Whether key has a common sense of time (睦月 1월).
bool TimeKey(const dict::Snapshot& snap, std::string_view key) {
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.source == key && !c.codes->sem.HasPrefix("QPN") && c.codes->sem.HasCode("UMe")) return true;
    }
    return false;
}

// Whether before is the start of an utterance, also after a call word and 、 (「おい、筧、).
bool CallStart(std::string_view before) {
    if (UtteranceStart(before)) return true;
    std::string_view b = TrimEndSpaces(before);
    if (!b.ends_with("、")) return false;
    b.remove_suffix(std::string_view("、").size());
    std::size_t k = b.size();
    while (k > 0 && !InRunes(RuneBefore(b, k).cp, kOpens) && !InRunes(RuneBefore(b, k).cp, "、 　")) k -= RuneBefore(b, k).size;
    return k < b.size() && dict::InClass(HDTRANS_CLASS("name.call-word"), b.substr(k)) && UtteranceStart(b.substr(0, k));
}

// Whether the word is called after a command at the end of an utterance (「まあ、落ち着け小太刀。」, 戻れ、高峰！).
bool CommandCallAt(const dict::Snapshot& snap, std::string_view before, std::string_view after) {
    if (!(after.empty() || InRunes(FirstRune(after).cp, kUtteranceCloses))) return false;
    std::string_view b = TrimEndSpaces(before);
    if (b.ends_with("、")) b.remove_suffix(std::string_view("、").size());
    return !b.empty() && IsHiragana(RuneBefore(b, b.size()).cp) && CommandEndIn(snap, ClauseBefore(b), true);
}

// Whether the word is called after a stutter of its first syllable at the start of an utterance (「……ち、千里？」):
// one kana and 、 before it, whose Hangul starts the reading of a person's name row of key, and a mark or the end
// after it.
bool StutterCallAt(const dict::Snapshot& snap, std::string_view key, std::string_view before, std::string_view after) {
    if (!(after.empty() || InRunes(FirstRune(after).cp, "」』！？!?…‥"))) return false;
    std::string_view b = before;
    if (!b.ends_with("、")) return false;
    b.remove_suffix(std::string_view("、").size());
    const text::Rune kana = RuneBefore(b, b.size());
    if (!IsHiragana(kana.cp) && !IsKatakana(kana.cp)) return false;
    std::string_view head = b.substr(0, b.size() - kana.size);
    // The stutter may repeat (た、た、高峰).
    const std::string again = std::string(b.substr(b.size() - kana.size)) + "、";
    while (head.ends_with(again)) head.remove_suffix(again.size());
    while (head.ends_with("…") || head.ends_with("‥")) head.remove_suffix(RuneBefore(head, head.size()).size);
    if (!UtteranceStart(head)) return false;
    // The matrix holds katakana: a hiragana is its katakana less 0x60.
    std::string k;
    text::AppendRune(k, IsHiragana(kana.cp) ? kana.cp + 0x60 : kana.cp);
    const int index = word::KanaMatrixIndex(k);
    if (index < 0) return false;
    for (const dict::Record& r : snap.NameRecords(key)) {
        if (r.codes == nullptr || !r.codes->sem.HasPrefix("QPN")) continue;
        const std::string_view first = r.target.substr(0, FirstRune(r.target).size);
        for (const int n : {index % 5, 5 + index % 5}) {
            if (!first.empty() && word::KanaCell(index / 5, n) == first) return true;
        }
    }
    return false;
}

// Whether the kanji or katakana run that ends s is a noun of a person (彼女, 姉, 先生), a word of its own.
bool PersonNounBefore(const dict::Snapshot& snap, std::string_view s) {
    std::size_t start = s.size();
    for (std::size_t runes = 0; start > 0 && runes < 4; runes++) {
        const char32_t c = RuneBefore(s, start).cp;
        if (!IsKanji(c) && !IsKatakana(c)) break;
        start -= RuneBefore(s, start).size;
    }
    if (start == s.size() || (start > 0 && (IsKanji(RuneBefore(s, start).cp) || IsKatakana(RuneBefore(s, start).cp)))) return false;
    const std::string_view word = s.substr(start);
    for (const dict::Candidate& c : snap.RetrieveRecords(word, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.source == word && !c.codes->sem.HasPrefix("QPN") && c.codes->sem.HasCode("VHn")) return true;
    }
    return false;
}

// Whether b ends in the first of a pair of persons before と: a speaker's pronoun (pronoun.speaker), a noun of a
// person or a word with an honorific, standing as a word of its own. With nouns false, only a pronoun or an
// honorific.
bool PairAnchorBefore(const dict::Snapshot& snap, std::string_view b, bool nouns) {
    if (nouns && PersonNounBefore(snap, b)) return true;
    std::size_t n = dict::EndsWithMember(HDTRANS_CLASS("pronoun.speaker"), b);
    bool honorific = false;
    if (n == 0) {
        n = dict::EndsWithMember(HDTRANS_CLASS("honorific.candidate"), b);
        honorific = n > 0;
    }
    if (n == 0 || n == b.size()) return n > 0 && !honorific;
    const char32_t first = FirstRune(b.substr(b.size() - n)).cp;
    const char32_t prev = RuneBefore(b, b.size() - n).cp;
    // An honorific follows a name of kanji or katakana (高峰さん); a pronoun is no part of a longer word (独自分).
    if (honorific) return IsKanji(prev) || IsKatakana(prev);
    if (IsKanji(first)) return !IsKanji(prev);
    return !IsHiragana(prev) && !IsKatakana(prev);
}

// The byte length of a pronoun of a person (俺, お前, 彼女) or a noun of a person (姉) that starts s, 0 for none.
std::size_t PersonWordAt(const dict::Snapshot& snap, std::string_view s) {
    std::size_t n = 0;
    // The longest member: 彼女 is no 彼.
    for (const dict::ClassId c : {HDTRANS_CLASS("pronoun.speaker"), HDTRANS_CLASS("pronoun.listener"), HDTRANS_CLASS("pronoun.third-person")}) {
        for (const std::string_view m : dict::ClassMembers(c)) {
            if (s.starts_with(m)) n = std::max(n, m.size());
        }
    }
    return n > 0 ? n : PersonNounLength(snap, s);
}

// Whether after, past と, goes on with another person of a chain (千里と高峰で, 千里と俺の): a person word or a
// person's name of the name layer.
bool ChainGoesOnAt(const dict::Snapshot& snap, std::string_view after) {
    if (!after.starts_with("と")) return false;
    const std::string_view rest = after.substr(std::string_view("と").size());
    return PersonWordAt(snap, rest) > 0 || !snap.PersonKeysAt(rest).empty();
}

// Whether the word stands second in a pair of persons joined by と before a particle of the clause (俺と千里は,
// 僕と凪が, 高峰さんと凪は, 姉と千里は; spec LEX-4820): the first is a speaker's pronoun (pronoun.speaker), a noun
// of a person or a word with an honorific, standing as a word of its own. Not before の, where the pair may own a
// common word (俺と千里の道).
// A common word of one kanji pairs only with a pronoun or an honorific (俺と凪は; not 女房と畳は), with nouns false.
// With nouns set, a chain counts too: the word goes on with と and another person (俺と千里と高峰で), or stands
// after one more word and と (俺と千里と高峰で), or before で.
bool PairedAt(const dict::Snapshot& snap, std::string_view before, std::string_view after, bool nouns = true) {
    if (!before.ends_with("と")) return false;
    if (!InRunes(FirstRune(after).cp, "がはもをに") && !(nouns && (after.starts_with("で") || ChainGoesOnAt(snap, after)))) return false;
    const std::string_view b = before.substr(0, before.size() - std::string_view("と").size());
    if (PairAnchorBefore(snap, b, nouns)) return true;
    if (!nouns) return false;
    // The middle of a chain: a word of kanji or katakana and と before the word.
    std::size_t k = b.size();
    for (std::size_t runes = 0; k > 0 && runes < 4 && (IsKanji(RuneBefore(b, k).cp) || IsKatakana(RuneBefore(b, k).cp)); runes++) {
        k -= RuneBefore(b, k).size;
    }
    if (k == b.size() || !b.substr(0, k).ends_with("と")) return false;
    return PairAnchorBefore(snap, b.substr(0, k - std::string_view("と").size()), true);
}

// Whether the word stands first in a pair with a person after と and a particle of the clause (高峰と俺は,
// 高峰と彼女が, 千里とお前は; spec LEX-4820). Not before の.
bool PairedFirstAt(const dict::Snapshot& snap, std::string_view after) {
    if (!after.starts_with("と")) return false;
    const std::string_view rest = after.substr(std::string_view("と").size());
    const std::size_t n = PersonWordAt(snap, rest);
    if (n == 0) return false;
    const std::string_view tail = rest.substr(n);
    return InRunes(FirstRune(tail).cp, "がはもで") || ChainGoesOnAt(snap, tail);
}

bool PersonActAt(const dict::Snapshot& snap, std::string_view key, std::string_view before, std::string_view after) {
    // A katakana name that also spells an animal (アリ, サイ) names a person as the subject of a verb of
    // saying or thinking in its clause, as a single kanji does (アリが言った 아리가 말했다; spec LEX-4840).
    if (KatakanaAnimalKey(snap, key) && !FigureKey(snap, key)) {
        for (const std::string_view p : {"が", "は"}) {
            if (after.starts_with(p)) return SingleSayer(after.substr(p.size()));
        }
        return false;
    }
    // A verb stem (力 of 力む) is no verb before a particle (力は笑った).
    const bool particle = InRunes(FirstRune(after).cp, "はがもにの、");
    if (!HonorableKey(snap, key, particle) || FigureKey(snap, key)) return false;
    // A single kanji names a person only as the subject of a verb of saying or thinking in its clause
    // (南が言った; not 左の頬, 車が音を言わせて): its other cues are too often its common word.
    // One that only the name layer has, as a person, takes the cues of longer names (晶の胸, 凪は; spec LEX-5160):
    // no dictionary word reads it but the reading of the kanji (정, 지).
    if (text::RuneCount(key) < 2) {
        if (after.starts_with("が") && SingleSayer(after.substr(std::string_view("が").size()))) return true;
        if (!SingleNameKey(snap, key)) return false;
        // One that is also a common word (樹 나무, 光 빛) is a person where only a person can stand: the
        // subject or topic of what only a person does (樹は笑った), one that speaks (樹の言うとおり), one whose
        // act befalls someone (樹に呆れられる), or one called at the start of an utterance (「樹、どこ行くの？」;
        // spec LEX-5175).
        if (snap.DictionaryWord(key)) {
            if (PairedAt(snap, before, after, false) && !after.starts_with("を") && !after.starts_with("に")) return true;
            if (after.starts_with("が") || after.starts_with("は") || after.starts_with("も")) {
                return PersonActIn(after.substr(FirstRune(after).size), Role::Act, true);
            }
            if (after.starts_with("の") || after.starts_with("に")) return SpeakerOrAgentAt(after);
            // A call opens the quote (not 元、… or …！　尚、).
            const std::string_view b = TrimEndSpaces(before);
            return after.starts_with("、") && (b.ends_with("「") || b.ends_with("『")) && CallableKey(snap, key, true) && !CommonSenseAt(after);
        }
    }
    if (after.starts_with("の")) {
        // A voice is a person's after a clause of what a person does (静かに語る凪の声が響く).
        const std::string_view rest = after.substr(std::string_view("の").size());
        if (PersonPartAt(rest)) return true;
        // Hair is a person's for a word that only the name layer reads (凪の髪; not 白銀の髪).
        if (const std::size_t hair = dict::StartsWithMember(HDTRANS_CLASS("name.person-hair"), rest);
            hair > 0 && !snap.DictionaryWord(key) && !IsKanji(FirstRune(rest.substr(hair)).cp)) {
            return true;
        }
        const std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.person-voice"), rest);
        return n > 0 && !IsKanji(FirstRune(rest.substr(n)).cp) && IsHiragana(RuneBefore(before, before.size()).cp) &&
               PersonActIn(ClauseBefore(before), Role::Other, false, nullptr, MoverKey(snap, key));
    }
    if (PairedAt(snap, before, after) || PairedFirstAt(snap, after)) return true;
    const bool mover = MoverKey(snap, key);
    if (after.starts_with("が")) return PersonActIn(after.substr(std::string_view("が").size()), Role::Subject, true, nullptr, mover);
    if (after.starts_with("は") || after.starts_with("も")) {
        return PersonActIn(after.substr(FirstRune(after).size), SpeakingTopic(snap, key) ? Role::NamedTopic : Role::Topic, true, &snap,
                           mover);
    }
    if (after.starts_with("、") && (UtteranceStart(before) || TrimEndSpaces(before).ends_with("、")) && !AdverbKey(snap, key)) {
        const std::string_view rest = after.substr(std::string_view("、").size());
        if (RequestAt(rest) || PersonActIn(rest, UtteranceStart(before) ? Role::Other : Role::Aside, true, nullptr, mover)) return true;
        // Called at the start of an utterance with a command, a request or a question that runs to its end.
        return CallStart(before) &&
               (CalledClauseAt(snap, rest, false, false,
                                !internal::AllRunes(key, IsHiragana) && !(internal::AllRunes(key, IsKatakana) && snap.DictionaryWord(key))) || (!CommonTimeKey(snap, key) && CalledClauseAt(snap, rest, true, TimeKey(snap, key))));
    }
    // Not a katakana spelling of a native word, which after a command is a word said at the one told (行けボケ).
    if (!internal::AllRunes(key, IsHiragana) && !(internal::AllRunes(key, IsKatakana) && snap.DictionaryWord(internal::ToHiragana(key))) &&
        (CommandCallAt(snap, before, after) || StutterCallAt(snap, key, before, after))) {
        return true;
    }
    if (text::RuneCount(key) >= 2 && !internal::AllRunes(key, IsHiragana) && FinalCallAt(key, before, after)) return true;
    if (after.empty() || after.starts_with("。") || after.starts_with("！") || after.starts_with("!") || after.starts_with("…")) {
        return IsHiragana(RuneBefore(before, before.size()).cp) && PersonActIn(ClauseBefore(before), Role::Other, false, nullptr, mover);
    }
    return false;
}

// Whether a cue of a person holds for the document (NameCue::person): key has two characters or
// more and is no hiragana nor a katakana spelling of a native word, which other lines may mean.
bool Carried(const dict::Snapshot& snap, std::string_view key) {
    if (text::RuneCount(key) < 2 || internal::AllRunes(key, IsHiragana)) return false;
    return !(internal::AllRunes(key, IsKatakana) && snap.DictionaryWord(internal::ToHiragana(key)));
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

std::vector<std::string> PersonVerbProblems() {
    std::vector<std::string> out;
    const auto members = dict::ClassMembers(HDTRANS_CLASS("name.person-verb"));
    const auto notes = dict::ClassNotes(HDTRANS_CLASS("name.person-verb"));
    for (std::size_t k = 0; k < members.size(); k++) {
        PersonVerb v;
        if (k >= notes.size() || PersonVerbKind(members[k], notes[k], v) < 0) out.emplace_back(members[k]);
    }
    return out;
}

std::vector<NameCue> NameCues(const dict::Snapshot& snap, std::string_view text) {
    std::vector<NameCue> out;
    // person: an honorific after the word; acted: a predicate of a person (NameCue).
    auto add = [&](std::string_view key, bool strong, bool person = false, bool acted = false) {
        const bool evidence = strong || person || acted;
        const bool recurring = strong && text::RuneCount(key) >= 2 && !Memorable(snap, key);
        strong = strong && !recurring && Memorable(snap, key);
        person = person && Carried(snap, key);
        // A single kanji's cue holds in its line only (SingleNameKey).
        acted = acted && Carried(snap, key) && !FigureKey(snap, key) && text::RuneCount(key) >= 2;
        const auto found = std::find_if(out.begin(), out.end(), [&](const NameCue& c) { return c.key == key; });
        if (found == out.end()) {
            out.push_back({std::string(key), strong, recurring, person, acted, evidence});
        } else {
            found->evidence = found->evidence || evidence;
            found->strong = found->strong || strong;
            found->recurring = (found->recurring || recurring) && !found->strong;
            found->person = found->person || person;
            found->acted = found->acted || acted;
        }
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
        if (dict::InClass(HDTRANS_CLASS("noun.role"), key) || dict::InClass(HDTRANS_CLASS("noun.label-role"), key) || key.ends_with("屋")) {
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
            (hiragana && (IsHiragana(last) || text::RuneCount(key) < 3))) {
            i += step;
            continue;
        }
        // A label in round brackets, or one that a colon or ≫ closes, is a whole word: it neither
        // inflects (さくら：, (わかば)) nor stands for a verb or a prefix of one character ((恋)); a
        // character of noun.bracket-note in brackets annotates the line ((続)).
        const bool bracketLabel = internal::BracketLabelAt(before, after) || internal::NameBoxAt(before, after);
        if (HeadingBoxAt(snap, key, before, after)) {
            i += step;
            continue;
        }
        // A box of one kanji with a common sense names the speaker in the box only, which Registry::ReadBoxNames
        // writes there (【月】「月がきれい」 달이, 愛がすべてだ 사랑이): it cues no other occurrence.
        if (BoxNameAt(snap, key, before, after)) {
            i += step;
            continue;
        }
        const bool closedLabel = bracketLabel || ClosedLabelAt(before, after);
        // A hiragana name introduces the speaker, also after a comma (わたし、つむぎです), or is
        // called at the start of an utterance (つむぎ、こっちに来て). Only these cues and a closed
        // label make a name of one that starts a verb or an adjective (つむぎ, つぐみ).
        if (hiragana) {
            if (HiraganaCall(snap, key) &&
                (IntroductionAt(TrimEndSpaces(before).ends_with("、") ? std::string_view() : before, after, true) ||
                 (UtteranceStart(before) && after.starts_with("、")))) {
                add(key, false);
            }
            const bool predicate = HiraganaPredicate(snap, key);
            if (HiraganaAlone(snap, key, after, predicate)) add(key, false);
            if (!closedLabel && predicate && !PersonActAt(snap, key, before, after) && !HiraganaAlone(snap, key, after, predicate)) {
                i += step;
                continue;
            }
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
            (LabelAt(snap, before, after) &&
             (!single || (bracketLabel && !dict::InClass(HDTRANS_CLASS("noun.bracket-note"), key)) || LabelNoun(snap, key) || (SingleNameKey(snap, key) && !dict::InClass(HDTRANS_CLASS("noun.bracket-note"), key)))) || (!single && ActionAt(snap, key, before, after))) {
            add(key, true);
        } else if (runStart && !hiragana && HonorificAfter(snap, after, key)) {
            add(key, false, true);
        } else if (kanji && runStart && AloneCallAt(before, after, !OnlyNames(snap, key) || !single) &&
                   (SingleNameKey(snap, key) || (text::RuneCount(key) >= 2 && LayerPersonKey(snap, key) && CallableKey(snap, key, true) && !PlaceKey(snap, key)))) {
            // A name called alone (航太？, じゃあな 匡｡; spec LEX-4953).
            add(key, false, false, true);
        } else if (runStart && WeakKey(snap, key) && PersonActAt(snap, key, before, after)) {
            add(key, false, false, true);
        } else if (runStart && !hiragana && TitleAfter(snap, after, key)) {
            add(key, false, true);
        } else if ((runStart && hiragana && HonorificAfter(snap, after, key)) ||
                   (WeakKey(snap, key) && (IntroductionAt(before, after, hiragana) ||
                                          ((katakana || (kanji && text::RuneCount(key) >= 2)) &&
                                           CallAt(before, after) && CallableKey(snap, key, kanji) &&
                                           !PlaceKey(snap, key))))) {
            add(key, false, false, true);
        }
        i += step;
    }
    return out;
}

bool SpeakerLabelAt(const dict::Snapshot& snap, std::string_view before, std::string_view after) {
    return LabelAt(snap, before, after) || internal::SquareLabelAt(before, after);
}

bool BoxNameAt(const dict::Snapshot& snap, std::string_view key, std::string_view before, std::string_view after) {
    return text::RuneCount(key) == 1 && internal::NameBoxAt(before, after) && !internal::BracketLabelAt(before, after) &&
           SingleNameKey(snap, key) && !NameOnlyKey(snap, key) && !HeadingBoxAt(snap, key, before, after) &&
           !dict::InClass(HDTRANS_CLASS("noun.bracket-note"), key) && !dict::InClass(HDTRANS_CLASS("noun.role"), key) &&
           !dict::InClass(HDTRANS_CLASS("noun.label-role"), key);
}

bool PersonSlotAt(const dict::Snapshot& snap, std::string_view key, std::string_view before, std::string_view after) {
    if (CommonSenseAt(after)) return false;
    // An honorific: the document has named the word already, so a sense of an animal does not stand in the
    // way (千鳥（ちどり）さん).
    const bool honorific = HonorificStart(after);
    // Alone on its line it is a name line (桃花).
    const bool alone = TrimEndSpaces(before).empty() && TrimEndSpaces(after).empty();
    if (honorific || alone || LabelAt(snap, before, after) || CallAt(before, after) ||
        AloneCallAt(before, after, true) || FinalCallAt(key, before, after) || ActionAt(snap, key, before, after) ||
        IntroductionAt(before, after, false) || (TitleBefore(before, false) && NameEnd(after))) {
        return true;
    }
    if (SpeakerOrAgentAt(after)) return true;
    if (after.starts_with("の")) {
        const std::string_view rest = after.substr(std::string_view("の").size());
        return PersonPartAt(rest) || dict::StartsWithMember(HDTRANS_CLASS("name.person-belonging"), rest) > 0;
    }
    const bool mover = MoverKey(snap, key);
    if (after.starts_with("が")) return PersonActIn(after.substr(std::string_view("が").size()), Role::Subject, true, nullptr, mover);
    if (after.starts_with("は") || after.starts_with("も")) {
        return PersonActIn(after.substr(FirstRune(after).size), Role::Topic, true, nullptr, mover);
    }
    if (after.starts_with("と")) {
        const std::string_view rest = after.substr(std::string_view("と").size());
        const std::vector<std::size_t> keys = snap.PersonKeysAt(rest);
        if (!keys.empty() && !CommonWord(snap, rest.substr(0, keys.back()))) return true;
        return PersonActIn(rest, Role::Other, true);
    }
    // At the start of an utterance, before 、, it is called (「朝日、待って！」); before 、 and a call to
    // action it is the one acting, also after a unit and 、 (第六十一駆逐隊、初月、出撃するぞ).
    if (!after.starts_with("、")) return false;
    return UtteranceStart(before) ||
           (TrimEndSpaces(before).ends_with("、") &&
            dict::StartsWithMember(HDTRANS_CLASS("name.action-after"), after.substr(std::string_view("、").size())) > 0);
}

bool WordBeforeAt(std::string_view before) {
    return dict::EndsWithMember(HDTRANS_CLASS("name.word-before"), before) > 0 &&
           dict::EndsWithMember(HDTRANS_CLASS("name.word-before-not"), before) == 0;
}

bool CommonAfterAt(std::string_view after) {
    // A start of name.common-after that ends a word there (力の伸び, 朝日を浴びて).
    const std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.common-after"), after);
    return n > 0 && !IsKanji(FirstRune(after.substr(n)).cp);
}

bool CommonSenseAt(std::string_view after) {
    if (StatusFieldAt(after) || CommonAfterAt(after)) return true;
    if (!(after.starts_with("が") || after.starts_with("は") || after.starts_with("も"))) return false;
    // A predicate of name.common-predicate in the clause (敏捷が特筆して高い), before any other subject or
    // topic (朝日は背が高い is the person's height).
    std::string_view rest = after.substr(FirstRune(after).size);
    for (std::size_t i = 0, runes = 0; i < rest.size() && runes < kClauseReach; runes++) {
        const text::Rune r = text::DecodeRune(rest.substr(i));
        const char32_t prev = i == 0 ? 0 : RuneBefore(rest, i).cp;
        if (InRunes(r.cp, kClauseEnds) || ((r.cp == U'が' || r.cp == U'は') && (IsKanji(prev) || IsKatakana(prev)))) return false;
        if (dict::StartsWithMember(HDTRANS_CLASS("name.common-predicate"), rest.substr(i)) > 0) return true;
        i += r.size == 0 ? 1 : r.size;
    }
    return false;
}

bool PlantSenseAt(const dict::Snapshot& snap, std::string_view key, std::string_view after) {
    if (dict::StartsWithMember(HDTRANS_CLASS("name.plant-after"), after) == 0) return false;
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN") || c.codes->sem.HasPrefix("QCT")) continue;
        if (c.codes->sem.HasCode("QAL") || c.codes->sem.HasCode("VPt")) return true;
    }
    return false;
}

bool StatusValueAt(std::string_view rest) {
    std::size_t i = 0;
    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t' || rest.substr(i).starts_with("　"))) i += rest[i] == ' ' || rest[i] == '\t' ? 1 : std::string_view("　").size();
    // The value runs to the next space, where another field may start (力：35　敏捷：52).
    std::size_t end = i;
    while (end < rest.size() && rest[end] != ' ' && rest[end] != '\t' && rest[end] != '\r' && !rest.substr(end).starts_with("　")) {
        end += std::max<std::size_t>(1, text::DecodeRune(rest.substr(end)).size);
    }
    const std::string_view value = rest.substr(i, end - i);
    if (value.empty()) return false;
    // The whole value is a number (35, ３５, 7/10, 1,200, 50％, +5, ∞), a level (Lv.3) or a grade of one to
    // three letters with a sign (S, ＢＢ+, A-); a word, punctuation or a sentence with a number in it
    // (……, OK, あと10分だよ。) is none.
    std::string_view v = value;
    for (const std::string_view lv : {std::string_view("Lv."), std::string_view("Lv"), std::string_view("LV"), std::string_view("Ｌｖ．"), std::string_view("Ｌｖ")}) {
        if (v.starts_with(lv) && v.size() > lv.size()) {
            v.remove_prefix(lv.size());
            break;
        }
    }
    bool digit = false;
    std::size_t letters = 0;
    for (std::size_t k = 0; k < v.size();) {
        const text::Rune r = text::DecodeRune(v.substr(k));
        if ((r.cp >= U'0' && r.cp <= U'9') || (r.cp >= U'０' && r.cp <= U'９') || r.cp == U'∞') {
            digit = true;
        } else if (InRunes(r.cp, "SABCDEFＳＡＢＣＤＥＦ")) {
            letters++;
        } else if (!InRunes(r.cp, "+-＋－/／.．,，%％")) {
            return false;
        }
        k += r.size == 0 ? 1 : r.size;
    }
    if (digit) return letters == 0;
    return letters >= 1 && letters <= 3;
}

bool StatusFieldAt(std::string_view after) {
    if (after.starts_with(":")) return StatusValueAt(after.substr(1));
    if (after.starts_with("：")) return StatusValueAt(after.substr(std::string_view("：").size()));
    return false;
}

bool CommonWord(const dict::Snapshot& snap, std::string_view key) {
    if (!internal::AllRunes(key, IsKanji)) return false;
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->part.Empty() || c.codes->part.Tag() != 'N') continue;
        // A name row is no common word, also one whose codes put VHn first (海江田 카이에다 VHnQPN Wa).
        if (c.codes->sem.HasCode("QPN") || c.codes->gram.HasPrefix("Wa") || c.codes->gram.HasPrefix("Wb") || c.codes->gram.HasCode("On") ||
            c.codes->sem.HasCode("QCT") || c.codes->sem.HasCode("QNN") || c.codes->sem.HasCode("VPl")) {
            continue;
        }
        return true;
    }
    return false;
}

bool NameOnlyKey(const dict::Snapshot& snap, std::string_view key) {
    if (!internal::AllRunes(key, IsKanji) && !internal::AllRunes(key, IsHiragana)) return false;
    const auto bare = [](std::string_view ko) {
        std::string out;
        for (const char ch : ko) {
            if (ch != ' ') out += ch;
        }
        return out;
    };
    std::set<std::string> person;
    const std::vector<dict::Record> names = snap.NameRecords(key);
    for (const dict::Record& r : names) {
        if (r.codes != nullptr && r.codes->sem.HasPrefix("QPN")) person.insert(bare(r.target));
    }
    if (person.empty()) return false;
    for (const dict::Record& r : names) {
        if (r.codes != nullptr && !r.codes->sem.HasPrefix("QPN") && !person.contains(bare(r.target))) return false;
    }
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != key || c.codes->sem.HasPrefix("QPN") || c.codes->sem.HasPrefix("QNN")) continue;
        if (!person.contains(bare(c.target))) return false;
    }
    return true;
}

}  // namespace hdTrans::names
