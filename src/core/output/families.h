#pragma once
// Marker families: the static rows of the three marker tables and the families made
// for stems that no static family conjugates. A key such as "&A1@0" is a family id
// (marker &, $ or %, two characters, '@') and a form code '0'..'9', 'A', 'B'.
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/dict/user_text.h"

namespace hdTrans::output {

// The form codes of a family, in row order.
inline constexpr std::string_view kFormCodes = "0123456789AB";

// Rows of a family by the position of their form code in kFormCodes; an empty row is
// no row.
using FamilyRows = std::array<std::string, 12>;

// Generated families. A family is identified by its marker and rows, so the same rows
// always get the same id; ids use first characters that no static family of the marker
// uses (MarkerFamilyLetters) and are given in the order the families are made. Safe to
// use from several threads; a row view stays valid while the families live.
class Families {
public:
    Families() = default;
    Families(const Families&) = delete;
    Families& operator=(const Families&) = delete;

    // The row of a generated family's key; "" when there is none.
    std::string_view Row(std::string_view key) const;

    // The id ("&Ba@") of the family of marker ('&', '$' or '%') with rows, made when
    // no family has these rows yet; "" for another marker or when the ids of the
    // marker are used up.
    std::string Generate(char marker, const FamilyRows& rows);

    // Adds the family with the given id ("&Ba@") and rows, the id the dictionaries gave it.
    // Returns false when id is not a generated id of marker, is taken, or when the rows
    // are already a family; later Generate calls do not give the id again.
    bool Insert(char marker, std::string_view id, const FamilyRows& rows);

    size_t Size() const;

    // Every key and row, in no order; not safe while Generate runs.
    const std::unordered_map<std::string, std::string, dict::StringHash, std::equal_to<>>& Rows() const {
        return rows_;
    }

private:
    bool IdTaken(std::string_view id) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string, dict::StringHash, std::equal_to<>> rows_;  // "&Ba@5" -> row
    std::unordered_map<std::string, std::string, dict::StringHash, std::equal_to<>> ids_;   // marker and rows -> id
    std::array<int, 3> count_{};                                                            // ids made per marker
};

// The row of key in the &, $ and % table, then among the generated families; "" when
// there is no such row.
std::string_view LookupSource(std::string_view key, const Families& generated);
std::string_view LookupDollar(std::string_view key, const Families& generated);
std::string_view LookupPercent(std::string_view key, const Families& generated);

// The lookup of the table that the first byte of key names; "" for other keys.
std::string_view LookupByMarker(std::string_view key, const Families& generated);

// text with each marker family left in it written as its dictionary form (천&AV@ 천하다); the
// marker of a family without a stem row is dropped. The loader keeps such rows out of the
// output, so a family here is an internal error: each one counts in UnexpandedMarkers.
std::string DictionaryForms(std::string text, const Families& generated);

// The marker families DictionaryForms has written since the process started.
std::uint64_t UnexpandedMarkers();

// A conjugating target that ends in a marker family ("만&A5@") in its dictionary form
// (만나다); other targets as they are.
std::string Lemma(std::string_view target, const Families& generated);

}  // namespace hdTrans::output
