#include "core/names/document.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/names/chars.h"
#include "core/names/historical.h"
#include "core/names/labels.h"
#include "core/names/readings.h"
#include "core/names/registry.h"
#include "core/pipeline/normalize.h"
#include "core/dict/builtin.h"
#include "core/dict/char_readings.h"
#include "core/text/utf8.h"
#include "core/text/lines.h"
#include "core/userdict/userdict.h"

namespace hdTrans::pipeline {

namespace {

// The titles that show a name to be a person when they follow it (霧島さん, 千歳お姉, 加賀改二).
constexpr std::string_view kPersonTitles[] = {"さん", "ちゃん", "くん", "君", "様", "さま", "先輩", "先生", "殿", "氏",
                                                     "たん", "っち", "姉", "兄", "お姉", "お兄", "改"};

// Whether key occurs in text with a person title after it or 艦 before it.
bool Titled(std::string_view text, std::string_view key) {
    for (std::size_t i = 0;;) {
        const std::size_t at = text.find(key, i);
        if (at == std::string_view::npos) return false;
        std::string_view before = text.substr(0, at);
        if (before.ends_with("、")) before.remove_suffix(std::string_view("、").size());
        const std::string_view after = text.substr(at + key.size());
        if (before.ends_with("艦")) return true;
        for (const std::string_view t : kPersonTitles) {
            if (after.starts_with(t)) return true;
        }
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

struct DocumentHistory {
    static constexpr std::size_t kLines = 2048;
    static constexpr std::size_t kBytes = 1024 * 1024;
    struct Memo {
        names::HistoricalLine historical;
        std::vector<std::string> persons;
        internal::SpeechCues speech;
        std::size_t uses = 0;
    };
    struct Line {
        std::string text;
        names::Registry::Evidence evidence;
        bool verse = false;
    };
    explicit DocumentHistory(const dict::Snapshot& snapshot)
        : snap(&snapshot), registry(snapshot.DocumentData().registry) {
        registry.CacheDictionaryFacts();
    }

    Memo& Facts(std::string_view line) {
        auto [it, inserted] = memo.try_emplace(std::string(line));
        if (inserted) {
            it->second.historical = names::ReadHistoricalLine(*snap, line);
            it->second.persons = internal::PersonPlaces(*snap, line, nullptr, nullptr);
            it->second.speech = internal::ReadSpeechCues(line);
        }
        return it->second;
    }

    // Longer runs win; among equal runs the earliest retained line wins.
    using Rank = std::pair<std::size_t, uint64_t>;
    using Readings = std::map<Rank, std::string, std::greater<Rank>>;
    void Historical(const Memo& facts, uint64_t id, bool add) {
        for (const auto& [key, reading] : facts.historical.found) {
            const Rank rank{reading.run, std::numeric_limits<uint64_t>::max() - id};
            if (add) historical[key][rank] = reading.text;
            else {
                auto it = historical.find(key);
                it->second.erase(rank);
                if (it->second.empty()) historical.erase(it);
            }
        }
        for (const auto& surname : facts.historical.surnames) Count(surnames, surname, add);
        if (!facts.historical.listed.empty()) {
            if (add) listed[id] = facts.historical.listed;
            else listed.erase(id);
        }
    }

    names::HistoricalLine Historical() const {
        names::HistoricalLine out;
        for (const auto& [key, readings] : historical) {
            const auto& [rank, value] = *readings.begin();
            out.found[key] = {value, rank.first};
        }
        for (const auto& [key, count] : surnames) out.surnames.insert(key);
        for (const auto& [id, runs] : listed) out.listed.insert(out.listed.end(), runs.begin(), runs.end());
        return out;
    }

    static void Count(std::map<std::string, std::size_t>& counts, const std::string& key, bool add) {
        if (add) counts[key]++;
        else {
            auto it = counts.find(key);
            if (--it->second == 0) counts.erase(it);
        }
    }
    static std::string VerseKey(std::string_view line) {
        if (line.ends_with('\r')) line.remove_suffix(1);
        return std::string(line);
    }
    static bool Continues(std::string_view line, std::string_view next) {
        line = names::internal::TrimSpace(line);
        return (line.ends_with("て") || line.ends_with("で")) && !names::internal::TrimSpace(next).empty();
    }
    void Remember(std::string_view text) {
        if (!lines.empty() && Continues(lines.back().text, text)) {
            lines.back().verse = true;
            Count(verses, VerseKey(lines.back().text), true);
        }
        Line line{std::string(text), {}, false};
        registry.Add(text, &line.evidence);
        // Comment-only lines still need a distinct eviction id.
        const uint64_t id = next++;
        ids.push_back(id);
        Memo& facts = Facts(text);
        facts.uses++;
        female += facts.speech.female;
        maleKin += facts.speech.maleKin;
        for (const auto& key : facts.persons) Count(persons, key, true);
        Historical(facts, id, true);
        bytes += text.size();
        lines.push_back(std::move(line));
        while (lines.size() > kLines || bytes > kBytes) {
            const Line& old = lines.front();
            auto found = memo.find(old.text);
            Memo& oldFacts = found->second;
            registry.Forget(old.evidence);
            female -= oldFacts.speech.female;
            maleKin -= oldFacts.speech.maleKin;
            for (const auto& key : oldFacts.persons) Count(persons, key, false);
            if (old.verse) Count(verses, VerseKey(old.text), false);
            Historical(oldFacts, ids.front(), false);
            if (--oldFacts.uses == 0) memo.erase(found);
            bytes -= old.text.size();
            lines.pop_front();
            ids.pop_front();
        }
    }

    const dict::Snapshot* snap;
    names::Registry registry;
    std::deque<Line> lines;
    std::deque<uint64_t> ids;
    std::map<std::string, Memo, std::less<>> memo;
    std::map<std::string, std::size_t> persons, verses, surnames;
    std::map<std::string, Readings> historical;
    std::map<uint64_t, std::vector<std::vector<char32_t>>> listed;
    std::size_t bytes = 0, female = 0, maleKin = 0;
    uint64_t next = 0;
};

namespace {

DocumentHistory& HistoryOf(const dict::Snapshot& snap, std::shared_ptr<DocumentHistory>& history) {
    if (!history) history = std::make_shared<DocumentHistory>(snap);
    else if (history->snap != &snap) {
        auto rebuilt = std::make_shared<DocumentHistory>(snap);
        for (const auto& line : history->lines) rebuilt->Remember(line.text);
        history = std::move(rebuilt);
    }
    return *history;
}

}  // namespace

void RememberDocument(const dict::Snapshot& snap, std::span<const std::string> lines,
                      std::shared_ptr<DocumentHistory>& history) {
    DocumentHistory& retained = HistoryOf(snap, history);
    for (const std::string& line : lines) retained.Remember(line);
}

namespace internal {

std::set<std::string> VerseLines(std::span<const std::string> lines) {
    std::set<std::string> verse;
    for (std::size_t i = 0; i + 1 < lines.size(); i++) {
        const std::string_view t = names::internal::TrimSpace(lines[i]);
        if ((t.ends_with("て") || t.ends_with("で")) && !names::internal::TrimSpace(lines[i + 1]).empty()) {
            std::string_view line = lines[i];
            if (line.ends_with('\r')) line.remove_suffix(1);
            verse.insert(std::string(line));
        }
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

std::unique_ptr<DocumentContext> BuildDocument(const dict::Snapshot& snap, std::span<const std::string> lines,
                                               bool extraBuiltin, std::shared_ptr<DocumentHistory>* history) {
    std::shared_ptr<DocumentHistory> local;
    auto& retained = history != nullptr ? *history : local;
    DocumentHistory& memory = HistoryOf(snap, retained);
    auto doc = std::make_unique<DocumentContext>();
    doc->snap = &snap;
    doc->extraBuiltin = extraBuiltin;
    doc->data = &snap.DocumentData();
    doc->codes = std::make_unique<dict::CodeTable>();
    doc->registry = std::make_unique<names::Registry>(memory.registry);
    names::Registry& r = *doc->registry;
    for (const std::string& line : lines) r.Add(line);

    doc->registryKey = r.Key();
    bool female = memory.female > 0, maleKin = memory.maleKin > 0;
    std::vector<const names::HistoricalLine*> historical;
    const names::HistoricalLine earlier = memory.Historical();
    historical.push_back(&earlier);
    dict::KeySet persons;
    for (const auto& [key, count] : memory.persons) persons.insert(key);
    for (const std::string& line : lines) {
        const DocumentHistory::Memo& facts = memory.Facts(line);
        female = female || facts.speech.female;
        maleKin = maleKin || facts.speech.maleKin;
        persons.insert(facts.persons.begin(), facts.persons.end());
        historical.push_back(&facts.historical);
    }
    doc->female = female && !maleKin;
    doc->key = doc->registryKey;
    if (doc->female) doc->key += std::string("\0female", 7);

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

    doc->verseLines = internal::VerseLines(lines);
    for (const auto& [line, count] : memory.verses) doc->verseLines.insert(line);
    if (!memory.lines.empty() && !lines.empty() && DocumentHistory::Continues(memory.lines.back().text, lines.front())) {
        doc->verseLines.insert(DocumentHistory::VerseKey(memory.lines.back().text));
    }
    doc->verse.assign(doc->verseLines.begin(), doc->verseLines.end());
    for (const std::string& line : doc->verse) doc->key += std::string("\0verse\0", 7) + line;

    doc->historical = names::HistoricalRows(snap, historical);
    if (!doc->historical.empty()) doc->key += std::string("\0historical\0", 12) + doc->historical;

    for (const std::string& t : r.Names()) doc->accepted.insert(t);
    doc->named = doc->accepted;
    for (const std::string& t : r.NamedSurfaces()) doc->named.insert(t);

    const std::string registryDict = r.Dict();
    userdict::Context ctx{doc->codes.get(), &doc->families, &snap.Base()};
    doc->extra = userdict::Parse(registryDict, -1, ctx).entries;
    for (dict::UserEntry& e : userdict::Parse(doc->historical, -1, ctx).entries) doc->extra.push_back(std::move(e));
    FinishCodes(*doc->codes);
    return doc;
}

namespace {

std::string Utf8Of(char32_t c) {
    std::string out;
    text::AppendRune(out, c);
    return out;
}

// A line that is one name and nothing else, a speaker line before a quote: a run
// of kanji with a person reading, read as that person (筧 카케이).
std::optional<std::string> NameLine(const dict::Snapshot& snap, std::string_view line) {
    const std::string_view t = names::internal::TrimLineSpace(line);
    const std::size_t runes = names::internal::RuneCount(t);
    if (runes == 0 || runes > 6) return std::nullopt;
    if (!names::internal::AllRunes(t, names::internal::IsKanji)) return std::nullopt;
    std::string surname, given;  // the first person readings of each kind
    bool word = false;           // a noun of its own beyond the kanji's Sino-Korean reading
    auto person = [&](const dict::Codes* codes, std::string_view target) {
        if (codes == nullptr || !codes->sem.Text().starts_with("QPN") || !names::internal::IsHangulText(target)) return;
        std::string& slot = codes->gram.Text().starts_with("Wa") ? surname : given;
        if (slot.empty()) slot = std::string(target);
    };
    auto noun = [&](const dict::Codes* codes, std::string_view target) {
        if (codes == nullptr || codes->sem.Text().starts_with("QPN") || codes->sem.Text().starts_with("QCT")) return;
        const char tag = codes->partText.empty() ? 0 : codes->partText[0];
        if (tag != 'N' && tag != 'I') return;
        if (runes == 1) {
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
    for (const dict::UserEntry* e : snap.TextPlain().Search(t, dict::LookupMode::General)) {
        if (e->key == t && !e->nameLayer) noun(e->codes, e->target);
    }
    // A surname of two kanji or more names the speaker though it is a word too (高峰
    // 타카미네); a given name or a single kanji only where it is no word (朔夜, 慶; 未来,
    // 輪廻 and 愛 stay words).
    std::string name;
    if (!surname.empty() && (runes > 1 || !word)) name = surname;
    else if (!given.empty() && !word) name = given;
    if (name.empty()) return std::nullopt;
    const std::size_t at = line.find(t);
    return std::string(line.substr(0, at)) + name + std::string(line.substr(at + t.size()));
}

}  // namespace

LineFacts ReadLine(DocumentContext& doc, std::string_view line) {
    LineFacts facts;
    if (line.ends_with('\r')) line.remove_suffix(1);
    facts.text = names::StripRuby(pipeline::internal::WriteKinTerms(line, doc.female));
    names::Registry& r = *doc.registry;
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
    facts.persons = internal::PersonPlaces(*doc.snap, facts.text, doc.registry.get(), &doc.persons_);
    std::sort(facts.persons.begin(), facts.persons.end());
    facts.wholeLine = WholeLineOf(doc.data->wholeLines, facts.text);
    if (!facts.wholeLine) facts.wholeLine = NameLine(*doc.snap, facts.text);
    facts.verse = doc.verseLines.count(std::string(line)) > 0;
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
            if (!doc.data->dictKeys.contains(e.key) &&
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
    return out;
}

LineContext ContextOf(const DocumentContext& doc, const LineLookup& lookup) {
    LineContext ctx;
    ctx.document = true;
    ctx.female = doc.female;
    ctx.extraBuiltin = doc.extraBuiltin;
    ctx.lookup.plain = lookup.plain;
    ctx.lookup.confirmed = lookup.confirmed;
    ctx.lookup.persons = &lookup.persons;
    ctx.named = &doc.named;
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
