// Speaker labels, honorifics and the evidence of a single run: the script formats
// (OutputLine, Ren'Py, KiriKiri, TyranoScript), the plain-text forms (【名】, [名]:,
// 名「」, 名：), the speaker cues of a line and the name runs of a line.
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/codes.h"
#include "core/names/chars.h"
#include "core/names/labels.h"
#include "core/names/registry.h"

namespace hdTrans::names {

using namespace internal;

namespace {

constexpr std::string_view kHonorificList[] = {"さん", "ちゃん", "くん", "君", "様", "さま", "先輩", "殿", "氏", "たん",
                                                      "嬢", "はん", "っち", "ちゃま", "先生", "せんせい", "さぁん", "ちゃーん",
                                                      "くーん", "公", "卿"};

constexpr std::string_view kKanjiHonorificList[] = {"先生", "先輩", "様", "殿", "君", "氏", "嬢"};

constexpr std::string_view kTitleSuffixList[] = {"先生", "先輩", "選手", "様", "殿", "君", "氏", "嬢", "家", "姉",
                                                        "兄", "夫妻", "一家", "達", "たち"};

constexpr std::string_view kPersonSuffixList[] = {"達", "家", "氏", "ら", "一家", "夫妻", "夫婦", "兄弟", "姉妹",
                                                         "親子", "一族", "様", "殿", "君"};

constexpr std::string_view kRoleWords[] = {
    "父", "母", "兄", "姉", "弟", "妹", "叔父", "叔母", "伯父", "伯母", "祖父", "祖母", "爺", "婆", "主人", "奥", "奥様", "皆", "俺",
    "私", "僕", "自分", "子供", "子", "お前", "貴方", "先生", "先輩", "後輩", "部長", "会長", "店長", "社長", "委員長", "校長", "村長",
    "監督", "師匠", "殿下", "陛下", "姫", "王", "王子", "王女", "女王", "魔王", "勇者", "神", "神様", "下級生", "上級生", "生徒", "男",
    "女", "女子", "男子", "少年", "少女", "客", "前", "屋", "皆様", "皆さん", "親父", "お袋", "娘", "息子", "嫁", "旦那", "彼", "彼女",
    "誰", "方", "赤", "坊", "店員", "黒服", "邪神", "お客", "刑事", "警部", "博士", "教授", "医者", "看護師", "選手", "コーチ",
    "ウェイトレス", "メイド", "マスター", "ギャラリー", "シスター", "ママ", "パパ", "オーナー", "マネージャー", "キャプテン",
    "リーダー", "ボス", "お嬢", "じい", "ばあ"};

constexpr std::string_view kLabelRoleWords[] = {
    "みんな", "皆", "一同", "全員", "２人", "二人", "3人", "３人", "声", "電話", "アナウンス", "ナレーション", "司会", "実況", "解説",
    "店長", "店員", "担任", "秘書", "父親", "母親", "刑事", "女性", "男性", "女の子", "男の子", "クラスメイト", "おばあさん",
    "おじいさん", "ばあちゃん", "じいちゃん", "おばちゃん", "おじちゃん", "おばさん", "おじさん", "客", "お客さん", "子供",
    "スタッフ", "警察", "守衛", "審判", "ネコ", "猫", "犬", "魔物", "騎士", "兵士", "部員", "生徒", "教師", "医者"};

constexpr std::string_view kLabelRoleSuffixes[] = {"生徒", "女子", "男子", "部", "客", "員", "長", "者", "親", "性", "係",
                                                          "手", "官", "師", "顧問", "補佐", "主将", "担当", "学生", "ＤＪ", "DJ",
                                                          "たち", "達", "メンバー"};

constexpr std::string_view kParentSuffixes[] = {"の父", "の母", "の兄", "の姉", "の弟", "の妹"};

// Particles that may follow a name where a verb or adjective stem would take its
// inflection.
constexpr std::string_view kParticles[] = {"は", "が", "を", "の", "に", "へ", "と", "も", "や", "で", "よ", "ね", "さ"};

bool Has(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }
bool InList(std::string_view text, std::span<const std::string_view> list) {
    for (const std::string_view w : list) {
        if (w == text) return true;
    }
    return false;
}

bool AnyRuneOf(std::string_view s, bool (*pred)(char32_t)) {
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = RuneAt(s, i);
        if (pred(r.cp)) return true;
        i += r.size;
    }
    return false;
}

bool IsParticleRune(char32_t c) {
    for (const std::string_view p : kParticles) {
        if (FirstRune(p).cp == c) return true;
    }
    return false;
}

// labelRolePattern: ？ ? （ or a digit, a Latin or full-width letter A-F at the end, or X の Y
// with X and Y not hiragana.
bool LabelRolePattern(std::string_view p) {
    if (AnyRuneOf(p, [](char32_t c) { return InRunes(c, "？?（") || (c >= '0' && c <= '9') || (c >= 0xFF10 && c <= 0xFF19); })) {
        return true;
    }
    if (!p.empty()) {
        const char32_t last = RuneBefore(p, p.size()).cp;
        if (InRunes(last, "ＡＢＣＤＥＦ") || (last >= 'A' && last <= 'F')) return true;
    }
    std::vector<char32_t> rs;
    for (std::size_t i = 0; i < p.size();) {
        const text::Rune r = RuneAt(p, i);
        rs.push_back(r.cp);
        i += r.size;
    }
    for (std::size_t i = 0; i + 2 < rs.size(); i++) {
        if (!IsHiragana(rs[i]) && rs[i + 1] == 0x306E && !IsHiragana(rs[i + 2])) return true;
    }
    return false;
}

// Matches of the script and plain-text label forms. A form matches at the start of the
// line; \s is the ASCII white space of the patterns.

std::size_t SkipSpace(std::string_view s, std::size_t p) {
    while (p < s.size() && IsPatternSpace(static_cast<unsigned char>(s[p]))) p++;
    return p;
}

bool IsWordByte(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

std::size_t WordEnd(std::string_view s, std::size_t p) {
    while (p < s.size() && IsWordByte(s[p])) p++;
    return p;
}

// OutputLine\(\s*"([^"]*)"\s*,\s*NULL, unanchored.
bool OutputLineForm(std::string_view line, std::string& label) {
    static constexpr std::string_view kCall = "OutputLine(";
    for (std::size_t pos = 0; pos < line.size(); pos++) {
        if (!line.substr(pos).starts_with(kCall)) continue;
        std::size_t p = SkipSpace(line, pos + kCall.size());
        if (p >= line.size() || line[p] != '"') continue;
        const std::size_t close = line.find('"', p + 1);
        if (close == std::string_view::npos) continue;
        const std::string_view group = line.substr(p + 1, close - p - 1);
        p = SkipSpace(line, close + 1);
        if (p >= line.size() || line[p] != ',') continue;
        p = SkipSpace(line, p + 1);
        if (!line.substr(p).starts_with("NULL")) continue;
        label = std::string(group);
        return true;
    }
    return false;
}

// ^\s*define\s+(\w+)\s*=\s*Character\(\s*(?:_\(\s*)?["']([^"']+)["']
bool RenpyDefine(std::string_view line, std::string& actor, std::string& name) {
    std::size_t p = SkipSpace(line, 0);
    if (!line.substr(p).starts_with("define")) return false;
    const std::size_t afterKeyword = SkipSpace(line, p + 6);
    if (afterKeyword == p + 6) return false;
    p = afterKeyword;
    const std::size_t wordEnd = WordEnd(line, p);
    if (wordEnd == p) return false;
    actor = std::string(line.substr(p, wordEnd - p));
    p = SkipSpace(line, wordEnd);
    if (p >= line.size() || line[p] != '=') return false;
    p = SkipSpace(line, p + 1);
    if (!line.substr(p).starts_with("Character(")) return false;
    p = SkipSpace(line, p + 10);
    if (line.substr(p).starts_with("_(")) p = SkipSpace(line, p + 2);
    if (p >= line.size() || (line[p] != '"' && line[p] != '\'')) return false;
    p++;
    std::size_t q = p;
    while (q < line.size() && line[q] != '"' && line[q] != '\'') q++;
    if (q == p || q >= line.size()) return false;
    name = std::string(line.substr(p, q - p));
    return true;
}

// ^\s*(\w+)\s+"
bool RenpySay(std::string_view line, std::string& actor) {
    const std::size_t p = SkipSpace(line, 0);
    const std::size_t wordEnd = WordEnd(line, p);
    if (wordEnd == p) return false;
    const std::size_t spaceEnd = SkipSpace(line, wordEnd);
    if (spaceEnd == wordEnd || spaceEnd >= line.size() || line[spaceEnd] != '"') return false;
    actor = std::string(line.substr(p, wordEnd - p));
    return true;
}

// ^\s*(?:\[|@)(?:name|nm)\s+(?:text|t)="([^"]+)"
bool KagName(std::string_view line, std::string& label) {
    std::size_t p = SkipSpace(line, 0);
    if (p >= line.size() || (line[p] != '[' && line[p] != '@')) return false;
    p++;
    if (line.substr(p).starts_with("name")) {
        p += 4;
    } else if (line.substr(p).starts_with("nm")) {
        p += 2;
    } else {
        return false;
    }
    const std::size_t spaceEnd = SkipSpace(line, p);
    if (spaceEnd == p) return false;
    p = spaceEnd;
    if (line.substr(p).starts_with("text=")) {
        p += 5;
    } else if (line.substr(p).starts_with("t=")) {
        p += 2;
    } else {
        return false;
    }
    if (p >= line.size() || line[p] != '"') return false;
    p++;
    const std::size_t close = line.find('"', p);
    if (close == std::string_view::npos || close == p) return false;
    label = std::string(line.substr(p, close - p));
    return true;
}

// ^#([^\s#:]{1,16})\s*$
bool TyranoName(std::string_view line, std::string& label) {
    if (line.empty() || line[0] != '#') return false;
    std::size_t p = 1, n = 0;
    while (p < line.size()) {
        const text::Rune r = RuneAt(line, p);
        if (IsPatternSpace(r.cp) || r.cp == '#' || r.cp == ':') break;
        p += r.size;
        n++;
    }
    if (n < 1 || n > 16 || SkipSpace(line, p) != line.size()) return false;
    label = std::string(line.substr(1, p - 1));
    return true;
}

// ^\s*(?:【([^【】]{1,16})】|\[([^\[\]]{1,16})\]\s*[:：])
bool BracketName(std::string_view line, std::string& label) {
    const std::size_t p = SkipSpace(line, 0);
    if (line.substr(p).starts_with("【")) {
        const std::size_t start = p + std::string_view("【").size();
        std::size_t q = start, n = 0;
        while (q < line.size()) {
            const text::Rune r = RuneAt(line, q);
            if (r.cp == U'【' || r.cp == U'】') break;
            q += r.size;
            n++;
        }
        if (n >= 1 && n <= 16 && line.substr(q).starts_with("】")) {
            label = std::string(line.substr(start, q - start));
            return true;
        }
    }
    if (p < line.size() && line[p] == '[') {
        std::size_t q = p + 1, n = 0;
        while (q < line.size() && line[q] != '[' && line[q] != ']') {
            q += RuneAt(line, q).size;
            n++;
        }
        if (n >= 1 && n <= 16 && q < line.size() && line[q] == ']') {
            const std::size_t after = SkipSpace(line, q + 1);
            if (line.substr(after).starts_with(":") || line.substr(after).starts_with("：")) {
                label = std::string(line.substr(p + 1, q - p - 1));
                return true;
            }
        }
    }
    return false;
}

// ^\s*([^\s「」『』【】\[\]（）：:。、！？!?…]{1,16})\s*(?:[「『]|[:：])
bool QuoteName(std::string_view line, std::string& label) {
    static constexpr std::u32string_view kExcluded = U"「」『』【】[]（）：:。、！？!?…";
    const std::size_t p = SkipSpace(line, 0);
    std::size_t q = p, n = 0;
    while (q < line.size()) {
        const text::Rune r = RuneAt(line, q);
        if (IsPatternSpace(r.cp) || kExcluded.find(r.cp) != std::u32string_view::npos) break;
        q += r.size;
        n++;
    }
    if (n < 1 || n > 16) return false;
    const std::size_t after = SkipSpace(line, q);
    if (after >= line.size()) return false;
    const std::string_view rest = line.substr(after);
    if (!(rest.starts_with("「") || rest.starts_with("『") || rest.starts_with(":") || rest.starts_with("："))) return false;
    // A reading in brackets that the word goes on after is ruby, not a quote (全「すべ」て).
    if (rest.starts_with("「") || rest.starts_with("『")) {
        const std::size_t open = std::string_view("「").size();
        const std::size_t close = rest.find(rest.starts_with("「") ? "」" : "』", open);
        if (close != std::string_view::npos) {
            const std::string_view inside = rest.substr(open, close - open);
            const std::string_view tail = rest.substr(close + std::string_view("」").size());
            bool kana = !inside.empty();
            for (std::size_t i = 0; i < inside.size() && kana;) {
                const text::Rune r = RuneAt(inside, i);
                kana = (r.cp >= U'ぁ' && r.cp <= U'ゖ') || (r.cp >= U'ァ' && r.cp <= U'ヺ') || r.cp == U'ー';
                i += r.size == 0 ? 1 : r.size;
            }
            if (kana && !tail.empty() && !IsPatternSpace(RuneAt(tail, 0).cp) &&
                kExcluded.find(RuneAt(tail, 0).cp) == std::u32string_view::npos) {
                return false;
            }
        }
    }
    label = std::string(line.substr(p, q - p));
    return true;
}

// <[^>]*> removed.
std::string StripTags(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '<') {
            const std::size_t close = s.find('>', i + 1);
            if (close != std::string_view::npos) {
                i = close + 1;
                continue;
            }
        }
        out += s[i];
        i++;
    }
    return out;
}

