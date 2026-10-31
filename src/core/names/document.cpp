#include "core/names/document.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/names/chars.h"
#include "core/names/given.h"
#include "core/names/historical.h"
#include "core/names/labels.h"
#include "core/names/name_cues.h"
#include "core/names/readings.h"
#include "core/names/registry.h"
#include "core/names/ruby.h"
#include "core/pipeline/normalize.h"
#include "core/dict/builtin.h"
#include "core/dict/classes.h"
#include "core/dict/names.h"
#include "core/dict/pack.h"
#include "core/dict/char_readings.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"
#include "core/text/lines.h"
#include "core/tokenizer/classify.h"
#include "core/userdict/userdict.h"

namespace hdTrans::pipeline {

namespace {

// Whether key occurs in text with a person title after it or 艦 before it.
bool Titled(std::string_view text, std::string_view key) {
    for (std::size_t i = 0;;) {
        const std::size_t at = text.find(key, i);
        if (at == std::string_view::npos) return false;
        std::string_view before = text.substr(0, at);
        if (before.ends_with("、")) before.remove_suffix(std::string_view("、").size());
        const std::string_view after = text.substr(at + key.size());
        if (before.ends_with("艦")) return true;
        if (dict::StartsWithMember(HDTRANS_CLASS("honorific.person-title"), after) > 0) return true;
        i = at + key.size();
    }
}

// The codes of the entries compiled into codes get their with-XXX and plain views, as the
// snapshot's own codes do.
void FinishCodes(dict::CodeTable& codes) {
    for (int32_t c = 0; c < static_cast<int32_t>(codes.Size()); c++) {
        codes.SetWithXXX(c);
        codes.SetAsPlain(c);
    }
}

// Lines of lines.tsv: the whole-line rows, JP TAB KR (builtin).
std::map<std::string, std::string> WholeLineRows() {
    std::map<std::string, std::string> rows;
    const std::string text = dict::BuiltinText("lines.tsv");
    for (const std::string_view raw : text::Lines(text)) {
        const std::string_view line = names::internal::TrimCR(raw);
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) continue;
        std::string_view kr = line.substr(tab + 1);
        const std::size_t next = kr.find('\t');
        if (next != std::string_view::npos) kr = kr.substr(0, next);
        const std::string_view jp = line.substr(0, tab);
        if (jp.empty() || jp.starts_with("//")) continue;
        rows[std::string(jp)] = std::string(kr);
    }
    return rows;
}

// Replaces a whole-line entry while preserving surrounding whitespace.
std::optional<std::string> WholeLineOf(const std::map<std::string, std::string>& rows, std::string_view line) {
    const std::string_view t = names::internal::TrimLineSpace(line);
    const auto it = rows.find(std::string(t));
    if (it == rows.end()) return std::nullopt;
    const std::size_t at = line.find(t);
    std::string out(line.substr(0, at));
    out += it->second;
    out += line.substr(at + t.size());
    return out;
}

}  // namespace

struct DocumentData {
    explicit DocumentData(const dict::Snapshot& snap) : registry(&snap), wholeLines(WholeLineRows()) {
        for (const dict::File& f : snap.PlainFiles()) {
            if (f.layer == dict::FileLayer::Readings) registry.LoadReadings(dict::internal::WithoutHeader(f.text));
        }
        for (const dict::UserEntry& e : snap.TextPlain().Entries()) {
            if (e.file < 0) continue;
            dictKeys.insert(e.key);
            if (e.name) registry.SetName(e.key, e.target);
        }
    }

    names::Registry registry;
    dict::KeySet dictKeys;
    std::map<std::string, std::string> wholeLines;
};

namespace {

std::string VerseKey(std::string_view line) {
    if (line.ends_with('\r')) line.remove_suffix(1);
    return std::string(line);
}

// A line in て goes on in the next one when it holds no sentence before (…よ？私に任せて asks) and the
// next line is Japanese with hiragana, not a name or title (骨まで愛して before 莫文蔚).
bool Continues(std::string_view line, std::string_view next) {
    line = names::internal::TrimSpace(line);
    next = names::internal::TrimSpace(next);
    if ((!line.ends_with("て") && !line.ends_with("で")) || next.empty()) return false;
    const auto in = [&](std::string_view mark) { return line.find(mark) != std::string_view::npos; };
    return !std::ranges::any_of(dict::ClassMembers(HDTRANS_CLASS("mark.sentence-end")), in) &&
           !names::internal::AllRunes(next, [](char32_t c) { return !names::internal::IsHiragana(c); });
}

}  // namespace

