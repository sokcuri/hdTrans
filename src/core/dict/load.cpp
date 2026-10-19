#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "core/dict/builtin.h"
#include "core/dict/integrity.h"
#include "core/dict/layer.h"
#include "core/dict/mapped_file.h"
#include "core/dict/names.h"
#include "core/dict/snapshot.h"
#include "core/dict/snapshot_file.h"
#include "core/text/hangul.h"
#include "core/text/utf8.h"
#include "core/userdict/userdict.h"

namespace hdTrans::dict {

namespace {

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8Of(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// With prefix, keeps every chunk read so a plain file can continue on the same handle.
bool ReadColumnHeaderFromFile(std::FILE* f, bool& column, std::string* prefix) {
    column = false;
    char buf[4096];
    bool first = true, comment = false, carriage = false;
    std::size_t matched = 0;
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof buf, f);
        if (std::ferror(f)) return false;
        if (prefix) prefix->append(buf, n);
        std::string_view bytes(buf, n);
        if (first && bytes.starts_with("\xEF\xBB\xBF")) bytes.remove_prefix(3);
        first = false;
        for (const char c : bytes) {
            if (c == '\n') {
                if (!comment && matched > 0) {
                    column = matched == 3;
                    return true;
                }
                comment = carriage = false;
                matched = 0;
            } else if (comment) {
                continue;
            } else if (c == '\r') {
                carriage = true;
            } else if (carriage) {
                return true;
            } else if (matched == 0 && c == '#') {
                comment = true;
            } else if (c == '\t') {
                column = matched == 3;
                return true;
            } else if (matched == 3 || c != std::string_view("key")[matched]) {
                return true;
            } else {
                matched++;
            }
        }
        if (n < sizeof buf) {
            column = !comment && matched == 3;
            return true;
        }
    }
}

// Appends the bytes of path to out. With column, a column file is read only through
// the line that makes it one.
bool ReadFile(const std::filesystem::path& path, std::string& out, bool* column = nullptr) {
    std::FILE* f = internal::OpenShared(path);
    if (!f) return false;
    if (column) {
        const bool ok = ReadColumnHeaderFromFile(f, *column, &out);
        if (!ok || *column) {
            std::fclose(f);
            return ok;
        }
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (!ec) out.reserve(static_cast<std::size_t>(size));
    std::vector<char> buf(1 << 16);
    bool ok = true;
    for (;;) {
        const std::size_t n = std::fread(buf.data(), 1, buf.size(), f);
        out.append(buf.data(), n);
        if (n < buf.size()) {
            ok = std::ferror(f) == 0;
            break;
        }
    }
    std::fclose(f);
    return ok;
}

// Returns whether the physical line decides the file's column/plain membership.
bool ColumnLine(std::string_view line, bool first, bool& column) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    if (first && line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
    if (line.empty() || line.starts_with("#")) return false;
    column = line.substr(0, line.find('\t')) == "key";
    return true;
}

// Whether text starts, after blank and "#" lines, with a header line whose first
// column is key.
bool ColumnFile(std::string_view text) {
    bool column = false;
    for (std::size_t pos = 0, n = 0; pos < text.size(); n++) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        if (ColumnLine(line, n == 0, column)) return column;
    }
    return false;
}

// Whether f is a column file.
bool Column(const File& f) { return f.head || ColumnFile(f.text); }

// The number of rows the column file f can add (one per line, as ReadColumnFile
// reserves); 0 when it cannot be read.
std::size_t RowBound(const File& f) {
    if (!f.head) return static_cast<std::size_t>(std::count(f.text.begin(), f.text.end(), '\n')) + 1;
    std::FILE* in = internal::OpenShared(PathOf(f.name));
    if (!in) return 0;
    std::vector<char> buf(1 << 16);
    std::size_t lines = 1;
    for (;;) {
        const std::size_t n = std::fread(buf.data(), 1, buf.size(), in);
        lines += static_cast<std::size_t>(std::count(buf.data(), buf.data() + n, '\n'));
        if (n < buf.size()) break;
    }
    std::fclose(in);
    return lines;
}

void CloseCodes(CodeTable& codes, std::size_t first) {
    for (int32_t c = static_cast<int32_t>(first); c < static_cast<int32_t>(codes.Size()); c++) {
        codes.SetWithXXX(c);
        codes.SetAsPlain(c);
    }
}

bool ReadFolderFiles(const std::vector<std::string>& folders, bool plainOnly, std::vector<File>& files,
                     std::vector<LoadError>& errors) {
    for (std::size_t f = 0; f < folders.size(); f++) {
        if (folders[f].find('\0') != std::string::npos || !internal::ValidUtf8(folders[f])) {
            errors.push_back({folders[f], LoadErrorKind::Folder, 0, TsvErrorKind::Read, {}});
            return false;
        }
        const std::filesystem::path dir = PathOf(folders[f]);
        std::vector<std::string> names;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = Utf8Of(it->path().filename());
            std::error_code fileEc;
            if (name.ends_with(".tsv") && it->is_regular_file(fileEc)) names.push_back(name);
        }
        if (ec) {
            errors.push_back({folders[f], LoadErrorKind::Folder, 0, TsvErrorKind::Read, {}});
            return false;
        }
        std::sort(names.begin(), names.end());
        for (const std::string& name : names) {
            File file;
            file.folder = static_cast<int>(f);
            file.name = Utf8Of(dir / PathOf(name));
            if ((plainOnly && !internal::ValidUtf8(file.name)) || !ReadFile(dir / PathOf(name), file.text, &file.head)) {
                errors.push_back({file.name, LoadErrorKind::Read, 0, TsvErrorKind::Read, {}});
                return false;
            }
            if (!plainOnly || !file.head) files.push_back(std::move(file));
        }
    }
    return true;
}

