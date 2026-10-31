#pragma once
// Predicate conjugation: one stem and one ending in eomi.tsv notation to text.
#include <string>
#include <string_view>

#include "core/register/tables.h"

namespace hdTrans::reg {

struct PredicateInfo;

struct Stem {
    std::u32string text;  // without 다; for the copula 이
    char pos = 'V';       // V A X C N P (P: after 었/겠)
    std::string cls = "REG";
    bool ae = false;
    bool honPrefinal = false;  // the text ends in the honorific 시
};

// Attaches one ending ("어요", "(으)면", "습니다/ㅂ니다", "~").
std::u32string Attach(const Tables& t, const Stem& s, std::u32string form);

// The copula 이 and an ending after host: 학생이야, 친구야, 친구였다, 학생입니다.
std::u32string CopulaAfter(const Tables& t, std::u32string_view host, const std::u32string& form);

// info: LookupPredicate of lemma.
Stem StemOf(const std::string& lemma, const std::string& pos, const std::string& cls, const PredicateInfo& info);

}  // namespace hdTrans::reg
