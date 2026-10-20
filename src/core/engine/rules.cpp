#include "core/engine/rules.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/rules/compiled.h"

namespace hdTrans::internal {

namespace {

// A record line of the program: its kind, index (L) or PRIO (G), file and text.
struct Record {
    char kind;
    int id;
    std::size_t file;
    std::string_view line;
};

struct Loaded {
    rules::Program program;
    bool ok = false;
    std::vector<std::size_t> localFile;     // by index
    std::map<int, std::size_t> globalFile;  // by PRIO
};

std::vector<Record> Records() {
    std::vector<Record> out;
    const auto lines = RuleLines();
    const auto files = RuleFiles();
    for (std::size_t f = 0; f < files.size(); f++) {
        for (std::size_t i = files[f].first; i < files[f].first + files[f].count && i < lines.size(); i++) {
            std::string_view line = lines[i];
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
            if (!line.starts_with("L ") && !line.starts_with("G ")) continue;
            long long id = 0;
            std::size_t k = 2;
            const bool negative = k < line.size() && line[k] == '-';
            if (negative) k++;
            for (; k < line.size() && line[k] >= '0' && line[k] <= '9' && id < (1LL << 31); k++) id = id * 10 + (line[k] - '0');
            out.push_back({line[0], static_cast<int>(negative ? -id : id), f, line});
        }
    }
    return out;
}

#ifndef HDTRANS_INTERPRET_RULES
// Gives each rule the compiled code made from its record line, when that line is unchanged.
// Global rules are matched in PRIO order, as they run.
void AttachCompiled(rules::Program& program, const std::vector<Record>& records) {
    const auto local = rules::compiled::LocalRules();
    const auto global = rules::compiled::GlobalRules();
    std::vector<const Record*> globals;
    for (const Record& r : records) {
        if (r.kind == 'G') {
            globals.push_back(&r);
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(r.id);
        if (index >= local.size() || index >= program.localRules.size() || local[index].code == nullptr) continue;
        if (local[index].hash != rules::compiled::RecordHash(r.line)) continue;
        if (program.localCode.size() <= index) program.localCode.resize(index + 1, nullptr);
        program.localCode[index] = local[index].code;
    }
    std::stable_sort(globals.begin(), globals.end(), [](const Record* a, const Record* b) { return a->id < b->id; });
    for (std::size_t i = 0; i < globals.size() && i < global.size() && i < program.globalRules.size(); i++) {
        if (global[i].code == nullptr || global[i].hash != rules::compiled::RecordHash(globals[i]->line)) continue;
        if (program.globalCode.size() <= i) program.globalCode.resize(i + 1, nullptr);
        program.globalCode[i] = global[i].code;
    }
}
#endif

const Loaded& Load() {
    static const Loaded loaded = [] {
        std::string text;
        for (std::string_view line : RuleLines()) text += line;
        Loaded l;
        rules::LoadError error{};
        l.ok = rules::Load(text, l.program, error);
        const std::vector<Record> records = Records();
        l.localFile.assign(l.program.localRules.size(), SIZE_MAX);
        for (const Record& r : records) {
            if (r.kind == 'L' && r.id >= 0 && static_cast<std::size_t>(r.id) < l.localFile.size()) l.localFile[static_cast<std::size_t>(r.id)] = r.file;
            if (r.kind == 'G') l.globalFile.emplace(r.id, r.file);
        }
#ifndef HDTRANS_INTERPRET_RULES
        if (l.ok) AttachCompiled(l.program, records);
#endif
        return l;
    }();
    return loaded;
}

}  // namespace

void AttachCompiled(rules::Program& program, std::string_view text) {
#ifndef HDTRANS_INTERPRET_RULES
    std::vector<Record> records;
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.starts_with("L ") && !line.starts_with("G ")) continue;
        long long id = 0;
        std::size_t k = 2;
        for (; k < line.size() && line[k] >= '0' && line[k] <= '9' && id < (1LL << 31); k++) id = id * 10 + (line[k] - '0');
        records.push_back({line[0], static_cast<int>(id), 0, line});
    }
    AttachCompiled(program, records);
#else
    (void)program;
    (void)text;
#endif
}

const rules::Program& RulesProgram() { return Load().program; }

bool RulesLoaded() { return Load().ok; }

std::string RuleSource(char kind, int id) {
    // A local rule lives in a dictionary row, not in a file; its number names it until a
    // row id replaces the number.
    if (kind == 'L') return "L" + std::to_string(id);
    const Loaded& l = Load();
    std::size_t file = SIZE_MAX;
    if (kind == 'G') {
        if (const auto it = l.globalFile.find(id); it != l.globalFile.end()) file = it->second;
    }
    if (file >= RuleFiles().size()) return {};
    return std::string(RuleFiles()[file].name) + ":" + kind + std::to_string(id);
}

}  // namespace hdTrans::internal
