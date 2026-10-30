#pragma once
// Korean side of the phrase-based layer: the morpheme token notation and its recombination.
//
// A Korean line is a sequence of tokens. A token that starts a word has no prefix; a token glued to the
// previous one starts with '+'. Abstract tokens:
//   V:<lemma>[~<cls>]      predicate lemma at the start of a word (가다, 걷다~D)
//   +L:<lemma>[~<cls>]     the same, glued to the text before it (「 +L:가다)
//   +X:<lemma>             light-verb lemma glued to the text before it (공부 +X:하다 → lemma 공부하다)
//   +V:<lemma>             copula-like lemma whose host is the text before it (학생 +V:이다)
//   +E|c|eomi|rel|mood|f   ending bundle of the pending lemma (c: F final, C conn, A adn, N nom, Q quote,
//                          S stem; f: p past, g -겠-, h -시-); a final may carry @level (round trip only)
//   +J:<notation>          particle in generator notation (을/를, 이/가, (으)로, 에)
// Literal text is written as it is; a literal that starts with '+' or '\' is escaped with '\'.
// Final endings carry no speech level in training data: the level comes from the caller (discourse
// state or the strict mapping), so recombination is Join(tokens, level).
#include <memory>
#include <string>
#include <vector>

namespace hd8gen {
struct Tables;
}

namespace hd8::smt {

class KoText {
 public:
  bool Load(const std::string& generatorDataKo, std::string* err);
  // Recombines tokens into a surface line; finals without @level take `level` (haera hae haeyo hapsyo).
  std::string Join(const std::vector<std::string>& toks, const std::string& level) const {
    return Join(toks, level, level);
  }
  // The same with a second level for finals inside quotation brackets (「」『』“”).
  std::string Join(const std::vector<std::string>& toks, const std::string& level, const std::string& quoteLevel) const;
  // Build-time queries for the segmenter. Bundle token (without '+') for a predicate word, or "" when no
  // bundle of the paradigm writes `surface`. want: any final conn adn nom; mood: decl q imp prop.
  std::string QueryPredicate(const std::string& lemma, const std::string& cls, const std::string& pos,
                             const std::string& host, const std::string& surface, const std::string& want,
                             const std::string& mood, std::string* level) const;
  bool CheckParticle(const std::string& prev, const std::string& notation, const std::string& surface) const;
  const hd8gen::Tables* tables() const { return tables_.get(); }

 private:
  std::shared_ptr<const hd8gen::Tables> tables_;
};

std::vector<std::string> SplitTokens(const std::string& line);

}  // namespace hd8::smt