// Reads the user dictionary file at path after the files of the folders; nothing for an
// empty path or a missing file. With plainOnly, a column file is left out. Of a column
// file, only the head is read (File::head).
bool ReadUserFile(std::string_view path, std::size_t folders, bool plainOnly, std::vector<File>& files,
                  std::vector<LoadError>& errors) {
    if (path.empty()) return true;
    File file;
    file.folder = static_cast<int>(folders);
    file.name = std::string(path);
    file.user = true;
    if (path.find('\0') != std::string_view::npos || !internal::ValidUtf8(path)) {
        errors.push_back({file.name, LoadErrorKind::Read, 0, TsvErrorKind::Read, {}});
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(PathOf(path), ec) && !ec) return true;
    if (!ReadFile(PathOf(path), file.text, &file.head)) {
        errors.push_back({file.name, LoadErrorKind::Read, 0, TsvErrorKind::Read, {}});
        return false;
    }
    if (!plainOnly || !file.head) files.push_back(std::move(file));
    return true;
}

// Interns the codes of the senses of a load, remembering the codes of each pos value.
class CodeReader {
public:
    explicit CodeReader(CodeTable& codes) : codes_(codes) {}

    // The number of the codes of s; false for an unknown pos.
    bool Code(const Sense& s, int32_t& code) {
        auto it = pos_.find(s.pos);
        if (it == pos_.end()) {
            it = pos_.emplace(std::string(s.pos), PosInfo()).first;
            PosInfo& info = it->second;
            info.known = PosCodes(it->first, info.part, info.sem, info.gram);
        }
        const PosInfo& p = it->second;
        if (!p.known) return false;
        std::string_view sem = s.sem.empty() ? p.sem : s.sem;
        const std::string_view gram = s.gram.empty() ? p.gram : s.gram;
        if (s.xxx && sem.find("XXX") == std::string_view::npos) {
            withXXX_.assign(sem);
            withXXX_ += "XXX";
            sem = withXXX_;
        }
        // Consecutive senses often have the same codes.
        if (sem.data() != withXXX_.data() && lastCode_ >= 0 && p.part == last_[0] && sem == last_[1] &&
            gram == last_[2] && s.dict == last_[3]) {
            code = lastCode_;
            return true;
        }
        code = codes_.Intern(p.part, sem, gram, s.dict);
        if (sem.data() == withXXX_.data()) {
            lastCode_ = -1;
        } else {
            last_[0] = p.part;
            last_[1] = sem;
            last_[2] = gram;
            last_[3] = s.dict;
            lastCode_ = code;
        }
        return true;
    }

private:
    struct PosInfo {
        bool known = false;
        std::string_view part, sem, gram;  // the pos value itself or the tag table
    };

