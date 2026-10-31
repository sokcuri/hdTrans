#pragma once
// The 0.8 IR (IR.md is the contract). One writer per field; the comment names it.
// Enum values carry the names written in JSON (IR.md §7): Name(F::x, v) and Parse(F::x, s, v).
#include <cstdint>
#include <string>
#include <string_view>
#include <deque>
#include <utility>
#include <unordered_map>
#include <vector>

namespace hd08 {

// X-macro lists: the index is the stored value, the string is the JSON name.
#define HD08_EV(X) X(Lexical, "lexical") X(Cued, "cued") X(Default, "default") X(UnmatchedForm, "unmatched-form") \
    X(Fallback, "fallback") X(Dropped, "dropped") X(NameGuess, "name-guess") X(User, "user") X(Example, "example")
#define HD08_POS(X) X(n, "n") X(pn, "pn") X(num, "num") X(suf, "suf") X(pref, "pref") X(v1, "v1") X(v5, "v5") \
    X(vs, "vs") X(vk, "vk") X(vz, "vz") X(adj, "adj") X(na, "na") X(adv, "adv") X(adn, "adn") X(conj, "conj")    \
    X(intj, "intj") X(aux, "aux") X(p_case, "p_case") X(p_topic, "p_topic") X(p_conj, "p_conj")                   \
    X(p_final, "p_final") X(p_adv, "p_adv") X(sym, "sym") X(unk, "unk")
#define HD08_TOKFLAG(X) X(Unknown, "unknown") X(Fallback, "fallback") X(LongUnit, "long-unit") X(UserArc, "user-arc") \
    X(NameCand, "name-cand") X(Spoken, "spoken") X(Elongated, "elongated") X(SpaceBefore, "space-before") \
    X(SinoReading, "sino-reading")
#define HD08_EDIT(X) X(width, "width") X(elong, "elong") X(spoken, "spoken") X(join, "join") X(ruby, "ruby")
#define HD08_SRCLEVEL(X) X(plain, "plain") X(desu_masu, "desu-masu") X(keigo, "keigo") X(archaic, "archaic") \
    X(casual, "casual")
#define HD08_CASE(X) X(none, "none") X(ga, "ga") X(wo, "wo") X(ni, "ni") X(de, "de") X(to, "to") X(kara, "kara") \
    X(made, "made") X(yori, "yori") X(he, "he") X(no, "no") X(wa, "wa") X(mo, "mo") X(nado, "nado") X(quote, "quote")
#define HD08_BUNTYPE(X) X(N, "N") X(P, "P") X(A, "A") X(C, "C") X(I, "I") X(Q, "Q")
#define HD08_BUNFLAG(X) X(topic, "topic") X(commaAfter, "comma-after") X(quoteClose, "quote-close")
#define HD08_ROLE(X) X(subj, "subj") X(obj, "obj") X(dat, "dat") X(loc, "loc") X(from, "from") X(with, "with") \
    X(to, "to") X(cmp, "cmp") X(topic, "topic") X(head, "head")
#define HD08_HOW(X) X(marked, "marked") X(topic, "topic") X(relhead, "relhead") X(omitted, "omitted") X(bare, "bare")
#define HD08_LINK(X) X(final, "final") X(te, "te") X(tara, "tara") X(to, "to") X(ba, "ba") X(nara, "nara") \
    X(node, "node") X(kara, "kara") X(noni, "noni") X(tame, "tame") X(you_ni, "you-ni") X(nagara, "nagara")  \
    X(tari, "tari") X(adn, "adn") X(nom, "nom") X(quote, "quote") X(ga, "ga") X(temo, "temo") X(shi, "shi") \
    X(zu, "zu") X(renyo, "renyo") X(time, "time")
// v2d: the kind of a `time` link (Clause.linkSub).
#define HD08_TIMESUB(X) X(none, "none") X(uchi, "uchi") X(toki, "toki") X(mae, "mae") X(ato, "ato") X(mama, "mama")
#define HD08_TENSE(X) X(nonpast, "nonpast") X(past, "past")
#define HD08_JASPECT(X) X(none, "none") X(te_iru, "te-iru") X(te_aru, "te-aru") X(te_shimau, "te-shimau") \
    X(te_oku, "te-oku") X(te_kuru, "te-kuru") X(te_iku, "te-iku") X(te_miru, "te-miru")
#define HD08_POL(X) X(pos, "pos") X(neg, "neg")
#define HD08_NEGFN(X) X(none, "none") X(plain, "plain") X(rhetorical, "rhetorical") X(prohibitive, "prohibitive") \
    X(obligation, "obligation") X(potential_neg, "potential-neg") X(archaic_vol, "archaic-vol") X(double_, "double")
#define HD08_VOICE(X) X(active, "active") X(rare, "rare") X(passive, "passive") X(potential, "potential") \
    X(honorific, "honorific") X(spontaneous, "spontaneous") X(causative, "causative") X(caus_pass, "caus-pass")
#define HD08_BENE(X) X(none, "none") X(give_out, "give-out") X(give_in, "give-in") X(receive, "receive")
#define HD08_MODALITY(X) X(explanatory, "explanatory") X(conjecture, "conjecture") X(likeness, "likeness") \
    X(hearsay, "hearsay") X(want, "want") X(must, "must") X(may, "may") X(volitional, "volitional")          \
    X(advice, "advice") X(exclam, "exclam") X(try_, "try") X(cannot, "cannot")
#define HD08_FORCE(X) X(decl, "decl") X(q, "q") X(imp, "imp") X(prohib, "prohib") X(prop, "prop") X(excl, "excl") \
    X(request, "request")
#define HD08_POLITE(X) X(plain, "plain") X(desu_masu, "desu-masu") X(sonkei, "sonkei") X(kenjo, "kenjo") \
    X(gozaimasu, "gozaimasu")
#define HD08_JREG(X) X(casual_final, "casual-final") X(old, "old") X(ssu, "ssu") X(desuwa, "desuwa") \
    X(archaic, "archaic") X(rough, "rough")
#define HD08_KK(X) X(N, "N") X(P, "P") X(V, "V") X(Adv, "Adv") X(Det, "Det") X(Intj, "Intj") X(Punct, "Punct") \
    X(Raw, "Raw")
#define HD08_KCLS(X) X(REG, "REG") X(HA, "HA") X(D, "D") X(B, "B") X(BWA, "BWA") X(S, "S") X(H, "H") X(REU, "REU") \
    X(REO, "REO") X(U, "U") X(EU, "EU") X(L, "L") X(COP, "COP") X(EXIST, "EXIST")
#define HD08_SEM(X) X(person, "person") X(place, "place") X(time, "time") X(counter, "counter") X(num, "num") \
    X(sino, "sino") X(native, "native") X(animate, "animate")
#define HD08_KASPECT(X) X(simple, "simple") X(prog, "prog") X(result, "result") X(compl_, "compl") \
    X(dir_come, "dir-come") X(dir_go, "dir-go") X(try_, "try") X(give, "give")
#define HD08_MOOD(X) X(decl, "decl") X(q, "q") X(imp, "imp") X(prop, "prop") X(excl, "excl")
#define HD08_CTYPE(X) X(final, "final") X(conn, "conn") X(adn_nonpast, "adn:nonpast") X(adn_past, "adn:past") \
    X(adn_fut, "adn:fut") X(nom_m, "nom:m") X(nom_gi, "nom:gi") X(nom_geot, "nom:geot") X(quote, "quote") \
    X(quote_adn, "quote:adn") X(stem, "stem") X(adn_retro, "adn:retro") X(adn_past_retro, "adn:past-retro")
#define HD08_REL(X) X(seq, "seq") X(and_, "and") X(cause, "cause") X(cond, "cond") X(concess, "concess") \
    X(purpose, "purpose") X(while_, "while") X(when, "when") X(quote, "quote") X(like, "like") X(sothat, "sothat") \
    X(even_try, "even-try") X(reason, "reason") X(bg, "bg") X(purpose_motion, "purpose-motion") X(switch_, "switch") \
    X(until, "until") X(result, "result") X(neg, "neg") X(as_if, "as-if") X(cause_gi, "cause-gi") \
    X(if_quote, "if-quote") X(after, "after") X(as_soon, "as-soon") X(despite, "despite") X(even_if, "even-if") \
    X(whether, "whether") X(the_more, "the-more") X(manner, "manner") X(or_, "or")
#define HD08_KNEG(X) X(none, "none") X(an, "an") X(ji_anh, "ji-anh") X(mot, "mot") X(ji_mot, "ji-mot") \
    X(ji_ma, "ji-ma")
#define HD08_JAFACT(X) X(te_iru, "te-iru") X(past_state, "past-state") X(likeness, "likeness") X(link_seq, "link-seq") \
    X(sonkei, "sonkei")
#define HD08_KASE(X) X(subj, "subj") X(top, "top") X(obj, "obj") X(gen, "gen") X(dat, "dat") X(src, "src") \
    X(loc, "loc") X(instr, "instr") X(com, "com") X(quot, "quot") X(voc, "voc") X(LEX, "LEX")
#define HD08_PUNCT(X) X(comma, "comma") X(pause, "pause") X(end, "end") X(q, "q") X(excl, "excl") X(qopen, "qopen") \
    X(qclose, "qclose") X(ellipsis, "ellipsis") X(dash, "dash") X(mark, "mark")
#define HD08_LEVEL(X) X(haera, "haera") X(hae, "hae") X(haeyo, "haeyo") X(hapsyo, "hapsyo") X(hao, "hao") \
    X(hage, "hage")
#define HD08_PERSONA(X) X(none, "none") X(rough, "rough") X(old_male, "old-male") X(lady, "lady") X(formal, "formal") \
    X(pompous, "pompous") X(archaic, "archaic") X(grand, "grand")
#define HD08_FLAG(X) X(fallback, "fallback") X(dropped, "dropped") X(added, "added") X(flip, "flip") \
    X(residue, "residue") X(carry, "carry") X(unmatched_form, "unmatched-form") X(check, "check")
#define HD08_REFKIND(X) X(none, "none") X(overt, "overt") X(speaker, "speaker") X(addressee, "addressee") \
    X(third, "third") X(generic, "generic")
// v2f (W5): what the generator needs beyond IR v1.
#define HD08_VPOS(X) X(none, "") X(V, "V") X(A, "A") X(X_, "X") X(C, "C") X(N, "N")
#define HD08_MODAL(X) X(none, "") X(gess, "gess")
// Lexicon row status of the row a T decision used (Decision.st); none when no lexicon row.
#define HD08_ST(X) X(none, "") X(r, "r") X(m, "m") X(u, "u")

#define HD08_ENUM_VALUE(id, name) id,
#define HD08_ENUM(ns, list) namespace ns { enum : uint8_t { list(HD08_ENUM_VALUE) count }; }
#define HD08_BIT_VALUE(id, name) id##_bit,
#define HD08_BITS(ns, list) namespace ns { enum : uint8_t { list(HD08_BIT_VALUE) count }; \
    enum : uint32_t { list(HD08_BIT_MASK) }; }
