#pragma once
// A one-line text form of kir nodes, for rule cases and tests:
//   N:현관 P:gen N:문 P:obj V:닫다{mood=imp} Punct:end
//   V:하다{ctype=adn,rel=fut} N:것{dep=1} V:이다     P:lex=도     Raw:PC     Punct:comma{jaMark=、}
// Features: tense aspect mood ctype rel eomi modal neg kase aux dep glue sem(person|place…) vpos kcls jaMark utt.
#include <string>
#include <vector>

#include "hd8gen/kir.h"

namespace hd8gen {

bool ParseKirText(const std::string& text, std::vector<Node>& out, std::string& err);
std::string KirText(const std::vector<Node>& nodes);
// Sets one feature of a node by name (shared with the rewrite actions). False if the name or value is unknown.
bool SetFeature(Node& n, const std::string& name, const std::string& value);
// Reads one feature as a string ("" when unset).
std::string GetFeature(const Node& n, const std::string& name);

}  // namespace hd8gen
