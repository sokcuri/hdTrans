#include <algorithm>
#include <string>
#include <string_view>

#include "core/pipeline/protect.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/sound.h"

namespace hdTrans::pipeline::internal {

namespace {

// The longest dictionary keys the variant pass looks for, in characters.
constexpr size_t kMaxVariantKey = 12;

bool In(char32_t r, std::u32string_view set) { return set.find(r) != std::u32string_view::npos; }

// Whether a key of the dictionaries (plain and kana spellings included) is s.
bool IsKey(std::u32string_view s, const LineContext& ctx, const dict::Snapshot& snap) {
    return !KeyTags(EncodeText(s), ctx, snap).empty();
}

// Whether a key of two characters or more holds rs[p..last]: it starts at or after from, within
// six characters before p, and ends after last.
bool ReadsAsKeys(std::u32string_view s, const LineContext& ctx, const dict::Snapshot& snap);

// Whether a key of two characters or more holds rs[p..last]: it starts at or after from, within
// six characters before p, and ends after last; a key of two characters only where the kana after
// it up to the end of the run read as words or start with a kana that ends a stem (すき, すごい,
// するね; not すが of しゅがみん).
bool Held(std::u32string_view rs, size_t from, size_t p, size_t last, const LineContext& ctx, const dict::Snapshot& snap) {
    size_t runEnd = last;
    while (runEnd < rs.size() && IsHiraganaRune(rs[runEnd])) runEnd++;
    for (size_t s = p > from + 6 ? p - 6 : from; s <= p; s++) {
        for (size_t e = std::min(rs.size(), s + kMaxVariantKey); e > last && e >= s + 2; e--) {
            if (!IsKey(rs.substr(s, e - s), ctx, snap)) continue;
            if (e - s >= 3) return true;
            const std::u32string_view rest = rs.substr(e, runEnd > e ? runEnd - e : 0);
            // At the end of the run, before no katakana or kanji (not すご of しゅごキャラ).
            const bool closed = rest.empty() && (runEnd >= rs.size() || !(IsKatakanaRune(rs[runEnd]) || IsKanjiRune(rs[runEnd])));
            if (closed || (!rest.empty() && (In(rest[0], U"いくけかっるれろらりんしさせそぁぃぅぇぉー") || ReadsAsKeys(rest, ctx, snap)))) {
                return true;
            }
        }
    }
    return false;
}

// Whether a key of three characters or more ends at end in rs, starting at or after from and at or
// before p.
bool Ends(std::u32string_view rs, size_t from, size_t p, size_t end, const LineContext& ctx, const dict::Snapshot& snap) {
    for (size_t s = end > from + kMaxVariantKey ? end - kMaxVariantKey : from; s + 3 <= end && s <= p; s++) {
        if (IsKey(rs.substr(s, end - s), ctx, snap)) return true;
    }
    return false;
}

// The run of kana and kanji around rs[p]: where a key that holds it may start.
size_t WordFrom(std::u32string_view rs, size_t p) {
    size_t s = p;
    while (s > 0 && (IsHiraganaRune(rs[s - 1]) || IsKanjiRune(rs[s - 1]) || rs[s - 1] == U'ー')) s--;
    return s;
}

// A spelling of baby talk or of a cat's speech and the kana it stands for (しゅき 好き, ごめんなちゃい,
// しちゅれい, ご主人ちゃま, にゃい).
struct Lisp {
    std::u32string_view said, meant;
};
constexpr Lisp kLisps[] = {{U"しゅ", U"す"}, {U"ちゅれ", U"つれ"}, {U"なちゃい", U"なさい"}, {U"だちゃい", U"ださい"}, {U"ちゃま", U"さま"},
                           {U"にゃ", U"な"}, {U"にゅ", U"ぬ"}, {U"にょ", U"の"}};
// Kana a lisping speaker says for だ and ど, taken only in a line with another lisp (らいしゅき 大好き).
constexpr Lisp kLispsAfter[] = {{U"ら", U"だ"}, {U"ろ", U"ど"}};

// rs with each lisp of lisps that no key holds as said and a key holds as meant spelled as meant;
// changed tells whether one was.
std::u32string Unlisp(std::u32string rs, std::span<const Lisp> lisps, const LineContext& ctx, const dict::Snapshot& snap,
                      bool& changed) {
    for (size_t p = 0; p < rs.size(); p++) {
        if (!IsHiraganaRune(rs[p])) continue;
        for (const Lisp& l : lisps) {
            if (std::u32string_view(rs).substr(p).compare(0, l.said.size(), l.said) != 0) continue;
            // お子ちゃま is a word of its own (어린애).
            if (l.said == U"ちゃま" && p > 0 && rs[p - 1] == U'こ') continue;
            const size_t from = WordFrom(rs, p);
            const size_t saidEnd = p + l.said.size();
            size_t runEnd = saidEnd;
            while (runEnd < rs.size() && IsHiraganaRune(rs[runEnd])) runEnd++;
            // A cat's にゃ, にゅ, にょ closes no sentence and starts no cry here (仕方ないにゃー, にゃらーん):
            // a kana goes on after it and a key of three characters holds it respelled.
            const bool cat = l.said.starts_with(U"に");
            if (cat && (saidEnd >= rs.size() || !(IsHiraganaRune(rs[saidEnd]) || IsKanjiRune(rs[saidEnd])) ||
                        In(rs[saidEnd], U"ぁぃぅぇぉゃゅょ"))) {
                continue;
            }
            // Held as said by a key that holds more than the spelling (しゅくだい, こんにゃく, お子ちゃま), or
            // a name of the name layer (けんしゅけ, ぱんにゃ).
            bool said = false;
            for (size_t s = p > from + 6 ? p - 6 : from; s <= p && !said; s++) {
                for (size_t e = std::min(rs.size(), s + kMaxVariantKey); e >= saidEnd && e > s + l.said.size() && !said; e--) {
                    said = IsKey(std::u32string_view(rs).substr(s, e - s), ctx, snap);
                }
                for (size_t e = saidEnd; e <= runEnd && !said; e++) said = snap.NamePerson(EncodeText(std::u32string_view(rs).substr(s, e - s)));
            }
            // A sound of the sound lexicon (しゅたっ, しゅる).
            for (size_t e = saidEnd + 1; e <= runEnd && !said; e++) said = IsSoundKey(ToKatakana(std::u32string_view(rs).substr(p, e - p)));
            // A short run that a small tsu closes is a mimetic word (しゅたっ, しゅるっ).
            if (runEnd < rs.size() + 1 && runEnd - from <= 4 && runEnd > from && rs[runEnd - 1] == U'っ') said = true;
            if (said) continue;
            std::u32string meant = rs.substr(0, p) + std::u32string(l.meant) + rs.substr(saidEnd);
            // A key that holds the respelling and reaches the kana after it (にゃいの ない; not んにゃ、 んな),
            // or one of three characters or more that ends with it (ごめんなさい, ご主人さま).
            const size_t last = p + l.meant.size();
            const bool atEnd = last >= meant.size() || !IsHiraganaRune(meant[last]);
            const bool ends = Ends(meant, from, p, last, ctx, snap);
            // A cat's にゃい closing its run is ない (問題にゃい): a key from it to the end of the run, before no
            // long mark.
            size_t meantEnd = p;
            while (meantEnd < meant.size() && IsHiraganaRune(meant[meantEnd])) meantEnd++;
            const bool closing = cat && meantEnd > last &&
                                 (meantEnd >= meant.size() || !(meant[meantEnd] == U'ー' || meant[meantEnd] == U'～' ||
                                                                (meant[meantEnd] >= 0xE000 && meant[meantEnd] <= 0xF8FF))) &&
                                 IsKey(std::u32string_view(meant).substr(p, meantEnd - p), ctx, snap);
            if (!ends && !closing && (cat || atEnd || !Held(meant, from, p, last, ctx, snap))) continue;
            rs = std::move(meant);
            changed = true;
            break;
        }
    }
    return rs;
}

bool ReadsAsKeys(std::u32string_view s, const LineContext& ctx, const dict::Snapshot& snap);

// The vowel kana that a long mark after kana stands for: あ, い, う, い for the e row, う for the o
// row; 0 for none.
char32_t LongVowel(char32_t kana) {
    static constexpr std::u32string_view rows[] = {U"あかさたなはまやらわがざだばぱぁゃ", U"いきしちにひみりぎじぢびぴぃ",
                                                   U"うくすつぬふむゆるぐずづぶぷぅゅ", U"えけせてねへめれげぜでべぺぇ",
                                                   U"おこそとのほもよろをごぞどぼぽぉょ"};
    static constexpr char32_t vowels[] = {U'あ', U'い', U'う', U'い', U'う'};
    for (int v = 0; v < 5; v++) {
        if (In(kana, rows[v])) return vowels[v];
    }
    return 0;
}

// rs with the long mark of いい, どう, そう and こう at the start of a run of hiragana spelled as the
// vowel where kana that read as words or the end of the run follow (いーから いいから, どーなんや,
// そーだよ, いーじゃん); other long marks the sound stage draws out (うーん, かなーって, だったー).
std::u32string SpellLongVowels(std::u32string rs, const LineContext& ctx, const dict::Snapshot& snap, bool& changed) {
    for (size_t q = 1; q < rs.size(); q++) {
        // One mark after the kana, which starts the run after no kanji or mark (not なーーいーー), or follows
        // kana that read as words ending in a particle (とかいーから; not うんそー).
        if (rs[q] != U'ー' || !In(rs[q - 1], U"いどそこ") || (q + 1 < rs.size() && (rs[q + 1] == U'ー' || rs[q + 1] == U'～')) ||
            (q >= 2 && (IsKanjiRune(rs[q - 2]) || IsKatakanaRune(rs[q - 2]) || rs[q - 2] == U'～' || rs[q - 2] == U'ー'))) {
            continue;
        }
        if (q >= 2 && IsHiraganaRune(rs[q - 2])) {
            const size_t from = WordFrom(rs, q - 1);
            const std::u32string_view lead = std::u32string_view(rs).substr(from, q - 1 - from);
            if (lead.size() < 2 || IsKanjiRune(lead[0]) || !In(lead.back(), U"とかもはがのよねって") || !ReadsAsKeys(lead, ctx, snap)) continue;
        }
        std::u32string spelled = rs;
        spelled[q] = rs[q - 1] == U'い' ? U'い' : U'う';
        size_t end = q + 1;
        while (end < spelled.size() && IsHiraganaRune(spelled[end])) end++;
        const std::u32string_view rest = std::u32string_view(spelled).substr(q + 1, end - q - 1);
        // A name before an honorific (こーくん) is NicknamesToHangul's.
        if (rest.starts_with(U"くん") || rest.starts_with(U"ちゃん") || rest.starts_with(U"さん")) continue;
        // Not before a run drawn out syllable by syllable (いーつーねーるー), nor where a key has the
        // mark as written (そーなんだ).
        const bool drawn = end < spelled.size() && (spelled[end] == U'ー' || (spelled[end] >= 0xE000 && spelled[end] <= 0xF8FF));
        if (drawn && end - q - 1 <= 2) continue;
        bool written = false;
        for (size_t k = q + 1; k <= end && !written; k++) written = IsKey(std::u32string_view(rs).substr(q - 1, k - q + 1), ctx, snap);
        if (written) continue;
        if (!IsKey(std::u32string_view(spelled).substr(q - 1, 2), ctx, snap) || !ReadsAsKeys(rest, ctx, snap)) continue;
        rs = std::move(spelled);
        changed = true;
    }
    return rs;
}

// rs with a long mark inside a run of hiragana spelled as the vowel it stands for (しょーもない
// しょうもない, かいちょーだ かいちょうだ, かわいー かわいい), where the run is no name and a key of three
// characters or more, longer than any key that holds the mark as written, holds the vowel while the
// kana before and after it read as words. The vowel is that of the row (う after the o row, い after the e row), or お and
// え (とーい, ねーさん). Marks drawn twice or before a drawn run stay the sound stage's.
std::u32string SpellInnerLongVowels(std::u32string rs, const LineContext& ctx, const dict::Snapshot& snap, bool& changed) {
    for (size_t q = 1; q < rs.size(); q++) {
        if (rs[q] != U'ー' || !IsHiraganaRune(rs[q - 1]) || In(rs[q - 1], U"ぁぃぅぇぉっんゎ")) continue;
        if (q + 1 < rs.size() && (rs[q + 1] == U'ー' || rs[q + 1] == U'～' || rs[q + 1] == U'〜' || In(rs[q + 1], U"っぁぃぅぇぉ") ||
                                  (rs[q + 1] >= 0xE000 && rs[q + 1] <= 0xF8FF))) {
            continue;
        }
        const size_t from = WordFrom(rs, q - 1);
        size_t runEnd = q + 1;
        while (runEnd < rs.size() && IsHiraganaRune(rs[runEnd])) runEnd++;
        // The longest key that holds the mark as written (そーなんだ, ちょー of 超); a name holding it
        // (ほっしー) keeps the mark.
        size_t written = 0;
        bool name = false;
        for (size_t st = q > from + 6 ? q - 6 : from; st < q && !name; st++) {
            for (size_t e = q + 1; e <= runEnd && !name; e++) {
                const std::u32string_view w = std::u32string_view(rs).substr(st, e - st);
                if (IsKey(w, ctx, snap)) written = std::max(written, e - st);
                name = snap.NamePerson(EncodeText(w));
            }
        }
        if (name) continue;
        const char32_t row = LongVowel(rs[q - 1]);
        if (row == 0) continue;
        std::u32string vowels(1, row);
        if (row == U'う' && In(rs[q - 1], U"おこそとのほもよろごぞどぼぽ")) vowels += U'お';  // not after ょ
        if (row == U'い' && In(rs[q - 1], U"えけせてねへめれげぜでべぺ")) vowels += U'え';
        for (const char32_t v : vowels) {
            std::u32string spelled = rs;
            spelled[q] = v;
            // The longest key of three characters or more that holds the vowel, from within six
            // characters before it.
            size_t ks = 0, ke = 0;
            for (size_t st = q > from + 6 ? q - 6 : from; st < q; st++) {
                for (size_t e = std::min(runEnd, st + kMaxVariantKey); e > q && e >= st + 3; e--) {
                    if (e - st > ke - ks && e - st > written && IsKey(std::u32string_view(spelled).substr(st, e - st), ctx, snap)) {
                        ks = st;
                        ke = e;
                    }
                }
            }
            // A key that starts with a small kana is no word of its own (っこう of そっこー).
            if (ke == 0 || In(spelled[ks], U"ぁぃぅぇぉっゃゅょゎ")) continue;
            // A vowel that ends the key draws out the end of a word (見るなー, してー, だねー) unless the key is
            // the whole word from the start of the run (せんせー, かいちょー), after no particle or ending.
            if (ke == q + 1 && (ks != from || In(rs[q - 1], U"なねよてでわさぞぜかのや"))) continue;
            // A key that reads only as nouns is mostly a kanji word spelled in kana (さんそう 산장, せいへん
            // 정변): it must start the run and stand before a particle, the copula or no more kana
            // (かいちょーだ, せんせーの, しょくぎょー研究; not さんそーぎょらーい, 存在せーへん).
            const std::string tags = KeyTags(EncodeText(std::u32string_view(spelled).substr(ks, ke - ks)), ctx, snap);
            if (tags.find_first_not_of("NIK") == std::string::npos) {
                const std::u32string_view tail = std::u32string_view(spelled).substr(ke, runEnd - ke);
                const bool ends = ke < spelled.size() && spelled[ke] == U'ー';
                if (ks != from || ends || (!tail.empty() && !In(tail[0], U"だでじがはをにのもとやへかよね"))) continue;
            }
            // A key that the mark ends is an adjective (きれー) or a word of four kana or more (せんせー,
            // かいちょー; not そこー, which the sound stage leaves of そっこー).
            if (ke == q + 1 && ke - ks < 4 && tags.find_first_of("GH") == std::string::npos) continue;
            // A name before an honorific (こーくん) is NicknamesToHangul's.
            const std::u32string_view after = std::u32string_view(spelled).substr(q + 1);
            if (after.starts_with(U"くん") || after.starts_with(U"ちゃん") || after.starts_with(U"さん") || after.starts_with(U"様")) continue;
            // The kana before the key from the last kanji on, and the kana after it, read as words; the
            // copula だ may follow (かいちょうだ), and the kana of an ending a stem key (しょうもな) takes.
            std::u32string_view lead = std::u32string_view(spelled).substr(from, ks - from);
            size_t kanji = lead.size();
            while (kanji > 0 && !IsKanjiRune(lead[kanji - 1])) kanji--;
            lead.remove_prefix(kanji);
            std::u32string_view rest = std::u32string_view(spelled).substr(ke, runEnd - ke);
            if (rest.starts_with(U"だ")) rest.remove_prefix(1);
            // A stem key (しょうもな) goes on with the kana of its ending (い).
            if (!rest.empty() && !ReadsAsKeys(rest, ctx, snap) && In(rest[0], U"いくけかっるれろらりんしさせそ")) rest.remove_prefix(1);
            if ((!lead.empty() && !ReadsAsKeys(lead, ctx, snap)) || !ReadsAsKeys(rest, ctx, snap)) continue;
            rs = std::move(spelled);
            changed = true;
            break;
        }
    }
    return rs;
}

// Whether r is a small vowel that a loanword in hiragana writes (ふぁ, てぃ).
bool LoanKana(char32_t r) { return In(r, U"ぁぃぅぇぉ"); }

// The particles and copulas that may close a loanword written in hiragana.
constexpr std::u32string_view kLoanTails[] = {U"ですが", U"でした", U"です", U"だよ", U"だ", U"って", U"が", U"は", U"を", U"に",
                                              U"と",    U"の",    U"も",   U"で",   U"や", U"へ",   U"か", U"よ", U"ね", U""};

// The katakana of a loanword written in hiragana, its long vowels written ー after the e and o rows
// (ちょこれいと チョコレート), if a dictionary key spells it; empty otherwise.
std::u32string LoanKey(std::u32string_view h, const LineContext& ctx, const dict::Snapshot& snap) {
    std::u32string k = ToKatakana(h);
    if (IsKey(k, ctx, snap)) return k;
    std::u32string longed;
    for (size_t i = 0; i < h.size(); i++) {
        const char32_t v = i > 0 ? LongVowel(h[i - 1]) : 0;
        if ((h[i] == U'い' && v == U'い' && In(h[i - 1], U"えけせてねへめれげぜでべぺぇ")) || (h[i] == U'う' && v == U'う' && In(h[i - 1], U"おこそとのほもよろごぞどぼぽぉょ"))) {
            longed += U'ー';
        } else {
            longed += ToKatakana(h.substr(i, 1));
        }
    }
    return longed != k && IsKey(longed, ctx, snap) ? longed : std::u32string();
}

// Whether s reads as keys of two characters or more and one-kana particles, longest first.
bool ReadsAsKeys(std::u32string_view s, const LineContext& ctx, const dict::Snapshot& snap) {
    while (!s.empty()) {
        size_t take = 0;
        for (size_t e = std::min(s.size(), kMaxVariantKey); e >= 2 && take == 0; e--) {
            // A key that ends in a small tsu is a clipped word, no reading of the run (せっ of せっくす).
            if (s[e - 1] != U'っ' && IsKey(s.substr(0, e), ctx, snap)) take = e;
        }
        if (take == 0) {
            if (!In(s[0], U"はがをにともへでやのかよねなわぞぜさ")) return false;
            take = 1;
        }
        s.remove_prefix(take);
    }
    return true;
}

// Whether a native word starts h: a key of two kana or more not ending in a small tsu (しゃべる,
// かっこいい, ほっとく, くそったれ).
bool NativeStart(std::u32string_view h, const LineContext& ctx, const dict::Snapshot& snap) {
    for (size_t e = 2; e < h.size(); e++) {
        if (h[e - 1] == U'っ') continue;
        if (!KeyTags(EncodeText(h.substr(0, e)), ctx, snap).empty()) return true;
    }
    return false;
}

// rs with each whole run of hiragana that writes a loanword with a small vowel, a small tsu or a
// long mark (ふぁいん, ちょこれいと, せっくす, あだるてぃー), and reads as no words, in katakana
// where a key spells it, before the particle or copula that closes the run.
std::u32string LoanwordsInHiragana(std::u32string rs, const LineContext& ctx, const dict::Snapshot& snap, bool& changed) {
    for (size_t i = 0; i < rs.size();) {
        if (!IsHiraganaRune(rs[i]) || (i > 0 && (IsHiraganaRune(rs[i - 1]) || IsKanjiRune(rs[i - 1]) || IsKatakanaRune(rs[i - 1])))) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < rs.size() && (IsHiraganaRune(rs[j]) || rs[j] == U'ー')) j++;
        const std::u32string_view run = std::u32string_view(rs).substr(i, j - i);
        std::u32string key;
        size_t core = 0;
        for (const std::u32string_view tail : kLoanTails) {
            if (run.size() < tail.size() + 3 || run.substr(run.size() - tail.size()) != tail) continue;
            const std::u32string_view h = run.substr(0, run.size() - tail.size());
            // A small vowel marks a loanword (ふぁいん, あだるてぃー); a small tsu, a small y kana or a long
            // mark only in four kana or more (せっくす, ちょこれいと, ぶらぼー; not あっと, あーと, えっち).
            const bool vowel = std::any_of(h.begin(), h.end(), LoanKana);
            const bool weak = std::any_of(h.begin(), h.end(), [](char32_t r) { return In(r, U"っゃゅょー"); });
            if (!(vowel || (weak && h.size() >= 4)) || IsKey(h, ctx, snap) || ReadsAsKeys(h, ctx, snap) || NativeStart(h, ctx, snap)) continue;
            key = LoanKey(h, ctx, snap);
            if (!key.empty()) {
                core = h.size();
                break;
            }
        }
        if (!key.empty()) {
            rs = rs.substr(0, i) + key + rs.substr(i + core);
            changed = true;
            j = i + key.size() + (run.size() - core);
        }
        i = j;
    }
    return rs;
}

// rs with the drawn う of a polite ending before a mark or the end dropped (ですう です, ますう).
std::u32string PoliteDrawnU(std::u32string rs, bool& changed) {
    for (const std::u32string_view end : {std::u32string_view(U"です"), std::u32string_view(U"ます")}) {
        for (size_t p = rs.find(end); p != std::u32string::npos; p = rs.find(end, p + 1)) {
            size_t q = p + end.size();
            size_t u = q;
            while (u < rs.size() && rs[u] == U'う') u++;
            // One う only: a longer run is a cry the sound stage draws out (ですううう).
            if (u != q + 1 || (u < rs.size() && IsHiraganaRune(rs[u]))) continue;
            rs.erase(q, u - q);
            changed = true;
        }
    }
    return rs;
}

// Whether a dictionary key other than a name is s (クリトリス).
bool CommonKey(std::u32string_view s, const dict::Snapshot& snap) {
    const std::string key = EncodeText(s);
    for (const dict::Candidate& c : snap.RetrieveRecords(key, dict::LookupMode::General)) {
        if (c.codes != nullptr && c.source == key && !c.codes->sem.HasPrefix("QPN")) return true;
    }
    return false;
}

}  // namespace

