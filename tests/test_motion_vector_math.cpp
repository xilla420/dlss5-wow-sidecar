#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "flow/MotionVectorMath.h"

using namespace sidecar;
using Catch::Matchers::WithinAbs;

TEST_CASE("S10.5 decodes to pixels at the documented scale", "[unit]") {
  // 32 raw units == 1 pixel.
  REQUIRE_THAT(FlowToMotionPixels(32, 0).x, WithinAbs(-1.0, 1e-6));
  REQUIRE_THAT(FlowToMotionPixels(128, 0).x, WithinAbs(-4.0, 1e-6));
  REQUIRE_THAT(FlowToMotionPixels(0, 64).y, WithinAbs(-2.0, 1e-6));
}

TEST_CASE("direction is negated, because NVOFA and NGX disagree on it", "[unit]") {
  // NVOFA reports where a pixel came from; NGX wants where it is going.
  // Getting this backwards is the smearing bug the spec warns about.
  const auto forward = FlowToMotionPixels(128, 0);
  REQUIRE(forward.x < 0.0f);

  const auto backward = FlowToMotionPixels(-128, 0);
  REQUIRE(backward.x > 0.0f);
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
  // The bar moves +4 px per frame, so NVOFA reports the previous position at
  // -4 px, which is raw -128. NGX must receive +4 px.
  const auto motion = FlowToMotionPixels(-128, 0);
  REQUIRE_THAT(motion.x, WithinAbs(4.0, 1e-6));
}

TEST_CASE("the extremes of the S10.5 range do not overflow", "[unit]") {
  const auto maxPositive = FlowToMotionPixels(32767, 32767);
  REQUIRE_THAT(maxPositive.x, WithinAbs(-1023.96875, 1e-4));
  const auto maxNegative = FlowToMotionPixels(-32768, -32768);
  REQUIRE_THAT(maxNegative.x, WithinAbs(1024.0, 1e-4));
}
