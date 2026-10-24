#pragma once
// Engine codes of a dictionary sense and their typed views.
//
// A sense carries four codes, each read from a column of its file:
//   - the part code (pos): tag letter, class digit, subclass and kind, "M429";
//   - the semantic codes (sem): 3-byte codes concatenated in order, "QPNVHn";
//   - the grammar codes (gram): 2-byte codes concatenated in order, "WaWb";
//   - the dict code (dict): the source dictionary, "GN", "SO", "BZ", ...
// Every view keeps the bytes as written, malformed ones included: a semantic text
// with a space ("QCT VPl"), a grammar text of odd length ("Njd"), a part code of
// other length. Tests on them are byte tests, as the rules and the word stage make
// them.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core/text/lifetime.h"

namespace hdTrans::dict {

// The class digit of a part code (byte 1).
enum class WordClass : uint8_t {
    Adnominal,      // '0': adnominals and conjunctive expressions
    Noun,           // '1'
    Particle,       // '2'
    Adverb,         // '3'
    Verb,           // '4'
    Ending,         // '5'
    Symbol,         // '6'
    Adjective,      // '7'
    AdjectivalNoun, // '8'
    Phrase,         // '9'
    Other = 255,    // any other byte, or none
};

// A part code as 4 raw bytes and its length. Bytes are read as C chars: a byte
// past the end is 0 and bytes >= 0x80 are negative, so the tag order of the
// lookups is a signed comparison. A text longer than 4 bytes keeps its length
// and its first 4 bytes.
class PartCode {
public:
    constexpr PartCode() = default;
    explicit PartCode(std::string_view text);

    // Byte i, 0 past the end.
    constexpr int8_t At(std::size_t i) const {
        return i < 4 && i < size_ ? static_cast<int8_t>(bytes_[i]) : 0;
    }
    constexpr char Tag() const { return static_cast<char>(At(0)); }
    constexpr char ClassDigit() const { return static_cast<char>(At(1)); }
    constexpr char Sub() const { return static_cast<char>(At(2)); }
    constexpr char Kind() const { return static_cast<char>(At(3)); }

    constexpr bool Empty() const { return size_ == 0; }
    constexpr std::size_t Size() const { return size_; }
    // The kept bytes: the whole text up to 4 bytes.
    std::string_view Text() const HDTRANS_LIFETIMEBOUND { return {bytes_.data(), size_ < 4 ? size_ : std::size_t{4}}; }
    // The bytes after the tag ("429" of "M429"), the code the rules see.
    std::string_view Code() const HDTRANS_LIFETIMEBOUND { return Text().substr(Empty() ? 0 : 1); }

    // Whether the tag is one of the bytes of set ("NIK").
    bool TagIn(std::string_view set) const { return !Empty() && set.find(Tag()) != std::string_view::npos; }
    // Whether the tag is an upper-case ASCII letter, the tags of the lookup buckets.
    constexpr bool LetterTag() const { return Tag() >= 'A' && Tag() <= 'Z'; }
    WordClass Class() const;
    // Kind minus '0' (the conjugation row of a verb); may be negative or above 9.
    constexpr int VerbKind() const { return At(3) - '0'; }
    // Subclass minus '0' (the branch an ending attaches to).
    constexpr int EndingBranch() const { return At(2) - '0'; }
    // Whether the class digits are equal, 0 standing for a missing byte.
    constexpr bool SameClass(PartCode o) const { return At(1) == o.At(1); }
    // Whether the subclasses are equal, 0 standing for a missing byte.
    constexpr bool SameSub(PartCode o) const { return At(2) == o.At(2); }

    constexpr bool operator==(const PartCode&) const = default;

private:
    std::array<char, 4> bytes_{};
    uint8_t size_ = 0;  // 255 for 255 bytes or more
};

// Codes of a fixed width concatenated, as text: the codes are the Width-byte
// chunks of the text in order, and a shorter rest is no code.
template <std::size_t Width>
class CodeList {
public:
    CodeList() = default;
    explicit CodeList(std::string_view text) : text_(text) {}

    std::string_view Text() const HDTRANS_LIFETIMEBOUND { return text_; }
    bool Empty() const { return text_.empty(); }
    bool Aligned() const { return text_.size() % Width == 0; }

    std::size_t Count() const { return text_.size() / Width; }
    std::string_view At(std::size_t i) const HDTRANS_LIFETIMEBOUND { return std::string_view(text_).substr(i * Width, Width); }
    std::string_view First() const HDTRANS_LIFETIMEBOUND { return Count() > 0 ? At(0) : std::string_view(); }
    std::string_view Last() const HDTRANS_LIFETIMEBOUND { return Count() > 0 ? At(Count() - 1) : std::string_view(); }
    // The bytes after the last whole code.
    std::string_view Rest() const HDTRANS_LIFETIMEBOUND { return std::string_view(text_).substr(Count() * Width); }