    CodeTable& codes_;
    std::unordered_map<std::string, PosInfo, StringHash, std::equal_to<>> pos_;
    std::string withXXX_;
    std::string_view last_[4];  // the codes of lastCode_, views into the file
    int32_t lastCode_ = -1;
};

// Parses the column file f into layer, interning its codes in codes; adds the indexes
// of its rows with the flag replace to replacing. false on an error other than unknown
// pos: a file with malformed lines fails the load, and its unknown pos lines are not
// listed.
bool ReadColumnFile(const File& f, CodeTable& codes, internal::Layer& layer, std::vector<uint32_t>* replacing,
                    std::vector<LoadError>& errors) {
    layer.Rows().reserve(layer.Rows().size() + static_cast<std::size_t>(std::count(f.text.begin(), f.text.end(), '\n')) + 1);
    CodeReader reader(codes);
    TsvReader rows(f.text);
    SenseReader senses(rows);
    std::vector<LoadError> unknown;
    Sense s;
    while (senses.Next(s)) {
        int32_t code;
        if (!reader.Code(s, code)) {
            unknown.push_back({f.name, LoadErrorKind::UnknownPos, rows.Line(), TsvErrorKind::Read, {}});
            continue;
        }
        for (const std::string_view key : {s.key, s.reading}) {
            if (key.empty()) continue;
            if (s.replace && replacing) replacing->push_back(static_cast<uint32_t>(layer.Rows().size()));
            layer.Add(key, s.ko, code, s.prio, s.rule, s.id);
        }
    }
    if (senses.Errors().empty()) {
        errors.insert(errors.end(), unknown.begin(), unknown.end());
        return true;
    }
    for (const TsvError& e : senses.Errors()) {
        const LoadErrorKind kind = e.kind == TsvErrorKind::Read ? LoadErrorKind::Read : LoadErrorKind::Tsv;
        errors.push_back({f.name, kind, e.line, e.kind, {}});
    }
    return false;
}

// The builtin files that the text path reads itself; the other builtin files are
// plain dictionaries.
bool OwnBuiltin(std::string_view name) {
    return name == "sound.tsv" || name == "phrases.tsv" || name == "patterns.tsv" || name == "senses.tsv" ||
           name == "lines.tsv" || name == "cleanup.tsv" || name == "masked.tsv";
}

// Whether a line of phrases.tsv is an interjection in kana: an utterance of the sound
// lexicon, not a plain entry.
bool StandalonePhrase(std::string_view line) {
    std::vector<std::string_view> f;
    for (std::size_t pos = 0;;) {
        const std::size_t tab = line.find('\t', pos);
        f.push_back(line.substr(pos, tab == std::string_view::npos ? std::string_view::npos : tab - pos));
        if (tab == std::string_view::npos) break;
        pos = tab + 1;
    }
    if (f.size() < 3 || f[2] != "intj" || f[0].empty()) return false;
    const char32_t r = text::DecodeRune(f[0]).cp;
    return (r >= U'ぁ' && r <= U'ゖ') || (r >= U'ァ' && r <= U'ヶ') || r == U'ー' || r == U'～';
}

// The text of phrases.tsv without its standalone phrases, which keep their lines.
std::string BuiltinPhrases(bool extraOnly = false) {
    const std::string text = extraOnly ? ExtraBuiltinText("phrases.tsv") : BuiltinText("phrases.tsv");
    std::string out;
    out.reserve(text.size());
    for (std::size_t pos = 0;;) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view line = std::string_view(text).substr(pos, nl == std::string::npos ? nl : nl - pos);
        std::string_view bare = line;
        while (bare.ends_with('\r')) bare.remove_suffix(1);
        if (!StandalonePhrase(bare)) out += line;
        if (nl == std::string::npos) break;
        out += '\n';
        pos = nl + 1;
    }
    return out;
}

}  // namespace

bool internal::ReadLayer(std::string_view text, FileLayer& layer) {
    layer = FileLayer::Base;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    if (!text.starts_with("#!")) return true;
    text.remove_prefix(2);
    std::string_view line = text.substr(0, text.find('\n'));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    if (!line.starts_with("layer=")) return false;
    line.remove_prefix(6);
    constexpr std::string_view kNames[] = {"base", "words", "names", "user", "kana", "readings", "prefilter", "postfilter"};
    for (std::size_t i = 0; i < std::size(kNames); i++) {
        if (line == kNames[i]) {
            layer = static_cast<FileLayer>(i);
            return true;
        }
    }
    return false;
}

