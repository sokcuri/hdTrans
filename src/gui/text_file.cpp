#include "gui/text_file.h"

#include <climits>
#include <cstdint>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace hdTrans::gui {
namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::string_view kUtf16LeBom = "\xFF\xFE";

unsigned char Byte(char value) {
    return static_cast<unsigned char>(value);
}

bool ValidUtf8(std::string_view bytes) {
    for (size_t i = 0; i < bytes.size();) {
        const auto first = Byte(bytes[i++]);
        if (first == 0) return false;
        if (first < 0x80) continue;
        unsigned int remaining;
        uint32_t cp;
        uint32_t minimum;
        if (first >= 0xC2 && first <= 0xDF) {
            remaining = 1;
            cp = first & 0x1FU;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            remaining = 2;
            cp = first & 0x0FU;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            remaining = 3;
            cp = first & 0x07U;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (bytes.size() - i < remaining) return false;
        while (remaining-- != 0) {
            const auto next = Byte(bytes[i++]);
            if ((next & 0xC0U) != 0x80U) return false;
            cp = (cp << 6) | (next & 0x3FU);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    }
    return true;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

bool DecodeUtf16Le(std::string_view bytes, std::string& out) {
    if (bytes.size() % 2 != 0) return false;
    auto unit = [&](size_t i) -> uint32_t {
        return Byte(bytes[i]) | (static_cast<uint32_t>(Byte(bytes[i + 1])) << 8);
    };
    for (size_t i = 0; i < bytes.size(); i += 2) {
        uint32_t cp = unit(i);
        if (cp == 0 || (cp >= 0xDC00 && cp <= 0xDFFF)) return false;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            i += 2;
            if (i == bytes.size()) return false;
            const auto low = unit(i);
            if (low < 0xDC00 || low > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
        AppendUtf8(out, cp);
    }
    return true;
}

bool ShiftJisLead(unsigned char value) {
    return (value >= 0x81 && value <= 0x9F) || (value >= 0xE0 && value <= 0xFC);
}

bool ValidShiftJis(std::string_view bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        const auto first = Byte(bytes[i]);
        if (ShiftJisLead(first)) {
            if (++i == bytes.size()) return false;
            const auto trail = Byte(bytes[i]);
            if (trail < 0x40 || trail == 0x7F || trail > 0xFC) return false;
        } else if (first == 0 || (first >= 0x80 && (first < 0xA1 || first > 0xDF))) {
            return false;
        }
    }
    return true;
}

bool DecodeShiftJis(std::string_view bytes, std::string& out) {
    if (!ValidShiftJis(bytes)) return false;
#ifdef _WIN32
    while (!bytes.empty()) {
        size_t count = 0;
        while (count < bytes.size() && count < 32768) {
            count += ShiftJisLead(Byte(bytes[count])) ? 2 : 1;
        }
        const auto length = static_cast<int>(count);
        const int wideLength = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes.data(), length, nullptr, 0);
        if (wideLength == 0) return false;
        std::wstring wide(static_cast<size_t>(wideLength), L'\0');
        if (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes.data(), length, wide.data(), wideLength) != wideLength) {
            return false;
        }
        const int utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideLength,
                                                 nullptr, 0, nullptr, nullptr);
        if (utf8Length == 0) return false;
        std::string converted(static_cast<size_t>(utf8Length), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideLength,
                                converted.data(), utf8Length, nullptr, nullptr) != utf8Length) {
            return false;
        }
        out += converted;
        bytes.remove_prefix(count);
    }
    return true;
#else
    const iconv_t converter = iconv_open("UTF-8", "CP932");
    if (converter == reinterpret_cast<iconv_t>(-1)) return false;
    char* input = const_cast<char*>(bytes.data());
    size_t remaining = bytes.size();
    bool ok = true;
    while (remaining != 0) {
        char buffer[4096];
        char* output = buffer;
        size_t available = sizeof(buffer);
        const auto result = iconv(converter, &input, &remaining, &output, &available);
        const int error = errno;
        out.append(buffer, sizeof(buffer) - available);
        if (result == static_cast<size_t>(-1)) {
            if (error == E2BIG) continue;
            ok = false;
            break;
        }
        if (result != 0) ok = false;
        break;
    }
    if (iconv_close(converter) != 0) ok = false;
    return ok;
#endif
}

void NormalizeNewlines(std::string& text) {
    size_t output = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            text[output++] = '\n';
        } else {
            text[output++] = text[i];
        }
    }
    text.resize(output);
}

}  // namespace

bool DecodeText(std::string_view bytes, std::string& utf8, TextEncoding& encoding) {
    std::string decoded;
    TextEncoding detected;
    if (bytes.starts_with(kUtf8Bom)) {
        bytes.remove_prefix(kUtf8Bom.size());
        if (!ValidUtf8(bytes)) return false;
        decoded.assign(bytes);
        detected = TextEncoding::Utf8Bom;
    } else if (bytes.starts_with(kUtf16LeBom)) {
        bytes.remove_prefix(kUtf16LeBom.size());
        if (!DecodeUtf16Le(bytes, decoded)) return false;
        detected = TextEncoding::Utf16Le;
    } else if (bytes.starts_with("\xFE\xFF")) {
        return false;
    } else if (ValidUtf8(bytes)) {
        decoded.assign(bytes);
        detected = TextEncoding::Utf8;
    } else {
        if (!DecodeShiftJis(bytes, decoded) || !ValidUtf8(decoded)) return false;
        detected = TextEncoding::ShiftJis;
    }
    NormalizeNewlines(decoded);
    utf8.swap(decoded);
    encoding = detected;
    return true;
}

bool ReadTextFile(const std::filesystem::path& path, std::string& utf8, TextEncoding& encoding) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::string bytes;
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() != 0) {
        bytes.append(buffer, static_cast<size_t>(file.gcount()));
    }
    if (file.bad() || !file.eof()) return false;
    return DecodeText(bytes, utf8, encoding);
}

bool WriteTextFile(const std::filesystem::path& path, std::string_view utf8) {
    if (!ValidUtf8(utf8)) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(kUtf8Bom.data(), static_cast<std::streamsize>(kUtf8Bom.size()));
    while (!utf8.empty() && file) {
        const auto count = utf8.size() < 8192 ? utf8.size() : size_t{8192};
        file.write(utf8.data(), static_cast<std::streamsize>(count));
        utf8.remove_prefix(count);
    }
    file.close();
    return static_cast<bool>(file);
}

std::filesystem::path TranslatedPath(const std::filesystem::path& input, const std::filesystem::path& folder) {
    std::filesystem::path name = input.stem();
    name += std::filesystem::path(u8"_번역.txt");
    return (folder.empty() ? input.parent_path() : folder) / name;
}

void AppendFiles(std::vector<std::filesystem::path>& list, const std::vector<std::filesystem::path>& added) {
    auto same = [](const std::filesystem::path& left, const std::filesystem::path& right) {
        const auto a = left.lexically_normal().native();
        const auto b = right.lexically_normal().native();
#ifdef _WIN32
        return a.size() <= INT_MAX && b.size() <= INT_MAX &&
               CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
#else
        return a == b;
#endif
    };
    for (const auto& path : added) {
        bool found = false;
        for (const auto& old : list) found = found || same(old, path);
        if (!found) list.push_back(path);
    }
}

}  // namespace hdTrans::gui