namespace internal {

std::set<std::string> VerseLines(std::span<const std::string> lines) {
    std::set<std::string> verse;
    for (std::size_t i = 0; i + 1 < lines.size(); i++) {
        if (Continues(lines[i], lines[i + 1])) verse.insert(VerseKey(lines[i]));
    }
    return verse;
}

std::vector<std::string> PersonPlaces(const dict::Snapshot& snap, std::string_view text, names::Registry* registry,
                                      const dict::KeySet* persons) {
    std::vector<std::string> cues = names::SpeakerCues(text);
    std::vector<std::string> out;
    for (const std::string& k : snap.PersonPlaceKeys(text)) {
        const bool named = registry != nullptr && (registry->Named(k) || (persons != nullptr && persons->count(k) > 0));
        const bool cued = std::find(cues.begin(), cues.end(), k) != cues.end();
        if (named || Titled(text, k) || cued) out.push_back(k);
    }
    return out;
}

}  // namespace internal

std::vector<std::string> CuedNames(const dict::Snapshot& snap, std::string_view text) {
    std::vector<std::string> keys;
    for (const std::string& key : names::SpeakerCues(text)) {
        if (std::find(keys.begin(), keys.end(), key) == keys.end() && snap.PersonCueKey(key)) keys.push_back(key);
    }
    return keys;
}

namespace {

const std::vector<names::NameCue>& CuesOf(DocumentContext& doc, std::string_view text) {
    auto found = doc.nameCues.find(text);
    if (found == doc.nameCues.end()) found = doc.nameCues.emplace(text, names::NameCues(*doc.snap, text)).first;
    return found->second;
}

// The listed names as pack name rows, id names:LINE: of any word of the name's key, or for a hon
// name of one an honorific follows.
// The pack row id of a listed name: names:LINE for the list of the session, FILE:LINE for a file.
std::string RowId(const names::Listed& l) {
    std::string source = l.source.empty() ? std::string("names") : l.source;
    for (char& c : source) {
        if (c == '\t' || c == '\n' || c == '\r') c = '_';
    }
    return source + ":" + std::to_string(l.line);
}

std::string ListPackRows(const names::List& list) {
    std::string honorific = "next=";
    for (const std::string_view h : dict::internal::kNameHonorifics) {
        if (honorific.size() > 5) honorific += '|';
        honorific += h;
    }
    std::string out = "id\tlemma\tpos\tcond\ttarget\tslot\n";
    for (const names::Listed& l : list.names) {
        out += RowId(l) + "\t" + l.ja + "\tn\t" + (l.honorificOnly ? honorific : "*") + "\t" +
               l.ko + "\tname\n";
    }
    return out;
}

}  // namespace

bool CompileNames(const dict::Snapshot& snap, const names::List& list, std::unique_ptr<DocumentNames>& out,
                  std::vector<std::string>* errors) {
    out = std::make_unique<DocumentNames>(snap.DocumentData().registry);
    names::List kept;
    if (!list.names.empty()) {
        const std::set<std::string, std::less<>> off = dict::CastOffNames();
        for (const names::Listed& l : list.names) {
            if (!off.contains(l.ja)) kept.names.push_back(l);
        }
    }
    userdict::Families families;
    std::vector<dict::PackError> packErrors;
    dict::ParsePackRows("names", ListPackRows(kept), &snap.Base(), families, out->packs, packErrors);
    // Row i of the rows is line i + 2 of their text, under the header.
    std::set<std::size_t> failed;
    for (const dict::PackError& e : packErrors) {
        const std::size_t i = e.line >= 2 ? static_cast<std::size_t>(e.line - 2) : kept.names.size();
        if (i < kept.names.size()) failed.insert(i);
        if (errors != nullptr) {
            errors->push_back((i < kept.names.size() ? names::Where(kept.names[i]) : std::string("0")) + ": " + e.message);
        }
    }
    std::map<std::string, std::string, std::less<>> honIds;  // row id to JA
    for (const names::Listed& l : kept.names) {
        if (l.honorificOnly) honIds[RowId(l)] = l.ja;
    }
    for (std::size_t i = 0; i < out->packs.rows.size(); i++) {
        if (const auto h = honIds.find(out->packs.rows[i].id); h != honIds.end()) out->honRows[h->second] = i;
    }
    for (std::size_t i = 0; i < kept.names.size(); i++) {
        if (failed.contains(i)) continue;
        const names::Listed& l = kept.names[i];
        out->list.names.push_back(l);
        out->keys.insert(l.ja);
        out->registry.AddListed(l);
        if (names::PersonKind(l.kind)) {
            out->persons[l.ja] = l.ko;
        } else {
            out->rows += l.ja + "\t" + l.ko + "\t" + (l.kind == names::ListKind::Place ? "place" : "n") + "\n";
        }
        out->key += std::string("\0listed\0", 8) + names::ListLine(l);
    }
    return packErrors.empty();
}

