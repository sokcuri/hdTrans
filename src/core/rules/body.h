#pragma once
// The body syntax of the rules (docs/rule-language.md), shared by the rules program (load.cpp)
// and the construction table of the rewrite layer (core/word/constructions.h): tokens separated
// by one space, a string literal in double quotes with \\, \" and \xHH as its only escapes, and a
// prefix expression of connectives over atoms "FdOP ARG".
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace hdTrans::rules {

// The tokens of one record line. The first failure sets error; later ones keep it.
class Tokens {
public:
    explicit Tokens(std::string_view line) : rest_(line) {}

    const char* error = nullptr;

    // The next token; a string literal token keeps its quotes and escapes.
    bool Next(std::string_view& token);
    bool NextInt(int lo, int hi, int& out);
    bool AtEnd() const { return rest_.empty(); }
    bool Fail(const char* message);

private:
    std::string_view rest_;
};

// A decimal integer with an optional sign, within [lo, hi].
bool ParseInt(std::string_view s, int lo, int hi, int& out);
// A signed delta: '+' or '-' and digits, within int8_t.
bool ParseDelta(std::string_view s, int8_t& out);
// Appends the bytes of the string literal token t to out; false for a token that is not one.
bool AppendLiteral(std::string_view t, std::string& out, const char*& error);
// The same for the text between the quotes.
bool AppendEscaped(std::string_view body, std::string& out, const char*& error);

// The connectives: ||N, &&N and &N, and in the extended syntax !1 (not) and the scans <N and >N.
enum class BodyKind : uint8_t { Or, And, AndAll, Not, ScanLeft, ScanRight };

// Receives a body as it is parsed, in prefix order: each connective before its operands, closed
// by End with the handle List returned.
class BodySink {
public:
    // count is the operand count of a list, or the bound of a scan (0 for none).
    virtual std::size_t List(BodyKind kind, int count) = 0;
    virtual void End(std::size_t list) = 0;
    // The atom FdOP ARG, arg as its raw token; false with tokens.Fail for an atom the sink lacks.
    virtual bool Atom(char field, int8_t delta, std::string_view op, std::string_view arg, Tokens& tokens) = 0;

protected:
    ~BodySink() = default;
};

// Parses one expression from tokens into sink. extended allows !1 and the scans <N and >N, whose
// two operands are the target and the barrier.
bool ParseBody(Tokens& tokens, BodySink& sink, bool extended);

}  // namespace hdTrans::rules
