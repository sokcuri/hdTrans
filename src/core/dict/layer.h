#pragma once
// The senses of one dictionary while it is loaded, until Build makes the Dict.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/dict.h"

namespace hdTrans::dict::internal {

// A sense in load order. key and target point into the storage of the layer that
// added the row.
struct Row {
    std::string_view key;
    std::string_view target;
    int32_t code;  // index in the CodeTable; kDeleted for a row Compact drops
    int32_t prio;
    int32_t rule;
    int32_t id;
};

class Layer {
public:
    static constexpr int32_t kDeleted = -1;

    // prio: Build orders the records of a key by prio, higher first.
    explicit Layer(bool prio) : prio_(prio) {}

    // Appends a row, copying key and target. A byte of key that is not valid UTF-8 is
    // stored as U+FFFD, as lookups decode it.
    void Add(std::string_view key, std::string_view target, int32_t code, int32_t prio, int32_t rule, int32_t id);

    std::vector<Row>& Rows() { return rows_; }
    const std::vector<Row>& Rows() const { return rows_; }

    // Orders the rows from start by prio, higher first, keeping the order of equal
    // ones.
    void ByPrio(std::size_t start);
    // Marks deleted the rows of [0, end) whose key and class digit a row of later
    // (indexes in Rows; all rows from end when null) has. Compact drops them.
    void Shadow(std::size_t end, const std::vector<uint32_t>* later, const CodeTable& codes);
    // The rows [start, end) of one file, and with some the rows among them that shadow (indexes in Rows); all of them
    // shadow without.
    struct FileRows {
        std::size_t start = 0, end = 0;
        bool some = false;
        std::vector<uint32_t> shadows;
    };
    // What Shadow after each file in order marks: each row whose key and class digit a shadowing row of a later file
    // has, as that file read it. One pass from the last file, so it does not grow with files times rows.
    void ShadowFiles(const std::vector<FileRows>& files, const CodeTable& codes);
    void Compact();

    // Builds the dictionary of the rows. The records of a key are ordered by prio
    // when the layer orders by prio, then by row order.
    Dict Build(const CodeTable& codes);

private:
    void Sort();
    // A copy of s in chunks_.
    std::string_view Keep(std::string_view s);

    bool prio_;
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<char[]>> chunks_;  // the keys and targets of rows_
    std::size_t used_ = 0, room_ = 0;               // bytes used and held by chunks_.back()
};

}  // namespace hdTrans::dict::internal
