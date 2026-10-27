#include "core/filter/ehnd.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <system_error>

#include "core/text/utf8.h"

namespace hdTrans::filter {

namespace {

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8Of(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::string Upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

bool ReadBytes(const std::filesystem::path& path, std::string& out) {
#ifdef _WIN32
    std::FILE* f = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    char buf[1 << 16];
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof buf, f);
        out.append(buf, n);
        if (n < sizeof buf) break;
    }
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

bool ValidEhndText(std::string_view bytes) {
    if (!bytes.starts_with("\xFF\xFE")) {
        for (std::size_t i = 0; i < bytes.size();) {
            const auto rune = text::DecodeRune(bytes.substr(i));
            if (rune.size == 1 && rune.cp == text::kReplacement) return false;
            i += rune.size;
        }
        return true;
    }
    if (bytes.size() % 2 != 0) return false;
    const auto unit = [&](std::size_t i) {
        return static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
    };
    for (std::size_t i = 2; i < bytes.size(); i += 2) {
        const auto cp = unit(i);
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 3 >= bytes.size() || unit(i + 2) < 0xDC00 || unit(i + 2) > 0xDFFF) return false;
            i += 2;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return false;
        }
    }
    return true;
}

// The width of s in a double-byte code page: 1 for ASCII and, with halfKana, for the
// half-width katakana of CP932, else 2.
std::size_t CodePageBytes(std::string_view s, bool halfKana) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        n += r.cp < 0x80 || (halfKana && r.cp >= 0xFF61 && r.cp <= 0xFF9F) ? 1 : 2;
        i += r.size;
    }
    return n;
}

}  // namespace

std::string EhndText(std::string_view bytes) {
    std::string text;
    if (bytes.starts_with("\xFF\xFE")) {
        std::u16string units;
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            units.push_back(static_cast<char16_t>(static_cast<unsigned char>(bytes[i]) |
                                                  static_cast<unsigned char>(bytes[i + 1]) << 8));
        }
        for (std::size_t i = 0; i < units.size(); i++) {
            char32_t cp = units[i];
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (units[++i] - 0xDC00);
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                cp = text::kReplacement;
            }
            text::AppendRune(text, cp);
        }
    } else {
        text = bytes.starts_with("\xEF\xBB\xBF") ? bytes.substr(3) : bytes;
    }
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        out += text[i];
    }
    return out;
}

bool EhndFiles(std::string_view dir, std::vector<EhndFile>& files) {
    struct Kind {
        const char* prefix;
        Source::Kind kind;
    };
    constexpr Kind kKinds[] = {{"PREFILTER", Source::Kind::PreFilter},
                               {"POSTFILTER", Source::Kind::PostFilter},
                               {"SKIPLAYER", Source::Kind::SkipLayer},
                               {"USERDICT", Source::Kind::UserDict}};
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(PathOf(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) names.push_back(Utf8Of(it->path().filename()));
    }
    if (ec) return false;
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return Upper(a) < Upper(b); });
    for (const Kind& k : kKinds) {
        for (const std::string& name : names) {
            const std::string upper = Upper(name);
            if (upper.starts_with(k.prefix) && upper.ends_with(".TXT")) {
                files.push_back({k.kind, name, Utf8Of(PathOf(dir) / PathOf(name))});
            }
        }
    }
    return true;
}

bool ReadEhndFolder(std::string_view dir, std::vector<Source>& sources, std::vector<Error>& errors) {
    std::vector<EhndFile> files;
    if (!EhndFiles(dir, files)) {
        errors.push_back({std::string(dir), 0, "the folder cannot be listed"});
        return false;
    }
    std::vector<Source> read;
    for (const EhndFile& f : files) {
        std::string bytes;
        if (!ReadBytes(PathOf(f.path), bytes)) {
            errors.push_back({f.name, 0, "the file cannot be read"});
            return false;
        }
        if (!ValidEhndText(bytes)) {
            errors.push_back({f.name, 0, "the file cannot be decoded"});
            return false;
        }
        read.push_back({f.kind, f.name, EhndText(bytes), false});
    }
    std::move(read.begin(), read.end(), std::back_inserter(sources));
    return true;
}

std::vector<UserRow> ParseUserDict(std::string_view text, std::string_view file, std::vector<Error>& errors) {
    std::vector<UserRow> rows;
    int number = 0;
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        number++;
        if (line.starts_with("//")) continue;
        if (line.ends_with('\r')) line.remove_suffix(1);
        // A field ends at a tab or before "//", which ends the line without
        // counting that field; the last field runs to the end of the line, a final tab
        // included. A line needs two counted fields.
        std::string_view fields[4];
        int t = 0;
        for (std::size_t p = 0, n = 0; p < line.size();) {
            const bool comment = line.substr(p).starts_with("//");
            if (line[p] == '\t' || comment || p + 1 == line.size()) {
                const std::string_view field = line.substr(p - n, p + 1 == line.size() && !comment ? n + 1 : n);
                if (t < 4) {
                    fields[t] = field;
                    if (comment) break;
                }
                p++, n = 0, t++;
            } else {
                p++, n++;
            }
        }
        if (t < 2) {
            if (!line.empty()) {
                errors.push_back({std::string(file), number, "a UserDict line needs two tab-separated fields",
                                  ErrorKind::IgnoredShortFields});
            }
            continue;
        }
        UserRow row;
        row.key = fields[0];
        row.target = fields[1];
        row.phrase = fields[2] == "0" || fields[2] == "2";
        row.attr = fields[3];
        const char* error = nullptr;
        if (row.key.empty()) error = "a UserDict line without a key";
        else if (CodePageBytes(row.key, true) > 30) error = "the key is longer than 30 bytes in CP932";
        else if (CodePageBytes(row.target, false) > 30) error = "the Korean is longer than 30 bytes in CP949";
        else if (CodePageBytes(row.attr, false) > 36) error = "the attribute is longer than 36 bytes in CP949";
        if (error) {
            errors.push_back({std::string(file), number, error});
            continue;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace hdTrans::filter
