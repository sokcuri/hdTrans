// Anemone's AneDic.txt read as Ehnd UserDict text.
#include <string>
#include <vector>

#include "core/filter/ehnd.h"
#include "core/filter/filter.h"
#include "hdtest.h"

HD_TEST(AneDicLines) {
    // The file as Anemone writes it: a UTF-8 mark, CR LF, its comment header.
    const std::string text = hdTrans::filter::EhndText(
        "\xEF\xBB\xBF// 아네모네 사전파일 V1\r\n//\r\n雪々\t유키\t1\tVHn\r\nお疲れ様\t수고했어\t0\r\n"
        "強制\t강제\t2\r\n門前\t문 앞\r\n一語\r\n\t빈 키\t1\r\n");
    std::vector<hdTrans::filter::Error> errors;
    const auto rows = hdTrans::filter::ParseUserDict(text, "AneDic.txt", errors);
    HD_REQUIRE(rows.size() == 4);
    HD_CHECK_EQ(rows[0].key, "雪々");
    HD_CHECK_EQ(rows[0].attr, "VHn");
    HD_CHECK(!rows[0].phrase);
    HD_CHECK(rows[1].phrase);
    HD_CHECK(rows[2].phrase);
    HD_CHECK(!rows[3].phrase);
    HD_REQUIRE(errors.size() == 2);
    HD_CHECK_EQ(errors[0].line, 7);
    HD_CHECK_EQ(errors[1].line, 8);
}

HD_TEST(AneDicUtf16) {
    // UTF-16LE after its byte order mark, as an editor may save it.
    const std::string bytes("\xFF\xFE" "\x6A\x30\x09\x00\x98\xB0\x09\x00" "0\x00\r\x00\n\x00", 16);
    std::vector<hdTrans::filter::Error> errors;
    const auto rows = hdTrans::filter::ParseUserDict(hdTrans::filter::EhndText(bytes), "AneDic.txt", errors);
    HD_REQUIRE(rows.size() == 1);
    HD_CHECK_EQ(rows[0].key, "な");
    HD_CHECK_EQ(rows[0].target, "나");
    HD_CHECK(rows[0].phrase);
    HD_CHECK(errors.empty());
}
