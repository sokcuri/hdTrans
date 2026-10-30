#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hd08 {

const Json* Json::Get(std::string_view key) const {
    for (const auto& [k, v] : o)
        if (k == key) return &v;
    return nullptr;
}

namespace {

struct Parser {
    std::string_view t;
    size_t i = 0;
    std::string err;

    bool Fail(const char* what) {
        if (err.empty()) err = std::string(what) + " at byte " + std::to_string(i);
        return false;
    }
    void Ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++;
    }
    static void PutUtf8(std::string& s, unsigned cp) {
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool Hex4(unsigned& v) {
        if (i + 4 > t.size()) return Fail("short \\u escape");
        v = 0;
        for (int k = 0; k < 4; k++) {
            const char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        return true;
    }
    bool String(std::string& s) {
        i++;  // opening quote
        for (;;) {
            if (i >= t.size()) return Fail("unterminated string");
            const char c = t[i++];
            if (c == '"') return true;
            if (c != '\\') {
                s += c;
                continue;
            }
            if (i >= t.size()) return Fail("unterminated escape");
            const char e = t[i++];
            switch (e) {
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                    i += 2;
                    unsigned lo = 0;
                    if (!Hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                PutUtf8(s, cp);
                break;
            }
            default: return Fail("bad escape");
            }
        }
    }
    bool Value(Json& v, int depth) {
        if (depth > 64) return Fail("nesting too deep");
        Ws();
        if (i >= t.size()) return Fail("unexpected end");
        const char c = t[i];
        if (c == '{') {
            v.type = Json::Obj;
            i++;
            Ws();
            if (i < t.size() && t[i] == '}') { i++; return true; }
            for (;;) {
                Ws();
                if (i >= t.size() || t[i] != '"') return Fail("expected key");
                std::string key;
                if (!String(key)) return false;
                Ws();
                if (i >= t.size() || t[i] != ':') return Fail("expected ':'");
                i++;
                v.o.emplace_back(std::move(key), Json{});
                if (!Value(v.o.back().second, depth + 1)) return false;
                Ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == '}') { i++; return true; }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Json::Arr;
            i++;
            Ws();
            if (i < t.size() && t[i] == ']') { i++; return true; }
            for (;;) {
                v.a.emplace_back();
                if (!Value(v.a.back(), depth + 1)) return false;
                Ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == ']') { i++; return true; }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.type = Json::Str;
            return String(v.s);
        }
        if (t.substr(i, 4) == "true") { v.type = Json::Bool; v.b = true; i += 4; return true; }
        if (t.substr(i, 5) == "false") { v.type = Json::Bool; v.b = false; i += 5; return true; }
        if (t.substr(i, 4) == "null") { v.type = Json::Null; i += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t j = i;
            while (j < t.size() && (t[j] == '-' || t[j] == '+' || t[j] == '.' || t[j] == 'e' || t[j] == 'E' ||
                                    (t[j] >= '0' && t[j] <= '9')))
                j++;
            const std::string num(t.substr(i, j - i));
            char* end = nullptr;
            v.type = Json::Num;
            v.n = std::strtod(num.c_str(), &end);
            if (end != num.c_str() + num.size()) return Fail("bad number");
            i = j;
            return true;
        }
        return Fail("unexpected character");
    }
};

}  // namespace

bool ParseJson(std::string_view text, Json& out, std::string& err) {
    Parser p;
    p.t = text;
    out = Json{};
    if (!p.Value(out, 0)) {
        err = p.err;
        return false;
    }
    p.Ws();
    if (p.i != text.size()) {
        err = "trailing text at byte " + std::to_string(p.i);
        return false;
    }
    return true;
}

void AppendJsonString(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void AppendJsonNumber(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += '0';
        return;
    }
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        out += std::to_string(static_cast<long long>(value));
        return;
    }
    // Floats in the IR are float; 6 significant digits keep the output short and stable.
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", value);
    out += buf;
}

}  // namespace hd08
