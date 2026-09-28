#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace sidecar {

// Interface text, in the operator's language.
//
// Strings are keyed by their English source text rather than by an invented
// identifier. That choice costs a hash lookup and buys the property that
// matters most here: a string with no entry in the table returns the English
// it was called with. A missing translation is then a paragraph in the wrong
// language, which is readable, rather than a blank panel or a bare key like
// "status.gpu.title", which is not. In a tool whose whole job is to explain
// why something will not run, the failure mode of the explanation is worth
// more than the tidiness of the keys.
//
// The cost is that editing an English string orphans its translation. That is
// caught by ci/check_translations.py rather than left to be noticed: every key
// in every table has to appear in the sources.
//
// Log messages are deliberately not routed through here. They go to
// sidecar.log for the maintainer to read in a bug report, and a log in a
// language the maintainer does not read makes the report harder to act on.

// Order is the order of the language picker. New languages go on the end so a
// stored index never changes meaning -- though what is stored is the tag.
enum class Language {
  English,
  Russian,
  Spanish,
  German,
  French,
  Turkish,
  Arabic,
  ChineseSimplified,
  Japanese,
  Korean,
  Count
};

constexpr size_t kLanguageCount = static_cast<size_t>(Language::Count);

// English is the default until the operator chooses: every screenshot in the
// documentation was taken in it.
Language CurrentLanguage();
void SetLanguage(Language language);

// The tag stored in sidecar.toml ("en", "ru", "zh", ...). Unknown tags are not
// an error worth refusing to start over -- Parse reports it and the caller
// warns.
const char* TagForLanguage(Language language);
bool ParseLanguageTag(std::string_view tag, Language& out);

// The language's name in itself ("Deutsch", "日本語"), for the picker. Each
// language names itself because "German" in an English list is no help to
// someone who cannot read the list. Logical order, unshaped.
const char* NativeLanguageName(Language language);

bool IsRightToLeft(Language language);

// The translation, or `english` unchanged when there is none. The returned
// pointer is valid for the life of the program: it is either the caller's own
// literal or an entry in a static table, never a temporary.
const char* Tr(const char* english);

// Same, for text assembled at runtime. Returns a copy because there is nothing
// static to point at.
std::string Tr(const std::string& english);

// A renderer with no bidirectional text support -- ImGui -- needs right-to-left
// text shaped and put in visual order before it draws it. Windows' own text
// output (message boxes, the HUD's GDI text) does that itself and must be
// handed logical text, or it would be reversed twice. So this is off by
// default and only the manager turns it on.
void SetVisualOrdering(bool enabled);
bool VisualOrdering();

// The rows of one language's table, for the tests and the glyph-range builder.
// English has none.
size_t TranslationCount(Language language);
void TranslationAt(Language language, size_t index, const char*& english,
                   const char*& translated);

// Kept for the tests that predate the other languages.
size_t RussianEntryCount();
void RussianEntryAt(size_t index, const char*& english, const char*& russian);

}  // namespace sidecar
