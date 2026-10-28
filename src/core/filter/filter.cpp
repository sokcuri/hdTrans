#include "core/filter/filter.h"

#include <algorithm>
#include <climits>
#include <iterator>
#include <unordered_map>

#include "core/dict/layer.h"
#include "core/filter/ehnd.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "pcre2.h"

namespace hdTrans::filter {

std::u16string Utf16(std::string_view s) {
    std::u16string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        i += r.size;
        const char32_t cp = r.cp >= 0xD800 && r.cp <= 0xDFFF ? text::kReplacement : r.cp;
        if (cp >= 0x10000) {
            out += static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10));
            out += static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            out += static_cast<char16_t>(cp);
        }
    }
    return out;
}

std::string Utf8(std::u16string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (std::size_t i = 0; i < s.size(); i++) {
        char32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<char32_t>(s[++i]) - 0xDC00);
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = text::kReplacement;
        }
        text::AppendRune(out, cp);
    }
    return out;
}

namespace {

// _wtoi: leading blanks, a sign, then digits up to the first other character; 0 for
// none, clamped to int.
int ToInt(std::u16string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == u' ' || (s[i] >= u'\t' && s[i] <= u'\r'))) i++;
    bool negative = false;
    if (i < s.size() && (s[i] == u'+' || s[i] == u'-')) negative = s[i++] == u'-';
    long long v = 0;
    for (; i < s.size() && s[i] >= u'0' && s[i] <= u'9'; i++) {
        v = std::min<long long>(v * 10 + (s[i] - u'0'), static_cast<long long>(INT_MAX) + 1);
    }
    if (negative) v = -v;
    return static_cast<int>(std::clamp<long long>(v, INT_MIN, INT_MAX));
}

// The fields of line (with its "\n", if any) from start: they end at a tab, at "\n", at the
// end, and at the second "/" of "//", which with stop also ends the line; the field
// before "//" keeps its first "/".
std::vector<std::u16string_view> Fields(std::u16string_view line, std::size_t start, bool stop) {
    std::vector<std::u16string_view> out;
    for (std::size_t i = start, prev = start; i <= line.size(); i++) {
        const bool comment = i < line.size() && i > 0 && line[i] == u'/' && line[i - 1] == u'/';
        if (i == line.size() || line[i] == u'\t' || line[i] == u'\n' || comment) {
            out.push_back(line.substr(prev, i - prev));
            prev = i + 1;
            if (comment && stop) break;
        }
    }
    return out;
}

// Splits text into lines that keep their "\n".
std::vector<std::u16string_view> LinesOf(std::u16string_view text) {
    std::vector<std::u16string_view> out;
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t nl = text.find(u'\n', pos);
        const std::size_t end = nl == std::u16string_view::npos ? text.size() : nl + 1;
        out.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return out;
}

// The text after prefix and before the next tab, and the position after that tab; false
// without a tab.
bool Name(std::u16string_view line, std::size_t at, std::u16string& name, std::size_t& next) {
    const std::size_t tab = line.find(u'\t', at);
    if (tab == std::u16string_view::npos) return false;
    name = std::u16string(line.substr(at, tab - at));
    next = tab + 1;
    return true;
}

bool StartsAt(std::u16string_view line, std::size_t at, std::u16string_view s) {
    return at <= line.size() && line.substr(at).starts_with(s);
}

// A row as read, before its patterns are compiled.
struct ParsedRow {
    std::u16string src, dest, aux;
    int layer = 0, regex = 0;
    bool hasAux = false;
    std::u16string gate, sets;
    bool on = false;
    int kind = 0;  // 0 plain, 1 guard, 2 guarded
    std::string file;
    int line = 0;
    std::size_t source = 0;
};

// The builder of the toggles: names to numbers, and the declared states.
struct ToggleTable {
    std::unordered_map<std::u16string, int32_t> ids;
    std::vector<char> states;
    std::vector<bool> declared;

