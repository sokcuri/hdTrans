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
#include "core/names/document.h"
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
    names::List parsed;
    HD_REQUIRE(names::ParseList(kNames, parsed));
    std::unique_ptr<pipeline::DocumentNames> list;
    HD_REQUIRE(pipeline::CompileNames(*loaded.snapshot, parsed, list));
    auto translate = [&](const std::string& line, const pipeline::DocumentNames* names) {
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
        {"手を振るみみちゃん。", "손을 흔드는 미미짱."},  // after a verb that modifies it
        {"笑ったみみちゃんが来た。", "웃은 미미짱이 왔다."},
        {"謝るみみちゃん。", "사과하는 미미짱."},
    };
    for (const auto& [line, want] : cases) HD_CHECK_EQ(translate(line, list.get()), want);

    // The listed names are the document's name rows (docs/packs.md): id names:LINE, a hon name
    // only before an honorific.
    std::string rows;
    for (const dict::PackRow& r : list->packs.rows) {
        rows += r.id + " " + r.key + " " + r.target + " " + std::to_string(r.atoms.size()) + "\n";
    }
    HD_CHECK_EQ(rows, std::string("names:1 睦月 무츠키 0\nnames:2 若葉 와카바 0\nnames:3 暁 아카츠키 0\n"
                                  "names:4 黒沢 쿠로사와 0\nnames:5 恋 렌 1\nnames:6 みみ 미미 1\nnames:7 星見町 호시미초 0\n"));
    // A hon name the lines show standing alone is a name there too.
    const std::vector<std::string> alone = {"みみ「おはよう」", "みみ「またね」", "みみ「うん」", "みみが来た。"};
    internal::TranslationOptions named;
    named.names = list.get();
    HD_CHECK_EQ(internal::TranslateDocument(*loaded.snapshot, *generated, alone, {}, named).back(),
                std::string("미미가 왔다."));
    HD_CHECK_EQ(translate("みみが来た。", list.get()), std::string("귀가 왔다."));

    // A listed kana name starts where a name starts, and the guards of nicknames hold for it: a
    // shorter listed name does not split a longer one, nor a verb before くんだ; ちゃん and と
    // after it are the honorific and the particle.
    names::List kanaParsed;
    HD_REQUIRE(names::ParseList("ゆき\t유키\nある\t아루\nしろ\t시로\tnick\n", kanaParsed));
    std::unique_ptr<pipeline::DocumentNames> kana;
    HD_REQUIRE(pipeline::CompileNames(*loaded.snapshot, kanaParsed, kana));
    HD_CHECK_EQ(translate("みゆきちゃん", kana.get()), std::string("미유키짱"));
    HD_CHECK_EQ(translate("まだあるくんだ", kana.get()), std::string("아직 걷는 거야"));
    HD_CHECK_EQ(translate("それでゆきちゃん", kana.get()), std::string("그래서 유키짱"));
    HD_CHECK_EQ(translate("それでゆきちゃん", nullptr), std::string("그래서 가"));
    HD_CHECK_EQ(translate("ともゆきくんが来た。", kana.get()), std::string("토모유키 군이 왔다."));
    HD_CHECK_EQ(translate("あのゆきちゃんが来た。", kana.get()), std::string("그 유키짱이 왔다."));
    HD_CHECK_EQ(translate("今日はゆきちゃんと遊んだ。", kana.get()), std::string("오늘은 유키짱과 놀았다."));
    HD_CHECK_EQ(translate("しろちゃんと遊ぶ。", kana.get()), std::string("시로짱과 논다."));

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
    HD_CHECK_EQ(session.Translate("若葉は笑った。\r\n睦月は笑った。\r\n"), std::string("와카바는 웃었다.\r\n1월은 웃었다.\r\n"));
    HD_CHECK(session.SetNames(kNames));
    HD_CHECK_EQ(session.Translate("恋が来た。", "恋「おはよう」\n恋「またね」\n恋「うん」"), std::string("렌이 왔다."));
    HD_CHECK_EQ(session.Translate("恋が来た。"), std::string("사랑이 왔다."));
    session.SetWrapped(true);
    HD_CHECK_EQ(session.Translate("睦月は\n笑った。"), std::string("무츠키는\n웃었다."));
    session.SetWrapped(false);
    // A JA the pack rows cannot hold is a malformed line.
    HD_CHECK(!session.SetNames("若葉\t와카바\n睦月|暁\t무츠키"));
    HD_CHECK_EQ(session.Translate("若葉は笑った。"), std::string("와카바는 웃었다."));
    HD_CHECK(session.SetNames(""));
    HD_CHECK_EQ(session.Translate("若葉は笑った。"), std::string("새잎은 웃었다."));
}
