#pragma once
// Dictionary files: tab-separated UTF-8 text, one row per line.
//
// TsvReader splits the input into lines and fields. Lines end at "\n"; one "\r"
// before it (or at the end of the input) is dropped. A UTF-8 byte order mark at
// the start of the first line is dropped. Empty lines and lines starting with the
// comment prefix are skipped. Fields are split at every tab; there is no quoting
// or escaping.
//
// SenseReader reads a column file of senses on top of it: the first row is the
// header naming the columns; key, ko, pos and id are required, sem, gram, dict,
// rule, flags, prio and reading are read and other columns are ignored. A row may
// have fewer fields than the header (the missing ones are empty), not more. flags
// is a list separated by white space of xxx, kana, hidden, replace and llm (no
// effect).
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::dict {

enum class TsvErrorKind : uint8_t {
    Read,            // the read function failed
    NoKeyColumn,     // the header has no such column; nothing else is read
    NoKoColumn,
    NoPosColumn,
    NoIdColumn,
    TooManyColumns,  // the row has more fields than the header
    BadNumber,       // rule or prio is not a decimal number
    BadFlag,         // flags holds an unknown flag
    EmptyKeyOrId,
    DuplicateColumn,  // the header names one column twice; the second would be ignored
};

struct TsvError {
    int line;  // 1-based, counting every line of the input; 0 for Read
    TsvErrorKind kind;
};

// Fills buf with up to size bytes and returns their number: 0 at the end of the
// input, negative on a read error.
using ReadFunc = std::ptrdiff_t (*)(void* context, char* buf, std::size_t size);

class TsvReader {
public:
    // Reads text, which must outlive the reader; fields point into it.
    explicit TsvReader(std::string_view text, std::string_view comment = "#");
    // Reads the input of read in chunks; fields point into a buffer of the reader.
    TsvReader(ReadFunc read, void* context, std::string_view comment = "#");

    // Moves to the next row. false at the end of the input or on a read error.
    bool Next();

    // The fields of the row, valid until the next call of Next.
    const std::vector<std::string_view>& Fields() const { return fields_; }
    // The 1-based line number of the row.
    int Line() const { return line_; }
    bool Failed() const { return failed_; }

private:
    bool NextLine(std::string_view& line);

    std::string_view text_;
    std::size_t pos_ = 0;  // start of the unread part of text_ or buffer_
    ReadFunc read_ = nullptr;
    void* context_ = nullptr;
    std::string buffer_;
    bool eof_ = false;
    bool failed_ = false;
    std::string_view comment_;
    int line_ = 0;
    std::vector<std::string_view> fields_;
};

// One sense row of a column file. The views are valid until the next call of
// SenseReader::Next.
struct Sense {
    std::string_view key;
    // The second key the flag kana files the sense under (the reading column when
    // it differs from key); empty for none.
    std::string_view reading;
    std::string_view ko;
    std::string_view pos;  // a PartCode or a part-of-speech tag, as written
    std::string_view sem;  // empty: the codes of pos
    std::string_view gram; // empty: the codes of pos
    std::string_view dict;
    // The body of the local rule the row names, when the row carries it. Empty for a row
    // that only names the rule, which the program then reads from the rule files.
    std::string_view ctx;
    int32_t rule = 0;
    int32_t prio = 0;
    // The number the id column ends in (1234 for 1234 or jisjk:1234), otherwise the
    // line number.
    int32_t id = 0;
    bool xxx = false;      // flag xxx: append XXX to the semantic codes unless present
    bool replace = false;  // flag replace
};

class SenseReader {
public:
    explicit SenseReader(TsvReader& rows) : rows_(rows) {}

    // Reads the next sense; rows with the flag hidden and rows with errors are
    // skipped. false at the end of the input or after a header or read error.
    bool Next(Sense& out);

    // The errors so far, in line order. A file with errors is not to be used.
    const std::vector<TsvError>& Errors() const { return errors_; }

private:
    enum Column { kKey, kKo, kPos, kSem, kGram, kDict, kRule, kCtx, kFlags, kPrio, kReading, kId, kColumnCount };

    bool ReadHeader();

    TsvReader& rows_;
    int index_[kColumnCount] = {};  // field of each column, -1 when absent
    std::size_t width_ = 0;         // number of header fields; 0 before the header
    bool stopped_ = false;
    std::vector<TsvError> errors_;
};

namespace internal {

// Parses a decimal number with an optional sign that fits in 64 bits, keeping its
// low 32 bits.
bool ParseInt32(std::string_view s, int32_t& out);

}  // namespace internal

}  // namespace hdTrans::dict