// The speaker-cue form: ^[ 　]*([^\s「」『』、。！？…‥・]{2,6}?)(?:「|です[。！!、…‥ ]|です$|でち|っす|、(?:出撃|...))
// with (?m): ^ is a line start and $ a line end. Returns the label of a match at pos.
bool CueLabelChar(char32_t c) {
    static constexpr std::u32string_view kExcluded = U"「」『』、。！？…‥・";
    return !IsPatternSpace(c) && kExcluded.find(c) == std::u32string_view::npos;
}

bool CueAltAt(std::string_view s, std::size_t pos) {
    static const std::vector<std::string_view> kWords = {"出撃", "出る", "出ます", "行く", "行き", "いく", "参り", "参る", "参ります",
                                                         "見参", "抜錨", "了解", "推参", "負け", "守り", "突撃", "いざ"};
    const std::string_view rest = s.substr(pos);
    if (rest.starts_with("「") || rest.starts_with("でち") || rest.starts_with("っす")) return true;
    if (rest.starts_with("です")) {
        const std::size_t after = pos + std::string_view("です").size();
        if (after == s.size() || s[after] == '\n') return true;
        static constexpr std::u32string_view kEnds = U"。！!、…‥ ";
        if (kEnds.find(RuneAt(s, after).cp) != std::u32string_view::npos) return true;
    }
    if (rest.starts_with("、")) {
        const std::string_view tail = rest.substr(std::string_view("、").size());
        for (const std::string_view w : kWords) {
            if (tail.starts_with(w)) return true;
        }
    }
    return false;
}

