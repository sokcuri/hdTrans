#include "core/names/historical.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/text/kana.h"
#include "core/text/utf8.h"

namespace hdTrans::names {

namespace {

using Runes = std::vector<char32_t>;
using Entries = std::map<std::string, std::string>;

// The code points of s.
Runes ToRunes(std::string_view s) {
    Runes out;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        out.push_back(r.cp);
        i += r.size;
    }
    return out;
}

// The UTF-8 text of rs[from:to].
std::string Sub(const Runes& rs, std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t i = from; i < to; i++) text::AppendRune(out, rs[i]);
    return out;
}

// Whether rs[from:] begins with the UTF-8 text p.
bool HasPrefixAt(const Runes& rs, std::size_t from, std::string_view p) {
    for (std::size_t i = 0, k = from; i < p.size(); k++) {
        const text::Rune r = text::DecodeRune(p.substr(i));
        if (k >= rs.size() || rs[k] != r.cp) return false;
        i += r.size;
    }
    return true;
}

bool IsNameKanji(char32_t c) { return (c >= 0x4E00 && c <= 0x9FFF) || c == U'々' || c == U'ヶ'; }

bool AllHangul(std::string_view s) {
    if (s.empty()) return false;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        if (r.cp < 0xAC00 || r.cp > 0xD7A3) return false;
        i += r.size;
    }
    return true;
}

std::string Join(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& p : parts) {
        if (p.empty()) continue;
        if (!out.empty()) out += " ";
        out += p;
    }
    return out;
}

// The records of s: the name-layer records, then the base records.
std::vector<dict::Record> Records(const dict::Snapshot& snap, const std::string& s) {
    std::vector<dict::Record> out = snap.NameRecords(s);
    const std::vector<dict::Record> base = snap.Base().Records(s);
    out.insert(out.end(), base.begin(), base.end());
    return out;
}

bool Contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// The Hangul of s as the dictionary reads it: a surname (rank 3), a person (2), or a city
// or region (1); "" and 0 for none.
std::string SurnameOf(const dict::Snapshot& snap, const std::string& s, int& rank) {
    const std::vector<dict::Record> records = Records(snap, s);
    for (rank = 3; rank >= 1; rank--) {
        for (const dict::Record& r : records) {
            if (r.codes == nullptr) continue;
            const std::string_view sem = r.codes->sem.Text(), attr = r.codes->gram.Text();
            const bool hit = (rank == 3 && sem.starts_with("QPN") && Contains(attr, "Wa")) ||
                             (rank == 2 && Contains(sem, "QPN")) || (rank == 1 && Contains(sem, "QCT"));
            if (AllHangul(r.target) && hit) return std::string(r.target);
        }
    }
    rank = 0;
    return "";
}

bool KnownPerson(const dict::Snapshot& snap, const std::string& s) {
    for (const dict::Record& r : Records(snap, s)) {
        if (r.codes != nullptr && Contains(r.codes->sem.Text(), "QPN")) return true;
    }
    return false;
}

// Readings of kanji in surnames.
const std::map<char32_t, std::string>& SurnameKun() {
    static const std::map<char32_t, std::string> table = {
        {U'岩', "いわ"}, {U'脇', "わき"}, {U'瀬', "せ"}, {U'見', "み"}, {U'梁', "やな"}, {U'田', "た"}, {U'成', "なり"}, {U'野', "の"},
        {U'川', "かわ"}, {U'山', "やま"}, {U'木', "き"}, {U'井', "い"}, {U'原', "はら"}, {U'村', "むら"}, {U'島', "しま"}, {U'沢', "さわ"},
        {U'崎', "さき"}, {U'谷', "たに"}, {U'本', "もと"}, {U'口', "ぐち"}, {U'宮', "みや"}, {U'松', "まつ"}, {U'杉', "すぎ"}, {U'竹', "たけ"},
        {U'石', "いし"}, {U'小', "こ"}, {U'大', "おお"}, {U'中', "なか"}, {U'上', "かみ"}, {U'下', "しも"}, {U'西', "にし"}, {U'東', "ひがし"},
        {U'北', "きた"}, {U'南', "みなみ"}, {U'前', "まえ"}, {U'高', "たか"}, {U'長', "なが"}, {U'渡', "わた"}, {U'辺', "なべ"}, {U'藤', "ふじ"},
        {U'森', "もり"}, {U'林', "はやし"}, {U'池', "いけ"}, {U'岡', "おか"}, {U'坂', "さか"}, {U'浦', "うら"}, {U'堀', "ほり"}, {U'橋', "はし"},
        {U'永', "なが"}, {U'久', "ひさ"}, {U'吉', "よし"}, {U'福', "ふく"}, {U'平', "ひら"}, {U'安', "やす"}, {U'寺', "てら"}, {U'尾', "お"},
        {U'内', "うち"}, {U'戸', "と"}, {U'江', "え"}, {U'津', "つ"}, {U'浜', "はま"}, {U'黒', "くろ"}, {U'白', "しろ"}, {U'赤', "あか"},
        {U'青', "あお"}, {U'金', "かね"}, {U'鈴', "すず"}, {U'今', "いま"}, {U'古', "ふる"}, {U'若', "わか"}, {U'早', "はや"}, {U'秋', "あき"},
        {U'春', "はる"}, {U'城', "しろ"}, {U'塚', "つか"}, {U'菅', "すが"}, {U'稲', "いな"}, {U'桜', "さくら"}, {U'梅', "うめ"}, {U'柳', "やなぎ"},
        {U'芦', "あし"}, {U'栗', "くり"}, {U'柴', "しば"}, {U'浅', "あさ"}, {U'深', "ふか"}, {U'細', "ほそ"}, {U'渋', "しぶ"}, {U'飯', "いい"},
        {U'笠', "かさ"}, {U'畑', "はた"}, {U'岸', "きし"}, {U'根', "ね"}, {U'星', "ほし"}, {U'水', "みず"}, {U'清', "きよ"}, {U'富', "とみ"},
        {U'有', "あり"}, {U'狩', "かり"}, {U'鷹', "たか"}, {U'熊', "くま"}, {U'鹿', "しか"}, {U'鳥', "とり"}, {U'猪', "い"}, {U'犬', "いぬ"},
        {U'馬', "うま"}, {U'牛', "うし"}, {U'鶴', "つる"}, {U'亀', "かめ"}, {U'弓', "ゆみ"}, {U'矢', "や"}, {U'舟', "ふな"}, {U'船', "ふな"},
        {U'磯', "いそ"}, {U'沼', "ぬま"}, {U'泉', "いずみ"}, {U'滝', "たき"}, {U'坪', "つぼ"}, {U'溝', "みぞ"}, {U'垣', "かき"}, {U'門', "かど"},
        {U'宅', "やけ"}, {U'屋', "や"}, {U'倉', "くら"}, {U'蔵', "くら"},
    };
    return table;
}

