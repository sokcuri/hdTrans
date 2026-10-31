#pragma once
// What the structure stage reads and writes over one analysis: tokens, bunsetsu (C), clauses with their
// features and arguments (L) and the referent of an omitted subject (Z).
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hdTrans::attach {

struct Span {
    uint32_t begin = 0, end = 0;
};

// Interned strings; id 0 is the empty string. A table made over a base (Strings(&base)) finds the base's
// strings under their ids and adds its own after them, so the tables' names need no copy per line.
class Strings {
public:
    Strings() { Intern(""); }
    explicit Strings(const Strings* base) : base_(base), first_(static_cast<uint32_t>(base->Size())) {}
    uint32_t Intern(std::string_view s) {
        if (base_ != nullptr)
            if (const uint32_t id = base_->Find(s); id != 0 || s.empty()) return id;
        auto it = ids_.find(s);
        if (it != ids_.end()) return it->second;
        uint32_t id = first_ + static_cast<uint32_t>(all_.size());
        all_.emplace_back(s);
        ids_.emplace(all_.back(), id);
        return id;
    }
    uint32_t Find(std::string_view s) const {
        if (base_ != nullptr)
            if (const uint32_t id = base_->Find(s); id != 0) return id;
        auto it = ids_.find(s);
        return it == ids_.end() ? 0 : it->second;
    }
    const std::string& Get(uint32_t id) const {
        if (base_ != nullptr && id < first_) return base_->Get(id);
        const uint32_t k = id - first_;
        return k < all_.size() ? all_[k] : (base_ != nullptr ? base_->Get(0) : all_[0]);
    }
    std::size_t Size() const { return first_ + all_.size(); }

private:
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    const Strings* base_ = nullptr;
    uint32_t first_ = 0;
    std::deque<std::string> all_;  // stable: ids_ keys view into it
    std::unordered_map<std::string_view, uint32_t, Hash, std::equal_to<>> ids_;
};

enum class Pos : uint8_t { n, pn, num, suf, pref, v1, v5, vs, vk, vz, adj, na, adv, adn, conj, intj,
                           aux, p_case, p_topic, p_conj, p_final, p_adv, sym, unk };
enum TokFlag : uint16_t { Unknown = 1, Fallback = 2, LongUnit = 4, UserArc = 8, NameCand = 16, Spoken = 32,
                          Elongated = 64, SpaceBefore = 128 };

// ud, ctype and cform are interned UniDic strings here (ids into the analyzer tables in ir.h).
struct Token {
    Span src;
    uint32_t surf = 0, lemma = 0, reading = 0;
    uint32_t lex = 0;
    Pos pos = Pos::unk;
    uint32_t ud = 0;
    uint32_t ctype = 0, cform = 0;
    uint16_t flags = 0;
    float cost = 0, margin = 0;
};

enum class Ev : uint8_t { Lexical, Cued, Default, UnmatchedForm, Fallback, Dropped, NameGuess, User, Example };

struct Contrib {
    uint32_t feature;
    float value;
};

struct Decision {
    uint8_t stage = 0;
    Ev kind = Ev::Lexical;
    uint32_t rule = 0;
    Span src;
    float score = 0, margin = 0;
    uint8_t nAlt = 0;
    uint32_t runnerUp = 0;
    std::vector<uint32_t> because;
    std::vector<Contrib> contrib;
    uint8_t path = 0;
};

enum class Case : uint8_t { none, ga, wo, ni, de, to, kara, made, yori, he, no, wa, mo, nado, quote };

enum BunFlag : uint8_t { Topic = 1, CommaAfter = 2, QuoteClose = 4 };

struct Bunsetsu {
    uint16_t first = 0, last = 0, content = 0;
    int16_t head = -1;
    float headMargin = 0;
    int16_t head2 = -1;
    uint8_t type = 'N';  // N, P, A, C, I, Q
    Case kase = Case::none;
    uint8_t flags = 0;
};

enum class Role : uint8_t { subj, obj, dat, loc, from, with, to, cmp, topic, head };
enum class ArgHow : uint8_t { marked, topic, relhead, omitted, bare };

struct Arg {
    Role role = Role::subj;
    uint16_t bun = 0xFFFF;
    uint32_t lemma = 0;
    uint32_t cls = 0;
    uint8_t how = 0;  // ArgHow
};

struct Referent {
    uint8_t kind = 0;
    uint32_t entity = 0;
    uint16_t bun = 0xFFFF;
    float score = 0, margin = 0;
    uint32_t decision = 0;
};

enum class Link : uint8_t { final, te, tara, to, ba, nara, node, kara, noni, tame, you_ni, nagara, tari, adn, nom,
                            quote, ga, temo, shi, zu, renyo, time };
enum class LinkSub : uint8_t { none, uchi, toki, mae, ato, mama };
enum class Tense : uint8_t { nonpast, past };
enum class Aspect : uint8_t { none, te_iru, te_aru, te_shimau, te_oku, te_kuru, te_iku, te_miru };
enum class Pol : uint8_t { pos, neg };
enum class NegFn : uint8_t { none, plain, rhetorical, prohibitive, obligation, potential_neg, archaic_vol, dbl };
enum class Voice : uint8_t { active, rare, passive, potential, honorific, spontaneous, causative, caus_pass };
enum class Bene : uint8_t { none, give_out, give_in, receive };
enum Modality : uint32_t { Explanatory = 1, Conjecture = 2, Likeness = 4, Hearsay = 8, Want = 16, Must = 32, May = 64,
                           Volitional = 128, Advice = 256, Exclam = 512, Try = 1024, Cannot = 2048 };
enum class Force : uint8_t { decl, q, imp, prohib, prop, excl, request };
enum class Polite : uint8_t { plain, desu_masu, sonkei, kenjo, gozaimasu };
enum Jreg : uint16_t { CasualFinal = 1, Old = 2, Ssu = 4, Desuwa = 8, Archaic = 16, Rough = 32 };

struct Clause {
    uint16_t pred = 0, firstBun = 0, lastBun = 0;
    int16_t parent = -1;
    Link link = Link::final;
    LinkSub linkSub = LinkSub::none;  // for link=time
    Tense tense = Tense::nonpast;
    Aspect aspect = Aspect::none;
    Pol pol = Pol::pos;
    NegFn negFn = NegFn::none;
    Voice voice = Voice::active;
    Bene bene = Bene::none;
    uint32_t modality = 0;
    Force force = Force::decl;
    Polite polite = Polite::plain;
    uint16_t jreg = 0;
    std::vector<Arg> args;
    std::vector<uint16_t> consumed;
    std::vector<uint16_t> unfolded;
    Referent subj;    // writer Z
    Referent beneTo;  // writer Z
};

// One analysis path of a line as C and L see it.
struct Path {
    std::vector<Token> toks;
    std::vector<Bunsetsu> buns;
    std::vector<Clause> clauses;
};

struct Line {
    std::string doc;
    uint32_t id = 0;
    std::string src;
    Path path;
    std::vector<Decision> decisions;
};

}  // namespace hdTrans::attach
