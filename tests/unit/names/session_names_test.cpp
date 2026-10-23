#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kBase =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "かすみ\t안개\tN110\t\t\tGN\t0\t1\n"
    "桃花\t복숭아꽃\tN110\t\t\tGN\t0\t2\n"
    "は\t(은)는\tP211\t\t\tGN\t0\t3\n";

constexpr std::string_view kNames =
    "#! layer=names\n"
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "かすみ\t카스미\tN130\tQPNVHn\tWb\tGN\t0\t4\n"
    "桃花\t모모카\tN130\tQPNVHn\tWb\tGN\t0\t5\n";

struct NameSession {
    std::shared_ptr<const dict::Snapshot> snap;
    std::shared_ptr<internal::Generated> generated;
    internal::Memory memory;

    NameSession() {
        static const auto loaded = dict::Load({{0, "base.tsv", std::string(kBase)}, {0, "names.tsv", std::string(kNames)}});
        HD_CHECK(loaded.errors.empty());
        snap = loaded.snapshot;
        if (snap != nullptr) generated = internal::NewGenerated(*snap);
    }

    // The translation of the lines of one call, joined with "\n".
    std::string Call(const std::vector<std::string>& lines) {
        if (snap == nullptr) return {};
        std::string out;
        for (const std::string& line : internal::TranslateDocument(*snap, *generated, lines, memory, {}, {})) {
            if (!out.empty()) out += "\n";
            out += line;
        }
        return out;
    }
};

}  // namespace

// A name that honorifics have shown in earlier calls of a session reads as the person where
// it stands alone in a later call; a common word needs four honorifics.
HD_TEST(names_session_reuses_called_names) {
    {
        NameSession s;
        HD_CHECK(s.Call({"かすみは"}).find("안개") != std::string::npos);
        s.Call({"かすみちゃん、", "かすみちゃん、", "かすみちゃん、"});
        HD_CHECK(s.Call({"かすみは"}).find("안개") != std::string::npos);
        s.Call({"かすみちゃん、"});
        HD_CHECK(s.Call({"かすみは"}).find("카스미") != std::string::npos);
    }
    {
        NameSession s;
        HD_CHECK(s.Call({"桃花は"}).find("복숭아꽃") != std::string::npos);
        s.Call({"桃花さん、", "桃花さん、", "桃花さん、", "桃花さん、"});
        HD_CHECK(s.Call({"桃花は"}).find("모모카") != std::string::npos);
    }
}