    int32_t Id(const std::u16string& name) {
        const auto [it, added] = ids.emplace(name, static_cast<int32_t>(states.size()));
        if (added) {
            states.push_back(0);
            declared.push_back(false);
        }
        return it->second;
    }
    // The first declaration of a name holds.
    void Declare(const std::u16string& name, bool on) {
        const int32_t id = Id(name);
        if (declared[static_cast<std::size_t>(id)]) return;
        declared[static_cast<std::size_t>(id)] = true;
        states[static_cast<std::size_t>(id)] = on;
    }
};

// Reads the rows of a filter file; declarations go to toggles.
void ReadRows(const Source& source, std::size_t ordinal, std::vector<ParsedRow>& rows,
              ToggleTable& toggles, std::vector<Error>& errors) {
    const std::u16string text = Utf16(source.text);
    int number = 0;
    for (const std::u16string_view line : LinesOf(text)) {
        number++;
        if (line.starts_with(u"//") || (source.native && line.starts_with(u"#"))) continue;
        ParsedRow row;
        row.file = source.name;
        row.line = number;
        row.source = ordinal;
        std::size_t start = 0;
        bool malformed = false;
        if (line.starts_with(u"_TGL\t")) {
            std::size_t next = 0;
            if (StartsAt(line, 5, u"_DECR:")) {
                std::u16string name;
                if (Name(line, 11, name, next) && (StartsAt(line, next, u"_ON") || StartsAt(line, next, u"_OFF"))) {
                    toggles.Declare(name, StartsAt(line, next, u"_ON"));
                    continue;
                }
                malformed = true;
            } else if (StartsAt(line, 5, u"_CONN:")) {
                malformed = !Name(line, 11, row.gate, next);
                start = next;
                if (!malformed && StartsAt(line, next, u"_ON:")) {
                    malformed = !Name(line, next + 4, row.sets, start);
                    row.on = true;
                } else if (!malformed && StartsAt(line, next, u"_OFF:")) {
                    malformed = !Name(line, next + 5, row.sets, start);
                } else if (!malformed && StartsAt(line, next, u"_SkipLayer\t")) {
                    row.kind = 1;
                    start = next + 11;
                }
            } else if (StartsAt(line, 5, u"_ON:")) {
                malformed = !Name(line, 9, row.sets, start);
                row.on = true;
            } else if (StartsAt(line, 5, u"_OFF:")) {
                malformed = !Name(line, 10, row.sets, start);
            }
        } else if (line.starts_with(u"_SkipLayer\t")) {
            row.kind = 1;
            start = 11;
        } else if (line.starts_with(u"_skip_sub\t")) {
            row.kind = 2;
            start = 10;
        }
        if (malformed) {
            errors.push_back({source.name, number, "a _TGL line without its tab-separated name"});
            continue;
        }
        const std::vector<std::u16string_view> f = Fields(line, start, true);
        if (f.size() < 3) {
            if (!source.native && !line.empty()) {
                errors.push_back({source.name, number, "a filter line needs three tab-separated fields",
                                  ErrorKind::IgnoredShortFields});
            }
            continue;
        }
        row.src = f[0];
        row.dest = f[1];
        row.layer = ToInt(f[2]);
        if (f.size() > 3) row.regex = ToInt(f[3]);
        if (f.size() > 4 && f[4].starts_with(u"_AUX:")) {
            row.hasAux = true;
            row.aux = f[4].substr(5);
        }
        rows.push_back(std::move(row));
    }
}

// Whether c may have another case, so that a caseless pattern could match it otherwise.
bool Cased(uint32_t c) {
    return c < 0x0590 || (c >= 0x10A0 && c < 0x1400) || (c >= 0x1C80 && c < 0x2000) || (c >= 0x2100 && c < 0x2190) ||
           (c >= 0x24B6 && c < 0x24EA) || (c >= 0x2C00 && c < 0x2E00) || (c >= 0xA640 && c < 0xA800) ||
           (c >= 0xAB70 && c < 0xABC0) || (c >= 0xD800 && c < 0xE000) || (c >= 0xFF21 && c < 0xFF5B);
}

}  // namespace

