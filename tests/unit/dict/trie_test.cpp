#include "core/dict/trie.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <new>
#include <span>
#include <string>
#include <vector>

#include "core/dict/tsv.h"
#include "hdtest.h"

using hdTrans::dict::Trie;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

// Counts the allocations of the test program, to check that lookups make none.
namespace {
size_t allocations = 0;
}  // namespace

// Every form is replaced, so that each allocation and its release use the same allocator.
void* operator new(size_t size) {
    allocations++;
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void* operator new[](size_t size) { return operator new(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept {
    allocations++;
    return std::malloc(size ? size : 1);
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return operator new(size, std::nothrow); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {

// A dictionary of keys with one value each, as the trie and the value of each key
// number.
struct Dict {
    Trie trie;
    std::vector<std::string> keys;    // sorted
    std::vector<std::string> values;  // by key number
};

Dict Build(std::vector<std::pair<std::string, std::string>> entries) {
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Dict d;
    std::vector<std::string> values;
    for (const auto& e : entries) {
        if (!d.keys.empty() && d.keys.back() == e.first) continue;  // the first value of a key
        d.keys.push_back(e.first);
        values.push_back(e.second);
    }
    std::vector<std::string_view> views(d.keys.begin(), d.keys.end());
    std::vector<int32_t> order;
    d.trie = Trie::Build(views, &order);
    d.values.resize(order.size());
    for (size_t k = 0; k < order.size(); k++) d.values[k] = values[static_cast<size_t>(order[k])];
    return d;
}

std::string Quote(std::string_view s) { return "\"" + std::string(s) + "\""; }

// The keys in Each order with their values, then for each query the keys that
// prefix it as length:value.
std::string Dump(const Dict& d, const std::vector<std::string>& queries) {
    std::vector<std::string> out;
    d.trie.Each([&](std::string_view key, int k) { out.push_back(Quote(key) + "=" + d.values[static_cast<size_t>(k)]); });
    for (const auto& q : queries) {
        std::string ms;
        d.trie.Prefixes(q, [&](int k, size_t n) {
            if (!ms.empty()) ms += ",";
            ms += std::to_string(n) + ":" + d.values[static_cast<size_t>(k)];
        });
        out.push_back(Quote(q) + ">" + ms);
    }
    std::string s;
    for (const auto& o : out) s += (s.empty() ? "" : " ") + o;
    return s;
}

// splitmix64
struct Rng {
    uint64_t s;
    uint64_t Next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
        return z ^ (z >> 31);
    }
    size_t N(size_t n) { return static_cast<size_t>(Next() % n); }
};

bool RuneStart(char b) { return (static_cast<unsigned char>(b) & 0xC0) != 0x80; }

// Every key of keys that is a prefix of q ending at a code point boundary of q, as
// (length, key), shortest first.
std::vector<std::pair<size_t, std::string>> BrutePrefixes(const std::map<std::string, int>& keys, const std::string& q) {
    std::vector<std::pair<size_t, std::string>> out;
    for (size_t n = 0; n <= q.size(); n++) {
        if (n < q.size() && !RuneStart(q[n])) continue;
        if (keys.count(q.substr(0, n))) out.push_back({n, q.substr(0, n)});
    }
    return out;
}

size_t SlowSelect0(std::span<const uint8_t> bits, size_t zero) {
    for (size_t i = 0; i < bits.size(); i++) {
        if (bits[i] == 0) {
            if (zero == 0) return i;
            zero--;
        }
    }
    return bits.size();
}

void CheckTrieLookups(const Trie& trie, const std::map<std::string, int>& reference) {
    auto check = [&](const std::string& query) {
        const auto found = reference.find(query);
        HD_CHECK_EQ(trie.Find(query), found == reference.end() ? -1 : found->second);
        std::vector<std::pair<size_t, int>> want, got;
        for (const auto& [key, number] : reference) {
            if (query.starts_with(key)) want.emplace_back(key.size(), number);
        }
        trie.Prefixes(query, [&](int key, size_t length) { got.emplace_back(length, key); });
        HD_CHECK(got == want);
        const auto longest = trie.LongestPrefix(query);
        HD_CHECK_EQ(longest.key, want.empty() ? -1 : want.back().second);
        HD_CHECK_EQ(longest.length, want.empty() ? size_t{0} : want.back().first);
    };
    check("");
    check("not-a-key");
    for (const auto& [key, number] : reference) {
        (void)number;
        check(key);
        check(key + "!");
    }
    std::vector<std::pair<std::string, int>> each;
    trie.Each([&](std::string_view key, int number) { each.emplace_back(key, number); });
    const std::vector<std::pair<std::string, int>> want(reference.begin(), reference.end());
    HD_CHECK(each == want);
}

std::string SourceDir() {
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return slash == std::string::npos ? "." : f.substr(0, slash);
}

}  // namespace

HD_TEST(trie_dict_cases) {
    struct Case {
        const char* name;
        std::vector<std::pair<std::string, std::string>> entries;
        std::vector<std::string> queries;
        std::string want;
    };
    const Case cases[] = {
        {"empty", {}, {"", "a"}, R"(""> "a">)"},
        {"empty key", {{"", "e"}, {"a", "a"}}, {"ab"}, R"(""=e "a"=a "ab">0:e,1:a)"},
        {"first value of a key",
         {{"学生", "학생"}, {"学", "학"}, {"学生", "배우"}},
         {"学生です", "学", "生"},
         R"("学"=학 "学生"=학생 "学生です">3:학,6:학생 "学">3:학 "生">)"},
        {"code-point order across surrogates",
         {{"\U00020BB7野家", "요시노야"}, {"\U00020BB7", "길"}, {"\uFF5E", "물결"}, {"\uE000", "사용"},
          {"\U00010000", "선형"}, {"\uD7FF", "끝"}},
         {"\U00020BB7野家だ", "\U00020BB8", "\uFF5E"},
         "\"\uD7FF\"=끝 \"\uE000\"=사용 \"\uFF5E\"=물결 \"\U00010000\"=선형 \"\U00020BB7\"=길 \"\U00020BB7野家\"=요시노야 "
         "\"\U00020BB7野家だ\">4:길,10:요시노야 \"\U00020BB8\"> \"\uFF5E\">3:물결"},
        // Keys hold U+FFFD for invalid bytes; a lookup reads an invalid byte as U+FFFD.
        {"invalid UTF-8 is U+FFFD",
         {{"a\uFFFDb", "x"}, {"a\uFFFDc", "y"}},
         {"a\xFE" "bc", "a\uFFFDb", "a\xE3\x81" "b"},
         "\"a\uFFFDb\"=x \"a\uFFFDc\"=y \"a\xFE" "bc\">3:x \"a\uFFFDb\">5:x \"a\xE3\x81" "b\">"},
    };
    for (const auto& c : cases) {
        const std::string got = Dump(Build(c.entries), c.queries);
        if (got != c.want) std::fprintf(stderr, "%s:\n got %s\nwant %s\n", c.name, got.c_str(), c.want.c_str());
        HD_CHECK_EQ(got, c.want);
    }
}

HD_TEST(trie_empty) {
    const Trie none;
    HD_CHECK_EQ(none.Find(""), -1);
    HD_CHECK_EQ(none.LongestPrefix("abc").key, -1);
    HD_CHECK_EQ(none.KeyCount(), size_t{0});
    const Trie built = Trie::Build(std::span<const std::string_view>(), nullptr);
    HD_CHECK_EQ(built.Find(""), -1);
    HD_CHECK_EQ(built.NodeCount(), size_t{1});
    int calls = 0;
    built.Each([&](std::string_view, int) { calls++; });
    built.Prefixes("a", [&](int, size_t) { calls++; });
    HD_CHECK_EQ(calls, 0);
}

// Compares the trie with a map on random dictionaries whose keys share many
// prefixes and mix 1 to 4 byte code points.
HD_TEST(trie_random) {
    const char32_t alphabet[] = {U'a', U'あ', U'学', 0xFFFD, 0xE000, 0x20BB7, 0x20BB8, 0x10000};
    Rng rng{12};
    auto word = [&] {
        std::string w;
        for (size_t n = rng.N(6); n > 0; n--) hdTrans::text::AppendRune(w, alphabet[rng.N(std::size(alphabet))]);
        return w;
    };
    for (size_t size : {1, 7, 8, 9, 100, 3000}) {
        std::map<std::string, int> want;  // key -> index in sorted order
        for (size_t i = 0; i < size; i++) want[word()] = 0;
        std::vector<std::string> keys;
        for (auto& [k, v] : want) {
            v = static_cast<int>(keys.size());
            keys.push_back(k);
        }
        std::vector<std::string_view> views(keys.begin(), keys.end());
        std::vector<int32_t> order;
        const Trie t = Trie::Build(views, &order);
        HD_CHECK_EQ(t.KeyCount(), keys.size());
        HD_REQUIRE(order.size() == keys.size());
        std::vector<int32_t> seen = order;
        std::sort(seen.begin(), seen.end());
        for (size_t i = 0; i < seen.size(); i++) HD_CHECK_EQ(seen[i], static_cast<int32_t>(i));

        std::vector<std::string> each;
        bool numbersMatch = true;
        t.Each([&](std::string_view key, int k) {
            each.emplace_back(key);
            numbersMatch = numbersMatch && keys[static_cast<size_t>(order[static_cast<size_t>(k)])] == key;
        });
        HD_CHECK(each == keys);
        HD_CHECK(numbersMatch);

        for (int i = 0; i < 2000; i++) {
            const std::string q = word() + word();
            const int k = t.Find(q);
            const auto it = want.find(q);
            if (it == want.end()) {
                HD_CHECK_EQ(k, -1);
            } else {
                HD_REQUIRE(k >= 0);
                HD_CHECK_EQ(order[static_cast<size_t>(k)], it->second);
            }
            std::vector<std::pair<size_t, std::string>> got;
            t.Prefixes(q, [&](int key, size_t n) { got.push_back({n, keys[static_cast<size_t>(order[static_cast<size_t>(key)])]}); });
            const auto brute = BrutePrefixes(want, q);
            HD_CHECK(got == brute);
            const auto m = t.LongestPrefix(q);
            if (brute.empty()) {
                HD_CHECK_EQ(m.key, -1);
            } else {
                HD_CHECK_EQ(m.length, brute.back().first);
                HD_CHECK_EQ(keys[static_cast<size_t>(order[static_cast<size_t>(m.key)])], brute.back().second);
            }
        }
    }
}

// Keys of testdata/sample_keys.tsv: every lookup at every position of some text
// agrees with a scan of the key list, in order, without allocating.
HD_TEST(trie_sample_keys) {
    const std::string path = SourceDir() + "/testdata/sample_keys.tsv";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    HD_REQUIRE(f != nullptr);
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);

    std::map<std::string, int> want;
    hdTrans::dict::TsvReader rows(text);
    while (rows.Next()) want[std::string(rows.Fields()[0])] = 0;
    HD_REQUIRE(want.size() > 100 && want.size() <= 200);
    std::vector<std::string> keys;
    for (auto& [k, v] : want) {
        v = static_cast<int>(keys.size());
        keys.push_back(k);
    }
    std::vector<std::string_view> views(keys.begin(), keys.end());
    std::vector<int32_t> order;
    const size_t beforeBuild = allocations;
    const Trie t = Trie::Build(views, &order);
    HD_CHECK(allocations > beforeBuild);  // the counter sees the library's allocations
    auto keyOf = [&](int k) { return keys[static_cast<size_t>(order[static_cast<size_t>(k)])]; };

    struct Case {
        std::string_view text;
        std::vector<std::string> prefixes;  // at the start of text, shortest first
    };
    const Case cases[] = {
        {"東京都庁へ行く", {"東", "東京", "東京都"}},
        {"東京タワーの近く", {"東", "東京", "東京タワー"}},
        {"大学生です", {"大", "大学", "大学生"}},
        {"大丈夫です", {"大", "大丈夫"}},
        {"日本語の本", {"日", "日本", "日本語"}},
        {"ありがとうございました", {"ありがとう"}},
        {"ありがとうございます。", {"ありがとう", "ありがとうございます"}},
        {"ではない", {"では"}},
        {"では無い", {"では", "では無い"}},
        {"CDプレーヤー", {"CD", "CDプレーヤー"}},
        {"ＯＫです", {"ＯＫ"}},
        {"ﾃﾚﾋﾞ番組", {"ﾃﾚﾋﾞ"}},
        {"𠮷野家の牛丼", {"𠮷", "𠮷野", "𠮷野家"}},
        {"𠮷田さん", {"𠮷", "𠮷田"}},
        {"𩸽の干物", {"𩸽", "𩸽の干物"}},
        {"𠀋", {"𠀋"}},
        {"𠮸", {}},
        {"～だ", {"～"}},
        {"〜だ", {"〜"}},
        {"一人暮らし", {"一", "一人", "一人暮らし"}},
        {"花火大会", {"花", "花火", "花火大会"}},
        {"コーヒーカップ", {"コーヒー", "コーヒーカップ"}},
        {"カメラマン", {"カメラ", "カメラマン"}},
        {"東北地方の", {"東", "東北", "東北地方"}},
        {"", {}},
        {"x", {}},
    };
    for (const auto& c : cases) {
        std::vector<std::string> got;
        t.Prefixes(c.text, [&](int k, size_t n) {
            got.push_back(keyOf(k));
            HD_CHECK_EQ(n, keyOf(k).size());
        });
        HD_CHECK(got == c.prefixes);
        if (got != c.prefixes) std::fprintf(stderr, "prefixes of %.*s\n", static_cast<int>(c.text.size()), c.text.data());
    }

    const std::string sentences[] = {
        "東京都の大学生は日本語と中国語を学ぶ。",
        "私たちは𠮷野家で𩸽の干物を食べた。",
        "おはようございます、山田先生。今日は花火大会ですね！",
        "CDプレーヤーとＯＫボタン、ﾃﾚﾋﾞとカメラマン。",
        "\xFF東\xE3\x81京",
    };
    std::vector<std::pair<size_t, std::string>> got;
    got.reserve(16);
    for (const auto& s : sentences) {
        for (size_t at = 0; at <= s.size(); at++) {
            if (at < s.size() && !RuneStart(s[at])) continue;
            const std::string rest = s.substr(at);
            got.clear();
            const size_t before = allocations;
            int found = 0;
            t.Prefixes(rest, [&](int, size_t) { found++; });
            const auto longest = t.LongestPrefix(rest);
            const int exact = t.Find(rest);
            HD_CHECK_EQ(allocations, before);
            (void)longest;
            (void)exact;
            t.Prefixes(rest, [&](int k, size_t n) { got.push_back({n, keyOf(k)}); });
            const auto brute = BrutePrefixes(want, rest);
            HD_CHECK(got == brute);
            HD_CHECK_EQ(found, static_cast<int>(brute.size()));
        }
    }
    for (const auto& k : keys) {
        const size_t before = allocations;
        const int n = t.Find(k);
        HD_CHECK_EQ(allocations, before);
        HD_REQUIRE(n >= 0);
        HD_CHECK_EQ(keyOf(n), k);
    }
}

HD_TEST(trie_borrowed_arrays_and_moves) {
    const std::string_view keys[] = {"", "東", "東京", "東京都", "𠮷", "𠮷野家"};
    Trie owned = Trie::Build(keys, nullptr);
    const Trie::ViewData arrays = owned.View();
    const size_t before = allocations;
    Trie borrowed = Trie::FromView(arrays);
    HD_CHECK_EQ(allocations, before);
    HD_CHECK(borrowed.View().tree.data() == arrays.tree.data());
    HD_CHECK(borrowed.View().label.data() == arrays.label.data());
    HD_CHECK_EQ(borrowed.MemoryBytes(), owned.MemoryBytes());

    Trie moved = std::move(owned);
    owned = std::move(moved);
    for (const auto key : keys) HD_CHECK_EQ(borrowed.Find(key), owned.Find(key));
    HD_CHECK_EQ(borrowed.Find("東北"), -1);
    HD_CHECK_EQ(borrowed.LongestPrefix("東京都庁").length, size_t{9});
    std::vector<std::string> got;
    borrowed.Each([&](std::string_view key, int) { got.emplace_back(key); });
    HD_CHECK(got == std::vector<std::string>(std::begin(keys), std::end(keys)));
    borrowed = Trie::FromView(owned.View());
    HD_CHECK_EQ(borrowed.Find("𠮷野家"), owned.Find("𠮷野家"));
}

HD_TEST(trie_children_in_high_word) {
    const std::string_view keys[] = {"ax", "bxx", "cxxx", "dxxxx", "exxxxx"};
    const Trie owned = Trie::Build(keys, nullptr);
    auto view = owned.View();
    HD_REQUIRE(view.tree.size() == 1);
    const uint64_t tree[] = {view.tree[0], 0};
    view.tree = std::span(tree).first(1);
    const Trie borrowed = Trie::FromView(view);
    HD_REQUIRE(borrowed.Validate());
    for (const auto key : keys) {
        HD_CHECK(borrowed.Find(key) >= 0);
        HD_CHECK_EQ(borrowed.LongestPrefix(std::string(key) + "!").length, key.size());
    }
}

HD_TEST(trie_random_bit_vectors_against_slow_select) {
    bool highSample = false, highStart = false, crossedWord = false;
    for (uint64_t seed : {uint64_t{1}, uint64_t{17}, uint64_t{0x5A17C9E3}}) {
        Rng rng{seed};
        for (size_t nodes : {size_t{1}, size_t{2}, size_t{15}, size_t{16}, size_t{17}, size_t{31}, size_t{32},
                             size_t{33}, size_t{63}, size_t{64}, size_t{65}, size_t{127}, size_t{128},
                             size_t{129}, size_t{200}, size_t{257}, size_t{512}}) {
            for (size_t density : {size_t{1}, size_t{4}, size_t{7}}) {
                std::vector<uint8_t> bits;
                size_t ones = 0, zeros = 0;
                while (bits.size() < 2 * nodes - 1) {
                    const bool one = ones < nodes - 1 && (ones == zeros || rng.N(8) < density);
                    bits.push_back(one ? 1 : 0);
                    if (one) ones++;
                    else zeros++;
                }
                const size_t words = (2 * nodes + 63) / 64;
                std::vector<uint64_t> tree(words + 1, 0), terminal(nodes / 64 + 1, 0);
                std::vector<uint32_t> samples, rank(terminal.size());
                std::vector<uint16_t> labels(nodes, 0);
                std::vector<std::string> paths(nodes);
                size_t parent = 0, child = 1, sibling = 0;
                for (size_t p = 0; p < bits.size(); p++) {
                    if (bits[p] != 0) {
                        tree[p / 64] |= uint64_t{1} << (p % 64);
                        labels[child] = static_cast<uint16_t>(0x100 + sibling++);
                        paths[child] = paths[parent];
                        hdTrans::text::AppendRune(paths[child], labels[child]);
                        child++;
                    } else {
                        if (parent % 16 == 0) {
                            samples.push_back(static_cast<uint32_t>(p));
                            highSample |= p % 64 >= 32;
                        }
                        parent++;
                        sibling = 0;
                    }
                }
                std::map<std::string, int> reference;
                for (size_t node = 0; node < nodes; node++) {
                    terminal[node / 64] |= uint64_t{1} << (node % 64);
                    reference.emplace(paths[node], static_cast<int>(node));
                }
                for (size_t w = 0; w < rank.size(); w++) rank[w] = static_cast<uint32_t>(std::min(w * 64, nodes));
                Trie::ViewData data{std::span(tree).first(words), samples, labels, terminal, rank, nodes};
                const Trie trie = Trie::FromView(data);
                HD_REQUIRE(trie.Validate());
                for (size_t node = 0; node < nodes; node++) {
                    const size_t start = node == 0 ? 0 : SlowSelect0(bits, node - 1) + 1;
                    const size_t end = SlowSelect0(bits, node);
                    const size_t first = start - node + 1;
                    highStart |= start % 64 >= 32;
                    crossedWord |= start / 64 != end / 64;
                    HD_CHECK_EQ(trie.Next(static_cast<int>(node), 0xFF), -1);
                    for (size_t i = 0; i <= end - start; i++) {
                        const int want = i < end - start ? static_cast<int>(first + i) : -1;
                        HD_CHECK_EQ(trie.Next(static_cast<int>(node), static_cast<char32_t>(0x100 + i)), want);
                    }
                }
                CheckTrieLookups(trie, reference);
            }
        }
    }
    HD_CHECK(highSample);
    HD_CHECK(highStart);
    HD_CHECK(crossedWord);
}

HD_TEST(trie_chain_and_fan_shapes_against_sorted_keys) {
    for (size_t count = 1; count <= 200; count++) {
        for (int shape : {0, 1, 2}) {
            std::vector<std::string> keys;
            for (size_t i = 0; i < count; i++) {
                std::string key;
                if (shape == 0) key.assign(i + 1, 'a');
                else {
                    if (shape == 2) key = "a";
                    hdTrans::text::AppendRune(key, static_cast<char32_t>(0x100 + i));
                    if (shape == 2) key.append(i % 7, 'x');
                }
                keys.push_back(std::move(key));
            }
            std::sort(keys.begin(), keys.end());
            const std::vector<std::string_view> views(keys.begin(), keys.end());
            std::vector<int32_t> order;
            const Trie trie = Trie::Build(views, &order);
            HD_REQUIRE(trie.Validate());
            HD_REQUIRE(order.size() == keys.size());
            std::map<std::string, int> reference;
            for (size_t number = 0; number < order.size(); number++) {
                HD_REQUIRE(order[number] >= 0 && static_cast<size_t>(order[number]) < keys.size());
                reference.emplace(keys[static_cast<size_t>(order[number])], static_cast<int>(number));
            }
            HD_CHECK_EQ(reference.size(), keys.size());
            CheckTrieLookups(trie, reference);
        }
    }
}

HD_TEST(trie_select_zero_rejects_invalid_sample_positions) {
    const std::string_view keys[] = {"a", "aa", "ab", "b"};
    const Trie owned = Trie::Build(keys, nullptr);
    const auto data = owned.View();
    std::vector<uint32_t> samples(data.zeros.begin(), data.zeros.end());
    const uint32_t invalid[] = {0, static_cast<uint32_t>(2 * data.label.size() - 1),
                                static_cast<uint32_t>(data.tree.size() * 64), UINT32_MAX};
    for (uint32_t position : invalid) {
        auto view = data;
        samples[0] = position;
        view.zeros = samples;
        const Trie trie = Trie::FromView(view);
        HD_CHECK_EQ(trie.Next(1, U'a'), -1);
    }
}

HD_TEST(trie_guarded_views_and_validation) {
    const std::string_view keys[] = {"", "学", "学生", "東京", "𠮷野家"};
    const Trie owned = Trie::Build(keys, nullptr);
    HD_CHECK(owned.Validate());
    HD_CHECK(Trie{}.Validate());
    struct GuardState {
        bool readable = true;
        mutable int checks = 0;
        mutable int failures = 0;
    } state;
    auto data = owned.View();
    data.guard = {&state,
                  [](const void* context, const void* pointer, size_t size) {
                      const auto& s = *static_cast<const GuardState*>(context);
                      s.checks++;
                      return s.readable && pointer == nullptr && size == 0;
                  },
                  [](const void* context) { static_cast<const GuardState*>(context)->failures++; }};
    const Trie borrowed = Trie::FromView(data);
    HD_CHECK(borrowed.Validate());
    HD_CHECK(borrowed.View().guard.context == &state);
    const size_t before = allocations;
    HD_CHECK_EQ(borrowed.Find("学生"), owned.Find("学生"));
    int prefixes = 0;
    borrowed.Prefixes("学生です", [&](int, size_t) { prefixes++; });
    HD_CHECK_EQ(prefixes, 3);
    HD_CHECK_EQ(allocations, before);
    state.readable = false;
    HD_CHECK_EQ(borrowed.Find("学生"), -1);
    HD_CHECK_EQ(borrowed.Next(0, U'学'), -1);
    HD_CHECK_EQ(borrowed.KeyAt(0), -1);
    HD_CHECK_EQ(borrowed.LongestPrefix("学生").key, -1);
    borrowed.Prefixes("学生", [&](int, size_t) { prefixes++; });
    borrowed.Each([&](std::string_view, int) { prefixes++; });
    HD_CHECK_EQ(prefixes, 3);
    HD_CHECK(!borrowed.Validate());
    HD_CHECK(state.failures >= 7);
}

HD_TEST(trie_malformed_arrays) {
    const std::string_view keys[] = {"a", "ab", "b", "bc"};
    const Trie owned = Trie::Build(keys, nullptr);
    HD_REQUIRE(owned.Validate());
    const auto valid = owned.View();
    auto exercise = [](Trie::ViewData view) {
        const Trie t = Trie::FromView(view);
        HD_CHECK(!t.Validate());
        (void)t.Find("abc");
        (void)t.Next(-1, U'a');
        (void)t.Next(999, U'a');
        (void)t.KeyAt(-1);
        (void)t.KeyAt(999);
        t.Prefixes("abc", [](int, size_t) {});
        size_t visits = 0;
        t.Each([&](std::string_view, int) { visits++; });
        HD_CHECK(visits <= view.label.size());
    };
    for (int field = 0; field < 5; field++) {
        auto data = valid;
        switch (field) {
            case 0: data.tree = data.tree.first(data.tree.size() - 1); break;
            case 1: data.zeros = {}; break;
            case 2: data.label = {}; break;
            case 3: data.terminal = {}; break;
            case 4: data.rank = {}; break;
        }
        exercise(data);
    }
    auto data = valid;
    std::vector<uint64_t> tree(valid.tree.size(), ~uint64_t{0});
    data.tree = tree;
    exercise(data);
    data = valid;
    std::vector<uint32_t> zeros(valid.zeros.begin(), valid.zeros.end());
    zeros[0] = UINT32_MAX;
    data.zeros = zeros;
    exercise(data);
    zeros[0] = 0;
    exercise(data);
    data = valid;
    std::vector<uint16_t> labels(valid.label.begin(), valid.label.end());
    std::swap(labels[1], labels[2]);
    data.label = labels;
    exercise(data);
    data = valid;
    std::vector<uint32_t> ranks(valid.rank.begin(), valid.rank.end());
    ranks[0] = UINT32_MAX;
    data.rank = ranks;
    exercise(data);
    data = valid;
    data.keys++;
    exercise(data);
    for (size_t nodes : {size_t{31}, size_t{32}, size_t{63}, size_t{64}, size_t{65}}) {
        const std::string key(nodes - 1, 'a');
        const std::string_view one[] = {key};
        HD_CHECK(Trie::Build(one, nullptr).Validate());
    }
}
