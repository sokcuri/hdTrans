#include "core/dict/tsv.h"

#include <cstdint>
#include <bit>
#include <cstring>

#include "core/text/utf8.h"

namespace hdTrans::dict {

namespace {

constexpr std::size_t kChunkSize = 64 << 10;
constexpr std::string_view kBom = "\xEF\xBB\xBF";

// The index of the first tab of s at or after i, or s.size().
std::size_t FindTab(std::string_view s, std::size_t i) {
    if constexpr (std::endian::native == std::endian::little) {
        constexpr uint64_t kOnes = 0x0101010101010101ull;
        for (; i + 8 <= s.size(); i += 8) {
            uint64_t x;
            std::memcpy(&x, s.data() + i, 8);
            x ^= kOnes * '\t';
            // The lowest flagged byte is the first zero byte, the first tab.
            if (const uint64_t zero = (x - kOnes) & ~x & (kOnes << 7)) {
                return i + static_cast<std::size_t>(std::countr_zero(zero) / 8);
            }
        }
    }
    while (i < s.size() && s[i] != '\t') i++;
    return i;
}

std::string_view DropCR(std::string_view s) {
    if (!s.empty() && s.back() == '\r') s.remove_suffix(1);
    return s;
}

// Unicode White_Space.
bool IsSpace(char32_t c) {
    switch (c) {
    case '\t': case '\n': case '\v': case '\f': case '\r': case ' ': case 0x85: case 0xA0:
    case 0x1680: case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
        return true;
    }
    return c >= 0x2000 && c <= 0x200A;
}

// Returns the next run of non-space characters of s and removes s up to its end;
// empty when s holds no more.
std::string_view NextWord(std::string_view& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto r = text::DecodeRune(s.substr(i));
        if (!IsSpace(r.cp)) break;
        i += r.size;
    }
    const std::size_t start = i;
    while (i < s.size()) {
        const auto r = text::DecodeRune(s.substr(i));
        if (IsSpace(r.cp)) break;
        i += r.size;
    }
    const std::string_view word = s.substr(start, i - start);
    s.remove_prefix(i);
    return word;
}

}  // namespace

namespace internal {

bool ParseInt32(std::string_view s, int32_t& out) {
    std::size_t i = 0;
    bool negative = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
        negative = s[0] == '-';
        i = 1;
    }
    if (i == s.size()) return false;
    // Magnitude up to 2^63 for a negative number, 2^63-1 otherwise.
    const uint64_t limit = negative ? uint64_t{1} << 63 : (uint64_t{1} << 63) - 1;
    uint64_t v = 0;
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        const unsigned d = static_cast<unsigned>(s[i] - '0');
        if (v > (limit - d) / 10) return false;
        v = v * 10 + d;
    }
    if (negative) v = 0 - v;
    out = static_cast<int32_t>(static_cast<uint32_t>(v));
    return true;
}

}  // namespace internal

TsvReader::TsvReader(std::string_view text, std::string_view comment) : text_(text), comment_(comment) {}

TsvReader::TsvReader(ReadFunc read, void* context, std::string_view comment)
    : read_(read), context_(context), comment_(comment) {}

bool TsvReader::NextLine(std::string_view& line) {
    if (!read_) {
        if (pos_ >= text_.size()) return false;
        const std::size_t nl = text_.find('\n', pos_);
        const std::size_t end = nl == std::string_view::npos ? text_.size() : nl;
        line = DropCR(text_.substr(pos_, end - pos_));
        pos_ = nl == std::string_view::npos ? text_.size() : nl + 1;
        return true;
    }
    std::size_t searched = pos_;
    for (;;) {
        const std::size_t nl = buffer_.find('\n', searched);
        if (nl != std::string::npos) {
            line = DropCR(std::string_view(buffer_).substr(pos_, nl - pos_));
            pos_ = nl + 1;
            return true;
        }
        if (eof_) {
            if (pos_ >= buffer_.size()) return false;
            line = DropCR(std::string_view(buffer_).substr(pos_));
            pos_ = buffer_.size();
            return true;
        }
        buffer_.erase(0, pos_);
        pos_ = 0;
        searched = buffer_.size();
        buffer_.resize(searched + kChunkSize);
        const std::ptrdiff_t n = read_(context_, buffer_.data() + searched, kChunkSize);
        if (n < 0) {
            buffer_.resize(searched);
            failed_ = true;
            return false;
        }
        buffer_.resize(searched + static_cast<std::size_t>(n));
        if (n == 0) eof_ = true;
    }
}

