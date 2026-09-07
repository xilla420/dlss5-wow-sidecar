#pragma once
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
// caught by a test rather than left to be noticed: every key in the Russian
// table has to appear in the sources.
//
// Log messages are deliberately not routed through here. They go to
// sidecar.log for the maintainer to read in a bug report, and a log in a
// language the maintainer does not read makes the report harder to act on.

enum class Language { English, Russian };

// English is the default, and stays the default on a Russian Windows: the
// operator chooses, and until they do they see the language every screenshot
// in the documentation was taken in.
Language CurrentLanguage();
void SetLanguage(Language language);

// The tag stored in sidecar.toml. Unknown tags are not an error worth refusing
// to start over -- Parse reports it and the caller warns.
const char* TagForLanguage(Language language);
bool ParseLanguageTag(std::string_view tag, Language& out);

// The translation, or `english` unchanged when there is none. The returned
// pointer is valid for the life of the program: it is either the caller's own
// literal or an entry in a static table, never a temporary.
const char* Tr(const char* english);

// Same, for text assembled at runtime. Returns a copy because there is nothing
// static to point at.
std::string Tr(const std::string& english);

// How many pairs the Russian table holds, and the pair at an index. Exposed so
// the table can be checked for duplicates and for entries that were never
// actually translated, which is a test's job rather than a reviewer's.
size_t RussianEntryCount();
void RussianEntryAt(size_t index, const char*& english, const char*& russian);

}  // namespace sidecar
