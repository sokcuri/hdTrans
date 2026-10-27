#include "core/pipeline/normalize.h"
#include "core/pipeline/pattern.h"

namespace hdTrans::pipeline::internal {

namespace {

// An optional part at pos (part(pos, rest) tries it and then rest), taken when it lets
// rest match, skipped otherwise.
template <class Part, class Rest>
bool Optional(size_t pos, Part part, Rest rest) {
    return part(pos, rest) || rest(pos);
}

// 《([^《》]{1,30})》 at p, then the end of the match.
bool Reading(std::string_view s, size_t p, Match& m) {
    if (!Lit(s, p, "《")) return false;
    return Repeat(s, p, [](char32_t c) { return c != U'《' && c != U'》'; }, 1, 30, [&](size_t q) {
        size_t r = q;
        if (!Lit(s, r, "》")) return false;
        m.end = r;
        return true;
    });
}

// [|｜]([^|｜《》]{1,20})《([^《》]{1,30})》, base as group 1
bool BarRuby(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"|｜")) return false;
    return Repeat(s, p, [](char32_t c) { return !In(c, U"|｜《》"); }, 1, 20, [&](size_t q) {
        m.group[1] = Span(s, p, q);
        return Reading(s, q, m);
    });
}

// ([一-龥々〆ヶ]+)《([ぁ-ヿ　 ]{1,30})》, base as group 3: without the bar only a kana reading is ruby,
// and a title in 《》 after kanji (是《胧月夜》) stays.
bool KanjiRuby(std::string_view s, size_t pos, Match& m) {
    return Repeat(s, pos, [](char32_t c) { return IsKanjiClass(c) || c == U'〆' || c == U'ヶ'; }, 1, kUnbounded,
                  [&](size_t q) {
                      m.group[3] = Span(s, pos, q);
                      size_t p = q;
                      if (!Lit(s, p, "《")) return false;
                      return Repeat(s, p, [](char32_t c) { return (c >= U'ぁ' && c <= U'ヿ') || c == U'　' || c == U' '; }, 1, 30,
                                    [&](size_t r) {
                                        if (!Lit(s, r, "》")) return false;
                                        m.end = r;
                                        return true;
                                    });
                  });
}

// <rp>[^<]*</rp> at p, then rest.
template <class Rest>
bool Paren(std::string_view s, size_t p, Rest rest) {
    if (!Lit(s, p, "<rp>")) return false;
    return Repeat(s, p, [](char32_t c) { return c != U'<'; }, 0, kUnbounded, [&](size_t q) {
        return Lit(s, q, "</rp>") && rest(q);
    });
}

// <ruby>(?:<rb>)?([^<]{1,20})(?:</rb>)?(?:<rp>[^<]*</rp>)?<rt>([^<]{1,30})</rt>(?:<rp>[^<]*</rp>)?</ruby>,
// base as group 5
bool HtmlRuby(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "<ruby>")) return false;
    auto notOpen = [](char32_t c) { return c != U'<'; };
    auto end = [&](size_t e) {
        if (!Lit(s, e, "</ruby>")) return false;
        m.end = e;
        return true;
    };
    auto annotation = [&](size_t a) {
        if (!Lit(s, a, "<rt>")) return false;
        return Repeat(s, a, notOpen, 1, 30, [&](size_t b) {
            if (!Lit(s, b, "</rt>")) return false;
            return Optional(b, [&](size_t c, auto rest) { return Paren(s, c, rest); }, end);
        });
    };
    auto base = [&](size_t b) {
        return Repeat(s, b, notOpen, 1, 20, [&](size_t c) {
            m.group[5] = Span(s, b, c);
            auto afterRb = [&](size_t d) {
                return Optional(d, [&](size_t e, auto rest) { return Paren(s, e, rest); }, annotation);
            };
            return Optional(
                c, [&](size_t d, auto rest) { return Lit(s, d, "</rb>") && rest(d); }, afterRb);
        });
    };
    return Optional(p, [&](size_t q, auto rest) { return Lit(s, q, "<rb>") && rest(q); }, base);
}

