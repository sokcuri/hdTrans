#pragma once
// Label, honorific and role vocabulary shared by the registry and the document context.
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::names::internal {

// Person suffixes that follow a name (さん, 様, 先生 ...).
std::span<const std::string_view> Honorifics();
// Honorifics that fuse with a kanji name into one run (市松殿).
std::span<const std::string_view> KanjiHonorifics();
// Title words that may follow a name inside its run (茜たち, 水無月家).
std::span<const std::string_view> TitleSuffixes();
// Person suffixes that follow a surname without being a given name.
std::span<const std::string_view> PersonSuffixes();

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
