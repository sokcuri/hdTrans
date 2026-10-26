#pragma once
// The speech styles that keep the sentence ends of quotes.
#include <cstdint>
#include <string_view>
#include <vector>

namespace hdTrans::pipeline {

// The styles of the quotes of a line that keep their plain sentence ends: polite speech
// (です, ます, ください), whose plain sentences are part of a polite talk; old or lordly
// speech (ておる, せよ, ござる, わしは, ならぬ); a 『』 outside 「」, more often written text
// or a title than speech; a reading after a kanji (想「おも」い); a cited phrase: a quote
// that a noun or a particle other than と and って follows (「すぐ消える」魔法,
// 「雨が降る」ということ), a word quoted inside a sentence (モットーは「従う」だ), or a quote
// inside a sentence that no punctuation, って or と before a verb of speech follows
// (彼は「明日は雨だ」と言った is speech, 「雨だ」と認めた is not).
constexpr std::uint8_t kPoliteSpeech = 1;
constexpr std::uint8_t kOldSpeech = 2;
constexpr std::uint8_t kWritten = 4;
constexpr std::uint8_t kReading = 8;
constexpr std::uint8_t kCited = 16;
// です or ます proper, not なさい alone, でしょうがない or ますます (set with kPoliteSpeech).
constexpr std::uint8_t kPoliteForm = 128;

// The styles of line: element 0 for its text outside 「」 and 『』, element k for its k-th
// quote (a 「 or 『 outside any quote, with the quotes inside it); 「 and 『 open a quote,
// 」 and 』 close one, and a closing bracket outside a quote is ignored. With depth, the
// line starts inside a quote an earlier line opened, which is its first quote.
std::vector<std::uint8_t> QuoteStyles(std::string_view line, int depth = 0);

// Whether text holds a fused or clipped adjective that the dictionary tells (すげえ, 痛え, さみい,
// 寒っ), forms of speech only; holds is null where nothing is looked up.
struct FusedTest {
    bool (*holds)(const void* context, std::string_view text) = nullptr;
    const void* context = nullptr;
};

// For each quote of line (indexed as QuoteStyles), whether its own text, without the quotes
// inside it, is casual speech: a sentence-final よ, ね, わ, じゃん, さ, もん, って, かな or
// かしら not after a polite form (そうですよ), or a contraction (食べちゃう, 見てる, やっとく,
// 知らねえ, and what fused holds: でけぇ, 痛え); not a quote with stiff or lordly marks (のだ,
// である, たまえ, かね, 我々, a sentence ended by が).
std::vector<bool> CasualQuotes(std::string_view line, int depth = 0, const FusedTest& fused = {});

// Whether line, in a document without 「」, is a line of speech (spec SPC-18): Marked when its
// text outside quotes holds a sentence-final particle of casual speech (CasualQuotes, not its
// contractions) or what fused holds; End when it ends without 。, ！, ？, an ellipsis, a comma or a dash, looking
// past trailing ☆, ♪, ー and closing brackets, and holds no label colon (名前：説明) or
// column of spaces. Never a line indented with an ideographic space.
enum class Spoken : std::uint8_t { No, End, Marked };
Spoken SpokenLine(std::string_view line, int depth = 0, const FusedTest& fused = {});

}  // namespace hdTrans::pipeline
