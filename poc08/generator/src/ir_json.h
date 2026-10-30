#pragma once
// IR.md §7 JSON ↔ generator structs.
#include <string>
#include <vector>

#include "hd8gen/kir.h"
#include "json.h"

namespace hd8gen {

struct LineIR {
    std::vector<Node> kir2;
    std::vector<Reg> reg;
    std::vector<UttLevel> lev;
};

bool NodeFromJson(const json::Value& v, Node& n, std::string& err);
json::Value NodeToJson(const Node& n);
bool LineFromJson(const json::Value& v, LineIR& out, std::string& err, const char* nodesKey = "kir2");
json::Value RegToJson(const Reg& r);
json::Value LevToJson(const UttLevel& u);

const char* KindName(Kind);
const char* CTypeName(CType);
const char* AspectName(Aspect);
const char* NegName(Neg);
const char* MoodName2(Mood);
const char* PunctName(Punct);

}  // namespace hd8gen
