#include "core/dict/layer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "hdtest.h"

using hdTrans::dict::internal::Row;
using hdTrans::dict::internal::Rows;

// Rows grow from heap blocks into page blocks and back, and keep their order.
HD_TEST(LayerRowsAcrossPageBlocks) {
    constexpr std::size_t kCount = 3 * ((std::size_t{1} << 20) / sizeof(Row));
    Rows rows;
    for (std::size_t i = 0; i < kCount; i++) rows.push_back({{}, {}, static_cast<int32_t>(i % 7), 0, 0, static_cast<int32_t>(i)});
    HD_REQUIRE(rows.size() == kCount);
    for (std::size_t i = 0; i < kCount; i++) HD_REQUIRE(rows[i].id == static_cast<int32_t>(i));
    std::erase_if(rows, [](const Row& r) { return r.code != 0; });
    rows.shrink_to_fit();
    HD_CHECK_EQ(rows.size(), (kCount + 6) / 7);
    for (std::size_t i = 0; i < rows.size(); i++) HD_REQUIRE(rows[i].id == static_cast<int32_t>(7 * i));
    Rows small(rows.begin(), rows.begin() + 10);
    rows.clear();
    rows.shrink_to_fit();
    HD_CHECK_EQ(small.back().id, 63);
    Rows reserved;
    reserved.reserve(2 * kCount);
    reserved.push_back(small.front());
    HD_CHECK_EQ(reserved.front().id, 0);
}
