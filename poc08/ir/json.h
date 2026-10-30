#pragma once
// Minimal JSON value, parser and string escaping for the IR interchange files.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hd08 {

struct Json {
    enum Type : unsigned char { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json* Get(std::string_view key) const;
};

// Parses one JSON text; false with a message on error.
bool ParseJson(std::string_view text, Json& out, std::string& err);
// Appends text as a JSON string literal.
void AppendJsonString(std::string& out, std::string_view text);
// Appends a number in its shortest round-trip form.
void AppendJsonNumber(std::string& out, double value);

}  // namespace hd08