// The Hangul of a surname the dictionary does not know, read kanji by kanji; "" when a
// kanji has no surname reading.
std::string KunSurname(const std::string& s) {
    std::string kana;
    for (const char32_t c : ToRunes(s)) {
        const auto it = SurnameKun().find(c);
        if (it == SurnameKun().end()) return "";
        kana += it->second;
    }
    return text::FromKana(kana, text::Preset::FanShort);
}

// The name readings of kanji in given names.
const std::map<char32_t, std::string>& Nanori() {
    static const std::map<char32_t, std::string> table = {
        {U'綱', "つな"}, {U'晴', "はる"}, {U'稙', "たね"}, {U'藤', "ふじ"}, {U'成', "なり"}, {U'直', "なお"}, {U'輝', "てる"}, {U'孝', "たか"},
        {U'惟', "これ"}, {U'安', "やす"}, {U'基', "もと"}, {U'行', "ゆき"}, {U'近', "ちか"}, {U'貞', "さだ"}, {U'忠', "ただ"}, {U'仲', "なか"},
        {U'頼', "より"}, {U'道', "みち"}, {U'継', "つぐ"}, {U'長', "なが"}, {U'好', "よし"}, {U'満', "みつ"}, {U'張', "はる"}, {U'永', "なが"},
        {U'重', "しげ"}, {U'義', "よし"}, {U'正', "まさ"}, {U'春', "はる"}, {U'定', "さだ"}, {U'裕', "ひろ"}, {U'嗣', "つぐ"}, {U'員', "かず"},
        {U'清', "きよ"}, {U'昭', "あき"}, {U'光', "みつ"}, {U'影', "かげ"}, {U'明', "あき"}, {U'昌', "まさ"}, {U'方', "かた"}, {U'昇', "のり"},
        {U'篤', "あつ"}, {U'経', "つね"}, {U'親', "ちか"}, {U'世', "よ"}, {U'征', "ゆき"}, {U'潤', "ひろ"}, {U'次', "つぐ"}, {U'家', "いえ"},
        {U'幸', "ゆき"}, {U'久', "ひさ"}, {U'高', "たか"}, {U'秀', "ひで"}, {U'俊', "とし"}, {U'隆', "たか"}, {U'意', "もと"}, {U'信', "のぶ"},
        {U'守', "もり"}, {U'盛', "もり"}, {U'慶', "よし"}, {U'康', "やす"}, {U'勝', "かつ"}, {U'政', "まさ"}, {U'豊', "とよ"}, {U'賢', "かた"},
        {U'宗', "むね"}, {U'実', "さね"}, {U'持', "もち"}, {U'武', "たけ"}, {U'朝', "とも"}, {U'兼', "かね"}, {U'存', "まさ"}, {U'逸', "やす"},
        {U'虎', "とら"}, {U'通', "みち"}, {U'景', "かげ"}, {U'乃', "より"}, {U'続', "つぐ"}, {U'連', "つら"}, {U'誠', "まさ"}, {U'紹', "つぐ"},
        {U'周', "ちか"}, {U'照', "てる"}, {U'旦', "あき"}, {U'就', "なり"}, {U'理', "まさ"}, {U'堯', "たか"}, {U'尭', "たか"}, {U'雅', "まさ"},
        {U'教', "のり"}, {U'一', "かず"}, {U'内', "うち"}, {U'前', "さき"}, {U'範', "のり"}, {U'為', "ため"}, {U'冬', "ふゆ"}, {U'秋', "あき"},
        {U'夏', "なつ"}, {U'種', "たね"}, {U'氏', "うじ"}, {U'治', "はる"}, {U'吉', "よし"}, {U'恒', "つね"}, {U'常', "つね"}, {U'末', "すえ"},
        {U'友', "とも"}, {U'知', "とも"}, {U'智', "とも"}, {U'具', "とも"}, {U'季', "すえ"}, {U'将', "まさ"}, {U'元', "もと"}, {U'興', "おき"},
        {U'敦', "あつ"}, {U'和', "かず"}, {U'数', "かず"}, {U'是', "これ"}, {U'之', "ゆき"}, {U'良', "よし"}, {U'善', "よし"}, {U'能', "よし"},
        {U'芳', "よし"}, {U'由', "よし"}, {U'喜', "よし"}, {U'嘉', "よし"}, {U'宣', "のぶ"}, {U'延', "のぶ"}, {U'允', "まさ"}, {U'晃', "あき"},
        {U'彰', "あき"}, {U'顕', "あき"}, {U'英', "ひで"}, {U'栄', "ひで"}, {U'寿', "ひさ"}, {U'尚', "なお"}, {U'茂', "しげ"}, {U'繁', "しげ"},
        {U'規', "のり"}, {U'典', "のり"}, {U'憲', "のり"}, {U'紀', "のり"}, {U'則', "のり"}, {U'徳', "のり"}, {U'寄', "より"}, {U'克', "かつ"},
        {U'雄', "かつ"}, {U'猛', "たけ"}, {U'健', "たけ"}, {U'剛', "たけ"}, {U'威', "たけ"}, {U'修', "なが"}, {U'国', "くに"}, {U'邦', "くに"},
        {U'統', "むね"}, {U'時', "とき"}, {U'辰', "とき"}, {U'業', "なり"}, {U'利', "とし"}, {U'広', "ひろ"}, {U'弘', "ひろ"}, {U'宏', "ひろ"},
        {U'博', "ひろ"}, {U'祐', "すけ"}, {U'資', "すけ"}, {U'助', "すけ"}, {U'輔', "すけ"}, {U'介', "すけ"}, {U'亮', "すけ"}, {U'胤', "たね"},
        {U'房', "ふさ"}, {U'総', "ふさ"}, {U'泰', "やす"}, {U'靖', "やす"}, {U'保', "やす"}, {U'寧', "やす"}, {U'恭', "やす"}, {U'庸', "やす"},
        {U'昆', "ひで"}, {U'衡', "ひら"}, {U'平', "ひら"}, {U'衛', "もり"}, {U'護', "もり"}, {U'師', "もろ"}, {U'諸', "もろ"}, {U'晋', "くに"},
        {U'敏', "とし"}, {U'年', "とし"}, {U'禎', "さだ"}, {U'完', "さだ"}, {U'充', "みつ"}, {U'三', "みつ"}, {U'密', "みつ"}, {U'匡', "まさ"},
        {U'真', "さね"}, {U'心', "むね"}, {U'旨', "むね"}, {U'致', "むね"}, {U'至', "よし"}, {U'儀', "よし"}, {U'佳', "よし"}, {U'美', "よし"},
        {U'賀', "よし"}, {U'令', "よし"}, {U'淑', "よし"}, {U'宜', "よし"}, {U'欣', "よし"}, {U'因', "より"}, {U'依', "より"}, {U'貴', "たか"},
        {U'崇', "たか"}, {U'尊', "たか"}, {U'敬', "たか"}, {U'考', "たか"}, {U'登', "のり"}, {U'矩', "のり"}, {U'法', "のり"}, {U'式', "のり"},
        {U'伸', "のぶ"}, {U'暢', "のぶ"}, {U'展', "のぶ"}, {U'述', "のぶ"}, {U'順', "のぶ"}, {U'亘', "のぶ"}, {U'冨', "とみ"}, {U'富', "とみ"},
        {U'臣', "とみ"}, {U'頓', "とみ"}, {U'日', "ひ"}, {U'月', "つき"}, {U'雪', "ゆき"}, {U'葉', "は"}, {U'綾', "あや"}, {U'彩', "あや"},
        {U'文', "ふみ"}, {U'史', "ふみ"}, {U'郁', "ふみ"}, {U'倫', "とも"}, {U'伴', "とも"}, {U'共', "とも"}, {U'巴', "とも"}, {U'鞆', "とも"},
        {U'寛', "ひろ"}, {U'洋', "ひろ"}, {U'浩', "ひろ"}, {U'啓', "ひろ"}, {U'鑑', "あき"}, {U'映', "てる"}, {U'陽', "はる"}, {U'温', "はる"},
        {U'遠', "とお"}, {U'達', "たつ"}, {U'竜', "たつ"}, {U'龍', "たつ"}, {U'起', "おき"}, {U'沖', "おき"}, {U'幹', "もと"}, {U'本', "もと"},
        {U'素', "もと"}, {U'求', "もと"}, {U'職', "もと"}, {U'晟', "あき"}, {U'璋', "あき"}, {U'朗', "あき"}, {U'玄', "はる"}, {U'敷', "のぶ"},
        {U'鎮', "しげ"}, {U'蕃', "しげ"}, {U'樹', "しげ"}, {U'殖', "しげ"}, {U'槻', "つき"}, {U'勇', "いさ"}, {U'功', "いさ"}, {U'績', "いさ"},
        {U'烈', "つら"}, {U'列', "つら"}, {U'貫', "つら"}, {U'面', "つら"}, {U'公', "きみ"}, {U'君', "きみ"}, {U'仁', "ひと"}, {U'人', "ひと"},
        {U'均', "ひとし"}, {U'斉', "なり"}, {U'也', "なり"}, {U'形', "なり"}, {U'生', "なり"}, {U'丈', "たけ"}, {U'岳', "たけ"}, {U'偉', "たけ"},
        {U'豪', "たけ"}, {U'毅', "たけ"}, {U'乗', "のり"}, {U'紘', "ひろ"},
    };
    return table;
}

