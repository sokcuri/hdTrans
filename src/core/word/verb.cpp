// Verb conjugation: the branch of a verb record and the acceptance of what follows it.
#include <cstdint>
#include <algorithm>

#include "core/dict/classes.h"
#include "core/output/markers.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/word/conjugation.h"
#include "core/word/strings.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

constexpr int kKindDirect = 14;  // pattern kind matched without an ending

int Byte(char c) { return static_cast<unsigned char>(c); }

bool Starts(std::string_view s, std::string_view p) { return s.starts_with(p); }

// たく of the desire after a stem (見たくない), not the た of the past before くせに or くらい
// (見たくせに, 見たくらい).
bool Taku(std::string_view s) { return s.starts_with(kJaTaku) && !s.starts_with(kJaTakuseni) && !s.starts_with(kJaTakurai); }

// Whether a form of たい starts s (たい, たかった, たく, たければ, たき).
bool Desire(std::string_view s) {
    return Starts(s, kJaTai) || Starts(s, kJaTakatta) || Taku(s) || Starts(s, kJaTakereba) || Starts(s, kJaTaki);
}

// Whether も at the start of s is followed by a form of する that ends there (立ち上がりもせず,
// 笑いもしない, 読みもした; not しても, していない): the continuative before it is the verb,
// emphasised, not a noun.
bool SuruAfterMo(std::string_view s) {
    if (!s.starts_with("も")) return false;
    const std::string_view r = s.substr(std::string_view("も").size());
    if (r.starts_with("して") || r.starts_with("しち")) return false;
    return r.starts_with("し") || r.starts_with("せ") || r.starts_with("す");
}

// Whether a godan continuative (pattern kind not ichidan, する, ずる or くる) is the noun of
// manner before s: the よう of 疑いようもない, やりようがない, 品のないなさりよう！ (verb.way-of-doing).
// An ichidan stem keeps its volitional よう.
bool WayOfDoing(int patternKind, std::string_view s, std::string_view next) {
    if (patternKind == 7 || patternKind == 9 || patternKind == 10 || patternKind == 11 || patternKind == 13) return false;
    for (const std::string_view m : dict::ClassMembers("verb.way-of-doing")) {
        if (s.starts_with(m)) return true;
        // the mark or particle after the bare form starts the next token
        if (!s.empty() && m.size() > s.size() && m.starts_with(s) && next.starts_with(m.substr(s.size()))) return true;
    }
    return false;
}

// Whether s is a single hiragana (the stem で of 出る, which でもする is not).
bool OneKana(std::string_view s) {
    const text::Rune r = text::DecodeRune(s);
    return r.size == s.size() && r.cp >= U'ぁ' && r.cp <= U'ゖ';
}

}  // namespace

bool ProgressiveEndings(Group& g, Detail& d, std::string_view head) {
    if (head.size() < 4) return false;
    const std::string_view prefix = head.substr(0, head.size() - 4), family = head.substr(head.size() - 4);
    bool any = false;
    for (std::size_t i = 0; i < g.entries.size(); i++) {
        if (i < d.enabled.size() && !d.enabled[i]) continue;
        Entry& e = *g.entries[i];
        const std::string_view row = e.dst.empty() ? std::string_view() : output::SourceMarkerRow(std::string(family) + e.dst[0]);
        if (row.empty()) {
            if (i < d.enabled.size()) d.enabled[i] = false;
            continue;
        }
        e.dst = std::string(prefix) + std::string(row) + e.dst.substr(1);
        any = true;
    }
    return any;
}

bool MatchConjugation(Detail& d, std::string_view text, int offset, Record* records, Record*& stop) {
    stop = records;
    if (offset > static_cast<int>(text.size())) return false;
    const std::string_view recordText = records->source;
    const std::string_view prefix = text.substr(0, static_cast<std::size_t>(offset));
    if (recordText.size() + static_cast<std::size_t>(offset) > text.size()) return false;
    const std::string_view tail = From(text, offset + static_cast<long>(recordText.size()));
    if (WidthOf(prefix) > 2 && recordText == kJaOki && tail.starts_with(",") &&
        (prefix.ends_with(kJaTe) || prefix.ends_with(kJaDe))) {
        return false;
    }
    // A stem that one of its records reads as a potential and that is the imperative of a shorter
    // godan record (笑え, 言え) is no ichidan stem before the よ of an imperative: a potential
    // has none.
    bool potential = false, imperative = false;
    const bool imperativeYo = Starts(tail, Conjugation(7, 12)) && !Starts(tail, Conjugation(7, 5));
    for (const Record* r = imperativeYo ? records : nullptr; r != nullptr; r = r->child) {
        if (r->source.size() == recordText.size()) potential = potential || Has(r->attr, "Ke");
        if (r->source.size() < recordText.size() && At(r->partCode, 1) == '4' && recordText.starts_with(r->source)) {
            const int k = Byte(At(r->partCode, 3)) - '0';
            imperative = imperative || (k != 7 && recordText.substr(r->source.size()) == Conjugation(k, 11));
        }
    }
    potential = potential && imperative;
    int previousKind = -1;
    for (Record* record = records; record != nullptr; record = record->child) {
        stop = record;
        if (recordText.size() != record->source.size() || At(record->partCode, 1) != '4') return false;
        d.patternKind = Byte(At(record->partCode, 3)) - '0';
        if (d.patternKind != previousKind && MatchKind(d, tail, recordText, *record, potential)) return true;
        previousKind = d.patternKind;
    }
    stop = nullptr;
    return false;
}

