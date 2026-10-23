#pragma once
// Ruby markup and kana glosses of a line, as the name evidence reads them.
#include <cstddef>
#include <string>
#include <string_view>

namespace hdTrans::names::internal {

// A match of ruby markup or of a kana gloss: the base text and the reading, as byte ranges
// of the line.
struct RubyMatch {
    std::size_t begin = 0, end = 0;
    std::string_view base, reading;
};

// The leftmost ruby markup in s at or after from: |漢字《かな》, |漢字（かな）, 漢字《かな》
// or HTML <ruby>. base and reading are the parts of the alternative that matched.
bool FindRuby(std::string_view s, std::size_t from, RubyMatch& m);

// The leftmost kana gloss in s at or after from: 漢字（かな） and カタカナ(かな).
bool FindGloss(std::string_view s, std::size_t from, RubyMatch& m);

// The morae of a reading: its kana but the small ones; spaces and ・ are not counted, nor
// allowed at its ends. 0 for a run of mixed or other characters.
std::size_t ReadingMorae(std::string_view s);

// The leftmost reading in parentheses in s at or after from: a kanji run, then （かな） or
// (かな) of hiragana or of katakana, with as many morae as the run could be read with
// (梁（はり）, 錆（さ）びない, 和気藹々(わきあいあい)). Spaces and ・ may stand inside. A
// hiragana reading may stand in 「」 where hiragana other than と goes on after it
// (全「すべ」て). base is the kanji run.
bool FindReading(std::string_view s, std::size_t from, RubyMatch& m);

}  // namespace hdTrans::names::internal
