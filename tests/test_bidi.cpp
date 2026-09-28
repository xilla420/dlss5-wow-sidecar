#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/Bidi.h"

using namespace sidecar;

TEST_CASE("text with no Arabic is returned untouched", "[unit]") {
  CHECK(ArabicForDisplay("Start overlay") == "Start overlay");
  CHECK(ArabicForDisplay("") == "");
  CHECK(ArabicForDisplay("日本語") == "日本語");
  CHECK_FALSE(ContainsArabic("Ctrl+Alt+D"));
}

TEST_CASE("letters take their joined forms and the word is reversed", "[unit]") {
  // "بيت" (beh yeh teh): initial beh, medial yeh, final teh -- then drawn right
  // to left, so the final form comes first in the string ImGui walks.
  const std::string shaped = ArabicForDisplay("\xD8\xA8\xD9\x8A\xD8\xAA");
  CHECK(shaped == "\xEF\xBA\x96\xEF\xBB\xB4\xEF\xBA\x91");   // FE96 FEF4 FE91
}

TEST_CASE("lam followed by alef becomes one ligature", "[unit]") {
  // "لا" alone: the isolated lam-alef, U+FEFB.
  CHECK(ArabicForDisplay("\xD9\x84\xD8\xA7") == "\xEF\xBB\xBB");
}

TEST_CASE("left-to-right runs keep their order inside Arabic", "[unit]") {
  // "ب DLSS 5" -- the Latin run and its internal space survive intact, and it
  // is placed to the left of the Arabic because the paragraph runs right to left.
  const std::string shaped = ArabicForDisplay("\xD8\xA8 DLSS 5");
  CHECK(shaped.find("DLSS 5") != std::string::npos);
  CHECK(shaped.rfind("\xEF\xBA\x8F") == shaped.size() - 3);   // isolated beh last
}

TEST_CASE("format specifiers stay whole", "[unit]") {
  const std::string shaped = ArabicForDisplay("\xD8\xA8 %zu \xD8\xA8");
  CHECK(shaped.find("%zu") != std::string::npos);
}

TEST_CASE("lines are reordered separately", "[unit]") {
  const std::string shaped = ArabicForDisplay("\xD8\xA8\n\xD8\xAA");
  const size_t newline = shaped.find('\n');
  REQUIRE(newline != std::string::npos);
  // beh's line stays first.
  CHECK(shaped.substr(0, newline) == "\xEF\xBA\x8F");
}