#define HD08_BIT_MASK(id, name) id = 1u << id##_bit,

enum class Ev : uint8_t { HD08_EV(HD08_ENUM_VALUE) };
enum class Pos : uint8_t { HD08_POS(HD08_ENUM_VALUE) };
enum class Case : uint8_t { HD08_CASE(HD08_ENUM_VALUE) };
enum class Role : uint8_t { HD08_ROLE(HD08_ENUM_VALUE) };
enum class KK : uint8_t { HD08_KK(HD08_ENUM_VALUE) };
HD08_BITS(tokflag, HD08_TOKFLAG)
HD08_ENUM(edit, HD08_EDIT)
HD08_ENUM(srclevel, HD08_SRCLEVEL)
HD08_ENUM(buntype, HD08_BUNTYPE)
HD08_BITS(bunflag, HD08_BUNFLAG)
HD08_ENUM(how, HD08_HOW)
HD08_ENUM(link, HD08_LINK)
HD08_ENUM(timesub, HD08_TIMESUB)
HD08_ENUM(tense, HD08_TENSE)
HD08_ENUM(jaspect, HD08_JASPECT)
HD08_ENUM(pol, HD08_POL)
HD08_ENUM(negfn, HD08_NEGFN)
HD08_ENUM(voice, HD08_VOICE)
HD08_ENUM(bene, HD08_BENE)
HD08_BITS(modality, HD08_MODALITY)
HD08_ENUM(force, HD08_FORCE)
HD08_ENUM(polite, HD08_POLITE)
HD08_BITS(jreg, HD08_JREG)
HD08_ENUM(kcls, HD08_KCLS)
HD08_BITS(sem, HD08_SEM)
HD08_ENUM(kaspect, HD08_KASPECT)
HD08_ENUM(mood, HD08_MOOD)
HD08_ENUM(ctype, HD08_CTYPE)
HD08_ENUM(rel, HD08_REL)
HD08_ENUM(kneg, HD08_KNEG)
HD08_BITS(jafact, HD08_JAFACT)
HD08_ENUM(kase, HD08_KASE)
HD08_ENUM(punct, HD08_PUNCT)
HD08_ENUM(level, HD08_LEVEL)
HD08_ENUM(persona, HD08_PERSONA)
HD08_ENUM(flag, HD08_FLAG)
HD08_ENUM(st, HD08_ST)
HD08_ENUM(refkind, HD08_REFKIND)
HD08_ENUM(vpos, HD08_VPOS)
HD08_ENUM(modal, HD08_MODAL)

