#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <set>
#include <string>

#include "core/I18n.h"

using namespace sidecar;

namespace {

// The language is process-wide state, and other tests draw HUD text through
// the same lookup. Leaving Russian set behind would make those fail depending
// on the order Catch2 happened to shuffle them into.
struct LanguageGuard {
  Language previous = CurrentLanguage();
  ~LanguageGuard() { SetLanguage(previous); }
};

}  // namespace

TEST_CASE("English is the default", "[unit]") {
  // Deliberately not taken from the Windows locale: every screenshot in the
  // documentation is English, and a first run that does not match them is a
  // worse introduction than one in a second language.
  CHECK(CurrentLanguage() == Language::English);
}

TEST_CASE("in English a string is returned exactly as given", "[unit]") {
  LanguageGuard guard;
  SetLanguage(Language::English);
  const char* source = "Re-run checks";
  // The same pointer, not merely an equal string: there is nothing to look up
  // and nothing to copy.
  CHECK(Tr(source) == source);
}

TEST_CASE("in Russian a known string is translated", "[unit]") {
  LanguageGuard guard;
  SetLanguage(Language::Russian);
  const char* translated = Tr("Re-run checks");
  CHECK(std::strcmp(translated, "Re-run checks") != 0);
  CHECK(std::strlen(translated) > 0);
}

TEST_CASE("an untranslated string falls back to English, not to nothing",
          "[unit]") {
  // The whole reason keys are the English text. A missing entry has to leave a
  // readable sentence on screen; a blank panel or a bare key name would be
  // worse than the wrong language, in a tool whose job is explaining why
  // something will not run.
  LanguageGuard guard;
  SetLanguage(Language::Russian);
  const char* absent = "This sentence is in no translation table.";
  CHECK(Tr(absent) == absent);
}

TEST_CASE("switching back to English undoes the translation", "[unit]") {
  LanguageGuard guard;
  const char* source = "Detect";
  SetLanguage(Language::Russian);
  const char* russian = Tr(source);
  SetLanguage(Language::English);
  CHECK(Tr(source) == source);
  CHECK(std::strcmp(russian, source) != 0);
}

TEST_CASE("a null string is passed through rather than dereferenced", "[unit]") {
  LanguageGuard guard;
  SetLanguage(Language::Russian);
  CHECK(Tr(static_cast<const char*>(nullptr)) == nullptr);
}

TEST_CASE("the std::string overload behaves like the pointer one", "[unit]") {
  LanguageGuard guard;
  SetLanguage(Language::Russian);
  CHECK(Tr(std::string("Detect")) == std::string(Tr("Detect")));
  const std::string absent = "Not a key in any table.";
  CHECK(Tr(absent) == absent);
}

TEST_CASE("language tags round-trip", "[unit]") {
  CHECK(std::string(TagForLanguage(Language::English)) == "en");
  CHECK(std::string(TagForLanguage(Language::Russian)) == "ru");

  Language parsed = Language::Russian;
  REQUIRE(ParseLanguageTag("en", parsed));
  CHECK(parsed == Language::English);
  REQUIRE(ParseLanguageTag("ru", parsed));
  CHECK(parsed == Language::Russian);
}

TEST_CASE("an unknown tag is refused and leaves the target alone", "[unit]") {
  // The caller warns and keeps its default. Refusing to start over a typo in a
  // preference would be the wrong trade in a tool that has to run in order to
  // explain itself.
  Language parsed = Language::Russian;
  CHECK_FALSE(ParseLanguageTag("xx", parsed));
  CHECK(parsed == Language::Russian);
  CHECK_FALSE(ParseLanguageTag("", parsed));
  CHECK_FALSE(ParseLanguageTag("EN", parsed));   // tags are lower-case
  CHECK(parsed == Language::Russian);
}

