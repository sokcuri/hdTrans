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

// The styles of line: element 0 for its text outside 「」 and 『』, element k for its k-th
// quote (a 「 or 『 outside any quote, with the quotes inside it); 「 and 『 open a quote,
// 」 and 』 close one, and a closing bracket outside a quote is ignored. With depth, the
// line starts inside a quote an earlier line opened, which is its first quote.
std::vector<std::uint8_t> QuoteStyles(std::string_view line, int depth = 0);

}  // namespace hdTrans::pipeline
