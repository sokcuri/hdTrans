#include "gui/user_dicts.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "core/dict/codes.h"
#include "core/userdict/userdict.h"
#include "gui/text_file.h"

namespace hdTrans::gui {
namespace fs = std::filesystem;
namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

fs::path PathOf(std::string_view utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8Of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string Lower(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

bool DictName(const std::string& lower) {
    return lower.starts_with("userdict") && (lower.ends_with(".txt") || lower.ends_with(".tsv"));
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

std::string Field(std::string_view text) {
    std::string out(Trim(text));
    for (char& ch : out) {
        if (ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
    }
    return out;
}

// The fields before a field starting with //, and the rest of the line from it.
std::vector<std::string_view> Fields(std::string_view line, std::string_view* tail) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        const std::string_view field = line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start);
        if (Trim(field).starts_with("//")) {
            if (tail) *tail = line.substr(start);
            break;
        }
        fields.push_back(field);
        if (tab == std::string_view::npos) break;
        start = tab + 1;
    }
    return fields;
}

bool ColumnText(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        const std::string_view trimmed = Trim(line);
        if (trimmed.empty() || trimmed.starts_with('#')) continue;
        return line.substr(0, line.find('\t')) == "key";
    }
    return false;
}

}  // namespace

fs::path OffPath(const fs::path& path) {
    fs::path off = path;
    off += ".off";
    return off;
}

std::vector<UserDict> ListUserDicts(const std::vector<std::string>& sources) {
    std::vector<UserDict> dicts;
    for (const std::string& source : sources) {
        if (source.empty()) continue;
        const fs::path path = PathOf(source);
        std::error_code error;
        if (!fs::is_directory(path, error)) {
            if (fs::exists(path, error)) dicts.push_back({path, true});
            else if (fs::exists(OffPath(path), error)) dicts.push_back({path, false});
            continue;
        }
        std::vector<UserDict> found;
        for (fs::directory_iterator entries(path, error), end; !error && entries != end; entries.increment(error)) {
            std::error_code kindError;
            if (!entries->is_regular_file(kindError)) continue;
            std::string name = Utf8Of(entries->path().filename());
            const bool off = Lower(name).ends_with(".off");
            if (off) name.resize(name.size() - 4);
            if (DictName(Lower(name))) found.push_back({path / PathOf(name), !off});
        }
        std::sort(found.begin(), found.end(), [](const UserDict& left, const UserDict& right) {
            const std::string a = Utf8Of(left.path.filename()), b = Utf8Of(right.path.filename());
            const std::string lowerA = Lower(a), lowerB = Lower(b);
            if (lowerA != lowerB) return lowerA < lowerB;
            return a != b ? a < b : left.on > right.on;
        });
        for (UserDict& dict : found) {
            if (dicts.empty() || dicts.back().path != dict.path) dicts.push_back(std::move(dict));
        }
    }
    return dicts;
}

bool SetUserDictOn(UserDict& dict, bool on) {
    if (dict.on == on) return true;
    const fs::path from = on ? OffPath(dict.path) : dict.path;
    const fs::path to = on ? dict.path : OffPath(dict.path);
    std::error_code error;
    if (fs::exists(to, error) || error) return false;
    fs::rename(from, to, error);
    if (error) return false;
    dict.on = on;
    return true;
}

fs::path UserDictFolder(const std::vector<std::string>& sources) {
    for (const std::string& source : sources) {
        if (source.empty()) continue;
        const fs::path path = PathOf(source);
        std::error_code error;
        if (fs::is_directory(path, error)) return path;
        const std::string lower = Lower(source);
        if (!fs::exists(path, error) && !error && !lower.ends_with(".txt") && !lower.ends_with(".tsv")) return path;
    }
    return {};
}

fs::path UserDictPath(const fs::path& folder, std::string_view name) {
    std::string clean(Trim(name));
    for (char& ch : clean) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || std::string_view("\\/:*?\"<>|").find(ch) != std::string_view::npos) ch = '_';
    }
    while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) clean.pop_back();
    if (clean.empty()) return {};
    const std::string lower = Lower(clean);
    if (!lower.starts_with("userdict")) clean = "userdict-" + clean;
    if (!lower.ends_with(".txt") && !lower.ends_with(".tsv")) clean += ".txt";
    return folder / PathOf(clean);
}