std::shared_ptr<const Filters> Build(const std::vector<Source>& sources, std::vector<Error>& errors,
                                     std::vector<std::size_t>* acceptedCounts) {
    if (acceptedCounts) acceptedCounts->assign(sources.size(), 0);
    if (sources.empty()) return nullptr;
    auto out = std::make_shared<Filters>();
    Filters& fs = *out;
    ToggleTable toggles;
    std::vector<ParsedRow> rows[2];
    struct ParsedCondition {
        Stage stage;
        int layer;
        std::string label;
        std::u16string cond;
        std::string file;
        int line;
        std::size_t source;
    };
    std::vector<ParsedCondition> conditions;
    dict::internal::Layer user(false);
    std::vector<UserRow> userRows;

    for (std::size_t ordinal = 0; ordinal < sources.size(); ++ordinal) {
        const Source& s = sources[ordinal];
        switch (s.kind) {
        case Source::Kind::PreFilter:
            ReadRows(s, ordinal, rows[0], toggles, errors);
            break;
        case Source::Kind::PostFilter:
            ReadRows(s, ordinal, rows[1], toggles, errors);
            break;
        case Source::Kind::SkipLayer: {
            const std::u16string text = Utf16(s.text);
            int number = 0;
            for (const std::u16string_view line : LinesOf(text)) {
                number++;
                if (line.starts_with(u"//")) continue;
                const std::vector<std::u16string_view> f = Fields(line, 0, false);
                if (f.size() < 2 || !(f[0].starts_with(u"PRE") || f[0].starts_with(u"POST"))) continue;
                conditions.push_back({f[0].starts_with(u"PRE") ? Stage::Pre : Stage::Post, ToInt(f[1]), Utf8(f[1]),
                                      f.size() > 2 ? std::u16string(f[2]) : std::u16string(), s.name, number, ordinal});
            }
            break;
        }
        case Source::Kind::UserDict: {
            std::vector<UserRow> r = ParseUserDict(s.text, s.name, errors);
            if (acceptedCounts) (*acceptedCounts)[ordinal] = r.size();
            std::move(r.begin(), r.end(), std::back_inserter(userRows));
            break;
        }
        }
    }

    // A pattern that does not compile is skipped with PCRE2's message. A lookbehind must
    // have a fixed length in each of its branches.
    pcre2_compile_context_16* context = pcre2_compile_context_create_16(nullptr);
    pcre2_set_max_varlookbehind_16(context, 0);
    // units receives the code units every match starts with and holds, as patterns of
    // matcher, -1 for none.
    auto compile = [&](const std::u16string& pattern, Matcher& matcher, int32_t& regex, int32_t (&units)[2],
                       const std::string& file, int line) {
        int code = 0;
        PCRE2_SIZE offset = 0;
        pcre2_code_16* re = pcre2_compile_16(reinterpret_cast<PCRE2_SPTR16>(pattern.data()), pattern.size(),
                                             PCRE2_UCP | PCRE2_MULTILINE | PCRE2_DOTALL, &code, &offset, context);
        if (!re) {
            PCRE2_UCHAR16 message[256];
            const int n = pcre2_get_error_message_16(code, message, std::size(message));
            const std::u16string text(reinterpret_cast<const char16_t*>(message), n > 0 ? static_cast<std::size_t>(n) : 0);
            errors.push_back({file, line, "regex rejected (" + Utf8(text) + " at " + std::to_string(offset) + "): " +
                                              Utf8(pattern)});
            return false;
        }
        pcre2_jit_compile_16(re, PCRE2_JIT_COMPLETE);
        uint32_t captures = 0;
        pcre2_pattern_info_16(re, PCRE2_INFO_CAPTURECOUNT, &captures);
        fs.pairs_ = std::max(fs.pairs_, captures + 1);
        const uint32_t infos[2][2] = {{PCRE2_INFO_FIRSTCODETYPE, PCRE2_INFO_FIRSTCODEUNIT},
                                      {PCRE2_INFO_LASTCODETYPE, PCRE2_INFO_LASTCODEUNIT}};
        for (int k = 0; k < 2; k++) {
            uint32_t type = 0, unit = 0;
            if (pcre2_pattern_info_16(re, infos[k][0], &type) == 0 && type == 1 &&
                pcre2_pattern_info_16(re, infos[k][1], &unit) == 0 && !Cased(unit)) {
                units[k] = matcher.Add(std::u16string(1, static_cast<char16_t>(unit)));
            }
        }
        regex = static_cast<int32_t>(fs.regexes_.size());
        fs.regexes_.push_back(re);
        return true;
    };

    for (int stage = 0; stage < 2; stage++) {
        Filters::Program& p = stage == 0 ? fs.pre_ : fs.post_;
        std::stable_sort(rows[stage].begin(), rows[stage].end(),
                         [](const ParsedRow& a, const ParsedRow& b) { return a.layer < b.layer; });
        for (ParsedRow& r : rows[stage]) {
            Filters::Rule rule;
            rule.layer = r.layer;
            rule.file = r.file;
            rule.line = r.line - 1;
            rule.regexFlag = r.regex;
            rule.kind = static_cast<Filters::Kind>(r.kind);
            rule.check = r.dest == u"_MATCH_CHECK";
            if (!r.gate.empty()) rule.gate = toggles.Id(r.gate);
            if (!r.sets.empty()) rule.sets = toggles.Id(r.sets);
            rule.on = r.on;
            if (r.regex != 0) {
                if (!compile(r.src, p.matcher, rule.regex, rule.units, r.file, r.line)) continue;
            } else if (r.src.empty()) {
                errors.push_back({r.file, r.line, "a literal row with an empty source"});
                continue;
            } else {
                rule.units[0] = p.matcher.Add(r.src);
            }
            if (r.hasAux && !r.aux.empty()) rule.aux = p.matcher.Add(r.aux);
            rule.src = std::move(r.src);
            rule.dest = std::move(r.dest);
            p.rules.push_back(std::move(rule));
            if (acceptedCounts) ++(*acceptedCounts)[r.source];
        }
        for (std::size_t i = 0; i < p.rules.size(); i++) {
            p.rules[i].blockEnd = i + 1 == p.rules.size() || p.rules[i + 1].kind != Filters::Kind::Guarded;
        }
    }
    std::stable_sort(conditions.begin(), conditions.end(),
                     [](const ParsedCondition& a, const ParsedCondition& b) { return a.layer < b.layer; });
    for (const ParsedCondition& c : conditions) {
        Filters::Program& p = c.stage == Stage::Pre ? fs.pre_ : fs.post_;
        Filters::Condition cond;
        cond.layer = c.layer;
        cond.label = c.label;
        if (compile(c.cond, p.matcher, cond.regex, cond.units, c.file, c.line)) {
            p.conditions.push_back(std::move(cond));
            if (acceptedCounts) ++(*acceptedCounts)[c.source];
        }
    }
    pcre2_compile_context_free_16(context);
    for (const Filters::Rule& r : fs.pre_.rules) {
        AddMarkCharacters(r.src, r.dest, fs.marks_);
        AddWrittenCharacters(r.src, r.dest, fs.written_);
    }
    fs.pre_.matcher.Build();
    fs.post_.matcher.Build();
    fs.defaults_ = toggles.states;
    fs.toggleNames_.resize(toggles.states.size());
    for (const auto& [name, id] : toggles.ids) fs.toggleNames_[static_cast<std::size_t>(id)] = Utf8(name);

    for (const UserRow& r : userRows) {
        const int32_t code = fs.codes_.Intern(r.phrase ? "A9D0" : "I110", r.attr, "", "GN");
        user.Add(r.key, r.target, code, 0, 0, 0);
    }
    fs.user_ = user.Build(fs.codes_);
    for (int32_t c = 0; c < static_cast<int32_t>(fs.codes_.Size()); c++) {
        fs.codes_.SetWithXXX(c);
        fs.codes_.SetAsPlain(c);
    }
    return out;
}

