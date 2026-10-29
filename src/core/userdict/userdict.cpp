#include "core/userdict/userdict.h"

#include <cstdint>
#include <charconv>
#include <set>

#include "core/rules/match.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/userdict/compile.h"

namespace hdTrans::userdict {

namespace internal {

std::u32string Runes(std::string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        out += r.cp;
        i += r.size;
    }
    return out;
}

std::string Utf8(std::u32string_view r) {
    std::string out;
    for (const char32_t c : r) text::AppendRune(out, c);
    return out;
}

std::string_view TrimSpace(std::string_view s) {
    std::size_t begin = 0;
    while (begin < s.size()) {
        const text::Rune r = text::DecodeRune(s.substr(begin));
        if (!text::IsSpace(r.cp)) break;
        begin += r.size;
    }
    std::size_t end = begin;
    for (std::size_t i = begin; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        i += r.size;
        if (!text::IsSpace(r.cp)) end = i;
    }
    return s.substr(begin, end - begin);
}

std::vector<std::string_view> Fields(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = std::string_view::npos;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        if (text::IsSpace(r.cp)) {
            if (start != std::string_view::npos) out.push_back(s.substr(start, i - start));
            start = std::string_view::npos;
        } else if (start == std::string_view::npos) {
            start = i;
        }
        i += r.size;
    }
    if (start != std::string_view::npos) out.push_back(s.substr(start));
    return out;
}

std::vector<std::string_view> Split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    for (;;) {
        const std::size_t i = s.find(sep);
        out.push_back(s.substr(0, i));
        if (i == std::string_view::npos) return out;
        s.remove_prefix(i + 1);
    }
}