std::unique_ptr<DocumentContext> BuildDocument(const dict::Snapshot& snap, std::span<const std::string> lines,
                                               bool extraBuiltin, const DocumentNames* names) {
    std::vector<std::string> canonicalLines;
    if (std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
            return line.find("゙") != std::string::npos || line.find("゚") != std::string::npos;
        })) {
        canonicalLines.reserve(lines.size());
        for (const std::string& line : lines) canonicalLines.push_back(internal::ComposeKanaPairs(line));
        lines = canonicalLines;
    }
    auto doc = std::make_unique<DocumentContext>();
    doc->snap = &snap;
    doc->extraBuiltin = extraBuiltin;
    doc->data = &snap.DocumentData();
    doc->codes = std::make_unique<dict::CodeTable>();
    doc->names = names;
    doc->registry = std::make_unique<names::Registry>(names != nullptr ? names->registry : doc->data->registry);
    names::Registry& r = *doc->registry;
    for (const std::string& line : lines) r.Add(line);

    doc->registryKey = r.Key();
    bool female = false, maleKin = false, quoted = false;
    std::vector<names::HistoricalLine> historicalLines;
    historicalLines.reserve(lines.size());
    dict::KeySet persons;
    std::map<std::string, std::size_t> recurring;
    for (const std::string& line : lines) {
        historicalLines.push_back(names::ReadHistoricalLine(snap, line));
        for (const std::string& key : internal::PersonPlaces(snap, line, nullptr, nullptr)) persons.insert(key);
        const internal::SpeechCues speech = internal::ReadSpeechCues(line);
        female = female || speech.female;
        maleKin = maleKin || speech.maleKin;
        quoted = quoted || speech.quoted;
        for (const names::NameCue& c : CuesOf(*doc, line)) {
            if (c.strong) doc->cued.insert(c.key);
            if (c.recurring) recurring[c.key]++;
        }
    }
    std::vector<const names::HistoricalLine*> historical;
    for (const names::HistoricalLine& h : historicalLines) historical.push_back(&h);
    for (const auto& [key, count] : recurring) {
        if (count >= 2) doc->cued.insert(key);
    }
    doc->female = female && !maleKin;
    doc->key = doc->registryKey;
    if (doc->female) doc->key += std::string("\0female", 7);
    doc->narrated = quoted;
    if (doc->narrated) doc->key += std::string("\0narrated", 9);

    doc->persons.assign(persons.begin(), persons.end());
    std::sort(doc->persons.begin(), doc->persons.end());
    if (!doc->persons.empty()) {
        doc->key += std::string("\0persons\t", 9);
        for (std::size_t i = 0; i < doc->persons.size(); i++) {
            if (i > 0) doc->key += "\t";
            doc->key += doc->persons[i];
        }
    }
    doc->persons_ = persons;
    if (!doc->cued.empty()) {
        doc->key += std::string("\0cued", 5);
        for (const std::string& k : std::set<std::string>(doc->cued.begin(), doc->cued.end())) doc->key += "\t" + k;
    }

    doc->verseLines = internal::VerseLines(lines);
    doc->verse.assign(doc->verseLines.begin(), doc->verseLines.end());
    for (const std::string& line : doc->verse) doc->key += std::string("\0verse\0", 7) + line;

    doc->historical = names::HistoricalRows(snap, historical);
    if (!doc->historical.empty()) doc->key += std::string("\0historical\0", 12) + doc->historical;

    if (names != nullptr && !names->packs.Empty()) {
        doc->packs = &names->packs;
        const std::vector<std::string> honorificOnly = r.HonorificOnlyNames();
        for (const auto& [t, row] : names->honRows) {
            if (std::binary_search(honorificOnly.begin(), honorificOnly.end(), t)) continue;
            if (doc->ownPacks == nullptr) {
                doc->ownPacks = std::make_unique<dict::PackTable>(names->packs);
                doc->packs = doc->ownPacks.get();
            }
            doc->ownPacks->rows[row].atoms.clear();
        }
    }
    for (const std::string& t : r.Names()) doc->accepted.insert(t);
    doc->named = doc->accepted;
    for (const std::string& t : r.NamedSurfaces()) doc->named.insert(t);
    for (const auto& [t, n] : r.ProseKatakana()) doc->prose.emplace(t, n);

    const std::string registryDict = r.Dict();
    userdict::Context ctx{doc->codes.get(), &doc->families, &snap.Base()};
    doc->extra = userdict::Parse(registryDict, -1, ctx).entries;
    if (names != nullptr) {
        for (dict::UserEntry& e : userdict::Parse(names->rows, -1, ctx).entries) doc->extra.push_back(std::move(e));
        for (dict::UserEntry& e : doc->extra) {
            if (names->keys.contains(e.key)) e.file = kListedFile;
        }
        doc->key += names->key;
    }
    for (dict::UserEntry& e : userdict::Parse(doc->historical, -1, ctx).entries) doc->extra.push_back(std::move(e));
    FinishCodes(*doc->codes);
    return doc;
}