// The given name of one to three kanji: the dictionary's given-name sense, else its kanji
// read one by one in their name readings; "" when a kanji has none and for an honorific
// alone (太郎君 is 太郎 with 君).
std::string GivenName(const dict::Snapshot& snap, const std::string& s) {
    const std::size_t n = text::RuneCount(s);
    if (n == 0 || n > 3 || s == "君" || s == "殿" || s == "様" || s == "氏") return "";
    for (const dict::Record& r : Records(snap, s)) {
        if (r.codes == nullptr) continue;
        if (Contains(r.codes->sem.Text(), "QPN") && Contains(r.codes->gram.Text(), "Wb") && AllHangul(r.target)) {
            return std::string(r.target);
        }
    }
    std::string kana;
    for (const char32_t c : ToRunes(s)) {
        const auto it = Nanori().find(c);
        if (it == Nanori().end()) return "";
        kana += it->second;
    }
    return text::FromKana(kana, text::Preset::FanShort);
}

// A piece of a title: its kana reading, whether it is a full office or common name rather
// than a bare department, whether it is a bare core that joins the piece before it, and
// whether it is a common name.
struct TitlePiece {
    std::string kana;
    bool full = false;
    bool bare = false;
    bool common = false;
};

// A key of a table with its reading.
struct OfficeKey {
    std::string key, kana;
};