// The table's own integrity. ci/check_translations.py checks this too, and
// checks the part this cannot -- whether each key still exists in the sources.
// These run wherever the tests run, including where Python does not.
TEST_CASE("the Russian table has no duplicate keys", "[unit]") {
  std::set<std::string> seen;
  for (size_t i = 0; i < RussianEntryCount(); ++i) {
    const char* english = nullptr;
    const char* russian = nullptr;
    RussianEntryAt(i, english, russian);
    INFO("row " << i << ": " << english);
    // A duplicate makes the second row unreachable, and which of the two wins
    // is a detail of how the index is built rather than anything anyone chose.
    CHECK(seen.insert(english).second);
  }
}

TEST_CASE("no translation is empty or identical to its key", "[unit]") {
  for (size_t i = 0; i < RussianEntryCount(); ++i) {
    const char* english = nullptr;
    const char* russian = nullptr;
    RussianEntryAt(i, english, russian);
    INFO("row " << i << ": " << english);
    CHECK(std::strlen(russian) > 0);
    CHECK(std::strcmp(english, russian) != 0);
  }
}

TEST_CASE("the table is not empty", "[unit]") {
  // Every check above passes trivially over an empty table, so the count is
  // asserted rather than assumed.
  CHECK(RussianEntryCount() > 100);
}

TEST_CASE("every language's tag round-trips and names itself", "[unit]") {
  std::set<std::string> tags;
  for (size_t i = 0; i < kLanguageCount; ++i) {
    const auto language = static_cast<Language>(i);
    const std::string tag = TagForLanguage(language);
    INFO(tag);
    CHECK(tags.insert(tag).second);
    Language parsed = Language::English;
    REQUIRE(ParseLanguageTag(tag, parsed));
    CHECK(parsed == language);
    CHECK(std::strlen(NativeLanguageName(language)) > 0);
  }
  CHECK(IsRightToLeft(Language::Arabic));
  CHECK_FALSE(IsRightToLeft(Language::Japanese));
}

TEST_CASE("every table is sound: no duplicates, no empty rows, most keys covered",
          "[unit]") {
  for (size_t i = 1; i < kLanguageCount; ++i) {
    const auto language = static_cast<Language>(i);
    INFO(TagForLanguage(language));
    std::set<std::string> seen;
    for (size_t row = 0; row < TranslationCount(language); ++row) {
      const char* english = nullptr;
      const char* translated = nullptr;
      TranslationAt(language, row, english, translated);
      INFO("row " << row << ": " << english);
      CHECK(seen.insert(english).second);
      CHECK(std::strlen(translated) > 0);
    }
    // Arabic leaves the runtime-joined fragments in English on purpose.
    CHECK(TranslationCount(language) > 200);
  }
}

TEST_CASE("each language translates the navigation", "[unit]") {
  LanguageGuard guard;
  for (size_t i = 1; i < kLanguageCount; ++i) {
    SetLanguage(static_cast<Language>(i));
    INFO(TagForLanguage(static_cast<Language>(i)));
    CHECK(std::strcmp(Tr("Start overlay"), "Start overlay") != 0);
  }
}

TEST_CASE("visual ordering reorders Arabic but leaves other languages alone", "[unit]") {
  LanguageGuard guard;
  struct OrderingGuard {
    ~OrderingGuard() { SetVisualOrdering(false); }
  } ordering;

  SetLanguage(Language::Arabic);
  SetVisualOrdering(false);
  const std::string logical = Tr("Start overlay");
  SetVisualOrdering(true);
  const std::string visual = Tr("Start overlay");
  CHECK(logical != visual);          // shaped and reversed for ImGui
  CHECK_FALSE(visual.empty());

  // A string whose two specifiers would swap places in visual order is not
  // shown in Arabic at all: printf would read its arguments backwards.
  CHECK(std::string(Tr("%llu presented, %llu dropped")) == "%llu presented, %llu dropped");

  SetLanguage(Language::German);
  CHECK(std::string(Tr("Start overlay")) == "Overlay starten");
}