namespace {

// Replaces every src in text, left to right, resuming after each replacement. Returns
// whether the row fires: it does unless the last replacement ends at 0.
bool ReplaceAll(std::u16string& text, std::u16string_view src, std::u16string_view dest) {
    std::size_t offset = 0;
    for (std::size_t pos; (pos = text.find(src, offset)) != std::u16string::npos;) {
        text.replace(pos, src.size(), dest);
        offset = pos + dest.size();
    }
    return offset != 0;
}

}  // namespace

void Filters::Apply(Stage stage, std::string& line, Toggles& toggles, LogSink log) const {
    const Program& p = stage == Stage::Pre ? pre_ : post_;
    if (line.empty() || p.rules.empty()) return;
    std::u16string text = Utf16(line);
    Run(stage, p, text, toggles, log);
    line = Utf8(text);
}

Filters::~Filters() {
    for (pcre2_real_code_16* re : regexes_) pcre2_code_free_16(re);
}

namespace {

int HexDigit(char16_t c) {
    if (c >= u'0' && c <= u'9') return c - u'0';
    if (c >= u'a' && c <= u'f') return c - u'a' + 10;
    if (c >= u'A' && c <= u'F') return c - u'A' + 10;
    return -1;
}

}  // namespace

void AppendReplacement(std::u16string& out, std::u16string_view fmt, std::u16string_view subject,
                       const std::size_t* ovector, uint32_t pairs, std::size_t previous) {
    auto group = [&](std::size_t n) {
        if (n >= pairs || ovector[2 * n] == PCRE2_UNSET) return;
        out.append(subject.substr(ovector[2 * n], ovector[2 * n + 1] - ovector[2 * n]));
    };
    auto digits = [&](std::size_t& i, int base, std::size_t most) {
        std::size_t v = 0, n = 0;
        for (; i < fmt.size() && n < most; i++, n++) {
            const int d = HexDigit(fmt[i]);
            if (d < 0 || d >= base) break;
            v = v * static_cast<std::size_t>(base) + static_cast<std::size_t>(d);
        }
        return n == 0 ? std::string::npos : v;
    };
    for (std::size_t i = 0; i < fmt.size();) {
        const char16_t c = fmt[i++];
        if (c == u'$' && i < fmt.size()) {
            const char16_t k = fmt[i];
            if (k == u'&') group(0), i++;
            else if (k == u'`') out.append(subject.substr(previous, ovector[0] - previous)), i++;
            else if (k == u'\'') out.append(subject.substr(ovector[1])), i++;
            else if (k == u'$') out += u'$', i++;
            else if (k >= u'0' && k <= u'9') group(digits(i, 10, std::string::npos));
            else if (k == u'{') {
                std::size_t j = i + 1;
                const std::size_t n = digits(j, 10, std::string::npos);
                if (n != std::string::npos && j < fmt.size() && fmt[j] == u'}') {
                    group(n);
                    i = j + 1;
                } else {
                    out += c;
                }
            } else {
                out += c;
            }
        } else if (c == u'\\' && i < fmt.size()) {
            const char16_t k = fmt[i++];
            switch (k) {
            case u'a': out += u'\a'; break;
            case u'e': out += char16_t{27}; break;
            case u'f': out += u'\f'; break;
            case u'n': out += u'\n'; break;
            case u'r': out += u'\r'; break;
            case u't': out += u'\t'; break;
            case u'v': out += u'\v'; break;
            case u'l': case u'u': case u'L': case u'U': case u'E': break;
            case u'c':
                if (i < fmt.size()) out += static_cast<char16_t>(fmt[i++] % 32);
                else out += k;
                break;
            case u'x': {
                std::size_t v = std::string::npos;
                if (i < fmt.size() && fmt[i] == u'{') {
                    std::size_t j = i + 1;
                    v = digits(j, 16, 6);
                    if (v != std::string::npos && j < fmt.size() && fmt[j] == u'}') i = j + 1;
                    else v = std::string::npos;
                } else {
                    v = digits(i, 16, 2);
                }
                if (v == std::string::npos) {
                    out += k;
                } else if (v < 0x10000) {
                    out += static_cast<char16_t>(v);
                } else {
                    std::string utf8;
                    text::AppendRune(utf8, v <= 0x10FFFF ? static_cast<char32_t>(v) : text::kReplacement);
                    out += Utf16(utf8);
                }
                break;
            }
            default:
                if (k >= u'1' && k <= u'9') {
                    group(static_cast<std::size_t>(k - u'0'));
                } else if (k == u'0') {
                    std::size_t j = i - 1;
                    const std::size_t v = digits(j, 8, 4);
                    i = j;
                    out += static_cast<char16_t>(v);
                } else {
                    out += k;
                }
            }
        } else {
            out += c;
        }
    }
}