std::string ProposeNames(const dict::Snapshot& snap, std::span<const std::string> lines, const DocumentNames* names) {
    const std::unique_ptr<DocumentContext> doc = BuildDocument(snap, lines, false, names);
    std::string out;
    for (const names::Listed& l : doc->registry->Proposed()) out += names::ListLine(l) + "\n";
    return out;
}

namespace {

std::string Utf8Of(char32_t c) {
    std::string out;
    text::AppendRune(out, c);
    return out;
}

// A line that is a hiragana run of three to six kana and nothing else that the name layer
// reads as a person, a speaker line (かすみ 카스미, しおり 시오리), read as that person; not
// a run the base dictionary reads as an adverb, a creature, plant or food, or a place
// (いつか, ひまわり, たんぽぽ).
std::optional<std::string> KanaNameLine(const dict::Snapshot& snap, std::string_view line, std::string_view t) {
    const std::size_t runes = names::internal::RuneCount(t);
    if (runes < 3 || runes > 6 || !names::internal::AllRunes(t, names::internal::IsHiragana)) return std::nullopt;
    std::string name;
    for (const dict::Record& rec : snap.NameRecords(t)) {
        if (rec.codes == nullptr || !rec.codes->sem.Text().starts_with("QPN") || !names::internal::IsHangulText(rec.target)) continue;
        name = std::string(rec.target);
        break;
    }
    if (name.empty()) return std::nullopt;
    for (const dict::Candidate& c : snap.RetrieveRecords(t, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != t) continue;
        const std::string_view sem = c.codes->sem.Text();
        const char tag = c.codes->part.Empty() ? 0 : c.codes->part.Tag();
        if (tag == 'E' || tag == 'P' || sem.starts_with("QA") || sem.starts_with("QCT") ||
            sem.find("VPl") != std::string_view::npos) {
            return std::nullopt;
        }
    }
    const std::size_t at = line.find(t);
    return std::string(line.substr(0, at)) + name + std::string(line.substr(at + t.size()));
}

// The marks around a lone kanji written as the text path writes them (！ !, （ ().
std::string WrittenMarks(std::string_view marks) {
    std::string out;
    for (std::size_t i = 0; i < marks.size();) {
        const text::Rune r = text::DecodeRune(marks.substr(i));
        if (r.cp >= 0xFF01 && r.cp <= 0xFF5E) {
            out += static_cast<char>(r.cp - 0xFEE0);  // full-width ASCII
        } else {
            tokenizer::AppendCanonical(out, r.cp);
        }
        i += std::max<std::size_t>(1, r.size);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::optional<std::string> NameLine(const dict::Snapshot& snap, std::string_view line, bool marked = false);

// Whether a dictionary's name spells written, a reading as the registry writes it, but for the
// conventions of SameSound and spaces, and し, じ and ち with a glide written without it
// (순푸테이 코아사 for しゅんぷうていこあさ, 주 for じゅ). A name that writes ㅈ where the reading has
// ㅊ, or ㅊ for ㅈ, spells another sound (주야 for 츄야 of ちゅうや).
bool SpellsReading(std::string_view written, std::string_view name) {
    auto affricates = [](std::string_view s) {
        std::string out;
        while (!s.empty()) {
            const text::Rune ru = text::DecodeRune(s);
            s.remove_prefix(ru.size == 0 ? 1 : ru.size);
            if (const auto syl = text::Split(ru.cp); syl && (syl->initial == 12 || syl->initial == 14)) {
                out += syl->initial == 12 ? 'j' : 'c';
            }
        }
        return out;
    };
    auto key = [](std::string_view s) {
        std::string out;
        while (!s.empty()) {
            const text::Rune ru = text::DecodeRune(s);
            s.remove_prefix(ru.size == 0 ? 1 : ru.size);
            if (ru.cp == U' ') continue;
            char32_t cp = ru.cp;
            if (const auto syl = text::Split(cp);
                syl && (syl->initial == 9 || syl->initial == 10 || syl->initial == 12 || syl->initial == 14)) {
                const int v = syl->vowel == 2 ? 0 : syl->vowel == 6 ? 4 : syl->vowel == 12 ? 8 : syl->vowel == 17 ? 13 : syl->vowel;
                cp = text::Join(syl->initial, v, syl->final);
            }
            text::AppendRune(out, cp);
        }
        return out;
    };
    return text::SameSound(key(written), key(name)) && affricates(written) == affricates(name);
}

// A lone kanji in brackets or before sentence marks, read like the bare line ((望) (노조미),
// 恵？ 메구미?, 勝！ 카츠!) unless it is a word (NameLine with marked).
std::optional<std::string> MarkedKanjiNameLine(const dict::Snapshot& snap, std::string_view t) {
    constexpr std::u32string_view kOpen = U"（(「『【", kClose = U"）)」』】", kMarks = U"！？!?…‥。、～ー";
    std::size_t start = 0, end = t.size();
    const text::Rune first = text::DecodeRune(t);
    const std::size_t bracket = kOpen.find(first.cp);
    if (bracket != std::u32string_view::npos) start = first.size;
    while (end > start) {
        const text::Rune last = names::internal::RuneBefore(t, end);
        if (kMarks.find(last.cp) == std::u32string_view::npos) break;
        end -= last.size;
    }
    std::size_t closeAt = end;
    if (bracket != std::u32string_view::npos) {
        const text::Rune last = names::internal::RuneBefore(t, end);
        if (end <= start || last.cp != kClose[bracket]) return std::nullopt;
        closeAt = end - last.size;
    }
    const std::string_view core = t.substr(start, closeAt - start);
    if ((start == 0 && end == t.size()) || names::internal::RuneCount(core) != 1 ||
        !names::internal::AllRunes(core, names::internal::IsKanji)) {
        return std::nullopt;
    }
    const std::optional<std::string> name = NameLine(snap, core, true);
    if (!name) return std::nullopt;
    return WrittenMarks(t.substr(0, start)) + *name + WrittenMarks(t.substr(closeAt));
}

// A line that is one name and nothing else, a speaker line before a quote: a run
// of kanji with a person reading, read as that person (筧 카케이), or a hiragana name
// (KanaNameLine). A marked lone kanji (MarkedKanjiNameLine) is also a word where an adjective or
// adjectival noun has it as its stem (好, 楽) or a noun row reads it, even as its Sino-Korean
// reading (王 왕); a bare one, a speaker label, only where a noun reads it otherwise.
std::optional<std::string> NameLine(const dict::Snapshot& snap, std::string_view line, bool marked) {
    const std::string_view t = names::internal::TrimLineSpace(line);
    const std::size_t runes = names::internal::RuneCount(t);
    if (runes == 0 || runes > 6) return std::nullopt;
    if (names::internal::AllRunes(t, names::internal::IsHiragana)) return KanaNameLine(snap, line, t);
    if (!names::internal::AllRunes(t, names::internal::IsKanji)) return std::nullopt;
    std::string surname, given;  // the first person readings of each kind
    bool word = false;           // a word of its own beyond the kanji's Sino-Korean reading
    auto person = [&](const dict::Codes* codes, std::string_view target) {
        if (codes == nullptr || !codes->sem.Text().starts_with("QPN") || !names::internal::IsHangulText(target)) return;
        std::string& slot = codes->gram.Text().starts_with("Wa") ? surname : given;
        if (slot.empty()) slot = std::string(target);
    };
    auto noun = [&](const dict::Codes* codes, std::string_view target) {
        if (codes == nullptr || codes->sem.Text().starts_with("QPN") || codes->sem.Text().starts_with("QCT")) return;
        const char tag = codes->partText.empty() ? 0 : codes->partText[0];
        if (marked && (tag == 'H' || tag == 'P') && codes->partText.size() > 1 &&
            (codes->partText[1] == '7' || codes->partText[1] == '8')) {
            word = true;
            return;
        }
        if (tag != 'N' && tag != 'I') return;
        if (marked && codes->sem.Text().find("XXX") != std::string_view::npos) return;
        if (!marked && runes == 1) {
            const std::optional<char32_t> sino = dict::Written(text::DecodeRune(t).cp);
            if (sino && target == Utf8Of(*sino)) return;
        }
        word = true;
    };
    for (const dict::Candidate& c : snap.RetrieveRecords(t, dict::LookupMode::General)) {
        person(c.codes, c.target);
        noun(c.codes, c.target);
    }
    for (const dict::Record& rec : snap.NameRecords(t)) person(rec.codes, rec.target);
    // A user's name row is written as the user wrote it (the shipped 郎 rows lose their long vowels).
    std::string userName;
    for (const dict::UserEntry* e : snap.TextPlain().Search(t, dict::LookupMode::General)) {
        if (e->key != t || e->nameLayer) continue;
        noun(e->codes, e->target);
        if (e->name && userName.empty()) userName = e->target;
    }
    // A surname of two kanji or more names the speaker though it is a word too (高峰
    // 타카미네); a given name or a single kanji only where it is no word (朔夜, 慶; 未来,
    // 輪廻 and 愛 stay words).
    std::string name;
    if (!surname.empty() && (runes > 1 || !word)) name = surname;
    else if (!given.empty() && !word) name = given;
    if (name.empty()) return std::nullopt;
    if (!userName.empty()) name = userName;
    const std::size_t at = line.find(t);
    return std::string(line.substr(0, at)) + name + std::string(line.substr(at + t.size()));
}

// Whether a word of the snapshot starts at line[at] or at a kanji of the run before it and
// reaches end (WriteKinTerms).
bool KanjiWordHolds(const void* context, std::string_view line, std::size_t at, std::size_t end) {
    const auto& snap = *static_cast<const dict::Snapshot*>(context);
    for (std::size_t start = at;;) {
        for (const dict::Candidate& c : snap.GetWordInfo(line.substr(start), dict::LookupMode::General, {}).list) {
            if (start + c.source.size() >= end) return true;
        }
        if (start == 0) return false;
        const text::Rune last = names::internal::RuneBefore(line, start);
        if (!names::internal::IsKanji(last.cp)) return false;
        start -= last.size;
    }
}

}  // namespace

LineFacts ReadLine(DocumentContext& doc, std::string_view line) {
    LineFacts facts;
    if (line.ends_with('\r')) line.remove_suffix(1);
    std::string canonical;
    if (line.find("゙") != std::string_view::npos || line.find("゚") != std::string_view::npos) {
        canonical = internal::ComposeKanaPairs(line);
        line = canonical;
    }
    names::Registry& r = *doc.registry;
    std::string cueText;
    const std::string kin = pipeline::internal::WriteKinTerms(line, doc.female, KanjiWordHolds, doc.snap);
    facts.text = r.DropReadings(names::StripRuby(kin), &cueText, &facts.cued);
    const std::string_view cues = cueText.empty() ? std::string_view(facts.text) : std::string_view(cueText);
    dict::KeySet plain, guarded;
    for (const names::Occurrence& o : r.Occurrences(facts.text)) {
        if (o.noun) {
            guarded.insert(o.surface);
        } else {
            plain.insert(o.surface);
        }
    }
    // A name only before an honorific takes part where an honorific follows it in the line.
    for (const std::string& t : r.HonorificOnlyNames()) {
        if (plain.count(t) == 0) guarded.insert(t);
    }
    for (const std::string& t : guarded) {
        if (plain.count(t) == 0) facts.skip.push_back(t);
    }
    std::sort(facts.skip.begin(), facts.skip.end());
    facts.persons = internal::PersonPlaces(*doc.snap, cues, doc.registry.get(), &doc.persons_);
    std::sort(facts.persons.begin(), facts.persons.end());
    facts.wholeLine = WholeLineOf(doc.data->wholeLines, facts.text);
    if (!facts.wholeLine) facts.wholeLine = NameLine(*doc.snap, facts.text);
    // A name alone on its line is read by a reading written over it where the dictionary's name
    // spells another (龍生《たつき》 타츠키, not 타츠오).
    if (facts.wholeLine && facts.text != kin) {
        const std::string_view t = names::internal::TrimLineSpace(facts.text);
        names::internal::RubyMatch m;
        if ((names::internal::FindRuby(kin, 0, m) || names::internal::FindReading(kin, 0, m) ||
             names::internal::FindGloss(kin, 0, m)) && m.base == t) {
            const std::size_t at = facts.text.find(t);
            const std::size_t tail = facts.text.size() - at - t.size();
            const std::string name = facts.wholeLine->substr(at, facts.wholeLine->size() - at - tail);
            const std::string written = r.Written(m.reading);
            if (!written.empty() && !SpellsReading(written, name)) {
                facts.wholeLine = facts.text.substr(0, at) + written + facts.text.substr(at + t.size());
            }
        }
    }
    // A reading in the line ((恭(きょう)）) is read, not the name.
    if (!facts.wholeLine && names::internal::TrimLineSpace(facts.text) == names::internal::TrimLineSpace(line)) {
        facts.wholeLine = MarkedKanjiNameLine(*doc.snap, names::internal::TrimLineSpace(facts.text));
    }
    facts.verse = doc.verseLines.count(std::string(line)) > 0;
    facts.prose = names::ProseRuns(names::StripRuby(line));
    for (const names::NameCue& c : CuesOf(doc, cues)) facts.cued.push_back(c.key);
    std::sort(facts.cued.begin(), facts.cued.end());
    facts.cued.erase(std::unique(facts.cued.begin(), facts.cued.end()), facts.cued.end());
    return facts;
}

LineLookup LookupOf(DocumentContext& doc, const LineFacts& facts) {
    LineLookup out;
    std::string key;
    for (const std::string& t : facts.skip) {
        key += std::to_string(t.size());
        key += ':';
        key += t;
    }
    auto found = doc.plainByKey.find(key);
    if (found == doc.plainByKey.end()) {
        std::vector<dict::UserEntry> entries;
        for (const dict::UserEntry& e : doc.extra) {
            if ((e.file == kListedFile || !doc.data->dictKeys.contains(e.key)) &&
                !std::binary_search(facts.skip.begin(), facts.skip.end(), e.key)) entries.push_back(e);
        }
        DocumentContext::CachedLookup cached;
        cached.plain = std::make_unique<dict::UserText>(doc.snap->TextPlain(doc.extraBuiltin), std::move(entries));
        if (!facts.skip.empty()) {
            for (const std::string& t : doc.accepted) {
                if (!std::binary_search(facts.skip.begin(), facts.skip.end(), t)) cached.confirmed.insert(t);
            }
        }
        found = doc.plainByKey.emplace(key, std::move(cached)).first;
    }
    out.plain = found->second.plain.get();
    out.confirmed = facts.skip.empty() ? &doc.accepted : &found->second.confirmed;
    out.persons.insert(facts.persons.begin(), facts.persons.end());
    out.cued = doc.cued;
    for (const std::string& t : facts.cued) {
        const std::vector<std::size_t> keys = doc.snap->PersonKeysAt(t);
        if (!keys.empty() && keys.back() == t.size()) out.cued.insert(t);
    }
    out.prose = facts.prose;
    return out;
}

LineContext ContextOf(const DocumentContext& doc, const LineLookup& lookup) {
    LineContext ctx;
    ctx.document = true;
    ctx.female = doc.female;
    ctx.narrated = doc.narrated;
    ctx.extraBuiltin = doc.extraBuiltin;
    ctx.lookup.plain = lookup.plain;
    ctx.lookup.confirmed = lookup.confirmed;
    ctx.lookup.persons = &lookup.persons;
    ctx.lookup.cued = &lookup.cued;
    ctx.named = &doc.named;
    ctx.prose = &doc.prose;
    ctx.lineProse = &lookup.prose;
    if (doc.names != nullptr && !doc.names->persons.empty()) ctx.listed = &doc.names->persons;
    return ctx;
}

}  // namespace hdTrans::pipeline

namespace hdTrans::dict {

const pipeline::DocumentData& Snapshot::DocumentData() const {
    std::call_once(documentOnce_, [this] {
        documentData_ = std::make_shared<const pipeline::DocumentData>(*this);
    });
    return *documentData_;
}

}  // namespace hdTrans::dict
