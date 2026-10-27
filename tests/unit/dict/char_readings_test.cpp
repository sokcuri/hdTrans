#include <cstdint>
#include "core/dict/char_readings.h"

#include "hdtest.h"

using hdTrans::dict::Reading;
using hdTrans::dict::Written;

HD_TEST(char_readings_sample) {
    struct Case {
        char32_t ch;
        char32_t reading;  // 0: none
        char32_t written;  // 0: none
    };
    const Case cases[] = {
        {U'一', U'일', U'일'},
        {U'不', U'부', U'불'},
        {U'予', U'여', U'예'},
        {U'兩', U'량', U'양'},
        {U'樂', U'악', U'요'},
        {U'鹿', U'록', U'녹'},
        {U'六', U'륙', U'육'},
        {U'龍', U'룡', U'용'},
        {U'金', U'김', U'금'},
        {U'豈', U'기', U'개'},
        {U'國', U'국', U'국'},
        {U'串', U'곶', U'곶'},
        {U'国', 0, U'국'},  // written only
        {U'学', 0, U'학'},
        {U'乗', 0, U'승'},
        {U'噛', 0, U'교'},
        {U'嚙', 0, U'교'},
        {U'蕙', U'혜', U'혜'},
        {U'曩', 0, U'낭'},
        {U'々', 0, U'ː'},
        {U'〇', 0, U'○'},
        {U'・', 0, U'·'},
        {0xF900, U'개', 0},  // reading only (a compatibility ideograph)
        {U'가', 0, 0},       // neither
    };
    for (const auto& c : cases) {
        const auto r = Reading(c.ch);
        const auto w = Written(c.ch);
        HD_CHECK_EQ(r.has_value(), c.reading != 0);
        HD_CHECK_EQ(w.has_value(), c.written != 0);
        if (r) HD_CHECK_EQ(static_cast<uint32_t>(*r), static_cast<uint32_t>(c.reading));
        if (w) HD_CHECK_EQ(static_cast<uint32_t>(*w), static_cast<uint32_t>(c.written));
    }
    HD_CHECK(!Reading(0).has_value());
    HD_CHECK(!Written(0x10FFFF).has_value());
}

HD_TEST(char_readings_table) {
    const auto table = hdTrans::dict::internal::CharReadingTable();
    size_t reading = 0, written = 0, differ = 0;
    for (size_t i = 1; i < table.size(); i++) HD_CHECK(table[i - 1].ch < table[i].ch);
    for (const auto& r : table) {
        HD_CHECK(r.reading != 0 || r.written != 0);
        reading += r.reading != 0;
        written += r.written != 0;
        differ += r.reading != 0 && r.written != 0 && r.reading != r.written;
    }
    HD_CHECK_EQ(table.size(), size_t{8250});
    HD_CHECK_EQ(reading, size_t{4888});
    HD_CHECK_EQ(written, size_t{7853});
    HD_CHECK_EQ(differ, size_t{75});
}