std::string_view internal::WithoutHeader(std::string_view text) {
    const std::string_view bare = text.starts_with("\xEF\xBB\xBF") ? text.substr(3) : text;
    if (!bare.starts_with("#!")) return text;
    const std::size_t nl = bare.find('\n');
    return nl == std::string_view::npos ? std::string_view() : bare.substr(nl);
}

bool internal::ReadColumnHeader(std::string_view utf8Path, bool& column) {
    column = false;
    if (utf8Path.find('\0') != std::string_view::npos || !internal::ValidUtf8(utf8Path)) return false;
    const auto path = PathOf(utf8Path);
    std::FILE* f = internal::OpenShared(path);
    if (!f) return false;
    const bool ok = ReadColumnHeaderFromFile(f, column, nullptr);
    std::fclose(f);
    return ok;
}

// Builds a Snapshot (Load).
struct SnapshotLoader {
    std::vector<File> files;
    std::vector<LoadError> errors;
    std::shared_ptr<Snapshot> snap{new Snapshot()};

    SnapshotLoader() = default;
    explicit SnapshotLoader(std::shared_ptr<Snapshot> existing) : snap(std::move(existing)) {}

    LoadResult Run();
    bool ReadLayers();
    bool ReadColumns(std::size_t i, internal::Layer& layer, std::vector<uint32_t>* replacing);
    bool BuildBase(const std::vector<std::size_t>& baseFiles);
    bool BuildNames(const std::vector<std::size_t>& nameFiles);
    LoadResult FinishPlain(const std::vector<std::size_t>& rest);
    bool CompilePlain(const std::vector<std::size_t>& rest);
    bool Compile(const std::string& name, std::string_view text, int file, userdict::Context& ctx,
                 std::vector<UserEntry>& out);
    void Respell(std::string_view key, std::string_view ko);
    std::string PersonSpelling(std::string_view ko, const std::vector<Record>& records) const;
};

void SnapshotLoader::Respell(std::string_view key, std::string_view ko) {
    Snapshot::Respelling sp;
    sp.ko = ko;
    sp.mode = Snapshot::Respelling::Mode::AllNouns;
    const std::size_t keyRunes = text::RuneCount(key);
    for (const Record& r : snap->base_.Records(key)) {
        if (!internal::PlaceSense(r.codes->sem)) continue;
        if (keyRunes <= 2 && text::SameSound(r.target, ko)) {
            sp.mode = Snapshot::Respelling::Mode::SameSound;
            break;
        }
        sp.mode = Snapshot::Respelling::Mode::AllPlaces;
    }
    snap->places_.insert_or_assign(std::string(key), std::move(sp));
}

bool SnapshotLoader::Compile(const std::string& name, std::string_view text, int file, userdict::Context& ctx,
                             std::vector<UserEntry>& out) {
    userdict::Compiled c = userdict::Parse(text, file, ctx);
    for (userdict::Error& e : c.errors) {
        errors.push_back({name, LoadErrorKind::Plain, e.line, TsvErrorKind::Read, std::move(e.message)});
    }
    std::move(c.entries.begin(), c.entries.end(), std::back_inserter(out));
    return c.errors.empty();
}

// Sets the layer of each file from its header.
bool SnapshotLoader::ReadLayers() {
    for (File& f : files) {
        const char* message = nullptr;
        if (f.user) {
            const bool column = Column(f);
            if (!internal::ReadLayer(f.text, f.layer) ||
                (f.layer != FileLayer::User && internal::WithoutHeader(f.text).size() != f.text.size())) {
                message = "the first line of the user dictionary is not #! layer=user";
            }
            f.layer = column ? FileLayer::User : FileLayer::Base;
        } else if (!internal::ReadLayer(f.text, f.layer)) {
            message = "the first line is not #! layer=base|words|names|user|kana|readings|prefilter|postfilter";
        } else if (f.layer != FileLayer::Base) {
            const bool plainLayer = f.layer == FileLayer::Kana || f.layer == FileLayer::Readings ||
                                    f.layer == FileLayer::PreFilter || f.layer == FileLayer::PostFilter;
            const bool column = Column(f);
            if (column && plainLayer) message = "a column file cannot be of the layer kana, readings, prefilter or postfilter";
            if (!column && !plainLayer) message = "a plain file cannot be of the layer words, names or user";
        }
        if (message) {
            errors.push_back({f.name, LoadErrorKind::Header, 1, TsvErrorKind::Read, message});
            return false;
        }
    }
    return true;
}

