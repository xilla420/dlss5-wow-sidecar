#include <catch2/catch_test_macros.hpp>
#include "../src/testpattern/Pattern.h"

using sidecar::testpattern::PixelAt;

TEST_CASE("quadrants are flat, saturated and distinct", "[unit]") {
  constexpr uint32_t W = 640, H = 480;
  const auto tl = PixelAt(10, 10, W, H, 0);
  const auto tr = PixelAt(W - 10, 10, W, H, 0);
  const auto bl = PixelAt(10, H - 10, W, H, 0);
  const auto br = PixelAt(W - 10, H - 10, W, H, 0);

  REQUIRE(tl.r == 255); REQUIRE(tl.g == 0);   REQUIRE(tl.b == 0);
  REQUIRE(tr.r == 0);   REQUIRE(tr.g == 255); REQUIRE(tr.b == 0);
  REQUIRE(bl.r == 0);   REQUIRE(bl.g == 0);   REQUIRE(bl.b == 255);
  REQUIRE(br.r == 255); REQUIRE(br.g == 255); REQUIRE(br.b == 255);
  REQUIRE(tl.a == 255);
}

TEST_CASE("quadrant colour does not change between frames", "[unit]") {
  constexpr uint32_t W = 640, H = 480;
  REQUIRE(PixelAt(10, 10, W, H, 0).r == PixelAt(10, 10, W, H, 37).r);
}

TEST_CASE("the motion bar translates exactly four pixels per frame", "[unit]") {
  constexpr uint32_t W = 640, H = 480;
  const uint32_t barY = H / 2;

  // The bar is black; find its left edge on two consecutive frames.
  auto findBar = [&](uint32_t frame) -> int {
    for (uint32_t x = 0; x < W; ++x) {
      const auto p = PixelAt(x, barY, W, H, frame);
      if (p.r == 0 && p.g == 0 && p.b == 0) return static_cast<int>(x);
    }
    return -1;
  };

  const int a = findBar(0);
  const int b = findBar(1);
  REQUIRE(a >= 0);
  REQUIRE(b >= 0);
  REQUIRE(b - a == 4);
}

TEST_CASE("the motion bar wraps rather than leaving the frame", "[unit]") {
  constexpr uint32_t W = 640, H = 480;
  const uint32_t barY = H / 2;
  bool found = false;
  for (uint32_t x = 0; x < W && !found; ++x) {
    const auto p = PixelAt(x, barY, W, H, 10'000);
    found = (p.r == 0 && p.g == 0 && p.b == 0);
  }
  REQUIRE(found);
}
