#include "core/dict/integrity.h"

#include <cstdint>
#include <algorithm>
#include <bit>
#include <cstring>

namespace hdTrans::dict::internal {
namespace {

constexpr std::array<uint32_t, 64> roundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

// Slicing-by-8: crcTables[k][b] is the CRC of byte b followed by k zero bytes, so eight bytes fold
// with eight lookups. crcTables[0] is the bytewise table.
constexpr auto crcTables = [] {
    std::array<std::array<uint32_t, 256>, 8> tables{};
    for (std::size_t i = 0; i < 256; ++i) {
        auto crc = static_cast<uint32_t>(i);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) != 0 ? 0x82f63b78u : 0u);
        tables[0][i] = crc;
    }
    for (std::size_t i = 0; i < 256; ++i) {
        for (std::size_t k = 1; k < tables.size(); ++k)
            tables[k][i] = (tables[k - 1][i] >> 8) ^ tables[0][tables[k - 1][i] & 0xffu];
    }
    return tables;
}();

uint32_t LoadLittle32(const std::byte* p) {
    return std::to_integer<uint32_t>(p[0]) | std::to_integer<uint32_t>(p[1]) << 8 |
           std::to_integer<uint32_t>(p[2]) << 16 | std::to_integer<uint32_t>(p[3]) << 24;
}

}  // namespace

void Sha256::Compress(const std::byte* block) {
    std::array<uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        for (std::size_t j = 0; j < 4; ++j)
            words[i] = (words[i] << 8) | std::to_integer<uint32_t>(block[i * 4 + j]);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const uint32_t x = words[i - 15], y = words[i - 2];
        const uint32_t s0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
        const uint32_t s1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + choose + roundConstants[i] + words[i];
        const uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::Update(std::span<const std::byte> data) {
    byteCount_ += static_cast<uint64_t>(data.size());
    while (!data.empty()) {
        if (buffered_ == 0 && data.size() >= buffer_.size()) {
            Compress(data.data());
            data = data.subspan(buffer_.size());
            continue;
        }
        const std::size_t n = std::min(data.size(), buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, data.data(), n);
        buffered_ += n;
        data = data.subspan(n);
        if (buffered_ == buffer_.size()) {
            Compress(buffer_.data());
            buffered_ = 0;
        }
    }
}

std::array<std::byte, 32> Sha256::Finish() {
    const uint64_t bitCount = byteCount_ << 3;
    buffer_[buffered_++] = std::byte{0x80};
    if (buffered_ > 56) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.end(), std::byte{0});
        Compress(buffer_.data());
        buffered_ = 0;
    }
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.begin() + 56, std::byte{0});
    for (std::size_t i = 0; i < 8; ++i)
        buffer_[63 - i] = static_cast<std::byte>(bitCount >> (i * 8));
    Compress(buffer_.data());
    std::array<std::byte, 32> digest{};
    for (std::size_t i = 0; i < digest.size(); ++i)
        digest[i] = static_cast<std::byte>(state_[i / 4] >> (24 - (i % 4) * 8));
    return digest;
}

std::array<std::byte, 32> Hash256(std::span<const std::byte> data) {
    Sha256 hash;
    hash.Update(data);
    return hash.Finish();
}

uint32_t Crc32c(std::span<const std::byte> data) {
    const auto& t = crcTables;
    uint32_t crc = 0xffffffffu;
    const std::byte* p = data.data();
    std::size_t n = data.size();
    for (; n >= 8; p += 8, n -= 8) {
        const uint32_t lo = crc ^ LoadLittle32(p), hi = LoadLittle32(p + 4);
        crc = t[7][lo & 0xffu] ^ t[6][(lo >> 8) & 0xffu] ^ t[5][(lo >> 16) & 0xffu] ^ t[4][lo >> 24] ^
              t[3][hi & 0xffu] ^ t[2][(hi >> 8) & 0xffu] ^ t[1][(hi >> 16) & 0xffu] ^ t[0][hi >> 24];
    }
    for (; n > 0; ++p, --n)
        crc = (crc >> 8) ^ t[0][(crc ^ std::to_integer<uint32_t>(*p)) & 0xffu];
    return ~crc;
}

}  // namespace hdTrans::dict::internal
