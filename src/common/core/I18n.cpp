#include "core/I18n.h"

#include <cstring>
#include <unordered_map>

#include "core/I18nTable.h"

namespace sidecar {
namespace {

// Not atomic on purpose. This is set from the one thread that draws the
// interface and read from the same thread; the runtime only reads it, and only
// before it has started a second thread. Making it atomic would suggest a
// concurrency story that does not exist.
Language g_language = Language::English;

// Built once, on first use. std::string_view keys point into the table's own
// static literals, so nothing here owns a character.
const std::unordered_map<std::string_view, const char*>& RussianIndex() {
  static const std::unordered_map<std::string_view, const char*> index = [] {
    std::unordered_map<std::string_view, const char*> map;
    map.reserve(kRussianTableSize);
    for (size_t i = 0; i < kRussianTableSize; ++i) {
      map.emplace(kRussianTable[i].english, kRussianTable[i].translated);
    }
    return map;
  }();
  return index;
}

const char* Lookup(std::string_view english) {
  if (g_language == Language::English) return nullptr;
  const auto& index = RussianIndex();
  const auto found = index.find(english);
  return found == index.end() ? nullptr : found->second;
}

}  // namespace

Language CurrentLanguage() { return g_language; }

void SetLanguage(Language language) { g_language = language; }

const char* TagForLanguage(Language language) {
  return language == Language::Russian ? "ru" : "en";
}

bool ParseLanguageTag(std::string_view tag, Language& out) {
  if (tag == "en") {
    out = Language::English;
    return true;
  }
  if (tag == "ru") {
    out = Language::Russian;
    return true;
  }
  return false;
}

const char* Tr(const char* english) {
  if (english == nullptr) return nullptr;
  const char* translated = Lookup(english);
  return translated != nullptr ? translated : english;
}

std::string Tr(const std::string& english) {
  const char* translated = Lookup(english);
  return translated != nullptr ? std::string(translated) : english;
}

size_t RussianEntryCount() { return kRussianTableSize; }

void RussianEntryAt(size_t index, const char*& english, const char*& russian) {
  english = kRussianTable[index].english;
  russian = kRussianTable[index].translated;
}

}  // namespace sidecar