// The enum-like fields, for Name/Parse.
enum class F : uint8_t {
    ev, pos, tokflag, edit, srclevel, kase_ja, buntype, bunflag, role, how, link, tense, jaspect, pol, negfn, voice,
    bene, modality, force, polite, jreg, kk, kcls, sem, kaspect, mood, ctype, rel, kneg, jafact, kase, punct, level,
    persona, flag, st, refkind, timesub, vpos, modal
};
std::string_view Name(F field, unsigned value);  // empty when out of range
bool Parse(F field, std::string_view name, unsigned& value);
bool IsBits(F field);
unsigned Count(F field);

// Interned strings; id 0 is the empty string. Ids are per Doc and never leave it as numbers.
struct Strings {
    uint32_t Intern(std::string_view s);
    std::string_view Str(uint32_t id) const { return id < all.size() ? std::string_view(all[id]) : std::string_view(); }
    uint32_t Find(std::string_view s) const;  // 0 when absent
    Strings();
    Strings(const Strings& other);
    Strings& operator=(const Strings& other);
    // Moving a deque keeps its elements in place, so the index keys stay valid.
    Strings(Strings&&) noexcept = default;
    Strings& operator=(Strings&&) noexcept = default;

private:
    std::deque<std::string> all;  // stable addresses for the index keys
    std::unordered_map<std::string_view, uint32_t> index;
    void Reindex();
};

