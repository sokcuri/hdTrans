#include <algorithm>
#include <cstdint>

#include "core/dict/codes.h"
#include "core/rules/rules.h"

namespace hdTrans::rules {

namespace {

constexpr int kMaxDepth = 32;
constexpr uint32_t kNoRule = UINT32_MAX;

struct OpSpelling {
    char field;
    std::string_view op;
    bool wordOperand;
    Op value;
};

constexpr OpSpelling kOps[] = {
    {'S', "==", false, Op::SrcEq},       {'S', "!=", false, Op::SrcNe},
    {'S', "==", true, Op::SrcEqW},       {'D', "==", false, Op::DstEq},
    {'D', "!=", false, Op::DstNe},       {'D', "=", false, Op::DstSet},
    {'D', "=", true, Op::DstSetW},       {'P', "==", false, Op::PumsaEq},
    {'P', "!=", false, Op::PumsaNe},     {'A', "==", false, Op::AttrEq},
    {'A', "!=", false, Op::AttrNe},      {'A', "-=", false, Op::AttrMinusEq},
    {'A', "-=", true, Op::AttrMinusEqW}, {'G', "==", false, Op::GramEq},
    {'G', "!=", false, Op::GramNe},      {'G', "-=", false, Op::GramMinusEq},
    {'U', "==", false, Op::GobiSrcEq},   {'U', "!=", false, Op::GobiSrcNe},
    {'Y', "==", false, Op::GobiDstEq},   {'Y', "=", false, Op::GobiDstSet},
    {'Y', "=", true, Op::GobiDstSetW},   {'M', "==", false, Op::GobiPumsaEq},
    {'T', "==", false, Op::GobiAttrEq},  {'T', "-=", false, Op::GobiAttrMinusEq},
    {'W', "==", false, Op::GobiGramEq},  {'W', "!=", false, Op::GobiGramNe},
};

// Parses a decimal integer with an optional sign into [lo, hi].
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

// A signed delta: '+' or '-' and digits, within int8_t.
bool ParseDelta(std::string_view s, int8_t& out) {
    int v = 0;
    if (s.empty() || (s[0] != '+' && s[0] != '-') || !ParseInt(s, -127, 127, v)) return false;
    out = static_cast<int8_t>(v);
    return true;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// One record line being parsed into program.
class Record {
public:
    Record(std::string_view line, Program& program) : rest_(line), program_(program) {}

    const char* error = nullptr;

    // The next token; a string literal token keeps its quotes and escapes.
    bool Next(std::string_view& token) {
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

    bool NextInt(int lo, int hi, int& out) {
        std::string_view t;
        if (!Next(t)) return false;
        return ParseInt(t, lo, hi, out) || Fail("bad number");
    }

    bool AtEnd() const { return rest_.empty(); }

    // Parses one body expression and appends its nodes; returns false on error.
    bool Expr(int depth) {
        if (depth > kMaxDepth) return Fail("expression nested too deeply");
        std::string_view t;
        if (!Next(t)) return false;
        NodeKind kind = NodeKind::AndAll;
        std::size_t opLength = 2;
        if (t.starts_with("||")) {
            kind = NodeKind::Or;
        } else if (t.starts_with("&&")) {
            kind = NodeKind::And;
        } else if (t.starts_with("&")) {
            opLength = 1;
        } else {
            return Atom(t);
        }

        int count = 0;
        if (!ParseInt(t.substr(opLength), 0, 1 << 20, count) || t[opLength] < '0' || t[opLength] > '9')
            return Fail("bad connective");
        const std::size_t self = program_.nodes.size();
        program_.nodes.push_back({kind, Op::SrcEq, 0, 0, static_cast<uint32_t>(count), 0});
        for (int k = 0; k < count; k++)
            if (!Expr(depth + 1)) return false;
        program_.nodes[self].second = static_cast<uint32_t>(program_.nodes.size());
        return true;
    }

private:
    bool Fail(const char* message) {
        if (error == nullptr) error = message;
        return false;
    }

    // FdOP ARG
    bool Atom(std::string_view t) {
        if (t.size() < 4) return Fail("bad atom");
        const char field = t[0];
        std::size_t opStart = 2;
        while (opStart < t.size() && t[opStart] >= '0' && t[opStart] <= '9') opStart++;
        Node n{NodeKind::Atom, Op::SrcEq, 0, 0, 0, 0};
        if (!ParseDelta(t.substr(1, opStart - 1), n.delta)) return Fail("bad atom delta");
        const std::string_view op = t.substr(opStart);

        std::string_view arg;
        if (!Next(arg)) return false;
        const bool wordOperand = arg[0] == '@';
        if (wordOperand) {
            if (!ParseDelta(arg.substr(1), n.rightDelta)) return Fail("bad word operand");
        } else if (!Literal(arg, field, n)) {
            return false;
        }
        for (const OpSpelling& o : kOps) {
            if (o.field == field && o.op == op && o.wordOperand == wordOperand) {
                n.op = o.value;
                program_.nodes.push_back(n);
                return true;
            }
        }
        return Fail("unknown operator");
    }

    // A part condition may name its pattern ("명사") instead of writing it ("1**").
    bool Literal(std::string_view t, char field, Node& n) {
        if (t.size() < 2 || t.front() != '"' || t.back() != '"') return Fail("bad operand");
        std::string named;
        std::string_view body = t.substr(1, t.size() - 2);
        if (field == 'P' || field == 'M') {
            named = dict::PartPatternOf(field, body);
            body = named;
        }
        std::string& pool = program_.literals;
        const std::size_t start = pool.size();
        for (std::size_t i = 0; i < body.size(); i++) {
            if (body[i] != '\\') {
                pool += body[i];
                continue;
            }
            const char e = i + 1 < body.size() ? body[i + 1] : '\0';
            if (e == '\\' || e == '"') {
                pool += e;
                i++;
            } else if (e == 'x' && i + 3 < body.size() && HexDigit(body[i + 2]) >= 0 && HexDigit(body[i + 3]) >= 0) {
                pool += static_cast<char>(HexDigit(body[i + 2]) * 16 + HexDigit(body[i + 3]));
                i += 3;
            } else {
                return Fail("bad escape in string literal");
            }
        }
        n.first = static_cast<uint32_t>(start);
        n.second = static_cast<uint32_t>(pool.size() - start);
        return true;
    }

    std::string_view rest_;
    Program& program_;
};

}  // namespace

bool Load(std::string_view text, Program& program, LoadError& error) {
    program = Program{};
    int line = 0;
    int localCount = -1, globalCount = -1;
    std::size_t locals = 0;
    auto fail = [&](const char* message) {
        error = {line, message};
        return false;
    };
    while (!text.empty()) {
        line++;
        std::size_t end = text.find('\n');
        std::string_view s = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!s.empty() && s.back() == '\r') s.remove_suffix(1);
        if (s.empty() || s[0] == '#') continue;

        Record r(s, program);
        std::string_view kind;
        int index = 0;
        if (!r.Next(kind)) return fail(r.error);
        if (localCount < 0) {
            int version = 0;
            if (kind != "rules" || !r.NextInt(1, 1, version) || !r.NextInt(0, 1 << 20, localCount) ||
                !r.NextInt(0, 1 << 20, globalCount) || !r.AtEnd())
                return fail(r.error ? r.error : "bad header");
            program.localRules.assign(static_cast<std::size_t>(localCount), kNoRule);
            program.globalRules.reserve(static_cast<std::size_t>(globalCount));
            continue;
        }
        if (kind == "L") {
            if (!r.NextInt(0, localCount - 1, index)) return fail(r.error);
            uint32_t& root = program.localRules[static_cast<std::size_t>(index)];
            if (root != kNoRule) return fail("local rule index repeated");
            root = static_cast<uint32_t>(program.nodes.size());
            locals++;
        } else if (kind == "G") {
            std::string_view pumsa;
            int number = 0, ugan = 0;
            if (!r.NextInt(-(1 << 30), 1 << 30, index)) return fail(r.error);
            if (program.globalRules.size() == static_cast<std::size_t>(globalCount)) return fail("more global rules than the header counts");
            if (!r.Next(pumsa) || !r.NextInt(-(1 << 30), 1 << 30, number) || !r.NextInt(0, 1, ugan))
                return fail(r.error);
            if (pumsa.size() != 1) return fail("pumsa key must be one byte");
            program.globalRules.push_back(
                {static_cast<unsigned char>(pumsa[0]), number, ugan == 1, static_cast<uint32_t>(program.nodes.size()), index});
        } else {
            return fail("unknown record");
        }
        const std::size_t root = program.nodes.size();
        if (!r.Expr(0)) return fail(r.error);
        if (program.nodes[root].kind != NodeKind::Or) return fail("rule body must be a || list");
        if (!r.AtEnd()) return fail("trailing tokens");
    }
    if (localCount < 0) return fail("no header");
    if (locals != static_cast<std::size_t>(localCount) || program.globalRules.size() != static_cast<std::size_t>(globalCount))
        return fail("rule count differs from the header");
    std::stable_sort(program.globalRules.begin(), program.globalRules.end(),
                     [](const GlobalRule& a, const GlobalRule& b) { return a.prio < b.prio; });
    return true;
}

}  // namespace hdTrans::rules
