#pragma once
// The name list of a document: names a caller gives with their Korean for the lines of one
// translation (Session::SetNames). One name per line, "#" starts a comment:
//   JA <TAB> KO [<TAB> KIND [<TAB> OPTIONS]]
// KIND is given (default), family, full, nick, place, org or title. The option hon makes
// JA a name only before an honorific or inside a longer name of the registry.
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::names {

enum class ListKind { Given, Family, Full, Nick, Place, Org, Title };

struct Listed {
    std::string ja, ko;
    ListKind kind = ListKind::Given;
    bool honorificOnly = false;
    int line = 0;  // its 1-based line in the list text
};

struct List {
    std::vector<Listed> names;  // a later line of the same JA replaces the earlier one
};

// Whether the kind names a person, whom the name registry takes as an accepted name.
inline bool PersonKind(ListKind k) {
    return k == ListKind::Given || k == ListKind::Family || k == ListKind::Full || k == ListKind::Nick;
}

// Parses a name list. A malformed line is left out and reported in errors as
// "LINE: message"; returns whether every line parsed.
bool ParseList(std::string_view text, List& out, std::vector<std::string>* errors = nullptr);

// The line of a name in the list format, without "\n".
std::string ListLine(const Listed& name);

std::string_view KindName(ListKind k);

}  // namespace hdTrans::names
