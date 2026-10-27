// The name cues of a line (names::NameCues) and the cued-name constraint rule.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/names/document.h"
#include "core/names/name_cues.h"
#include "core/names/ruby.h"
#include "core/pipeline/protect.h"
#include "core/pipeline/segment.h"
#include "core/text/hangul.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

const dict::Snapshot* Dictionary() {
    static const dict::LoadResult loaded = dict::Load({
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "村雨\t소나기\tN110\tVFr\t\tGN\t1\n"
         "時雨\t늦가을 비\tN110\tVFr\t\tGN\t2\n"
         "睦月\t1월\tN130\tUMe\t\tGN\t3\n"
         "明日\t내일\tN110\tUMe\t\tGN\t4\n"
         "夕立ち\t소나기\tN110\tVFr\t\tGN\t5\n"
         "夕立\t소나기\tN110\tVFr\t\tGN\t6\n"
         "一番\t제일\tN110\t\t\tGN\t7\n"
         "義姉\t시누이\tN110\tVHn\t\tGN\t8\n"
         "アヤメ\t붓꽃\tN110\tQAL\t\tGN\t9\n"
         "雷\t번개\tN110\tVFr\t\tGN\t10\n"
         "高峰\t고봉\tN110\t\t\tGN\t22\n"
         "潮\t조수\tN110\t\t\tGN\t26\n"
         "風\t바람\tN110\tVFr\t\tGN\t27\n"
         "元気\t건강%A1@\tG8L0\t\t\tGN\t23\n"
         "つむ\t뽑&AW@\tM425\t\t\tGN\t30\n"
         "ここ\t여기\tN120\t\t\tGN\t31\n"
         "いつか\t언젠가\tE300\t\t\tGN\t32\n"
         "続\t계속&C6@\tM411\t\t\tGN\t38\n"},
        {0, "names.tsv",
         "#! layer=names\n"
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "村雨\t무라사메\tN130\tQPNVHn\tWb\tGN\t11\n"
         "時雨\t시구레\tN130\tQPNVHn\tWb\tGN\t12\n"
         "睦月\t무츠키\tN130\tQPNVHn\tWb\tGN\t13\n"
         "明日\t아스카\tN130\tQPNVHn\tWb\tGN\t14\n"
         "夕立\t유다치\tN130\tQPNVHn\tWb\tGN\t15\n"
         "一番\t이치반\tN130\tQPNVHn\tWb\tGN\t16\n"
         "義\t요시\tN130\tQPNVHn\tWb\tGN\t17\n"
         "アヤメ\t아야메\tN130\tQPNVHn\tWb\tGN\t18\n"
         "まるゆ\t마루유\tN130\tQPNVHn\tWb\tGN\t19\n"
         "鈴谷\t스즈야\tN130\tQPNVHn\tWb\tGN\t20\n"
         "雷\t이카즈치\tN130\tQPNVHn\tWb\tGN\t21\n"
         "高峰\t타카미네\tN130\tQPNVHn\tWa\tGN\t24\n"
         "潮\t우시오\tN130\tQPNVHn\tWb\tGN\t28\n"
         "風\t펑\tN130\tQPNVHn\tWb\tGN\t29\n"
         "元気\t겐키\tN130\tQPNVHn\tWb\tGN\t25\n"
         "つむぎ\t츠무기\tN130\tQPNVHn\tWb\tGN\t33\n"
         "ここね\t코코네\tN130\tQPNVHn\tWb\tGN\t34\n"
         "いつか\t이츠카\tN130\tQPNVHn\tWb\tGN\t35\n"
         "いくよ\t이쿠요\tN130\tQPNVHn\tWb\tGN\t36\n"
         "続\t츠즈키\tN130\tQPNVHn\tWa\tGN\t37\n"},
    });
    return loaded.snapshot.get();
}

}  // namespace

