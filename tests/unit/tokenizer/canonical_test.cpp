#include <cstdint>
#include "core/tokenizer/canonical.h"

#include "fnv.h"
#include "hdtest.h"

using namespace hdTrans::tokenizer;

namespace {

// Hash of a character -> form lookup over every code point 0..U+10FFFF.
uint64_t FormFingerprint(std::optional<std::string_view> (*lookup)(char32_t)) {
    hdtest::Fnv h;
    for (char32_t c = 0; c <= 0x10FFFF; c++) {
        const auto form = lookup(c);
        h.Byte(form ? 1 : 0);
        if (form) h.Str(*form);
    }
    return h.h;
}

}  // namespace

// Hashes of the tables; a change to a table changes its hash.
HD_TEST(canonical_fingerprints) {
    HD_CHECK_EQ(FormFingerprint(CanonicalForm), uint64_t{0xe4e9758e31bb72c2ull});
    HD_CHECK_EQ(FormFingerprint(FirstForm), uint64_t{0xa6eaf75cd1d112dfull});
    hdtest::Fnv widen;
    for (char32_t c = 0; c <= 0x10FFFF; c++) widen.U32(static_cast<uint32_t>(WidenHalfWidth(c)));
    HD_CHECK_EQ(widen.h, uint64_t{0x29e553f923d31d83ull});
    HD_CHECK_EQ(internal::CanonicalFormTable().size(), size_t{2617});
    HD_CHECK_EQ(internal::FirstFormTable().size(), size_t{8});
}

HD_TEST(canonical_tables_sorted) {
    for (const auto table : {internal::CanonicalFormTable(), internal::FirstFormTable()}) {
        for (size_t i = 1; i < table.size(); i++) HD_CHECK(table[i - 1].ch < table[i].ch);
    }
}

HD_TEST(canonical_samples) {
    struct Case {
        char32_t ch;
        const char* form;  // nullptr: none
    };
    const Case cases[] = {
        {U'、', ", "}, {U'。', ". "}, {U'与', "與"}, {U'㈱', "㈜"}, {U'々', nullptr}, {U'°', nullptr}, {U'ゝ', nullptr},
        {U'ー', "―"},  {U'猫', nullptr}, {U'あ', nullptr}, {U'a', nullptr},
    };
    for (const auto& c : cases) {
        const auto form = CanonicalForm(c.ch);
        HD_CHECK_EQ(form.has_value(), c.form != nullptr);
        if (form && c.form) HD_CHECK_EQ(std::string(*form), std::string(c.form));
    }
    HD_CHECK_EQ(std::string(FirstForm(U'〒').value_or("")), std::string("(우)"));
    HD_CHECK_EQ(std::string(FirstForm(U'“').value_or("")), std::string(" \""));
    HD_CHECK(!FirstForm(U'。').has_value());
    HD_CHECK(WidenHalfWidth(U'ｱ') == U'ア');
    HD_CHECK(WidenHalfWidth(U'ﾞ') == U'゛');
    HD_CHECK(WidenHalfWidth(U'ｰ') == U'ー');
    HD_CHECK(WidenHalfWidth(U'ア') == U'ア');
}
