// The packs layer reads an attachment provider's annotations (attach.h, docs/packs.md): the how
// of an argument and the features of a clause decide rows only when a provider gives them.
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/engine/rules.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/analyze.h"
#include "core/word/attach.h"
#include "core/word/flush.h"
#include "core/word/worker.h"
#include "hdtest.h"
#include "rule_fixture.h"

using namespace hdTrans;

namespace {

std::shared_ptr<const dict::Snapshot> Packed() {
    std::vector<dict::File> files{
        {0, "base.tsv", fixture::WithRuleBodies("key\tko\tpos\tsem\tgram\tdict\trule\tid\n本\t책\tN110\tXXX\t\tGN\t0\t1\n")},
        {0, "pack.tsv",
         "#! layer=pack\n"
         "id\tlemma\tpos\tcond\ttarget\n"
         "bare\t本\tn\thow=bare\t장부\n"
         "past\t本\tn\tcl.tense=past\t서적\n"
         "mainpast\t本\tn\tmain.tense=past cl.link=te\t도서\n"}};
    const dict::LoadResult loaded = dict::Load(std::move(files));
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

const rules::Program& Rules(const dict::Snapshot& snapshot) {
    static std::vector<std::shared_ptr<internal::Generated>> kept;
    kept.push_back(internal::NewGenerated(snapshot));
    return kept.back()->hasRules ? kept.back()->rules : internal::RulesProgram();
}

// The annotation the fake provider gives the first item: an argument written how, in a clause
// with tense and link, under a main clause with mainTense ("" for none).
struct Fake {
    std::string how, tense, link, mainTense;
};

bool Provide(void* context, const tokenizer::Tokens&, const word::Chain& chain, word::Attachment& out) {
    const Fake& f = *static_cast<const Fake*>(context);
    out = {};
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) out.items.emplace_back();
    if (out.items.empty()) return false;
    word::AttachClause own;
    if (!f.tense.empty()) own.features.push_back({"tense", f.tense});
    if (!f.link.empty()) own.features.push_back({"link", f.link});
    out.clauses.push_back(own);
    out.items[0].clause = 0;
    if (!f.how.empty()) {
        out.items[0].argOf = 0;
        out.items[0].how = f.how;
    }
    if (!f.mainTense.empty()) {
        word::AttachClause main;
        main.features.push_back({"tense", f.mainTense});
        out.clauses[0].parent = 1;
        out.clauses.push_back(main);
    }
    return true;
}

std::string Translate(const dict::Snapshot& snapshot, const Fake* fake) {
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Analysis analysis;
    word::Settings settings = word::Settings::Reference();
    settings.text = true;
    word::AttachProvider provider{const_cast<Fake*>(fake), Provide};
    if (fake != nullptr) settings.attach = &provider;
    tokenizer::Tokenize("本。", tokens);
    word::Analyze(tokens, snapshot, {}, settings, arena, analysis);
    if (analysis.fault || analysis.sentences.empty()) return "fault";
    word::Worker& w = *analysis.worker;
    w.chain = analysis.sentences.front().chain;
    w.current = analysis.sentences.front().current;
    w.transformSurface = analysis.sentences.front().transformSurface;
    word::Flusher f{w, Rules(snapshot), snapshot.OutputFamilies(), {}};
    word::Flush(f);
    for (const word::Item* it = w.chain.head; it != nullptr; it = it->next)
        if (it->group->surface == "本") return std::string(it->EntryText(it->selected));
    return "none";
}

}  // namespace

HD_TEST(word_pack_attachment_atoms) {
    HD_REQUIRE(internal::RulesLoaded());
    const auto snapshot = Packed();
    HD_REQUIRE(snapshot != nullptr);
    // Without a provider the attachment atoms never hold.
    HD_CHECK_EQ(Translate(*snapshot, nullptr), std::string("책"));
    const Fake bare{"bare", "", "", ""}, past{"marked", "past", "", ""}, nested{"", "nonpast", "te", "past"},
        none{"", "", "", ""};
    HD_CHECK_EQ(Translate(*snapshot, &bare), std::string("장부"));
    HD_CHECK_EQ(Translate(*snapshot, &past), std::string("서적"));
    // main.* reads the clause at the top of the parent chain; two atoms beat one.
    HD_CHECK_EQ(Translate(*snapshot, &nested), std::string("도서"));
    HD_CHECK_EQ(Translate(*snapshot, &none), std::string("책"));
}