HD_TEST(names_name_cues) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    // + strong, ~ recurring (strong where two lines of the document give it), - weak.
    using Cues = std::vector<std::pair<std::string, char>>;
    const std::vector<std::pair<std::string, Cues>> cases = {
        // Weather words and words of time hold in their line, and in the document where two lines
        // give them such a cue.
        {"駆逐艦村雨、出撃", {{"村雨", '~'}}},
        {"白露型駆逐艦「村雨」だよ", {{"村雨", '~'}}},
        {"二番艦の時雨だよ", {{"時雨", '~'}}},
        {"村雨姉さん！", {{"村雨", '~'}}},
        {"睦月型も頑張る", {{"睦月", '~'}}},
        {"時雨「行くよ」", {{"時雨", '~'}}},
        {"時雨、出撃するよ。", {{"時雨", '~'}}},
        {"夕立ちゃん、遅いよ", {{"夕立", '-'}}},
        // Strong cues of a plain noun hold through the document.
        {"高峰「行くよ」", {{"高峰", '+'}}},
        // An honorific holds in its line; the name registry weighs honorifics across lines.
        {"高峰君、待って", {{"高峰", '-'}}},
        {"戦艦高峰です", {{"高峰", '+'}}},
        {"アヤメと名乗った", {{"アヤメ", '+'}}},
        {"聖女アヤメの旅", {{"アヤメ", '+'}}},
        {"元気、出撃！", {}},
        // Weak cues.
        {"睦月です。", {{"睦月", '-'}}},
        {"だから…まるゆです。", {{"まるゆ", '-'}}},
        {"潮です。", {{"潮", '-'}}},
        {"雷よ！", {}},
        {"「アヤメ、来て」", {}},
        {"「大丈夫か、アヤメ」", {{"アヤメ", '-'}}},
        {"「ありがとう、高峰」", {{"高峰", '-'}}},
        {"「高峰、テントは？」", {}},
        {"「鈴谷」「村雨」「夕立」の旗艦", {{"鈴谷", '-'}, {"村雨", '-'}, {"夕立", '-'}}},
        // Not cued: a time word called at the start, a number after a ship type, a longer
        // word, a weak shape inside a sentence, a single kanji called.
        {"明日、行こう", {}},
        {"駆逐艦一番艦", {}},
        {"義姉さん", {}},
        {"これは睦月です。", {}},
        {"雷、落ちた", {}},
        {"時雨が降る", {}},
        {"高峰、テントは？", {}},
        {"げ、アヤメ！？", {}},
        {"海防艦計3隻", {}},
        {"32村雨改二", {}},
        // A hiragana name that starts a verb is cued only by a call at the start of an utterance
        // or a self-introduction, also after a comma; not a word of the dictionary or a word or
        // predicate closed by a final particle.
        {"つむぎ、来て", {{"つむぎ", '-'}}},
        {"「わたし、つむぎです」", {{"つむぎ", '-'}}},
        {"糸をつむぎ、織る", {}},
        {"「ここね、待って」", {}},
        {"「いくよ、待って」", {}},
        {"いつか、行こう", {}},
        // Caption labels: a part of a label in round brackets before speech, a label that ≫ closes,
        // and the label shapes after caption marks. A word in brackets alone, inside a sentence or
        // with more than a name and an honorific in its brackets is none.
        {"（アヤメ）待って", {{"アヤメ", '+'}}},
        {"⚟（アヤメ）待って", {{"アヤメ", '+'}}},
        {"[無線](高峰)了解", {{"高峰", '+'}}},
        {"(高峰／アヤメ)はい", {{"高峰", '+'}, {"アヤメ", '+'}}},
        {"(高峰さん)はい", {{"高峰", '+'}}},
        {"アヤメ≫待って", {{"アヤメ", '+'}}},
        {"≫アヤメ：待って", {{"アヤメ", '+'}}},
        {"》高峰：はい", {{"高峰", '+'}}},
        {"(潮)はい", {{"潮", '-'}}},
        {"(つむぎ)はい", {{"つむぎ", '~'}}},
        {"（アヤメ）", {}},
        {"（アヤメ）と呼ばれた", {}},
        {"（アヤメ）は、きれい", {}},
        {"(続)航空火力艦の運用", {}},
        {"花の(アヤメ)だ", {}},
        {"(高峰の母)はい", {}},
        // A person sense that is no Japanese reading cues nothing.
        {"駆逐艦風", {}},
    };
    for (const auto& [text, want] : cases) {
        Cues got;
        for (const names::NameCue& c : names::NameCues(*snap, text)) {
            got.emplace_back(c.key, c.strong ? '+' : c.recurring ? '~' : '-');
        }
        auto show = [&](const Cues& cues) {
            std::string out = text + " ->";
            for (const auto& [k, kind] : cues) out += " " + k + kind;
            return out;
        };
        HD_CHECK_EQ(show(got), show(want));
    }
}