// Compiles the plain dictionaries of the builtin folder and of the files rest, the
// kana spellings and the sense table, in this order, which numbers the generated
// marker families.
bool SnapshotLoader::CompilePlain(const std::vector<std::size_t>& rest) {
    Snapshot& s = *snap;
    bool ok = true;
    userdict::Context builtin{&s.codes_, &s.families_, nullptr};
    std::vector<UserEntry> endings, phrases, entries, kana;
    for (const std::string_view name : BuiltinNames()) {
        if (OwnBuiltin(name)) continue;
        const std::string data = BuiltinText(name);
        if (!ColumnFile(data)) ok = Compile(std::string(name), data, -1, builtin, endings) && ok;
    }
    ok = Compile("phrases.tsv", BuiltinPhrases(), -1, builtin, phrases) && ok;
    ok = Compile("patterns.tsv", BuiltinText("patterns.tsv"), -1, builtin, phrases) && ok;
    userdict::Context ctx{&s.codes_, &s.families_, &s.base_};
    int file = 0;
    for (const std::size_t i : rest) {
        const std::string_view text = internal::WithoutHeader(files[i].text);
        if (files[i].layer == FileLayer::Readings || files[i].layer == FileLayer::PreFilter ||
            files[i].layer == FileLayer::PostFilter) {
            continue;
        }
        if (files[i].layer == FileLayer::Kana) {
            std::vector<UserEntry> k = userdict::KanaSpellings(text, s.base_, s.codes_);
            std::move(k.begin(), k.end(), std::back_inserter(kana));
            continue;
        }
        ok = Compile(files[i].name, text, file++, ctx, entries) && ok;
    }
    userdict::CompiledSenses senses = userdict::ParseSenses(BuiltinText("senses.tsv"), s.families_);
    for (userdict::Error& e : senses.errors) {
        errors.push_back({"senses.tsv", LoadErrorKind::Plain, e.line, TsvErrorKind::Read, std::move(e.message)});
    }
    if (!ok || !senses.errors.empty()) return false;
    s.senses_ = std::move(senses.table);

    std::vector<UserEntry> rawView, textView;
    rawView.reserve(endings.size() + entries.size());
    rawView.insert(rawView.end(), endings.begin(), endings.end());
    rawView.insert(rawView.end(), entries.begin(), entries.end());
    textView.reserve(kana.size() + endings.size() + phrases.size() + entries.size());
    for (std::vector<UserEntry>* part : {&kana, &endings, &phrases, &entries}) {
        std::move(part->begin(), part->end(), std::back_inserter(textView));
    }
    s.rawPlain_ = std::make_unique<const UserText>(std::move(rawView));
    s.textPlain_ = std::make_unique<const UserText>(std::move(textView));

    std::vector<UserEntry> extraEndings, extraPhrases;
    ok = Compile("endings.tsv", ExtraBuiltinText("endings.tsv"), -1, builtin, extraEndings) && ok;
    ok = Compile("phrases.tsv", BuiltinPhrases(true), -1, builtin, extraPhrases) && ok;
    ok = Compile("patterns.tsv", ExtraBuiltinText("patterns.tsv"), -1, builtin, extraPhrases) && ok;
    if (!ok) return false;
    rawView = s.rawPlain_->Entries();
    textView = s.textPlain_->Entries();
    rawView.insert(rawView.end(), extraEndings.begin(), extraEndings.end());
    textView.insert(textView.end(), extraEndings.begin(), extraEndings.end());
    textView.insert(textView.end(), extraPhrases.begin(), extraPhrases.end());
    s.rawExtraPlain_ = std::make_unique<const UserText>(std::move(rawView));
    s.textExtraPlain_ = std::make_unique<const UserText>(std::move(textView));
    return true;
}