const std::vector<OfficeKey>& CourtOffices() {
    static const std::vector<OfficeKey> table = {
        {"宮内少輔", "くないしょうゆう"}, {"民部少輔", "みんぶしょうゆう"}, {"治部少輔", "じぶしょうゆう"}, {"式部少輔", "しきぶしょうゆう"},
        {"中務少輔", "なかつかさしょうゆう"}, {"兵部少輔", "ひょうぶしょうゆう"}, {"刑部少輔", "ぎょうぶしょうゆう"}, {"大蔵少輔", "おおくらしょうゆう"},
        {"宮内大輔", "くないたいふ"}, {"民部大輔", "みんぶたいふ"}, {"治部大輔", "じぶたいふ"}, {"式部大輔", "しきぶたいふ"},
        {"中務大輔", "なかつかさたいふ"}, {"兵部大輔", "ひょうぶたいふ"}, {"刑部大輔", "ぎょうぶたいふ"}, {"大蔵大輔", "おおくらたいふ"},
        {"式部丞", "しきぶのじょう"}, {"民部丞", "みんぶのじょう"}, {"治部丞", "じぶのじょう"}, {"兵部丞", "ひょうぶのじょう"},
        {"修理大夫", "しゅりのだいぶ"}, {"修理亮", "しゅりのすけ"}, {"修理進", "しゅりのしん"},
        {"左京大夫", "さきょうのだいぶ"}, {"右京大夫", "うきょうのだいぶ"}, {"左京亮", "さきょうのすけ"}, {"右京亮", "うきょうのすけ"},
        {"左京進", "さきょうのしん"}, {"右京進", "うきょうのしん"}, {"大膳大夫", "だいぜんのだいぶ"}, {"大膳亮", "だいぜんのすけ"},
        {"弾正忠", "だんじょうのちゅう"}, {"弾正少弼", "だんじょうしょうひつ"}, {"弾正大弼", "だんじょうだいひつ"},
        {"左馬頭", "さまのかみ"}, {"右馬頭", "うまのかみ"}, {"左馬助", "さまのすけ"}, {"右馬助", "うまのすけ"}, {"左馬允", "さまのじょう"}, {"右馬允", "うまのじょう"},
        {"兵庫頭", "ひょうごのかみ"}, {"兵庫助", "ひょうごのすけ"}, {"主税頭", "ちからのかみ"}, {"主税助", "ちからのすけ"},
        {"主殿頭", "とのものかみ"}, {"主殿助", "とのものすけ"}, {"主水正", "もんどのしょう"}, {"主計頭", "かずえのかみ"},
        {"掃部頭", "かもんのかみ"}, {"掃部助", "かもんのすけ"}, {"内蔵助", "くらのすけ"}, {"内蔵頭", "くらのかみ"}, {"内匠頭", "たくみのかみ"},
        {"内膳正", "ないぜんのしょう"}, {"隼人正", "はやとのしょう"}, {"隼人佑", "はやとのすけ"}, {"采女正", "うねめのしょう"},
        {"大炊助", "おおいのすけ"}, {"大炊充", "おおいのすけ"}, {"大炊頭", "おおいのかみ"}, {"図書頭", "ずしょのかみ"}, {"図書助", "ずしょのすけ"},
        {"玄蕃頭", "げんばのかみ"}, {"玄蕃允", "げんばのじょう"}, {"雅楽頭", "うたのかみ"}, {"雅楽助", "うたのすけ"},
        {"左近将監", "さこんのしょうげん"}, {"右近将監", "うこんのしょうげん"},
        {"左近衛権少将", "さこんえごんのしょうしょう"}, {"右近衛権少将", "うこんえごんのしょうしょう"},
        {"左近衛権中将", "さこんえごんのちゅうじょう"}, {"右近衛権中将", "うこんえごんのちゅうじょう"},
        {"左近衛少将", "さこんえのしょうしょう"}, {"右近衛少将", "うこんえのしょうしょう"},
        {"左近衛中将", "さこんえのちゅうじょう"}, {"右近衛中将", "うこんえのちゅうじょう"},
        {"左近衛大将", "さこんえのたいしょう"}, {"右近衛大将", "うこんえのたいしょう"},
        {"左衛門尉", "さえもんのじょう"}, {"右衛門尉", "うえもんのじょう"}, {"左衛門佐", "さえもんのすけ"}, {"右衛門佐", "うえもんのすけ"},
        {"左衛門督", "さえもんのかみ"}, {"右衛門督", "うえもんのかみ"}, {"左衛門大夫", "さえもんのだいぶ"}, {"右衛門大夫", "うえもんのだいぶ"},
        {"左衛門大輔", "さえもんのたいふ"}, {"右衛門大輔", "うえもんのたいふ"},
        {"左兵衛尉", "さひょうえのじょう"}, {"右兵衛尉", "うひょうえのじょう"}, {"左兵衛佐", "さひょうえのすけ"}, {"右兵衛佐", "うひょうえのすけ"},
        {"左兵衛督", "さひょうえのかみ"}, {"右兵衛督", "うひょうえのかみ"}, {"左兵衛大夫", "さひょうえのだいぶ"}, {"右兵衛大夫", "うひょうえのだいぶ"},
        {"左衛門", "さえもん"}, {"右衛門", "うえもん"}, {"左兵衛", "さひょうえ"}, {"右兵衛", "うひょうえ"}, {"兵衛", "ひょうえ"},
        {"左近", "さこん"}, {"右近", "うこん"}, {"左京", "さきょう"}, {"右京", "うきょう"}, {"左門", "さもん"},
        {"主殿", "とのも"}, {"主税", "ちから"}, {"主馬", "しゅめ"}, {"主水", "もんど"}, {"主膳", "しゅぜん"}, {"主計", "かずえ"},
        {"蔵人", "くろうど"}, {"刑部", "ぎょうぶ"}, {"兵部", "ひょうぶ"}, {"民部", "みんぶ"}, {"治部", "じぶ"}, {"式部", "しきぶ"},
        {"中務", "なかつかさ"}, {"大膳", "だいぜん"}, {"帯刀", "たてわき"}, {"監物", "けんもつ"}, {"外記", "げき"}, {"将監", "しょうげん"},
        {"図書", "ずしょ"}, {"掃部", "かもん"}, {"采女", "うねめ"}, {"内記", "ないき"}, {"隼人", "はやと"}, {"兵庫", "ひょうご"},
        {"弾正", "だんじょう"}, {"修理", "しゅり"}, {"大炊", "おおい"}, {"内蔵", "くら"}, {"玄蕃", "げんば"}, {"雅楽", "うた"},
    };
    return table;
}

