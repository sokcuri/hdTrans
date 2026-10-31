// Stub N and A until poc08/analyzer lands: identity normalization, and one token per run of one
// character class, every token a fallback.
#include "ir/stages.h"
#include "ir/utf8.h"

namespace hd08 {

struct AnalyzerData {};

namespace {

enum Cls { kSpace, kKanji, kHira, kKata, kAlnum, kOther };

Cls ClassOf(uint32_t cp) {
    if (cp == ' ' || cp == '\t' || cp == 0x3000) return kSpace;
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || cp == 0x3005 || cp == 0x3006)
        return kKanji;
    if (cp >= 0x3041 && cp <= 0x309F) return kHira;
    if ((cp >= 0x30A1 && cp <= 0x30FA) || cp == 0x30FC || (cp >= 0xFF66 && cp <= 0xFF9F)) return kKata;
    if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= 0xFF10 && cp <= 0xFF19) || (cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A))
        return kAlnum;
    return kOther;
}

Pos PosOf(Cls c, bool digitsOnly) {
    switch (c) {
    case kKanji: case kKata: return Pos::n;
    case kAlnum: return digitsOnly ? Pos::num : Pos::n;
    case kOther: return Pos::sym;
    default: return Pos::unk;
    }
}

}  // namespace

AnalyzerData* LoadAnalyzer(const Config&, std::string&) { return new AnalyzerData; }
void FreeAnalyzer(AnalyzerData* data) { delete data; }

void StageN(const AnalyzerData&, const Config&, Doc&, Line& line) {
    line.norm = line.src;
    line.map.resize(line.src.size() + 1);
    for (uint32_t i = 0; i <= line.src.size(); i++) line.map[i] = i;
    line.edits.clear();
}

void StageA(const AnalyzerData&, const Config&, Doc& doc, Line& line, const std::vector<ForcedSpan>&) {
    line.toks.clear();
    line.alts.clear();
    const std::string_view s = line.norm;
    size_t i = 0;
    bool spaceBefore = false;
    while (i < s.size()) {
        const size_t begin = i;
        const Cls c = ClassOf(NextCodePoint(s, i));
        if (c == kSpace) {
            spaceBefore = true;
            continue;
        }
        bool digits = true;
        if (c != kOther) {
            while (i < s.size()) {
                size_t j = i;
                const uint32_t cp = NextCodePoint(s, j);
                if (ClassOf(cp) != c) break;
                i = j;
            }
        }
        for (size_t k = begin; k < i; k++)
            if (!((s[k] >= '0' && s[k] <= '9') || static_cast<unsigned char>(s[k]) >= 0x80)) digits = false;
        Token t;
        t.src = {line.map[begin], line.map[i]};
        t.surf = t.lemma = doc.I(s.substr(begin, i - begin));
        t.pos = PosOf(c, digits && c == kAlnum);
        t.flags = static_cast<uint16_t>(tokflag::Fallback | tokflag::Unknown | (spaceBefore ? tokflag::SpaceBefore : 0u));
        spaceBefore = false;
        line.toks.push_back(t);
        Flag f;
        f.kind = flag::fallback;
        f.src = t.src;
        line.flags.push_back(f);
    }
}

// input repair (poc08/analyzer, hd8/stage_repair.h): nothing to repair without the analyzer
bool EnableRepair(AnalyzerData*) { return false; }
bool EnableStageRepair(AnalyzerData*) { return false; }
bool RepairEnabled(const AnalyzerData&) { return false; }
bool RepairText(const AnalyzerData&, std::string_view, std::string*, std::string*) { return false; }

}  // namespace hd08
