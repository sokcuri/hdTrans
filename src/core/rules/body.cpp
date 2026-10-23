#include "core/rules/body.h"

namespace hdTrans::rules {

namespace {

constexpr int kMaxDepth = 32;

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// FdOP ARG
bool Atom(Tokens& tokens, BodySink& sink, std::string_view t) {
    if (t.size() < 4) return tokens.Fail("bad atom");
    std::size_t opStart = 2;
    while (opStart < t.size() && t[opStart] >= '0' && t[opStart] <= '9') opStart++;
    int8_t delta = 0;
    if (!ParseDelta(t.substr(1, opStart - 1), delta)) return tokens.Fail("bad atom delta");
    std::string_view arg;
    if (!tokens.Next(arg)) return false;
    return sink.Atom(t[0], delta, t.substr(opStart), arg, tokens);
}

bool Expr(Tokens& tokens, BodySink& sink, bool extended, int depth) {
    if (depth > kMaxDepth) return tokens.Fail("expression nested too deeply");
    std::string_view t;
    if (!tokens.Next(t)) return false;
    BodyKind kind = BodyKind::AndAll;
    std::size_t opLength = 2;
    if (t.starts_with("||")) {
        kind = BodyKind::Or;
    } else if (t.starts_with("&&")) {
        kind = BodyKind::And;
    } else if (t.starts_with("&")) {
        opLength = 1;
    } else if (extended && (t.starts_with("!") || t.starts_with("<") || t.starts_with(">"))) {
        kind = t[0] == '!' ? BodyKind::Not : t[0] == '<' ? BodyKind::ScanLeft : BodyKind::ScanRight;
        opLength = 1;
    } else {
        return Atom(tokens, sink, t);
    }

    int count = 0;
    if (!ParseInt(t.substr(opLength), 0, 1 << 20, count) || t[opLength] < '0' || t[opLength] > '9')
        return tokens.Fail("bad connective");
    if (kind == BodyKind::Not && count != 1) return tokens.Fail("! takes one operand: !1");
    const int operands = kind == BodyKind::ScanLeft || kind == BodyKind::ScanRight ? 2 : count;
    const std::size_t self = sink.List(kind, count);
    for (int k = 0; k < operands; k++)
        if (!Expr(tokens, sink, extended, depth + 1)) return false;
    sink.End(self);
    return true;
}

}  // namespace

bool Tokens::Next(std::string_view& token) {
    if (rest_.empty()) return Fail("record ends early");
    std::size_t end = 0;
    if (rest_[0] == '"') {
        end = 1;
        while (end < rest_.size() && rest_[end] != '"') end += rest_[end] == '\\' ? 2 : 1;
        if (end >= rest_.size()) return Fail("unterminated string literal");
        end++;
    } else {
        end = rest_.find(' ');
        if (end == std::string_view::npos) end = rest_.size();
    }
    if (end == 0) return Fail("empty token");
    token = rest_.substr(0, end);
    rest_.remove_prefix(end);
    if (!rest_.empty()) {
        if (rest_[0] != ' ' || rest_.size() == 1) return Fail("tokens must be separated by one space");
        rest_.remove_prefix(1);
    }
    return true;
}

bool Tokens::NextInt(int lo, int hi, int& out) {
    std::string_view t;
    if (!Next(t)) return false;
    return ParseInt(t, lo, hi, out) || Fail("bad number");
}

bool Tokens::Fail(const char* message) {
    if (error == nullptr) error = message;
    return false;
}

bool ParseInt(std::string_view s, int lo, int hi, int& out) {
    std::size_t i = 0;
    const bool negative = !s.empty() && s[0] == '-';
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) i = 1;
    if (i == s.size()) return false;
    long long v = 0;
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
        if (v > 1LL << 32) return false;
    }
    if (negative) v = -v;
    if (v < lo || v > hi) return false;
    out = static_cast<int>(v);
    return true;
}

bool ParseDelta(std::string_view s, int8_t& out) {
    int v = 0;
    if (s.empty() || (s[0] != '+' && s[0] != '-') || !ParseInt(s, -127, 127, v)) return false;
    out = static_cast<int8_t>(v);
    return true;
}

bool AppendLiteral(std::string_view t, std::string& out, const char*& error) {
    if (t.size() < 2 || t.front() != '"' || t.back() != '"') {
        error = "bad operand";
        return false;
    }
    return AppendEscaped(t.substr(1, t.size() - 2), out, error);
}

bool AppendEscaped(std::string_view body, std::string& out, const char*& error) {
    for (std::size_t i = 0; i < body.size(); i++) {
        if (body[i] != '\\') {
            out += body[i];
            continue;
        }
        const char e = i + 1 < body.size() ? body[i + 1] : '\0';
        if (e == '\\' || e == '"') {
            out += e;
            i++;
        } else if (e == 'x' && i + 3 < body.size() && HexDigit(body[i + 2]) >= 0 && HexDigit(body[i + 3]) >= 0) {
            out += static_cast<char>(HexDigit(body[i + 2]) * 16 + HexDigit(body[i + 3]));
            i += 3;
        } else {
            error = "bad escape in string literal";
            return false;
        }
    }
    return true;
}

bool ParseBody(Tokens& tokens, BodySink& sink, bool extended) { return Expr(tokens, sink, extended, 0); }

}  // namespace hdTrans::rules
