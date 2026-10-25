#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = dict::Load({{0, "base.tsv",
                                            "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
                                            "V\t4다\tS500\t\t\tGN\t0\t1\n"
                                            "V\t6\tS571\t\t\tGN\t0\t2\n"
                                            "V\t1자\tS5A0\t\t\tGN\t0\t3\n"
                                            "見せ\t보&AJ@\tM437\t\t\tGN\t0\t4\n"
                                            "あげ\t&CJ@\tM417\t\tDs\tGN\t0\t5\n"
                                            "や\t&CJ@\tM438\t\tDs\tGN\t0\t6\n"
                                            "母\t어머니\tN110\tVHn\t\tGN\t0\t7\n"
                                            "が\t(이)가\tP200\t\t\tGN\t0\t8\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const std::string line(text);
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_offer_promise_reads_the_speakers_giving) {
    // An exclamation mark after the あげる of a て form is the speaker's promise.
    HD_CHECK_EQ(Translate("見せてあげる！"), std::string("보여 줄게!"));
    // In a quote it may also end in an ellipsis.
    HD_CHECK_EQ(Translate("「見せてあげる……」"), std::string("「보여 줄게……」"));
    // Another doer before it, or a shouted やる, keeps the plain statement.
    HD_CHECK_EQ(Translate("母が見せてあげる！"), std::string("어머니가 보여 준다!"));
    HD_CHECK_EQ(Translate("見せてやる！"), std::string("보여 준다!"));
    // The volitional of やる after a て form is the haughty grant 주마.
    HD_CHECK_EQ(Translate("見せてやろう。"), std::string("보여 주마."));
    HD_CHECK_EQ(Translate("見せてやろう……"), std::string("보여 주마……"));
}
