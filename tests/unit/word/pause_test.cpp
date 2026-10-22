#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/flush.h"
#include "core/word/worker.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

const dict::LoadResult& PauseDictionary() {
    static const dict::LoadResult result = dict::Load({{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "高\t$D3@\tH7N0\t\t\tGN\t0\t1\n"
        "い\t4\tT500\t\t\tGN\t0\t2\n"
        "い\t1다\tT500\t\t\tGN\t0\t3\n"
        "本当に\t정말로\tE311\t\t\tGN\t0\t4\n"
        "山\t산\tN110\t\t\tGN\t0\t5\n"
        "が\t이\tP200\t\t\tGN\t0\t6\n"
        "こと\t것\tN110\t\t\tGN\t0\t7\n"
        "でも\t에서도\tP200\t\t\tGN\t0\t8\n"
        "でも\t하지만\tP200\t\t\tGN\t0\t9\n"
        "」\t」\tN600\t\t\tGN\t0\t10\n"
        "んだ\t것이다\tN110\t\t\tGN\t0\t11\n"}});
    return result;
}

// The ending of 高い at the start of line and the target of the item after it once the
// conjunction and terminal passes ran; words are the items after 高い, each at the
// next occurrence of its text.
std::string AfterPasses(std::string_view line, const std::vector<std::string_view>& words) {
    const auto& loaded = PauseDictionary();
    if (loaded.snapshot == nullptr || !loaded.errors.empty()) return "no dictionary";
    tokenizer::Tokens tokens;
    tokenizer::TokenizeSpans({{tokenizer::Piece{std::string(line), 0}}}, true, tokens);
    word::Arena arena;
    word::Settings settings = word::Settings::Reference();
    settings.text = true;
    word::Worker worker(tokens, *loaded.snapshot, {}, settings, arena, {});
    bool missing = false;
    auto add = [&](std::string_view text, int token, int start, std::string_view key, bool predicate) {
        const auto found = worker.DoLookup(key, word::kModeGeneral);
        if (found.records == nullptr) {
            missing = true;
            return;
        }
        auto* detail = worker.NewDetail(predicate ? word::kDetailKeiyo : word::kDetailInfo, token);
        if (predicate) {
            const auto endings = worker.DoLookup("い", word::kModeAdjectiveEnding);
            detail->kind = '0';
            detail->group = worker.NewGroup(endings.records, ' ');
            detail->ResetDirect(2);
            detail->selected = 0;
        }
        auto* item = worker.NewItem(detail, worker.NewGroup(found.records, ' '), text, start, token);
        item->length = static_cast<int>(key.size()) + (predicate ? 3 : 0);
        item->selected = 0;
        item->prev = worker.chain.tail;
        (worker.chain.tail != nullptr ? worker.chain.tail->next : worker.chain.head) = item;
        worker.chain.tail = item;
    };
    add(tokens.Raw(0), 0, 0, "高", true);
    std::size_t token = 0, from = std::string_view("高い").size();
    for (const std::string_view w : words) {
        for (; token < tokens.tokens.size(); token++, from = 0) {
            const std::size_t at = tokens.Raw(token).find(w, from);
            if (at == std::string_view::npos) continue;
            add(tokens.Raw(token), static_cast<int>(token), static_cast<int>(at), w, false);
            from = at + w.size();
            break;
        }
    }
    if (missing || worker.chain.head->next == nullptr) return "no record";
    rules::Program program;
    word::Flusher flusher{worker, program, loaded.snapshot->OutputFamilies()};
    word::ChooseInitialConjunctions(flusher);
    word::PreferTerminal(flusher);
    const word::Item* head = worker.chain.head;
    return std::string(head->DirectText(head->DirectIndex())) + " " +
           std::string(head->next->EntryText(head->next->selected));
}

}  // namespace

// A predicate before an ellipsis or a dash ends its clause before an adverb, a
// conjunction or a noun that goes on; it keeps modifying a dependent noun and a noun
// that ends the sentence. A conjunction after an ellipsis starts its sentence unless it
// ends the utterance.
HD_TEST(word_pause_ends_clause) {
    HD_CHECK_EQ(AfterPasses("高い……本当に", {"本当に"}), std::string("1다 정말로"));
    HD_CHECK_EQ(AfterPasses("高い――本当に", {"本当に"}), std::string("1다 정말로"));
    HD_CHECK_EQ(AfterPasses("高い・・・本当に", {"本当に"}), std::string("1다 정말로"));
    HD_CHECK_EQ(AfterPasses("高い本当に", {"本当に"}), std::string("4 정말로"));
    HD_CHECK_EQ(AfterPasses("高い……山が", {"山", "が"}), std::string("1다 산"));
    HD_CHECK_EQ(AfterPasses("高い……山", {"山"}), std::string("4 산"));
    HD_CHECK_EQ(AfterPasses("高い……ことが", {"こと", "が"}), std::string("4 것"));
    HD_CHECK_EQ(AfterPasses("高い――んだが", {"んだ", "が"}), std::string("4 것이다"));
    HD_CHECK_EQ(AfterPasses("高い……でも山", {"でも", "山"}), std::string("1다 하지만"));
    HD_CHECK_EQ(AfterPasses("高い……でも」", {"でも", "」"}), std::string("4 에서도"));
}
