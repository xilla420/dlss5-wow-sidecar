#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "flow/MotionVectorMath.h"

using namespace sidecar;
using Catch::Matchers::WithinAbs;

TEST_CASE("S10.5 decodes to pixels at the documented scale", "[unit]") {
  // 32 raw units == 1 pixel.
  REQUIRE_THAT(FlowToMotionPixels(32, 0).x, WithinAbs(1.0, 1e-6));
  REQUIRE_THAT(FlowToMotionPixels(128, 0).x, WithinAbs(4.0, 1e-6));
  REQUIRE_THAT(FlowToMotionPixels(0, 64).y, WithinAbs(2.0, 1e-6));
}

TEST_CASE("direction is kept: NVOFA and NGX both point at the previous frame", "[unit]") {
  // NVOFA (input = current, reference = previous) reports where a pixel was a
  // frame ago, and NGX reads history at uv + mv. Negating here is the temporal
  // ghosting bug of 0.4.0/0.5.0.
  REQUIRE(FlowToMotionPixels(128, 0).x > 0.0f);
  REQUIRE(FlowToMotionPixels(-128, 0).x < 0.0f);
}

TEST_CASE("the diagnostic inversion flips both axes and nothing else", "[unit]") {
  const auto inverted = FlowToMotionPixels(128, -64, true);
  REQUIRE_THAT(inverted.x, WithinAbs(-4.0, 1e-6));
  REQUIRE_THAT(inverted.y, WithinAbs(2.0, 1e-6));
}

TEST_CASE("zero flow is exactly zero motion, with no sign flip", "[unit]") {
  const auto zero = FlowToMotionPixels(0, 0);
  REQUIRE(zero.x == 0.0f);
  REQUIRE(zero.y == 0.0f);
}

TEST_CASE("pixel motion converts to NDC against the full-resolution extent", "[unit]") {
  const MotionVec pixels{8.0f, -4.0f};
  const auto ndc = MotionPixelsToNdc(pixels, 1920, 1080);
  REQUIRE_THAT(ndc.x, WithinAbs(8.0 / 1920.0, 1e-9));
  REQUIRE_THAT(ndc.y, WithinAbs(-4.0 / 1080.0, 1e-9));
}

TEST_CASE("the test pattern's four-pixel bar round-trips end to end", "[unit]") {
  // The bar moves +4 px per frame. A bar pixel now at x = 100 was at x = 96,
  // NVOFA reports that as raw -128, and NGX must be sent -4 px so that history
  // is read at 100 + (-4) = 96 -- where the bar actually was.
  const auto motion = FlowToMotionPixels(-128, 0);
  REQUIRE_THAT(motion.x, WithinAbs(-4.0, 1e-6));
  REQUIRE_THAT(HistoryPosition(MotionVec{100.0f, 50.0f}, motion).x, WithinAbs(96.0, 1e-6));
}

TEST_CASE("the extremes of the S10.5 range do not overflow", "[unit]") {
  const auto maxPositive = FlowToMotionPixels(32767, 32767);
  REQUIRE_THAT(maxPositive.x, WithinAbs(1023.96875, 1e-4));
  const auto maxNegative = FlowToMotionPixels(-32768, -32768);
  REQUIRE_THAT(maxNegative.x, WithinAbs(-1024.0, 1e-4));
}
