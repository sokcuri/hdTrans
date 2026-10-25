#include "core/dict/dict.h"

#include <cstdint>
#include <algorithm>
#include <limits>
#include <utility>

namespace hdTrans::dict {

namespace {

uint64_t Hash(std::string_view s, uint64_t h) {
    for (const char c : s) h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ull;
    return (h ^ 0xFF) * 0x100000001B3ull;  // field separator
}

uint64_t HashCodes(std::string_view part, std::string_view sem, std::string_view gram, std::string_view dict) {
    return Hash(dict, Hash(gram, Hash(sem, Hash(part, 0xCBF29CE484222325ull))));
}

}  // namespace

bool CodeTable::WithBase(std::shared_ptr<const CodeTable> base, CodeTable& out) {
    if (base && (base->base_ || base.get() == &out)) return false;
    CodeTable table;
    table.base_ = std::move(base);
    out = std::move(table);
    return true;
}

int32_t CodeTable::Find(std::string_view part, std::string_view sem, std::string_view gram, std::string_view dict) const {
    if (slots_.empty()) return -1;
    const std::size_t mask = slots_.size() - 1;
    for (std::size_t s = HashCodes(part, sem, gram, dict) & mask;; s = (s + 1) & mask) {
        const int32_t i = slots_[s];
        if (i < 0) return -1;
        const Codes& c = list_[static_cast<std::size_t>(i)];
        if (c.partText == part && c.sem.Text() == sem && c.gram.Text() == gram && c.dict.Text() == dict) return i;
    }
}

int32_t CodeTable::Intern(std::string_view part, std::string_view sem, std::string_view gram, std::string_view dict) {
    if (base_) {
        const int32_t i = base_->Find(part, sem, gram, dict);
        if (i >= 0) return i;
    }
    if (slots_.empty() || list_.size() * 2 >= slots_.size()) Grow();
    const std::size_t mask = slots_.size() - 1;
    for (std::size_t s = HashCodes(part, sem, gram, dict) & mask;; s = (s + 1) & mask) {
        const int32_t i = slots_[s];
        if (i < 0) {
            Codes& c = list_.emplace_back();
            c.part = PartCode(part);
            c.partText = part;
            c.sem = SemCodes(sem);
            c.gram = GramCodes(gram);
            c.dict = DictCode(dict);
            slots_[s] = static_cast<int32_t>(list_.size() - 1);
            return static_cast<int32_t>(BaseSize() + list_.size() - 1);
        }
        const Codes& c = list_[static_cast<std::size_t>(i)];
        if (c.partText == part && c.sem.Text() == sem && c.gram.Text() == gram && c.dict.Text() == dict)
            return static_cast<int32_t>(BaseSize() + static_cast<std::size_t>(i));
    }
}

void CodeTable::Grow() {
    std::size_t size = slots_.empty() ? 1024 : slots_.size() * 2;
    slots_.assign(size, -1);
    for (std::size_t i = 0; i < list_.size(); i++) {
        const Codes& c = list_[i];
        for (std::size_t s = HashCodes(c.partText, c.sem.Text(), c.gram.Text(), c.dict.Text()) & (size - 1);;
             s = (s + 1) & (size - 1)) {
            if (slots_[s] < 0) {
                slots_[s] = static_cast<int32_t>(i);
                break;
            }
        }
    }
}

void CodeTable::SetWithXXX(int32_t i) {
    const std::size_t prefix = BaseSize();
    if (static_cast<std::size_t>(i) < prefix) return;
    Codes& c = list_[static_cast<std::size_t>(i) - prefix];
    if (c.withXXX) return;
    if (c.sem.Contains("XXX")) {
        c.withXXX = &c;
        return;
    }
    const std::string sem = std::string(c.sem.Text()) + "XXX";
    const std::string part = c.partText;
    const std::string gram(c.gram.Text());
    const std::string dict(c.dict.Text());
    const int32_t twin = Intern(part, sem, gram, dict);
    if (static_cast<std::size_t>(twin) >= prefix)
        list_[static_cast<std::size_t>(twin) - prefix].withXXX = At(twin);
    c.withXXX = At(twin);
}

void CodeTable::SetAsPlain(int32_t i) {
    const std::size_t prefix = BaseSize();
    if (static_cast<std::size_t>(i) < prefix) return;
    Codes& c = list_[static_cast<std::size_t>(i) - prefix];
    if (c.asPlain) return;
    if (c.dict == "GN") {
        c.asPlain = &c;
        return;
    }
    const std::string part = c.partText;
    const std::string sem(c.sem.Text());
    const std::string gram(c.gram.Text());
    const int32_t twin = Intern(part, sem, gram, "GN");
    if (static_cast<std::size_t>(twin) >= prefix)
        list_[static_cast<std::size_t>(twin) - prefix].asPlain = At(twin);
    c.asPlain = At(twin);
}

std::size_t CodeTable::MemoryBytes() const {
    std::size_t n = (base_ ? base_->MemoryBytes() : 0) + slots_.size() * sizeof(int32_t) + list_.size() * sizeof(Codes);
    for (const Codes& c : list_) {
        for (const std::string_view s : {std::string_view(c.partText), c.sem.Text(), c.gram.Text(), c.dict.Text()}) {
            if (s.size() > sizeof(std::string) - 2) n += s.size() + 1;
        }
    }
    return n;
}

bool Dict::ReadUvarint(std::size_t& i, std::size_t end, uint64_t& value) const {
    value = 0;
    if (i >= end || end > blob_.size()) return guard_.Fail();
    for (unsigned byte = 0; byte < 10; byte++) {
        if (i >= end) return guard_.Fail();
        const auto b = static_cast<unsigned char>(blob_[i++]);
        if (byte == 9 && b > 1) return guard_.Fail();
        value |= uint64_t{b & 0x7Fu} << (byte * 7);
        if (b < 0x80) return true;
    }
    return guard_.Fail();
}

bool Dict::ReadStart(std::size_t group, std::size_t& offset) const {
    if (group >= starts_.size()) return guard_.Fail();
    if (!guard_.Check(starts_.data() + group, sizeof(uint32_t))) return guard_.Fail();
    offset = starts_[group];
    if (offset >= blob_.size()) return guard_.Fail();
    return true;
}

bool Dict::RecordsStart(int k, std::size_t& i, std::size_t& end) const {
    const std::size_t keys = Len();
    if (k < 0 || static_cast<std::size_t>(k) >= keys ||
        starts_.size() != keys / kKeysPerStart + (keys % kKeysPerStart != 0)) return guard_.Fail();
    const auto group = static_cast<std::size_t>(k / kKeysPerStart);
    std::size_t first;
    if (!ReadStart(0, first)) return false;
    if (first != 0) return guard_.Fail();
    if (!ReadStart(group, i)) return false;
    end = blob_.size();
    if (group + 1 < starts_.size() && !ReadStart(group + 1, end)) return false;
    if (i >= end || end > blob_.size() || !guard_.Check(blob_.data() + i, end - i)) return guard_.Fail();
    if (group > 0) {
        std::size_t previous;
        if (!ReadStart(group - 1, previous)) return false;
        if (previous >= i) return guard_.Fail();
    }
    for (int skip = k % kKeysPerStart; skip > 0;) {
        Record r;
        bool more;
        if (!Decode(i, end, r, more)) return false;
        if (!more) skip--;
    }
    if (i >= end) return guard_.Fail();
    return true;
}

bool Dict::Decode(std::size_t& i, std::size_t end, Record& r, bool& more) const {
    uint64_t h;
    if (!ReadUvarint(i, end, h)) return false;
    const uint64_t length = h >> 2;
    if (i > end || end > blob_.size() || length > end - i) return guard_.Fail();
    const auto n = static_cast<std::size_t>(length);
    const std::string_view target(blob_.data() + i, n);
    i += n;
    uint64_t code, rule = 0, id;
    if (!ReadUvarint(i, end, code)) return false;
    if (code >= codes_.size() || codes_[static_cast<std::size_t>(code)] == nullptr) return guard_.Fail();
    if ((h & 2) != 0 && !ReadUvarint(i, end, rule)) return false;
    if (!ReadUvarint(i, end, id)) return false;
    if (rule > std::numeric_limits<uint32_t>::max() || id > std::numeric_limits<uint32_t>::max())
        return guard_.Fail();
    r = {target, codes_[static_cast<std::size_t>(code)], static_cast<int32_t>(static_cast<uint32_t>(rule)),
         static_cast<int32_t>(static_cast<uint32_t>(id))};
    more = (h & 1) != 0;
    return true;
}

bool Dict::Validate() const {
    if (!keys_.Validate()) return guard_.Fail();
    const std::size_t keys = Len();
    if (starts_.size() != keys / kKeysPerStart + (keys % kKeysPerStart != 0)) return guard_.Fail();
    if (keys == 0) return blob_.empty() && codes_.empty() ? true : guard_.Fail();
    if (!guard_.Check(blob_.data(), blob_.size())) return guard_.Fail();
    for (const Codes* codes : codes_) {
        if (codes == nullptr) return guard_.Fail();
    }
    std::size_t i = 0;
    for (std::size_t k = 0; k < keys; k++) {
        if (k % kKeysPerStart == 0) {
            std::size_t offset;
            if (!ReadStart(k / kKeysPerStart, offset)) return false;
            if (offset != i) return guard_.Fail();
        }
        bool more;
        do {
            Record r;
            if (!Decode(i, blob_.size(), r, more)) return false;
        } while (more);
    }
    return i == blob_.size() ? true : guard_.Fail();
}

void Dict::AppendRecords(std::vector<Record>& out, int k) const {
    EachRecord(k, [&](const Record& r) {
        out.push_back(r);
        return true;
    });
}

std::vector<Record> Dict::Records(std::string_view key) const {
    std::vector<Record> out;
    if (const int k = keys_.Find(key); k >= 0) AppendRecords(out, k);
    return out;
}

Dict Dict::FromView(ViewData data) {
    Dict d;
    d.keys_ = Trie::FromView(data.keys);
    d.blob_ = data.blob;
    d.starts_ = data.starts;
    d.codes_ = data.codes;
    d.guard_ = data.guard;
    return d;
}

std::size_t Dict::MemoryBytes() const {
    if (storage_) {
        return keys_.MemoryBytes() + storage_->blob.capacity() + storage_->starts.capacity() * sizeof(uint32_t) +
               storage_->codes.capacity() * sizeof(const Codes*);
    }
    return keys_.MemoryBytes() + blob_.size() + starts_.size_bytes() + codes_.size_bytes();
}

}  // namespace hdTrans::dict