bool TsvReader::Next() {
    std::string_view line;
    while (!failed_ && NextLine(line)) {
        line_++;
        if (line_ == 1 && line.starts_with(kBom)) line.remove_prefix(kBom.size());
        if (line.empty() || (!comment_.empty() && line.starts_with(comment_))) continue;
        fields_.clear();
        for (std::size_t start = 0;;) {
            const std::size_t tab = FindTab(line, start);
            fields_.push_back(line.substr(start, tab - start));
            if (tab == line.size()) break;
            start = tab + 1;
        }
        return true;
    }
    return false;
}

bool SenseReader::ReadHeader() {
    static constexpr std::string_view kNames[kColumnCount] = {
        "key", "ko", "pos", "sem", "gram", "dict", "rule", "ctx", "flags", "prio", "reading", "id",
    };
    const auto& fields = rows_.Fields();
    width_ = fields.size();
    // A copied column would be read once and ignored; say so rather than let it rot.
    for (std::size_t i = 0; i < fields.size(); i++) {
        for (std::size_t j = i + 1; j < fields.size(); j++) {
            if (fields[i] == fields[j]) {
                errors_.push_back({rows_.Line(), TsvErrorKind::DuplicateColumn});
                return false;
            }
        }
    }
    for (int c = 0; c < kColumnCount; c++) {
        index_[c] = -1;
        for (std::size_t i = 0; i < fields.size(); i++) {
            if (fields[i] == kNames[c]) {
                index_[c] = static_cast<int>(i);
                break;
            }
        }
    }
    const struct {
        Column column;
        TsvErrorKind kind;
    } required[] = {
        {kKey, TsvErrorKind::NoKeyColumn},
        {kKo, TsvErrorKind::NoKoColumn},
        {kPos, TsvErrorKind::NoPosColumn},
        {kId, TsvErrorKind::NoIdColumn},
    };
    for (const auto& r : required) {
        if (index_[r.column] < 0) {
            errors_.push_back({rows_.Line(), r.kind});
            return false;
        }
    }
    return true;
}

bool SenseReader::Next(Sense& out) {
    while (!stopped_) {
        if (!rows_.Next()) {
            if (rows_.Failed()) errors_.push_back({0, TsvErrorKind::Read});
            stopped_ = true;
            break;
        }
        const auto& fields = rows_.Fields();
        if (width_ == 0) {
            if (!ReadHeader()) stopped_ = true;
            continue;
        }
        const int line = rows_.Line();
        if (fields.size() > width_) {
            errors_.push_back({line, TsvErrorKind::TooManyColumns});
            continue;
        }
        std::string_view col[kColumnCount];
        for (int c = 0; c < kColumnCount; c++) {
            const int i = index_[c];
            if (i >= 0 && static_cast<std::size_t>(i) < fields.size()) col[c] = fields[static_cast<std::size_t>(i)];
        }

        Sense s;
        s.key = col[kKey];
        s.ko = col[kKo];
        s.pos = col[kPos];
        s.sem = col[kSem];
        s.gram = col[kGram];
        s.dict = col[kDict];
        // Of several errors in a row the last one is recorded.
        bool failed = false;
        TsvErrorKind kind = TsvErrorKind::BadNumber;
        if (!col[kRule].empty() && !internal::ParseInt32(col[kRule], s.rule)) failed = true;
        s.ctx = col[kCtx];
        if (!col[kPrio].empty() && !failed && !internal::ParseInt32(col[kPrio], s.prio)) failed = true;
        bool hidden = false;
        std::string_view flags = col[kFlags];
        for (std::string_view flag = NextWord(flags); !flag.empty(); flag = NextWord(flags)) {
            if (flag == "xxx") {
                s.xxx = true;
            } else if (flag == "kana") {
                if (col[kReading] != s.key) s.reading = col[kReading];
            } else if (flag == "hidden") {
                hidden = true;
            } else if (flag == "replace") {
                s.replace = true;
            } else if (flag != "llm") {
                failed = true;
                kind = TsvErrorKind::BadFlag;
            }
        }
        if (failed) {
            errors_.push_back({line, kind});
            continue;
        }
        if (s.key.empty() || col[kId].empty()) {
            errors_.push_back({line, TsvErrorKind::EmptyKeyOrId});
            continue;
        }
        if (hidden) continue;
        const std::string_view id = col[kId];
        const std::size_t colon = id.rfind(':');
        const std::string_view digits = colon == std::string_view::npos ? id : id.substr(colon + 1);
        if (!internal::ParseInt32(digits, s.id)) s.id = line;
        out = s;
        return true;
    }
    return false;
}

}  // namespace hdTrans::dict