bool MatchKind(Detail& d, std::string_view tail, std::string_view recordText, const Record& record, bool potential) {
    const int kind = d.patternKind;
    if (potential && kind == 7) return false;
    if (kind == kKindDirect) {
        d.patternMatched = 1;
        d.patternLength = static_cast<int>(recordText.size());
        return true;
    }
    for (int branch = 0; branch < kConjugationBranches; branch++) {
        if (branch == 13) continue;
        const std::string_view entry = Conjugation(kind, branch);
        const bool contractedConditional = kind == 9 && branch == 3 && Starts(tail, "すりゃ");
        if ((kind != 7 && entry.empty()) || (!Starts(tail, entry) && !contractedConditional)) continue;
        // A bare ichidan stem is no form the quoting って or と follows (行けって, 言えと are the
        // godan imperatives 行け, 言え, not potential stems); と after it only begins the
        // ておく contraction (食べとく, 食べとけ, 食べといて, 食べとかない, 食べとこう, 食べとった).
        if (kind == 7 && entry.empty() &&
            (Starts(tail, "って") ||
             (Starts(tail, "と") && !Starts(tail, "とく") && !Starts(tail, "とけ") && !Starts(tail, "とい") &&
              !Starts(tail, "とか") && !Starts(tail, "とこ") && !Starts(tail, "とっ") && !Starts(tail, "ときゃ")))) {
            continue;
        }
        if (branch == 4 || branch == 11) {
            const std::size_t n = entry.size();
            const std::string_view rest = tail.substr(n);
            if (branch == 4 && entry == Conjugation(kind, 11) &&
                (Starts(rest, "だの") || Starts(rest, "」と") || Starts(rest, "』と") ||
                 Starts(rest, "」って") || Starts(rest, "』って") || Starts(rest, "」だの") ||
                 Starts(rest, "』だの"))) {
                continue;
            }
            if (Starts(rest, kJaFullComma) || Starts(rest, kJaComma)) continue;
            // A bare e-row form before a closing bracket, a full-width sentence mark or the
            // quoting って or と is the imperative (「撃て」, 「書け」の一点張り, 言えって言った):
            // a potential stem cannot end there without its る. Neither form takes a copula
            // or に, except the だの, だと (だとか), だって or だなんて that quote an order, and only
            // an order takes の (背負えだとか, 帰れだと, 寝ろの一言). The imperative of いただく is
            // no order (待っていただけだと is いた and だけ).
            if (branch == 4 && (Starts(rest, "」") || Starts(rest, "』") || Starts(rest, "）") || Starts(rest, "。") ||
                                Starts(rest, "…") || Starts(rest, "‥") || Starts(rest, "！") || Starts(rest, "？") ||
                                Starts(rest, "って") || Starts(rest, "と"))) {
                continue;
            }
            const bool quoted = branch == 11 && recordText != "いただ" && recordText != "頂" &&
                                (Starts(rest, "だの") || Starts(rest, "だと") || Starts(rest, "だって") || Starts(rest, "だなんて"));
            if (!entry.empty() && !quoted &&
                (Starts(rest, "だ") || Starts(rest, "で") || Starts(rest, "じゃ") || Starts(rest, "に") ||
                 (branch == 4 && Starts(rest, "の")))) {
                continue;
            }
            if (n < tail.size()) {
                const char next = tail[n];
                if ((branch == 4 && Byte(next) < 0x80 && IsPunct(next)) || (branch == 11 && next == ',')) continue;
            } else if (n == tail.size() && branch == 4) {
                continue;
            }
        }
        d.kind = branch;
        if (d.kind == 4 && Starts(tail, Conjugation(kind, 16))) d.kind = 16;
        if ((kind == 7 || kind == 11 || kind == 13) && d.kind == 1) d.kind = 4;  // られ: the potential before the passive
        if (d.patternMatched == 0) {
            if (kind == 7 && d.kind == 6) {
                if (Desire(tail)) d.kind = 10;
            } else if (kind == 3 && d.kind == 6) {
                if (Starts(tail, kJaShitai) || Starts(tail, kJaShitakatta) ||
                    (Starts(tail, kJaShitaku) && Taku(tail.substr(std::string_view("し").size()))) ||
                    Starts(tail, kJaShitakereba) || Starts(tail, kJaShitaki)) {
                    d.kind = 10;
                }
            } else if (kind == 9 && d.kind == 6) {
                if (Cut(tail, 2, 4) == kJaTai || Cut(tail, 2, 8) == kJaTakatta ||
                    (Cut(tail, 2, 4) == kJaTaku && Taku(tail.substr(text::DecodeRune(tail).size))) ||
                    Cut(tail, 2, 8) == kJaTakereba || Cut(tail, 2, 4) == kJaTaki) {
                    d.kind = 10;
                }
            } else if (kind == 9 && (d.kind == 8 || d.kind == 10)) {
                const auto lengthText = d.tailLengthCheck ? tail : recordText;
                if (WidthOf(lengthText) >= 4 && Cut(tail, 0, 4) == Conjugation(kind, 11)) d.kind = 11;
            }
        }
        if (d.kind == 0) {
            if (Starts(tail, Conjugation(kind, 15))) d.kind = 15;
            else if (Starts(tail, Conjugation(kind, 14))) d.kind = 14;
        }
        // くる and 来る write their stems (こ, き, 来) apart from the forms built on them, so a
        // stem match takes the 来たい family over the past 来た (as for ichidan stems) ...
        if ((kind == 11 || kind == 13) && d.kind == 6 && d.patternMatched == 0) {
            const std::string stem(Conjugation(kind, 10));
            for (const std::string_view t : {kJaTai, kJaTakatta, kJaTaku, kJaTakereba, kJaTaki}) {
                if (Starts(tail, stem + std::string(t)) && (t != kJaTaku || Taku(tail.substr(stem.size())))) {
                    d.kind = 10;
                    break;
                }
            }
        }
        // ... and a stem form gives way to a longer form that begins with it (来い, 来ちゃ).
        if ((kind == 7 || kind == 11 || kind == 13) && (d.kind == 8 || d.kind == 10)) {
            const int old = d.kind;
            std::size_t longest = Conjugation(kind, old).size();
            for (int next = old + 1; next <= 17; next++) {
                if (next == 13) continue;
                const std::string_view e = Conjugation(kind, next);
                if (e.empty() || !Starts(tail, e)) continue;
                if (kind == 7) {  // ichidan: the first later form (its stem forms are empty)
                    d.kind = next;
                    break;
                }
                if (e.size() > longest) {  // くる, 来る: the longest form that begins with the stem
                    d.kind = next;
                    longest = e.size();
                }
            }
            if (old == d.kind && (Starts(tail, kJaKudasaKanji) || Starts(tail, kJaKudasa))) d.kind = 10;
        }
        if (d.kind == 10) {
            if (Starts(tail, Conjugation(kind, 15))) d.kind = 15;
            else if (Starts(tail, Conjugation(kind, 17))) d.kind = 17;
        }
        if (branch != 8) return true;
        if (const std::size_t n = Conjugation(kind, 8).size(); n > 0) {
            const std::string_view rest = tail.substr(n);
            if (Starts(rest, kJaNa) || Starts(rest, kJaN) || Starts(rest, kJaZaru) || Starts(rest, kJaNna) ||
                Starts(rest, kJaNeba) || Starts(rest, kJaNu) ||
                (d.causativePassive && kind != 3 && Starts(rest, kJaSare))) {
                return true;
            }
        } else {
            if (!Starts(tail, kJaComma) && !Starts(tail, ",")) return true;
            if (WidthOf(record.source) != 2) {
                d.kind = 7;
                return true;
            }
        }
    }
    if (kind == 7 && Starts(tail, kJaRe) && (Starts(recordText, kJaMiKanji) || Starts(recordText, kJaTabeKanji))) {
        d.patternKind = 8;
        d.kind = 4;
        return true;
    }
    auto politeRequest = [&]() {
        Window middle;
        if (WidthOf(tail) >= 6) middle = Cut(tail, 2, 4);
        return Starts(tail, kJaI) && (middle == kJaMasu || middle == kJaMase || middle == kJaMashi);
    };
    for (const std::string_view stem : dict::ClassMembers("stem.request")) {
        if (const std::size_t i = record.source.find(stem);
            i != std::string_view::npos && i == record.source.size() - stem.size() && politeRequest()) {
            d.patternKind = 8;
            d.kind = 10;
            return true;
        }
    }
    if (Has(record.attr, "Hr") && politeRequest()) {
        d.patternKind = 8;
        d.kind = 10;
        return true;
    }
    return false;
}

