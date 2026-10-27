#include "gui/user_dicts.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gui/text_file.h"
#include "hdtest.h"

namespace {

namespace fs = std::filesystem;
namespace gui = hdTrans::gui;

struct TempFolder {
    fs::path path;

    TempFolder() {
        std::error_code error;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path(error) / ("hdtrans-dicts-" + std::to_string(stamp));
        if (error || !fs::create_directory(path, error)) path.clear();
    }

    ~TempFolder() {
        std::error_code error;
        if (!path.empty()) fs::remove_all(path, error);
    }
};

void WriteBytes(const fs::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string Utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

}  // namespace

HD_TEST(GuiUserDictRowsRoundTrip) {
    TempFolder folder;
    HD_REQUIRE(!folder.path.empty());
    const fs::path path = folder.path / "userdict.txt";
    WriteBytes(path, "\xEF\xBB\xBF# 주석\r\n魔導具\t마도구\r\n\r\nユキ\t유키\tname\t// 이름\r\n");
    gui::DictText text;
    HD_REQUIRE(gui::ReadDictText(path, text));
    HD_CHECK(text.bom && text.crlf && !text.column && text.lines.size() == 4);
    HD_CHECK(!gui::EntryLine(text.lines[0]) && gui::EntryLine(text.lines[1]) && !gui::EntryLine(text.lines[2]));
    const gui::DictRow row = gui::ParseRow(text.lines[3]);
    HD_CHECK(row.key == "ユキ" && row.ko == "유키" && row.pos == "name" && row.options.empty());
    text.lines[3] = gui::RowLine({"ユキ", "유키", "surname", ""}, text.lines[3]);
    text.lines[1] = gui::RowLine({"魔導具", "마\t도구", "", "prio=5"}, text.lines[1]);
    text.lines.push_back(gui::RowLine({"蒼月", "아오츠키", "", ""}));
    HD_REQUIRE(gui::WriteDictText(path, text));
    HD_CHECK_EQ(ReadBytes(path), std::string("\xEF\xBB\xBF# 주석\r\n魔導具\t마 도구\tn\tprio=5\r\n\r\n"
                                             "ユキ\t유키\tsurname\t// 이름\r\n蒼月\t아오츠키\r\n"));
    WriteBytes(path, "#! layer=user\nkey\tko\tpos\n");
    HD_CHECK(gui::ReadDictText(path, text) && text.column && !text.bom && !text.crlf);
}

HD_TEST(GuiUserDictToggleAndImport) {
    TempFolder folder;
    HD_REQUIRE(!folder.path.empty());
    const fs::path user = folder.path / "user";
    HD_REQUIRE(gui::UserDictFolder({Utf8(user)}) == user);
    std::error_code error;
    HD_REQUIRE(fs::create_directory(user, error));
    WriteBytes(user / "userdict.txt", "犬\t개\n");
    WriteBytes(user / "UserDict-b.txt", "猫\t고양이\n");
    WriteBytes(user / "notes.txt", "");
    const std::vector<std::string> sources{Utf8(user)};
    std::vector<gui::UserDict> dicts = gui::ListUserDicts(sources);
    HD_REQUIRE(dicts.size() == 2);
    HD_CHECK(dicts[0].path.filename() == "UserDict-b.txt" && dicts[1].path.filename() == "userdict.txt");
    HD_REQUIRE(gui::SetUserDictOn(dicts[1], false));
    HD_CHECK(!fs::exists(user / "userdict.txt", error) && fs::exists(user / "userdict.txt.off", error));
    dicts = gui::ListUserDicts(sources);
    HD_CHECK(dicts.size() == 2 && dicts[0].on && !dicts[1].on);
    HD_CHECK(gui::SetUserDictOn(dicts[1], true) && fs::exists(user / "userdict.txt", error));

    HD_CHECK(gui::UserDictPath(user, " 지명 ") == user / fs::path(u8"userdict-지명.txt"));
    HD_CHECK(gui::UserDictPath(user, "userdict-a.tsv") == user / "userdict-a.tsv");
    HD_CHECK(gui::UserDictPath(user, " ").empty());
    const fs::path source = folder.path / "names.txt";
    WriteBytes(source, "\x93\xFA\x96\x7B\t\x82\xA0\n");
    const fs::path imported = gui::ImportUserDict(source, user);
    std::string text;
    gui::TextEncoding encoding{};
    HD_CHECK(imported == user / "userdict-names.txt" && gui::ReadTextFile(imported, text, encoding) && text == "日本\tあ\n");
    HD_CHECK(gui::ImportUserDict(source, user) == user / "userdict-names-2.txt");

    std::vector<fs::path> files{folder.path / "a.txt"};
    gui::AppendFiles(files, {folder.path / "b.txt", folder.path / "." / "a.txt", folder.path / "b.txt"});
    HD_CHECK(files == (std::vector<fs::path>{folder.path / "a.txt", folder.path / "b.txt"}));
}

HD_TEST(GuiUserDictRowCheck) {
    struct Case {
        gui::DictRow row;
        gui::RowField field;
        const char* message;
    };
    const Case cases[] = {
        {{"鳥", "새", "", ""}, gui::RowField::Key, ""},
        {{"鳥", "새", "n", "prio=2"}, gui::RowField::Key, ""},
        {{"様", "", "hon", ""}, gui::RowField::Key, ""},
        {{"鳥", "", "", ""}, gui::RowField::Ko, "한국어가 비어 있습니다"},
        {{"鳥", "새", "zzz", ""}, gui::RowField::Pos, "알 수 없는 품사입니다: zzz"},
        {{"鳥", "새", "n", "prio=x"}, gui::RowField::Options, "옵션 오류: option \"prio=x\": \"x\" is not an integer"},
        {{"鳥", "새", "adj", ""}, gui::RowField::Pos, "품사와 맞지 않습니다: adj JP must be a dictionary form ending in い"},
    };
    for (const Case& test : cases) {
        const gui::RowProblem problem = gui::CheckRow(test.row);
        HD_CHECK_EQ(problem.message, std::string(test.message));
        if (*test.message) HD_CHECK(problem.field == test.field);
    }
}