HD_TEST(names_caption_reading_cue_passage) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::string> lines = {"[無線]（高峰(たかみね)）では皆さん", "（恭(きょう)）はい"};
    const auto doc = pipeline::BuildDocument(*snap, lines);
    HD_REQUIRE(doc != nullptr);
    HD_CHECK(doc->cued.empty());
    names::internal::RubyMatch reading;
    HD_REQUIRE(names::internal::FindReading(lines[0], 0, reading));
    HD_CHECK_EQ(reading.begin, std::string("[無線]（").size());
    HD_CHECK_EQ(reading.end, std::string("[無線]（高峰(たかみね)").size());
    HD_CHECK(reading.base == "高峰" && reading.reading == "たかみね");

    const auto facts = pipeline::ReadLine(*doc, lines[0]);
    HD_CHECK_EQ(facts.text, "[無線]（타카미네）では皆さん");
    HD_CHECK(facts.cued == std::vector<std::string>{"高峰"});
    const auto lookup = pipeline::LookupOf(*doc, facts);
    HD_CHECK(lookup.cued.contains("高峰"));
    const auto ctx = pipeline::ContextOf(*doc, lookup);
    HD_REQUIRE(ctx.lookup.cued != nullptr);
    HD_CHECK(ctx.lookup.cued->contains("高峰"));
    const auto chunks = pipeline::Segment({{facts.text, output::SpanText}});
    HD_REQUIRE(chunks.size() == 2);
    HD_REQUIRE(chunks[0].size() == 1 && chunks[1].size() == 1);
    HD_CHECK_EQ(chunks[0][0].text, "[無線]（타카미네）");
    HD_CHECK_EQ(chunks[1][0].text, "では皆さん");
    const auto protectedChunks = pipeline::Segment(pipeline::Protect(facts.text, ctx, *snap));
    HD_REQUIRE(protectedChunks.size() == 2);
    HD_CHECK_EQ(protectedChunks[1][0].text, "では皆さん");
    const auto asideFacts = pipeline::ReadLine(*doc, "（拍手と歓声）（高峰(たかみね)）では皆さん");
    const auto asideLookup = pipeline::LookupOf(*doc, asideFacts);
    const auto asideContext = pipeline::ContextOf(*doc, asideLookup);
    const auto asideChunks = pipeline::Segment(pipeline::Protect(asideFacts.text, asideContext, *snap));
    HD_REQUIRE(asideChunks.size() == 2);
    std::string asideHead;
    for (const auto& piece : asideChunks[0]) asideHead += piece.text;
    HD_CHECK_EQ(asideHead, "（拍手と歓声）（타카미네）");
    HD_CHECK_EQ(asideChunks[1][0].text, "では皆さん");

    const auto explicitOnly = pipeline::ReadLine(*doc, lines[1]);
    HD_CHECK_EQ(explicitOnly.text, "（쿄）はい");
    HD_CHECK(names::NameCues(*snap, "（恭）はい").empty());
    HD_CHECK(explicitOnly.cued == std::vector<std::string>{"恭"});
    const auto explicitLookup = pipeline::LookupOf(*doc, explicitOnly);
    HD_CHECK(!explicitLookup.cued.contains("恭"));
    const auto explicitContext = pipeline::ContextOf(*doc, explicitLookup);
    HD_REQUIRE(explicitContext.lookup.cued != nullptr);
    HD_CHECK(!explicitContext.lookup.cued->contains("恭"));
    for (const std::string_view raw : {"（恭(きょう)）はい。恭", "（恭(きょう)／恭）はい"}) {
        const auto repeated = pipeline::ReadLine(*doc, raw);
        HD_CHECK(repeated.cued == std::vector<std::string>{"恭"});
        const auto repeatedLookup = pipeline::LookupOf(*doc, repeated);
        HD_CHECK(!repeatedLookup.cued.contains("恭"));
    }
    const auto bare = pipeline::ReadLine(*doc, "恭");
    HD_CHECK_EQ(bare.text, "恭");
    HD_CHECK(bare.cued.empty());

    const auto standalone = pipeline::ReadLine(*doc, "（恭(きょう)）");
    HD_CHECK_EQ(standalone.text, "（恭）");
    HD_CHECK(standalone.cued.empty());
    HD_CHECK(doc->cued.empty());

    const auto prefixKeys = snap->PersonKeysAt("高峰の母");
    HD_REQUIRE(!prefixKeys.empty());
    HD_CHECK(prefixKeys.back() < std::string("高峰の母").size());
    pipeline::LineFacts prefixOnly;
    prefixOnly.cued = {"高峰の母"};
    HD_CHECK(!pipeline::LookupOf(*doc, prefixOnly).cued.contains("高峰の母"));

    const std::vector<std::string> lexicalLines = {"（高峰）はい"};
    const auto lexicalDoc = pipeline::BuildDocument(*snap, lexicalLines);
    HD_REQUIRE(lexicalDoc != nullptr);
    HD_CHECK(lexicalDoc->cued.contains("高峰"));
    const auto added = pipeline::ReadLine(*lexicalDoc, lines[1]);
    const auto addedLookup = pipeline::LookupOf(*lexicalDoc, added);
    HD_CHECK(addedLookup.cued.contains("高峰"));
    HD_CHECK(!addedLookup.cued.contains("恭"));
    HD_CHECK(lexicalDoc->cued.contains("高峰"));
    HD_CHECK(!lexicalDoc->cued.contains("恭"));
}

