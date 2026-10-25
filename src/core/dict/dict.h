#pragma once
// An immutable dictionary of UTF-8 keys, each with its records in order.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/codes.h"
#include "core/dict/trie.h"

namespace hdTrans::dict {

// One sense of a key.
struct Record {
    std::string_view target;  // Korean, as written (stem markers included)
    const Codes* codes = nullptr;
    RuleNumber rule = 0;
    int32_t id = 0;  // the number of the id column
};

// Holds each distinct tuple of codes once, at a stable address, numbered in the
// order of their first Intern.
class CodeTable {
public:
    CodeTable() = default;
    // The base must be complete, including all rewrite twins, and remain unchanged.
    // Rejects a chained base or a base aliasing out, leaving out unchanged.
    static bool WithBase(std::shared_ptr<const CodeTable> base, CodeTable& out);
    CodeTable(CodeTable&&) = default;
    CodeTable& operator=(CodeTable&&) = default;

    // The number of the tuple, added when new after the immutable base prefix.
    int32_t Intern(std::string_view part, std::string_view sem, std::string_view gram, std::string_view dict);
    const Codes* At(int32_t i) const {
        const auto index = static_cast<std::size_t>(i);
        const std::size_t prefix = BaseSize();
        return index < prefix ? &base_->list_[index] : &list_[index - prefix];
    }
    std::size_t Size() const { return BaseSize() + list_.size(); }

    // Set Codes::withXXX and Codes::asPlain of owned tuple i, adding their twins.
    // Base indices are already complete and are not changed.
    void SetWithXXX(int32_t i);
    void SetAsPlain(int32_t i);

    std::size_t MemoryBytes() const;

private:
    std::size_t BaseSize() const { return base_ ? base_->list_.size() : 0; }
    int32_t Find(std::string_view part, std::string_view sem, std::string_view gram, std::string_view dict) const;
    void Grow();

    std::shared_ptr<const CodeTable> base_;
    std::deque<Codes> list_;
    std::vector<int32_t> slots_;  // open addressing over owned list_; -1 for empty
};

class Dict {
public:
    struct ViewData {
        Trie::ViewData keys;
        std::string_view blob;
        std::span<const uint32_t> starts;
        std::span<const Codes* const> codes;
        ReadGuard guard = {};
    };

    Dict() = default;
    // The buffers and codes must outlive the dictionary.
    static Dict FromView(ViewData data);
    ViewData View() const { return {keys_.View(), blob_, starts_, codes_, guard_}; }

    // The number of key or -1.
    int Find(std::string_view key) const { return keys_.Find(key); }
    // Calls f(int key, std::size_t length) for every key that is a prefix of s,
    // shortest first.
    template <class F>
    void Prefixes(std::string_view s, F&& f) const {
        keys_.Prefixes(s, f);
    }
    // Calls f(const Record&) for each record of key number k in order; stops when f
    // returns false.
    template <class F>
    void EachRecord(int k, F&& f) const;
    void AppendRecords(std::vector<Record>& out, int k) const;
    // The records of key, empty for none.
    std::vector<Record> Records(std::string_view key) const;

    // The number of keys.
    std::size_t Len() const { return keys_.KeyCount(); }
    // Calls f(std::string_view key, std::vector<Record> records) for every key in
    // code-point order.
    template <class F>
    void Each(F&& f) const;

    std::size_t MemoryBytes() const;
    bool Validate() const;

private:
    friend struct DictBuilder;
    static constexpr int kKeysPerStart = 8;

    // On success, advances i and sets more when another record of the key follows.
    // Decode and ReadUvarint read [i, end) of blob_, which the caller has checked
    // with guard_.
    bool Decode(std::size_t& i, std::size_t end, Record& r, bool& more) const;
    bool ReadUvarint(std::size_t& i, std::size_t end, uint64_t& value) const;
    bool ReadStart(std::size_t group, std::size_t& offset) const;
    bool RecordsStart(int k, std::size_t& i, std::size_t& end) const;

    // blob_ holds the records of every key, key number k after key k-1. A record is
    // uvarint(len(target)<<2 | hasRule<<1 | more), target, uvarint(code),
    // uvarint(rule) when rule is not 0, uvarint(id); code is an index in codes_ and
    // more is set when another record of the key follows. starts_[g] is the offset
    // of key number g*kKeysPerStart.
    struct Storage {
        std::string blob;
        std::vector<uint32_t> starts;
        std::vector<const Codes*> codes;
    };
    std::unique_ptr<Storage> storage_;
    Trie keys_;
    std::string_view blob_;
    std::span<const uint32_t> starts_;
    std::span<const Codes* const> codes_;  // by descending use
    ReadGuard guard_ = {};
};

template <class F>
void Dict::EachRecord(int k, F&& f) const {
    std::size_t i, end;
    if (!RecordsStart(k, i, end)) return;
    for (;;) {
        Record r;
        bool more;
        if (!Decode(i, end, r, more)) return;
        const bool last = static_cast<std::size_t>(k) + 1 == Len() || (static_cast<std::size_t>(k) + 1) % kKeysPerStart == 0;
        if ((!more && last && i != end) || ((more || !last) && i == end)) {
            guard_.Fail();
            return;
        }
        if (!f(static_cast<const Record&>(r)) || !more) return;
    }
}

template <class F>
void Dict::Each(F&& f) const {
    keys_.Each([&](std::string_view key, int k) {
        std::vector<Record> records;
        AppendRecords(records, k);
        f(key, records);
    });
}

}  // namespace hdTrans::dict