namespace {

// Replaces every match of re in text with dest; after an empty match the next match may
// start there only if it is not empty. Returns whether the text changed; a match error,
// such as a limit reached, leaves the text as it was.
bool ReplaceRegex(const pcre2_code_16* re, std::u16string& text, std::u16string_view dest, pcre2_match_data_16* md) {
    const auto* subject = reinterpret_cast<PCRE2_SPTR16>(text.data());
    const PCRE2_SIZE* ov = pcre2_get_ovector_pointer_16(md);
    const uint32_t pairs = pcre2_get_ovector_count_16(md);
    std::u16string out;
    std::size_t copied = 0, previous = 0, start = 0;
    uint32_t options = 0;
    bool matched = false;
    while (start <= text.size()) {
        const int rc = pcre2_match_16(re, subject, text.size(), start, options, md, nullptr);
        if (rc == PCRE2_ERROR_NOMATCH && options != 0) {
            options = 0;
            start++;
            continue;
        }
        if (rc == PCRE2_ERROR_NOMATCH) break;
        if (rc < 0) return false;
        matched = true;
        out.append(text, copied, ov[0] - copied);
        AppendReplacement(out, dest, text, ov, std::min<uint32_t>(pairs, static_cast<uint32_t>(rc)), previous);
        copied = previous = ov[1];
        start = ov[1];
        options = ov[0] == ov[1] ? PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED : 0;
    }
    if (!matched) return false;
    out.append(text, copied);
    if (out == text) return false;
    text = std::move(out);
    return true;
}

}  // namespace

