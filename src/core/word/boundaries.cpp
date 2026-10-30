#include "core/word/boundaries.h"

namespace hdTrans::word {

namespace {
BoundaryFn oracleFn = nullptr;
void* oracleCtx = nullptr;
}  // namespace

void SetBoundaryOracle(BoundaryFn fn, void* ctx) {
    oracleFn = fn;
    oracleCtx = ctx;
}

bool QueryBoundaries(std::string_view text, std::vector<uint8_t>* ends) {
    return oracleFn != nullptr && oracleFn(oracleCtx, text, ends);
}

}  // namespace hdTrans::word
