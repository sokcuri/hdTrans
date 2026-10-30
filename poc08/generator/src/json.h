#pragma once
// Minimal JSON value, reader and writer for the IR interchange (IR.md §7). No exceptions.
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hd8gen::json {

struct Value {
    enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    const Value* Get(const std::string& key) const;
    std::string Str(const std::string& key, const std::string& def = "") const;
    double Num(const std::string& key, double def = 0) const;
    bool Bool(const std::string& key, bool def = false) const;
    void Set(const std::string& key, Value v);
};

bool Parse(const std::string& text, Value& out, std::string& err);
std::string Write(const Value& v);
Value S(const std::string& s);
Value N(double d);
Value B(bool b);

}  // namespace hd8gen::json
