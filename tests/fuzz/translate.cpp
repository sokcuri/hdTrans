// libFuzzer target: translates each input as a document in a fresh session with the dictionaries
// named by HDTRANS_FUZZ_DICTS (a folder or .hdsnap). The mutator edits text a code point at a time
// with the characters Japanese text is made of (kana, kanji, punctuation, ruby markup, runs of one
// character, whitespace runs, line breaks) and sometimes bytes. tools/fuzz.py runs it.
#include "core/engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "core/text/utf8.h"

extern "C" size_t LLVMFuzzerMutate(uint8_t* data, size_t size, size_t maxSize);

namespace {

hdTrans::Engine* engine = nullptr;

using Rng = std::minstd_rand;

size_t Below(Rng& rng, size_t n) { return n ? std::uniform_int_distribution<size_t>(0, n - 1)(rng) : 0; }

// Byte offsets of the code point starts of s, and s.size().
std::vector<size_t> Starts(std::string_view s) {
    std::vector<size_t> starts;
    for (size_t pos = 0; pos < s.size();) {
        starts.push_back(pos);
        pos += hdTrans::text::DecodeRune(s.substr(pos)).size;
    }
    starts.push_back(s.size());
    return starts;
}

std::string Utf8(char32_t cp) {
    std::string out;
    hdTrans::text::AppendRune(out, cp);
    return out;
}

constexpr std::string_view kPunct[] = {
    "「", "」", "『", "』", "（", "）", "(", ")", "【", "】", "〔", "〕", "［", "］", "〈", "〉", "《", "》",
    "、", "。", "，", "．", "・", "：", "；", "？", "！", "?", "!", "…", "‥", "〜", "～", "ー", "—", "―",
    "“", "”", "‘", "’", "\"", "'", "｜", "|", "♪", "☆", "※", "々", "〆", "ヶ", "ゝ", "ゞ", "ヽ", "ヾ"};
constexpr std::string_view kMarkup[] = {
    "｜漢字《かんじ》", "|山《やま》", "漢字《かんじ》", "｜漢字（かんじ）", "｜文字(もじ)",
    "<ruby>漢字<rt>かんじ</rt></ruby>", "<ruby><rb>字</rb><rp>(</rp><rt>じ</rt><rp>)</rp></ruby>", "《", "》",
    "｜", "<rt>", "</ruby>", "<br>", "&amp;", "\\n", "%s", "{0}", "[name]", "@", "#", "$"};
constexpr std::string_view kOdd[] = {
    "\xEF\xBF\xBD", "\xEF\xBB\xBF", "\xE2\x80\x8B", "\xE3\x82\x99", "\xE3\x82\x9A", "\xEF\xB8\x8F",
    "\xF0\x9F\x98\x80", "\xF0\xA0\xAE\xB7", "\xCC\x81", "\r", "\r\n", std::string_view("\0", 1), "\x7F",
    "\xE3", "\xE3\x81", "\xFF", "\xC0\x80", "\xED\xA0\x80"};
constexpr std::string_view kSpace[] = {" ", "\xE3\x80\x80", "\t"};

// One code point or short piece of text of a random kind.
std::string Piece(Rng& rng) {
    switch (Below(rng, 12)) {
        case 0: case 1: case 2: return Utf8(static_cast<char32_t>(0x3041 + Below(rng, 0x3096 - 0x3041 + 1)));
        case 3: case 4: return Utf8(static_cast<char32_t>(0x30A1 + Below(rng, 0x30FA - 0x30A1 + 1)));
        case 5: return Utf8(static_cast<char32_t>(0x4E00 + Below(rng, 0x9FFF - 0x4E00 + 1)));
        case 6: return Utf8(static_cast<char32_t>(0xFF61 + Below(rng, 0xFF9F - 0xFF61 + 1)));
        case 7: case 8: return std::string(kPunct[Below(rng, std::size(kPunct))]);
        case 9: return std::string(kMarkup[Below(rng, std::size(kMarkup))]);
        case 10: return std::string(kOdd[Below(rng, std::size(kOdd))]);
        default: {
            const char32_t ascii[] = {U'0', U'０', U'a', U'Ａ'};
            const size_t i = Below(rng, std::size(ascii));
            return Utf8(ascii[i] + static_cast<char32_t>(Below(rng, i == 0 || i == 1 ? 10 : 26)));
        }
    }
}

size_t RunLength(Rng& rng) { return Below(rng, 8) == 0 ? 50 + Below(rng, 400) : 2 + Below(rng, 8); }

std::string Repeat(std::string_view s, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; i++) out += s;
    return out;
}

