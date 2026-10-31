#include "core/register/conj.h"

#include "core/register/generate.h"
#include "core/register/hangul.h"

namespace hdTrans::reg {

using namespace hangul;

namespace {

bool IsHarmonic(char32_t c) { return IsSyllable(c) && Initial(c) == kInitIeung && Medial(c) == EO; }

struct EndingFx {
    bool dropEu = false, keepEu = false, absorb = false, noContract = false, contractMust = false;
    bool reu = false, dropVowel = false;
    int harmony = 0;        // 0 by stem, 1 bright, 2 dark
    char32_t vowelSyl = 0;  // 여 or 러
};

std::string CtxOf(const std::u32string& form) {
    if (form.empty()) return "";
    const char32_t c = form[0];
    if (IsHarmonic(c)) return "A";
    if (IsConsonantJamo(c)) return ToU8(std::u32string(1, c));
    if (c == kO) return ToU8(std::u32string(1, kO));
    if (IsSyllable(c)) return ToU8(std::u32string(1, JamoOfInitial(Initial(c))));
    return "";
}

void ApplyRows(const Tables& t, const Stem& s, const std::string& ctx, std::u32string& text, EndingFx& fx) {
    for (const auto& row : t.irregular) {
        if (row.cls != s.cls || row.before != ctx) continue;
        for (const auto& a : row.stem) {
            if (text.empty() || !IsSyllable(text.back())) break;
            char32_t& last = text.back();
            if (a.starts_with("final=")) {
                const std::string v = a.substr(6);
                last = WithFinal(last, v == "0" ? 0 : FinalOfJamo(ToU32(v)[0]));
            } else if (a.starts_with("append=")) {
                text += ToU32(a.substr(7));
            } else if (a == "vowel=hieuh") {
                const int m = Medial(last);
                int nm = m;
                if (s.ae) nm = AE;
                else if (m == A) nm = AE;
                else if (m == YA) nm = YAE;
                else if (m == EO) nm = E;
                else if (m == YEO) nm = YE;
                last = WithMedial(last, nm);
            } else if (a.starts_with("vowel=")) {
                last = WithMedial(last, MedialOfJamo(ToU32(a.substr(6))[0]));
            } else if (a == "reu") {
                fx.reu = true;
            } else if (a == "drop-vowel") {
                fx.dropVowel = true;
            }
        }
        for (const auto& a : row.ending) {
            if (a == "drop-eu") fx.dropEu = true;
            else if (a == "keep-eu") fx.keepEu = true;
            else if (a == "absorb") fx.absorb = true;
            else if (a == "no-contract") fx.noContract = true;
            else if (a == "contract=must") fx.contractMust = true;
            else if (a == "harmony=dark") fx.harmony = 2;
            else if (a == "harmony=bright") fx.harmony = 1;
            else if (a.starts_with("vowel=")) fx.vowelSyl = ToU32(a.substr(6))[0];
        }
    }
}

bool Bright(int medial) { return medial == A || medial == O || medial == YA || medial == WA; }

}  // namespace

std::u32string Attach(const Tables& t, const Stem& s, std::u32string form) {
    std::u32string text = s.text;
    if (form == U"~" || form.empty()) return text;
    if (text.empty()) return form;
    // A/B alternants: after a consonant / after a vowel or the ㄹ of an ㄹ stem
    const std::size_t slash = form.find(U'/');
    if (slash != std::u32string::npos) {
        const int f = Final(text.back());
        const bool cons = f > 0 && !(f == kFinRieul && s.cls == "L");
        form = cons ? form.substr(0, slash) : form.substr(slash + 1);
    }
    EndingFx fx;
    if (form.starts_with(EuMark())) {
        form = form.substr(3);
        ApplyRows(t, s, "EU", text, fx);
        const bool vowelFinal = IsSyllable(text.back()) && Final(text.back()) == 0;
        bool insert = !vowelFinal && s.cls != "L";
        if (fx.dropEu) insert = false;
        if (fx.keepEu) insert = true;
        if (insert) {
            if (!form.empty() && IsConsonantJamo(form[0])) {
                text.push_back(Join(kInitIeung, EU, FinalOfJamo(form[0])));
                form = form.substr(1);
            } else {
                text.push_back(kEu);
            }
            return text + form;
        }
        fx = EndingFx{};
    }
    const std::string ctx = CtxOf(form);
    if (ctx != "A") {
        ApplyRows(t, s, ctx, text, fx);
        if (!form.empty() && IsConsonantJamo(form[0])) {
            const char32_t j = form[0];
            const int fj = FinalOfJamo(j);
            char32_t& last = text.back();
            const int f = Final(last);
            if (f == 0) {
                last = WithFinal(last, fj);
            } else if (f == kFinRieul && j == U'ㄹ') {
                // 살 + ㄹ → 살
            } else if (f == kFinRieul && j == U'ㅁ') {
                last = WithFinal(last, kFinRieulMieum);
            } else {
                text.push_back(Join(kInitIeung, EU, fj));
            }
            form = form.substr(1);
        }
        return text + form;
    }
    // harmonic ending: 아/어 (with the final of 었), then the rest
    ApplyRows(t, s, "A", text, fx);
    const char32_t h = form[0];
    const int hFinal = Final(h);
    const std::u32string rest = form.substr(1);
    int vowel = EO;
    if (fx.vowelSyl == kYeo) {
        vowel = YEO;
    } else if (fx.harmony == 1) {
        vowel = A;
    } else if (fx.harmony == 2 || s.pos == 'P') {
        vowel = EO;
    } else if (fx.dropVowel || fx.reu) {
        vowel = text.size() >= 2 && Bright(Medial(text[text.size() - 2])) ? A : EO;
    } else {
        vowel = Bright(Medial(text.back())) ? A : EO;
    }
    char32_t& last = text.back();
    if (fx.vowelSyl == kReo) {
        text.push_back(Join(kInitRieul, EO, hFinal));
        return text + rest;
    }
    if (fx.absorb) {
        last = WithFinal(last, hFinal);
        return text + rest;
    }
    if (fx.dropVowel) {
        last = Join(Initial(last), vowel, hFinal);
        return text + rest;
    }
    if (fx.reu) {
        if (text.size() >= 2) text[text.size() - 2] = WithFinal(text[text.size() - 2], kFinRieul);
        text.back() = Join(kInitRieul, vowel, hFinal);
        return text + rest;
    }
    if (IsSyllable(last) && !fx.noContract) {
        const char32_t open = Join(Initial(last), Medial(last), 0);
        for (const auto& row : t.contract) {
            // a row naming a syllable with a final (놓) matches it exactly; other rows need an open syllable
            const bool stemOk = row.stemSyllable ? (row.stemSyllable == last || (Final(last) == 0 && open == row.stemSyllable))
                                                 : (Final(last) == 0 && Medial(last) == row.stemVowel);
            if (!stemOk || row.endVowel != vowel) continue;
            if (row.initIeung && Initial(last) != kInitIeung) continue;
            if (row.nextYo && (rest.empty() || rest[0] != kYo)) continue;
            if (row.prefinal && !s.honPrefinal) continue;
            if (row.result < 0) break;
            if (row.mode == "must" || fx.contractMust || row.mode == "default") {
                last = Join(Initial(last), row.result, hFinal);
                return text + rest;
            }
            break;
        }
    }
    text.push_back(Join(kInitIeung, vowel, hFinal));
    return text + rest;
}

std::u32string CopulaAfter(const Tables& t, std::u32string_view host, const std::u32string& form) {
    Stem s;
    s.text = std::u32string(1, kI);
    s.pos = 'C';
    s.cls = "NC";
    std::u32string r = Attach(t, s, form);
    if (EndSound(host) != 'V' || r.size() < 2 || r[0] != kI) return r;
    if (IsHarmonic(r[1])) return std::u32string(1, Join(kInitIeung, YEO, Final(r[1]))) + r.substr(2);  // 였, 여도
    if (r[1] == kE) return std::u32string(1, kYe) + r.substr(2);                                     // 예요
    if (r[1] == kO) return r;                                                                          // 이오
    return r.substr(1);                                                                                // 다, 야, 고, 면
}

Stem StemOf(const Tables& t, const std::string& lemma, const std::string& pos, const std::string& cls) {
    Stem s;
    std::u32string u = ToU32(lemma);
    if (!u.empty() && u.back() == Syl(3, A)) u.pop_back();  // 다
    s.text = u;
    s.pos = pos.empty() ? 'V' : pos[0];
    s.cls = cls.empty() ? "REG" : cls;
    const PredicateInfo info = LookupPredicate(t, lemma);
    s.ae = info.ae;
    s.honPrefinal = info.honSi;
    return s;
}

std::string RealizeParticle(const std::string& form, std::u32string_view prev) {
    const char snd = EndSound(prev);
    const bool cons = snd == 'C' || snd == 'L';
    const std::size_t slash = form.find('/');
    if (slash != std::string::npos) return cons ? form.substr(0, slash) : form.substr(slash + 1);
    const std::string eu = ToU8(EuMark()), im = ToU8(IMark());
    if (form.starts_with(eu)) return (snd == 'C' ? ToU8(std::u32string(1, kEu)) : "") + form.substr(eu.size());
    if (form.starts_with(im)) return (cons ? ToU8(std::u32string(1, kI)) : "") + form.substr(im.size());
    return form;
}

PredicateInfo LookupPredicate(const Tables& t, const std::string& lemmaIn) {
    PredicateInfo info;
    std::string lemma = lemmaIn;
    const std::size_t colon = lemma.find(':');
    if (colon != std::string::npos) {
        info.cls = lemma.substr(colon + 1);
        lemma = lemma.substr(0, colon);
    }
    const auto whole = t.stems.find(lemma);
    if (whole != t.stems.end()) {
        info.pos = whole->second.pos;
        if (info.cls.empty()) info.cls = whole->second.cls;
        info.ae = whole->second.ae;
        info.honSi = whole->second.honSi;
    }
    if (info.pos.empty()) {
        const auto lp = t.lemmaPos.find(lemma);
        if (lp != t.lemmaPos.end()) info.pos = lp->second;
    }
    const std::u32string u = ToU32(lemma);
    if (info.cls.empty()) {
        // the longest suffix listed in stems.tsv (whole syllables), except whole-only rows
        for (std::size_t start = 1; start + 1 < u.size(); ++start) {
            const auto it = t.stems.find(ToU8(u.substr(start)));
            if (it != t.stems.end() && !it->second.whole) {
                info.cls = it->second.cls;
                info.ae = it->second.ae;
                if (info.pos.empty()) info.pos = it->second.pos;  // an unknown compound takes its head's pos
                break;
            }
        }
    }
    if (info.pos.empty()) info.pos = lemma == t.G("lemma.cop") ? "C" : "V";
    if (info.cls.empty() && u.size() >= 2) {
        const char32_t last = u[u.size() - 2];
        for (const auto& r : t.classRules) {
            if (r.pos != "*" && r.pos != info.pos) continue;
            bool ok = false;
            if (r.kind == "lemma") ok = lemma == r.value;
            else if (r.kind == "suffix") ok = lemma.ends_with(r.value);
            else if (r.kind == "final") ok = IsSyllable(last) && Final(last) == (r.value == "0" ? 0 : FinalOfJamo(ToU32(r.value)[0]));
            else if (r.kind == "vowel") ok = IsSyllable(last) && Final(last) == 0 && Medial(last) == MedialOfJamo(ToU32(r.value)[0]);
            else if (r.kind == "syl") ok = ToU8(std::u32string(1, last)) == r.value;
            else if (r.kind == "-") ok = true;
            if (ok) {
                info.cls = r.cls;
                break;
            }
        }
    }
    if (info.cls == "COP" && info.pos != "C") info.pos = "C";
    if (info.cls == "EXIST") info.pos = "X";
    if (lemma == t.G("lemma.negcop")) info.pos = "N";
    // an honorific form taken for a lemma (가시다 = 가다 + -시-): its 시 is the honorific prefinal, so 가시+어요 is
    // 가세요, not 가셔요; lemmas whose 시 belongs to the stem (마시다) are stems.tsv rows
    if (whole == t.stems.end() && u.size() > 2 && u[u.size() - 2] == U'시' && u.back() == U'다') {
        const std::string base = ToU8(u.substr(0, u.size() - 2) + U"다");
        if (t.lemmaPos.contains(base) || t.stems.contains(base) || base == t.G("lemma.cop")) info.honSi = true;
    }
    return info;
}

}  // namespace hdTrans::reg