// The offices whose title is a full title only with a given name.
bool Department(const std::string& key) {
    static const std::set<std::string> table = {
        "左衛門", "右衛門", "左兵衛", "右兵衛", "兵衛", "左近", "右近", "左京", "右京", "左門", "主殿", "主税", "主馬", "主水", "主膳",
        "主計", "蔵人", "刑部", "兵部", "民部", "治部", "式部", "中務", "大膳", "帯刀", "監物", "外記", "将監", "図書", "掃部", "采女",
        "内記", "隼人", "兵庫", "弾正", "修理", "大炊", "内蔵", "玄蕃", "雅楽",
    };
    return table.count(key) > 0;
}

// The old provinces, read before 守, 介 and the like.
const std::vector<OfficeKey>& Provinces() {
    static const std::vector<OfficeKey> table = {
        {"山城", "やましろ"}, {"大和", "やまと"}, {"河内", "かわち"}, {"和泉", "いずみ"}, {"摂津", "せっつ"}, {"伊賀", "いが"}, {"伊勢", "いせ"},
        {"志摩", "しま"}, {"尾張", "おわり"}, {"三河", "みかわ"}, {"遠江", "とおとうみ"}, {"駿河", "するが"}, {"伊豆", "いず"}, {"甲斐", "かい"},
        {"相模", "さがみ"}, {"武蔵", "むさし"}, {"安房", "あわ"}, {"上総", "かずさ"}, {"下総", "しもうさ"}, {"常陸", "ひたち"}, {"近江", "おうみ"},
        {"美濃", "みの"}, {"飛騨", "ひだ"}, {"信濃", "しなの"}, {"上野", "こうずけ"}, {"下野", "しもつけ"}, {"陸奥", "むつ"}, {"出羽", "でわ"},
        {"若狭", "わかさ"}, {"越前", "えちぜん"}, {"加賀", "かが"}, {"能登", "のと"}, {"越中", "えっちゅう"}, {"越後", "えちご"}, {"佐渡", "さど"},
        {"丹波", "たんば"}, {"丹後", "たんご"}, {"但馬", "たじま"}, {"因幡", "いなば"}, {"伯耆", "ほうき"}, {"出雲", "いずも"}, {"石見", "いわみ"},
        {"隠岐", "おき"}, {"播磨", "はりま"}, {"美作", "みまさか"}, {"備前", "びぜん"}, {"備中", "びっちゅう"}, {"備後", "びんご"}, {"安芸", "あき"},
        {"周防", "すおう"}, {"長門", "ながと"}, {"紀伊", "きい"}, {"淡路", "あわじ"}, {"阿波", "あわ"}, {"讃岐", "さぬき"}, {"伊予", "いよ"},
        {"土佐", "とさ"}, {"筑前", "ちくぜん"}, {"筑後", "ちくご"}, {"豊前", "ぶぜん"}, {"豊後", "ぶんご"}, {"肥前", "ひぜん"}, {"肥後", "ひご"},
        {"日向", "ひゅうが"}, {"大隅", "おおすみ"}, {"薩摩", "さつま"}, {"壱岐", "いき"}, {"対馬", "つしま"}, {"飛驒", "ひだ"},
    };
    return table;
}

// The office suffixes of a province (権守, 守, 介, 之助, 之介).
const std::vector<std::pair<std::string, std::string>>& ProvinceOffices() {
    static const std::vector<std::pair<std::string, std::string>> table = {
        {"権守", "ごんのかみ"}, {"守", "のかみ"}, {"介", "のすけ"}, {"之助", "のすけ"}, {"之介", "のすけ"},
    };
    return table;
}

