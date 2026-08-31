#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/Sha256.h"

using namespace sidecar;

namespace {

// A directory the test owns outright, so a failure never leaves anything
// behind in the source tree or the build output.
std::filesystem::path ScratchDir() {
  auto dir = std::filesystem::temp_directory_path() / "sidecar_sha256_tests";
  std::filesystem::create_directories(dir);
  return dir;
}

}  // namespace

// The published NIST vectors. These pin the algorithm itself: if BCrypt were
// handed the wrong algorithm identifier, or the digest were byte-reversed, or
// the hex encoding dropped a leading zero, one of these fails.
TEST_CASE("Sha256Hex matches the published vectors", "[unit]") {
  SECTION("empty input") {
    REQUIRE(Sha256Hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  }

  SECTION("abc") {
    REQUIRE(Sha256Hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  }

  SECTION("the 56-byte two-block vector") {
    REQUIRE(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  }
}

// A million 'a's. Larger than any sane internal buffer, so this is what proves
// the implementation feeds the hash incrementally rather than assuming one shot.
TEST_CASE("Sha256Hex hashes input larger than one buffer", "[unit]") {
  const std::string million(1000000, 'a');
  REQUIRE(Sha256Hex(million) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("Sha256Hex output is lowercase hex of the full digest", "[unit]") {
  const std::string digest = Sha256Hex("abc");
  REQUIRE(digest.size() == 64);
  for (char c : digest) {
    REQUIRE(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
  }
}

TEST_CASE("Sha256File hashes a file's contents", "[unit]") {
  const auto path = ScratchDir() / "abc.bin";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "abc";
  }
  REQUIRE(Sha256File(path) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::filesystem::remove(path);
}

// The NR runtime is ~166 MB. Reading it whole into memory to hash it would be
// wasteful, so the file path must stream -- and streaming is exactly where a
// chunk-boundary bug hides. A file spanning many reads with a non-aligned tail
// catches it.
TEST_CASE("Sha256File streams a file spanning many chunks", "[unit]") {
  const auto path = ScratchDir() / "million_a.bin";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) out << chunk;
  }
  REQUIRE(Sha256File(path) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  std::filesystem::remove(path);
}

TEST_CASE("Sha256File of an empty file is the empty digest", "[unit]") {
  const auto path = ScratchDir() / "empty.bin";
  { std::ofstream out(path, std::ios::binary | std::ios::trunc); }
  REQUIRE(Sha256File(path) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  std::filesystem::remove(path);
}

// A missing file must be distinguishable from a file that happens to hash to
// something. Empty string is the sentinel, and it can never collide with a real
// digest because a real digest is always 64 characters.
TEST_CASE("Sha256File returns empty for a file that cannot be read", "[unit]") {
  REQUIRE(Sha256File(ScratchDir() / "no_such_file_here.bin").empty());
}
