#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv",
                                            "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
                                            "帰\t돌아&A1@\tM418\t\t\tGN\t0\t1\n"
                                            "ますか\t7니까\tS5:0\t\t\tGN\t0\t2\n"
                                            "V\t7니까\tS5:0\t\t\tGN\t0\t3\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    internal::Memory memory;
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, memory, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_masuka_offer_marks_after_the_question) {
    // A question that ends the sentence offers the act.
    HD_CHECK_EQ(Translate("帰りますか？"), std::string("돌아갈까요?"));
    // An exclamation mark after it makes a surprised remark (exclaim-after).
    HD_CHECK_EQ(Translate("帰りますか！"), std::string("돌아갑니까!"));
    HD_CHECK_EQ(Translate("帰りますか？！"), std::string("돌아갑니까?!"));
    // A prompt on a screen asks the reader's will (prompt-after).
    HD_CHECK(Translate("帰りますか？はい／いいえ").starts_with("돌아가시겠습니까?"));
    HD_CHECK(Translate("帰りますか？（はい／いいえ）").starts_with("돌아가시겠습니까?"));
}
