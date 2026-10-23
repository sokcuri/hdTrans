// The flat format of rules::Load, parsed with the same acceptance and printed back.
#include <algorithm>
#include <cstdint>

#include "rulec/rulec.h"

namespace hdTrans::rulec {

namespace {

using rules::NodeKind;

constexpr int kMaxDepth = 32;

bool ParseInt(std::string_view s, long long lo, long long hi, int& out) {
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

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct Token {
    std::string_view text;
    uint32_t offset;
};

// One record line. Errors stop the record; the first one is reported.
class Record {
public:
    Record(uint32_t start, std::string_view line, Diagnostics& diags) : line_(line), start_(start), diags_(diags) {}

    bool failed = false;

    bool Next(Token& t, const char* what) {
        if (pos_ >= line_.size()) return Fail(End(), 0, std::string("record ends early, expected ") + what);
        const std::string_view rest = line_.substr(pos_);
        std::size_t end = 0;
        if (rest[0] == '"') {
            end = 1;
            while (end < rest.size() && rest[end] != '"') end += rest[end] == '\\' ? 2 : 1;
            if (end >= rest.size()) return Fail(Here(), static_cast<uint32_t>(rest.size()), "unterminated string literal");
            end++;
        } else {
            end = rest.find(' ');
            if (end == std::string_view::npos) end = rest.size();
        }
        if (end == 0) return Fail(Here(), 1, "tokens must be separated by one space");
        t = {rest.substr(0, end), Here()};
        pos_ += end;
        if (pos_ < line_.size()) {
            if (line_[pos_] != ' ') return Fail(Here(), 1, "tokens must be separated by one space");
            if (pos_ + 1 == line_.size()) return Fail(Here(), 1, "space at the end of the record");
            pos_++;
        }
        return true;
    }

    bool NextInt(long long lo, long long hi, int& out, const char* what) {
        Token t;
        if (!Next(t, what)) return false;
        if (ParseInt(t.text, lo, hi, out)) return true;
        return Fail(t.offset, Len(t), std::string("expected ") + what + ", found '" + std::string(t.text) + "'");
    }

    bool AtEnd() const { return pos_ >= line_.size(); }
    uint32_t Here() const { return start_ + static_cast<uint32_t>(pos_); }
    uint32_t End() const { return start_ + static_cast<uint32_t>(line_.size()); }

    bool Fail(uint32_t offset, uint32_t length, std::string message) {
        if (!failed) diags_.Error(offset, length, std::move(message));
        failed = true;
        return false;
    }

    bool Expr(rulec::Expr& e, int depth) {
        Token t;
        if (!Next(t, "an expression")) return false;
        if (depth > kMaxDepth) return Fail(t.offset, Len(t), "expression nested deeper than 32 lists");
        NodeKind kind = NodeKind::AndAll;
        std::size_t opLength = 2;
        if (t.text.starts_with("||")) {
            kind = NodeKind::Or;
        } else if (t.text.starts_with("&&")) {
            kind = NodeKind::And;
        } else if (t.text.starts_with("&")) {
            opLength = 1;
        } else {
            return Atom(t, e);
        }
        int count = 0;
        if (!ParseInt(t.text.substr(opLength), 0, 1 << 20, count) || t.text[opLength] < '0' || t.text[opLength] > '9')
            return Fail(t.offset, Len(t), "bad connective '" + std::string(t.text) + "': expected a count after " +
                                              std::string(t.text.substr(0, opLength)));
        e.kind = kind;
        e.offset = t.offset;
        e.length = Len(t);
        for (int k = 0; k < count; k++) {
            if (AtEnd() && !failed) {
                Fail(End(), 0, "'" + std::string(t.text) + "' has " + std::to_string(k) + " of " +
                                   std::to_string(count) + " operands when the record ends");
                diags_.Note(t.offset, Len(t), "list opened here");
                return false;
            }
            e.children.emplace_back();
            if (!Expr(e.children.back(), depth + 1)) return false;
            e.length = e.children.back().offset + e.children.back().length - e.offset;
        }
        return true;
    }

private:
    static uint32_t Len(const Token& t) { return static_cast<uint32_t>(t.text.size()); }

    bool Atom(const Token& t, rulec::Expr& e) {
        e.kind = NodeKind::Atom;
        e.offset = t.offset;
        if (t.text.size() < 4)
            return Fail(t.offset, Len(t), "bad atom '" + std::string(t.text) + "': expected FIELD, delta and operator");
        const char field = t.text[0];
        std::size_t opStart = 2;
        while (opStart < t.text.size() && t.text[opStart] >= '0' && t.text[opStart] <= '9') opStart++;
        if (!ParseDelta(t.text.substr(1, opStart - 1), e.delta))
            return Fail(t.offset + 1, static_cast<uint32_t>(opStart - 1), "bad delta: expected + or - and a number within 127");
        const std::string_view op = t.text.substr(opStart);
        Token arg;
        if (!Next(arg, "an operand")) return false;
        e.length = arg.offset + Len(arg) - t.offset;
        const bool word = arg.text[0] == '@';
        if (word) {
            if (!ParseDelta(arg.text.substr(1), e.rightDelta))
                return Fail(arg.offset, Len(arg), "bad word operand: expected @ and a signed delta");
        } else if (!Literal(arg, e.literal)) {
            return false;
        }
        for (const OpInfo& o : Ops()) {
            if (o.field == field && o.flat == op && o.word == word) {
                e.op = o.op;
                return true;
            }
        }
        std::string known;
        for (const OpInfo& o : Ops()) {
            if (o.field == field) known += std::string(known.empty() ? "" : ", ") + o.flat + (o.word ? " @d" : "");
        }
        if (known.empty())
            return Fail(t.offset, 1, std::string("unknown field '") + field + "': expected one of S D P A G U Y M T W");
        return Fail(t.offset + static_cast<uint32_t>(opStart), static_cast<uint32_t>(op.size()),
                    std::string("field ") + field + " has no operator '" + std::string(op) + "'" +
                        (word ? " with a word operand" : "") + "; it has " + known);
    }

    bool Literal(const Token& t, std::string& out) {
        if (t.text.size() < 2 || t.text.front() != '"' || t.text.back() != '"')
            return Fail(t.offset, Len(t), "bad operand '" + std::string(t.text) + "': expected a string or @delta");
        for (std::size_t i = 1; i + 1 < t.text.size(); i++) {
            if (t.text[i] != '\\') {
                out += t.text[i];
                continue;
            }
            const char c = t.text[i + 1];
            if (c == '\\' || c == '"') {
                out += c;
                i++;
            } else if (c == 'x' && i + 3 < t.text.size() && HexDigit(t.text[i + 2]) >= 0 && HexDigit(t.text[i + 3]) >= 0) {
                out += static_cast<char>(HexDigit(t.text[i + 2]) * 16 + HexDigit(t.text[i + 3]));
                i += 3;
            } else {
                return Fail(t.offset + static_cast<uint32_t>(i), 2, "bad escape: expected \\\\, \\\" or \\xHH");
            }
        }
        return true;
    }

    std::string_view line_;
    std::size_t pos_ = 0;
    uint32_t start_;
    Diagnostics& diags_;
};

void PrintLiteral(std::string& out, std::string_view s) { AppendQuoted(out, s); }

std::string Delta(int d) { return (d < 0 ? "-" : "+") + std::to_string(d < 0 ? -d : d); }

void PrintExpr(std::string& out, const Expr& e) {
    if (e.kind != NodeKind::Atom) {
        out += e.kind == NodeKind::Or ? "||" : e.kind == NodeKind::And ? "&&" : "&";
        out += std::to_string(e.children.size());
        for (const Expr& c : e.children) {
            out += ' ';
            PrintExpr(out, c);
        }
        return;
    }
    const OpInfo& o = Info(e.op);
    out += o.field;
    out += Delta(e.delta);
    out += o.flat;
    out += ' ';
    if (o.word) {
        out += '@';
        out += Delta(e.rightDelta);
    } else {
        PrintLiteral(out, e.literal);
    }
}

}  // namespace

File ParseFlat(const Source& source, Diagnostics& diags) {
    File file;
    int localCount = -1, globalCount = -1;
    uint32_t headerOffset = 0;
    std::vector<Rule>* lastList = nullptr;
    std::vector<uint32_t> seen;  // record offset by local index
    std::vector<std::string> comments;
    const std::string_view text(source.text);
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view s = text.substr(start, end - start);
        const uint32_t offset = static_cast<uint32_t>(start);
        start = end + 1;
        if (!s.empty() && s.back() == '\r') s.remove_suffix(1);
        if (s.starts_with("#test ")) {
            RuleTest t;
            if (lastList == nullptr || lastList->empty()) {
                diags.Error(offset, 5, "test line before the first rule");
            } else if (ParseTest(s.substr(6), offset + 6, diags, t)) {
                t.offset = offset;
                t.length = static_cast<uint32_t>(s.size());
                lastList->back().tests.push_back(std::move(t));
            }
            continue;
        }
        if (s.empty() || s[0] == '#') {
            if (localCount >= 0 && !s.empty()) comments.emplace_back(s.substr(1));
            continue;
        }

        Record r(offset, s, diags);
        Token kind;
        r.Next(kind, "a record kind");
        if (r.failed) continue;
        if (localCount < 0) {
            int version = 0;
            headerOffset = offset;
            if (kind.text != "rules") {
                r.Fail(kind.offset, static_cast<uint32_t>(kind.text.size()), "expected the header 'rules 1 LOCALCOUNT GLOBALCOUNT'");
                localCount = globalCount = 1 << 20;
                continue;
            }
            if (!r.NextInt(1, 1, version, "format version 1") || !r.NextInt(0, 1 << 20, localCount, "the local rule count") ||
                !r.NextInt(0, 1 << 20, globalCount, "the global rule count"))
                continue;
            if (!r.AtEnd()) r.Fail(r.Here(), static_cast<uint32_t>(s.size()) + offset - r.Here(), "trailing tokens after the header");
            continue;
        }
        Rule rule;
        rule.comments = std::move(comments);
        comments.clear();
        rule.offset = offset;
        rule.length = static_cast<uint32_t>(s.size());
        rule.kind = kind.text == "L" ? 'L' : kind.text == "G" ? 'G' : 0;
        if (rule.kind == 0) {
            r.Fail(kind.offset, static_cast<uint32_t>(kind.text.size()), "unknown record '" + std::string(kind.text) + "': expected L or G");
            continue;
        }
        std::vector<Rule>& list = rule.kind == 'L' ? file.local : file.global;
        Token indexToken;
        if (!r.Next(indexToken, rule.kind == 'L' ? "the rule index" : "the rule priority")) continue;
        const long long lo = rule.kind == 'L' ? 0 : -(1LL << 30), hi = rule.kind == 'L' ? localCount - 1 : 1LL << 30;
        if (!ParseInt(indexToken.text, lo, hi, rule.index)) {
            r.Fail(indexToken.offset, static_cast<uint32_t>(indexToken.text.size()),
                   rule.kind == 'L' ? "rule index '" + std::string(indexToken.text) + "' is not in 0.." +
                                          std::to_string(localCount - 1) + " (the header count)"
                                    : "bad rule priority '" + std::string(indexToken.text) + "'");
            continue;
        }
        if (rule.kind == 'L') {
            if (seen.size() < static_cast<std::size_t>(localCount)) seen.assign(static_cast<std::size_t>(localCount), UINT32_MAX);
            uint32_t& at = seen[static_cast<std::size_t>(rule.index)];
            if (at != UINT32_MAX) {
                r.Fail(indexToken.offset, static_cast<uint32_t>(indexToken.text.size()),
                       "local rule " + std::to_string(rule.index) + " is defined twice");
                diags.Note(at, 1, "first defined here");
                continue;
            }
            at = offset;
        }
        if (rule.kind == 'G') {
            Token pumsa;
            int ugan = 0;
            if (!r.Next(pumsa, "the pumsa key")) continue;
            if (pumsa.text.size() != 1) {
                r.Fail(pumsa.offset, static_cast<uint32_t>(pumsa.text.size()), "pumsa key must be one byte");
                continue;
            }
            rule.pumsa = static_cast<unsigned char>(pumsa.text[0]);
            if (!r.NextInt(-(1LL << 30), 1LL << 30, rule.number, "the rule number") || !r.NextInt(0, 1, ugan, "0 or 1 (stem rule)"))
                continue;
            rule.ugan = ugan == 1;
        }
        const uint32_t bodyAt = r.Here();
        r.Expr(rule.body, 0);
        if (!r.failed && rule.body.kind != NodeKind::Or) r.Fail(bodyAt, rule.body.length, "rule body must be a || list");
        if (!r.failed && !r.AtEnd()) r.Fail(r.Here(), r.End() - r.Here(), "trailing tokens after the rule body");
        list.push_back(std::move(rule));
        lastList = &list;
    }
    std::stable_sort(file.local.begin(), file.local.end(), [](const Rule& a, const Rule& b) { return a.index < b.index; });
    std::stable_sort(file.global.begin(), file.global.end(), [](const Rule& a, const Rule& b) { return a.index < b.index; });
    for (std::size_t i = 1; i < file.global.size(); i++) {
        if (file.global[i].index != file.global[i - 1].index) continue;
        diags.Warning("prio-tie", file.global[i].offset, file.global[i].length,
                      "global rules with priority " + std::to_string(file.global[i].index) + " run in file order");
        diags.list.back().kind = 'G';
        diags.list.back().index = file.global[i].index;
    }
    for (std::size_t i = 0; localCount != (1 << 20) && i < seen.size(); i++) {
        if (seen[i] == UINT32_MAX) diags.Error(headerOffset, 0, "local rule " + std::to_string(i) + " is missing");
    }
    if (localCount < 0) {
        diags.Error(0, 0, "no header");
    } else if (localCount != (1 << 20) &&
               (file.local.size() != static_cast<std::size_t>(localCount) || file.global.size() != static_cast<std::size_t>(globalCount))) {
        diags.Error(headerOffset, 0, "the header counts " + std::to_string(localCount) + " local and " +
                                         std::to_string(globalCount) + " global rules; the file has " +
                                         std::to_string(file.local.size()) + " and " + std::to_string(file.global.size()));
    }
    return file;
}

std::string PrintFlat(const File& file) {
    std::string out = "rules 1 " + std::to_string(file.local.size()) + " " + std::to_string(file.global.size()) + "\n";
    for (const Rule& r : file.local) {
        for (const std::string& c : r.comments) out += "#" + c + "\n";
        out += "L " + std::to_string(r.index) + " ";
        PrintExpr(out, r.body);
        out += '\n';
        for (const RuleTest& t : r.tests) out += "#test " + PrintTest(t) + "\n";
    }
    for (const Rule& r : file.global) {
        for (const std::string& c : r.comments) out += "#" + c + "\n";
        out += "G " + std::to_string(r.index) + " ";
        out += static_cast<char>(r.pumsa);
        out += " " + std::to_string(r.number) + " " + (r.ugan ? "1 " : "0 ");
        PrintExpr(out, r.body);
        out += '\n';
        for (const RuleTest& t : r.tests) out += "#test " + PrintTest(t) + "\n";
    }
    return out;
}

}  // namespace hdTrans::rulec