// The first Korean of a person sense (QPN) among records that differs from ko in
// spelling only: the same sound and as many characters.
std::string SnapshotLoader::PersonSpelling(std::string_view ko, const std::vector<Record>& records) const {
    for (const Record& r : records) {
        if (r.codes->sem.HasPrefix("QPN") && r.target != ko && text::SameSound(r.target, ko) &&
            text::RuneCount(r.target) == text::RuneCount(ko)) {
            return std::string(r.target);
        }
    }
    return {};
}

// Parses the column file i into layer (ReadColumnFile), reading the rest of a file
// of which only the head was read, and frees its text. A read error replaces the
// errors so far.
bool SnapshotLoader::ReadColumns(std::size_t i, internal::Layer& layer, std::vector<uint32_t>* replacing) {
    File& f = files[i];
    if (f.head) {
        f.text.clear();
        f.head = false;
        if (!ReadFile(PathOf(f.name), f.text)) {
            errors = {{f.name, LoadErrorKind::Read, 0, TsvErrorKind::Read, {}}};
            return false;
        }
    }
    const bool ok = ReadColumnFile(f, snap->codes_, layer, replacing, errors);
    std::string().swap(f.text);
    return ok;
}

// Builds the base and user dictionaries from the column files baseFiles, in order.
bool SnapshotLoader::BuildBase(const std::vector<std::size_t>& baseFiles) {
    Snapshot& s = *snap;
    internal::Layer base(true), user(false);
    // Reserved ahead, the rows of a layer are not moved as its files are added.
    std::size_t baseRows = 0, userRows = 0;
    for (const std::size_t i : baseFiles) (files[i].layer == FileLayer::User ? userRows : baseRows) += RowBound(files[i]);
    base.Rows().reserve(baseRows);
    user.Rows().reserve(userRows);
    for (const std::size_t i : baseFiles) {
        if (files[i].layer == FileLayer::User) {
            const std::size_t start = user.Rows().size();
            if (!ReadColumns(i, user, nullptr)) return false;
            user.ByPrio(start);
            user.Shadow(start, nullptr, s.codes_);
            user.Compact();
        } else {
            const std::size_t start = base.Rows().size();
            std::vector<uint32_t> replacing;
            if (!ReadColumns(i, base, &replacing)) return false;
            base.Shadow(start, files[i].layer == FileLayer::Words ? &replacing : nullptr, s.codes_);
            base.Compact();
        }
    }
    if (s.hasDictionary_) {
        s.user_ = user.Build(s.codes_);
        s.base_ = base.Build(s.codes_);
    }
    return true;
}

// Builds the name layer from the name files nameFiles, in order, after the base.
bool SnapshotLoader::BuildNames(const std::vector<std::size_t>& nameFiles) {
    Snapshot& s = *snap;
    internal::Layer names(false);
    std::size_t bound = 0;
    for (const std::size_t i : nameFiles) bound += RowBound(files[i]);
    names.Rows().reserve(bound);
    for (const std::size_t i : nameFiles) {
        const std::size_t start = names.Rows().size();
        std::vector<uint32_t> replacing;
        if (!ReadColumns(i, names, &replacing)) return false;
        std::unordered_set<std::string_view> own;
        for (const uint32_t at : replacing) {
            const internal::Row& r = names.Rows()[at];
            own.insert(r.key);
            Respell(r.key, r.target);
        }
        auto& rows = names.Rows();
        for (std::size_t k = start; k < rows.size(); k++) {
            const internal::Row& r = rows[k];
            if (own.count(r.key)) continue;
            const Snapshot::Respelling* sp = s.Place(r.key);
            if (sp && !sp->ko.empty() && !s.codes_.At(r.code)->sem.HasPrefix("QPN")) rows[k].code = internal::Layer::kDeleted;
        }
        names.Compact();
        names.ByPrio(start);
        const auto dropped = internal::DropNameKeys(rows.data() + start, rows.size() - start, s.base_);
        if (dropped.punct + dropped.inflected + dropped.longKorean > 0) {
            s.warnings_.push_back(files[i].name + ": name senses dropped: " + std::to_string(dropped.punct) +
                                  " keys with 、 or 。, " + std::to_string(dropped.inflected) + " inflected keys, " +
                                  std::to_string(dropped.longKorean) + " long Korean");
        }
        names.Shadow(start, nullptr, s.codes_);
        names.Compact();
    }
    s.names_ = names.Build(s.codes_);
    return true;
}