std::string JoinPauseCommas(std::string_view text, const dict::Snapshot& snap) {
    std::u32string rs = DecodeText(text);
    bool changed = false;
    for (size_t i = 1; i + 1 < rs.size(); i++) {
        if (rs[i] != U'、') continue;
        const char32_t before = rs[i - 1], next = rs[i + 1];
        bool join = false;
        // A predicate, then a final particle that ends the utterance (ない、よ？ ないよ？, いい、の？). Not か,
        // whose pause muses (世は巡る、か), the adverbial なく (比喩じゃなく、ね), また (また、ね) or an
        // interjection in ん (ん、なっ…).
        const std::u32string_view head = std::u32string_view(rs).substr(0, i);
        if (IsHiraganaRune(before) && In(before, U"いただるうくすつぬむぶぐ") && In(next, U"よのねなわぞ") && !head.ends_with(U"なく") &&
            !head.ends_with(U"また")) {
            size_t k = i + 2;
            if (k < rs.size() && In(rs[k], U"ねなよわ")) k++;  // よね, のよ
            while (k < rs.size() && In(rs[k], U"ーっぉぇ～〜")) k++;
            join = k == rs.size() || In(rs[k], U"？?！!…‥。」』");
        }
        // A word in kanji or katakana, then さま (ご主人、さまぁ ご主人さまぁ).
        if (!join && (IsKanjiRune(before) || IsKatakanaRune(before))) {
            const std::u32string_view rest = std::u32string_view(rs).substr(i + 1);
            // さま and 様 only: a name before a hesitant さん or ちゃん may not be a word (占守、ちゃん).
            join = rest.starts_with(U"さま") || rest.starts_with(U"様");
            // Not the adverb ちゃんと (モノ、ちゃんと抱えて) or a word that starts with 様 (医療品、様々な).
            if (rest.starts_with(U"ちゃんと") || (rest.starts_with(U"様") && rest.size() > 1 && (IsKanjiRune(rest[1]) || rest[1] == U'々'))) {
                join = false;
            }
        }
        // A katakana word split by a pause whose halves are no words of their own and whose whole is
        // (クリ、トリス クリトリス; not パン、ケーキ).
        if (!join && IsKatakanaRune(before) && IsKatakanaRune(next)) {
            size_t a = i, b = i + 1;
            while (a > 0 && IsKatakanaRune(rs[a - 1])) a--;
            while (b < rs.size() && IsKatakanaRune(rs[b])) b++;
            const std::u32string_view left = std::u32string_view(rs).substr(a, i - a), right = std::u32string_view(rs).substr(i + 1, b - i - 1);
            join = left.size() >= 2 && right.size() >= 2 && !CommonKey(left, snap) && !CommonKey(right, snap) &&
                   CommonKey(std::u32string(left) + std::u32string(right), snap);
        }
        if (!join) continue;
        rs.erase(i, 1);
        changed = true;
    }
    return changed ? EncodeText(rs) : std::string(text);
}


std::string RewriteSpellingVariants(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    std::u32string rs = DecodeText(text);
    if (std::none_of(rs.begin(), rs.end(), IsHiraganaRune)) return std::string(text);
    bool changed = false, lisped = false;
    rs = Unlisp(std::move(rs), kLisps, ctx, snap, lisped);
    if (lisped) rs = Unlisp(std::move(rs), kLispsAfter, ctx, snap, lisped);
    changed = lisped;
    rs = SpellLongVowels(std::move(rs), ctx, snap, changed);
    rs = SpellInnerLongVowels(std::move(rs), ctx, snap, changed);
    rs = LoanwordsInHiragana(std::move(rs), ctx, snap, changed);
    rs = PoliteDrawnU(std::move(rs), changed);
    return changed ? EncodeText(rs) : std::string(text);
}

}  // namespace hdTrans::pipeline::internal
