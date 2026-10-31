// The name list of a document (Session::SetNames): listed names over the dictionary's reading of
// the same surface, under longer words, with the particles, spacing and honorifics of person
// names. Needs HDTRANS_DICTS.
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "core/dict/folders.h"
#include "core/engine.h"
#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

const std::string kNames =
    "睦月\t무츠키\n"
    "若葉\t와카바\n"
    "暁\t아카츠키\n"
    "黒沢\t쿠로사와\tfamily\n"
    "恋\t렌\tgiven\thon\n"
    "みみ\t미미\tnick\thon\n"
    "星見町\t호시미초\tplace\n";

}  // namespace

HD_TEST(engine_document_names) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    names::List list;
    HD_REQUIRE(names::ParseList(kNames, list));
    auto translate = [&](const std::string& line, const names::List* names) {
        internal::TranslationOptions options;
        options.names = names;
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, options);
        return out.empty() ? std::string() : out[0];
    };
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"睦月は笑った。", "무츠키는 웃었다."},  // a month name
        {"若葉は笑った。", "와카바는 웃었다."},
        {"暁", "아카츠키"},                       // alone on its line
        {"若葉マークだ。", "초보운전자 마크다."},  // a longer word
        {"恋は大事だ。", "사랑은 소중하다."},     // hon: the word where no honorific follows
        {"恋さんに渡した。", "렌 씨에게 건네주었다."},
        {"黒沢の代わりに行く。", "쿠로사와 대신에 간다."},
        {"黒沢さんに渡した。", "쿠로사와 씨에게 건네주었다."},
        {"黒沢先輩が来た。", "쿠로사와 선배가 왔다."},
        {"黒沢暁が来た。", "쿠로사와 아카츠키가 왔다."},
        {"星見町で会った。", "호시미초에서 만났다."},
        // A listed kana name before an honorific inside a kana run.
        {"それじゃ、若葉にはみみちゃんから話して。", "그럼, 와카바에게는 미미짱에게서 이야기해 줘."},
        {"もしもしみみちゃん？", "여보세요 미미짱?"},
        {"察したみみちゃんが来た。", "헤아린 미미짱이 왔다."},
    };
    for (const auto& [line, want] : cases) HD_CHECK_EQ(translate(line, &list), want);
    HD_CHECK_EQ(translate("睦月は笑った。", nullptr), std::string("1월은 웃었다."));
    HD_CHECK_EQ(translate("もしもしみみちゃん？", nullptr), std::string("여보세요 귀짱?"));

    // The session's list applies to each later call; empty text drops it.
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.Open({dicts}));
    Session session(engine);
    HD_CHECK(session.SetNames(kNames));
    HD_CHECK_EQ(session.Translate("睦月は笑った。"), std::string("무츠키는 웃었다."));
    HD_CHECK_EQ(session.Translate("暁が来た。"), std::string("아카츠키가 왔다."));
    HD_CHECK(!session.SetNames("睦月\n若葉\t와카바"));  // a malformed line; the other is kept
    HD_CHECK_EQ(session.Translate("若葉は笑った。\n睦月は笑った。"), std::string("와카바는 웃었다.\n1월은 웃었다."));
    HD_CHECK(session.SetNames(""));
    HD_CHECK_EQ(session.Translate("若葉は笑った。"), std::string("새잎은 웃었다."));
}
