#pragma once
// Label, honorific and role vocabulary shared by the registry and the document context.
#include <cstddef>
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

// The most characters a bracketed group of a caption lead holds.
constexpr int kMaxCaptionGroup = 24;

// Whether s, from its line start, holds only caption marks: symbols and punctuation other than
// brackets and quotes (⚟, ≫, 》, ・, -, 📱), white space, and bracketed groups that close (an
// aside or a tag before a label: (会場内 笑い)(せいや), [無線](武)).
bool CaptionLead(std::string_view s);

// The end of the group in round brackets that a caption line opens with after caption marks
// (（島田）では, ⚟（明）さい～！, (会場内 笑い)(せいや)世界), when an utterance follows it
// (SpeechAfterLabel): a speaker label or an aside, not a number ((1)では); 0 for none.
std::size_t CaptionLabelEnd(std::string_view line);

// Whether the word between before and after is a part of a closed round-bracket speaker
// label at the start of a caption, followed by speech.
bool BracketLabelAt(std::string_view before, std::string_view after);

// Whether rest, the text after a speaker label in brackets, starts an utterance: there is text
// before the line ends, and it neither quotes the brackets with と or って nor starts with a
// particle of one kana that attaches to them ((人形)は、, (W)は西; not はい, では).
bool SpeechAfterLabel(std::string_view rest);

// The names of a label or a chain, honorifics removed.
std::vector<std::string> LabelParts(std::string_view k);

}  // namespace hdTrans::names::internal
