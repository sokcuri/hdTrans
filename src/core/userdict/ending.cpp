// End and pat rows. An end row writes an ending attached to a model word: the verb
// 書く / 쓰다, the i-adjective 高い / 높다, the adjectival noun 静か / 조용하다 or the
// noun 学生 / 학생. The JP rest after the model's conjugation is the ending key; the KR
// rest after the model's form becomes the form code and text of the ending
// ("썼잖아" -> "5잖아").
#include <string_view>

#include "core/userdict/compile.h"

namespace hdTrans::userdict::internal {

namespace {

constexpr std::string_view kModelVerb = "書", kModelVerbKR = "쓰";
constexpr std::string_view kModelAdjective = "高", kModelAdjectiveKR = "높";
constexpr std::string_view kModelNa = "静か", kModelNaKR = "조용";
constexpr std::string_view kModelNoun = "学生", kModelNounKR = "학생";

std::string NotAForm(std::string_view kr, std::string_view model) {
    return "KR " + Quote(kr) + " does not start with a form of " + std::string(model) + "다";
}

// The form code of the model 쓰다 at the start of kr and the text after the form.
std::string VerbForm(std::string_view kr, char& code, std::string& text) {
    const std::u32string r = Runes(kr);
    const std::u32string_view next = r.size() > 1 ? std::u32string_view(r).substr(1, 1) : std::u32string_view();
    const std::string rest = Utf8(std::u32string_view(r).substr(r.empty() ? 0 : 1));
    const auto form = [&](char c, std::string t) {
        code = c;
        text = std::move(t);
        return std::string();
    };
    if (!r.empty()) {
        switch (r[0]) {
            case U'썼': return form('5', rest);
            case U'써': return form('6', rest);
            case U'쓸': return form('3', rest);
            case U'쓴': return form(next == U"다" || next == U"답" || next == U"단" ? '4' : '8', rest);
            case U'씁': return form(next == U"시" ? '9' : '7', rest);
            case U'씀': return form('B', rest);
            case U'쓰':
                if (next == U"는") return form('0', Utf8(std::u32string_view(r).substr(2)));
                if (next == U"면" || next == U"러" || next == U"려") return form('2', rest);
                if (next == U"니" || next == U"며" || next == U"오" || next == U"옵" || next == U"세" || next == U"시" ||
                    next == U"십") {
                    return form('A', rest);
                }
                return form('1', rest);
        }
    }
    return NotAForm(kr, kModelVerbKR);
}

// Strips model from kr and reads the form code from forms, each the form's text after
// the model followed by its code; the last form is the bare model.
std::string FormByPrefix(std::string_view kr, std::string_view model, std::initializer_list<std::string_view> forms,
                         char& code, std::string& text) {
    if (kr.starts_with(model)) {
        const std::string_view rest = kr.substr(model.size());
        for (const std::string_view f : forms) {
            const std::string_view form = f.substr(0, f.size() - 1);
            if (rest.starts_with(form)) {
                code = f.back();
                text = rest.substr(form.size());
                return {};
            }
        }
    }
    return NotAForm(kr, model);
}

// The particles after a noun that alternate with the noun's final consonant:
// "이었" -> "(이었)였", and "이" + X -> "(이)" + X.
constexpr std::pair<std::string_view, std::string_view> kJosaForms[] = {
    {"이었", "(이었)였"}, {"이에", "(이에)예"}, {"이어", "(이어)여"}, {"이다", "(이)다"}, {"이라", "(이)라"},
    {"이랍", "(이)랍"}, {"이래", "(이)래"}, {"이예", "(이)예"}, {"이군", "(이)군"}, {"이구", "(이)구"},
    {"이지", "(이)지"}, {"이면", "(이)면"}, {"이니", "(이)니"}, {"이겠", "(이)겠"}, {"이야", "(이)야"},
    {"이든", "(이)든"}, {"이잖", "(이)잖"}, {"이란", "(이)란"}, {"이나", "(이)나"}, {"이고", "(이)고"}, {"이네", "(이)네"}, {"이로", "(이)로"}, {"이 ", "(이)가 "},
    {"을 ", "(을)를 "}, {"은 ", "(은)는 "}, {"으로", "(으)로"},
};

// The Korean after the noun model with its alternating particle.
std::string JosaForm(std::string_view rest) {
    for (const auto& [consonant, particle] : kJosaForms) {
        if (rest.starts_with(consonant)) return std::string(particle) + std::string(rest.substr(consonant.size()));
    }
    return std::string(rest);
}

}  // namespace

bool PoliteJP(std::string_view jp) {
    for (const std::string_view s : {"ます", "まし", "ませ", "です", "でし", "ござい"}) {
        if (jp.find(s) != std::string_view::npos) return true;
    }
    return false;
}

std::string CompileEnding(std::string_view jp, std::string_view kr, Rec& r) {
    char code = 0;
    std::string text, err;
    if (jp.starts_with(kModelVerb)) {
        const std::string_view tail = jp.substr(kModelVerb.size());
        int branch = 0;
        std::size_t length = 0;
        if (!AnalyseVerbTail(kModelVerb, '1', tail, branch, length)) {
            return Quote(jp) + " is not a conjugation of " + std::string(kModelVerb) + "く";
        }
        r.key = tail.substr(length < tail.size() ? length : tail.size());
        r.part = std::string("S5") + static_cast<char>('0' + branch) + "0";
        err = VerbForm(kr, code, text);
        r.euImperative = kr.starts_with("쓰라");
    } else if (jp.starts_with(kModelAdjective)) {
        r.key = jp.substr(kModelAdjective.size());
        r.part = "T500";
        err = FormByPrefix(kr, kModelAdjectiveKR, {"았5", "아6", "습7", "음8", "은4", "을3", "으2", "1"}, code, text);
    } else if (jp.starts_with(kModelNa)) {
        r.key = jp.substr(kModelNa.size());
        r.part = "U500";
        err = FormByPrefix(kr, kModelNaKR, {"했5", "해6", "합7", "함8", "한4", "할3", "하1"}, code, text);
        if (code == '1' && (text.starts_with("면") || text.starts_with("러") || text.starts_with("려") ||
                            text.starts_with("니") || text.starts_with("며") || text.starts_with("므") ||
                            text.starts_with("오"))) {
            code = '2';
        }
    } else if (jp.starts_with(kModelNoun)) {
        if (!kr.starts_with(kModelNounKR)) {
            return "KR " + Quote(kr) + " does not start with " + std::string(kModelNounKR);
        }
        r.key = jp.substr(kModelNoun.size());
        r.part = "P200";
        r.target = JosaForm(kr.substr(kModelNounKR.size()));
        return {};
    } else {
        return "end JP " + Quote(jp) + " does not start with a model word (" + std::string(kModelVerb) + "く verb, " +
               std::string(kModelAdjective) + "い i-adjective, " + std::string(kModelNa) + " adjectival noun, " +
               std::string(kModelNoun) + " noun)";
    }
    if (!err.empty()) return err;
    if (r.key.empty() && r.part[0] != 'S') return "end row has no ending after the model word";
    r.target = std::string(1, code) + text;
    return {};
}

}  // namespace hdTrans::userdict::internal