bool Worker::TeAfterWaStem(std::string_view stem, std::string_view after) const {
    if (stem.empty()) return false;
    for (std::string_view t = stem; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (!text::IsKanji(r.cp)) return false;
        t.remove_prefix(r.size);
    }
    if (after == kJaTe && current + 1 < TokenCount()) {
        const std::string_view next = Tok(current + 1).text;
        if (Starts(next, kJaEllipsis) || Starts(next, "‥")) return false;
    }
    if (Starts(after, kJaTe)) {
        // 言いてえ, 会いてぇ: the spoken たい.
        if (Starts(after, "てえ") || Starts(after, "てぇ") || Starts(after, "てー") || Starts(after, "てェ")) return false;
    } else {
        if (!Starts(after, "た")) return false;
        // The たい family, the honorific たまえ (言いたまえ) and the compounds in ため, たて and
        // たっ (買いため, 言いたて, 思いたった) go on from the stem of the わ verb.
        for (const std::string_view rest : {"たい", "たく", "たか", "たけ", "たが", "たさ", "たそ", "たげ", "たま", "ため", "たて", "たっ"}) {
            if (Starts(after, rest)) return false;
        }
    }
    // Only where the stem is also a く verb, whose て and た forms these are (彷徨いて, 拭いた).
    for (const dict::Candidate& c : snapshot.RetrieveRecords(stem, dict::LookupMode::General)) {
        const std::string_view pc = c.codes->partText;
        if (pc.size() != 4 || pc[0] != 'M' || pc[1] != '4') continue;
        const int kind = pc[3] - '0';
        if (kind >= 0 && kind < kConjugationKinds && Conjugation(kind, 6) == "いた") return true;
    }
    return false;
}

