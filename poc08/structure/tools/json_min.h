#pragma once
// A small JSON reader and string escaper for the structure dev tool (toks.jsonl in, ir JSON out).
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hd8json {

struct Value {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    const Value* Get(std::string_view key) const {
        for (const auto& [k, v] : obj)
            if (k == key) return &v;
        return nullptr;
    }
    std::string_view S(std::string_view key) const {
        const Value* v = Get(key);
        return v != nullptr && v->type == Str ? std::string_view(v->str) : std::string_view();
    }
    double N(std::string_view key, double def = 0) const {
        const Value* v = Get(key);
        return v != nullptr && v->type == Num ? v->num : def;
    }
};

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}
    bool Parse(Value& out) {
        Ws();
        if (!ParseValue(out)) return false;
        Ws();
        return true;
    }

private:
    std::string_view s_;
    size_t i_ = 0;

    void Ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_;
    }
    static void PutUtf8(std::string& o, unsigned cp) {
        if (cp < 0x80) {
            o += static_cast<char>(cp);
        } else if (cp < 0x800) {
            o += static_cast<char>(0xC0 | (cp >> 6));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool ParseString(std::string& o) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c != '\\') {
                o += c;
                continue;
            }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    if (i_ + 4 > s_.size()) return false;
                    unsigned cp = static_cast<unsigned>(std::strtoul(std::string(s_.substr(i_, 4)).c_str(), nullptr, 16));
                    i_ += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        unsigned lo = static_cast<unsigned>(
                            std::strtoul(std::string(s_.substr(i_ + 2, 4)).c_str(), nullptr, 16));
                        i_ += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    PutUtf8(o, cp);
                    break;
                }
                default: o += e; break;
            }
        }
        if (i_ >= s_.size()) return false;
        ++i_;
        return true;
    }
    bool ParseValue(Value& v) {
        Ws();
        if (i_ >= s_.size()) return false;
        char c = s_[i_];
        if (c == '{') {
            v.type = Value::Obj;
            ++i_;
            Ws();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return true;
            }
            for (;;) {
                Ws();
                std::string k;
                if (!ParseString(k)) return false;
                Ws();
                if (i_ >= s_.size() || s_[i_] != ':') return false;
                ++i_;
                Value x;
                if (!ParseValue(x)) return false;
                v.obj.emplace_back(std::move(k), std::move(x));
                Ws();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == '}') {
                    ++i_;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            v.type = Value::Arr;
            ++i_;
            Ws();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return true;
            }
            for (;;) {
                Value x;
                if (!ParseValue(x)) return false;
                v.arr.push_back(std::move(x));
                Ws();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == ']') {
                    ++i_;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            v.type = Value::Str;
            return ParseString(v.str);
        }
        if (s_.substr(i_, 4) == "true") {
            v.type = Value::Bool;
            v.b = true;
            i_ += 4;
            return true;
        }
        if (s_.substr(i_, 5) == "false") {
            v.type = Value::Bool;
            i_ += 5;
            return true;
        }
        if (s_.substr(i_, 4) == "null") {
            i_ += 4;
            return true;
        }
        size_t start = i_;
        while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-' ||
                                  s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E'))
            ++i_;
        if (start == i_) return false;
        v.type = Value::Num;
        v.num = std::strtod(std::string(s_.substr(start, i_ - start)).c_str(), nullptr);
        return true;
    }
};

inline std::string Quote(std::string_view s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o + "\"";
}

}  // namespace hd8json