struct Span { uint32_t begin = 0, end = 0; };
inline bool Overlaps(Span a, Span b) { return a.begin < b.end && b.begin < a.end; }
inline bool Contains(Span outer, Span inner) { return outer.begin <= inner.begin && inner.end <= outer.end; }

// §2 evidence
struct Decision {
    uint8_t stage = 0;  // 'A','S','C','L','Z','T','R','W','G'
    Ev kind = Ev::Lexical;
    uint32_t rule = 0;  // interned row id (IR.md §8)
    Span src;
    float score = 0, margin = 0;
    uint8_t nAlt = 0;
    uint32_t runnerUp = 0;
    std::vector<uint32_t> because;
    std::vector<std::pair<uint32_t, float>> contrib;  // v2: interned feature name → contribution to score
    uint8_t path = 0;   // v2: analysis path the winner used
    uint32_t flip = 0;  // v2b: interned smallest feature change that flips the winner, 0 none
    uint8_t st = 0;     // hd08::st value: lexicon row status of `rule` (r, m, u), set by T
};

// §3 analyzer
struct NormEdit { Span src; uint32_t from = 0, to = 0; uint8_t kind = 0; };
struct Token {
    Span src;
    uint32_t surf = 0, lemma = 0, reading = 0;
    uint32_t lex = 0;
    Pos pos = Pos::unk;
    uint16_t ud = 0;               // id in Doc::ud
    uint8_t ctype = 0, cform = 0;  // ids in Doc::ctypes, Doc::cforms
    uint16_t flags = 0;
    float cost = 0, margin = 0;
    uint16_t alt = 0, nAlt = 0;    // range in Line.alts
    uint32_t derive = 0;           // v2b: interned ja/derive.tsv row that produced this arc
    uint32_t removedBy = 0;        // v2b: Line.alts entries only: interned ja/constraints.tsv row that removed it
};