// The clans written before 朝臣.
const std::vector<OfficeKey>& Clans() {
    static const std::vector<OfficeKey> table = {
        {"平", "たいら"}, {"源", "みなもと"}, {"藤原", "ふじわら"}, {"橘", "たちばな"}, {"豊臣", "とよとみ"}, {"菅原", "すがわら"},
        {"大江", "おおえ"}, {"清原", "きよはら"}, {"安倍", "あべ"}, {"越智", "おち"}, {"大中臣", "おおなかとみ"},
    };
    return table;
}

// A common-name core (三郎, 左衛門, 之助 ...). ro marks the 郎 cores, which make a name
// without a prefix; alone is the reading without a prefix where the reading after one is
// voiced; office cores may take an office suffix; needsPrefix cores follow a prefix.
struct NameCore {
    std::string key, kana, alone;
    bool ro = false, needsPrefix = false, office = false;
};

const std::vector<NameCore>& NameCores() {
    static const std::vector<NameCore> table = {
        {"太郎", "たろう", "", true, false, false}, {"次郎", "じろう", "", true, false, false},
        {"二郎", "じろう", "", true, false, false}, {"三郎", "さぶろう", "", true, false, false},
        {"四郎", "しろう", "", true, false, false}, {"五郎", "ごろう", "", true, false, false},
        {"六郎", "ろくろう", "", true, false, false}, {"七郎", "しちろう", "", true, false, false},
        {"八郎", "はちろう", "", true, false, false}, {"九郎", "くろう", "", true, false, false},
        {"十郎", "じゅうろう", "", true, false, false}, {"一郎", "いちろう", "", true, false, false},
        {"左衛門", "ざえもん", "さえもん", false, false, true}, {"右衛門", "えもん", "うえもん", false, false, true},
        {"衛門", "えもん", "", false, true, true}, {"兵衛", "べえ", "ひょうえ", false, false, true},
        {"之助", "のすけ", "", false, true, false}, {"之介", "のすけ", "", false, true, false},
        {"之丞", "のじょう", "", false, true, false}, {"助", "すけ", "", false, true, false},
        {"介", "すけ", "", false, true, false}, {"丞", "じょう", "", false, true, false},
        {"蔵", "ぞう", "", false, true, false}, {"八", "はち", "", false, true, false},
        {"坊", "ぼう", "", false, true, false}, {"丸", "まる", "", false, true, false},
        {"千代", "ちよ", "", false, true, false},
    };
    return table;
}

// The prefixes of a common name (新三郎, 孫四郎, 又兵衛, 与次左衛門), longest first.
const std::vector<OfficeKey>& NamePrefixes() {
    static const std::vector<OfficeKey> table = [] {
        std::vector<OfficeKey> out = {
            {"新", "しん"}, {"孫", "まご"}, {"又", "また"}, {"平", "へい"}, {"源", "げん"}, {"甚", "じん"}, {"弥", "や"}, {"彦", "ひこ"},
            {"藤", "とう"}, {"徳", "とく"}, {"宗", "そう"}, {"重", "じゅう"}, {"半", "はん"}, {"市", "いち"}, {"与", "よ"}, {"与次", "よじ"},
            {"千", "せん"}, {"小", "こ"}, {"善", "ぜん"}, {"作", "さく"}, {"伝", "でん"}, {"陣", "じん"}, {"惣", "そう"}, {"総", "そう"},
            {"清", "せい"}, {"権", "ごん"}, {"庄", "しょう"}, {"喜", "き"}, {"吉", "きち"}, {"久", "きゅう"}, {"金", "きん"}, {"利", "り"},
            {"猪", "い"}, {"佐", "さ"}, {"茂", "も"}, {"勘", "かん"}, {"忠", "ちゅう"}, {"伊", "い"}, {"嘉", "か"}, {"加", "か"},
            {"文", "ぶん"}, {"武", "ぶ"}, {"九", "く"}, {"八", "はち"}, {"七", "しち"}, {"六", "ろく"}, {"五", "ご"}, {"四", "し"},
            {"十", "じゅう"}, {"助", "すけ"}, {"秀", "しゅう"}, {"祥", "しょう"}, {"犀", "さい"}, {"竹", "たけ"}, {"若", "わか"}, {"松", "まつ"},
            {"梅", "うめ"}, {"鶴", "つる"}, {"亀", "かめ"}, {"熊", "くま"}, {"虎", "とら"}, {"万", "まん"}, {"辰", "たつ"}, {"杢", "もく"},
            {"角", "かく"}, {"才", "さい"}, {"林", "りん"}, {"兵", "ひょう"}, {"右", "う"},
        };
        std::stable_sort(out.begin(), out.end(), [](const OfficeKey& a, const OfficeKey& b) {
            return text::RuneCount(a.key) > text::RuneCount(b.key);
        });
        return out;
    }();
    return table;
}

// One piece at rs[p]: the longest title piece there and its length in code points (0 for
// none). after reports a piece before it.
TitlePiece PieceAt(const Runes& rs, std::size_t p, bool after, std::size_t& size);

