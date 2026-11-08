#include "core/names/name_cues.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/names/chars.h"
#include "core/names/labels.h"
#include "core/text/unicode.h"
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
bool HonorableKey(const dict::Snapshot& snap, std::string_view key) {
    if (key.starts_with("お") || key.starts_with("ご") || key.starts_with("御") || dict::InClass(HDTRANS_CLASS("noun.role"), key) ||
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
bool HonorificAfter(const dict::Snapshot& snap, std::string_view after, std::string_view key) {
    return dict::StartsWithMember(HDTRANS_CLASS("honorific.candidate"), after) > 0 && !after.starts_with("ちゃんと") &&
           !after.starts_with("くんだり") && HonorableKey(snap, key);
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
bool ClosedLabelAt(std::string_view before, std::string_view after) {
    return internal::CaptionLead(before) && (after.starts_with(":") || after.starts_with("：") || after.starts_with("≫"));
}

// A speaker label: the word alone at the start of the line before 「, 『, a colon, ≫ or a role in
// round brackets, after caption marks only (⚟アヤメ：, ≫アヤメ：, 》アヤメ：), or a part of a label in
// round brackets there ((アヤメ), ⚟（アヤメ）).
bool LabelAt(const dict::Snapshot& snap, std::string_view before, std::string_view after) {
    if (internal::BracketLabelAt(before, after) || ClosedLabelAt(before, after) || RoleAfterLabel(snap, before, after)) return true;
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
};

// Whether the clause at the start of s holds what only a person does: a verb of name.person-act;
// a part of the body of the person's own, after no の, kanji or verb stem, before を, or before が after a
// topic (身をよじる, 顔が赤い; not その手を断ち切る); a verb of name.person-say after a subject or
// after an object in を; or a verb of name.person-transitive after an object in を (小太刀はスイッチを
// 入れた), going on as no wish or passive (見たい, 知られ). The clause ends at a mark, a bracket, a
// member of name.person-clause-stop and, with clause, at the next subject a kanji or katakana word
// with が or は opens (泉がある場所で彼女は).
bool PersonActIn(std::string_view s, Role role, bool clause) {
    bool object = false;
    std::size_t runes = 0;
    for (std::size_t i = 0; i < s.size() && runes < kClauseReach; runes++) {
        const std::string_view rest = s.substr(i);
        const char32_t prev = i == 0 ? 0 : RuneBefore(s, i).cp;
        if (const std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("name.body-part"), rest);
            n > 0 && !IsKanji(prev) && prev != U'の' && !(text::RuneCount(rest.substr(0, n)) == 1 && InRunes(prev, kStemEnds)) &&
            (rest.substr(n).starts_with("を") || ((role == Role::Topic || role == Role::NamedTopic) && rest.substr(n).starts_with("が")))) {
            return true;
        }
        // A verb starts a word: not after a kanji (世話して is no 話し).
        const bool wordStart = !IsKanji(prev) || !IsKanji(FirstRune(rest).cp);
        if (wordStart && dict::StartsWithMember(HDTRANS_CLASS("name.person-act"), rest) > 0) return true;
        object = object || (i > 0 && rest.starts_with("を"));
        if (wordStart && role == Role::NamedTopic && dict::StartsWithMember(HDTRANS_CLASS("name.person-speak"), rest) > 0) return true;
        for (const bool say : {true, false}) {
            if (!wordStart) break;
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
        if (InRunes(r.cp, kClauseEnds) || dict::StartsWithMember(HDTRANS_CLASS("name.person-clause-stop"), rest) > 0) return false;
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
            for (const std::string_view code : {"QAL", "VPt", "QAA", "QAB", "UMe"}) {
                if (c.codes->sem.HasCode(code)) return true;
            }
            if (c.codes->sem.HasCode("VPl") && !c.codes->sem.HasCode("UDr")) return true;
        } else if (c.codes->sem.HasCode("VPl")) {
            return true;
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

bool PersonActAt(const dict::Snapshot& snap, std::string_view key, std::string_view before, std::string_view after) {
    // A katakana name that also spells an animal (アリ, サイ) names a person as the subject of a verb of
    // saying or thinking in its clause, as a single kanji does (アリが言った 아리가 말했다; spec LEX-4840).
    if (KatakanaAnimalKey(snap, key) && !FigureKey(snap, key)) {
        for (const std::string_view p : {"が", "は"}) {
            if (after.starts_with(p)) return SingleSayer(after.substr(p.size()));
        }
        return false;
    }
    if (!HonorableKey(snap, key) || FigureKey(snap, key)) return false;
    // A single kanji names a person only as the subject of a verb of saying or thinking in its clause
    // (南が言った; not 左の頬, 車が音を言わせて): its other cues are too often its common word.
    // One that only the name layer has, as a person, takes the cues of longer names (晶の胸, 凪は; spec LEX-5160):
    // no dictionary word reads it but the reading of the kanji (정, 지).
    if (text::RuneCount(key) < 2) {
        if (after.starts_with("が") && SingleSayer(after.substr(std::string_view("が").size()))) return true;
        if (snap.DictionaryWord(key) || !SingleNameKey(snap, key)) return false;
    }
    if (after.starts_with("の")) return PersonPartAt(after.substr(std::string_view("の").size()));
    if (after.starts_with("が")) return PersonActIn(after.substr(std::string_view("が").size()), Role::Subject, true);
    if (after.starts_with("は") || after.starts_with("も")) {
        return PersonActIn(after.substr(FirstRune(after).size), SpeakingTopic(snap, key) ? Role::NamedTopic : Role::Topic, true);
    }
    if (after.starts_with("、") && (UtteranceStart(before) || TrimEndSpaces(before).ends_with("、"))) {
        const std::string_view rest = after.substr(std::string_view("、").size());
        return RequestAt(rest) || PersonActIn(rest, Role::Other, true);
    }
    if (text::RuneCount(key) >= 2 && !internal::AllRunes(key, IsHiragana) && FinalCallAt(key, before, after)) return true;
    if (after.empty() || after.starts_with("。") || after.starts_with("！") || after.starts_with("!") || after.starts_with("…")) {
        return IsHiragana(RuneBefore(before, before.size()).cp) && PersonActIn(ClauseBefore(before), Role::Other, false);
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

std::vector<NameCue> NameCues(const dict::Snapshot& snap, std::string_view text) {
    std::vector<NameCue> out;
    // person: an honorific after the word; acted: a predicate of a person (NameCue).
    auto add = [&](std::string_view key, bool strong, bool person = false, bool acted = false) {
        const bool recurring = strong && text::RuneCount(key) >= 2 && !Memorable(snap, key);
        strong = strong && !recurring && Memorable(snap, key);
        person = person && Carried(snap, key);
        // A single kanji's cue holds in its line only (SingleNameKey).
        acted = acted && Carried(snap, key) && !FigureKey(snap, key) && text::RuneCount(key) >= 2;
        const auto found = std::find_if(out.begin(), out.end(), [&](const NameCue& c) { return c.key == key; });
        if (found == out.end()) {
            out.push_back({std::string(key), strong, recurring, person, acted});
        } else {
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
        const bool bracketLabel = internal::BracketLabelAt(before, after);
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

}  // namespace hdTrans::names
