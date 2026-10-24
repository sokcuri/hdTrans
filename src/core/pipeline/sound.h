#pragma once
// The sound stage of the text path: masked words, sounds, elongation and stutter
// written before the line is translated, and the spoken spellings inside words that
// need the dictionary to be recognised.
//
// A masked word (伏せ字: オ○ニー, ち○こ, セッ××ス) is one word with its mask: a row of
// the builtin masked.tsv writes its Korean with the mask as written (자○, 섹××); a
// katakana word with katakana on both sides of each mask and a one-syllable piece of a
// word next to it is transliterated around the masks (オ○ニ 오○니, but not キー○ボタン).
// A mask without kana around it (○×クイズ, ○月○日, ○○さん) stays, and so does a masked
// word that a dictionary key covers, so a user entry (オ○ニー 자위) wins.
//
// A sound of the builtin sound lexicon (sound.tsv, and the interjections in kana of
// phrases.tsv) is written in Korean, so the protect stage makes it a Hangul span that
// is read as a noun. Before する an ono row written KR/lemma is written as the lemma
// without 하다 (ぼーっとした 멍했다). Elongation after the sound (はぁぁぁ, うわぁー,
// ああああん, きゃああっ) lengthens the Korean (하아아아, 우와아아, 아아아앙, 꺄아악).
//
// Where a sound may stand:
//   - a katakana run is taken only when sounds cover it completely, and when they are
//     different, with keys of four or more characters (ドキドキワクワク, but not the
//     name シャーカーン); after a kanji the first key needs three or more characters;
//   - a hiragana sound starts its hiragana run (after a kanji, katakana, letter or
//     digit only when it is an ono row with a key of four or more characters that does
//     not start with a particle), follows another sound, or, for ono rows, follows a
//     lone particle; the text between sounds is read as the longest dictionary words;
//   - sounds in a row are written with a space between those of two or more syllables
//     (あっあっ 앗앗, うふふふっ 우후후훗, もうやだ 정말 싫어), but not after a cry that ends
//     in a kana drawing out the vowel before it, before another cry (ひぃやあっ 히이야앗,
//     ふぁーふぁー 후아아후아아); the last copy of a cry row said again and clipped with ッ closes
//     its last syllable with ㅅ (やだやだっ 싫어 싫엇);
//   - katakana that only draw out the vowel of the hiragana before them are no sound of
//     their own (ひぃやアアア);
//   - an intj row must not be followed by kana, except a hiragana one by katakana
//     (はわわユウキ), and an exclaim row (よかった 다행이다) in hiragana as written, without
//     elongation, not by a question mark, after an ellipsis or not (よかった？ is the
//     question 좋았어?, but よかったぁ? the exclamation 다행이다~?), nor, a predicate row, after a noun in
//     kanji or katakana with は or が and a comma (あの頃は、よかった 좋았다); an ono row may be followed by anything, and と or the て of って
//     after it is dropped (ぎゅっと 꽉), also before a katakana word that is no sound
//     (スタスタとベッド 척척 ベッド, but ドキドキとワクワク). A space separates a sound from a
//     following kanji or verb (ぽりぽりかく), and after a dropped particle from kana, but
//     not from a form of する;
//   - a と after a sound and a pause (、 ！ ？ or an ellipsis) quotes the sound and is
//     written 하고 after the pause (ぱさり、と 툭, 하고; ダンッ！　と 쾅! 하고), unless it
//     starts a longer word (うん、とても), another と or する follows it, or a pause follows
//     it after an ellipsis (やっ……と、, a stutter);
//   - a lead row (おい、 ほら！ ちょっと…) is a sound only at the start of an utterance
//     and before a pause, and then wins over the dictionary words; an open row is a
//     lead row that needs no pause (ってかなんで 그보다 왜);
//   - a dictionary word longer than the sound with its elongation, or as long when
//     there is elongation (パターン is not ぱたん lengthened), and a plain-dictionary
//     entry at least as long as the sound's key (without a final と), win; the ending
//     of an end row wins only after no lone particle and where no kana that are no
//     lengthening go on after it (うさぎがぴょんと 폴짝, 読んでぴょん 뿅, 猫ぴょんと 폴짝;
//     報告ぴょん 보고다뿅), not over an intj row before a pause or an ellipsis, nor over
//     a sound with no word right before it that ends in its と or comes before no kanji or
//     katakana (敬礼！ぴょん！ 뿅), and it is no word where it may not end the sentence
//     (関連でちょこっと: で + ちょこっと).
//
// A katakana word that starts an utterance or follows 、 or an ellipsis, before ！と or
// ！っと, that an ono row reads alone or with ッ is that sound even where a dictionary word
// reads it (ドン！っと 쾅!).
//
// A katakana mimetic the lexicon lacks before と or っと (a run ending in ッ, or a unit of two
// to four characters said twice, that no dictionary key reads) is written in hiragana when
// it ends in ッ and a dictionary word reads it so (ジッと じっと), otherwise as heard with
// the と dropped (メキメキと 메키메키).
//
// A hiragana mimetic the lexicon lacks before と or っと (a unit of two plain kana said
// twice that starts a hiragana run or follows a lone particle, and that no dictionary key
// of four or more characters reads) is written as heard with the と dropped (とてとてと
// 토테토테).
//
// A lone っ or ッ that an utterance holds after nothing but an ellipsis or a dash, before
// a pause, an ellipsis, a dash or the end, is a caught breath (……ッ、 ……읏,).
//
// A burst of breath that stands alone (かはっ, だっは, かッハ……) and that no dictionary
// word reads is written as heard (카핫, 닷하, 캇하) rather than as particles.
//
// A stutter (な、なにを, だっ、だめ, べ…別に, ご、ごめ、ごめんなさい) becomes stutter marks
// before each separator; the restore stage writes them as the start of the next Korean
// word (WriteStutters).
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/pipeline/line_context.h"

