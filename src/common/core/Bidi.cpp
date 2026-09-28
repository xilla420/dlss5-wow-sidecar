#include "core/Bidi.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace sidecar {
namespace {

std::vector<char32_t> Decode(std::string_view s) {
  std::vector<char32_t> out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    char32_t cp = 0xFFFD;
    size_t len = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
      cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
      len = 2;
    } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
      cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
      len = 3;
    } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
      cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) |
           (s[i + 3] & 0x3F);
      len = 4;
    }
    out.push_back(cp);
    i += len;
  }
  return out;
}

void Encode(char32_t cp, std::string& out) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Presentation forms for U+0621..U+064A: isolated, final, initial, medial.
// Zero where the letter has no such form -- which is what makes it
// right-joining (it connects to the letter before it, never the one after).
struct Forms {
  char16_t isolated, final_, initial, medial;
};

constexpr Forms kForms[] = {
    {0xFE80, 0, 0, 0},                 // 0621 hamza
    {0xFE81, 0xFE82, 0, 0},            // 0622 alef with madda
    {0xFE83, 0xFE84, 0, 0},            // 0623 alef with hamza above
    {0xFE85, 0xFE86, 0, 0},            // 0624 waw with hamza
    {0xFE87, 0xFE88, 0, 0},            // 0625 alef with hamza below
    {0xFE89, 0xFE8A, 0xFE8B, 0xFE8C},  // 0626 yeh with hamza
    {0xFE8D, 0xFE8E, 0, 0},            // 0627 alef
    {0xFE8F, 0xFE90, 0xFE91, 0xFE92},  // 0628 beh
    {0xFE93, 0xFE94, 0, 0},            // 0629 teh marbuta
    {0xFE95, 0xFE96, 0xFE97, 0xFE98},  // 062A teh
    {0xFE99, 0xFE9A, 0xFE9B, 0xFE9C},  // 062B theh
    {0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0},  // 062C jeem
    {0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},  // 062D hah
    {0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8},  // 062E khah
    {0xFEA9, 0xFEAA, 0, 0},            // 062F dal
    {0xFEAB, 0xFEAC, 0, 0},            // 0630 thal
    {0xFEAD, 0xFEAE, 0, 0},            // 0631 reh
    {0xFEAF, 0xFEB0, 0, 0},            // 0632 zain
    {0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},  // 0633 seen
    {0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8},  // 0634 sheen
    {0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC},  // 0635 sad
    {0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0},  // 0636 dad
    {0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4},  // 0637 tah
    {0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8},  // 0638 zah
    {0xFEC9, 0xFECA, 0xFECB, 0xFECC},  // 0639 ain
    {0xFECD, 0xFECE, 0xFECF, 0xFED0},  // 063A ghain
    {0, 0, 0, 0},                      // 063B
    {0, 0, 0, 0},                      // 063C
    {0, 0, 0, 0},                      // 063D
    {0, 0, 0, 0},                      // 063E
    {0, 0, 0, 0},                      // 063F
    {0x0640, 0x0640, 0x0640, 0x0640},  // 0640 tatweel
    {0xFED1, 0xFED2, 0xFED3, 0xFED4},  // 0641 feh
    {0xFED5, 0xFED6, 0xFED7, 0xFED8},  // 0642 qaf
    {0xFED9, 0xFEDA, 0xFEDB, 0xFEDC},  // 0643 kaf
    {0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0},  // 0644 lam
    {0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},  // 0645 meem
    {0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},  // 0646 noon
    {0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC},  // 0647 heh
    {0xFEED, 0xFEEE, 0, 0},            // 0648 waw
    {0xFEEF, 0xFEF0, 0, 0},            // 0649 alef maksura
    {0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},  // 064A yeh
};

const Forms* FormsFor(char32_t cp) {
  if (cp < 0x0621 || cp > 0x064A) return nullptr;
  const Forms& f = kForms[cp - 0x0621];
  return f.isolated ? &f : nullptr;
}

