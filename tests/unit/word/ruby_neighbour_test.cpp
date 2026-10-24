// The rules of a ruby base read the word after its reading as the next word; the reading
// keeps the base before it, and the words after the reading read the reading.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_ruby_base_reads_the_word_after_its_reading) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "犬\t가\tN110\tXXX\t\tGN\t0\t1\t\n"
         "犬\t나\tN110\t\t\tGN\t2\t2\t||1 S+1== \"猫\"\n"
         "いぬ\t다\tN110\tXXX\t\tGN\t0\t3\t\n"
         "いぬ\t라\tN110\t\t\tGN\t4\t4\t||1 S-1== \"犬\"\n"
         "猫\t마\tN110\tXXX\t\tGN\t0\t5\t\n"
         "猫\t바\tN110\t\t\tGN\t6\t6\t||1 S-1== \"いぬ\"\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"{rb}犬{/rb}{rt}いぬ{/rt}猫", "{rb}나{/rb}{rt}라{/rt}바"},
        {"犬猫", "나마"},
        {"いぬ猫", "다바"},
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}

// The word stage's own reads of the next word (here the Ow boundary of a rule) pass over the
// reading of a ruby base too.
HD_TEST(word_ruby_base_boundary_after_its_reading) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "犬\t가\tN110\tXXX\t\tGN\t0\t1\t\n"
         "犬\t나\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Ow\"\n"
         "いぬ\t다\tN110\tXXX\t\tGN\t0\t3\t\n"
         "いぬ\t라\tN110\t\t\tGN\t4\t4\t||1 G+0== \"Ow\"\n"
         "猫\t마\tN110\t\t\tGN\t0\t5\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"{rb}犬{/rb}{rt}いぬ{/rt}", "{rb}나{/rb}{rt}라{/rt}"},
        {"{rb}犬{/rb}{rt}いぬ{/rt}猫", "{rb}가{/rb}{rt}다{/rt}마"},
        {"犬いぬ", "가라"},
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}

// A ruby word alone on its line is not alone: its reading stands after it (the Td piece).
HD_TEST(word_ruby_base_alone_keeps_its_reading_on_the_line) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "犬\t가\tN110\tXXX\t\tGN\t0\t1\t\n"
         "犬\t나\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Td\"\n"
         "いぬ\t다\tN110\tXXX\t\tGN\t0\t3\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"{rb}犬{/rb}{rt}いぬ{/rt}", "{rb}가{/rb}{rt}다{/rt}"},
        {"犬", "나"},
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
