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
    "ておる", "でおる",   "ておった", "でおった", "ておらぬ", "せよ",   "ござる", "ござらん", "のじゃ", "じゃ。",
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

}  // namespace

std::vector<std::uint8_t> QuoteStyles(std::string_view line) {
    std::vector<std::string> texts(1);
    std::vector<std::uint8_t> styles(1, 0);
    int depth = 0;
    char32_t previous = 0;  // the last character outside quotes that is not a space
    bool embedded = false;
    std::vector<bool> chained(1, false);  // the quote is followed by another
    for (std::size_t at = 0; at < line.size();) {
        const text::Rune r = text::DecodeRune(line.substr(at));
        const std::string_view c = line.substr(at, r.size == 0 ? 1 : r.size);
        at += c.size();
        const bool opens = r.cp == U'「' || r.cp == U'『';
        if (opens && depth == 0) {
            texts.emplace_back();
            styles.push_back(r.cp == U'『' ? kWritten : 0);
            if (internal::IsKanjiRune(previous)) styles.back() |= kReading;
            embedded = internal::IsHiraganaRune(previous) || RuneIn(previous, U"、，,");
        }
        if (opens) {
            depth++;
        } else if ((r.cp == U'」' || r.cp == U'』') && depth > 0 && --depth == 0) {
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

}  // namespace hdTrans::pipeline
