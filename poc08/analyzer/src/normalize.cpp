#include "normalize.h"

#include "utf8.h"

namespace hd8::ana::internal {

namespace {

// U+FF66..U+FF9D → full-width katakana
constexpr char16_t kHalfKana[] = u"ヲァィゥェォャュョッーアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワン";

bool TakesDakuten(char32_t c) {
  return (c >= 0x30AB && c <= 0x30C1 && (c - 0x30AB) % 2 == 0) ||  // カ..チ
         c == 0x30C4 || c == 0x30C6 || c == 0x30C8 || c == 0x30A6 ||  // ツテトウ
         (c >= 0x30CF && c <= 0x30DB && (c - 0x30CF) % 3 == 0);      // ハ..ホ
}
bool TakesHandakuten(char32_t c) { return c >= 0x30CF && c <= 0x30DB && (c - 0x30CF) % 3 == 0; }

char32_t Voice(char32_t c) { return c == 0x30A6 ? 0x30F4 : c + 1; }

bool IsKana(char32_t c) { return IsHiragana(c) || IsKatakana(c); }

void AddEdit(std::vector<NormEdit>* edits, std::string_view src, uint32_t sb, uint32_t se, std::string to,
             EditKind kind) {
  if (!edits->empty() && edits->back().kind == kind && edits->back().src.end == sb && kind == EditKind::width) {
    auto& e = edits->back();
    e.src.end = se;
    e.from = std::string(src.substr(e.src.begin, se - e.src.begin));
    e.to += to;
    return;
  }
  NormEdit e;
  e.src = {sb, se};
  e.from = std::string(src.substr(sb, se - sb));
  e.to = std::move(to);
  e.kind = kind;
  edits->push_back(std::move(e));
}

}  // namespace

void Normalize(std::string_view src, std::vector<NChar>* out, std::vector<NormEdit>* edits) {
  out->clear();
  edits->clear();
  std::vector<NChar> raw;
  size_t i = 0;
  while (i < src.size()) {
    auto sb = static_cast<uint32_t>(i);
    char32_t c = DecodeUtf8(src, i);
    raw.push_back({c, sb, static_cast<uint32_t>(i)});
  }
  // width
  std::vector<NChar> w;
  w.reserve(raw.size());
  for (size_t k = 0; k < raw.size(); ++k) {
    NChar x = raw[k];
    char32_t c = x.c;
    char32_t to = 0;
    if (c >= 0xFF66 && c <= 0xFF9D) {
      to = kHalfKana[c - 0xFF66];
      if (k + 1 < raw.size() && raw[k + 1].c == 0xFF9E && TakesDakuten(to)) {
        to = Voice(to);
        x.se = raw[++k].se;
      } else if (k + 1 < raw.size() && raw[k + 1].c == 0xFF9F && TakesHandakuten(to)) {
        to = to + 2;
        x.se = raw[++k].se;
      }
    } else if (c == 0xFF9E) {
      to = 0x309B;
    } else if (c == 0xFF9F) {
      to = 0x309C;
    } else if (c == 0xFF61) {
      to = 0x3002;
    } else if (c == 0xFF62) {
      to = 0x300C;
    } else if (c == 0xFF63) {
      to = 0x300D;
    } else if (c == 0xFF64) {
      to = 0x3001;
    } else if (c == 0xFF65) {
      to = 0x30FB;
    }
    if (to) {
      std::string t;
      AppendUtf8(t, to);
      AddEdit(edits, src, x.sb, x.se, t, EditKind::width);
      x.c = to;
    }
    w.push_back(x);
  }
  // ruby: ｜base《reading》, kanji《reading》, kanji（kana）
  for (size_t k = 0; k < w.size(); ++k) {
    char32_t c = w[k].c;
    if (c == U'｜' || c == U'|') {
      size_t open = k + 1;
      while (open < w.size() && w[open].c != U'《' && open - k <= 20) ++open;
      size_t close = open + 1;
      while (close < w.size() && w[close].c != U'》' && close - open <= 30) ++close;
      if (open < w.size() && w[open].c == U'《' && close < w.size() && w[close].c == U'》' && open > k + 1) {
        std::string base;
        for (size_t j = k + 1; j < open; ++j) AppendUtf8(base, w[j].c);
        AddEdit(edits, src, w[k].sb, w[close].se, base, EditKind::ruby);
        for (size_t j = k + 1; j < open; ++j) out->push_back(w[j]);
        k = close;
        continue;
      }
    }
    if ((c == U'《' || c == U'（' || c == U'(') && !out->empty() && IsKanji(out->back().c)) {
      char32_t closer = c == U'《' ? U'》' : c == U'（' ? U'）' : U')';
      size_t close = k + 1;
      bool kana = true;
      while (close < w.size() && w[close].c != closer && close - k <= 21) {
        if (!IsKana(w[close].c)) kana = false;
        ++close;
      }
      size_t base = 0;
      for (size_t j = out->size(); j > 0 && IsKanji((*out)[j - 1].c); --j) ++base;
      size_t len = close - k - 1;
      if (close < w.size() && w[close].c == closer && kana && len >= 1 && (c == U'《' || len <= 4 * base + 2)) {
        std::string from, to;
        for (size_t j = out->size() - base; j < out->size(); ++j) AppendUtf8(to, (*out)[j].c);
        AddEdit(edits, src, (*out)[out->size() - base].sb, w[close].se, to, EditKind::ruby);
        k = close;
        continue;
      }
    }
    out->push_back(w[k]);
  }
}

}  // namespace hd8::ana::internal
