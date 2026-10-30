#pragma once
// The lift: a sentence 0.7.x resolved (its items, each a selected Korean target with a marker
// family and a selected ending) as Korean morpheme IR nodes. The mapping is data,
// <data>/bridge/data/lift.tsv, one row per key:
//   family   <marker>  <suffix>  <node>   a target "stem<marker>" is a node with lemma stem+suffix
//   ending   <ja|*>    <ko>      <nodes>  the selected ending (Japanese key, Korean target) sets
//                                         fields of the word's node; more objects add nodes after it
//   particle <ko>      -         <node>   a particle target
//   word     <digit>   -         <node>   a target without marker, by the class digit of its code
// <node> is a KNode JSON object (IR.md §7), <nodes> one object or an array of them. An item whose
// target or ending has no row is a Raw node with fallback evidence, and its key is a miss.
#include <string>
#include <unordered_map>
#include <vector>

#include "core/engine/hooks.h"
#include "ir/ir.h"
#include "ir/json.h"

namespace hd08::bridge {

struct LiftTable {
    std::unordered_map<std::string, std::pair<std::string, Json>> families;  // marker → (suffix, node)
    std::unordered_map<std::string, Json> endings;                           // "ja\tko" → nodes
    std::unordered_map<std::string, Json> particles;                         // ko → node
    std::unordered_map<char, Json> words;                                    // class digit → node
};

bool LoadLift(const std::string& dataRoot, LiftTable& out, std::string& err);

struct LiftMiss {
    std::string kind;  // family, ending, particle, word
    std::string key;
};

// Appends the nodes of the sentence's items to line.kir (ids go on from the last node) with one
// T decision each carrying the 0.7.x provenance ("075:<layer>:<id>"). from is where the search for
// the items' source text in line.src goes on. False when some item had no row (then misses names it).
bool Lift(const LiftTable& table, const hdTrans::hooks::Sentence& sentence, Doc& doc, Line& line, uint32_t& from,
          std::vector<LiftMiss>& misses);

}  // namespace hd08::bridge
