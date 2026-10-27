#include <algorithm>
#include <cstdint>

#include "core/dict/codes.h"
#include "core/output/inflect.h"
#include "core/rules/body.h"
#include "core/rules/rules.h"

namespace hdTrans::rules {

namespace {

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

bool FamilyChar(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// Why the marker family ("&AV@") in the text value that a D= or Y= assignment writes cannot be
// expanded: a word's text may end in a family of the marker tables, an ending's text holds none.
// Null when it can.
const char* AssignedMarkerError(Op op, std::string_view value) {
    if (op != Op::DstSet && op != Op::GobiDstSet) return nullptr;
    for (std::size_t i = 0; i + 4 <= value.size(); i++) {
        const char c = value[i];
        if ((c != '&' && c != '$' && c != '%') || !FamilyChar(value[i + 1]) || !FamilyChar(value[i + 2]) ||
            value[i + 3] != '@') {
            continue;
        }
        if (op == Op::GobiDstSet) return "an ending text holds a marker family";
        if (i + 4 != value.size()) return "a marker family does not end the assigned text";
        if (output::internal::FindKoreanFamily(value.substr(i, 4)) == nullptr) {
            return "the assigned marker family is not in the marker tables";
        }
    }
    return nullptr;
}

// The nodes of a rule body appended to a program.
class RuleSink final : public BodySink {
public:
    explicit RuleSink(Program& program) : program_(program) {}

    std::size_t List(BodyKind kind, int count) override {
        const NodeKind k = kind == BodyKind::Or ? NodeKind::Or : kind == BodyKind::And ? NodeKind::And : NodeKind::AndAll;
        program_.nodes.push_back({k, Op::SrcEq, 0, 0, static_cast<uint32_t>(count), 0});
        return program_.nodes.size() - 1;
    }

    void End(std::size_t list) override { program_.nodes[list].second = static_cast<uint32_t>(program_.nodes.size()); }

    bool Atom(char field, int8_t delta, std::string_view op, std::string_view arg, Tokens& tokens) override {
        Node n{NodeKind::Atom, Op::SrcEq, delta, 0, 0, 0};
        const bool wordOperand = arg[0] == '@';
        if (wordOperand) {
            if (!ParseDelta(arg.substr(1), n.rightDelta)) return tokens.Fail("bad word operand");
        } else if (!Literal(arg, field, n, tokens)) {
            return false;
        }
        for (const OpSpelling& o : kOps) {
            if (o.field == field && o.op == op && o.wordOperand == wordOperand) {
                n.op = o.value;
                if (!wordOperand) {
                    const std::string_view value = std::string_view(program_.literals).substr(n.first, n.second);
                    if (const char* why = AssignedMarkerError(n.op, value)) return tokens.Fail(why);
                }
                program_.nodes.push_back(n);
                return true;
            }
        }
        return tokens.Fail("unknown operator");
    }

private:
    // A part condition may name its pattern ("명사") instead of writing it ("1**").
    bool Literal(std::string_view t, char field, Node& n, Tokens& tokens) {
        if (t.size() < 2 || t.front() != '"' || t.back() != '"') return tokens.Fail("bad operand");
        std::string named;
        std::string_view body = t.substr(1, t.size() - 2);
        if (field == 'P' || field == 'M') {
            named = dict::PartPatternOf(field, body);
            body = named;
        }
        std::string& pool = program_.literals;
        const std::size_t start = pool.size();
        const char* error = nullptr;
        if (!AppendEscaped(body, pool, error)) return tokens.Fail(error);
        n.first = static_cast<uint32_t>(start);
        n.second = static_cast<uint32_t>(pool.size() - start);
        return true;
    }

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

        Tokens r(s);
        RuleSink sink(program);
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
        if (!ParseBody(r, sink, false)) return fail(r.error);
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