namespace hdTrans::pipeline {

// The normalized line after the sound stage: JoinSpacedOut, JoinSpelledOut, WriteMasked, CompleteCutOffs,
// RewriteDrawnHonorifics,
// RewriteVolitionalSo, RewriteContractedIku, RewriteContractedAuxiliaries, RewriteRaLessKuru,
// RewriteContractedNo,
// RewriteOnbinU, WriteSounds, then RewriteCasualAdjectives;
// when RewriteEmphasis changes that text, WriteSounds of the rewritten text instead; then
// WriteCries.
// Markup tags, BBCode tags, entities and ctx.markers are left as they are. The lookups
// use ctx.lookup.
std::string Sound(std::string_view normalized, const LineContext& ctx, const dict::Snapshot& snap);

namespace internal {

// Each word spelled out with ・ between its characters for emphasis (さ・い・て・い, 大・好・き,
// ダ・メ) joined and put between kEmphasisOpen and kEmphasisClose: two or more pieces of one
// kana or kanji each (with a small kana and one ー, ～ or っ after it; after the first piece
// also a lone small kana, っ or ー), not next to other kana, kanji or ・ but for a copula or
// particle after it (さ・い・て・いです). A run of kanji only (赤・青・黄, 謹・賀・新・年) stays.
// Two pieces are joined only before ！？!?…‥、。」』） and not when both are particles (て・を).
// A run that starts with katakana is joined only when it is a dictionary key of two or more
// characters without ー, and two pieces not when the key is a person's name and no common
// word (ア・イ, スー・チー, ユ・リ; but マ・ジ？). Other runs, their kana in hiragana, are joined
// when the dictionary or the sound lexicon reads them as words. A run of kana that is not, a
// list in the order of the kana table or the iroha (あ・い・う, は・ひ・ふ・へ・ほ), the choices
// after a label (①あ・い, 問１　あ・い), a name (か・ん・こ・れ) or a run with a lone ー or small
// kana piece is written piece by piece as heard, ・ between the pieces and a lone ー kept
// (아・이・우, あ・ー 아・ー).
std::string JoinSpelledOut(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// Each word spaced out character by character (お　と　な　の, こ ろ し て や る) joined: three or
// more pieces of JoinSpelledOut with one space or ideographic space between them, not next to
// other kana or kanji but for a copula or particle after it, when the dictionary or the sound
// lexicon reads them as words as JoinSpelledOut does. A run of kanji only stays for the
// tokenizer (通 知 表), and a run that is no word stays as written (し　ら　さ　き, あ い う).
std::string JoinSpacedOut(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// text with its masked words in Korean (see the comment at the top).
std::string WriteMasked(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The sounds of text in Korean and each stutter as stutter marks before its separators
// (see the comment at the top). Invalid UTF-8 bytes are kept and read as neither kana
// nor letters.
std::string WriteSounds(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The spans [from, to) in characters of text that WriteSounds reads as keys of the sound lexicon.
std::vector<std::pair<size_t, size_t>> SoundSpans(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The cries of text that the lexicon lacks written in Korean as heard: a run of kana
// that starts with hiragana (katakana speech and names are left to later stages) where an
// utterance starts or after a comma, an ellipsis, a dash or a mark of tone (not after a
// closing bracket: 「左手」だぁああ is the copula), of one to three syllables and then two
// or more kana that draw out its last vowel (ひぃやあああ 히이야아아아, のおおお 노오오오,
// おりゃあああっ 오랴아아앗, ひぃやアアア; ー counts too), with ッ after them as the final ㅅ,
// before a pause, an ellipsis, a dash, a mark of tone or the end. Two or more syllables
// before the vowel must not start a dictionary key that reaches the vowel (てめーー), nor
// read, in hiragana, as a word of two or more characters or a stem followed by words,
// particles and endings (やったあああ, なんでよーーっ are words drawn out; おりゃあああ,
// のわあああ are cries). It runs on the text that RewriteEmphasis has left, so a word
// that it reads with the vowel dropped (やめてぇぇぇ) is no cry.
std::string WriteCries(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// A verb cut off before a dash or an ellipsis (― — … ‥, ・・ or ーー) completed to the form
// it starts, when the dictionary has the verb with its kanji: a part of the te form to the
// te form (待っ―― 待って――, 書い…… 書いて……), the negative stem with な to ない
// (行かな…… 行かない……). A stem or a fragment without kanji stays.
std::string CompleteCutOffs(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The いく after a て form contracted (連れてって 連れていって, 置いてかないで 置いていかないで,
// 出てった 出ていった) written in full: before the forms of contraction.te-iku; when the dictionary
// holds the verb with ていく (連れてい), also before those of contraction.te-iku-listed and, with a
// mark or the end after them, of contraction.te-iku-final (not やめてって, the quoting って). Not
// before contraction.te-iku-not (待ってったら).
// The て form is a verb of the dictionary that ends in its て or で.
std::string RewriteContractedIku(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The kana that casual speech drops of an auxiliary after a て form written back, so that the form
// reads as said in full: the い of いる and いらっしゃる before contraction.te-iru (見てたところ
// 見ていたところ, 待ってられない 待っていられない), also after the て form of ちゃう (寝ちゃってた
// 寝ちゃっていた); the あ of あげる before contraction.ta-ageru and the や of やる before
// contraction.ta-yaru, or contraction.ta-yaru-final before no letter, where the て before them is said
// た or だ (作ったげるから 作ってあげるから, やったるわ やってやるわ; not 秘めたる力). The て form is a verb
// of the dictionary, or する or くる, that ends in て or で. Not written back: before a word of the
// dictionary that reaches two or more characters past the contracted form (負けてたまるか,
// 割ってちゃわん), after a verb stem that ends there (捨てた, 育てる), inside
// a word of the dictionary (来たる, いってらっしゃい), where an ending row of the dictionary spells the
// contraction (見てるの, 見てた。, 作ったげる。; for the past た of いる any row longer than it, which the
// word stage reads on the past of いる: 見てたぞ) and before contraction.te-iru-past.
std::string RewriteContractedAuxiliaries(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The potential of the くる after a て form said without its ら (帰ってこれた 帰ってこられた, 逃げてこれる)
// written in full: before the forms of contraction.ra-less-kuru when the dictionary has the verb
// of the て form before them.
std::string RewriteRaLessKuru(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The の after a noun in kanji or katakana said ん before a member of contraction.n-no
// (頭ん中 頭の中, 先生んとこ 先生のとこ), when the dictionary has the noun and no word that
// spans the ん (真ん中), and no kanji follows a member that ends in kanji (いったん中断).
std::string RewriteContractedNo(std::string_view text, const dict::Snapshot& snap);

// The past and the て form of a godan verb in う said with う for っ (言うた 言った, 思うて 思って,
// 問うて), when the dictionary has the kanji stem as such a verb and no word that spans the
// form (名うて): before a member of contraction.u-onbin and not one of contraction.u-onbin-not
// (救うため).
std::string RewriteOnbinU(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// そっか after kanji written そうか where the longest end of the kanji run that the dictionary
// has is the stem of a verb in す: the volitional cut short (流そっか, 見逃そっか); after a noun
// (漢字そっか, 了解そっか) it is the answer そうか and stays. Before the end, a sentence mark, a
// closing bracket, な, ね or a drawn vowel.
std::string RewriteVolitionalSo(std::string_view text, const dict::Snapshot& snap);

// The casual adjective endings -ee and -ii of -ai, -oi, -ui (うるせぇ, すげー, わりぃ,
// 冷て～) written in full (うるさい, すごい, わるい, 冷たい) before the end of a clause or a
// sentence particle, when the dictionary has the stem, starting a word, as an
// adjective. Not after っ or ん, not the particle ぜ (歌おうぜぇ), not a て form
// (届いてぇ; 冷てぇ and いてぇ are adjectives). A kanji that starts a word clipped with っ
// before a mark is the adjective of that kanji with い (浅っ 浅い).
std::string RewriteCasualAdjectives(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// An honorific of honorific.drawn-out drawn out before its final ん after a name in kanji,
// katakana or two or more hiragana that are no verb continuative (圭ちゃ～ん, 田中さぁあん,
// アルくぅん; not かみつきさあん) is the honorific with the lengthening mark after it; a longer
// dictionary word that the honorific starts takes the mark after that word, also with no word
// before it (ちゃ～んと ちゃんと). A ん said again is dropped (さああんん); ちゃうん, whose う is
// no vowel of ちゃ, stays.
std::string RewriteDrawnHonorifics(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The emphasis that spoken text writes inside a word dropped when the dictionary then
// reads the word: a っ before a kana that does not start a て or た form (まっかせて,
// いっくぞ, 負っける), and a run of ー, ～ or small vowels that lengthens the kana before
// it (やーめーて, うるさ～い, 見ぃつけた, 勝ぁああつ), dropped or written as the long vowel
// (どーだ どうだ, おにーちゃん, おねーさん, てーとく ていとく); a run of two or more with a
// small vowel after a dictionary word before a shout's ッ, ！ or ？ is dropped
// (なんだそりゃああぁあッ). A lengthened vowel kana (あぁ) or particle (か～, なぁー), a mark
// that a word or a sound holds (もっと, せーの, おーい) and a lengthened nickname (うーちゃん)
// are left alone. The rewrite is kept when a dictionary key then reaches further past the
// mark than before or, when no key reached past it, after a kanji or, for a lengthening
// mark, a verb or adjective stem. A dropped run with ー or ～ before the last syllable of a
// phrase leaves kLengthenMark after that syllable (せんぱーい！ せんぱい + mark), and so does
// a run of three or more plain vowels of the kana before it inside a word, or of two before
// the い of an adjective stem (すごおおおい すごい + mark, さむううい; おばあああちゃん
// おばあちゃん keeps one vowel of the word).
// A ー or ～ that ends a volitional form after a verb stem is written う (いこー いこう).
// The tic っぽい drawn out before a pause (行くっぽーい) is っぽい with kLengthenMark.
// A drawn run of small vowels that ends a phrase after て, で, の, る, だ, く, ぐ or よ, or
// of three or more after words (ばかぁぁぁ), is dropped (ていとくぅ, ないでぇ, なのぉ), and so is a run of い and ぃ after the い that ends
// an adjective, before a pause or a shout's っ (ずるいぃ, 痛いぃぃ, きもちいいいいい; not the
// cry いいいいい), and a run of three or more plain vowel kana after words, before a pause or
// a ッ (待ったああああっ, 言うなあああっ). Such a dropped run of three or more vowel kana
// leaves one kDrawnMark for each, which takes a shout's ッ after it (やめてぇぇぇ やめて + 3
// marks). A run with ー that ends a phrase, also before a shout's っ, after a word is
// written ～ (提督ー！, 待ってー, だよねー, だめーーっ, かわいいー！), its small vowels dropped after a kanji
// or those kana and kept after others (だなぁー だなぁ～); the e row spelled with ー becomes い when
// that makes a word of four or more characters (せんせー せんせい). Neither touches a
// lone kana, a sound, the casual negative ね (知らねー), the casual たい (やりてー) or な
// after る or て. Returns text itself when nothing changes.
std::string RewriteEmphasis(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// korean with the elongation units of a sound match: 'r' for each repeat of the
// syllable the key ends with, which writes the last syllable again (아하하 아하하하) and
// lets '!' give the last syllable the final ㅅ (아하하 + rr! 아하하하핫), then 'v' for each
// lengthening character, '~' for each run of ～, '!' for a closing ッ. When the last syllable
// carries the sound (the only syllable, or one without an initial consonant: 앙, 하아,
// 우왓), each 'v' adds a syllable of its vowel, at most four, before the final
// consonant if it has one (앙 아앙, 꺅 꺄악), and '!' gives it the final ㅅ when it has none
// (하아 하앗); other words take one ~ instead (정말 정말~), unless 'd' (an interjection drawn
// out by three or more vowel kana) makes the last syllable carry it (안 돼 + vvvd 안 돼애애애).
// '~' adds ~, and 't' (a ～ that stood for the key's ー: う～ん) a ~ at the end. 'u' (the ッ
// closes a drawn う) writes a final 웃 as 읏 (하우 + ! 하읏).
std::string Lengthen(std::string_view korean, std::string_view units);

// A row of the sound lexicon; key in katakana.
struct SoundRow {
    std::u32string key;
    std::string korean;
    // The lemma of a KR/lemma row without a final 하다, written before する; empty for
    // rows without a lemma.
    std::string root;
    bool intj = false;    // an intj or lead row; the other rows are ono rows
    bool phrase = false;  // a row of phrases.tsv
    bool lead = false;    // an interjection only at the start of an utterance, before a pause
    bool open = false;    // a lead row that needs no pause after it (ってか 그보다)
    // An exclamation (fourth column exclaim): a statement that is no sound before a
    // question mark, where the words ask for an evaluation (よかった？ 좋았어?).
    bool exclaim = false;
    // An exclaim row that is no sound after a noun topic and a comma either, where it is the
    // predicate (fourth column predicate: あの頃は、よかった 좋았다).
    bool predicate = false;
    // An intj row read only where the utterance ends after it (fourth column final: また明日
    // 내일 보자, not また明日来る).
    bool final = false;
    // An intj row that is a predicate said as a cry (fourth column cry: やだ 싫어), whose last copy
    // clipped with ッ closes its last syllable with ㅅ (SND-2: やだやだっ 싫어 싫엇).
    bool cry = false;
    // The last word of the Korean in polite speech (fourth column polite=봬요: 내일 봬요), read
    // where the quote speaks politely or an honoured person is called right before the row.
    std::string polite;
};

// The rows of the sound lexicon whose key starts with the katakana c, longer keys
// first, rows of equal length in file order (sound.tsv, then phrases.tsv).
std::span<const SoundRow> SoundRows(char32_t c, bool extra = false);

// Whether key (katakana) is the key of a row of sound.tsv.
bool IsSoundKey(std::u32string_view key, bool extra = false);

// The characters of s; an invalid UTF-8 byte b is kInvalidByte + b.
constexpr char32_t kInvalidByte = 0x110000;
std::u32string DecodeText(std::string_view s);
// The inverse of DecodeText.
std::string EncodeText(std::u32string_view rs);
// The byte offset of each character of rs in EncodeText(rs), and its size last: an invalid byte
// takes one byte.
std::vector<size_t> EncodedOffsets(std::u32string_view rs);
// rs with the hiragana ぁ..ゖ in katakana.
std::u32string ToKatakana(std::u32string_view rs);

// The end of a match of row at f[p:] (f in katakana) and its elongation units (see
// Lengthen), or false. In a key of three or more characters a small vowel also matches
// the plain one (ふわぁ, ふわあ), and a small vowel at p after no letter the key's first
// vowel (ぅあっ). A key that ends in a doubled syllable, with or without
// ッ, whose Korean ends in a doubled syllable, continues over more of that syllable
// (あははは+はは, あっはっは+っはっ) with a unit 'r' for each, or 'h' when the syllable holds
// ッ: such a match is written as heard, each syllable with its ㅅ (앗핫핫핫). Elongation may follow the key, or come before its last
// character when that is ン or ッ; ッ may close it, with the ～ and ー after it, when no kana
// but ト, テ, ～ or ー follows. A ～ matches the key's ー, with a unit 't'.
bool MatchSound(const SoundRow& row, std::u32string_view f, size_t p, size_t& end, std::string& units);

}  // namespace internal

}  // namespace hdTrans::pipeline
