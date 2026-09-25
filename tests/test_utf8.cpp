#include <catch2/catch_test_macros.hpp>

#include "core/Utf8.h"

using namespace sidecar;

// These exist because std::filesystem::path's narrow accessors go through the
// process's active code page rather than UTF-8, and every path in this program
// crosses the boundary between Windows and ImGui at least twice. On an
// ASCII-only path the broken conversion and the correct one agree, which is
// precisely why the bug survives testing unless a test uses a path that is not
// ASCII.

TEST_CASE("empty input converts to empty output both ways", "[unit]") {
  CHECK(WideFromUtf8("").empty());
  CHECK(Utf8FromWide(L"").empty());
  CHECK(Utf8FromPath(std::filesystem::path{}).empty());
  CHECK(PathFromUtf8("").empty());
}

TEST_CASE("ASCII survives a round trip unchanged", "[unit]") {
  const std::string ascii = "D:/World of Warcraft/_retail_";
  CHECK(Utf8FromWide(WideFromUtf8(ascii)) == ascii);
}

TEST_CASE("a Cyrillic path survives a round trip", "[unit]") {
  // The case a code-page conversion would destroy: an ordinary folder name for
  // a Russian-language install. Written as explicit UTF-8 bytes so the test
  // does not depend on how this source file happens to be encoded.
  const std::string cyrillic = "D:/\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B/World of Warcraft";
  const std::wstring wide = WideFromUtf8(cyrillic);
  // Four Cyrillic letters. U+0400..U+04FF is two UTF-8 bytes and one UTF-16
  // unit apiece, so the wide form is four units shorter than the byte count --
  // which is the whole reason a length cannot be reused across the two.
  CHECK(wide.size() == cyrillic.size() - 4);
  CHECK(Utf8FromWide(wide) == cyrillic);
}

TEST_CASE("paths round trip through the filesystem type", "[unit]") {
  const std::string cyrillic = "D:\\\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B\\WoW";
  const auto path = PathFromUtf8(cyrillic);
  CHECK(Utf8FromPath(path) == cyrillic);
  // And the wide form is what Windows would actually be handed: U+0418, the
  // first letter of the folder name above.
  CHECK(path.native().find(L'\x0418') != std::wstring::npos);
}

TEST_CASE("non-BMP characters survive as surrogate pairs", "[unit]") {
  // U+1F600: four UTF-8 bytes, two UTF-16 units. Nobody names a game folder
  // this, but a conversion that sized its buffer wrongly would cut a surrogate
  // pair in half, and that failure is silent.
  const std::string emoji = "\xF0\x9F\x98\x80";
  const std::wstring wide = WideFromUtf8(emoji);
  CHECK(wide.size() == 2);
  CHECK(Utf8FromWide(wide) == emoji);
}