    // Whether s occurs anywhere in the text, at any offset.
    bool Contains(std::string_view s) const { return text_.find(s) != std::string::npos; }
    // Whether the text starts with s (for a whole code: the first code is s).
    bool HasPrefix(std::string_view s) const { return std::string_view(text_).starts_with(s); }
    bool HasSuffix(std::string_view s) const { return std::string_view(text_).ends_with(s); }
    // Whether one of the codes is code.
    bool HasCode(std::string_view code) const {
        for (std::size_t i = 0; i < Count(); i++) {
            if (At(i) == code) return true;
        }
        return false;
    }
    // Whether a code c has lo <= c <= hi in byte order (QAA..QAH).
    bool HasCodeIn(std::string_view lo, std::string_view hi) const {
        for (std::size_t i = 0; i < Count(); i++) {
            if (At(i) >= lo && At(i) <= hi) return true;
        }
        return false;
    }

    void Append(std::string_view s) { text_.append(s); }
    // Removes the suffix s; false when the text does not end with it.
    bool CutSuffix(std::string_view s) {
        if (!HasSuffix(s)) return false;
        text_.resize(text_.size() - s.size());
        return true;
    }

    bool operator==(const CodeList&) const = default;

private:
    std::string text_;
};

using SemCodes = CodeList<3>;

class GramCodes : public CodeList<2> {
public:
    using CodeList<2>::CodeList;
    // The digit after the first byte 'J' at any offset, when it is '0'..'7'; 0
    // otherwise (no 'J', or another byte after the first one).
    char PunctDigit() const;
};

// The source dictionary of a sense; "SO" senses never match.
class DictCode {
public:
    DictCode() = default;
    explicit DictCode(std::string_view text) : text_(text) {}
    std::string_view Text() const HDTRANS_LIFETIMEBOUND { return text_; }
    bool SourceOnly() const { return text_ == "SO"; }
    bool operator==(const DictCode&) const = default;
    bool operator==(std::string_view s) const { return text_ == s; }

private:
    std::string text_;
};

// The codes of a sense. Codes are shared: a CodeTable holds each distinct tuple
// once.
struct Codes {
    PartCode part;
    std::string partText;  // the pos column as written (part keeps 4 bytes of it)
    SemCodes sem;
    GramCodes gram;
    DictCode dict;

    // The same codes with "XXX" appended to sem, itself when sem holds XXX. Set by
    // CodeTable.
    const Codes* withXXX = nullptr;
    // The same codes with the dict code GN, the codes of a plain entry. Set by
    // CodeTable.
    const Codes* asPlain = nullptr;
};

// The local rule number of a sense: 0 for none, otherwise the rule the selection
// runs for it (a number outside the rule table runs nothing).
using RuleNumber = int32_t;

// The codes of a pos value. A value with '-' or ',' or starting with a lower-case
// letter is a tag of the part-of-speech table: UniDic or IPAdic pos1-pos2-pos3-pos4,
// then ",cType" for conjugating words, in Japanese or in its English form; a third
// field cForm may name the dictionary or stem form, "*" leaves a field empty, and
// single-segment tags (副詞, adverb) are tags too. Any other value is a part code,
// kept as written. Returns false for an unknown tag or a tag without part code.
bool PosCodes(std::string_view pos, std::string_view& partText, std::string_view& sem, std::string_view& gram);

// The Korean name of a part code ("N110" is 일반 명사), or an empty view for a code the
// schema does not carry.
std::string_view PartNameOf(std::string_view code);

// The pattern a rule condition value stands for in part field ('P' or 'M'). A name is
// looked up in the pattern table, and names joined by '+' are joined patterns
// ("명사+부사" is "1**3**"); any other value comes back as it is. The result is longer
// than value only for joined names, so it goes into a string the caller holds.
std::string PartPatternOf(char field, std::string_view value);

// Why a target with a marker family cannot be loaded: a family ("먹&AA@") must end the
// target, and its marker must fit the class of part ('&' families conjugate verb stems,
// class 4, '$' adjective stems, class 7, '%' adjectival nouns, class 8). Empty for a
// target without a family and for a match.
std::string MarkerClassError(std::string_view target, PartCode part);

// Why the target of a noun line of key is not the noun it should be: one word that ends in
// 하기, a 하다 verb made a noun (출항하기), which before する reads as the noun of 하다 again.
// The target is the noun (출항). Empty for other targets, for a phrase (직장 구하기), for
// 하기 itself, a proper noun (part 13x or semantic codes sem of a name, Q..) and a key ending
// in 期 (夏期 하기, 氷河期 빙하기).
std::string VerbNounError(std::string_view key, std::string_view target, PartCode part, std::string_view sem);

namespace internal {

struct PosTag {
    const char* tag;
    const char* part;
    const char* sem;
    const char* gram;
};

// One row of the part-of-speech schema (src/core/dict/pos_schema.tsv): the code, its
// class digit, its Korean name, and the standard analyzer tags that map to it, comma
// separated.
struct PartName {
    const char* code;
    const char* klass;
    const char* universal;  // the Universal Dependencies class the code belongs to
    const char* name;
    const char* standard;
};

// The schema of every part code the dictionaries use, sorted by code.
std::span<const PartName> PartSchema();

// A named part condition: field is P or M, name is the Korean name a rule may
// write in place of the pattern, pattern is the code form it stands for.
struct PartPattern {
    const char* field;
    const char* name;
    const char* pattern;
};

// The named part conditions, sorted by field then name.
std::span<const PartPattern> PartPatternTable();

// The tags with the codes of the first table row carrying them, sorted by tag
// bytes; an empty part code marks a tag without engine counterpart.
std::span<const PosTag> PosTagTable();

}  // namespace internal

}  // namespace hdTrans::dict
