#pragma once
#include <cstddef>

namespace sidecar {

// One translated string. Kept in its own header because each table lives in
// its own translation unit: they are long, they change for reasons that have
// nothing to do with the lookup code, and a reviewer reading one should not
// have to scroll through the others.
struct TranslationPair {
  const char* english;
  const char* translated;
};

extern const TranslationPair kRussianTable[];
extern const size_t kRussianTableSize;
extern const TranslationPair kSpanishTable[];
extern const size_t kSpanishTableSize;
extern const TranslationPair kGermanTable[];
extern const size_t kGermanTableSize;
extern const TranslationPair kFrenchTable[];
extern const size_t kFrenchTableSize;
extern const TranslationPair kTurkishTable[];
extern const size_t kTurkishTableSize;
extern const TranslationPair kArabicTable[];
extern const size_t kArabicTableSize;
extern const TranslationPair kChineseTable[];
extern const size_t kChineseTableSize;
extern const TranslationPair kJapaneseTable[];
extern const size_t kJapaneseTableSize;
extern const TranslationPair kKoreanTable[];
extern const size_t kKoreanTableSize;

}  // namespace sidecar
