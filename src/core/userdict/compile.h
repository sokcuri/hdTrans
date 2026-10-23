#pragma once
// The compilation of one plain-dictionary line, shared by the plain format and the
// sense table.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/dict.h"
#include "core/text/lifetime.h"
#include "core/userdict/conjugating.h"

namespace hdTrans::userdict::internal {

// One record of a line, with its codes as text.
struct Rec {
    std::string key, target, part, sem, gram;
    bool name = false;
    bool honorific = false;
    bool end = false;
    bool polite = false;
    bool euImperative = false;
};

// The records of a line with POS pos (the plain-format names: n, v, adj, end, ...),
// conjugation class cls and, for hon rows, space. base, which may be null, resolves
// る-verbs whose class the lemma does not show. Returns an error message, empty on
// success.
std::string Compile(std::string_view jp, std::string_view kr, std::string_view pos, std::string_view cls, bool space,
                    const dict::Dict* base, Families& families, std::vector<Rec>& out);

// The ending record of an end or pat row: jp and kr are a model word (書く 쓰다,
// 高い 높다, 静か 조용하다 or 学生 학생) with the ending. Returns an error message.
std::string CompileEnding(std::string_view jp, std::string_view kr, Rec& r);

// Whether jp is polite Japanese (ます, です, ございます in any form).
bool PoliteJP(std::string_view jp);

// The conjugation analysis of a verb with key and kind (part code byte 3) on tail,
// the text after the key: the branch the tail conjugates (endings that attach to it
// have part code byte 2 '0'+branch) and the length of the conjugation in bytes;
// false when no branch matches.
bool AnalyseVerbTail(std::string_view key, char kind, std::string_view tail, int& branch, std::size_t& length);

// Text helpers over UTF-8, with the white space of text::IsSpace.
std::u32string Runes(std::string_view s);
std::string Utf8(std::u32string_view r);
std::string_view TrimSpace(std::string_view s HDTRANS_LIFETIMEBOUND);
std::vector<std::string_view> Fields(std::string_view s);
std::vector<std::string_view> Split(std::string_view s, char sep);
// s in double quotes, with quotes, backslashes and control bytes escaped.
std::string Quote(std::string_view s);

}  // namespace hdTrans::userdict::internal