void Filters::Run(Stage stage, const Program& p, std::u16string& text, Toggles& toggles, LogSink log) const {
    std::vector<uint32_t> seen(p.matcher.Size());
    uint32_t stamp = 0;
    bool dirty = true;
    auto has = [&](int32_t pattern) {
        if (pattern < 0) return true;
        if (dirty) {
            p.matcher.Scan(text, seen, ++stamp);
            dirty = false;
        }
        return seen[static_cast<std::size_t>(pattern)] == stamp;
    };
    std::unique_ptr<pcre2_match_data_16, void (*)(pcre2_match_data_16*)> md(
        pcre2_match_data_create_16(pairs_, nullptr), pcre2_match_data_free_16);
    // A match error, such as a limit reached, counts as no match.
    auto units = [&](const int32_t (&u)[2]) { return has(u[0]) && has(u[1]); };
    auto search = [&](int32_t regex, const int32_t (&u)[2]) {
        return units(u) && pcre2_match_16(regexes_[static_cast<std::size_t>(regex)],
                                          reinterpret_cast<PCRE2_SPTR16>(text.data()), text.size(), 0, 0, md.get(),
                                          nullptr) >= 0;
    };

    int layer = -1;
    bool skipLayer = false, guard = false, loggedSkip = false;
    std::string skipped;
    for (const Rule& r : p.rules) {
        if (r.layer != layer) {
            layer = r.layer;
            skipLayer = false;
            const auto first = std::lower_bound(p.conditions.begin(), p.conditions.end(), layer,
                                                [](const Condition& c, int l) { return c.layer < l; });
            for (auto c = first; c != p.conditions.end() && c->layer == layer && !skipLayer; ++c) {
                skipLayer = !search(c->regex, c->units);
                if (skipLayer && log.write) {
                    if (loggedSkip) skipped += ", ";
                    skipped += c->label;
                    loggedSkip = true;
                }
            }
        }
        if (skipLayer) continue;
        if (r.gate >= 0 && !toggles[static_cast<std::size_t>(r.gate)]) continue;
        if (r.kind == Kind::Guarded) {
            if (!guard) continue;
            if (r.blockEnd) guard = false;
        }
        bool fired = false;
        if (r.regex < 0) {
            // Guarded and check rows ignore _AUX.
            const bool aux = r.kind == Kind::Guarded || r.check || has(r.aux);
            if (r.check) {
                fired = has(r.units[0]);
            } else if (aux && has(r.units[0])) {
                fired = ReplaceAll(text, r.src, r.dest);
                dirty = true;
            }
        } else if (has(r.aux)) {
            if (r.check) {
                fired = search(r.regex, r.units);
            } else if (units(r.units)) {
                fired = ReplaceRegex(regexes_[static_cast<std::size_t>(r.regex)], text, r.dest, md.get());
                dirty = dirty || fired;
            }
        }
        if (!fired) continue;
        if (log.write) {
            const std::string message = std::string(stage == Stage::Pre ? "PreFilter" : "PostFIlter") + " : [" +
                                        r.file + ":" + std::to_string(r.line) + "] | " + Utf8(r.src) + " | " +
                                        Utf8(r.dest) + " | " + std::to_string(r.layer) + " | " +
                                        std::to_string(r.regexFlag) + "\n";
            log.write(log.context, message);
        }
        if (r.sets >= 0) {
            toggles[static_cast<std::size_t>(r.sets)] = r.on;
            if (log.write) {
                const std::string message = "토글 변수 " + toggleNames_[static_cast<std::size_t>(r.sets)] +
                                            "의 상태가 " + (r.on ? "ON으로 변경.\n" : "OFF로 변경.\n");
                log.write(log.context, message);
            }
        }
        if (r.kind == Kind::Guard) guard = true;
    }
    if (loggedSkip) {
        const std::string message = std::string(stage == Stage::Pre ? "PreSkipLayer" : "PostSkipLayer") + " : " +
                                    skipped + "\n";
        log.write(log.context, message);
    }
}

