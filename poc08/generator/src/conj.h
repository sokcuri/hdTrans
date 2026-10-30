#pragma once
// Predicate conjugation: one stem + one ending in notation → text. Internal to the generator.
#include <string>

#include "tables.h"

namespace hd8gen {

struct Stem {
    std::u32string text;      // without 다; for the copula: 이
    char pos = 'V';           // V A X C N P (P: after 었/겠)
    std::string cls = "REG";
    bool ae = false;
    bool honPrefinal = false; // the text ends in the honorific 시
};

// Attaches one ending in eomi.tsv notation ("어요", "(으)면", "습니다/ㅂ니다", "~").
std::u32string Attach(const Tables&, const Stem&, std::u32string form, const GenOptions&);

// The copula 이 + ending after host (the word it joins): 학생이야, 친구야, 친구였다, 학생입니다.
std::u32string CopulaAfter(const Tables&, std::u32string_view host, const std::u32string& form, const GenOptions&);

Stem StemOf(const Tables&, const std::string& lemma, const std::string& pos, const std::string& cls);

// Realizes a particle notation (이/가, (으)로, (이)나, 도) after a word.
std::string RealizeParticle(const std::string& form, std::u32string_view prev);

}  // namespace hd8gen