bool AnalyseVerbTail(std::string_view key, char kind, std::string_view tail, int& branch, int& length) {
    Detail d;
    d.patternKind = Byte(kind) - '0';
    d.kind = -1;
    d.selected = -1;
    Record record;
    record.source = key;
    const char code[4] = {'M', '4', '2', kind};
    record.partCode = std::string_view(code, 4);
    if (!MatchKind(d, tail, key, record)) return false;
    branch = d.kind;
    length = static_cast<int>(Conjugation(d.patternKind, d.kind).size());
    return true;
}

// Checks what follows a verb's conjugation and looks up the ending (the direct group).
// following is the rest of the candidate list after the verb's group. The conjugation
// pattern is measured in width units (patternWidth) on the text after the stem, which
// need not start with the pattern.
bool Item::AcceptVerbContinuation(Record* following) {
    Detail& base = *detail;
    const bool matched = base.patternMatched != 0;
    std::string surface;
    int sourceLength = group->length;
    if (matched) {
        const std::string_view matchedText = From(text, start);
        surface = Left(text, WidthOf(matchedText.substr(0, std::min<std::size_t>(static_cast<std::size_t>(base.patternLength),
                                                                                  matchedText.size()))))
                      .Str();
        sourceLength = base.patternLength;
    } else {
        surface = group->surface;
    }
    std::string_view tail;
    if (sourceLength + start <= static_cast<int>(text.size())) tail = From(text, sourceLength + start);
    int patternWidth = 0;
    if (!matched) patternWidth = WidthOf(Conjugation(base.patternKind, base.kind));
    // The byte length of the pattern in tail, and the text after it.
    auto pattern = [&]() { return static_cast<int>(UnitOffset(tail, patternWidth).offset); };
    auto after = [&]() { return tail.substr(static_cast<std::size_t>(pattern())); };
    // A bare ichidan stem ends no utterance where its last kana also makes the imperative
    // of a godan verb: before a closing bracket or a full-width sentence mark that is the
    // reading (「立て」 서라, not 세우고). The mark may start the next token.
    if (!matched && base.patternKind == 7 && Conjugation(7, base.kind).empty()) {
        const std::string_view ahead = tail.empty() ? base.nextText : tail;
        if ((Starts(ahead, "」") || Starts(ahead, "』") || Starts(ahead, "。") || Starts(ahead, "！") ||
             Starts(ahead, "？")) &&
            w->GodanImperative(surface)) {
            return false;
        }
    }
    if (!w->MatchDictionary(tail, surface, '4', 'X')) return false;
    // A noun of the dictionary that ends with the よう of manner and starts inside the stem
    // takes the text (ありよう 상태, 救いよう).
    auto NounOfManner = [&](std::string_view stem) {
        const std::string_view way = after();
        const std::size_t first = text::DecodeRune(way).size;
        const std::string_view rest =
            tail.substr(0, static_cast<std::size_t>(pattern()) + first + text::DecodeRune(way.substr(first)).size);
        for (std::size_t i = 0; i < stem.size(); i += text::DecodeRune(stem.substr(i)).size) {
            const std::string query = std::string(stem.substr(i)) + std::string(rest);
            for (const Record* r = w->DoLookup(w->arena.Keep(query), kModeGeneral).records; r != nullptr; r = r->child) {
                if (r->source == query && At(r->partCode, 0) == 'N') return true;
            }
        }
        return false;
    };
    Window two, four, six;
    if (const std::string_view r = after(); WidthOf(r) >= 2) two = Left(r, 2);
    if (const std::string_view r = after(); WidthOf(r) >= 4) four = Left(r, 4);
    if (const std::string_view r = after(); WidthOf(r) >= 6) six = Left(r, 6);

    if (!matched && base.kind == 10 && base.patternKind == 0 && w->TeAfterWaStem(surface, after())) return false;
    // A bare stem in kanji that the dictionary also has as a noun, right after an adjective in
    // its attributive い or な and before a comma, is the noun the adjective describes (いい感じ、
    // 좋은 느낌), not a verb's continuative; a phrase verb stays (長い列ができ、).
    if (!matched && Starts(tail, kJaComma) && start > 0 && text::IsKanji(text::DecodeRune(surface).cp)) {
        const std::string_view before = LastChar(std::string_view(text).substr(0, static_cast<std::size_t>(start)));
        if (before == "い" || before == "な") {
            for (const Record* r = w->DoLookup(w->arena.Keep(surface), kModeGeneral).records; r != nullptr; r = r->child) {
                if (r->source == surface && At(r->partCode, 0) == 'N') return false;
            }
        }
    }
    if (!matched && base.kind == 10 &&
        ((two == kJaGa && !(four == kJaGara) && !(four == kJaGata) && !(four == kJaGachi)) || after().starts_with(".") ||
         (two == kJaNo && !(four == kJaNokataKanji) && !(six == kJaNokata)) || (two == kJaHa && !(four == kJaHaji)) ||
         two == kJaDa || two == kJaTo || two == kJaDe || two == kJaHe || (two == kJaMo && !SuruAfterMo(after())) ||
         (two == kJaYa && !(four == kJaYasu) && !(w->settings.text && four == kJaYaga)) || two == kJaWo ||
         (two == kJaKa && !(four == kJaKane) && !(four == kJaKake)) ||
         (two == kJaYo && !(WayOfDoing(base.patternKind, after(), base.nextText) && !NounOfManner(surface))) ||
         four == kJaNado ||
         four == kJaNazo || four == kJaShika || four == kJaMade || four == kJaSae)) {
        return false;
    }

    bool adjusted = false, desire = false;
    const int oldKind = base.patternKind, oldState = base.kind;
    const bool shortCausativePassive = w->settings.text && w->settings.improvements.causativePassive && !matched &&
        oldState == 8 && oldKind != 3 && oldKind != 7 && after().starts_with(kJaSare);
    if (shortCausativePassive && w->SuVerb(surface + std::string(Conjugation(oldKind, 8)))) return false;
    if (!matched && oldState == 4 && oldKind == 3 && w->GodanCausative(surface)) return false;  // 迷わせる of 迷う
    if (!matched && (oldState == 1 || oldState == 2 || oldState == 4 || shortCausativePassive)) {
        base.record = oldState;
        adjusted = true;
        sourceLength += pattern();
        base.patternKind = 7;
        tail = after();
        if (shortCausativePassive || (w->settings.text && w->settings.improvements.causativePassive &&
                                     oldState == 2 && tail.starts_with(kJaRare))) {
            base.record = 5;
            sourceLength += static_cast<int>(kJaRare.size());
            tail.remove_prefix(kJaRare.size());
        }
        int branch = 0;
        for (; branch < kConjugationBranches; branch++) {
            if (tail.starts_with(Conjugation(7, branch))) {
                base.kind = branch;
                break;
            }
        }
        if (branch == kConjugationBranches) return false;
        // The empty stem form gives way to the first later form that begins the tail, as after
        // an ichidan stem (褒められちゃった, 褒められず); not to an imperative (11, 12), which a
        // passive, potential or causative form does not take before よ (言えよ is 言え and よ), not
        // to 13 (the にな of a stem, no ending), nor to the start of a longer adverb
        // (見透かされずいぶん is ずいぶん).
        auto startsAdverb = [&](std::size_t n) {
            for (const Record* r = w->DoLookup(tail, kModeGeneral).records; r != nullptr; r = r->child) {
                if (r->source.size() > n && At(r->partCode, 0) == 'E' && At(r->partCode, 1) == '3') return true;
            }
            return false;
        };
        if (branch == 8) {
            for (int later = 9; later < kConjugationBranches; later++) {
                const std::string_view e = Conjugation(7, later);
                if (later != 11 && later != 12 && later != 13 && !e.empty() && tail.starts_with(e) && !startsAdverb(e.size())) {
                    base.kind = later;
                    break;
                }
            }
        }
        if (base.kind == 8 && (two == kJaComma || two.StartsWith(","))) base.kind = 7;
        // The たい of wanting after the られ of an ichidan or くる verb goes on the stem form, as after an
        // ichidan stem (褒められたい), not on the past た, and keeps the られ. A godan potential takes no
        // たい: the verb is another one (入れたい is 入れる).
        if (oldState == 4 && base.kind == 6 && Desire(tail)) {
            if (oldKind != 7 && oldKind != 11 && oldKind != 13) return false;
            base.kind = 10;
            desire = true;
        }
        patternWidth = WidthOf(Conjugation(7, base.kind));
        two = Left(after(), 2);
        four = Left(after(), 4);
    }

    bool state10 = base.kind == 10;
    six = {};
    if (const std::string_view r = after(); WidthOf(r) >= 6) six = Left(r, 6);
    if (((!adjusted && !matched && base.patternKind == 7 && base.kind == 8) || (base.patternMatched == 1 && base.kind == 8)) &&
        ((!(two == kJaNa) && !(four == kJaZaru) && !(four == kJaNna) && !(four == kJaNeba) && !(two == kJaNu)) ||
         six == kJaNasai || six == kJaNagara || four == kJaNao)) {
        state10 = true;
        base.kind = 10;
    }

    Window eight;
    if (w->settings.improvements.desireWindow && WidthOf(after()) >= 8) eight = Left(after(), 8);
    if ((!matched && base.patternKind == 7 && base.kind == 10) ||
        (surface == kJaOyo && Conjugation(base.patternKind, base.kind) == kJaBi)) {
        if (!(base.ClassifyTrailingPunctuation(after()) == 1 || four == kJaNiku || four == kJaTsutsu || four == kJaKire ||
              four == kJaKireKanji || four == kJaSou || after().starts_with("\"") || two == kJaMa || two == kJaHajiKanji ||
              two == kJaNasai || two == kJaNi || (SuruAfterMo(after()) && !OneKana(surface)))) {
            const std::pair<const Window*, std::string_view> compounds[] = {
                {&six, kJaNagara},     {&six, kJaNasai},       {&six, kJaTsutsua},     {&six, kJaHaji},
                {&four, kJaGata},      {&four, kJaGachi},      {&two, kJaGataKanji},   {&four, kJaYasu},
                {&four, kJaKane},      {&four, kJaYasuKanji},  {&four, kJaYou},        {&four, kJaUru},
                {&four, kJaTai},       {&eight, kJaTakatta},   {&four, kJaTaku},       {&eight, kJaTakereba},
                {&four, kJaTaki},      {&four, kJaKudasaKanji}, {&six, kJaKudasa},     {&four, kJaTsudzu},
                {&four, kJaSugi},      {&four, kJaNao},        {&two, kJaTsudzuKanji}, {&four, kJaSugiKanji},
                {&six, kJaSokona},     {&four, kJaSokonaKanji}, {&four, kJaDzura},     {&two, kJaNaoKanji},
                {&two, kJaAiKanji},    {&two, kJaA},           {&four, kJaKawaKanji},  {&four, kJaKawa},
                {&four, kJaAsa},       {&two, kJaAsaKanji},    {&four, kJaChira},      {&four, kJaChiraKanji},
                {&four, kJaTsuku},     {&four, kJaTsukuKanji},
            };
            bool compound = false;
            for (const auto& [win, s] : compounds) {
                if (*win == s && (s != kJaTaku || Taku(after()))) {
                    compound = true;
                    break;
                }
            }
            if (!compound) {
                if (!state10) return false;
                base.kind = 8;
            }
        }
    }

    if (!matched && base.patternKind == 7 && base.kind == 6 && (six == kJaKerepa || four == kJaKat || two == kJaI)) {
        patternWidth = 0;
        base.kind = 10;
    }

    length = sourceLength + pattern();
    base.ResetDirect(0);
    base.punct = 0;

    auto restore = [&]() {
        if (adjusted && patternWidth == 0 && !desire) {
            base.patternKind = oldKind;
            base.kind = oldState;
            if (base.record == 4) base.record = 0;
        }
    };
    auto fill = [&](const Group* g) {
        base.ResetDirect(g->entries.size());
        base.resultCount = static_cast<int>(g->entries.size());
        base.punct = base.ClassifyTrailingPunctuation(From(after(), g->length));
        for (std::size_t i = 0; i < g->entries.size(); i++) base.classes[i] = static_cast<uint8_t>(base.punct);
    };

    const std::string_view rest = after();
    const bool punctuation = tail.starts_with(kJaComma) || tail.starts_with(",");
    bool queryAllowed = !rest.empty() && !punctuation;
    if (queryAllowed && base.kind == 10 &&
        (four == kJaTai || eight == kJaTakatta || (four == kJaTaku && Taku(after())) || eight == kJaTakereba || four == kJaTaki ||
         four == kJaDzura || six == kJaSokona || four == kJaSokonaKanji)) {
        queryAllowed = false;
    }

    // The ending group at endingText, the text after the conjugation pattern.
    auto endingGroup = [&](std::string_view endingText) -> Group* {
        Group* g = nullptr;
        if (!queryAllowed || endingText.empty()) return g;
        const Lookup found = w->DoLookup(endingText, kModeVerbEnding);
        if (found.count == 0) return g;
        g = w->NewGroup(found.records, ' ');
        if (g->length <= 0) return g;
        bool keepScanning = true;
        for (;;) {
            fill(g);
            bool accepted = false;
            if (base.patternKind == 7 && base.kind == 8 &&
                (endingText.starts_with(kJaMasu) || endingText.starts_with(kJaMase) || endingText.starts_with(kJaMashi) ||
                 (w->settings.text && endingText.starts_with(kJaYaga)))) {
                base.kind = 10;
            } else {
                restore();
            }
            const std::string_view afterGroup = From(after(), g->length);
            for (int index = 0; keepScanning && index < static_cast<int>(g->entries.size()); index++) {
                const Entry& e = *g->entries[static_cast<std::size_t>(index)];
                if ((Has(e.gattr, "Sh") && Classify(afterGroup) != 0) || Byte(At(e.code, 1)) != base.kind + '0' ||
                    !w->EndingApplies(e, afterGroup, tokenIndex) ||
                    w->CutsWord(tail.substr(0, tail.size() - endingText.size()), endingText.substr(0, std::min<std::size_t>(static_cast<std::size_t>(g->length), endingText.size())), afterGroup)) {
                    base.enabled[index] = false;
                    continue;
                }
                base.group = g;
                if (g->length <= 0 || w->MatchDictionary(afterGroup, g->surface, '5', '4')) accepted = true;
                else keepScanning = false;
            }
            if (accepted) break;
            Record* more = g->records;
            g->records = nullptr;
            g = nullptr;
            if (more == nullptr) break;
            g = w->NewGroup(more, ' ');
            keepScanning = true;
        }
        return g;
    };
    Group* ending = endingGroup(rest);
    // V連用形 + やがる without an ending of its own conjugates as the verb in the form
    // of やがる (書きやがったので 썼으므로); its やがれ is always the imperative.
    if (ending == nullptr && w->settings.text && rest.starts_with(kJaYaga)) {
        int branch = 0, n = 0;
        if (AnalyseVerbTail(kJaYaga, '8', rest.substr(kJaYaga.size()), branch, n)) {
            if (branch == 4) branch = 11;
            patternWidth = WidthOf(tail.substr(0, static_cast<std::size_t>(pattern()) + kJaYaga.size() + static_cast<std::size_t>(n)));
            base.kind = branch;
            length = sourceLength + pattern();
            ending = endingGroup(after());
        }
    }
    // A て form whose いる has lost its い before its た, where no ending row of the て form or only a
    // shorter one covers the text (見てたぞ, してたんだろ, 言ってたじゃんの): the ending after the た is
    // read on the past of いる and written for 있 after 고 (5지만 → 1고 있었지만). The sound stage writes
    // the い back only before a bare た and more kana (見てたところ); before longer rows the full form
    // does not read as well as this yet.
    if (base.kind == 7 && w->settings.text && queryAllowed && rest.starts_with(Conjugation(7, 6)) &&
        (ending == nullptr || ending->length < static_cast<int>(rest.size()))) {
        const int teWidth = patternWidth;
        const std::size_t form = Conjugation(7, 6).size();
        patternWidth += WidthOf(Conjugation(7, 6));
        base.kind = 6;
        Group* g = endingGroup(after());
        const std::span<const std::string_view> head = dict::ClassMembers("ko.progressive-head");
        if (g != nullptr && (ending == nullptr || static_cast<int>(form) + g->length > ending->length) && !head.empty() &&
            ProgressiveEndings(*g, base, head.front())) {
            ending = g;
            length = sourceLength + pattern();
        } else {
            patternWidth = teWidth;
            base.kind = 7;
            if (ending != nullptr) ending = endingGroup(rest);
        }
    }
    // The ちゃ form whose ちゃう is in its て form before いる (寝ちゃっていた, 言っちゃっているから): the
    // state after it happened, done and past as ちゃった; the ending after いる is read on the past
    // and written for 버리 after the verb (5지만 → 6 버렸지만). Without an ending of its own the form
    // takes the empty ending below.
    std::string_view auxHead;
    if (base.kind == 17 && w->settings.text && queryAllowed && rest.starts_with(Conjugation(0, 7))) {
        const std::string_view iru = rest.substr(Conjugation(0, 7).size());
        const std::span<const std::string_view> head = dict::ClassMembers("ko.completive-head");
        std::size_t n = 0;  // bytes of the form of いる
        for (const int branch : {0, 6}) {
            const std::string form = std::string(kJaI) + std::string(Conjugation(7, branch));
            if (n == 0 && iru.starts_with(form)) n = form.size();
        }
        if (n > 0 && !head.empty() && (ending == nullptr || ending->length < static_cast<int>(rest.size()))) {
            const int chaWidth = patternWidth;
            patternWidth += WidthOf(rest.substr(0, Conjugation(0, 7).size() + n));
            base.kind = 6;
            if (Group* g = endingGroup(after()); g != nullptr && ProgressiveEndings(*g, base, head.front())) {
                ending = g;
                length = sourceLength + pattern();
            } else if (after().empty() || Classify(after()) != 0) {
                ending = nullptr;
                auxHead = head.front();
                length = sourceLength + pattern();
            } else {
                patternWidth = chaWidth;
                base.kind = 17;
            }
        }
    }

    // A past before the げ of an ichidan form (ただしいたげられる: ただ, し, い, た, げ) is no word: its
    // only such use is the あげる said without its あ (作ったげる), which the sound stage writes back.
    if (ending == nullptr && base.kind == 6 && w->settings.text && queryAllowed && rest.starts_with(kJaGe) &&
        Conjugation(base.patternKind, 7).size() == Conjugation(base.patternKind, 6).size()) {
        base.group = nullptr;
        return false;
    }
    if (ending == nullptr) {
        if (following != nullptr && base.patternKind == 7 && base.kind == 8) {
            if (Has(GAttr(0), "Ke") && !(!two.Empty() && Classify(two) != 0)) return false;
            for (const Record* r = following; r != nullptr; r = r->child) {
                if (const char sel = At(r->partCode, 1); sel != '4' && sel != '5') {
                    if (r->source.size() > group->surface.size() || (r->source.size() == group->surface.size() && two.Empty())) {
                        return false;
                    }
                }
            }
        }
        const Lookup found = w->DoLookup("", kModeVerbEnding);
        if (found.count == 0) {
            base.group = nullptr;
            return false;
        }
        auto applies = [&](const Entry& e) {
            return Byte(At(e.code, 1)) == base.kind + '0' && w->EndingApplies(e, after(), tokenIndex);
        };
        // A plain-dictionary group none of whose endings applies gives way to the groups
        // after it.
        for (ending = w->NewGroup(found.records, ' ');; ending = w->NewGroup(ending->records, ' ')) {
            if ((group->surface == kJaKudasaKanji || group->surface == kJaKudasa) && base.patternKind == 8 && base.kind == 10) {
                ending->surface = Left(tail, 2).Str();
            } else {
                ending->surface.assign(tail.substr(0, static_cast<std::size_t>(pattern())));
            }
            fill(ending);
            restore();
            if (!OwnedGroup(ending) || ending->records == nullptr ||
                std::any_of(ending->entries.begin(), ending->entries.end(), [&](const Entry* e) { return applies(*e); })) {
                break;
            }
        }
        for (int index = 0; index < static_cast<int>(ending->entries.size()); index++) {
            if (applies(*ending->entries[static_cast<std::size_t>(index)])) base.group = ending;
            else base.enabled[index] = false;
        }
        if (!auxHead.empty() && !ProgressiveEndings(*ending, base, auxHead)) {
            base.group = nullptr;
            return false;
        }
    }

    base.group = ending;
    base.resultCount = static_cast<int>(ending->entries.size());
    length += ending->length;
    const int end = length;
    const bool specialEnd = four == kJaTai || eight == kJaTakatta || (four == kJaTaku && Taku(after())) || eight == kJaTakereba ||
                            four == kJaTaki || four == kJaSugi || four == kJaSugiKanji || six == kJaTsudzuke ||
                            four == kJaTsudzukeKanji || four == kJaTsudzu || four == kJaDzura || six == kJaSokona ||
                            four == kJaSokonaKanji;
    if ((base.kind == 10 && specialEnd) || following == nullptr) return true;
    const std::string_view consumed = From(text, start);
    const int endWidth = WidthOf(consumed.substr(0, std::min<std::size_t>(static_cast<std::size_t>(end), consumed.size())));
    for (const Record* r = following;;) {
        if (const char sel = At(r->partCode, 1); sel != '4' && sel != '5' && end < static_cast<int>(r->source.size())) break;
        if (endWidth == 2) break;
        if ((r = r->child) == nullptr) return true;
    }
    base.group = nullptr;
    return false;
}

// A verb of the す row with this stem or one of its suffixes, excluding Korean causatives.
bool Worker::SuVerb(std::string_view stem) {
    for (std::size_t i = 0; i < stem.size();) {
        const std::string_view tail = stem.substr(i);
        if (text::RuneCount(tail) < 2) break;
        i += text::DecodeRune(tail).size;
        const std::string_view query = arena.Keep(std::string(tail) + "す");
        const Lookup found = DoLookup(query, kModeGeneral);
        if (found.count == 0) continue;
        for (const Record* record = found.records; record != nullptr; record = record->child) {
            if (record->source == tail && At(record->partCode, 1) == '4' && At(record->partCode, 3) == '3' &&
                !Has(record->target, "게 ")) return true;
        }
    }
    return false;
}

}  // namespace hdTrans::word
