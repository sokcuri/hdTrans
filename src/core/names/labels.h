#pragma once
// Label, honorific and role vocabulary shared by the registry and the document context.
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::names::internal {

// Whether p may be the label of a plain-text form.
bool PlainLabel(std::string_view p);
// Whether a label is a role (a group or a non-name) rather than a name.
bool LabelRole(std::string_view p);
// Whether t is a role word, or contains の and is not hiragana alone.
bool RoleLike(std::string_view t);
// X of a label Xの父 (and its relatives), when p is one.
bool ParentLabel(std::string_view p, std::string& x);

// The names of a label or a chain, honorifics removed.
std::vector<std::string> LabelParts(std::string_view k);

}  // namespace hdTrans::names::internal