// s with one text edit at code point boundaries.
std::string Edit(std::string s, Rng& rng) {
    const std::vector<size_t> starts = Starts(s);
    const size_t points = starts.size() - 1;
    const size_t k = Below(rng, points + 1);
    const size_t at = starts[k];
    auto range = [&](size_t maxLen) {
        const size_t i = Below(rng, points + 1);
        const size_t j = std::min(points, i + 1 + Below(rng, maxLen));
        return std::pair<size_t, size_t>(starts[i], starts[j]);
    };
    switch (Below(rng, 10)) {
        case 0: case 1: {
            std::string add;
            for (size_t n = 1 + Below(rng, 6); n > 0; n--) add += Piece(rng);
            s.insert(at, add);
            break;
        }
        case 2: {
            const auto [b, e] = range(16);
            s.erase(b, e - b);
            break;
        }
        case 3: {
            const std::string piece = k < points && Below(rng, 2) ? s.substr(at, starts[k + 1] - at) : Piece(rng);
            s.insert(at, Repeat(piece, RunLength(rng)));
            break;
        }
        case 4: {
            const auto [b, e] = range(12);
            s.insert(b, Repeat(s.substr(b, e - b), 1 + Below(rng, 12)));
            break;
        }
        case 5:
            s.insert(at, Repeat(kSpace[Below(rng, std::size(kSpace))], Below(rng, 4) ? 2 + Below(rng, 4) : 5 + Below(rng, 40)));
            break;
        case 6: {
            const auto [b, e] = range(8);
            const std::string reading = Piece(rng) + Piece(rng);
            const std::string_view open[] = {"｜", "|", "", "「", "（"};
            const std::string_view close[] = {"《", "《", "《", "」", "）"};
            const size_t form = Below(rng, std::size(open));
            s.insert(e, std::string(close[form]) + (form < 3 ? reading + "》" : ""));
            s.insert(b, open[form]);
            break;
        }
        case 7:
            s.insert(at, Below(rng, 3) ? std::string("\n") : Repeat("\n", 2 + Below(rng, 6)));
            break;
        case 8: {
            const auto [b, e] = range(1);
            s.replace(b, e - b, Piece(rng));
            break;
        }
        default: {
            const auto [b1, e1] = range(10);
            const auto [b2, e2] = range(10);
            if (e1 <= b2) {
                const std::string first = s.substr(b1, e1 - b1), second = s.substr(b2, e2 - b2);
                s.replace(b2, e2 - b2, first);
                s.replace(b1, e1 - b1, second);
            }
            break;
        }
    }
    return s;
}

// The longest prefix of s within maxSize bytes that ends at a code point boundary.
size_t Fit(std::string_view s, size_t maxSize) {
    if (s.size() <= maxSize) return s.size();
    size_t end = 0;
    for (const size_t start : Starts(s)) {
        if (start > maxSize) break;
        end = start;
    }
    return end;
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    const char* dicts = std::getenv("HDTRANS_FUZZ_DICTS");
    if (!dicts || !*dicts) {
        std::fprintf(stderr, "hdtrans_fuzz: set HDTRANS_FUZZ_DICTS to a dictionary folder or .hdsnap\n");
        std::exit(2);
    }
    engine = new hdTrans::Engine(false, hdTrans::CacheWrite::Off);
    if (!engine->Open({dicts})) {
        std::fprintf(stderr, "hdtrans_fuzz: cannot load %s\n", dicts);
        std::exit(2);
    }
    return 0;
}

// The input's bytes pick the speech level and whether the session translates the text a second
// time with what it learned the first time.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    uint32_t hash = 2166136261u;
    for (const unsigned char c : text) hash = (hash ^ c) * 16777619u;
    hdTrans::Session session(*engine);
    session.SetSpeech(hash & 1 ? hdTrans::Speech::Soft : hdTrans::Speech::Strict);
    session.Translate(text);
    if ((hash >> 1) % 4 == 0) session.Translate(text);
    return 0;
}

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t* data, size_t size, size_t maxSize, unsigned int seed) {
    Rng rng(seed);
    if (Below(rng, 16) == 0) return LLVMFuzzerMutate(data, size, maxSize);
    std::string s(reinterpret_cast<const char*>(data), size);
    for (size_t n = 1 + Below(rng, 3); n > 0; n--) s = Edit(std::move(s), rng);
    const size_t fit = Fit(s, maxSize);
    std::copy(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(fit), data);
    return fit;
}

// A prefix of one input followed by a suffix of the other, or a piece of the second inserted into
// the first, cut at code point boundaries.
extern "C" size_t LLVMFuzzerCustomCrossOver(const uint8_t* data1, size_t size1, const uint8_t* data2, size_t size2,
                                            uint8_t* out, size_t maxOutSize, unsigned int seed) {
    Rng rng(seed);
    const std::string_view a(reinterpret_cast<const char*>(data1), size1);
    const std::string_view b(reinterpret_cast<const char*>(data2), size2);
    const std::vector<size_t> sa = Starts(a), sb = Starts(b);
    const size_t cutA = sa[Below(rng, sa.size())];
    const size_t iB = Below(rng, sb.size());
    const size_t fromB = sb[iB];
    std::string s;
    if (Below(rng, 2)) {
        s = std::string(a.substr(0, cutA)) + std::string(b.substr(fromB));
    } else {
        const size_t toB = sb[std::min(sb.size() - 1, iB + 1 + Below(rng, 40))];
        s = std::string(a.substr(0, cutA)) + std::string(b.substr(fromB, toB - fromB)) + std::string(a.substr(cutA));
    }
    const size_t fit = Fit(s, maxOutSize);
    std::copy(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(fit), out);
    return fit;
}
