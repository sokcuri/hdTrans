#include "core/dict/integrity.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "hdtest.h"

using hdTrans::dict::internal::Crc32c;
using hdTrans::dict::internal::Hash256;
using hdTrans::dict::internal::Sha256;

namespace {

std::span<const std::byte> Bytes(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

std::string Hex(const std::array<std::byte, 32>& digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (std::byte b : digest) {
        const auto value = std::to_integer<unsigned int>(b);
        out += digits[value >> 4];
        out += digits[value & 15u];
    }
    return out;
}

}  // namespace

HD_TEST(integrity_sha256_vectors) {
    const struct {
        std::string_view text;
        std::string_view want;
    } cases[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
         "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
    };
    for (const auto& c : cases) {
        const auto data = Bytes(c.text);
        HD_CHECK_EQ(Hex(Hash256(data)), std::string(c.want));
        for (std::size_t split = 0; split <= data.size(); ++split) {
            Sha256 hash;
            hash.Update({});
            hash.Update(data.first(split));
            hash.Update({});
            hash.Update(data.subspan(split));
            hash.Update({});
            HD_CHECK_EQ(Hex(hash.Finish()), std::string(c.want));
        }
    }
}

HD_TEST(integrity_sha256_repeated_input) {
    const struct {
        std::size_t length;
        std::string_view want;
    } cases[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    };
    constexpr std::size_t chunks[] = {1, 7, 55, 56, 63, 64, 65, 127, 4096};
    for (const auto& c : cases) {
        const std::string text(c.length, 'a');
        const auto data = Bytes(text);
        HD_CHECK_EQ(Hex(Hash256(data)), std::string(c.want));
        for (std::size_t chunk : chunks) {
            Sha256 hash;
            for (std::size_t pos = 0; pos < data.size();) {
                const auto n = std::min(chunk, data.size() - pos);
                hash.Update(data.subspan(pos, n));
                pos += n;
            }
            HD_CHECK_EQ(Hex(hash.Finish()), std::string(c.want));
        }
    }
}

HD_TEST(integrity_crc32c_vectors) {
    HD_CHECK_EQ(Crc32c({}), 0u);
    HD_CHECK_EQ(Crc32c(Bytes("123456789")), 0xe3069283u);
    std::array<std::byte, 32> data{};
    HD_CHECK_EQ(Crc32c(data), 0x8a9136aau);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::byte>(i);
    HD_CHECK_EQ(Crc32c(data), 0x46dd794eu);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::byte>(31 - i);
    HD_CHECK_EQ(Crc32c(data), 0x113fdb5cu);
    data.fill(std::byte{0xff});
    HD_CHECK_EQ(Crc32c(data), 0x62a8ab43u);
}

namespace {

// The CRC one bit at a time, against which the table-driven Crc32c is checked.
uint32_t BitwiseCrc32c(std::span<const std::byte> data) {
    uint32_t crc = 0xffffffffu;
    for (std::byte b : data) {
        crc ^= std::to_integer<uint32_t>(b);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) != 0 ? 0x82f63b78u : 0u);
    }
    return ~crc;
}

}  // namespace

HD_TEST(integrity_crc32c_matches_bitwise) {
    std::vector<std::byte> buffer(4096 + 16);
    uint64_t state = 0x9e3779b97f4a7c15u;
    for (std::byte& b : buffer) {
        state = state * 6364136223846793005u + 1442695040888963407u;
        b = static_cast<std::byte>(state >> 56);
    }
    const std::span<const std::byte> all(buffer);
    for (std::size_t offset = 0; offset < 8; ++offset) {
        for (std::size_t size = 0; size <= 64; ++size)
            HD_CHECK_EQ(Crc32c(all.subspan(offset, size)), BitwiseCrc32c(all.subspan(offset, size)));
        for (const std::size_t size : {std::size_t{255}, std::size_t{1000}, std::size_t{4096}})
            HD_CHECK_EQ(Crc32c(all.subspan(offset, size)), BitwiseCrc32c(all.subspan(offset, size)));
    }
}