// The longest common name at rs[p] (commonName): its reading, its length in code points
// from p, and whether it is bare.
std::string CommonName(const Runes& rs, std::size_t p, bool after, std::size_t& size, bool& bare) {
    std::string best;
    size = 0;
    bare = false;
    struct Walker {
        const Runes& rs;
        std::size_t p;
        bool after;
        std::string& best;
        std::size_t& size;
        bool& bare;
        void Walk(std::size_t q, const std::string& kana, int prefixes) {
            for (const NameCore& c : NameCores()) {
                if (!HasPrefixAt(rs, q, c.key) || (c.needsPrefix && prefixes == 0) || (!c.ro && prefixes == 0 && !after)) continue;
                std::string k = kana + c.kana;
                if (prefixes == 0 && !c.alone.empty()) k = kana + c.alone;
                std::size_t n = q + text::RuneCount(c.key);
                bool office = c.office;
                if (c.ro) {
                    static const std::vector<std::pair<std::string, std::string>> ends = {
                        {"左衛門", "ざえもん"}, {"右衛門", "えもん"}, {"衛門", "えもん"}, {"兵衛", "べえ"}};
                    for (const auto& e : ends) {
                        if (HasPrefixAt(rs, n, e.first)) {
                            k += e.second;
                            n += text::RuneCount(e.first);
                            office = true;
                            break;
                        }
                    }
                }
                if (office) {
                    static const std::vector<std::pair<std::string, std::string>> suffixes = {
                        {"尉", "のじょう"}, {"佐", "のすけ"}, {"督", "のかみ"}, {"大夫", "のだいぶ"}};
                    for (const auto& s : suffixes) {
                        if (HasPrefixAt(rs, n, s.first)) {
                            k += s.second;
                            n += text::RuneCount(s.first);
                            break;
                        }
                    }
                }
                if (n - p > size) {
                    best = k;
                    size = n - p;
                    bare = prefixes == 0 && !c.ro;
                }
            }
            if (prefixes < 3) {
                for (const OfficeKey& pre : NamePrefixes()) {
                    if (HasPrefixAt(rs, q, pre.key)) Walk(q + text::RuneCount(pre.key), kana + pre.kana, prefixes + 1);
                }
            }
        }
    };
    Walker{rs, p, after, best, size, bare}.Walk(p, "", 0);
    return best;
}

TitlePiece PieceAt(const Runes& rs, std::size_t p, bool after, std::size_t& size) {
    TitlePiece best;
    size = 0;
    auto tryPiece = [&](const TitlePiece& t, std::size_t n) {
        if (n > size) {
            best = t;
            size = n;
        }
    };
    for (const OfficeKey& o : CourtOffices()) {
        if (HasPrefixAt(rs, p, o.key)) {
            TitlePiece t;
            t.kana = o.kana;
            t.full = !Department(o.key);
            tryPiece(t, text::RuneCount(o.key));
        }
    }
    for (const OfficeKey& pr : Provinces()) {
        if (!HasPrefixAt(rs, p, pr.key)) continue;
        for (const auto& s : ProvinceOffices()) {
            if (HasPrefixAt(rs, p + text::RuneCount(pr.key), s.first)) {
                TitlePiece t;
                t.kana = pr.kana + s.second;
                t.full = true;
                tryPiece(t, text::RuneCount(pr.key) + text::RuneCount(s.first));
            }
        }
    }
    for (const OfficeKey& cl : Clans()) {
        if (HasPrefixAt(rs, p, cl.key + "朝臣")) {
            TitlePiece t;
            t.kana = cl.kana + "のあそん";
            t.full = true;
            tryPiece(t, text::RuneCount(cl.key) + 2);
        }
    }
    std::size_t n = 0;
    bool bare = false;
    const std::string kana = CommonName(rs, p, after, n, bare);
    if (n > 0 && (n > size || (bare && n == size))) {
        best = TitlePiece{kana, true, bare, true};
        size = n;
    }
    return best;
}

// The pieces of the longest title at rs[at] and the index after it.
std::vector<TitlePiece> TitleAt(const Runes& rs, std::size_t at, std::size_t& end) {
    std::vector<TitlePiece> pieces;
    std::size_t p = at;
    while (pieces.size() < 3) {
        std::size_t n = 0;
        const TitlePiece piece = PieceAt(rs, p, !pieces.empty(), n);
        if (n == 0) break;
        pieces.push_back(piece);
        p += n;
    }
    end = p;
    return pieces;
}

bool IsCore(const std::string& s) {
    for (const NameCore& c : NameCores()) {
        if (c.key == s) return true;
    }
    return false;
}

bool FullTitle(const std::vector<TitlePiece>& pieces) {
    for (const TitlePiece& t : pieces) {
        if (!t.full) return false;
    }
    return true;
}

// The pieces in Hangul, separated by spaces; a bare core joins the piece before it.
std::string JoinTitle(const std::vector<TitlePiece>& pieces) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < pieces.size(); i++) {
        const TitlePiece& t = pieces[i];
        if (i > 0 && t.bare) {
            out.back() = text::FromKana(pieces[i - 1].kana + t.kana, text::Preset::FanShort);
            continue;
        }
        out.push_back(text::FromKana(t.kana, text::Preset::FanShort));
    }
    std::string joined;
    for (std::size_t i = 0; i < out.size(); i++) {
        if (i > 0) joined += " ";
        joined += out[i];
    }
    return joined;
}

