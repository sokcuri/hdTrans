#include "core/output/korean.h"

#include <array>
#include <map>
#include <set>
#include <vector>

#include "core/output/markers.h"
#include "core/text/utf8.h"

namespace hdTrans::output {

namespace {

constexpr std::string_view kClasses[] = {"규칙", "하다", "되다", "이다", "ㄹ", "ㅂ", "ㄷ", "ㅅ", "르", "러", "ㅎ", "우", "으"};

// Vowel (V) and final (T) indices of a syllable U+AC00 + (L*21 + V)*28 + T.
constexpr int kVowelA = 0, kVowelAe = 1, kVowelYa = 2, kVowelYae = 3, kVowelEo = 4, kVowelE = 5, kVowelYeo = 6,
              kVowelYe = 7, kVowelO = 8, kVowelWa = 9, kVowelWae = 10, kVowelOe = 11, kVowelU = 13, kVowelWo = 14,
              kVowelEu = 18, kVowelI = 20;
constexpr int kFinalN = 4, kFinalD = 7, kFinalL = 8, kFinalLM = 10, kFinalM = 16, kFinalB = 17, kFinalS = 19,
              kFinalSS = 20, kFinalH = 27;
constexpr int kInitialR = 5, kInitialNone = 11;

using Runes = std::u32string;
using RunesView = std::u32string_view;

struct Jamo {
    int initial = 0;
    int vowel = 0;
    int final = 0;
    bool ok = false;
};

Jamo Decompose(char32_t r) {
    if (r < 0xAC00 || r > 0xD7A3) return {};
    const int s = static_cast<int>(r - 0xAC00);
    return {s / 588, s % 588 / 28, s % 28, true};
}

std::string Str(RunesView rs) {
    std::string out;
    for (const char32_t r : rs) text::AppendRune(out, r);
    return out;
}

std::string Str(char32_t r) {
    std::string out;
    text::AppendRune(out, r);
    return out;
}

Runes ToRunes(std::string_view s) {
    Runes out;
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        out += r.cp;
        i += r.size;
    }
    return out;
}

std::string Compose(int initial, int vowel, int final) {
    return Str(static_cast<char32_t>(0xAC00 + (initial * 21 + vowel) * 28 + final));
}

bool Bright(int vowel) { return vowel == kVowelA || vowel == kVowelYa || vowel == kVowelO; }

bool EndsWith(std::string_view s, std::string_view suffix) { return s.ends_with(suffix); }

// Whether the one-character string c is one of the characters of set.
bool OneOf(std::string_view set, std::string_view c) { return !c.empty() && set.find(c) != std::string_view::npos; }

std::string Quote(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// The accepted past (row 5) and infinitive (row 6) forms of a stem in a class.
struct Forms {
    std::vector<std::string> past;
    std::vector<std::string> inf;
    std::string error;

    void Both(std::string p, std::string i) {
        past.push_back(std::move(p));
        inf.push_back(std::move(i));
    }
    // stem + 았/었 and stem + 아/어.
    void Suffixed(const std::string& stem, bool bright) {
        const int v = bright ? kVowelA : kVowelEo;
        Both(stem + Compose(kInitialNone, v, kFinalSS), stem + Compose(kInitialNone, v, 0));
    }
};

Forms StemForms(RunesView stem, std::string_view cls) {
    Forms f;
    const size_t n = stem.size();
    const std::string pre = Str(stem.substr(0, n - 1));
    const Jamo j = Decompose(stem[n - 1]);
    const int l = j.initial, v = j.vowel, t = j.final;
    const std::string whole = Str(stem);
    const std::string last = Str(stem[n - 1]);
    auto mismatch = [&] {
        f.error = "class " + std::string(cls) + " does not fit " + Quote(whole + "다");
        return f;
    };
    if (cls == "하다") {
        f.Both(pre + "했", pre + "해");
        f.Both(pre + "하였", pre + "하여");
    } else if (cls == "되다") {
        f.Both(pre + "됐", pre + "돼");
        f.Both(pre + "되었", pre + "되어");
    } else if (cls == "규칙" || cls == "ㄹ") {
        if (cls == "ㄹ" && t != kFinalL) return mismatch();
        int contract = 0;
        switch (v) {
            case kVowelO: contract = kVowelWa; break;
            case kVowelU: contract = kVowelWo; break;
            case kVowelI: contract = kVowelYeo; break;
            case kVowelOe: contract = kVowelWae; break;
        }
        if (t != 0) {
            f.Suffixed(whole, Bright(v));
        } else if (v == kVowelA || v == kVowelEo || v == kVowelAe || v == kVowelE || v == kVowelYeo) {
            f.Both(pre + Compose(l, v, kFinalSS), whole);
            if (v != kVowelA && v != kVowelEo) f.Suffixed(whole, false);
        } else if (v == kVowelEu) {
            return StemForms(stem, "으");
        } else if (contract != 0) {
            f.Both(pre + Compose(l, contract, kFinalSS), pre + Compose(l, contract, 0));
            f.Suffixed(whole, Bright(v));
        } else {
            f.Suffixed(whole, Bright(v));
        }
    } else if (cls == "ㅂ") {
        if (t != kFinalB) return mismatch();
        const std::string open = pre + Compose(l, v, 0);
        const int w = last == "돕" || last == "곱" ? kVowelWa : kVowelWo;
        f.Both(open + Compose(kInitialNone, w, kFinalSS), open + Compose(kInitialNone, w, 0));
    } else if (cls == "ㄷ") {
        if (t != kFinalD) return mismatch();
        f.Suffixed(pre + Compose(l, v, kFinalL), Bright(v));
    } else if (cls == "ㅅ") {
        if (t != kFinalS) return mismatch();
        f.Suffixed(pre + Compose(l, v, 0), Bright(v));
    } else if (cls == "르") {
        const Jamo p = Decompose(stem[n >= 2 ? n - 2 : 0]);
        if (last != "르" || n < 2 || !p.ok) return mismatch();
        const int rv = Bright(p.vowel) ? kVowelA : kVowelEo;
        const std::string head = Str(stem.substr(0, n - 2)) + Compose(p.initial, p.vowel, kFinalL);
        f.Both(head + Compose(kInitialR, rv, kFinalSS), head + Compose(kInitialR, rv, 0));
    } else if (cls == "러") {
        if (last != "르") return mismatch();
        f.Both(whole + "렀", whole + "러");
    } else if (cls == "ㅎ") {
        int front = -1;
        switch (v) {
            case kVowelA: front = kVowelAe; break;
            case kVowelYa: front = kVowelYae; break;
            case kVowelEo: front = kVowelAe; break;
            case kVowelYeo: front = kVowelYe; break;
        }
        if (t != kFinalH || front < 0) return mismatch();
        f.Both(pre + Compose(l, front, kFinalSS), pre + Compose(l, front, 0));
    } else if (cls == "우") {
        if (v != kVowelU || t != 0) return mismatch();
        f.Both(pre + Compose(l, kVowelEo, kFinalSS), pre + Compose(l, kVowelEo, 0));
    } else if (cls == "으") {
        if (v != kVowelEu || t != 0) return mismatch();
        int nv = kVowelEo;
        if (n >= 2) {
            if (const Jamo p = Decompose(stem[n - 2]); p.ok && Bright(p.vowel)) nv = kVowelA;
        }
        f.Both(pre + Compose(l, nv, kFinalSS), pre + Compose(l, nv, 0));
    } else {
        std::string known;
        for (const auto c : kClasses) {
            if (!known.empty()) known += ' ';
            known += c;
        }
        f.error = "unknown conjugation class " + Quote(cls) + " (known: " + known + ")";
    }
    return f;
}

// The class of a stem whose line names none: the class its spelling implies.
std::string_view AutoClass(RunesView stem, bool adjective) {
    const std::string s = Str(stem);
    const std::string last = Str(stem.back());
    const Jamo j = Decompose(stem.back());
    auto endsWithAny = [&](std::initializer_list<std::string_view> suffixes) {
        for (const auto x : suffixes) {
            if (EndsWith(s, x)) return true;
        }
        return false;
    };
    if (last == "하") return "하다";
    if (last == "되") return "되다";
    if (last == "이" && adjective && stem.size() > 1) return "이다";
    if (j.final == kFinalL) return "ㄹ";
    if (j.final == kFinalH && last != "좋" && (adjective || OneOf("렇떻얗갛랗맣", last))) return "ㅎ";
    if (j.final == kFinalB && !OneOf("잡입씹접뽑업집꼽좁", last)) return "ㅂ";
    if (j.final == kFinalD && endsWithAny({"듣", "걷", "묻", "싣", "긷", "붇", "컫", "깨닫", "내닫", "치닫"})) return "ㄷ";
    if (j.final == kFinalS && OneOf("짓낫붓잇긋젓잣", last)) return "ㅅ";
    if (last == "르" && stem.size() > 1 && !endsWithAny({"따르", "치르", "들르", "우러르", "다다르"})) return "르";
    if (last == "푸") return "우";
    if (j.vowel == kVowelEu && j.final == 0) return "으";
    return "규칙";
}

// A static family with its rows by form code.
struct Family {
    std::string_view id;
    std::array<std::string_view, 12> rows;

    std::string_view Row(char code) const { return rows[kFormCodes.find(code)]; }
};

// The static families of a marker by their row 1, each list in id order.
using FamilyIndex = std::map<std::string_view, std::vector<Family>, std::less<>>;

FamilyIndex IndexTable(std::span<const internal::MarkerRow> table) {
    FamilyIndex index;
    for (size_t i = 0; i < table.size();) {
        const std::string_view key = table[i].key;
        if (key.size() != 5 || key[3] != '@') {
            i++;
            continue;
        }
        Family f{key.substr(0, 4), {}};
        size_t k = i;
        for (; k < table.size() && std::string_view(table[k].key).substr(0, 4) == f.id; k++) {
            const std::string_view row = table[k].key;
            if (row.size() != 5) continue;
            if (const size_t c = kFormCodes.find(row[4]); c != std::string_view::npos) f.rows[c] = table[k].text;
        }
        i = k;
        if (f.Row('1').empty()) continue;
        index[f.Row('1')].push_back(f);
    }
    return index;
}

const FamilyIndex& Index(char marker) {
    static const std::array<FamilyIndex, 3> indexes = {IndexTable(internal::SourceMarkerTable()),
                                                       IndexTable(internal::DollarMarkerTable()),
                                                       IndexTable(internal::PercentMarkerTable())};
    static const FamilyIndex none;
    switch (marker) {
        case '&': return indexes[0];
        case '$': return indexes[1];
        case '%': return indexes[2];
    }
    return none;
}

int Code(char code) { return static_cast<int>(kFormCodes.find(code)); }

// The rows of a family for the final syllables tail of a stem in a class (standard
// forms): codes '0'..'9', 'A', 'B' for verbs and '1'..'8' for adjectives. past and inf
// are the stem's rows 5 and 6.
FamilyRows Conjugate(RunesView tail, std::string_view cls, std::string_view past, std::string_view inf,
                     bool adjective) {
    const size_t n = tail.size();
    const char32_t last = tail[n - 1];
    const std::string pre = Str(tail.substr(0, n - 1));
    const Jamo j = Decompose(last);
    const int l = j.initial, v = j.vowel, t = j.final;
    const std::string s = Str(last);
    const std::string open = pre + Compose(l, v, 0);
    auto with = [&](int final) { return pre + Compose(l, v, final); };
    FamilyRows rows;
    auto set = [&](char code, std::string value) { rows[Code(code)] = std::move(value); };
    // Vowel stems take ㄴ ㄹ ㅁ ㅂ as finals; consonant stems take 으 forms.
    auto vowelRows = [&](const std::string& base, RunesView b) {
        const Jamo bj = Decompose(b.back());
        const std::string bp = Str(b.substr(0, b.size() - 1));
        auto f = [&](int final) { return bp + Compose(bj.initial, bj.vowel, final); };
        set('2', base);
        set('3', f(kFinalL));
        set('4', f(kFinalN));
        set('7', f(kFinalB));
        set('8', f(kFinalN));
        set('9', f(kFinalB));
        set('A', base);
        set('B', f(kFinalM));
    };
    auto consonantRows = [&](const std::string& base) {
        set('2', base + "으");
        set('3', base + "을");
        set('4', s + "는");
        set('7', s + "습");
        set('8', base + "은");
        set('9', base + "읍");
        set('A', base + "으");
        set('B', base + "음");
    };
    if (cls == "ㄹ") {
        set('2', s);
        set('3', s);
        set('4', with(kFinalN));
        set('7', with(kFinalB));
        set('8', with(kFinalN));
        set('9', with(kFinalB));
        set('A', open);
        set('B', with(kFinalLM));
        set('0', open + "는");
    } else if (cls == "ㅂ") {
        const std::string u = open + "우";
        vowelRows(u, ToRunes(u));
        set('4', s + "는");
        set('7', s + "습");
    } else if (cls == "ㄷ") {
        consonantRows(with(kFinalL));
    } else if (cls == "ㅅ") {
        consonantRows(open);
    } else if (cls == "ㅎ") {
        vowelRows(open, ToRunes(open));
        set('4', s + "는");
        set('7', s + "습");
    } else if (t != 0) {
        consonantRows(s);
    } else {
        vowelRows(Str(tail), tail);
    }
    if (rows[Code('0')].empty()) set('0', Str(tail) + "는");
    set('1', Str(tail));
    set('5', std::string(past));
    set('6', std::string(inf));
    if (!adjective) return rows;
    constexpr std::pair<char, char> kAdjective[] = {{'1', '1'}, {'2', '2'}, {'3', '3'}, {'4', '8'},
                                                    {'5', '5'}, {'6', '6'}, {'7', '7'}, {'8', 'B'}};
    FamilyRows adj;
    for (const auto& [code, from] : kAdjective) adj[Code(code)] = rows[Code(from)];
    return adj;
}

bool ContainsString(const std::vector<std::string>& list, std::string_view s) {
    for (const auto& x : list) {
        if (x == s) return true;
    }
    return false;
}

// Conjugating, with no family made when generated is null.
Conjugated ConjugatingIn(std::string_view lemma, char marker, std::string_view cls, Families* generated) {
    Conjugated out;
    if (!lemma.ends_with("다") || lemma.size() == std::string_view("다").size()) {
        out.error = "KR " + Quote(lemma) + " is not a lemma ending in 다";
        return out;
    }
    const Runes stem = ToRunes(lemma.substr(0, lemma.size() - std::string_view("다").size()));
    const size_t n = stem.size();
    if (!Decompose(stem[n - 1]).ok) {
        out.error = "KR " + Quote(lemma) + " does not end in a Hangul syllable before 다";
        return out;
    }
    const bool explicitClass = !cls.empty();
    if (!explicitClass) cls = AutoClass(stem, marker != '&');
    const std::string last = Str(stem[n - 1]);
    if (cls == "하다" || cls == "되다") {
        if (last != cls.substr(0, cls.size() - std::string_view("다").size())) {
            out.error = "class " + std::string(cls) + " does not fit " + Quote(lemma);
            return out;
        }
        std::string_view id;
        switch (marker) {
            case '&': id = cls == "되다" ? "&C6@" : "&AV@"; break;
            case '$': id = cls == "되다" ? "$AE@" : "$AB@"; break;
            case '%': id = cls == "되다" ? "%D5@" : "%A1@"; break;
        }
        if (marker == '%' && cls == "하다" && n >= 2 && OneOf("아어워", Str(stem[n - 2]))) id = "%D1@";
        out.target = Str(RunesView(stem).substr(0, n - 1)) + std::string(id);
        return out;
    }
    if (cls == "이다") {
        if (last != "이" || n < 2) {
            out.error = "class 이다 does not fit " + Quote(lemma);
            return out;
        }
        std::string_view id;
        switch (marker) {
            case '&': id = "&Ac@"; break;
            case '$': id = "$AH@"; break;
            case '%': id = "%D4@"; break;
        }
        out.target = Str(RunesView(stem).substr(0, n - 1)) + std::string(id);
        return out;
    }
    Forms forms = StemForms(stem, cls);
    if (!forms.error.empty()) {
        out.error = std::move(forms.error);
        return out;
    }
    const FamilyIndex& index = Index(marker);
    for (size_t k = std::min<size_t>(3, n); k >= 1; k--) {
        const std::string pre = Str(RunesView(stem).substr(0, n - k));
        const auto it = index.find(Str(RunesView(stem).substr(n - k)));
        if (it == index.end()) continue;
        const std::vector<Family>& fams = it->second;
        const Family* fit = nullptr;
        size_t fits = 0;
        std::set<std::string> rows;
        for (const Family& f : fams) {
            // Some '$' families conjugate like verbs (attributive 는, as in 안되다); an
            // adjective lemma takes them only after 있 and 없.
            if (marker == '$' && f.Row('4').ends_with("는") && !f.Row('1').ends_with("있") &&
                !f.Row('1').ends_with("없")) {
                continue;
            }
            rows.insert(std::string(f.Row('5')) + "/" + std::string(f.Row('6')));
            if (ContainsString(forms.past, pre + std::string(f.Row('5'))) &&
                ContainsString(forms.inf, pre + std::string(f.Row('6')))) {
                if (fit == nullptr) fit = &f;
                fits++;
            }
        }
        if (fit == nullptr) continue;
        if (!explicitClass && cls == "규칙" && fits < fams.size() && rows.size() > 1) {
            std::string list;
            for (const Family& f : fams) {
                if (!list.empty()) list += ", ";
                list += std::string(f.id) + " " + std::string(f.Row('5')) + "/" + std::string(f.Row('6'));
            }
            out.error = "KR " + Quote(lemma) + " matches marker families that conjugate differently (" + list +
                        "): name the class";
            return out;
        }
        out.target = pre + std::string(fit->id);
        return out;
    }
    const size_t cover = cls == "르" ? 2 : 1;
    const std::string prefix = Str(RunesView(stem).substr(0, n - cover));
    auto trimPrefix = [&](std::string_view s) { return s.starts_with(prefix) ? s.substr(prefix.size()) : s; };
    const FamilyRows rows = Conjugate(RunesView(stem).substr(n - cover), cls, trimPrefix(forms.past[0]),
                                      trimPrefix(forms.inf[0]), marker != '&');
    if (marker != '&' && marker != '$' && marker != '%') {
        out.error = std::string("unknown marker '") + marker + "'";
        return out;
    }
    if (generated == nullptr) {
        out.error = "no static family conjugates " + Quote(lemma);
        return out;
    }
    const std::string id = generated->Generate(marker, rows);
    if (id.empty()) {
        out.error = std::string("no free ") + marker + " family ID";
        return out;
    }
    out.target = prefix + id;
    return out;
}

}  // namespace

std::span<const std::string_view> Classes() { return kClasses; }

Conjugated Conjugating(std::string_view lemma, char marker, std::string_view cls, Families& generated) {
    return ConjugatingIn(lemma, marker, cls, &generated);
}

Conjugated StaticConjugating(std::string_view lemma, char marker, std::string_view cls) {
    return ConjugatingIn(lemma, marker, cls, nullptr);
}

}  // namespace hdTrans::output