bool ReadDictText(const fs::path& path, DictText& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) return false;
    std::string utf8;
    TextEncoding encoding{};
    if (!DecodeText(bytes, utf8, encoding)) return false;
    DictText out;
    out.bom = encoding != TextEncoding::Utf8;
    out.crlf = bytes.find("\r\n") != std::string::npos;
    std::size_t start = 0;
    while (start < utf8.size()) {
        const std::size_t newline = utf8.find('\n', start);
        out.lines.push_back(utf8.substr(start, newline == std::string::npos ? std::string::npos : newline - start));
        if (newline == std::string::npos) break;
        start = newline + 1;
    }
    out.column = ColumnText(out.lines);
    text = std::move(out);
    return true;
}

bool WriteDictText(const fs::path& path, const DictText& text) {
    std::string bytes = text.bom ? std::string(kUtf8Bom) : std::string();
    for (const std::string& line : text.lines) bytes += line + (text.crlf ? "\r\n" : "\n");
    fs::path temporary = path;
    temporary += ".hdtrans.tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file) return false;
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (!error) return true;
    fs::remove(temporary, error);
    return false;
}

bool EntryLine(std::string_view line) {
    const std::string_view trimmed = Trim(line);
    return !trimmed.empty() && !trimmed.starts_with('#') && !trimmed.starts_with("//");
}

DictRow ParseRow(std::string_view line) {
    const std::vector<std::string_view> fields = Fields(line, nullptr);
    DictRow row;
    std::string* targets[] = {&row.key, &row.ko, &row.pos, &row.options};
    for (std::size_t index = 0; index < fields.size() && index < 4; ++index) *targets[index] = std::string(Trim(fields[index]));
    return row;
}

std::string RowLine(const DictRow& row, std::string_view old) {
    std::string_view tail;
    Fields(old, &tail);
    std::vector<std::string> fields{Field(row.key), Field(row.ko), Field(row.pos), Field(row.options)};
    if (fields[2].empty() && !fields[3].empty()) fields[2] = "n";
    while (fields.size() > 2 && fields.back().empty()) fields.pop_back();
    std::string line;
    for (std::size_t index = 0; index < fields.size(); ++index) line += (index ? "\t" : "") + fields[index];
    if (!tail.empty()) line += "\t" + std::string(tail);
    return line;
}

fs::path ImportUserDict(const fs::path& source, const fs::path& folder) {
    std::string utf8;
    TextEncoding encoding{};
    if (folder.empty() || !ReadTextFile(source, utf8, encoding)) return {};
    std::error_code error;
    fs::create_directories(folder, error);
    if (error) return {};
    const fs::path name = UserDictPath(folder, Utf8Of(source.filename()));
    if (name.empty()) return {};
    fs::path target = name;
    for (int index = 2; fs::exists(target, error) || fs::exists(OffPath(target), error); ++index) {
        if (index > 999) return {};
        target = name.parent_path() / name.stem();
        target += "-" + std::to_string(index);
        target += name.extension();
    }
    return WriteTextFile(target, utf8) ? target : fs::path();
}

RowProblem CheckRow(const DictRow& row, const dict::Dict* base) {
    dict::CodeTable codes;
    userdict::Families families;
    userdict::Context ctx{&codes, &families, base};
    const auto compiled = userdict::Parse(RowLine(row), 0, ctx);
    if (compiled.errors.empty()) return {};
    const std::string& error = compiled.errors.front().message;
    if (error == "empty JP") return {RowField::Key, "일본어가 비어 있습니다"};
    if (error == "empty KR") return {RowField::Ko, "한국어가 비어 있습니다"};
    if (error.starts_with("unknown POS ")) return {RowField::Pos, "알 수 없는 품사입니다: " + row.pos};
    if (error.starts_with("option ") || error.starts_with("neg:")) return {RowField::Options, "옵션 오류: " + error};
    return {RowField::Pos, "품사와 맞지 않습니다: " + error};
}

}  // namespace hdTrans::gui
