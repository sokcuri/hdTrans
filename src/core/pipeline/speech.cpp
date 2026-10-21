#include "core/pipeline/speech.h"

#include <string>

#include "core/pipeline/runes.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline {

namespace {

using internal::RuneIn;

constexpr std::string_view kPoliteMarkers[] = {"です", "ます", "ました", "まして", "ません", "でした", "ましょう",
                                                "でしょう", "ください", "下さい", "なさい", "ませ", "いただきたい", "ていただく"};

// Old or lordly speech; a classical negative ぬ (ならぬ, 知らぬ) only before the end of a
// clause, which 死ぬ is not.
constexpr std::string_view kOldMarkers[] = {
    "ておる", "でおる",   "ておった", "でおった", "ておって", "でおって", "ておらぬ", "せよ",   "ござる", "ござらん",
    "のじゃ", "じゃ。",
    "じゃ」", "じゃ！",   "じゃぞ",   "じゃな。", "じゃな」", "じゃなあ", "じゃわい", "だわい", "ぞよ",   "であろう",
    "かろう", "わしは",   "わしが",   "わしの",   "わしも",   "わしを", "わしに", "儂",       "余は",   "余の",
    "余が",   "余を",     "余に",     "余と",     "余も",     "我が",   "なり！", "なり」",   "なり。", "申した",
    "まする", "参れ",     "致せ",     "申せ",     "くれい",   "おぬし", "お主",   "そなた",   "拙者",   "わらわ",
    "かたじけない", "貴様", "ねば", "されど", "がよい", "が良い", "るがいい", "べけれ", "べし",
};
constexpr std::string_view kOldNegatives[] = {"らぬ", "せぬ", "きぬ", "まぬ", "わぬ", "かぬ", "がぬ", "たぬ", "ばぬ", "れぬ", "せん"};

std::uint8_t StyleOf(std::string_view text) {
    std::uint8_t style = 0;
    for (const std::string_view m : kPoliteMarkers) {
        if (text.find(m) != std::string_view::npos) style |= kPoliteSpeech;
    }
    for (const std::string_view m : kOldMarkers) {
        if (text.find(m) != std::string_view::npos) style |= kOldSpeech;
    }
    for (const std::string_view n : kOldNegatives) {
        for (std::size_t at = text.find(n); at != std::string_view::npos; at = text.find(n, at + 1)) {
            const std::string_view after = text.substr(at + n.size());
            for (const std::string_view end : {"。", "！", "」", "』", "、", "ぞ", "の", "わ", "か", "…", "?", "？", "!"}) {
                if (after.empty() || after.starts_with(end)) style |= kOldSpeech;
            }
        }
    }
    return style;
}

// Whether s starts with a subject (「行くよ」ハルが言った, 「…」私は): a word of up to eight
// kanji or katakana and は, が or も.
bool SubjectAfter(std::string_view s) {
    std::size_t at = 0;
    for (int n = 0; n < 8 && at < s.size(); n++) {
        const text::Rune r = text::DecodeRune(s.substr(at));
        if (!internal::IsKanjiRune(r.cp) && !internal::IsKatakanaRune(r.cp)) break;
        at += r.size;
    }
    if (at == 0) return false;
    const std::string_view rest = s.substr(at);
    return rest.starts_with("は") || rest.starts_with("が") || rest.starts_with("も");
}

constexpr std::string_view kSpeechVerbs[] = {
    "言", "いう", "いっ", "いわ", "叫", "呟", "つぶや", "囁", "ささや", "怒鳴", "どな", "尋ね", "訊", "聞", "答",
    "返", "告げ", "話", "笑", "頼", "喚", "わめ", "漏ら", "こぼ", "零", "呼", "叱", "怒", "泣", "続け", "言い", "思",
};

// Whether the text after the closing bracket of a quote makes it a cited phrase; embedded:
// the quote opens inside a sentence, after a particle or a comma (彼は「行く」と言った), not
// after a speaker name (ハル「行く」).
bool CitedBefore(std::string_view after, bool embedded) {
    for (const std::string_view m : {"もの", "こと", "という", "といった", "のでは", "ような", "みたいな", "なんて"}) {
        if (after.starts_with(m)) return true;
    }
    if (after.empty()) return false;
    const char32_t next = text::DecodeRune(after).cp;
    // A particle other than a quoting と or って (「行こう」が口癖, 「最上」を配備).
    if (internal::IsHiraganaRune(next) && !after.starts_with("と") && !after.starts_with("っ")) return true;
    // って before a noun (「…」って投稿) names what was said or written.
    if (after.starts_with("って")) {
        const std::string_view rest = after.substr(std::string_view("って").size());
        bool verb = false;
        for (const std::string_view v : kSpeechVerbs) verb = verb || rest.starts_with(v);
        if (!rest.empty() && internal::IsKanjiRune(text::DecodeRune(rest).cp) && !verb) return true;
    }
    if (embedded) {
        for (const std::string_view q : {"って", "、", "。", "！", "？", "!", "?", "「", "『", " ", "　"}) {
            if (after.starts_with(q)) return false;
        }
        // と before a verb of speech (と言った, と叫んだ); not と before a thought or a
        // judgement (と認識して, とは言えない).
        for (const std::string_view to : {"っと", "と"}) {
            if (!after.starts_with(to)) continue;
            std::string_view verb = after.substr(to.size());
            if (verb.starts_with("、")) verb.remove_prefix(std::string_view("、").size());
            for (const std::string_view v : kSpeechVerbs) {
                if (verb.starts_with(v)) return false;
            }
        }
        return true;
    }
    const bool noun = internal::IsKanjiRune(next) || internal::IsKatakanaRune(next) || text::IsDigit(next);
    return noun && !SubjectAfter(after);
}

// The text of each quote of line at its own level (element 0 outside quotes), with a
// nested quote written as its brackets only; indexed as QuoteStyles.
std::vector<std::string> OwnTexts(std::string_view line, int depth) {
    std::vector<std::string> texts(depth > 0 ? 2 : 1);
    for (std::size_t at = 0; at < line.size();) {
        const text::Rune r = text::DecodeRune(line.substr(at));
        const std::string_view c = line.substr(at, r.size == 0 ? 1 : r.size);
        at += c.size();
        // ｢ and ｣ too: CasualQuotes reads the text before they become 「 and 」.
        const bool opens = r.cp == U'「' || r.cp == U'『' || r.cp == U'｢';
        if (opens && depth == 0) texts.emplace_back();
        if (opens) {
            depth++;
            if (depth == 2) texts.back() += c;
            continue;
        }
        if ((r.cp == U'」' || r.cp == U'』' || r.cp == U'｣') && depth > 0) {
            if (--depth == 1) texts.back() += c;
            continue;
        }
        if (depth <= 1) texts[depth > 0 ? texts.size() - 1 : 0] += c;
    }
    return texts;
}

// Whether s, the text after a word, ends a sentence: after a drawl (ー, っ, small vowels,
// 〜), the end of the quote, a sentence mark or an ellipsis.
bool EndsAfter(std::string_view s) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.size == 0) return false;
        if (RuneIn(r.cp, U"ーっッぁぃぅぇぉ〜～")) {
            s.remove_prefix(r.size);
            continue;
        }
        return RuneIn(r.cp, U"。！？!?…‥」』♪☆★♥♡") || text::IsSpace(r.cp);
    }
    return true;
}

