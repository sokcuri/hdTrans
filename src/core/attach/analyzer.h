// The rule analyzer of the attachment provider (docs/attachment.md): a normalizer and a lattice over the
// lexicon of its snapshot whose costs are explicit connection and preference rules.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::attach::ana {

struct Span {
    uint32_t begin = 0, end = 0;  // UTF-8 byte offsets into the source line
};

enum class EditKind : uint8_t { width, join, ruby };  // layout only; spoken forms are derived arcs

struct NormEdit {
    Span src;
    std::string from, to;
    EditKind kind = EditKind::width;
};

enum class Pos : uint8_t {
    n,
    pn,
    num,
    suf,
    pref,
    v1,
    v5,
    vs,
    vk,
    vz,
    adj,
    na,
    adv,
    adn,
    conj,
    intj,
    aux,
    p_case,
    p_topic,
    p_conj,
    p_final,
    p_adv,
    sym,
    unk
};

enum TokFlag : uint16_t {
    Unknown = 1,
    Fallback = 2,
    LongUnit = 4,
    UserArc = 8,
    NameCand = 16,
    Spoken = 32,
    Elongated = 64,
    SpaceBefore = 128,
    SinoReading = 256
};

struct Token {
    Span src;
    std::string surf;     // text of the token in the normalized line
    std::string lemma;    // UniDic lemma (語彙素), or the long-unit key
    std::string reading;  // katakana reading of surf; empty when unknown
    Pos pos = Pos::unk;
    uint16_t ud = 0;               // UniDic POS id (pos1-4), Analyzer::UdName
    uint8_t ctype = 0, cform = 0;  // UniDic cType/cForm ids, 0 = none
    uint16_t flags = 0;
    float cost = 0, margin = 0;  // margin: best path avoiding this arc minus best path cost
    uint16_t alt = 0, nAlt = 0;  // range in Analysis::alts
    uint16_t cls = 0;            // connection class (right class for long units)
    std::string derive;          // ja/derive.tsv row that produced this arc, empty = none
    std::string removedBy;       // alternatives only: ja/constraints.tsv row that removed the arc
};

struct Analysis {
    std::string norm;
    std::vector<uint32_t> map;  // norm byte → src byte; size norm.size() + 1
    std::vector<NormEdit> edits;
    std::vector<Token> toks;
    std::vector<Token> alts;
    std::vector<Token> alt;  // path 1: best path differing at the lowest-margin span, when margin < altTau
    float altMargin = 0;
};

struct Options {
    bool margins = true;         // compute margins and alternatives (backward pass)
    float altTau = 0.5f;         // keep alternatives within this cost of the best path
    bool longUnits = true;       // use overlay arcs
    bool splitLongUnits = true;  // emit the short-unit parts of a long unit (false: the unit as one token)
};

struct Model;
struct Workspace;

class Analyzer {
public:
    Analyzer();
    ~Analyzer();
    Analyzer(const Analyzer&) = delete;
    Analyzer& operator=(const Analyzer&) = delete;

    // Loads a snapshot made by PackModel. The bytes are copied.
    bool Load(std::string_view snapshot, std::string* err);

    // Thread-safe on one loaded analyzer. A workspace may be reused by one thread to avoid allocations.
    void Analyze(std::string_view src, const Options& opt, Analysis* out, Workspace* ws = nullptr) const;

    std::string_view UdName(uint16_t ud) const;
    std::string_view CtypeName(uint8_t ctype) const;
    std::string_view CformName(uint8_t cform) const;
    static std::string_view PosName(Pos p);
    static std::string_view FlagName(uint16_t bit);
    static std::string_view EditName(EditKind k);

    const Model* model() const { return model_.get(); }
    Model* mutableModel() { return model_.get(); }

private:
    std::unique_ptr<Model> model_;
};

struct WorkspaceDeleter {
    void operator()(Workspace* ws) const;
};
using WorkspacePtr = std::unique_ptr<Workspace, WorkspaceDeleter>;
WorkspacePtr NewWorkspace();

}  // namespace hdTrans::attach::ana
