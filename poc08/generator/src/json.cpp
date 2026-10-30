#include "json.h"

#include <cstdio>
#include <cstdlib>

#include "hangul.h"

namespace hd8gen::json {

const Value* Value::Get(const std::string& key) const {
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}
std::string Value::Str(const std::string& key, const std::string& def) const {
    const Value* v = Get(key);
    return v && v->t == T::Str ? v->str : def;
}
double Value::Num(const std::string& key, double def) const {
    const Value* v = Get(key);
    return v && v->t == T::Num ? v->num : def;
}
bool Value::Bool(const std::string& key, bool def) const {
    const Value* v = Get(key);
    if (!v) return def;
    if (v->t == T::Bool) return v->b;
    if (v->t == T::Num) return v->num != 0;
    return def;
}
void Value::Set(const std::string& key, Value v) {
    for (auto& kv : obj)
        if (kv.first == key) {
            kv.second = std::move(v);
            return;
        }
    obj.emplace_back(key, std::move(v));
}

Value S(const std::string& s) {
    Value v;
    v.t = Value::T::Str;
    v.str = s;
    return v;
}
Value N(double d) {
    Value v;
    v.t = Value::T::Num;
    v.num = d;
    return v;
}
Value B(bool b) {
    Value v;
    v.t = Value::T::Bool;
    v.b = b;
    return v;
}

namespace {

struct Reader {
    const std::string& s;
    size_t i = 0;
    std::string err;
    void Ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool Fail(const std::string& m) {
        if (err.empty()) err = m + " at " + std::to_string(i);
        return false;
    }
    bool Hex4(unsigned& out) {
        if (i + 4 > s.size()) return Fail("short \\u");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return Fail("bad \\u");
        }
        return true;
    }
    bool String(std::string& out) {
        if (s[i] != '"') return Fail("expected string");
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i >= s.size()) return Fail("bad escape");
            c = s[i++];
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = 0;
                        if (!Hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    hangul::AppendU8(out, static_cast<char32_t>(cp));
                    break;
                }
                default: out.push_back(c);
            }
        }
        if (i >= s.size()) return Fail("unterminated string");
        ++i;
        return true;
    }
    bool Val(Value& v) {
        Ws();
        if (i >= s.size()) return Fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            v.t = Value::T::Obj;
            ++i;
            Ws();
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            for (;;) {
                Ws();
                std::string k;
                if (!String(k)) return false;
                Ws();
                if (i >= s.size() || s[i] != ':') return Fail("expected :");
                ++i;
                Value x;
                if (!Val(x)) return false;
                v.obj.emplace_back(std::move(k), std::move(x));
                Ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    ++i;
                    return true;
                }
                return Fail("expected , or }");
            }
        }
        if (c == '[') {
            v.t = Value::T::Arr;
            ++i;
            Ws();
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            for (;;) {
                Value x;
                if (!Val(x)) return false;
                v.arr.push_back(std::move(x));
                Ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    ++i;
                    return true;
                }
                return Fail("expected , or ]");
            }
        }
        if (c == '"') {
            v.t = Value::T::Str;
            return String(v.str);
        }
        if (s.compare(i, 4, "true") == 0) {
            v.t = Value::T::Bool;
            v.b = true;
            i += 4;
            return true;
        }
        if (s.compare(i, 5, "false") == 0) {
            v.t = Value::T::Bool;
            i += 5;
            return true;
        }
        if (s.compare(i, 4, "null") == 0) {
            i += 4;
            return true;
        }
        const size_t start = i;
        while (i < s.size() && (s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                                (s[i] >= '0' && s[i] <= '9')))
            ++i;
        if (start == i) return Fail("unexpected character");
        v.t = Value::T::Num;
        v.num = std::strtod(s.substr(start, i - start).c_str(), nullptr);
        return true;
    }
};

void Esc(std::string& out, const std::string& s) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void W(std::string& out, const Value& v) {
    switch (v.t) {
        case Value::T::Null: out += "null"; break;
        case Value::T::Bool: out += v.b ? "true" : "false"; break;
        case Value::T::Num: {
            char buf[32];
            if (v.num == static_cast<double>(static_cast<long long>(v.num)))
                std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v.num));
            else
                std::snprintf(buf, sizeof buf, "%g", v.num);
            out += buf;
            break;
        }
        case Value::T::Str: Esc(out, v.str); break;
        case Value::T::Arr:
            out.push_back('[');
            for (size_t k = 0; k < v.arr.size(); ++k) {
                if (k) out.push_back(',');
                W(out, v.arr[k]);
            }
            out.push_back(']');
            break;
        case Value::T::Obj:
            out.push_back('{');
            for (size_t k = 0; k < v.obj.size(); ++k) {
                if (k) out.push_back(',');
                Esc(out, v.obj[k].first);
                out.push_back(':');
                W(out, v.obj[k].second);
            }
            out.push_back('}');
            break;
    }
}

}  // namespace

bool Parse(const std::string& text, Value& out, std::string& err) {
    Reader r{text, 0, {}};
    out = Value{};
    if (!r.Val(out)) {
        err = r.err;
        return false;
    }
    return true;
}

std::string Write(const Value& v) {
    std::string out;
    W(out, v);
    return out;
}

}  // namespace hd8gen::json