// The last character of s before at, 0 at the start.
char32_t RuneBefore(std::string_view s, std::size_t at) { return at == 0 ? 0 : internal::LastRune(s.substr(0, at)).cp; }

// Each sentence-final particle with the characters that may stand before it ("" any
// hiragana) and those that may not.
struct Final {
    std::string_view particle;
    std::u32string_view after;
    std::u32string_view notAfter;
};

// Marks of stiff or lordly speech that the old markers of StyleOf do not cover: のだ and
// である ending a sentence, たまえ, かね, じゃろう, ワシ, 我々, a sentence ended by が (思うが……).
bool StiffSpeech(std::string_view text) {
    for (const std::string_view m : {"である", "であろう", "たまえ", "じゃろ", "ワシ", "しれん", "我々", "我ら"}) {
        if (text.find(m) != std::string_view::npos) return true;
    }
    for (const std::string_view m : {"のだ", "かね", "がね"}) {
        for (std::size_t at = text.find(m); at != std::string_view::npos; at = text.find(m, at + 1)) {
            std::string_view after = text.substr(at + m.size());
            // のだ after a predicate (知っているのだ, 静かなのだ); not ものだ or 俺のだ.
            if (m == "のだ" && !RuneIn(RuneBefore(text, at), U"るたいうな")) continue;
            if (m == "のだ") {
                for (const std::string_view p : {"よ", "ぞ", "な", "が", "ね"}) {
                    if (after.starts_with(p)) after.remove_prefix(p.size());
                }
            }
            if (EndsAfter(after) || after.starts_with("、")) return true;
        }
    }
    for (std::size_t at = text.find("が"); at != std::string_view::npos; at = text.find("が", at + 1)) {
        if (internal::IsHiraganaRune(RuneBefore(text, at)) && EndsAfter(text.substr(at + std::string_view("が").size()))) {
            return true;
        }
    }
    return false;
}

