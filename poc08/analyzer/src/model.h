// In-memory analyzer model, its TSV source form (data/) and its binary snapshot.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hd8/analyzer.h"

namespace hd8::ana {

struct Str {
  uint32_t off = 0, len = 0;  // range in Model::pool
};

struct Model {
  std::string pool;  // UTF-8 strings referenced by Str
  std::u32string cps;  // code points: stems and suffixes

  std::string_view S(Str s) const { return std::string_view(pool).substr(s.off, s.len); }
  std::u32string_view C(uint32_t off, uint32_t len) const { return std::u32string_view(cps).substr(off, len); }
  Str Intern(std::string_view s);
  std::unordered_map<std::string, Str> internMap;

  // POS and paradigm names; index 0 of ctype/cform is "" (none).
  std::vector<std::string> udNames;
  struct PosRule {
    std::string ud, ctype;  // prefixes; first matching rule wins
    Pos pos = Pos::unk;
  };
  std::vector<PosRule> posRules;
  std::vector<std::string> ctypeNames, cformNames;

  struct Class {
    uint16_t ud = 0;
    uint8_t ctype = 0, cform = 0;
    Str lemma;  // lexicalized function word, else empty
  };
  std::vector<Class> classes;

  // A paradigm instance: the forms of one template with their classes resolved for one (ud, ctype, lemma).
  struct PForm {
    uint32_t suf = 0;
    uint16_t sufLen = 0;
    uint16_t cls = 0;
    Str read;  // reading suffix
  };
  struct Paradigm {
    uint32_t first = 0, count = 0;
  };
  std::vector<PForm> pforms;
  std::vector<Paradigm> paradigms;

  struct Group {
    uint32_t stem = 0;     // in cps
    uint16_t stemLen = 0;
    uint16_t ud = 0;
    uint8_t ctype = 0;
    uint8_t readKind = 0;  // 0: katakana(stem), 1: `read`, 2: no reading
    uint32_t paradigm = 0;
    int32_t cost = 0;
    Str lemma, read;
  };
  std::vector<Group> groups;  // sorted by stem

  // Connection costs: observed pairs, else left backoff + right unigram.
  struct Pair {
    uint16_t l = 0, r = 0;  // previous token's class, next token's class
    int32_t cost = 0;
  };
  std::vector<Pair> pairs;
  std::vector<int32_t> boLeft, uniRight;

  // Unknown words by character category (UniDic char.def semantics).
  struct UnkArc {
    uint16_t cls = 0;
    int32_t cost = 0, perChar = 0;
  };
  struct Category {
    std::string name;
    bool invoke = false, group = false;
    uint8_t length = 0;
    std::vector<UnkArc> arcs;
  };
  std::vector<Category> cats;
  struct CharRange {
    char32_t lo = 0, hi = 0;
    uint32_t mask = 0;  // categories; the first listed is the primary one
    uint8_t primary = 0;
  };
  std::vector<CharRange> ranges;

  // ja/derive.tsv: spoken and lengthened forms, tried as penalised derived arcs.
  struct Rule {
    uint8_t kind = 0;  // 0: replace `from` by `to`; 1: elongation (a vowel run or ー run after a mora
                       // collapses); 2: stutter (a kana repeating the next word's first mora)
    std::u32string from, to;
    int32_t cost = 0;
    uint32_t prev = 0, next = 0;  // CharClass sets the neighbours must be in; 0 = any
    std::string id;
  };
  std::vector<Rule> rules;

  // ja/constraints.tsv: arcs removed before scoring.
  struct Cond {
    uint8_t what = 0;  // 0: text before the arc ends with one of `alts`; 1: text after starts with one;
                       // 2: char before is in `mask`; 3: char after is in `mask`
    bool neg = false;
    std::vector<std::u32string> alts;
    uint32_t mask = 0;
  };
  struct Constraint {
    std::string id;
    std::string lemma, surf, cform, ud;  // empty = any; ud and cform are prefixes
    int pos = -1;                         // Pos, -1 = any
    std::vector<Cond> when, unless;
  };
  std::vector<Constraint> constraints;

  // Space handling and stutter costs (params.tsv).
  int32_t joinCost = 150;      // crossing one space inside a spaced run (通 知 表)
  uint16_t stutterCls = 0;
  int16_t stutterRule = -1;  // derived at Finalize

  // Long-unit arcs (overlay).
  struct Long {
    uint32_t key = 0;
    uint16_t keyLen = 0;
    uint16_t lcls = 0, rcls = 0;
    uint16_t ud = 0;
    uint8_t ctype = 0, cform = 0;
    Pos pos = Pos::n;
    int32_t cost = 0;
    Str lemma, read;
  };
  std::vector<Long> longs;  // sorted by key

  // ---- derived at Finalize ----
  struct Node {
    uint32_t child = 0, nChild = 0;
    uint32_t first = 0, count = 0;  // groups (or longs) whose key ends here
  };
  struct Trie {
    std::vector<Node> nodes;
    std::vector<char32_t> edgeChar;
    std::vector<uint32_t> edgeNode;
    std::vector<uint32_t> rootDirect;  // BMP code point → child of the root
    uint32_t Child(uint32_t n, char32_t c) const;  // 0 = none (root is node 0)
  };
  Trie trie, longTrie;
  std::vector<Pos> classPos;
  uint32_t nClass = 0;
  std::vector<int16_t> conn;  // [r * nClass + l]
  std::vector<uint8_t> bmpPrimary;
  std::vector<uint32_t> bmpMask;
  uint32_t maxLongLen = 0;

  int Conn(uint16_t prev, uint16_t next) const { return conn[static_cast<size_t>(prev) * nClass + next]; }
  uint8_t Primary(char32_t c) const;
  uint32_t Mask(char32_t c) const;
};

// Character classes for rule conditions.
enum CharClass : uint32_t { ccHira = 1, ccKata = 2, ccKanji = 4, ccAlpha = 8, ccDigit = 16, ccSym = 32, ccOther = 64,
                            ccEnd = 128 };
uint32_t CharClassOf(char32_t c);

// data/ TSV files → model. Long units are not part of the core.
bool LoadTsvDir(const std::string& dir, Model* m, std::string* err);
// Long-unit TSV (key, pos, lcls, rcls, cost, ud, ctype, cform, lemma, reading) → model longs.
bool LoadLongTsv(const std::string& path, Model* m, std::string* err);
void Finalize(Model* m);
void FinalizeLongs(Model* m);
std::string Serialize(const Model& m);
std::string SerializeLongs(const Model& m);
bool Deserialize(std::string_view bytes, Model* m, std::string* err);
bool DeserializeLongs(std::string_view bytes, Model* m, std::string* err);

// Resolves a class by names; returns -1 when absent.
int FindClass(const Model& m, std::string_view ud, std::string_view ctype, std::string_view cform,
              std::string_view lemma);

}  // namespace hd8::ana
