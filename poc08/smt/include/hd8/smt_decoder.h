#pragma once
// Monotone phrase-based decoder over the analyzer's segmentation lattice.
// Every score is a fixed-point integer (features: log10 × 10^4; weights × 10^3), summed in int64; ties
// break on hypothesis creation order, which is deterministic, so output is bit-identical across builds.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hd8::smt {

class KoText;

enum Feature : int { kLm, kPef, kPfe, kLef, kLfe, kPhrase, kWord, kOov, kLat, kGloss, kTm, kRule, kNumFeatures };
const char* FeatureName(int f);

struct DecodeOptions {
  int beam = 64;
  int nbest = 0;        // > 0: print n-best lines "k ||| tokens ||| features" after the 1-best
  bool why = false;     // print the phrases used, with scores and provenance, after the line
  bool tokens = false;  // print Korean tokens instead of the recombined line
  bool join = true;
  bool lattice = true;  // use the analyzer's alternatives as lattice edges
  float latTau = 2.0f;  // alternatives within this cost of the best path
  int maxOpts = 20;     // options per source span
  std::string level = "haera";       // speech level of final endings (from the caller's discourse state)
  std::string quoteLevel = "";       // level inside quotation brackets; "" = level
  // Rule-engine translation of one unknown noun-like token (names, terms): "" = keep the passthrough.
  std::function<std::string(const std::string&)> oov;
};

struct DecoderImpl;

class Decoder {
 public:
  Decoder();
  ~Decoder();
  // model: folder with phrases.tsv, lm.bin, weights.tsv and optionally tm.tsv (whole-sentence memory).
  // analyzer: W1 core snapshot.
  bool Load(const std::string& model, const std::string& analyzer, const std::string& weights,
            const std::string& glossary, std::string* err);
  std::string Translate(const std::string& line, const KoText& ko, const DecodeOptions& opt) const;

 private:
  std::unique_ptr<DecoderImpl> impl_;
};

}  // namespace hd8::smt
