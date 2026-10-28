// Given names of one kanji and a 郎 core (凶一郎 쿄이치로) and their kana spellings, read as one
// name in the lookup of the word stage.
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/names/given.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"
#include "core/word/kana.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

bool PersonOf(const dict::Candidate& c) { return c.codes != nullptr && c.codes->sem.HasCode("QPN"); }

// A given-name record: a person with gram Wb and Hangul without a space.
bool GivenRecord(const dict::Candidate& c) {
    return PersonOf(c) && c.codes->gram.Contains("Wb") && text::AllSyllables(c.target);
}

// Whether a name can end where rest starts: at the end of the text, before a character of
// another class than last's, or before an honorific.
bool NameEndsAt(std::string_view rest, int last) {
    const std::string_view next = FirstChar(rest);
    if (next.empty() || NameClassOf(next) != last) return true;
    return dict::StartsWithMember(HDTRANS_CLASS("honorific.person-title"), rest) > 0 ||
           (last == kClassHiragana && dict::StartsWithMember(HDTRANS_CLASS("particle.after-name"), rest) > 0);
}

}  // namespace

// The person rows of the dictionaries whose key ends in a 郎 core (恭一郎, 佐竹宗一郎) written
// without their long vowels ou and uu, as LEX-29 writes names; the oo of a dictionary row stays
// (쿄우이치로우 쿄이치로, 오오시마 사부로우 오오시마 사부로).
void Worker::ShortenRoNames(Record* records) {
    for (Record* r = records; r != nullptr; r = r->child) {
        if (!r->partCode.starts_with("N") || !Has(r->semantic, "QPN")) continue;
        const bool ro = std::any_of(names::RoCores().begin(), names::RoCores().end(),
                                    [&](const names::RoCore& c) { return r->source.ends_with(c.kanji); });
        if (ro) r->target = text::WithoutNameLongVowels(r->target, false);
    }
}

// Text path, mode 0. A run that starts at query and is one kanji and a 郎 core (凶一郎), or a
// katakana or hiragana spelling of such a name (キョウイチロウ, きょういちろう), is one given name:
// where the dictionaries lack it, a name made from its reading (names::RoGivenName; katakana
// with its long vowels, as LEX-29 writes katakana names) joins the candidates;
// where they read it both as a given name and as a surname with a given name (響一郎 쿄이치로,
// 히비키 이치로), the full-name reading leaves.
void Worker::ReadGivenNames(dict::Candidates& found, std::string_view query) {
    const std::string_view surface = transformSurface;
    if (!surface.ends_with(query) || current < 0) return;
    // A token joined across a space the text leaves out (冠 二郎) is a name the dictionary
    // reads in parts.
    const tokenizer::Token& token = tokens[static_cast<std::size_t>(current)];
    if (token.sourceEnd - token.sourceBegin != tokens.Raw(static_cast<std::size_t>(current)).size()) return;
    const std::string_view head = surface.substr(0, surface.size() - query.size());
    std::string_view before = LastChar(head);
    if (head.empty() && current > 0) before = LastChar(Tok(current - 1).text);
    const int cls = NameClassOf(FirstChar(query));
    if (cls == kClassOther || NameClassOf(before) == cls) return;
    std::string_view key;
    std::string target;
    const names::RoCore* core = nullptr;
    if (cls == kClassKanji) {
        const std::string_view kanji = FirstChar(query);
        const std::string_view rest = query.substr(kanji.size());
        for (const names::RoCore& c : names::RoCores()) {
            if (rest.starts_with(c.kanji) && NameEndsAt(rest.substr(c.kanji.size()), kClassKanji)) core = &c;
        }
        if (core == nullptr) return;
        key = query.substr(0, kanji.size() + core->kanji.size());
    } else {
        // The kana before the core: one to three characters (きょう, ジュン).
        for (std::size_t p = FirstChar(query).size(), n = 1; p < query.size() && n <= 3 && core == nullptr; n++) {
            const std::string_view rest = query.substr(p);
            for (const names::RoCore& c : names::RoCores()) {
                const std::string_view spelled = cls == kClassKatakana ? c.katakana : c.hiragana;
                if (!spelled.empty() && rest.starts_with(spelled) && NameEndsAt(rest.substr(spelled.size()), cls)) {
                    core = &c;
                    key = query.substr(0, p + spelled.size());
                    break;
                }
            }
            const std::string_view next = FirstChar(rest);
            if (NameClassOf(next) != cls) break;
            p += next.size();
        }
        if (core == nullptr) return;
    }
    std::vector<dict::Candidate>& list = found.list;
    // A dictionary word that goes on past the name, or one that is the name but no person
    // (三太郎 멍청이), reads the text.
    bool given = false, whole = false;
    for (const dict::Candidate& c : list) {
        if (c.source.size() > key.size() && c.codes != nullptr) return;
        if (c.source != key) continue;
        whole = true;
        given = given || GivenRecord(c);
        if (!PersonOf(c)) return;
    }
    if (whole) {
        if (!given || cls != kClassKanji) return;
        const auto fullName = [&](const dict::Candidate& c) {
            return c.source == key && PersonOf(c) && c.target.find(' ') != std::string_view::npos;
        };
        const auto removed = std::remove_if(list.begin(), list.end(), fullName);
        found.count -= static_cast<int>(list.end() - removed);
        list.erase(removed, list.end());
        return;
    }
    if (cls == kClassKanji) {
        target = names::RoGivenName(snapshot, FirstChar(query), *core);
    } else if (cls == kClassKatakana) {
        // The name before the core as katakana names are written, the core by its sound
        // (ジュンイチロウ 준이치로우, not 쥰위치로우).
        const std::string_view spelled = core->katakana;
        std::string first;
        if (!TransliterateName(key.substr(0, key.size() - spelled.size()), first) || first.empty()) return;
        target = first + text::FromKana(spelled, text::Preset::Fan);
    } else {
        target = text::FromKana(key, text::Preset::FanShort, true);
    }
    if (target.empty()) return;
    // The codes of a given name of the name layer (一郎).
    const dict::Codes* codes = nullptr;
    for (const names::RoCore& c : names::RoCores()) {
        for (const dict::Record& r : snapshot.NameRecords(c.kanji)) {
            if (codes == nullptr && r.codes != nullptr && r.codes->sem.HasPrefix("QPN") && r.codes->gram.Contains("Wb")) {
                codes = r.codes->asPlain;
            }
        }
        if (codes != nullptr) break;
    }
    if (codes == nullptr) return;
    if (arena.namesUsed == arena.names.size()) arena.names.emplace_back();
    std::vector<dict::UserEntry>& kept = arena.names[arena.namesUsed++];
    kept.clear();
    dict::UserEntry& e = kept.emplace_back();
    e.key.assign(key);
    e.target = std::move(target);
    e.codes = codes;
    e.file = -2;
    e.name = true;
    e.nameLayer = true;
    dict::Candidate name;
    name.source = e.key;
    name.target = e.target;
    name.codes = e.codes;
    name.user = &e;
    list.insert(list.begin(), name);
    found.count++;
}

}  // namespace hdTrans::word