std::string Quote(std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "\"";
    for (const char c : s) {
        const auto b = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:
                if (b < 0x20 || b == 0x7F) {
                    out += "\\x";
                    out += kHex[b >> 4];
                    out += kHex[b & 15];
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

namespace {

std::size_t RuneCount(std::string_view s) { return text::RuneCount(s); }

bool IsKanji(char32_t r) { return (r >= 0x4E00 && r <= 0x9FFF) || r == U'々'; }

bool InRunes(std::u32string_view set, char32_t r) { return set.find(r) != std::u32string_view::npos; }

// Splits a verb lemma into its key and the conjugation kind (part code byte 3): the
// key is the lemma without the kind's dictionary-form ending.
std::string VerbKind(std::string_view jp, std::string_view pos, const dict::Dict* base, std::string& key, char& kind) {
    const std::u32string r = Runes(jp);
    const std::size_t n = r.size();
    if (n < 2) return "verb " + Quote(jp) + " has no stem";
    const char32_t prev = r[n - 2], last = r[n - 1];
    const std::string stem = Utf8(std::u32string_view(r).substr(0, n - 1));
    const auto set = [&](std::string k, char c) {
        key = std::move(k);
        kind = c;
        return std::string();
    };
    const auto without2 = [&] { return Utf8(std::u32string_view(r).substr(0, n - 2)); };
    if (jp.ends_with("する") && n > 2) return set(without2(), '9');
    if (jp.ends_with("ずる") && n > 2 && IsKanji(r[n - 3])) return set(without2(), ':');
    // 来ない, 来た, 来て, 来よう conjugate like an ichidan verb on 来; 出来る is できる.
    if (jp == "来る" || jp.ends_with("出来る")) return set(stem, '7');
    if (jp.ends_with("来る") && n > 2) return set(without2(), '=');
    if (jp.ends_with("てくる") || jp.ends_with("でくる")) return set(without2(), ';');
    if (jp.ends_with("行く") || jp.ends_with("逝く") || jp.ends_with("往く") || jp == "いく" || jp.ends_with("ていく") ||
        jp.ends_with("でいく")) {
        return set(stem, '?');
    }
    if (last != U'る') {
        if (pos == "v1") return "v1 verb " + Quote(jp) + " does not end in る";
        static constexpr std::u32string_view kEndings = U"うくぐすつむぶぬ";
        static constexpr std::string_view kKinds = "0123456<";
        const std::size_t i = kEndings.find(last);
        if (i != std::u32string_view::npos) return set(stem, kKinds[i]);
        return "verb " + Quote(jp) + " does not end in a dictionary-form kana";
    }
    if (pos == "v1") return set(stem, '7');
    if (pos == "v5") return set(stem, '8');
    if (InRunes(U"いきぎしじちぢにひびぴみりえけげせぜてでねへべぺめれ", prev)) return set(stem, '7');
    if ((prev >= U'ぁ' && prev <= U'ゖ') || (prev >= U'ァ' && prev <= U'ヺ') || prev == U'ー') return set(stem, '8');
    if (base) {
        std::set<char> kinds;
        for (const dict::Record& rec : base->Records(stem)) {
            const std::string& part = rec.codes->partText;
            if (part.size() >= 4 && part[1] == '4' && (part[3] == '7' || part[3] == '8')) kinds.insert(part[3]);
        }
        if (kinds.size() == 1) return set(stem, *kinds.begin());
    }
    return "る-verb " + Quote(jp) + " is ambiguous: write v1 (ichidan) or v5 (godan)";
}

std::string_view CutSuffix(std::string_view s, std::string_view suffix, bool& ok) {
    ok = s.ends_with(suffix);
    return ok ? s.substr(0, s.size() - suffix.size()) : s;
}

std::uint8_t RegisterOf(std::string_view v) {
    static constexpr std::pair<std::string_view, Register> kRegisters[] = {
        {"plain", Register::Plain},   {"casual", Register::Casual},   {"polite", Register::Polite},
        {"formal", Register::Formal}, {"archaic", Register::Archaic}, {"해라", Register::Plain},
        {"해", Register::Casual},     {"해요", Register::Polite},     {"합쇼", Register::Formal},
        {"하오", Register::Archaic}};
    for (const auto& [name, r] : kRegisters) {
        if (name == v) return static_cast<std::uint8_t>(r);
    }
    return 0;
}

// The integer of a decimal text with an optional sign.
bool Atoi(std::string_view s, int& out) {
    const bool plus = !s.empty() && s[0] == '+';
    if (plus) s.remove_prefix(1);
    if (s.empty() || (plus && s[0] == '-')) return false;
    const char* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(s.data(), end, out);
    return ec == std::errc() && ptr == end;
}

bool IsVerbPos(std::string_view pos) { return pos == "v" || pos == "v1" || pos == "v5"; }

}  // namespace

std::string Compile(std::string_view jp, std::string_view kr, std::string_view pos, std::string_view cls, bool space,
                    const dict::Dict* base, Families& families, std::vector<Rec>& out, std::size_t* ehndAttrPosErrors) {
    std::vector<Rec> recs;
    const auto add = [&recs](std::string key, std::string target, std::string_view part, std::string_view sem = {},
                             std::string_view gram = {}) {
        Rec r;
        r.key = std::move(key);
        r.target = std::move(target);
        r.part = part;
        r.sem = sem;
        r.gram = gram;
        recs.push_back(std::move(r));
    };
    // A negative form such as 出来ない is held as an i-adjective.
    if (IsVerbPos(pos) && jp.ends_with("ない") && RuneCount(jp) > 2) pos = "adj";
    std::string target;
    if (pos == "n") {
        add(std::string(jp), std::string(kr), "N110");
    } else if (pos == "phrase") {
        add(std::string(jp), std::string(kr), "A9D0");
    } else if (pos == "name") {
        add(std::string(jp), std::string(kr), "N130", "QPNVHn", "Wb");
    } else if (pos == "surname") {
        add(std::string(jp), std::string(kr), "N130", "QPNVHn", "Wa");
    } else if (pos == "place") {
        add(std::string(jp), std::string(kr), "N130", "QCTVPl");
    } else if (pos == "intj" || pos == "lead" || pos == "open") {
        add(std::string(jp), std::string(kr), "E000");
    } else if (pos == "hon") {
        add(std::string(jp), std::string(kr), "N110", "", space ? "Ha" : "Tu");
    } else if (IsVerbPos(pos)) {
        std::string key;
        char kind = 0;
        if (std::string err = VerbKind(jp, pos, base, key, kind); !err.empty()) return err;
        if (std::string err = Conjugating(kr, '&', cls, families, target); !err.empty()) return err;
        add(key, target, std::string("M42") + kind);
        bool ok = false;
        std::string_view noun = CutSuffix(kr, "하다", ok);
        if (!ok) noun = CutSuffix(kr, "되다", ok);
        if (kind == '9' && ok && !noun.empty()) add(key, std::string(noun), "N110", "", "Hd");
    } else if (pos == "adj") {
        bool ok = false;
        const std::string_view key = CutSuffix(jp, "い", ok);
        if (!ok || key.empty()) return "adj JP must be a dictionary form ending in い";
        if (std::string err = Conjugating(kr, '$', cls, families, target); !err.empty()) return err;
        add(std::string(key), target, "H7N0");
    } else if (pos == "na") {
        const std::size_t slash = kr.find('/');
        const std::string_view adjective = kr.substr(0, slash);
        if (std::string err = Conjugating(adjective, '%', cls, families, target); !err.empty()) return err;
        add(std::string(jp), target, "G8U0");
        if (slash != std::string_view::npos) add(std::string(jp) + "に", std::string(kr.substr(slash + 1)), "E300");
    } else if (pos == "adv" || pos == "ono") {
        const std::size_t slash = kr.find('/');
        const std::string_view adverb = kr.substr(0, slash);
        std::string verb = std::string(adverb) + " &AV@";
        if (slash != std::string_view::npos) {
            if (std::string err = Conjugating(kr.substr(slash + 1), '&', "", families, verb); !err.empty()) return err;
        }
        // The verb goes first: it fits only where する follows, and the adverb elsewhere.
        add(std::string(jp), verb, "M429");
        add(std::string(jp), std::string(adverb), "E300");
    } else if (pos == "end" || pos == "pat") {
        Rec r;
        if (std::string err = CompileEnding(jp, kr, r); !err.empty()) return err;
        recs.push_back(std::move(r));
    } else {
        if (ehndAttrPosErrors && pos.size() == 3) {
            for (const auto& family : rules::internal::AttributeFamilies()) {
                if (pos == family.name) {
                    ++*ehndAttrPosErrors;
                    break;
                }
            }
        }
        return "unknown POS " + Quote(pos);
    }
    for (Rec& r : recs) {
        r.name = pos == "name" || pos == "surname";
        r.honorific = pos == "hon";
        r.end = pos == "end";
        r.polite = pos == "end" && PoliteJP(jp);
        out.push_back(std::move(r));
    }
    return {};
}

}  // namespace internal

namespace {

using internal::Quote;

// The entries of one line, none for a blank or comment line. Returns an error
// message, empty on success.
std::string ParseLine(std::string_view line, Context& ctx, std::vector<dict::UserEntry>& out,
                      std::vector<SourceEntry>* sourceEntries, std::size_t& ehndAttrPosErrors, std::size_t* acceptedCount) {
    if (const std::string_view t = internal::TrimSpace(line); t.empty() || t.starts_with("//") || t.starts_with("#")) {
        return {};
    }
    std::vector<std::string_view> fields = internal::Split(line, '\t');
    for (std::size_t i = 0; i < fields.size(); i++) {
        if (fields[i].starts_with("//")) {
            fields.resize(i);
            break;
        }
    }
    if (fields.size() > 4) return std::to_string(fields.size()) + " columns, at most 4";
    fields.resize(4);
    const std::string_view jp = internal::TrimSpace(fields[0]), kr = internal::TrimSpace(fields[1]);
    std::string_view pos = internal::TrimSpace(fields[2]), cls;
    if (const std::size_t colon = pos.find(':'); colon != std::string_view::npos) {
        cls = pos.substr(colon + 1);
        pos = pos.substr(0, colon);
    }
    if (pos.empty()) pos = "n";
    if (jp.empty()) return "empty JP";
    if (kr.empty() && pos != "hon") return "empty KR";
    if (!cls.empty() && !internal::IsVerbPos(pos) && pos != "adj" && pos != "na") {
        return "POS " + std::string(pos) + " takes no conjugation class";
    }

    int prio = 0;
    char finalClass = 0;
    bool hasAttr = false, hasGattr = false;
    std::string_view attr, gattr, negative;
    bool force = false, hidden = false, space = false, stative = false, utteranceEnd = false;
    std::uint8_t registerLevel = 0;
    for (const std::string_view opt : internal::Fields(fields[3])) {
        const std::size_t eq = opt.find('=');
        const bool hasValue = eq != std::string_view::npos;
        const std::string_view key = opt.substr(0, eq), value = hasValue ? opt.substr(eq + 1) : std::string_view();
        if (key == "prio" && hasValue) {
            if (!internal::Atoi(value, prio)) return "option " + Quote(opt) + ": " + Quote(value) + " is not an integer";
        } else if (key == "final" && (value == "v" || value == "c" || value == "l")) {
            finalClass = value[0];
        } else if (key == "attr" && hasValue) {
            hasAttr = true;
            attr = value;
        } else if (key == "gattr" && hasValue) {
            hasGattr = true;
            gattr = value;
        } else if (opt == "force") {
            force = true;
        } else if (opt == "hidden") {
            hidden = true;
        } else if ((opt == "space" || opt == "attach") && pos == "hon") {
            space = opt == "space";
        } else if (opt == "stative" && pos == "end") {
            stative = true;
        } else if ((opt == "exclaim" || opt == "predicate" || opt == "cry" || opt == "haeyo") && pos == "intj") {
            // Read by the sound stage (an exclamation is no sound before a question mark, a Korean in
            // 해요 speech is no half of copies said apart).
        } else if (opt == "final" && pos == "intj") {
            utteranceEnd = true;
        } else if ((key == "polite" || key == "alone") && hasValue && pos == "intj") {
            // Read by the sound stage (the last word of the Korean in polite speech, the Korean of a
            // cry said once).
        } else if (key == "register" && pos == "end" && internal::RegisterOf(value) != 0) {
            registerLevel = internal::RegisterOf(value);
        } else if (key == "neg" && hasValue && (internal::IsVerbPos(pos) || pos == "adj")) {
            negative = value;
        } else {
            return "option " + Quote(opt) + ": unknown option " + Quote(opt);
        }
    }

    std::vector<internal::Rec> recs;
    if (std::string err = internal::Compile(jp, kr, pos, cls, space, ctx.base, *ctx.families, recs, &ehndAttrPosErrors);
        !err.empty()) {
        return err;
    }
    std::string negativeTarget;
    if (!negative.empty()) {
        const char marker = pos == "adj" ? '$' : '&';
        if (std::string err = Conjugating(negative, marker, "", *ctx.families, negativeTarget); !err.empty()) {
            return "neg: " + err;
        }
    }
    for (std::size_t i = 0; i < recs.size(); i++) {
        internal::Rec& r = recs[i];
        dict::UserEntry e;
        e.key = std::move(r.key);
        e.target = std::move(r.target);
        e.codes = ctx.codes->At(ctx.codes->Intern(r.part, hasAttr ? attr : std::string_view(r.sem),
                                                  hasGattr ? gattr : std::string_view(r.gram), "GN"));
        e.prio = prio;
        e.name = r.name;
        e.force = force;
        e.hidden = hidden;
        e.finalClass = finalClass;
        e.honorific = r.honorific;
        e.end = r.end;
        e.registerLevel = registerLevel;
        e.polite = r.polite;
        e.euImperative = r.euImperative;
        e.stative = stative;
        e.utteranceEnd = utteranceEnd;
        if (i == 0) e.negative = negativeTarget;
        out.push_back(std::move(e));
    }
    if (sourceEntries) {
        sourceEntries->push_back({std::string(jp), std::string(kr), std::string(internal::TrimSpace(fields[2])),
                                  std::string(internal::TrimSpace(fields[3]))});
    }
    if (acceptedCount) ++*acceptedCount;
    return {};
}

}  // namespace

Compiled Parse(std::string_view text, int file, Context& ctx, std::vector<SourceEntry>* sourceEntries,
               std::size_t* acceptedCount) {
    if (acceptedCount) *acceptedCount = 0;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    Compiled out;
    std::size_t ehndAttrPosErrors = 0;
    int n = 0;
    for (std::string_view line : internal::Split(text, '\n')) {
        n++;
        while (line.ends_with('\r')) line.remove_suffix(1);
        const std::size_t start = out.entries.size();
        if (std::string err = ParseLine(line, ctx, out.entries, sourceEntries, ehndAttrPosErrors, acceptedCount);
            !err.empty()) {
            out.entries.resize(start);
            out.errors.push_back({n, std::move(err)});
            continue;
        }
        for (std::size_t i = start; i < out.entries.size(); i++) {
            out.entries[i].file = file;
            out.entries[i].line = n;
            out.entries[i].order = static_cast<int>(i - start);
        }
    }
    if (ehndAttrPosErrors > out.errors.size() / 2) {
        out.errors.front().message += " (Ehnd 형식 사전으로 보입니다: ehnd 폴더에 두세요)";
    }
    return out;
}

std::vector<dict::UserEntry> KanaSpellings(std::string_view text, const dict::Dict& base, dict::CodeTable& codes) {
    std::vector<dict::UserEntry> out;
    int n = 0;
    for (std::string_view line : internal::Split(text, '\n')) {
        n++;
        while (line.ends_with('\r')) line.remove_suffix(1);
        const std::vector<std::string_view> f = internal::Split(line, '\t');
        if (f.size() < 2 || f[0].empty() || f[0].starts_with("//")) continue;
        const std::string_view cls = f.size() > 2 ? f[2] : std::string_view();
        bool seen[256] = {};
        for (const dict::Record& r : base.Records(f[1])) {
            const std::string& part = r.codes->partText;
            if (part.size() < 2) continue;
            const char d = part[1];
            if (seen[static_cast<unsigned char>(d)]) continue;
            if ((!cls.empty() && cls != std::string_view(&d, 1)) || (cls.empty() && (d == '4' || d == '7'))) continue;
            seen[static_cast<unsigned char>(d)] = true;
            dict::UserEntry e;
            e.key = f[0];
            e.target = r.target;
            e.codes = codes.At(codes.Intern(part, r.codes->sem.Text(), r.codes->gram.Text(), "GN"));
            e.file = -2;
            e.line = n;
            e.kana = true;
            out.push_back(std::move(e));
        }
    }
    return out;
}

}  // namespace hdTrans::userdict