// Matches the speaker cue at the line start pos; the label on success.
bool CueAt(std::string_view s, std::size_t pos, std::string& label, std::size_t& end) {
    // Leading ASCII and ideographic spaces; the greedy prefix gives back spaces when the
    // label needs them.
    std::vector<std::size_t> starts{pos};
    for (std::size_t p = pos; p < s.size();) {
        const text::Rune r = RuneAt(s, p);
        if (r.cp != ' ' && r.cp != 0x3000) break;
        p += r.size;
        starts.push_back(p);
    }
    for (std::size_t k = starts.size(); k-- > 0;) {
        std::size_t p = starts[k], n = 0;
        while (p < s.size() && n < 6) {
            const text::Rune r = RuneAt(s, p);
            if (!CueLabelChar(r.cp)) break;
            p += r.size;
            n++;
            if (n >= 2 && CueAltAt(s, p)) {
                label = std::string(s.substr(starts[k], p - starts[k]));
                end = p;
                return true;
            }
        }
    }
    return false;
}

// Line starts of s: 0 and each position after a \n.
bool IsLineStart(std::string_view s, std::size_t pos) { return pos == 0 || s[pos - 1] == '\n'; }

// The run of a kanji run or of katakana as RunsOf reports it.
Run MakeRun(std::string_view s, std::size_t start, std::size_t end, bool kanji) {
    Run r;
    r.start = start;
    r.end = end;
    r.text = s.substr(start, end - start);
    r.kanji = kanji;
    return r;
}

}  // namespace

