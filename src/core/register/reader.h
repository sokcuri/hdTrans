#pragma once
// The reader: what the word stage resolved for a line, read back into items of the
// translation (one per word, mark or particle), and the detectors of dialogue lines and caption
// documents.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/register/tables.h"

namespace hdTrans::word {
struct Chain;
}

namespace hdTrans::reg {

// What the word stage resolved per line of one call; the last pass over a line counts.
struct Capture {
    struct Item {
        int sentence = 0, token = 0;
        std::string surface, target, ending;  // source surface, Korean target, the selected ending's target
        // A verb: the conjugation branch and the source of its ending.
        bool verb = false;
        int kind = -1;
        std::string sourceEnding;
    };
    // The items of line l, in order; none for a line without items.
    std::vector<Item> Items(std::size_t l) const;
    // Adds an item to line l; lets line l's items go.
    void Add(std::size_t l, const Item& item);
    void Drop(std::size_t l);
    // The lines seen so far (a line past them was never reported).
    std::size_t Lines() const { return lines_.size(); }
    // When the call's long lines were cut into pieces: the line of each piece; empty otherwise.
    std::vector<int> pieceLine;
    // The lines whose items are kept (the dialogue lines); empty keeps every line.
    std::vector<bool> wanted;

    void Line(int piece);
    // Another pass over the current line (or piece) starts.
    void Pass();
    void Resolved(int sentence, const word::Chain& chain);

private:
    // A line's items packed: their strings in one text, each as an offset and a length.
    struct Packed {
        int32_t sentence = 0, token = 0;
        uint32_t at = 0;
        uint32_t surface = 0, target = 0, ending = 0, sourceEnding = 0;  // lengths, in this order from at
        int16_t kind = -1;
        bool verb = false;
    };
    struct LineItems {
        std::string text;
        std::vector<Packed> items;
    };
    LineItems& At(std::size_t l);
    std::vector<LineItems> lines_;
    int line_ = -1;
    std::size_t passStart_ = 0, passText_ = 0;  // where the items of the current piece start
};

// One item of a line of the translation.
struct PreItem {
    enum class Kind : uint8_t { N, P, V, Punct, Raw } kind = Kind::N;
    std::string t;                       // N, P, Raw: the text
    std::string p;                       // Punct: the mark's kind (comma, end, q …)
    std::string lemma, pos, cls, rest;  // V: the lemma, V A X C, the class, the ending's text after its row
    bool gap = false;                    // a mark of the source the word stage did not write as a word
};

// A line as the regeneration reads it.
struct PreLine {
    std::string ja, out;     // the source and the translation
    std::vector<PreItem> items;
    std::string speaker;     // the speaker tag of the line
    uint32_t scene = 0;      // blank lines cut scenes
    bool dialogue = false;
    bool imp = false;        // the source ends in a request or command (て, ろ, なさい, ください …)
    bool keigo = false;      // the source holds an honorific or humble form (reader.tsv keigo rows)
    bool command = false;    // a word of the source is in the imperative (命令形)
};

// Whether a line is speech by its shape: a quote opens it, after white space and an optional speaker tag.
bool DialogueLine(const Reader& r, std::string_view ja);
// Whether a document reads as captions: timing marks, speaker tags in parentheses or half-width caption
// marks on a fifth of its lines, or mostly short lines without quotes.
bool CaptionDocument(const Reader& r, const std::vector<std::string>& ja);

// The speaker tag of a line (【筧】「…」 → 筧), "" when it has none.
std::string LineSpeaker(const Reader& r, const std::string& ja);
// Line i of the capture as the regeneration reads it; a line that is no dialogue line gets no items.
PreLine ReadLine(const Data& d, std::size_t i, const std::string& ja, const std::string& out, const Capture& capture,
                 bool dialogue, uint32_t scene);

}  // namespace hdTrans::reg
