#include "core/config/config.h"

#include <yaml.h>

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>

namespace hdTrans::internal {
namespace {

std::filesystem::path PathOf(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string Lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool Number(std::string text) {
    if (text == ".inf" || text == "-.inf" || text == "+.inf" || text == ".nan") return true;
    if (!text.empty() && text.front() == '+') text.erase(0, 1);
    double value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ptr == text.data() + text.size() && parsed.ptr != text.data()) return true;
    if (text.starts_with("0x") || text.starts_with("0o")) {
        unsigned long long integer = 0;
        const auto digits = std::from_chars(text.data() + 2, text.data() + text.size(), integer,
                                           text[1] == 'x' ? 16 : 8);
        return digits.ptr == text.data() + text.size() && digits.ptr != text.data() + 2;
    }
    return false;
}

enum class Kind { String, Bool, Other };

std::string Scalar(const yaml_event_t& event) {
    return {reinterpret_cast<const char*>(event.data.scalar.value), event.data.scalar.length};
}

Kind ScalarKind(const yaml_event_t& event) {
    const auto* tag = event.data.scalar.tag;
    if (tag && std::strcmp(reinterpret_cast<const char*>(tag), "!") != 0) {
        const std::string_view type(reinterpret_cast<const char*>(tag));
        if (type == YAML_STR_TAG) return Kind::String;
        if (type == YAML_BOOL_TAG) return Kind::Bool;
        return Kind::Other;
    }
    if (event.data.scalar.style != YAML_PLAIN_SCALAR_STYLE) return Kind::String;
    const std::string text = Lower(Scalar(event));
    if (text == "true" || text == "false") return Kind::Bool;
    if (text.empty() || text == "null" || text == "~" || Number(text)) return Kind::Other;
    return Kind::String;
}

class Reader {
public:
    explicit Reader(ConfigResult& result) : result_(result), ready_(yaml_parser_initialize(&parser_) != 0) {
        if (!ready_) result_.diagnostics.push_back({0, "cannot allocate YAML parser"});
    }
    ~Reader() {
        yaml_event_delete(&event_);
        if (ready_) yaml_parser_delete(&parser_);
    }
    bool Ready() const { return ready_; }
    void Input(std::FILE* file) { yaml_parser_set_input_file(&parser_, file); }
    bool Next() {
        yaml_event_delete(&event_);
        event_ = {};
        if (yaml_parser_parse(&parser_, &event_)) return true;
        result_.diagnostics.push_back({parser_.problem_mark.line + 1,
                                      parser_.problem ? parser_.problem : "cannot parse YAML"});
        return false;
    }
    yaml_event_type_t Type() const { return event_.type; }
    std::size_t Line() const { return event_.start_mark.line + 1; }
    void Error(std::string message) { result_.diagnostics.push_back({Line(), std::move(message)}); }
    bool Skip() {
        if (Type() != YAML_MAPPING_START_EVENT && Type() != YAML_SEQUENCE_START_EVENT) return true;
        std::size_t depth = 0;
        do {
            if (Type() == YAML_MAPPING_START_EVENT || Type() == YAML_SEQUENCE_START_EVENT) ++depth;
            else if (Type() == YAML_MAPPING_END_EVENT || Type() == YAML_SEQUENCE_END_EVENT) --depth;
            if (depth == 0) return true;
        } while (Next());
        return false;
    }
    bool Path(std::string& value, const std::filesystem::path& base) const {
        if (Type() != YAML_SCALAR_EVENT || ScalarKind(event_) != Kind::String) return false;
        value = Scalar(event_);
        if (value.find('\0') != std::string::npos) return false;
        if (!value.empty()) value = Utf8((base / PathOf(value)).lexically_normal());
        return true;
    }
    bool Entry(const std::string& key, const std::filesystem::path& base) {
        if (key == "dicts" || key == "userdict") {
            std::vector<std::string> paths;
            std::string path;
            bool valid = true;
            if (Type() == YAML_SEQUENCE_START_EVENT) {
                if (!Next()) return false;
                while (Type() != YAML_SEQUENCE_END_EVENT) {
                    if (Path(path, base)) paths.push_back(path);
                    else { Error(key + ": expected a path"); valid = false; }
                    if (!Skip() || !Next()) return false;
                }
            } else if (Path(path, base)) paths.push_back(path);
            else { Error(key + ": expected a path or list of paths"); valid = false; }
            if (valid) {
                if (key == "dicts") result_.settings.dicts = std::move(paths);
                else result_.settings.userdict = std::move(paths);
            }
        } else if (key == "ehnd" || key == "cache") {
            if (key == "ehnd" && Type() == YAML_SCALAR_EVENT && ScalarKind(event_) == Kind::Bool) {
                const std::string enabled = Lower(Scalar(event_));
                if (enabled != "true" && enabled != "false") {
                    Error("ehnd: expected a path or boolean (true or false)");
                    return true;
                }
                result_.settings.ehnd.reset();
                result_.settings.useEhnd = enabled == "true";
                return true;
            }
            std::string path;
            if (key == "cache" && Type() == YAML_SCALAR_EVENT && ScalarKind(event_) == Kind::String &&
                Lower(Scalar(event_)) == "off") path = "off";
            else if (!Path(path, base)) {
                Error(key + ": expected a path" + (key == "cache" ? " or off" : key == "ehnd" ? " or boolean" : ""));
                return Skip();
            }
            if (key == "ehnd") {
                result_.settings.ehnd = std::move(path);
                result_.settings.useEhnd = !result_.settings.ehnd->empty();
            }
            else result_.settings.cache = std::move(path);
        } else if (key == "speech") {
            if (Type() != YAML_SCALAR_EVENT || ScalarKind(event_) != Kind::String ||
                !SpeechOf(Lower(Scalar(event_)), result_.settings.speech)) {
                Error("speech: expected soft or strict");
                return Skip();
            }
        } else if (key == "cg" || key == "misread" || key == "reference") {
            if (Type() != YAML_SCALAR_EVENT || ScalarKind(event_) != Kind::Bool ||
                (Lower(Scalar(event_)) != "true" && Lower(Scalar(event_)) != "false")) {
                Error(key + ": expected a boolean (true or false)");
                return Skip();
            }
            const bool value = Lower(Scalar(event_)) == "true";
            if (key == "cg") result_.settings.cg = value;
            else if (key == "misread") result_.settings.misread = value;
            else result_.settings.reference = value;
        } else {
            Error("unknown key: " + key);
        }
        return Skip();
    }
    void Read(const std::filesystem::path& base) {
        if (!Next() || !Next() || Type() == YAML_STREAM_END_EVENT || !Next()) return;
        if (Type() == YAML_MAPPING_START_EVENT) {
            if (!Next()) return;
            while (Type() != YAML_MAPPING_END_EVENT) {
                if (Type() != YAML_SCALAR_EVENT || ScalarKind(event_) != Kind::String) {
                    Error("expected a string key");
                    if (!Skip() || !Next() || !Skip() || !Next()) return;
                    continue;
                }
                const std::string key = Scalar(event_);
                const std::size_t keyLine = Line();
                if (!Next()) return;
                const std::size_t before = result_.diagnostics.size();
                if (!Entry(key, base)) return;
                if (result_.diagnostics.size() > before && result_.diagnostics[before].message.starts_with("unknown key:"))
                    result_.diagnostics[before].line = keyLine;
                if (!Next()) return;
            }
        } else if (Type() != YAML_SCALAR_EVENT || !Scalar(event_).empty()) {
            Error("expected a mapping");
            if (!Skip()) return;
        }
        if (!Next() || !Next()) return;
        if (Type() != YAML_STREAM_END_EVENT) Error("expected a single YAML document");
    }

private:
    ConfigResult& result_;
    yaml_parser_t parser_{};
    yaml_event_t event_{};
    bool ready_;
};

}  // namespace

bool SpeechOf(std::string_view name, Speech& out) {
    constexpr std::pair<std::string_view, Speech> kLevels[] = {{"soft", Speech::Soft}, {"strict", Speech::Strict}};
    for (const auto& [n, level] : kLevels) {
        if (n != name) continue;
        out = level;
        return true;
    }
    return false;
}

ConfigResult LoadConfig(std::string_view path) {
    ConfigResult result;
    const auto filePath = PathOf(path);
#ifdef _WIN32
    std::FILE* file = _wfopen(filePath.c_str(), L"rb");
#else
    std::FILE* file = std::fopen(filePath.c_str(), "rb");
#endif
    if (!file) {
        if (errno != ENOENT) result.diagnostics.push_back({0, "cannot open config file"});
        return result;
    }
    result.found = true;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(filePath, error);
    if (error) result.diagnostics.push_back({0, "cannot resolve config file path"});
    else {
        Reader reader(result);
        if (reader.Ready()) {
            reader.Input(file);
            reader.Read(absolute.parent_path());
        }
    }
    std::fclose(file);
    return result;
}

std::string ConfigErrors(const ConfigResult& result, std::string_view path) {
    std::string errors;
    for (const auto& diagnostic : result.diagnostics) {
        if (!errors.empty()) errors += '\n';
        errors += std::string(path) + ":" + std::to_string(diagnostic.line) + ": " + diagnostic.message;
    }
    return errors;
}

}  // namespace hdTrans::internal