// Whether text, the own text of a quote, is casual speech: a sentence-final particle (よ,
// ね, わ, じゃん, さ, もん, って, かな, かしら) or a contraction (ちゃう, てる, とく, ねえ).
bool CasualOf(std::string_view text) {
    if (StiffSpeech(text)) return false;
    // よ: not an old command (せよ, やめよ, 考えよ), ぞよ or a call (部下たちよ); ね: not かね;
    // って: a hearsay or quoting って (行くって, 来たって, そうだって), not a te-form.
    static constexpr Final kFinals[] = {
        {"よ", U"", U"せめべげえけぞち"}, {"ね", U"", U"かが"},          {"わ", U"るたいだんよ", U""},
        {"じゃん", U"", U""},             {"さ", U"うのらだ", U""},        {"もん", U"だいたるんの", U""},
        {"って", U"だるたい", U""},       {"かな", U"", U"ば"},            {"かしら", U"", U""},
    };
    for (const Final& f : kFinals) {
        for (std::size_t at = text.find(f.particle); at != std::string_view::npos; at = text.find(f.particle, at + 1)) {
            if (!EndsAfter(text.substr(at + f.particle.size()))) continue;
            const char32_t prev = RuneBefore(text, at);
            // After a kana word; じゃん after a noun too (嘘じゃん).
            const bool noun = internal::IsKanjiRune(prev) || internal::IsKatakanaRune(prev);
            if (!internal::IsHiraganaRune(prev) && !(noun && f.particle == "じゃん")) continue;
            if (!f.after.empty() && f.after.find(prev) == std::u32string_view::npos) continue;
            if (f.notAfter.find(prev) != std::u32string_view::npos) continue;
            // いって: ないって, いいって; not 言って.
            const std::string_view head = text.substr(0, at);
            if (f.particle == "って" && prev == U'い' && !head.ends_with("ない") && !head.ends_with("いい")) continue;
            return true;
        }
    }
    for (const std::string_view c : {"ちゃう", "ちゃった", "ちゃって", "ちゃえ", "ちゃお", "じゃう", "じゃった", "じゃって",
                                     "じゃえ", "じゃお"}) {
        if (text.find(c) != std::string_view::npos) return true;
    }
    for (const std::string_view c : {"ってる", "ってた", "ってん", "してる", "してた", "してん", "いてる", "いてた",
                                     "んでる", "んでた", "いでる", "いでた", "見てる", "見てた", "来てる", "来てた",
                                     "出てる", "出てた", "寝てる", "寝てた", "着てる", "着てた"}) {
        if (text.find(c) != std::string_view::npos) return true;
    }
    // しとく, 言っとく, 読んどく: ておく after a verb; not ちょっとくらい, ずっといて.
    for (const std::string_view c : {"とく", "とけ", "といて", "といた", "とこう", "どく", "どけ", "どいて", "どいた"}) {
        for (std::size_t at = text.find(c); at != std::string_view::npos; at = text.find(c, at + 1)) {
            const text::Rune te = internal::LastRune(text.substr(0, at));
            const char32_t stem = RuneBefore(text, at - te.size);
            const bool voiced = c.starts_with("ど");
            if (voiced ? te.cp == U'ん' && internal::IsKanjiRune(stem)
                       : te.cp == U'し' || ((te.cp == U'っ' || te.cp == U'い') && internal::IsKanjiRune(stem))) {
                return true;
            }
        }
    }
    for (const std::string_view c : {"ねえ", "ねぇ", "ねー"}) {
        for (std::size_t at = text.find(c); at != std::string_view::npos; at = text.find(c, at + 1)) {
            const std::string_view after = text.substr(at + c.size());
            if (after.starts_with("さん") || after.starts_with("ちゃん") || after.starts_with("さま") ||
                after.starts_with("様") || after.starts_with("さ")) {
                continue;
            }
            // 知らねえ, 行かねー: a negative after an a-row stem.
            const char32_t prev = RuneBefore(text, at);
            if (RuneIn(prev, U"らかさたなまわがばやじゃれっえ")) return true;
        }
    }
    return false;
}

}  // namespace