// §4 discourse scope
struct Utterance {
    Span span; uint8_t depth = 0;
    uint32_t scene = 0, speaker = 0, addressee = 0;
    bool narration = false;
    uint8_t sourceLevel = 0;
};
struct Mention { Span src; uint32_t entity = 0; float evidence = 0; uint32_t decision = 0; };
struct Speaker {
    uint32_t name = 0;
    uint16_t firstPerson[8] = {};
    uint16_t desuMasu = 0, plain = 0, keigo = 0;
    uint16_t finals[16] = {};
    uint8_t persona = 0;
};
struct Scene { uint32_t firstLine = 0, lastLine = 0; uint32_t decision = 0; };  // line indexes in Doc.lines
// A name span S hands back to A for one line (the single feedback loop). line is the 0-based index
// in Doc.lines, context lines included (not Line.id).
struct ForcedSpan { uint32_t line = 0; Span src; uint32_t entity = 0; };

// §5 structure
struct Bunsetsu {
    uint16_t first = 0, last = 0, content = 0;
    int16_t head = -1; float headMargin = 0;
    int16_t head2 = -1;
    uint8_t type = 0;
    Case kase = Case::none; uint8_t flags = 0;
};
struct Arg { Role role = Role::subj; uint16_t bun = 0; uint32_t lemma = 0; uint32_t cls = 0; uint8_t how = 0; };
struct Referent {                 // v2, writer Z
    uint8_t kind = 0;             // refkind
    uint32_t entity = 0;          // interned; overt args and S-named speakers only
    uint16_t bun = 0xFFFF;
    float score = 0, margin = 0;
    uint32_t decision = 0;
};
struct Clause {
    uint16_t pred = 0, firstBun = 0, lastBun = 0; int16_t parent = -1;
    uint8_t link = 0;
    uint8_t linkSub = 0;  // v2d: timesub value when link is time
    uint8_t tense = 0, aspect = 0, pol = 0, negFn = 0, voice = 0, bene = 0;
    uint32_t modality = 0;
    uint8_t force = 0, polite = 0;
    uint16_t jreg = 0;
    std::vector<Arg> args;
    std::vector<uint16_t> consumed;
    std::vector<uint16_t> unfolded;
    Referent subj;    // v2, Z
    Referent beneTo;  // v2, Z
};
struct Analysis { std::vector<Token> toks; std::vector<Bunsetsu> buns; std::vector<Clause> clauses; };  // v2 path 1
struct Chosen { uint8_t path = 0; std::vector<int16_t> heads; };  // v2, T: path and head per bunsetsu

// §6 Korean morpheme IR
struct KNode {
    uint32_t id = 0, origin = 0;
    KK kind = KK::Raw; uint32_t lemma = 0;
    uint8_t kcls = 0;
    uint32_t sem = 0;
    uint8_t tense = 0, aspect = 0, mood = 0, ctype = 0, rel = 0, neg = 0;
    uint8_t aux : 1 = 0, dep : 1 = 0, passive : 1 = 0, causative : 1 = 0;
    uint8_t potential : 1 = 0, spont : 1 = 0;  // v2e: -ㄹ 수 있다; -어지다 (spontaneous)
    uint8_t glue : 1 = 0;   // v2f: written without a space after the previous word
    uint8_t vpos = 0;       // v2f: V: verb A adjective X 있다/없다 C copula N 아니다 ("" = from the stem tables)
    uint8_t modal = 0;      // v2f: -겠- (gess)
    uint32_t eomi = 0;      // v2f: interned explicit ending ("-는데") or final family ("ne", "ji"); 0 = by features
    uint16_t ja = 0;
    uint8_t kase = 0;
    uint8_t punct = 0; uint32_t jaMark = 0;
    Span src; uint16_t clause = 0, utt = 0; uint32_t decision = 0;  // decision: 1-based index in Line.decisions, 0 none
};
struct KReg { uint32_t node = 0; uint8_t level = 0, persona = 0; bool hon = false; uint32_t decision = 0; };
struct UttLevel { uint16_t utt = 0; uint8_t level = 0, persona = 0; uint32_t decision = 0; };
struct OutSpan { uint32_t begin = 0, end = 0, node = 0; };  // out bytes → kir2 node id
struct Flag { uint8_t kind = 0; Span src; uint32_t node = 0; uint8_t inv = 0; };  // inv: invariant number I1–I8, 0 none