namespace {

// The character r with the full-width forms of ASCII folded (１ 1, Ａ A), as a pre-filter
// that rewrites them leaves the same character in its place.
char32_t Fold(char32_t r) { return r >= U'！' && r <= U'～' ? r - 0xFEE0 : r; }

// A digit or a Latin letter, also a full-width one.
bool DigitOrLatin(char32_t r) {
    const char32_t a = Fold(r);
    return (a >= U'0' && a <= U'9') || (a >= U'A' && a <= U'Z') || (a >= U'a' && a <= U'z');
}

// The first character of a word in kanji, katakana or Latin letters.
bool WordStart(char32_t r) {
    const char32_t a = Fold(r);
    return text::IsKanji(r) || (r >= U'ァ' && r <= U'ヺ') || r == U'ー' || (a >= U'A' && a <= U'Z') ||
           (a >= U'a' && a <= U'z');
}

struct Runes {
    std::u32string cps;
    std::vector<std::size_t> starts;  // the byte offset of each character, then the size
};

Runes Decode(std::string_view s) {
    Runes out;
    for (std::size_t k = 0; k < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(k));
        out.cps.push_back(r.cp);
        out.starts.push_back(k);
        k += r.size;
    }
    out.starts.push_back(s.size());
    return out;
}

}  // namespace