LoadResult SnapshotLoader::Run() {
    if (!ReadLayers()) return {nullptr, std::move(errors)};
    std::vector<std::size_t> baseFiles, nameFiles, rest;
    for (std::size_t i = 0; i < files.size(); i++) {
        if (!Column(files[i])) rest.push_back(i);
        else if (files[i].layer == FileLayer::Names) nameFiles.push_back(i);
        else baseFiles.push_back(i);
    }
    Snapshot& s = *snap;
    s.hasDictionary_ = rest.size() < files.size();
    if (!BuildBase(baseFiles)) return {nullptr, std::move(errors)};
    if (!BuildNames(nameFiles)) return {nullptr, std::move(errors)};
    for (auto& [key, sp] : s.places_) {
        sp.person = PersonSpelling(sp.ko, s.base_.Records(key));
        if (sp.person.empty()) sp.person = PersonSpelling(sp.ko, s.names_.Records(key));
    }
    CloseCodes(s.codes_, 0);
    s.columnCodes_ = std::make_shared<const CodeTable>(std::move(s.codes_));
    if (!CodeTable::WithBase(s.columnCodes_, s.codes_)) return {nullptr, std::move(errors)};
    return FinishPlain(rest);
}

LoadResult SnapshotLoader::FinishPlain(const std::vector<std::size_t>& rest) {
    if (!CompilePlain(rest)) return {nullptr, std::move(errors)};
    Snapshot& s = *snap;
    CloseCodes(s.codes_, s.columnCodes_->Size());
    for (const std::size_t i : rest) s.plain_.push_back(std::move(files[i]));
    files.clear();
    return {std::move(snap), std::move(errors)};
}

LoadResult Load(std::vector<File> files) {
    SnapshotLoader loader;
    loader.files = std::move(files);
    return loader.Run();
}

LoadResult LoadFoldersTsv(const std::vector<std::string>& folders, std::string_view userFile) {
    return LoadFoldersTsv(folders, std::vector<std::string>{std::string(userFile)});
}

LoadResult LoadFoldersTsv(const std::vector<std::string>& folders, const std::vector<std::string>& userFiles) {
    std::vector<File> files;
    std::vector<LoadError> errors;
    if (!ReadFolderFiles(folders, false, files, errors)) return {nullptr, std::move(errors)};
    bool read = true;
    for (const std::string& userFile : userFiles) {
        read = ReadUserFile(userFile, folders.size(), false, files, errors) && read;
    }
    if (!read) return {nullptr, std::move(errors)};
    return Load(std::move(files));
}

LoadResult LoadPlainFolders(std::shared_ptr<Snapshot> snapshot, const std::vector<std::string>& folders,
                           std::vector<LoadError> errors, const std::vector<std::string>& userFiles) {
    std::vector<File> files;
    std::vector<LoadError> readErrors;
    if (!ReadFolderFiles(folders, true, files, readErrors)) return {nullptr, std::move(readErrors)};
    return LoadPlainFiles(std::move(snapshot), std::move(files), folders.size(), std::move(errors), userFiles);
}

LoadResult LoadPlainFiles(std::shared_ptr<Snapshot> snapshot, std::vector<File> files, std::size_t folders,
                         std::vector<LoadError> errors, const std::vector<std::string>& userFiles) {
    SnapshotLoader loader(std::move(snapshot));
    loader.files = std::move(files);
    std::vector<LoadError> readErrors;
    bool read = true;
    for (const std::string& userFile : userFiles) {
        read = ReadUserFile(userFile, folders, true, loader.files, readErrors) && read;
    }
    if (!read) return {nullptr, std::move(readErrors)};
    loader.errors = std::move(errors);
    if (!loader.ReadLayers()) return {nullptr, std::move(loader.errors)};
    std::vector<std::size_t> rest;
    rest.reserve(loader.files.size());
    for (std::size_t i = 0; i < loader.files.size(); i++) {
        if (Column(loader.files[i])) return {nullptr, {{loader.files[i].name, LoadErrorKind::Header, 1,
                                                       TsvErrorKind::Read, "expected a plain dictionary file"}}};
        rest.push_back(i);
    }
    return loader.FinishPlain(rest);
}

}  // namespace hdTrans::dict
