#pragma once
// The attachment provider of the packs layer (core/word/attach.h, docs/attachment.md). The rule analyzer
// reads the text of a chunk; the structure stage groups its words into bunsetsu (C), folds them into clauses
// with their features and arguments (L), and classes each omitted subject by the constraint rules of
// data/zero.tsv (Z). The result is mapped onto the items of the chain the packs layer reads. The word stage
// keeps its own segmentation: only these annotations cross.
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/tokenizer/token.h"
#include "core/word/attach.h"

namespace hdTrans::word {
struct Chain;
}

namespace hdTrans::attach {

// The loaded analyzer and the structure tables; read-only after LoadData, shared by every thread.
struct Data;

// Loads an analyzer snapshot (the bytes of an analysis.bin) and the built-in structure tables. Null and err
// on a malformed snapshot.
std::shared_ptr<const Data> LoadData(std::string_view snapshot, std::string& err);

// The analyzer snapshot of the dictionary folders: the analysis.bin of the last folder that has one;
// previous again when it was loaded from that file unchanged. Null without one (err empty) or when it does
// not load (err says why).
std::shared_ptr<const Data> LoadFolders(const std::vector<std::string>& folders,
                                        const std::shared_ptr<const Data>& previous, std::string& err);

// The analysis of one chunk, made on the first request of a sentence in it.
struct ChunkAnalysis;
struct ChunkAnalysisDeleter {
    void operator()(ChunkAnalysis* analysis) const;
};

// The provider state of one chunk: data and the chunk's text (the pieces' texts joined, the text its tokens'
// sourceBegin and sourceEnd count in). Provider() is valid while the state lives.
struct ChunkAttach {
    const Data* data = nullptr;
    std::string_view text;
    std::unique_ptr<ChunkAnalysis, ChunkAnalysisDeleter> analysis;
    bool tried = false;

    word::AttachProvider Provider();
};

// The analyzer model folder (data/model of the analyzer's rule build: classes, lexicon, connrules … .tsv)
// as an analysis.bin, with the readings trimmed to what the analysis reads; empty and err on failure.
std::string CompileAnalysis(const std::string& modelDir, std::string& err);

// Fills out for the sentence chain whose items stand in tokens (the chunk's); false when the chunk has no
// analysis (it is longer than the analyzer reads, or holds no clause).
bool AttachChain(ChunkAttach& chunk, const tokenizer::Tokens& tokens, const word::Chain& chain, word::Attachment& out);

}  // namespace hdTrans::attach