void KeepSpacesAfterNumbers(std::string_view source, std::string& filtered) {
    if (filtered == source) return;
    const Runes a = Decode(source);
    const std::size_t n = a.cps.size();
    struct Gap {
        std::size_t left, right;  // the characters around the spaces, in source
    };
    std::vector<Gap> gaps;
    for (std::size_t k = 0; k < n; k++) {
        if (!DigitOrLatin(a.cps[k])) continue;
        std::size_t j = k + 1;
        while (j < n && (a.cps[j] == U' ' || a.cps[j] == U'　')) j++;
        if (j > k + 1 && j < n && WordStart(a.cps[j])) gaps.push_back({k, j});
    }
    if (gaps.empty()) return;
    const Runes b = Decode(filtered);
    const std::size_t m = b.cps.size();
    auto same = [&](std::size_t i, std::size_t j) { return Fold(a.cps[i]) == Fold(b.cps[j]); };
    // The common start and end align as they are; the parts between them on their longest
    // common subsequence. Parts of more than kMaxCells pairs stay unaligned, and the spaces
    // in them stay as the filter left them.
    constexpr std::size_t kMaxCells = std::size_t{1} << 22;
    std::size_t head = 0;
    while (head < n && head < m && same(head, head)) head++;
    std::size_t tail = 0;
    while (tail < n - head && tail < m - head && same(n - 1 - tail, m - 1 - tail)) tail++;
    std::vector<std::size_t> to(n, SIZE_MAX);  // the character of b each character of a is kept as
    for (std::size_t i = 0; i < head; i++) to[i] = i;
    for (std::size_t i = n - tail; i < n; i++) to[i] = i - n + m;
    const std::size_t an = n - head - tail, bm = m - head - tail;
    if (an > 0 && bm > 0 && an <= kMaxCells / bm) {
        // lcs[i * (bm + 1) + j]: the longest common subsequence of the parts from i and j.
        std::vector<uint16_t> lcs((an + 1) * (bm + 1), 0);
        for (std::size_t i = an; i-- > 0;) {
            for (std::size_t j = bm; j-- > 0;) {
                lcs[i * (bm + 1) + j] = same(head + i, head + j)
                                            ? static_cast<uint16_t>(lcs[(i + 1) * (bm + 1) + j + 1] + 1)
                                            : std::max(lcs[(i + 1) * (bm + 1) + j], lcs[i * (bm + 1) + j + 1]);
            }
        }
        for (std::size_t i = 0, j = 0; i < an && j < bm;) {
            if (same(head + i, head + j)) {
                to[head + i++] = head + j++;
            } else if (lcs[(i + 1) * (bm + 1) + j] >= lcs[i * (bm + 1) + j + 1]) {
                i++;
            } else {
                j++;
            }
        }
    }
    for (auto g = gaps.rbegin(); g != gaps.rend(); ++g) {
        const std::size_t l = to[g->left], r = to[g->right];
        if (l == SIZE_MAX || r != l + 1) continue;
        filtered.insert(b.starts[r], source.substr(a.starts[g->left + 1], a.starts[g->right] - a.starts[g->left + 1]));
    }
}

namespace {

std::u32string Runes32(std::u16string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size(); i++) {
        char32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        }
        out.push_back(c);
    }
    return out;
}

bool MarkCharacter(char32_t c) {
    const bool kana = (c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) || (c >= 0xFF66 && c <= 0xFF9F);
    const bool hangul = (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F);
    return c >= 0x80 && !kana && !hangul && !text::IsKanji(c) && !text::IsDigit(c) && !text::IsPunct(c) &&
           !text::IsSymbol(c) && !text::IsSpace(c);
}

// Whether c is a kana letter: hiragana and katakana with their small and half-width forms, not the
// middle dot ・ or the length mark ー.
bool KanaLetter(char32_t c) {
    return (c >= 0x3041 && c <= 0x309F) || (c >= 0x30A1 && c <= 0x30FA) || (c >= 0x30FD && c <= 0x30FF) ||
           (c >= 0x31F0 && c <= 0x31FF) || (c >= 0xFF66 && c <= 0xFF9D);
}

// Whether c is a character a pre-filter row writes into the text for its own use: not kana (・ and
// ー included), kanji, Hangul, ASCII, a digit, punctuation or white space.
bool WrittenCharacter(char32_t c) {
    const bool hangul = (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F);
    return c >= 0x80 && !KanaLetter(c) && c != U'・' && c != U'ー' && !hangul && !text::IsKanji(c) &&
           !text::IsDigit(c) && !text::IsPunct(c) && !text::IsSpace(c);
}

}  // namespace

void AddWrittenCharacters(std::u16string_view src, std::u16string_view dest, std::vector<char32_t>& written) {
    const std::u32string from = Runes32(src);
    const std::u32string to = Runes32(dest);
    for (std::size_t i = 0; i + 1 < to.size(); i++) {
        const char32_t c = to[i];
        if (!KanaLetter(to[i + 1]) || !WrittenCharacter(c) || from.find(c) != std::u32string::npos) continue;
        const auto at = std::lower_bound(written.begin(), written.end(), c);
        if (at == written.end() || *at != c) written.insert(at, c);
    }
}

void AddMarkCharacters(std::u16string_view src, std::u16string_view dest, std::vector<char32_t>& marks) {
    const std::u32string from = Runes32(src);
    for (const char32_t c : Runes32(dest)) {
        if (!MarkCharacter(c) || from.find(c) != std::u32string::npos) continue;
        const auto at = std::lower_bound(marks.begin(), marks.end(), c);
        if (at == marks.end() || *at != c) marks.insert(at, c);
    }
}

}  // namespace hdTrans::filter
