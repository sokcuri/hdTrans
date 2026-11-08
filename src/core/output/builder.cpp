#include "core/output/compose.h"
#include "core/output/text.h"

namespace hdTrans::output {

namespace {

using internal::Contains;
using internal::First;

// 500 for a Z6 item, 510 for Z7, else 0.
int ClassifySelected(const ResolvedItem& it) {
    const std::string_view g = internal::Selected(it).gattr;
    if (Contains(g, "Z6")) return 500;
    if (Contains(g, "Z7")) return 510;
    return 0;
}

// The part class of an item's first entry ('0'..'9').
char Kind(const ResolvedItem& it) { return First(internal::EntryAt(it, 0).code); }

bool NominalHead(const ResolvedItem& it) {
    const std::string_view g = internal::Selected(it).gattr;
    return Kind(it) == '1' || Kind(it) == '8' || Contains(g, "Nd") || Contains(g, "Nj");
}

}  // namespace

void BuildItems(const std::vector<ResolvedItem>& items, const std::vector<TokenText>& tokens, Nodes& nodes) {
    nodes.clear();
    const int count = static_cast<int>(items.size());
    for (int index = 0; index < count;) {
        bool tailBlocked = false;
        switch (Kind(items[index])) {
            case '0': case '3': case '4': case '5': case '7': case '8': case '9':
                index++;
                break;
            case '1': {
                int cls = ClassifySelected(items[index]);
                if (cls == 500) {
                    while (index < count) {
                        index++;
                        if (index < count) cls = ClassifySelected(items[index]);
                        if (cls != 500) break;
                    }
                }
                if (cls == 500 || index >= count || !NominalHead(items[index])) break;
                if (ClassifySelected(items[index]) == 0) {
                    if (tokens[items[index].tokenIndex].type == tokenizer::kTypeJapanese) {
                        index++;
                        while (index < count) {
                            const ResolvedItem& it = items[index];
                            if (index == 0 || items[index - 1].tokenIndex != it.tokenIndex) break;
                            if (IsOutputBuilderBlockedText(it.surface)) {
                                tailBlocked = true;
                                break;
                            }
                            if (static_cast<unsigned char>(First(it.surface)) < 0x80 || Kind(it) != '1' ||
                                internal::WidthOf(it.surface) != 2) {
                                break;
                            }
                            // A number before 月, or before 日 after 月, starts a part of a date of its own (一月二日
                            // 1월 2일, 昨年一月 작년 1월).
                            const std::string_view g = internal::Selected(it).gattr;
                            if ((Contains(g, "Nj") || Contains(g, "Nd")) && index + 1 < count &&
                                items[index + 1].tokenIndex == it.tokenIndex &&
                                (items[index + 1].surface == "月" ||
                                 (items[index + 1].surface == "日" && items[index - 1].surface == "月"))) {
                                break;
                            }
                            index++;
                        }
                    } else {
                        tailBlocked = true;
                        index++;
                    }
                }
                if (index < count && !tailBlocked && ClassifySelected(items[index]) == 510) {
                    while (index < count && ClassifySelected(items[index]) == 510) index++;
                }
                break;
            }
            default:
                tailBlocked = true;
                index++;
                break;
        }
        while (!tailBlocked && index < count && items[index].tokenIndex == items[index - 1].tokenIndex &&
               Kind(items[index]) == '2') {
            index++;
        }
        nodes.push_back(index);
    }
}

}  // namespace hdTrans::output