// Harakat and the superscript alef: drawn over a letter, ignored for joining.
bool IsTransparent(char32_t cp) {
  return (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670;
}

bool JoinsForward(char32_t cp) {
  const Forms* f = FormsFor(cp);
  return f && f->initial != 0;
}

bool IsArabicLetter(char32_t cp) { return FormsFor(cp) != nullptr; }

// Lam followed by one of the alefs becomes a single ligature.
char16_t LamAlef(char32_t alef, bool final_) {
  switch (alef) {
    case 0x0622: return final_ ? 0xFEF6 : 0xFEF5;
    case 0x0623: return final_ ? 0xFEF8 : 0xFEF7;
    case 0x0625: return final_ ? 0xFEFA : 0xFEF9;
    case 0x0627: return final_ ? 0xFEFC : 0xFEFB;
    default: return 0;
  }
}

std::vector<char32_t> Shape(const std::vector<char32_t>& in) {
  std::vector<char32_t> out;
  out.reserve(in.size());
  const auto neighbour = [&](size_t from, int step) -> char32_t {
    for (ptrdiff_t j = static_cast<ptrdiff_t>(from) + step;
         j >= 0 && j < static_cast<ptrdiff_t>(in.size()); j += step) {
      if (!IsTransparent(in[static_cast<size_t>(j)])) return in[static_cast<size_t>(j)];
    }
    return 0;
  };

  for (size_t i = 0; i < in.size(); ++i) {
    const char32_t cp = in[i];
    const Forms* forms = FormsFor(cp);
    if (!forms) {
      out.push_back(cp);
      continue;
    }
    const char32_t prev = neighbour(i, -1);
    const bool joinPrev = JoinsForward(prev);

    // Lam-alef: one glyph for two letters, joined to what precedes it.
    if (cp == 0x0644 && i + 1 < in.size()) {
      if (const char16_t lig = LamAlef(in[i + 1], joinPrev)) {
        out.push_back(lig);
        ++i;
        continue;
      }
    }

    const char32_t next = neighbour(i, +1);
    const bool joinNext = forms->initial != 0 && IsArabicLetter(next);
    char16_t shaped = forms->isolated;
    if (joinPrev && joinNext && forms->medial) {
      shaped = forms->medial;
    } else if (joinPrev && forms->final_) {
      shaped = forms->final_;
    } else if (joinNext && forms->initial) {
      shaped = forms->initial;
    }
    out.push_back(shaped);
  }
  return out;
}

enum class Dir { L, R, N };

Dir Classify(char32_t cp) {
  if ((cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) ||
      (cp >= 0xFE70 && cp <= 0xFEFF)) {
    return Dir::R;
  }
  switch (cp) {
    case ' ': case '.': case ',': case ':': case ';': case '!': case '?':
    case '(': case ')': case '[': case ']': case '"': case '\'': case '-':
    case '/': case 0x2013: case 0x2014: case 0x00AB: case 0x00BB: case 0x2026:
    case 0x00A0:
      return Dir::N;
    default:
      // Letters, digits, and '%' -- the last so a format specifier stays one
      // left-to-right run and reaches printf intact.
      return Dir::L;
  }
}

char32_t Mirror(char32_t cp) {
  switch (cp) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case 0x00AB: return 0x00BB;
    case 0x00BB: return 0x00AB;
    default: return cp;
  }
}

}  // namespace

bool ContainsArabic(std::string_view utf8) {
  for (char32_t cp : Decode(utf8)) {
    if (Classify(cp) == Dir::R) return true;
  }
  return false;
}

std::string ArabicForDisplay(std::string_view logical) {
  if (!ContainsArabic(logical)) return std::string(logical);

  // Each line is its own paragraph: reordering across a line break would put
  // the last line first.
  if (const size_t newline = logical.find('\n'); newline != std::string_view::npos) {
    return ArabicForDisplay(logical.substr(0, newline)) + "\n" +
           ArabicForDisplay(logical.substr(newline + 1));
  }

  const std::vector<char32_t> shaped = Shape(Decode(logical));

  // Resolve neutrals: between two left-to-right characters a neutral belongs
  // to that run ("DLSS 5", "12.5 ms"); anywhere else it takes the paragraph's
  // direction, right to left.
  std::vector<Dir> dir(shaped.size());
  for (size_t i = 0; i < shaped.size(); ++i) dir[i] = Classify(shaped[i]);
  for (size_t i = 0; i < shaped.size(); ++i) {
    if (dir[i] != Dir::N) continue;
    size_t j = i;
    while (j < shaped.size() && dir[j] == Dir::N) ++j;
    const bool leftIsL = i > 0 && dir[i - 1] == Dir::L;
    const bool rightIsL = j < shaped.size() && dir[j] == Dir::L;
    const Dir resolved = (leftIsL && rightIsL) ? Dir::L : Dir::R;
    for (size_t k = i; k < j; ++k) dir[k] = resolved;
    i = j - 1;
  }

  // Runs, laid out right to left. Right-to-left runs are reversed character
  // by character; left-to-right runs keep their order.
  std::vector<std::pair<size_t, size_t>> runs;
  for (size_t i = 0; i < shaped.size();) {
    size_t j = i;
    while (j < shaped.size() && dir[j] == dir[i]) ++j;
    runs.emplace_back(i, j);
    i = j;
  }
  std::string out;
  out.reserve(logical.size());
  for (auto run = runs.rbegin(); run != runs.rend(); ++run) {
    if (dir[run->first] == Dir::L) {
      for (size_t k = run->first; k < run->second; ++k) Encode(shaped[k], out);
    } else {
      for (size_t k = run->second; k > run->first; --k) Encode(Mirror(shaped[k - 1]), out);
    }
  }
  return out;
}

}  // namespace sidecar