HD_TEST(names_canonical_kana_before_document_and_line_readings) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    struct Case {
        std::string_view canonical;
        std::string_view decomposed;
    };
    const Case cases[] = {
        {"見れば見るほど頭はモヤモヤ　心もモヤモヤ。", "見れば見るほど頭はモヤモヤ　心もモヤモヤ。"},
        {"おはようございます。", "おはようございます。"},
        {"私と社長が　丹精込めて作ったんじゃないの。", "私と社長が　丹精込めて作ったんじゃないの。"},
        {"（ゴン）はんそくまけ！", "（ゴン）はんそくまけ！"},
        {"（ランディー）ノー。", "（ランディー）ノー。"},
        {"（ゲンノウ）はっ！", "（ゲンノウ）はっ！"},
        {"神頭有右生（こうずゆうせい）に出会います。", "神頭有右生（こうずゆうせい）に出会います。"},
        {"佐殿（すけどの）をお助けするのです。", "佐殿（すけどの）をお助けするのです。"},
        {"みんな！　ハピハピハッピー！", "みんな！　ハピハピハッピー！"},
        {"超エネルギッシュ！", "超エネルギッシュ！"},
        {"利きいちご大会があるんですね。", "利きいちご大会があるんですね。"},
        {"まあ　谷のおじ様ですね。", "まあ　谷のおじ様ですね。"},
        {"だったら　１番のルルヴェとか？", "だったら　１番のルルヴェとか？"},
        {"大英帝国最大の兵器会社ヴィッカース社の機関銃です。", "大英帝国最大の兵器会社ヴィッカース社の機関銃です。"},
        {"あっ！　おんなじだ。これも　これも。", "あっ！　おんなじだ。これも　これも。"},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> canonicalLines = {std::string(c.canonical)};
        const std::vector<std::string> decomposedLines = {std::string(c.decomposed)};
        const auto canonicalDoc = pipeline::BuildDocument(*snap, canonicalLines);
        const auto decomposedDoc = pipeline::BuildDocument(*snap, decomposedLines);
        HD_REQUIRE(canonicalDoc != nullptr);
        HD_REQUIRE(decomposedDoc != nullptr);
        HD_CHECK_EQ(canonicalDoc->key, decomposedDoc->key);
        HD_CHECK(canonicalDoc->accepted == decomposedDoc->accepted);
        HD_CHECK(canonicalDoc->cued == decomposedDoc->cued);
        const auto canonicalFacts = pipeline::ReadLine(*canonicalDoc, c.canonical);
        const auto decomposedFacts = pipeline::ReadLine(*decomposedDoc, c.decomposed);
        HD_CHECK_EQ(canonicalFacts.text, decomposedFacts.text);
        HD_CHECK(canonicalFacts.cued == decomposedFacts.cued);
        HD_CHECK_EQ(pipeline::ReadLine(*canonicalDoc, c.decomposed).text, canonicalFacts.text);
        HD_CHECK_EQ(pipeline::ReadLine(*decomposedDoc, c.canonical).text, canonicalFacts.text);
    }
}

HD_TEST(text_japanese_spelling) {
    for (const char* s : {"무라사메", "신지", "핫토리", "유우키", "와카바"}) HD_CHECK(text::JapaneseSpelling(s));
    for (const char* s : {"펑", "러", "김", "샹", "", "abc"}) HD_CHECK(!text::JapaneseSpelling(s));
}

// A cue that would hold for the document but names a weather word holds there when two lines give it.
HD_TEST(names_recurring_cues) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::string> twice = {"駆逐艦夕立、出撃", "夕立、出撃するよ", "夕立がね"};
    const auto doc = pipeline::BuildDocument(*snap, twice);
    HD_REQUIRE(doc != nullptr);
    HD_CHECK(doc->cued.count("夕立") == 1);
    const std::vector<std::string> once = {"駆逐艦夕立、出撃", "夕立がね"};
    const auto single = pipeline::BuildDocument(*snap, once);
    HD_REQUIRE(single != nullptr);
    HD_CHECK(single->cued.count("夕立") == 0);
}
