#pragma once
#include <cstdint>
#include <string>

namespace hdTrans::word {

enum class Provenance : uint8_t {
    None, Fixed, LocalRule, GlobalRule, Single, FirstXxx, Digit,
    Sense, SenseDefault, Initial, Rewrite, Scorer, Misread, Cg, Pack,
};

struct Selection {
    Provenance layer = Provenance::None;
    int id = -1;  // rule number, 1-based sense row, scorer index, or pack row (dict::PackRowById)
};

struct ShadowChoice {
    const char* scorer = nullptr;
    int from = -1;
    int candidate = -1;
    int currentScore = 0;
    int score = 0;
    bool eligible = false;
    bool winner = false;  // first eligible scorer that prefers another candidate
    std::string target;  // the candidate's target at selection time
};

const char* ProvenanceName(Provenance layer);

}  // namespace hdTrans::word