bool RubyForm(std::string_view s, size_t pos, Match& m) {
    if (BarRuby(s, pos, m)) return true;
    m = Match{};
    if (KanjiRuby(s, pos, m)) return true;
    m = Match{};
    return HtmlRuby(s, pos, m);
}

// Whether s starts with a particle or the copula, which follow a noun base rather than
// continuing its word (に, を, は, が, も, の, で, と, ね, よ, さ, だ, じゃ, から, まで, より), or
// with a small vowel that draws out the base (お前ぇ).
bool ParticleStart(std::string_view s) {
    for (std::string_view w : {"から", "まで", "より", "じゃ"}) {
        if (s.starts_with(w)) return true;
    }
    size_t p = 0;
    return OneOf(s, p, U"にをはがものでとねよさだぁぃぅぇぉ");
}

// Whether s starts with a name suffix (くん, くーん, ちゃん, さん, さま, たん, ちゃま) that
// ends a word or comes before a particle: the base is a name (蒼くーん, 蒼くんは).
bool NameSuffixStart(std::string_view s) {
    for (std::string_view w : {"くーん", "ちゃーん", "さーん", "くん", "ちゃん", "さん", "さま", "たん", "ちゃま"}) {
        if (!s.starts_with(w)) continue;
        const std::string_view rest = s.substr(w.size());
        size_t p = 0;
        // くんだ is also a verb ending (書くんだ).
        const bool particle = ParticleStart(rest) && !rest.starts_with("だ");
        if (rest.empty() || !Take(rest, p, IsHiraganaClass) || particle) return true;
    }
    return false;
}

// {rb}([^{}]{1,20}){/rb}{rt}([^{}]{1,30}){/rt}([ぁ-ゖ]*), the ruby tags of script markup:
// base as group 1, reading as group 2, the kana after the tags as group 3 and the rest of
// the line as group 4.
bool BraceRuby(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "{rb}")) return false;
    auto notBrace = [](char32_t c) { return c != U'{' && c != U'}'; };
    return Repeat(s, p, notBrace, 1, 20, [&](size_t q) {
        m.group[1] = Span(s, p, q);
        if (!Lit(s, q, "{/rb}{rt}")) return false;
        return Repeat(s, q, notBrace, 1, 30, [&](size_t r) {
            m.group[2] = Span(s, q, r);
            if (!Lit(s, r, "{/rt}")) return false;
            size_t e = r;
            while (Take(s, e, IsHiraganaClass)) {
            }
            m.group[3] = Span(s, r, e);
            m.group[4] = s.substr(e);
            m.end = e;
            return true;
        });
    });
}

}  // namespace

std::string StripRuby(std::string_view line) {
    if (line.find("《") == std::string_view::npos && line.find('<') == std::string_view::npos) {
        return std::string(line);
    }
    return ReplaceAll(line, RubyForm, [](const Match& m) { return Cat({m.group[1], m.group[3], m.group[5]}); });
}

}  // namespace hdTrans::pipeline::internal

namespace hdTrans::pipeline {

std::string JoinBraceRuby(std::string_view line, ReadBase read, const void* context) {
    if (line.find("{rb}") == std::string_view::npos) return std::string(line);
    return internal::ReplaceAll(line, internal::BraceRuby, [&](const internal::Match& m) {
        const std::string_view base = m.group[1], reading = m.group[2], kana = m.group[3];
        const std::string_view after = line.substr(line.size() - m.group[4].size() - kana.size());
        const auto kept = [&] { return internal::Cat({"{rb}", base, "{/rb}{rt}", reading, "{/rt}", kana}); };
        if (kana.empty() || internal::NameSuffixStart(after)) return kept();
        const BaseReading r = read(context, base, kana);
        const bool join = text::RuneCount(base) == 1 ? !internal::ParticleStart(kana) || r.inflected
                                                     : r.continues && !internal::ParticleStart(kana);
        if (!join) return kept();
        const std::string_view in = kana.substr(0, r.wordEnd > 0 && r.wordEnd < kana.size() ? r.wordEnd : kana.size());
        return internal::Cat({"{rb}", base, in, "{/rb}{rt}", reading, in, "{/rt}", kana.substr(in.size())});
    });
}

}  // namespace hdTrans::pipeline
