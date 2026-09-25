#pragma once
#include <cstddef>

namespace sidecar {

// One translated string. Kept in its own header because the table lives in its
// own translation unit: it is long, it changes for reasons that have nothing to
// do with the lookup code, and a reviewer reading one should not have to scroll
// through the other.
struct TranslationPair {
  const char* english;
  const char* translated;
};

extern const TranslationPair kRussianTable[];
extern const size_t kRussianTableSize;

}  // namespace sidecar
