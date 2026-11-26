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

// Whether the key at byte at of line stands as a word of its own: no kanji right before or after it (not
// 協力, 朝日新聞, 毎朝日).
bool KanjiBounded(std::string_view line, std::size_t at, std::size_t n) {
    if (at > 0 && names::internal::IsKanji(names::internal::RuneBefore(line, at).cp)) return false;
    return at + n >= line.size() || !names::internal::IsKanji(text::DecodeRune(line.substr(at + n)).cp);
}

// Whether before ends in a digit, which counts the word after it (3千里).
bool NumberBefore(std::string_view before) {
    const char32_t c = names::internal::RuneBefore(before, before.size()).cp;
    return (c >= U'0' && c <= U'9') || (c >= U'０' && c <= U'９');
}

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
        // A rank after a surname (滝本警部; spec LEX-4831).
        if (text::RuneCount(key) >= 2 && dict::StartsWithMember(HDTRANS_CLASS("name.rank-after"), after) > 0) return true;
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
    std::map<std::string, std::size_t> recurring, evidence;
    std::set<std::string> carried;  // the keys a carrying cue names for the document
    for (const std::string& line : lines) {
        historicalLines.push_back(names::ReadHistoricalLine(snap, line));
        for (const std::string& key : internal::PersonPlaces(snap, line, nullptr, nullptr)) persons.insert(key);
        const internal::SpeechCues speech = internal::ReadSpeechCues(line);
        female = female || speech.female;
        maleKin = maleKin || speech.maleKin;
        quoted = quoted || speech.quoted;
        for (const names::NameCue& c : CuesOf(*doc, line)) {
            // A predicate of a person in one line names the key in the other lines of the call too, unless a line
            // keeps the common sense there (doc->common below; spec LEX-4820).
            if (c.strong || c.person || c.acted) carried.insert(c.key);
            if (c.recurring) recurring[c.key]++;
            if (c.evidence) evidence[c.key]++;
        }
    }
    std::vector<const names::HistoricalLine*> historical;
    for (const names::HistoricalLine& h : historicalLines) historical.push_back(&h);
    for (const auto& [key, count] : recurring) {
        if (count >= 2) carried.insert(key);
    }
    // A common word of the dictionary (朝日 아침 해, 力 힘) that the document also writes in its common sense
    // (name.common-after: 朝日を浴びる, 力を込める) is a name of the whole document only on the cues of two
    // lines; with less, it is the name only where it stands where a person stands (names::PersonSlotAt;
    // spec LEX-5175): 朝日くん in one line makes no name of 朝日を浴びて in another. A common word with a
    // cue in one line and none of the carrying cues is the name in its person slots.
    const auto commonUse = [&](const std::string& key) {
        for (const std::string& raw : lines) {
            const std::string line = names::StripRuby(raw);
            for (std::size_t at = line.find(key); at != std::string::npos; at = line.find(key, at + key.size())) {
                if (KanjiBounded(line, at, key.size()) && names::CommonSenseAt(std::string_view(line).substr(at + key.size()))) return true;
            }
        }
        return false;
    };
    for (const std::string& key : carried) {
        const auto e = evidence.find(key);
        if (names::CommonWord(snap, key) && (e == evidence.end() || e->second < 2) && commonUse(key)) {
            doc->slotted.insert(key);
        } else {
            doc->cued.insert(key);
        }
    }
    // A common word whose cues stay in their lines (one kanji: 樹くん, 樹「うん」) is the name of the whole document
    // on the cues of two lines too, and in its person slots on one.
    // A key the dictionary has no common sense for (凪, 筧: a name or the reading of each kanji) is the name of
    // the whole document on the cue of one line: there is no word for a later line to mean (spec LEX-5940).
    for (const auto& [key, count] : evidence) {
        if (carried.contains(key)) continue;
        if (names::NameOnlyKey(snap, key)) {
            doc->cued.insert(key);
            continue;
        }
        if (!names::CommonWord(snap, key)) continue;
        if (count >= 2) {
            doc->cued.insert(key);
        } else {
            doc->slotted.insert(key);
        }
    }
    // The parts of a full name the document writes (古手梨花) name the person alone too (梨花は; spec
    // LEX-4829).
    for (const std::string& key : r.FullNameParts()) {
        if (!snap.NameRecords(key).empty()) doc->cued.insert(key);
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

    {
        // A name the registry writes a row for keeps that reading over a list line's (宗哲 소테츠, not
        // 소 테츠); a list line still reads a name the registry leaves to the words (浜姫 하마 히메).
        std::set<std::string> written;
        const std::string rows = r.Dict();
        for (std::size_t at = 0; at < rows.size();) {
            std::size_t end = rows.find('\n', at);
            if (end == std::string::npos) end = rows.size();
            const std::string_view row = std::string_view(rows).substr(at, end - at);
            if (const std::size_t tab = row.find('\t'); tab != std::string_view::npos) written.emplace(row.substr(0, tab));
            at = end + 1;
        }
        doc->historical = names::HistoricalRows(snap, historical, written);
    }
    if (!doc->historical.empty()) doc->key += std::string("\0historical\0", 12) + doc->historical;

    if (names != nullptr && !names->packs.Empty()) {
        doc->packs = &names->packs;
        const std::vector<std::string> honorificOnly = r.HonorificOnlyNames();
        for (const auto& [t, row] : names->honRows) {
            // The lines' own cues that name t (時雨は笑った) show it standing alone too, as in a document without the list.
            const bool shown = doc->cued.contains(t) || doc->slotted.contains(t);
            if (!shown && std::binary_search(honorificOnly.begin(), honorificOnly.end(), t)) continue;
            if (doc->ownPacks == nullptr) {
                doc->ownPacks = std::make_unique<dict::PackTable>(names->packs);
                doc->packs = doc->ownPacks.get();
            }
            doc->ownPacks->rows[row].atoms.clear();
        }
    }
    for (const std::string& t : r.Names()) {
        doc->accepted.insert(t);
        doc->slotted.erase(t);
        const auto e = evidence.find(t);
        // A part of a full name the document writes (神崎 真, 大見 健) or a speaker label names the person
        // there, as a list of names does.
        if (!names::CommonWord(snap, t) || (names != nullptr && names->keys.contains(t)) || r.FullNamePart(t)) continue;
        // The registry's acceptance is one piece of evidence and the cues of a line another.
        if (e == evidence.end() && commonUse(t)) doc->slotted.insert(t);
    }
    for (const std::string& t : doc->cued) doc->slotted.erase(t);
    // A name the document proves is read as a listed name is: a hiragana name no kana word of the dictionary spells,
    // which verb forms would split otherwise (つぐみらしい, 「つぐみっ」), and the kana of a kanji name it proves before an honorific
    // (筧 카케이 gives かけいさん; spec LEX-5940).
    {
        names::List learned;
        const auto personKo = [&](std::string_view k) -> std::string {
            for (const dict::Record& rec : snap.NameRecords(k)) {
                if (rec.codes != nullptr && rec.codes->sem.HasPrefix("QPN")) return std::string(rec.target);
            }
            return {};
        };
        const auto add = [&](const std::string& ja, const std::string& ko, bool hon) {
            if (ko.empty() || (names != nullptr && names->keys.contains(ja))) return;
            for (const names::Listed& l : learned.names) {
                if (l.ja == ja) return;
            }
            names::Listed l;
            l.ja = ja;
            l.ko = ko;
            l.honorificOnly = hon;
            l.line = static_cast<int>(learned.names.size()) + 1;
            l.source = std::string(names::kLearnedSource);
            learned.names.push_back(std::move(l));
        };
        std::map<std::string, std::string> spoken;  // the Korean of each proven kanji name, without spaces
        for (const std::string& k : std::set<std::string>(doc->cued.begin(), doc->cued.end())) {
            const std::string ko = personKo(k);
            if (names::internal::AllRunes(k, names::internal::IsHiragana)) {
                // Not a kana word of the dictionary (ことり 작은새), which keeps its sense outside the cue.
                const auto records = snap.RetrieveRecords(k, dict::LookupMode::General);
                if (std::none_of(records.begin(), records.end(), [&](const dict::Candidate& c) {
                        return c.codes != nullptr && c.source == k && !c.codes->sem.HasPrefix("QPN");
                    })) {
                    add(k, ko, false);
                }
            } else if (!ko.empty() && ko.find(' ') == std::string::npos) {
                spoken.emplace(ko, k);
            }
        }
        if (!spoken.empty()) {
            for (const std::string& raw : lines) {
                const std::string line = names::StripRuby(raw);
                for (const std::string_view h : dict::internal::kNameHonorifics) {
                    for (std::size_t at = line.find(h); at != std::string::npos; at = line.find(h, at + 1)) {
                        std::size_t start = at;
                        while (start > 0 && names::internal::IsHiragana(names::internal::RuneBefore(line, start).cp)) {
                            start -= names::internal::RuneBefore(line, start).size;
                        }
                        const std::string kana = line.substr(start, at - start);
                        if (names::internal::RuneCount(kana) < 2) continue;
                        // The kana spells the name with its long vowels (かけい 카케이; まおう is no 마오) and is no noun
                        // of the dictionary itself, which the honorific would address (ことりさん; かな of かなさん is a particle).
                        const auto f = spoken.find(text::FromKana(kana, text::Preset::Fan));
                        if (f == spoken.end()) continue;
                        const auto records = snap.RetrieveRecords(kana, dict::LookupMode::General);
                        if (std::none_of(records.begin(), records.end(), [&](const dict::Candidate& c) {
                                return c.codes != nullptr && c.source == kana && !c.codes->sem.HasPrefix("QPN") &&
                                       !c.codes->part.Empty() && c.codes->part.Tag() == 'N';
                            })) {
                            add(kana, f->first, true);
                        }
                    }
                }
            }
        }
        if (!learned.names.empty()) {
            if (doc->ownPacks == nullptr) {
                doc->ownPacks = names != nullptr ? std::make_unique<dict::PackTable>(names->packs) : std::make_unique<dict::PackTable>();
            }
            std::vector<dict::PackError> errors;
            dict::ParsePackRows("learned", ListPackRows(learned), &snap.Base(), doc->families, *doc->ownPacks, errors);
            doc->packs = doc->ownPacks.get();
            std::string key = std::string("\0learned", 8);
            for (const names::Listed& l : learned.names) {
                r.AddListed(l);
                key += "\t" + names::ListLine(l);
            }
            doc->registryKey += key;
            doc->key += key;
        }
    }
    for (const std::string& t : doc->cued) {
        if (names::CommonWord(snap, t) || names::WordContextKey(t)) doc->common.insert(t);
    }
    for (const std::string& t : doc->accepted) {
        if (names::CommonWord(snap, t) || names::WordContextKey(t)) doc->common.insert(t);
    }
    if (!doc->slotted.empty()) {
        doc->key += std::string("\0slotted", 8);
        for (const std::string& k : std::set<std::string>(doc->slotted.begin(), doc->slotted.end())) doc->key += "\t" + k;
    }
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

namespace {

// Whether the person rows of key are surnames only (gram Wa).
bool PersonSurname(const dict::Snapshot& snap, std::string_view key) {
    bool surname = false, given = false;
    for (const dict::Record& rec : snap.NameRecords(key)) {
        if (rec.codes == nullptr || !rec.codes->sem.HasPrefix("QPN")) continue;
        surname = surname || rec.codes->gram.HasPrefix("Wa");
        given = given || rec.codes->gram.HasPrefix("Wb");
    }
    return surname && !given;
}

// Whether the dictionaries other than the name layer have w as a common noun.
bool BaseNoun(const dict::Snapshot& snap, std::string_view w) {
    for (const dict::Candidate& c : snap.RetrieveRecords(w, dict::LookupMode::General)) {
        if (c.codes == nullptr || c.source != w || c.codes->part.Empty() || c.codes->part.Tag() != 'N') continue;
        if (c.codes->sem.HasPrefix("QPN") || c.codes->gram.HasPrefix("Wa") || c.codes->gram.HasPrefix("Wb")) continue;
        return true;
    }
    return false;
}

// Whether the katakana run t splits into two or more such nouns of two kana or more (ランチタイム, ニンジャソード):
// a compound word, not a name.
bool NounCompound(const dict::Snapshot& snap, std::string_view t) {
    if (!names::internal::AllRunes(t, names::internal::IsKatakana) || snap.NamePerson(t)) return false;
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < t.size(); i += std::max<std::size_t>(1, text::DecodeRune(t.substr(i)).size)) starts.push_back(i);
    starts.push_back(t.size());
    // parts[k]: the fewest nouns t up to starts[k] splits into; 0 for none.
    std::vector<int> parts(starts.size(), 0);
    for (std::size_t i = 0; i + 2 < starts.size(); i++) {
        if ((i > 0 && parts[i] == 0) || names::internal::InRunes(text::DecodeRune(t.substr(starts[i])).cp, "ーッァィゥェォャュョヮ")) continue;
        for (std::size_t j = i + 2; j < starts.size(); j++) {
            if (!BaseNoun(snap, t.substr(starts[i], starts[j] - starts[i]))) continue;
            const int n = (i == 0 ? 0 : parts[i]) + 1;
            if (parts[j] == 0 || n < parts[j]) parts[j] = n;
        }
    }
    return parts.back() >= 2;
}

// Whether ko is the Sino-Korean reading of the kanji of t, one syllable per kanji (学園長 학원장): a word read on
// its characters, which no Japanese name the registry writes reads as.
bool SinoReading(std::string_view t, std::string_view ko) {
    if (!names::internal::AllRunes(t, names::internal::IsKanji)) return false;
    std::string sino;
    for (std::size_t i = 0; i < t.size();) {
        const text::Rune r = text::DecodeRune(t.substr(i));
        const std::optional<char32_t> w = dict::Written(r.cp);
        if (!w) return false;
        text::AppendRune(sino, *w);
        i += std::max<std::size_t>(1, r.size);
    }
    return sino == ko;
}

// The proposed names that are no names of persons (spec LEX-8154): one with Latin letters (ＬＬ), a katakana
// name drawn out (マチルダー beside マチルダ), a compound of common katakana nouns or a chain with one
// (ランチタイム・ニュース), a kanji name read on its characters (学園長 학원장), a kanji run that is part of a
// longer word wherever it stands or that a katakana ruby glosses, a hiragana name that is another proposed one
// after a particle (はみお beside みお) or that only stands inside a longer hiragana run (やはりみおちゃん:
// りみお), a common word the cues of the document do not confirm (武士), and a full name or surname that only
// such a name holds (鎌倉武士).
void DropJunkNames(const dict::Snapshot& snap, std::span<const std::string> lines, const DocumentContext& doc,
                   std::map<std::string, names::Listed>& proposed) {
    using names::internal::AllRunes;
    using names::internal::IsHiragana;
    using names::internal::IsKatakana;
    std::vector<std::string> stripped;
    stripped.reserve(lines.size());
    for (const std::string& raw : lines) stripped.push_back(names::StripRuby(raw));
    const auto insideRun = [&](const std::string& t) {
        bool any = false;
        for (const std::string& line : stripped) {
            for (std::size_t at = line.find(t); at != std::string::npos; at = line.find(t, at + t.size())) {
                any = true;
                if (at == 0 || !IsHiragana(names::internal::RuneBefore(line, at).cp)) return false;
            }
        }
        return any;
    };
    // A kanji run whose every use goes on into a longer word of the dictionaries (瑞々しい, 項垂れる), and one
    // that every ruby over it reads in katakana (指令車《コマンドカー》), a term glossed.
    const auto wordPart = [&](const std::string& t) {
        bool stem = false;  // a verb or adjective stem of the dictionaries, which kana after it inflect (行く)
        for (const dict::Candidate& c : snap.RetrieveRecords(t, dict::LookupMode::General)) {
            stem = stem || (c.codes != nullptr && c.source == t && !c.codes->part.Empty() && c.codes->part.TagIn("MH"));
        }
        bool any = false;
        for (const std::string& line : stripped) {
            for (std::size_t at = line.find(t); at != std::string::npos; at = line.find(t, at + t.size())) {
                // Inside a longer kanji run or counting after a digit it is no use of its own (一行, １行).
                if (!KanjiBounded(line, at, t.size()) || NumberBefore(std::string_view(line).substr(0, at))) continue;
                any = true;
                // A word that goes on into an honorific's kana is a verb (行くんだ), no call.
                const std::string_view rest = std::string_view(line).substr(at);
                const char32_t next = at + t.size() < line.size() ? text::DecodeRune(rest.substr(t.size())).cp : 0;
                const bool inflected = stem && names::internal::IsHiragana(next) && !names::internal::InRunes(next, "のはがをにともへやで");
                if (snap.WordAt(rest) <= t.size() && !inflected) return false;
            }
        }
        return any;
    };
    const auto glossed = [&](const std::string& t) {
        bool any = false;
        for (const std::string& raw : lines) {
            names::internal::RubyMatch m;
            for (std::size_t from = 0; names::internal::FindRuby(raw, from, m); from = m.end) {
                if (m.base != t) continue;
                if (!AllRunes(m.reading, IsKatakana)) return false;
                any = true;
            }
        }
        return any;
    };
    // A kanji run that stands only before a common kanji word names a school, a company or a work there (文秀大学,
    // 久慈ヶ嶺学園), no person.
    const auto placeName = [&](const std::string& t) {
        bool any = false;
        for (const std::string& line : stripped) {
            for (std::size_t at = line.find(t); at != std::string::npos; at = line.find(t, at + t.size())) {
                std::size_t end = at + t.size();
                // Past the reading of brace ruby ({rb}文秀{/rb}{rt}ぶんしゅう{/rt}大学).
                if (std::string_view(line).substr(end).starts_with("{/rb}{rt}")) {
                    if (const std::size_t close = line.find("{/rt}", end); close != std::string::npos) end = close + 5;
                }
                std::size_t run = end;
                while (run < line.size() && (names::internal::IsKanji(text::DecodeRune(std::string_view(line).substr(run)).cp))) {
                    run += std::max<std::size_t>(1, text::DecodeRune(std::string_view(line).substr(run)).size);
                }
                const std::string_view next = std::string_view(line).substr(end);
                // A title or honorific after it is a person's (白崎先生, 滝本警部).
                const bool titled = std::any_of(dict::internal::kNameHonorifics.begin(), dict::internal::kNameHonorifics.end(),
                                                [&](std::string_view h) { return next.starts_with(h); }) ||
                                    dict::StartsWithMember(HDTRANS_CLASS("honorific.person-title"), next) > 0 ||
                                    dict::StartsWithMember(HDTRANS_CLASS("name.rank-after"), next) > 0;
                // A given name after it makes a full name (多岐川葵).
                const std::string word = line.substr(end, run - end);
                if (run == end || titled || !BaseNoun(snap, word) || snap.NamePerson(word) || proposed.contains(t + word)) return false;
                any = true;
            }
        }
        return any;
    };
    const auto latin = [](std::string_view t) {
        return !AllRunes(t, [](char32_t c) {
            return !((c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || (c >= U'Ａ' && c <= U'Ｚ') || (c >= U'ａ' && c <= U'ｚ'));
        });
    };
    std::set<std::string> drop;
    for (const auto& [t, l] : proposed) {
        if (l.kind == names::ListKind::Full) continue;
        bool junk = latin(t);
        if (AllRunes(t, IsKatakana)) {
            std::size_t end = t.size();
            while (end > 0) {
                const text::Rune r = names::internal::RuneBefore(t, end);
                if (r.cp != U'ー' && !names::internal::InRunes(r.cp, "ッァィゥェォャュョヮ")) break;
                end -= r.size;
            }
            junk = junk || (end < t.size() && end > 0 && proposed.contains(t.substr(0, end))) || NounCompound(snap, t);
        } else if (AllRunes(t, IsHiragana)) {
            for (const auto& [y, ly] : proposed) {
                if (y.size() >= t.size() || !t.ends_with(y) || !AllRunes(y, IsHiragana) || text::RuneCount(y) < 2) continue;
                const std::string_view head = std::string_view(t).substr(0, t.size() - y.size());
                if ((text::RuneCount(head) == 1 && names::internal::InRunes(text::DecodeRune(head).cp, "はがもをにへとやの")) ||
                    insideRun(t)) {
                    junk = true;
                }
            }
        } else if (AllRunes(t, names::internal::IsKanji)) {
            // A title read on its characters (学園長 학원장, 大先生 대선생), not a name read so (a Korean name in news).
            const bool title = dict::EndsWithMember(HDTRANS_CLASS("suffix.label-role"), t) > 0 ||
                               dict::EndsWithMember(HDTRANS_CLASS("honorific.person-title"), t) > 0;
            junk = junk || (title && SinoReading(t, l.ko)) || wordPart(t) || glossed(t) || placeName(t);
        }
        if (junk) {
            drop.insert(t);
        } else if (AllRunes(t, names::internal::IsKanji) && names::CommonWord(snap, t) && !doc.cued.contains(t)) {
            // A common word the cues of the document do not confirm is a name only where a line shows it (武士).
            proposed.at(t).honorificOnly = true;
        }
    }
    // A chain or full name with a dropped part, or a chain with a compound of common nouns, goes too; then a part
    // that only such names held.
    std::set<std::string> fullParts, keptParts;
    for (const auto& [t, l] : proposed) {
        if (l.kind != names::ListKind::Full) continue;
        const std::vector<std::string> parts = names::internal::LabelParts(t);
        bool junk = false;
        for (const std::string& p : parts) junk = junk || drop.contains(p) || NounCompound(snap, p);
        // A full name of kanji holds its parts unseparated (鎌倉武士: 武士).
        for (const std::string& d : drop) junk = junk || (d.size() < t.size() && (t.ends_with(d) || t.starts_with(d)));
        for (const std::string& p : parts) (junk ? fullParts : keptParts).insert(p);
        if (junk) drop.insert(t);
    }
    for (const auto& [t, l] : proposed) {
        // A full name of kanji holds its parts unseparated (鎌倉武士: 鎌倉).
        for (const auto& [f, lf] : proposed) {
            if (lf.kind != names::ListKind::Full || f == t || !f.starts_with(t)) continue;
            (drop.contains(f) ? fullParts : keptParts).insert(t);
        }
    }
    // A part stands alone where it is written outside every dropped full name.
    const auto alone = [&](const std::string& p) {
        for (const std::string& line : stripped) {
            for (std::size_t at = line.find(p); at != std::string::npos; at = line.find(p, at + p.size())) {
                bool inside = false;
                for (const std::string& f : drop) {
                    if (f.size() <= p.size() || !proposed.contains(f) || proposed.at(f).kind != names::ListKind::Full) continue;
                    for (std::size_t from = at >= f.size() ? at - f.size() + p.size() : 0; !inside;) {
                        const std::size_t fa = line.find(f, from);
                        if (fa == std::string::npos || fa > at) break;
                        inside = fa + f.size() >= at + p.size();
                        from = fa + 1;
                    }
                    if (inside) break;
                }
                if (!inside) return true;
            }
        }
        return false;
    };
    for (const std::string& p : fullParts) {
        if (!keptParts.contains(p) && proposed.contains(p) && proposed.at(p).kind != names::ListKind::Full && !alone(p)) {
            drop.insert(p);
        }
    }
    for (const std::string& t : drop) proposed.erase(t);
}

}  // namespace

std::string ProposeNames(const dict::Snapshot& snap, std::span<const std::string> lines, const DocumentNames* names) {
    const std::unique_ptr<DocumentContext> doc = BuildDocument(snap, lines, false, names);
    std::map<std::string, names::Listed> proposed;
    for (names::Listed& l : doc->registry->Proposed()) proposed.emplace(l.ja, std::move(l));
    // The persons of the dictionaries the cues of the document confirm (DocumentContext::cued), which a line
    // alone may read as a word or by the readings of its kanji (宵 소): their person row. One that is also a
    // common noun (力 힘) is a name only where a line shows it to be one (hon).
    for (const std::string& k : doc->cued) {
        if (proposed.contains(k) || (names != nullptr && names->keys.contains(k))) continue;
        std::string ko = doc->registry->PersonReading(k);
        if (ko.empty() || !snap.NamePerson(k)) continue;
        names::Listed l;
        l.ja = k;
        l.ko = std::move(ko);
        l.kind = PersonSurname(snap, k) ? names::ListKind::Family : names::ListKind::Given;
        l.honorificOnly = names::CommonWord(snap, k);
        proposed.emplace(k, std::move(l));
    }
    // One kanji of the name layer that the cues of two lines or more name (宵「おはよう」, 宵君): its cues hold in
    // their line only (spec LEX-4832), so the document names it nowhere else, but the list carries it to the lines
    // of a hooked text, where it is a name before an honorific or where the line shows it (hon).
    {
        std::map<std::string, std::size_t> single;
        for (const std::string& line : lines) {
            std::set<std::string> seen;
            for (const names::NameCue& c : CuesOf(*doc, line)) {
                if (text::RuneCount(c.key) == 1 && names::internal::AllRunes(c.key, names::internal::IsKanji)) seen.insert(c.key);
            }
            for (const std::string& k : seen) single[k]++;
        }
        for (const auto& [k, count] : single) {
            if (count < 2 || proposed.contains(k) || (names != nullptr && names->keys.contains(k)) || !snap.NamePerson(k)) continue;
            std::string ko = doc->registry->PersonReading(k);
            if (ko.empty()) continue;
            names::Listed l;
            l.ja = k;
            l.ko = std::move(ko);
            l.kind = PersonSurname(snap, k) ? names::ListKind::Family : names::ListKind::Given;
            l.honorificOnly = !names::NameOnlyKey(snap, k);
            proposed.emplace(k, std::move(l));
        }
    }
    DropJunkNames(snap, lines, *doc, proposed);
    std::string out;
    for (const auto& [ja, l] : proposed) out += names::ListLine(l) + "\n";
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
    facts.text = r.ReadKanaLabels(facts.text);
    facts.text = r.ReadBoxNames(facts.text);
    const std::string_view cues = cueText.empty() ? std::string_view(facts.text) : std::string_view(cueText);
    dict::KeySet plain, guarded;
    std::vector<std::pair<std::size_t, std::size_t>> held;  // the spans of the line's name occurrences read as names
    for (const names::Occurrence& o : r.Occurrences(facts.text)) {
        if (o.noun) {
            guarded.insert(o.surface);
        } else {
            plain.insert(o.surface);
            held.emplace_back(o.start, o.end);
        }
    }
    // A kana name is the name at the occurrences that hold, not elsewhere in the line: the verb or word it
    // also spells keeps its reading there (ひかるちゃん、星がひかるよ 별이 빛나; spec LEX-6300).
    std::vector<std::string> mixed;
    for (const std::string& t : plain) {
        if (!names::internal::AllRunes(t, names::internal::IsHiragana)) continue;
        for (std::size_t at = facts.text.find(t); at != std::string::npos; at = facts.text.find(t, at + t.size())) {
            const bool inside = std::any_of(held.begin(), held.end(), [&](const auto& h) { return h.first <= at && at + t.size() <= h.second; });
            if (!inside) {
                mixed.push_back(t);
                break;
            }
        }
    }
    // A name only before an honorific takes part where an honorific follows it in the line.
    // Not one proven where a bare name stands (LayerKanaNames): it holds as other proven names do.
    for (const std::string& t : r.HonorificOnlyNames()) {
        if (plain.count(t) == 0 && !r.LayerSlotName(t)) guarded.insert(t);
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
    // A common word the document names on too little evidence is the name where it stands where a person
    // stands in the line, and the word elsewhere (spec LEX-5175).
    for (const std::string& k : doc.slotted) {
        bool any = false, person = false;
        for (std::size_t at = cues.find(k); at != std::string_view::npos; at = cues.find(k, at + k.size())) {
            if (!KanjiBounded(cues, at, k.size())) continue;
            any = true;
            person = person || names::PersonSlotAt(*doc.snap, k, cues.substr(0, at), cues.substr(at + k.size()));
        }
        // A reading written over it in the line names it there (佑助(ゆうすけ), 石垣（いしがき）; not 魔力（まりょく）).
        for (const std::string_view open : {"(", "（", "《"}) {
            const std::string marked = k + std::string(open);
            for (std::size_t at = line.find(marked); at != std::string_view::npos && !person; at = line.find(marked, at + 1)) {
                person = at == 0 || !names::internal::IsKanji(names::internal::RuneBefore(line, at).cp);
            }
        }
        if (!any) continue;
        if (person) {
            facts.cued.push_back(k);
        } else {
            facts.skip.push_back(k);
        }
    }
    // A common word the document names stays the word in a line that writes it only as the field of a status
    // window, before name.common-after or before a row of name.common-word-context for it (力：35, 朝日を浴びて,
    // 植木に水をやる; spec LEX-7550); a line that also names it keeps the name.
    for (const std::string& k : doc.common) {
        bool common = false, other = false;
        for (std::size_t at = cues.find(k); at != std::string_view::npos; at = cues.find(k, at + k.size())) {
            if (!KanjiBounded(cues, at, k.size())) continue;
            // A name of the whole document gives way where the line keeps the common sense (names::CommonSenseAt:
            // 朝日を浴びて, 樹は大きく揺れた; 朝日は背が高い is the person, its own subject being 背).
            const std::string_view after = cues.substr(at + k.size());
            // An adnominal before it makes it the word unless a person act or call after it makes it the person
            // (大きな樹を見上げた; 大きな樹くん).
            // A number before it counts the word (3千里); a predicate of a plant keeps a plant (岩に玉藻がからみつく).
            if (names::CommonSenseAt(after) || names::WordContextAt(k, cues.substr(at)) || NumberBefore(cues.substr(0, at)) ||
                names::PlantSenseAt(*doc.snap, k, after) ||
                (names::WordBeforeAt(cues.substr(0, at)) && !names::PersonSlotAt(*doc.snap, k, cues.substr(0, at), after))) {
                common = true;
            } else {
                other = true;
            }
        }
        if (!common || other) continue;
        facts.skip.push_back(k);
        facts.uncued.push_back(k);
    }
    // Such a name is written as the name at those occurrences, before an honorific too, and is no name of the
    // line elsewhere: its entry and the line's cues of it stay out (【つき】「つきが出てるよ」, のぞみさん、
    // 最後ののぞみだ 노조미 씨, 마지막 희망이다).
    std::map<std::string, std::string> spelled;
    if (!facts.wholeLine) {
        for (const std::string& t : mixed) {
            const auto e = std::find_if(doc.extra.begin(), doc.extra.end(), [&](const dict::UserEntry& u) {
                return u.key == t && !u.honorific && names::internal::IsHangulText(u.target);
            });
            if (e == doc.extra.end()) continue;
            spelled.emplace(t, e->target);
            facts.skip.push_back(t);
            facts.uncued.push_back(t);
        }
    }
    std::sort(facts.skip.begin(), facts.skip.end());
    facts.skip.erase(std::unique(facts.skip.begin(), facts.skip.end()), facts.skip.end());
    // Nor is a skipped word a person of its place spelling there (朝日 아사히 as a place).
    std::erase_if(facts.persons, [&](const std::string& t) { return std::binary_search(facts.skip.begin(), facts.skip.end(), t); });
    std::sort(facts.cued.begin(), facts.cued.end());
    facts.cued.erase(std::unique(facts.cued.begin(), facts.cued.end()), facts.cued.end());
    if (!spelled.empty()) {
        std::sort(held.begin(), held.end());
        std::string out;
        std::size_t at = 0;
        for (const auto& [a, b] : held) {
            if (a < at) continue;
            const auto w = spelled.find(facts.text.substr(a, b - a));
            if (w == spelled.end()) continue;
            out.append(facts.text, at, a - at);
            out.append(w->second);
            at = b;
        }
        facts.text = out + facts.text.substr(at);
    }
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
        // A kana name given before an honorific that is also a word of the dictionary (のぼる, はるか) holds where
        // the line does not skip it: where an honorific follows it or it stands bare where a name stands
        // (Registry::BareNameSlot; spec LEX-6300).
        const std::vector<std::string> honorificOnly = doc.registry->HonorificOnlyNames();
        const auto hon = [&](const std::string& k) {
            return names::internal::AllRunes(k, names::internal::IsHiragana) &&
                   std::binary_search(honorificOnly.begin(), honorificOnly.end(), k);
        };
        for (const dict::UserEntry& e : doc.extra) {
            if ((e.file == kListedFile || !doc.data->dictKeys.contains(e.key) || hon(e.key)) &&
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
    for (const std::string& t : facts.uncued) out.cued.erase(t);
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
    ctx.lookup.named = &doc.named;
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