// The entries of the name with a title that the kanji run rs ends with, and its surname
// ("" for none). Of the readings with a surname, the one whose surname starts the run,
// then whose surname the dictionary knows best, then whose surname is longest wins; without
// a surname, the title and given name that start first make the entries.
Entries HistoricalName(const dict::Snapshot& snap, const Runes& rs, std::string& surnameOut) {
    Entries best, fallback;
    std::string bestSurname;
    int bestScore = 0;
    bool haveBest = false, haveFallback = false;
    for (std::size_t t = 0; t < rs.size(); t++) {
        std::size_t end = 0;
        const std::vector<TitlePiece> pieces = TitleAt(rs, t, end);
        if (pieces.empty()) continue;
        std::string given;
        std::size_t stop = rs.size();
        if (end < rs.size()) {
            // A given name has at most three kanji.
            if (rs.size() - end <= 3) given = GivenName(snap, Sub(rs, end, rs.size()));
            if (given.empty()) {
                if (!FullTitle(pieces) || rs.size() - end > 3) continue;
                stop = end;
            }
        } else if (!FullTitle(pieces)) {
            continue;
        }
        const std::string title = JoinTitle(pieces);
        const std::string named = given.empty() ? std::string() : Sub(rs, t, rs.size());
        for (std::size_t s = t >= 4 ? t - 4 : 0; s < t; s++) {
            const std::string sur = Sub(rs, s, t);
            int rank = 0;
            std::string surname = SurnameOf(snap, sur, rank);
            if (surname.empty() && s == 0 && t >= 2) surname = KunSurname(sur);
            int score = rank * 8 + static_cast<int>(t - s);
            if (s == 0) score += 32;
            if (surname.empty() || score <= bestScore || (given.empty() && t - s < 2)) continue;
            Entries out;
            out[Sub(rs, s, stop)] = Join({surname, title, given});
            if (t - s >= 2 && rank > 0) out[sur] = surname;
            if (stop == rs.size() && ((given.empty() && rs.size() - t >= 3) ||
                                      (pieces.size() == 1 && pieces[0].common && end - t >= 2 && !IsCore(Sub(rs, t, end))))) {
                out[Sub(rs, t, end)] = title;
            }
            if (!given.empty()) {
                out[sur + Sub(rs, end, rs.size())] = surname + " " + given;
                out[named] = title + " " + given;
                if (rs.size() - end >= 2) out[Sub(rs, end, rs.size())] = given;
            }
            best = out;
            haveBest = true;
            bestSurname = sur;
            bestScore = score;
        }
        if (!haveFallback && !given.empty()) {
            fallback[named] = title + " " + given;
            haveFallback = true;
            if (rs.size() - end >= 2) fallback[Sub(rs, end, rs.size())] = given;
        }
    }
    if (haveBest) {
        surnameOut = bestSurname;
        return best;
    }
    surnameOut.clear();
    return fallback;
}

// The entry of a kanji run that starts a line of a list of persons: a surname of the
// dictionary (or one of surnames, found with titles in the document) and a given name of
// one to three kanji.
Entries ListedName(const dict::Snapshot& snap, const Runes& rs, const std::set<std::string>& surnames) {
    for (std::size_t t = 1; t < rs.size() && t <= 4; t++) {
        if (rs.size() - t > 3) continue;
        const std::string head = Sub(rs, 0, t);
        int rank = 0;
        const std::string surname = SurnameOf(snap, head, rank);
        if (surname.empty() || (rank != 3 && surnames.count(head) == 0)) continue;
        if (rs.size() - t >= 2 && KnownPerson(snap, Sub(rs, t, rs.size()))) continue;
        const std::string given = GivenName(snap, Sub(rs, t, rs.size()));
        if (!given.empty()) return Entries{{Sub(rs, 0, rs.size()), surname + " " + given}};
    }
    return {};
}

// Whether the code points before rs[i] are all white space, so that the run at i starts the line.
bool LineStartsList(const Runes& rs, std::size_t i) {
    for (std::size_t k = 0; k < i; k++) {
        if (rs[k] != U' ' && rs[k] != U'　' && rs[k] != U'\t') return false;
    }
    return true;
}

}  // namespace

HistoricalLine ReadHistoricalLine(const dict::Snapshot& snap, std::string_view line) {
    HistoricalLine out;
    const Runes rs = ToRunes(line);
    for (std::size_t i = 0; i < rs.size();) {
        if (!IsNameKanji(rs[i])) {
            i++;
            continue;
        }
        std::size_t j = i;
        while (j < rs.size() && IsNameKanji(rs[j])) j++;
        const Runes run(rs.begin() + static_cast<std::ptrdiff_t>(i), rs.begin() + static_cast<std::ptrdiff_t>(j));
        std::string surname;
        const Entries entries = HistoricalName(snap, run, surname);
        for (const auto& [key, value] : entries) {
            if (j - i > out.found[key].run) out.found[key] = {value, j - i};
        }
        if (!surname.empty()) {
            out.surnames.insert(surname);
        } else if (entries.empty() && LineStartsList(rs, i) && j < rs.size() &&
                   (rs[j] == U' ' || rs[j] == U'　' || rs[j] == U'\t')) {
            out.listed.push_back(run);
        }
        i = j;
    }
    return out;
}

std::string HistoricalRows(const dict::Snapshot& snap, std::span<const HistoricalLine* const> lines) {
    std::map<std::string, HistoricalReading> found;
    std::set<std::string> surnames;
    for (const HistoricalLine* line : lines) {
        for (const auto& [key, reading] : line->found) {
            if (reading.run > found[key].run) found[key] = reading;
        }
        surnames.insert(line->surnames.begin(), line->surnames.end());
    }
    for (const HistoricalLine* line : lines) {
        for (const Runes& run : line->listed) {
            for (const auto& [key, value] : ListedName(snap, run, surnames)) {
                if (run.size() > found[key].run) found[key] = {value, run.size()};
            }
        }
    }
    std::string out;
    for (const auto& [key, reading] : found) out += key + "\t" + reading.text + "\tname\n";
    return out;
}

std::string HistoricalNames(const dict::Snapshot& snap, std::span<const std::string> lines) {
    std::vector<HistoricalLine> facts;
    facts.reserve(lines.size());
    for (const std::string& line : lines) facts.push_back(ReadHistoricalLine(snap, line));
    std::vector<const HistoricalLine*> views;
    for (const HistoricalLine& line : facts) views.push_back(&line);
    return HistoricalRows(snap, views);
}

std::string_view CommonNamePrefix(std::string_view prefix) {
    for (const OfficeKey& p : NamePrefixes()) {
        if (p.key == prefix) return p.kana;
    }
    return {};
}

}  // namespace hdTrans::names