struct Line {
    uint32_t id = 0;              // driver: 1-based line number in the input
    std::string name;             // driver: case or record id when read from JSON
    bool context = false;         // driver: --context line, not written out
    std::string src;              // driver
    std::string norm;             // N
    std::vector<uint32_t> map;    // N: norm byte → src byte, norm.size() + 1 entries
    std::vector<NormEdit> edits;  // N
    std::vector<Token> toks;      // A
    std::vector<Token> alts;      // A
    std::vector<Utterance> utts;  // S
    std::vector<Mention> mentions;// S
    std::vector<Bunsetsu> buns;   // C
    std::vector<Clause> clauses;  // L (Clause.subj/beneTo: Z)
    Analysis alt;                 // v2 path 1: A toks, C buns, L clauses; empty when none
    Chosen chosen;                // v2, T
    std::vector<KNode> kir;       // T
    std::vector<KReg> reg;        // R
    std::vector<UttLevel> lev;    // R
    std::vector<KNode> kir2;      // W
    std::string out;              // G
    std::vector<OutSpan> spans;   // G
    std::vector<Flag> flags;      // A (fallback), G (invariant kinds), check (--check)
    std::vector<Decision> decisions;  // all, append only

    // Appends a decision and returns its 1-based id for KNode/Mention/KReg/UttLevel/Scene.decision.
    uint32_t Decide(Decision d) { decisions.push_back(std::move(d)); return static_cast<uint32_t>(decisions.size()); }
    const Decision* DecisionOf(uint32_t n) const { return n && n <= decisions.size() ? &decisions[n - 1] : nullptr; }
    std::string_view Src(Span s) const {
        return s.begin <= s.end && s.end <= src.size() ? std::string_view(src).substr(s.begin, s.end - s.begin)
                                                        : std::string_view();
    }
};

struct Doc {
    std::string id;               // driver
    bool wrapped = false;         // driver: --wrapped, lines break mid-sentence
    std::vector<Line> lines;      // driver
    std::vector<Scene> scenes;    // S
    std::vector<Speaker> speakers;// S, ids are 1-based
    Strings strs;                 // shared interner for every uint32 string id
    Strings ud, ctypes, cforms;   // UniDic POS, cType and cForm names (Token.ud/ctype/cform)

    uint32_t I(std::string_view s) { return strs.Intern(s); }
    std::string_view S(uint32_t n) const { return strs.Str(n); }
};

// The token, bunsetsu and clause vectors of analysis path 0 (the line's fields) or 1 (line.alt).
struct PathRef { std::vector<Token>& toks; std::vector<Bunsetsu>& buns; std::vector<Clause>& clauses; };
inline PathRef PathOf(Line& line, int path) {
    if (path == 0) return {line.toks, line.buns, line.clauses};
    return {line.alt.toks, line.alt.buns, line.alt.clauses};
}
struct ConstPathRef {
    const std::vector<Token>& toks;
    const std::vector<Bunsetsu>& buns;
    const std::vector<Clause>& clauses;
};
inline ConstPathRef PathOf(const Line& line, int path) {
    if (path == 0 || line.alt.toks.empty()) return {line.toks, line.buns, line.clauses};
    return {line.alt.toks, line.alt.buns, line.alt.clauses};
}

const KNode* FindNode(const std::vector<KNode>& nodes, uint32_t id);

}  // namespace hd08
