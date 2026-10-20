// Generated table; do not edit.
#include "core/dict/codes.h"

namespace hdTrans::dict::internal {

namespace {

constexpr PartPattern kPatterns[] = {
    {"P", "い형용사", "7**"},
    {"P", "고유명사", "13*"},
    {"P", "구", "9**"},
    {"P", "기호", "6**"},
    {"P", "대명사", "12*"},
    {"P", "동사 어간", "4**"},
    {"P", "명사", "1**"},
    {"P", "부사", "3**"},
    {"P", "어미", "5**"},
    {"P", "연체사", "0**"},
    {"P", "조사", "2**"},
    {"P", "형용동사", "8**"},
};

}  // namespace

std::span<const PartPattern> PartPatternTable() { return kPatterns; }

}  // namespace hdTrans::dict::internal
