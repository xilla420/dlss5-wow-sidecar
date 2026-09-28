#include "core/I18n.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Bidi.h"
#include "core/I18nTable.h"

namespace sidecar {
namespace {

// Not atomic on purpose. This is set from the one thread that draws the
// interface and read from the same thread; the runtime only reads it, and only
// before it has started a second thread. Making it atomic would suggest a
// concurrency story that does not exist.
Language g_language = Language::English;
bool g_visual = false;

struct LanguageInfo {
  const char* tag;
  const char* nativeName;
  const TranslationPair* table;
  const size_t* size;
  bool rightToLeft;
};

const std::array<LanguageInfo, kLanguageCount> kLanguages = {{
    {"en", "English", nullptr, nullptr, false},
    {"ru", "Русский", kRussianTable, &kRussianTableSize, false},
    {"es", "Español", kSpanishTable, &kSpanishTableSize, false},
    {"de", "Deutsch", kGermanTable, &kGermanTableSize, false},
    {"fr", "Français", kFrenchTable, &kFrenchTableSize, false},
    {"tr", "Türkçe", kTurkishTable, &kTurkishTableSize, false},
    {"ar", "العربية", kArabicTable, &kArabicTableSize, true},
    {"zh", "简体中文", kChineseTable, &kChineseTableSize, false},
    {"ja", "日本語", kJapaneseTable, &kJapaneseTableSize, false},
    {"ko", "한국어", kKoreanTable, &kKoreanTableSize, false},
}};

const LanguageInfo& Info(Language language) {
  return kLanguages[static_cast<size_t>(language)];
}

// The format specifiers in a string, in order -- the same pattern the CI
// checker uses. A visual-order string whose specifiers no longer line up with
// the English is not shown: printf would read its arguments in the wrong order.
std::string Specifiers(const char* text) {
  static const std::regex pattern(R"(%[-+ #0-9.]*(?:ll|z|h)?[sdfuxzp])");
  std::string joined;
  for (auto it = std::cregex_iterator(text, text + std::strlen(text), pattern);
       it != std::cregex_iterator(); ++it) {
    joined += it->str();
    joined += '|';
  }
  return joined;
}

using Index = std::unordered_map<std::string_view, const char*>;

// Built once per language and per ordering, on first use. The keys point into
// the tables' own static literals; shaped strings are owned by `storage`,
// whose elements never move once the index is built.
const Index& IndexFor(Language language, bool visual) {
  static std::array<Index, kLanguageCount> logical;
  static std::array<Index, kLanguageCount> shaped;
  static std::array<bool, kLanguageCount> builtLogical{};
  static std::array<bool, kLanguageCount> builtShaped{};
  static std::array<std::vector<std::string>, kLanguageCount> storage;

  const size_t i = static_cast<size_t>(language);
  const LanguageInfo& info = kLanguages[i];
  const bool shape = visual && info.rightToLeft;
  Index& index = shape ? shaped[i] : logical[i];
  bool& built = shape ? builtShaped[i] : builtLogical[i];
  if (built) return index;
  built = true;
  if (!info.table) return index;

  const size_t count = *info.size;
  index.reserve(count);
  if (shape) storage[i].reserve(count);
  for (size_t row = 0; row < count; ++row) {
    const TranslationPair& pair = info.table[row];
    if (!shape) {
      index.emplace(pair.english, pair.translated);
      continue;
    }
    // Two specifiers swap places when a line is put in visual order, and
    // printf fills them left to right -- so "N presented, M dropped" would
    // print each count against the other's word, with every type still
    // matching. Such strings stay English on a right-to-left page.
    const std::string english = Specifiers(pair.english);
    if (std::count(english.begin(), english.end(), '|') >= 2) continue;
    std::string display = ArabicForDisplay(pair.translated);
    if (Specifiers(display.c_str()) != english) continue;
    storage[i].push_back(std::move(display));
    index.emplace(pair.english, storage[i].back().c_str());
  }
  return index;
}

const char* Lookup(std::string_view english) {
  if (g_language == Language::English) return nullptr;
  const Index& index = IndexFor(g_language, g_visual);
  const auto found = index.find(english);
  return found == index.end() ? nullptr : found->second;
}

}  // namespace

Language CurrentLanguage() { return g_language; }

void SetLanguage(Language language) {
  if (language >= Language::Count) language = Language::English;
  g_language = language;
}

const char* TagForLanguage(Language language) { return Info(language).tag; }

bool ParseLanguageTag(std::string_view tag, Language& out) {
  for (size_t i = 0; i < kLanguageCount; ++i) {
    if (tag == kLanguages[i].tag) {
      out = static_cast<Language>(i);
      return true;
    }
  }
  return false;
}

const char* NativeLanguageName(Language language) { return Info(language).nativeName; }

bool IsRightToLeft(Language language) { return Info(language).rightToLeft; }

const char* Tr(const char* english) {
  if (english == nullptr) return nullptr;
  const char* translated = Lookup(english);
  return translated != nullptr ? translated : english;
}

std::string Tr(const std::string& english) {
  const char* translated = Lookup(english);
  return translated != nullptr ? std::string(translated) : english;
}

void SetVisualOrdering(bool enabled) { g_visual = enabled; }
bool VisualOrdering() { return g_visual; }

size_t TranslationCount(Language language) {
  const LanguageInfo& info = Info(language);
  return info.table ? *info.size : 0;
}

void TranslationAt(Language language, size_t index, const char*& english,
                   const char*& translated) {
  const LanguageInfo& info = Info(language);
  english = info.table[index].english;
  translated = info.table[index].translated;
}

size_t RussianEntryCount() { return kRussianTableSize; }

void RussianEntryAt(size_t index, const char*& english, const char*& russian) {
  english = kRussianTable[index].english;
  russian = kRussianTable[index].translated;
}

}  // namespace sidecar