namespace internal {

std::span<const std::string_view> Honorifics() { return kHonorificList; }
std::span<const std::string_view> KanjiHonorifics() { return kKanjiHonorificList; }
std::span<const std::string_view> TitleSuffixes() { return kTitleSuffixList; }
std::span<const std::string_view> PersonSuffixes() { return kPersonSuffixList; }

bool PlainLabel(std::string_view p) {
    if (AllRunes(p, [](char32_t c) { return (IsNameRune(c) && !IsHiragana(c)) || InRunes(c, "・＆&？?"); })) return true;
    return AllRunes(p, IsHiragana) && RuneCount(p) <= 6;
}

bool LabelRole(std::string_view p) {
    if (InList(p, kLabelRoleWords) || LabelRolePattern(p)) return true;
    for (const std::string_view s : kLabelRoleSuffixes) {
        if (p.ends_with(s)) return true;
    }
    for (const char c : p) {
        if (static_cast<unsigned char>(c) < 0x80) return true;
    }
    return false;
}

bool RoleLike(std::string_view t) { return InList(t, kRoleWords) || Has(t, "の"); }

bool ParentLabel(std::string_view p, std::string& x) {
    for (const std::string_view s : kParentSuffixes) {
        if (p.size() > s.size() && p.ends_with(s)) {
            const std::string_view head = p.substr(0, p.size() - s.size());
            if (AllRunes(head, IsNameRune)) {
                x = std::string(head);
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> LabelParts(std::string_view k) {
    std::vector<std::string> parts;
    std::string cur;
    bool any = false;
    for (std::size_t i = 0; i <= k.size();) {
        const text::Rune r = i < k.size() ? RuneAt(k, i) : text::Rune{0, 0};
        if (i == k.size() || InRunes(r.cp, "＆&・＝、")) {
            if (any) parts.push_back(cur);
            cur.clear();
            any = false;
        } else {
            cur.append(k.substr(i, r.size));
            any = true;
        }
        i += r.size == 0 ? 1 : r.size;
    }
    for (std::string& p : parts) {
        std::string_view t = TrimSpace(p);
        for (const std::string_view h : kHonorificList) {
            if (t.size() > h.size() && t.ends_with(h)) {
                t.remove_suffix(h.size());
                break;
            }
        }
        p = std::string(t);
    }
    return parts;
}

std::vector<Run> RunsOf(std::string_view s) {
    std::vector<Run> out;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune c = RuneAt(s, i);
        if (IsKanji(c.cp)) {
            std::size_t j = i + c.size;
            while (j < s.size()) {
                const text::Rune d = RuneAt(s, j);
                if (IsKanji(d.cp)) {
                    j += d.size;
                    continue;
                }
                if (d.cp == 0x30CE) {  // ノ between kanji
                    const std::size_t e = j + d.size;
                    if (e < s.size() && IsKanji(RuneAt(s, e).cp)) {
                        j = e;
                        continue;
                    }
                }
                break;
            }
            out.push_back(MakeRun(s, i, j, true));
            i = j;
        } else if (IsKatakana(c.cp)) {
            std::size_t j = i + c.size;
            while (j < s.size() && IsKatakana(RuneAt(s, j).cp)) j += RuneAt(s, j).size;
            out.push_back(MakeRun(s, i, j, false));
            i = j;
        } else {
            i += c.size;
        }
    }
    return out;
}

std::vector<std::string> SpeakerCueLabels(std::string_view text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::string label;
        std::size_t end = pos;
        if (IsLineStart(text, pos) && CueAt(text, pos, label, end)) {
            out.push_back(label);
            pos = end;
            continue;
        }
        pos++;
    }
    return out;
}

}  // namespace internal

std::vector<std::string> SpeakerCues(std::string_view text) { return internal::SpeakerCueLabels(text); }

std::string Registry::Label(std::string_view line, bool& script) {
    script = false;
    std::string label;
    if (OutputLineForm(line, label)) {
        script = true;
        return std::string(TrimSpace(StripTags(label)));
    }
    std::string actor, name;
    if (RenpyDefine(line, actor, name)) {
        SetActor(actor, name);
        return "";
    }
    if (RenpySay(line, actor)) {
        const auto known = actors_.find(actor);
        if (known != actors_.end() && !known->second.empty()) {
            script = true;
            return known->second;
        }
    }
    if (KagName(line, label)) {
        script = true;
        return label;
    }
    if (TyranoName(line, label)) {
        script = true;
        return label;
    }
    if (BracketName(line, label)) {
        const std::string joined(TrimSpace(label));
        if (PlainLabel(joined)) return joined;
    }
    if (QuoteName(line, label) && PlainLabel(label)) return label;
    return "";
}

DictFlags Registry::Flags(std::string_view s) {
    auto& flags = sharedFlags_ ? sharedFlags_->values : flags_;
    const auto cached = flags.find(std::string(s));
    if (cached != flags.end()) return cached->second;
    DictFlags f;
    if (snap_ != nullptr && !s.empty()) {
        bool roleSem = false;
        for (const dict::Candidate& rec : snap_->RetrieveRecords(s, dict::LookupMode::General)) {
            if (rec.codes == nullptr) continue;
            f.records++;
            const std::string_view sem = rec.codes->sem.Text(), attr = rec.codes->gram.Text();
            if (sem.starts_with("QPN") || attr.starts_with("Wa") || attr.starts_with("Wb")) {
                f.person = true;
                f.surname = f.surname || attr.starts_with("Wa");
                f.given = f.given || attr.starts_with("Wb");
            } else if (sem.starts_with("QCT")) {
                f.city = true;
            } else {
                f.common = true;
                if (!Has(attr, "On") && (Has(sem, "VHn") || Has(sem, "UAq") || Has(sem, "QAA") || Has(sem, "QAB"))) {
                    roleSem = true;
                }
            }
        }
        f.role = roleSem && !f.person && !f.city;
        for (const dict::Record& rec : snap_->NameRecords(s)) {
            if (rec.codes == nullptr || !rec.codes->sem.Text().starts_with("QPN")) continue;
            const std::string_view attr = rec.codes->gram.Text();
            f.layerSurname = f.layerSurname || attr.starts_with("Wa");
            f.layerGiven = f.layerGiven || attr.starts_with("Wb");
            if (f.layerName.empty() && IsHangulText(rec.target)) f.layerName = std::string(rec.target);
        }
    }
    if (sharedFlags_) {
        if (flags.size() >= 8192 || sharedFlags_->bytes + s.size() > 1024 * 1024) {
            flags.clear();
            sharedFlags_->bytes = 0;
        }
        sharedFlags_->bytes += s.size();
    }
    flags[std::string(s)] = f;
    return f;
}

std::string Registry::HonorificRun(std::string_view line, const Run& run) {
    std::string t(run.text);
    std::size_t start = run.start, end = run.end;
    std::string_view rest = line.substr(end);
    std::string_view h;
    for (const std::string_view x : kHonorificList) {
        if (rest.starts_with(x)) {
            h = x;
            break;
        }
    }
    if (h.empty()) {
        for (const std::string_view k : kKanjiHonorificList) {
            if (t.size() > k.size() && std::string_view(t).ends_with(k)) {
                h = k;
                break;
            }
        }
        if (h.empty() || Flags(t).records > 0) return "";
        t.erase(t.size() - h.size());
        end -= h.size();
        rest = line.substr(end);
    }
    if (start > 0) {
        const char32_t prev = RuneBefore(line, start).cp;
        if (prev == U'お' || prev == U'ご') return "";
    }
    const char32_t next = FirstRune(rest.substr(h.size())).cp;
    if (next == U'ゃ' || next == U'ゅ' || next == U'ょ') return "";
    const std::string th = t + std::string(h);
    if (AllRunes(th, IsKanji) && Flags(th).records > 0) return "";
    const bool single = RuneCount(t) == 1 && AllRunes(t, IsKanji);
    if ((h == "たん" || h == "はん" || h == "っち") && (single || InRunes(next, "だでじや"))) return "";
    if (RoleLike(t)) return "";
    if (single && (h == "くん" || h == "さん" || h == "たん") && InRunes(next, "だでじなよ")) return "";
    const std::size_t n = RuneCount(t);
    if ((run.kanji && n <= 4 && AllRunes(t, IsKanji)) || (!run.kanji && n >= 2 && n <= 12)) return t;
    return "";
}

bool Registry::Addressed(std::string_view line, std::string_view t) {
    for (const std::string_view h : kHonorificList) {
        if (line.find(std::string(t) + std::string(h)) != std::string_view::npos) return true;
    }
    bool script = false;
    return Label(line, script) == t;
}

bool Registry::Guarded(std::string_view line, std::size_t a, std::size_t b) {
    struct NounContext {
        std::string_view surface;
        std::vector<std::string_view> contexts;
    };
    static const std::vector<NounContext> kNounContexts = {
        {"桜", {"の花", "の木", "が咲", "が散", "並木", "吹雪", "の季節"}},
        {"光", {"が差", "が射", "を浴び", "を放", "が漏れ", "の中"}},
        {"空", {"を見上げ", "が青", "は青", "を飛", "に浮か", "が晴れ", "が曇"}},
        {"雪", {"が降", "が積も", "が溶け"}},
        {"雨", {"が降", "が止"}},
        {"時雨", {"が降"}},
        {"小鳥", {"のさえずり", "たち", "が鳴"}},
        {"ミルク", {"を", "ティー"}},
        {"翼", {"を広げ", "が生え"}},
        {"花", {"が咲", "を摘"}},
        {"月", {"が出", "が昇", "明かり"}},
    };
    const std::string_view t = line.substr(a, b - a), rest = line.substr(b);
    for (const NounContext& nc : kNounContexts) {
        if (nc.surface != t) continue;
        for (const std::string_view ctx : nc.contexts) {
            if (rest.starts_with(ctx) && !Addressed(line, t)) return true;
        }
    }
    if (snap_ == nullptr) return false;
    std::string_view tail = line.substr(a);
    if (tail.size() > 48) {
        std::size_t k = 48;
        while (k > 0 && (static_cast<unsigned char>(tail[k]) & 0xC0) == 0x80) k--;
        tail = tail.substr(0, k);
    }
    const char32_t next = FirstRune(rest).cp;
    dict::LookupContext ctx;
    ctx.plain = &snap_->Plain();  // Lexical guards use base rows, never additional plain endings.
    const dict::Candidates cands = snap_->GetWordInfo(tail, dict::LookupMode::General, ctx);
    for (const dict::Candidate& c : cands.list) {
        if (c.user != nullptr || c.codes == nullptr) continue;
        const bool person = c.codes->sem.Text().starts_with("QPN");
        if (c.source.size() > t.size() && !person) return true;
        if (c.source.size() == t.size() && !c.codes->part.Empty() &&
            (c.codes->part.Tag() == 'M' || c.codes->part.Tag() == 'H') && IsHiragana(next) && !IsParticleRune(next)) {
            return true;
        }
    }
    return false;
}

}  // namespace hdTrans::names
