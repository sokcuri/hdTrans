#include <cstdlib>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

// The construction actions that write a verb in the active voice, and map-ending-tail: set-active on a verb analysed
// as a causative-passive (読まされ), set-verb-from on a dictionary row read 게 되다 (謝らされ),
// set-base-verb on a dictionary row that is the passive of a dictionary verb (使われ, 見られ:
// lexical-passive), and set-verb on the させられ after a noun. The rows and the verbs are
// the shipped ones, so the test needs HDTRANS_DICTS.
HD_TEST(word_voice_actions) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    auto translate = [&](std::string_view text) {
        const std::string line(text);
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
        return out.empty() ? std::string() : out[0];
    };
    HD_CHECK_EQ(translate("母に本を読まされた。"), "어머니 때문에 억지로 책을 읽었다.");
    HD_CHECK_EQ(translate("父に謝らされた。"), "아버지 때문에 억지로 사과했다.");
    HD_CHECK_EQ(translate("父に勉強させられた。"), "아버지 때문에 억지로 공부했다.");
    // Without a causer the causative-passive stays 게 되다, and an ending in 게 되다 takes its place.
    HD_CHECK_EQ(translate("本を読まされることになった。"), "책을 읽게 되었다.");
    HD_CHECK_EQ(translate("父に傘を使われた。"), "아버지가 우산을 썼다.");
    HD_CHECK_EQ(translate("母に日記を見られた。"), "어머니가 일기를 보았다.");
    // A dictionary passive before いる keeps its passive: the active verb cannot take 어 있다. Its
    // object is the subject of that passive.
    HD_CHECK_EQ(translate("皆に顔を知られている。"), "모두에게 얼굴이 알려져 있다.");
    // map-ending-tail: the auxiliary ending before くせに takes its connective form.
    HD_CHECK_EQ(translate("食べちゃったくせに！"), "먹어버렸으면서!");
    // A potential with no agent keeps its reading.
    HD_CHECK_EQ(translate("富士山を見られた。"), "후지산을 볼 수 있었다.");
    HD_CHECK_EQ(translate("続けさせられない。"), "계속하게 할 수 없다.");
    HD_CHECK_EQ(translate("続けさせられません。"), "계속하게 할 수 없습니다.");
    HD_CHECK_EQ(translate("これなら練習を続けさせられる。"), "이것이라면 연습을 계속하게 할 수 있다.");
    HD_CHECK_EQ(translate("私は先生に練習を続けさせられた。"), "나는 선생님 때문에 억지로 연습을 계속했다.");
    HD_CHECK(translate("私は先生に練習を続けさせられない。").find("할 수") == std::string::npos);
    HD_CHECK(translate("毎日無理やり練習を続けさせられなかった。").find("할 수") == std::string::npos);
    HD_CHECK(translate("弟君であらせられる。").find("할 수") == std::string::npos);
    HD_CHECK(translate("貴様の仕事の遅さにはへきえきさせられる｡").find("게 할 수") == std::string::npos);
    HD_CHECK(translate("殿下にあらせられるぞ！").find("있게 할 수") == std::string::npos);
    HD_CHECK(translate("かしこくも天皇陛下におかせられましては").find("두게 할 수") == std::string::npos);
    HD_CHECK(translate("続けられない。").find("하게") == std::string::npos);
    HD_CHECK(translate("今なら続けられる。").find("하게") == std::string::npos);
}
