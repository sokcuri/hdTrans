#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/dict/builtin.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/sound.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline::internal {

namespace {

using Lexicon = std::unordered_map<char32_t, std::vector<SoundRow>>;

std::vector<std::string_view> Fields(std::string_view line) {
    std::vector<std::string_view> f;
    for (size_t pos = 0;;) {
        const size_t tab = line.find('\t', pos);
        f.push_back(line.substr(pos, tab == std::string_view::npos ? std::string_view::npos : tab - pos));
        if (tab == std::string_view::npos) return f;
        pos = tab + 1;
    }
}

bool IsKana(char32_t r) { return IsHiraganaRune(r) || IsKatakanaRune(r) || r == U'～'; }

// The rows of phrases.tsv that are utterances read like sounds: interjections whose key
// starts with kana. The other rows are plain entries.
bool StandalonePhrase(const std::vector<std::string_view>& f) {
    return f.size() >= 3 && f[2] == "intj" && IsKana(text::DecodeRune(f[0]).cp);
}

void AddRows(Lexicon& lex, std::string_view file, bool phrases, bool extra) {
    const std::string text = dict::BuiltinText(file, extra);
    const std::string_view all = text;
    for (size_t pos = 0; pos <= all.size();) {
        size_t nl = all.find('\n', pos);
        if (nl == std::string_view::npos) nl = all.size();
        std::string_view line = all.substr(pos, nl - pos);
        pos = nl + 1;
        while (line.ends_with('\r')) line.remove_suffix(1);
        const auto f = Fields(line);
        if (f.size() < 3 || f[0].starts_with("//") || (phrases && !StandalonePhrase(f)) || f[0].empty()) continue;
        SoundRow row;
        const size_t slash = f[1].find('/');
        row.korean = std::string(f[1].substr(0, slash));
        if (slash != std::string_view::npos) {
            std::string_view lemma = f[1].substr(slash + 1);
            if (lemma.ends_with("하다")) lemma.remove_suffix(std::string_view("하다").size());
            row.root = std::string(lemma);
        }
        row.key = ToKatakana(DecodeText(f[0]));
        row.open = f[2] == "open";
        row.lead = f[2] == "lead" || row.open;
        row.intj = f[2] == "intj" || row.lead;
        row.phrase = phrases;
        if (f.size() >= 4) {
            std::string_view opts = f[3];
            while (!opts.empty()) {
                const size_t sp = opts.find(' ');
                row.exclaim = row.exclaim || opts.substr(0, sp) == "exclaim";
                row.predicate = row.predicate || opts.substr(0, sp) == "predicate";
                row.final = row.final || opts.substr(0, sp) == "final";
                opts = sp == std::string_view::npos ? std::string_view() : opts.substr(sp + 1);
            }
        }
        const char32_t first = row.key[0];
        lex[first].push_back(std::move(row));
    }
}

Lexicon BuildLexicon(bool extra) {
    Lexicon l;
    AddRows(l, "sound.tsv", false, extra);
    AddRows(l, "phrases.tsv", true, extra);
    for (auto& [first, rows] : l) {
        std::stable_sort(rows.begin(), rows.end(),
                         [](const SoundRow& a, const SoundRow& b) { return a.key.size() > b.key.size(); });
    }
    return l;
}

const Lexicon& SoundLexicon(bool extra) {
    if (extra) {
        static const Lexicon lex = BuildLexicon(true);
        return lex;
    }
    static const Lexicon lex = BuildLexicon(false);
    return lex;
}

}  // namespace

std::span<const SoundRow> SoundRows(char32_t c, bool extra) {
    const Lexicon& lex = SoundLexicon(extra);
    const auto it = lex.find(c);
    if (it == lex.end()) return {};
    return it->second;
}

bool IsSoundKey(std::u32string_view key, bool extra) {
    if (key.empty()) return false;
    for (const SoundRow& s : SoundRows(key[0], extra)) {
        if (s.key == key && !s.phrase) return true;
    }
    return false;
}

std::u32string DecodeText(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.cp == text::kReplacement && r.size == 1) {
            out.push_back(kInvalidByte + static_cast<unsigned char>(s[0]));
        } else {
            out.push_back(r.cp);
        }
        s.remove_prefix(r.size);
    }
    return out;
}

std::string EncodeText(std::u32string_view rs) {
    std::string out;
    out.reserve(rs.size() * 3);
    for (const char32_t r : rs) {
        if (r >= kInvalidByte) {
            out.push_back(static_cast<char>(r - kInvalidByte));
        } else {
            text::AppendRune(out, r);
        }
    }
    return out;
}

std::u32string ToKatakana(std::u32string_view rs) {
    std::u32string out(rs);
    for (char32_t& r : out) {
        if (IsHiraganaRune(r)) r += 0x60;
    }
    return out;
}

}  // namespace hdTrans::pipeline::internal
