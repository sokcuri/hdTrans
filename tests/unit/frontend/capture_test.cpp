// The capture of host lines: distinct lines once, limits, threads and unwritable paths.
#include "frontend/capture.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "hdtest.h"

namespace {

using hdTrans::frontend::Capture;

std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// A folder with a non-ASCII name, removed with its files.
struct TempFolder {
    std::filesystem::path folder;
    TempFolder() {
        std::error_code error;
        folder = std::filesystem::temp_directory_path(error) /
                 PathOf("hdtrans-캡처-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(folder, error);
    }
    ~TempFolder() {
        std::error_code error;
        std::filesystem::remove_all(folder, error);
    }
    std::string File(std::string_view name) const { return Utf8(folder / PathOf(name)); }
};

std::string Read(const std::string& path) {
    std::ifstream in(PathOf(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Exists(const std::string& path) {
    std::error_code error;
    return std::filesystem::exists(PathOf(path), error);
}

}  // namespace

HD_TEST(CaptureWritesEachLineOnce) {
    TempFolder temp;
    const std::string path = temp.File("capture/게임.txt");
    {
        Capture capture(path);
        HD_CHECK(!Exists(path));  // nothing written yet
        capture.Add("「おはよう」");
        capture.Add("「おはよう」");
        capture.Add("  ルル「またね」\r\n\nplain ASCII\n「おはよう」\n猫");
        capture.Add("");
    }
    HD_CHECK_EQ(Read(path), std::string("「おはよう」\nルル「またね」\n猫\n"));
    // A later session counts the lines the file holds.
    {
        Capture capture(path);
        capture.Add("猫");
        capture.Add("犬");
    }
    HD_CHECK_EQ(Read(path), std::string("「おはよう」\nルル「またね」\n猫\n犬\n"));
}

HD_TEST(CaptureStopsAtItsLimits) {
    TempFolder temp;
    const std::string bytes = temp.File("bytes.txt");
    {
        Capture capture(bytes, 10);  // room for one line of three kana and its newline
        capture.Add("あいう");
        capture.Add("かきく");
        capture.Add("さ");  // would fit, but the capture has stopped
    }
    HD_CHECK_EQ(Read(bytes), std::string("あいう\n"));
    {
        Capture capture(bytes, 10);
        capture.Add("た");
    }
    HD_CHECK_EQ(Read(bytes), std::string("あいう\n"));
    const std::string lines = temp.File("lines.txt");
    {
        Capture capture(lines, 1 << 20, 2);
        capture.Add("一\n二\n三\n四");
    }
    HD_CHECK_EQ(Read(lines), std::string("一\n二\n"));
}

HD_TEST(CaptureFromThreads) {
    TempFolder temp;
    const std::string path = temp.File("threads.txt");
    constexpr int kThreads = 8, kLines = 300;
    {
        Capture capture(path);
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; t++) {
            threads.emplace_back([&capture, t] {
                // Each thread sends the shared lines and its own.
                for (int i = 0; i < kLines; i++) {
                    capture.Add("共通" + std::to_string(i));
                    capture.Add("個別" + std::to_string(t) + "-" + std::to_string(i));
                }
            });
        }
        for (std::thread& thread : threads) thread.join();
    }
    const std::string text = Read(path);
    std::set<std::string> seen;
    std::size_t count = 0;
    for (std::size_t start = 0; start < text.size();) {
        const std::size_t nl = text.find('\n', start);
        HD_REQUIRE(nl != std::string::npos);
        seen.insert(text.substr(start, nl - start));
        count++;
        start = nl + 1;
    }
    HD_CHECK_EQ(count, static_cast<std::size_t>(kLines + kThreads * kLines));
    HD_CHECK_EQ(seen.size(), count);
    HD_CHECK(seen.contains("共通0") && seen.contains("個別7-299"));
}

HD_TEST(CaptureIntoAnUnwritablePath) {
    TempFolder temp;
    const std::string blocker = temp.File("file");
    std::ofstream(PathOf(blocker), std::ios::binary) << "x";
    // The folder of the capture would be inside a file.
    const std::string path = temp.File("file/capture/host.txt");
    Capture capture(path);
    capture.Add("猫");
    capture.Add("犬");
    HD_CHECK(!Exists(path));
    HD_CHECK_EQ(Read(blocker), std::string("x"));
}

HD_TEST(CaptureOffByDefault) {
    TempFolder temp;
    HD_CHECK(hdTrans::frontend::OpenCapture(false, Utf8(temp.folder)) == nullptr);
    const auto on = hdTrans::frontend::OpenCapture(true, Utf8(temp.folder));
    HD_REQUIRE(on != nullptr);
    HD_CHECK(on->Path().ends_with(".txt"));
    HD_CHECK(on->Path().find("capture") != std::string::npos);
    HD_CHECK(!Exists(on->Path()));
}

HD_TEST(CaptureReplacedOnlyForAnotherFile) {
    TempFolder temp;
    std::unique_ptr<Capture> current = std::make_unique<Capture>(temp.File("a.txt"));
    const Capture* first = current.get();
    hdTrans::frontend::ReplaceCapture(current, std::make_unique<Capture>(temp.File("a.txt")));
    HD_CHECK(current.get() == first);
    hdTrans::frontend::ReplaceCapture(current, std::make_unique<Capture>(temp.File("b.txt")));
    HD_CHECK(current->Path() == temp.File("b.txt"));
    hdTrans::frontend::ReplaceCapture(current, nullptr);
    HD_CHECK(current == nullptr);
}
