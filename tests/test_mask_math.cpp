#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "gpu/MaskMath.h"

using namespace sidecar;

// Pure rectangle arithmetic, unit-tested without a GPU. The spec calls UI
// masking "the largest quality risk in the project", and as with the
// motion-vector conversion the arithmetic is split out so the rule can be
// pinned here and the shader written to match it.

TEST_CASE("A point inside a rectangle is masked", "[unit]") {
  const MaskRect r{10, 20, 30, 40};
  CHECK(Covers(r, 10, 20));
  CHECK(Covers(r, 29, 39));
  CHECK(Covers(r, 20, 30));
}

// Half-open, matching the Win32 RECT convention the calibration UI produces.
// Stating it is the point: a rectangle that is inclusive on one side and
// exclusive on the other leaves a one-pixel seam that is very hard to see and
// very annoying once seen.
TEST_CASE("Rectangle bounds are half-open: left and top in, right and bottom out",
          "[unit]") {
  const MaskRect r{10, 20, 30, 40};
  CHECK(Covers(r, 10, 20));
  CHECK_FALSE(Covers(r, 30, 20));  // right edge excluded
  CHECK_FALSE(Covers(r, 10, 40));  // bottom edge excluded
  CHECK_FALSE(Covers(r, 9, 20));
  CHECK_FALSE(Covers(r, 10, 19));
}

TEST_CASE("An empty or inverted rectangle covers nothing", "[unit]") {
  CHECK_FALSE(Covers(MaskRect{10, 10, 10, 20}, 10, 15));  // zero width
  CHECK_FALSE(Covers(MaskRect{10, 10, 20, 10}, 15, 10));  // zero height
  CHECK_FALSE(Covers(MaskRect{30, 10, 10, 20}, 20, 15));  // inverted
}

TEST_CASE("An empty rectangle list masks nothing", "[unit]") {
  const std::vector<MaskRect> none;
  CHECK_FALSE(CoveredByAny(none, 0, 0));
  CHECK_FALSE(CoveredByAny(none, 100, 100));
}

// Union, not sum. A pixel under two action bars is masked once; anything that
// accumulated coverage would over-darken the overlap in the blend.
TEST_CASE("Overlapping rectangles union rather than double-count", "[unit]") {
  const std::vector<MaskRect> rects{{0, 0, 20, 20}, {10, 10, 30, 30}};
  CHECK(CoveredByAny(rects, 15, 15));  // in both
  CHECK(CoveredByAny(rects, 5, 5));    // in the first only
  CHECK(CoveredByAny(rects, 25, 25));  // in the second only
  CHECK_FALSE(CoveredByAny(rects, 25, 5));
}

TEST_CASE("Rectangles are clamped to the frame", "[unit]") {
  const MaskRect r{-50, -50, 5000, 5000};
  const MaskRect clamped = ClampToFrame(r, 1920, 1080);
  CHECK(clamped.left == 0);
  CHECK(clamped.top == 0);
  CHECK(clamped.right == 1920);
  CHECK(clamped.bottom == 1080);
}

TEST_CASE("A rectangle entirely outside the frame clamps to empty", "[unit]") {
  const MaskRect clamped = ClampToFrame(MaskRect{3000, 3000, 4000, 4000}, 1920, 1080);
  CHECK(IsEmpty(clamped));
}

// The operator calibrates at their desktop resolution, but the pass may run at
// a different internal one (DefaultInternalHeight is 4K on Blackwell, 1440p on
// Ada). A mask that did not scale would drift off the interface entirely.
TEST_CASE("Rectangles scale between resolutions", "[unit]") {
  const MaskRect r{100, 200, 300, 400};
  const MaskRect scaled = ScaleRect(r, 1920, 1080, 3840, 2160);
  CHECK(scaled.left == 200);
  CHECK(scaled.top == 400);
  CHECK(scaled.right == 600);
  CHECK(scaled.bottom == 800);
}

TEST_CASE("Scaling to the same resolution changes nothing", "[unit]") {
  const MaskRect r{100, 200, 300, 400};
  const MaskRect same = ScaleRect(r, 1920, 1080, 1920, 1080);
  CHECK(same.left == r.left);
  CHECK(same.top == r.top);
  CHECK(same.right == r.right);
  CHECK(same.bottom == r.bottom);
}

// Scaling down must not silently erase a thin element. A 1px-tall nameplate
// border halving to 0 would stop being masked at all, which is worse than
// masking one pixel too many.
TEST_CASE("Scaling down never collapses a non-empty rectangle to nothing",
          "[unit]") {
  const MaskRect thin{100, 100, 101, 101};
  const MaskRect scaled = ScaleRect(thin, 3840, 2160, 1920, 1080);
  CHECK_FALSE(IsEmpty(scaled));
  CHECK(scaled.right > scaled.left);
  CHECK(scaled.bottom > scaled.top);
}

TEST_CASE("Scaling is guarded against a zero source resolution", "[unit]") {
  const MaskRect r{10, 10, 20, 20};
  // Nothing sensible to compute; returning the input unchanged is safer than
  // dividing by zero, and the caller's clamp will still bound it.
  const MaskRect scaled = ScaleRect(r, 0, 0, 1920, 1080);
  CHECK(scaled.left == r.left);
  CHECK(scaled.right == r.right);
}

// Feathering: the blend fades across a band inside the rectangle edge so the
// transition between neural output and original pixels is not a hard seam.
TEST_CASE("Coverage is full deep inside a rectangle and zero well outside",
          "[unit]") {
  const MaskRect r{100, 100, 200, 200};
  CHECK(CoverageAt(r, 150, 150, 4) == 1.0f);
  CHECK(CoverageAt(r, 50, 150, 4) == 0.0f);
}

TEST_CASE("Coverage ramps across the feather band", "[unit]") {
  const MaskRect r{100, 100, 200, 200};
  constexpr int kFeather = 4;
  const float atEdge = CoverageAt(r, 100, 150, kFeather);
  const float inside = CoverageAt(r, 102, 150, kFeather);
  const float deep = CoverageAt(r, 150, 150, kFeather);

  CHECK(atEdge < inside);
  CHECK(inside < deep);
  CHECK(atEdge >= 0.0f);
  CHECK(deep == 1.0f);
}

TEST_CASE("A zero feather width gives a hard edge", "[unit]") {
  const MaskRect r{100, 100, 200, 200};
  CHECK(CoverageAt(r, 100, 150, 0) == 1.0f);
  CHECK(CoverageAt(r, 99, 150, 0) == 0.0f);
}

// A rectangle narrower than twice the feather width has no full-strength
// interior. It must still stay within [0,1] rather than overshooting from the
// two edges' bands overlapping.
TEST_CASE("Coverage stays within range on a rectangle thinner than the feather",
          "[unit]") {
  const MaskRect r{100, 100, 104, 104};
  for (int x = 98; x < 106; ++x) {
    for (int y = 98; y < 106; ++y) {
      const float c = CoverageAt(r, x, y, 8);
      INFO("at " << x << "," << y);
      CHECK(c >= 0.0f);
      CHECK(c <= 1.0f);
    }
  }
}

TEST_CASE("Union coverage takes the strongest rectangle", "[unit]") {
  const std::vector<MaskRect> rects{{0, 0, 100, 100}, {90, 90, 200, 200}};
  // Deep inside the second, only just inside the first: the result should be
  // the second's full coverage, not the first's faded edge.
  CHECK(CoverageAtAny(rects, 150, 150, 4) == 1.0f);
  CHECK(CoverageAtAny(rects, 500, 500, 4) == 0.0f);
}