std::vector<std::uint8_t> QuoteStyles(std::string_view line, int depth) {
    std::vector<std::string> texts(depth > 0 ? 2 : 1);
    std::vector<std::uint8_t> styles(texts.size(), 0);
    char32_t previous = 0;  // the last character outside quotes that is not a space
    bool embedded = false;
    std::vector<bool> chained(texts.size(), false);  // the quote is followed by another
    for (std::size_t at = 0; at < line.size();) {
        const text::Rune r = text::DecodeRune(line.substr(at));
        const std::string_view c = line.substr(at, r.size == 0 ? 1 : r.size);
        at += c.size();
        // ｢ and ｣ too: CasualQuotes reads the text before they become 「 and 」.
        const bool opens = r.cp == U'「' || r.cp == U'『' || r.cp == U'｢';
        if (opens && depth == 0) {
            texts.emplace_back();
            styles.push_back(r.cp == U'『' ? kWritten : 0);
            if (internal::IsKanjiRune(previous)) styles.back() |= kReading;
            embedded = internal::IsHiraganaRune(previous) || RuneIn(previous, U"、，,");
        }
        if (opens) {
            depth++;
        } else if ((r.cp == U'」' || r.cp == U'』' || r.cp == U'｣') && depth > 0 && --depth == 0) {
            texts.back() += c;
            // A word quoted inside a sentence (モットーは「従う」だ) is cited too.
            const bool word = embedded && text::RuneCount(texts.back()) <= 6;
            if (word || CitedBefore(line.substr(at), embedded)) styles.back() |= kCited;
            chained.resize(texts.size(), false);
            chained.back() = line.substr(at).starts_with("「") || line.substr(at).starts_with("『");
            previous = r.cp;
            continue;
        }
        texts[depth > 0 ? texts.size() - 1 : 0] += c;
        if (depth == 0 && !text::IsSpace(r.cp)) previous = r.cp;
    }
    // A quote right before a cited one (「…」「…」という) is cited with it.
    chained.resize(texts.size(), false);
    for (std::size_t k = texts.size() - 1; k > 1; k--) {
        if ((styles[k] & kCited) != 0 && chained[k - 1]) styles[k - 1] |= kCited;
    }
    for (std::size_t k = 0; k < texts.size(); k++) {
        styles[k] |= StyleOf(texts[k]);
        // A reading holds a few kana only.
        if ((styles[k] & kReading) != 0) {
            std::size_t kana = 0;
            bool only = true;
            for (std::string_view t = texts[k]; !t.empty();) {
                const text::Rune r = text::DecodeRune(t);
                t.remove_prefix(r.size == 0 ? 1 : r.size);
                if (r.cp == U'「' || r.cp == U'『' || r.cp == U'」' || r.cp == U'』') continue;
                only = only && (internal::IsHiraganaRune(r.cp) || internal::IsKatakanaRune(r.cp));
                kana++;
            }
            if (!only || kana > 4) styles[k] &= static_cast<std::uint8_t>(~kReading);
        }
    }
    return styles;
}

std::vector<bool> CasualQuotes(std::string_view line, int depth) {
    const std::vector<std::string> texts = OwnTexts(line, depth);
    std::vector<bool> casual(texts.size(), false);
    for (std::size_t k = 1; k < texts.size(); k++) casual[k] = CasualOf(texts[k]);
    return casual;
}

}  // namespace hdTrans::pipeline
