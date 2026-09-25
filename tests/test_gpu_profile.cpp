#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "core/GpuProfile.h"

using namespace sidecar;

// M4's half of the GPU matrix. DefaultInternalHeight has existed since M0 and
// was read by nothing; this is what finally consumes it.

TEST_CASE("capture at or below the ceiling is used unchanged", "[unit]") {
  // 1440p on Ada is exactly the ceiling: DLAA, not upscaling.
  const auto ada = InternalRenderSize(2560, 1440, GpuArch::Ada);
  CHECK(ada.width == 2560);
  CHECK(ada.height == 1440);

  // Below it, the pass must not upscale something that was never downscaled.
  const auto small = InternalRenderSize(1920, 1080, GpuArch::Ada);
  CHECK(small.width == 1920);
  CHECK(small.height == 1080);

  const auto blackwell = InternalRenderSize(3840, 2160, GpuArch::Blackwell);
  CHECK(blackwell.width == 3840);
  CHECK(blackwell.height == 2160);
}

// The reason the ceiling differs per architecture: neural rendering measured
// ~8 ms at 1440p on Ada, so running it at 4K there would leave the frame budget
// somewhere it cannot recover from.
TEST_CASE("capture above the ceiling is scaled down to it", "[unit]") {
  const auto ada4k = InternalRenderSize(3840, 2160, GpuArch::Ada);
  CHECK(ada4k.height == 1440);
  CHECK(ada4k.width == 2560);   // 16:9 preserved
}

TEST_CASE("aspect ratio survives scaling on unusual displays", "[unit]") {
  // 21:9 at 5120x2160 on Ada -> 1440 tall, width scaled to match.
  // 5120 * 1440 / 2160 = 3413.8, which rounds to 3413 and then down to the
  // nearest even pixel.
  const auto ultrawide = InternalRenderSize(5120, 2160, GpuArch::Ada);
  CHECK(ultrawide.height == 1440);
  CHECK(ultrawide.width == 3412);
  // Whatever the rounding, the ratio must not drift more than a pixel's worth.
  const double sourceRatio = 5120.0 / 2160.0;
  const double resultRatio = static_cast<double>(ultrawide.width) / ultrawide.height;
  CHECK(std::abs(sourceRatio - resultRatio) < 0.01);
}

TEST_CASE("both axes stay even", "[unit]") {
  for (uint32_t h : {2161u, 3000u, 4321u}) {
    const auto size = InternalRenderSize(h * 16 / 9, h, GpuArch::Ada);
    INFO("capture height " << h);
    CHECK(size.width % 2 == 0);
    CHECK(size.height % 2 == 0);
  }
}

// An architecture outside the matrix has no ceiling, and the pass refuses to
// run there anyway. Inventing a resolution policy for it would be inventing one
// for a case that cannot occur.
TEST_CASE("unsupported architectures pass the capture size through", "[unit]") {
  const auto turing = InternalRenderSize(3840, 2160, GpuArch::Turing);
  CHECK(turing.width == 3840);
  CHECK(turing.height == 2160);
}

TEST_CASE("a degenerate capture size yields nothing", "[unit]") {
  CHECK(InternalRenderSize(0, 1080, GpuArch::Ada).height == 0);
  CHECK(InternalRenderSize(1920, 0, GpuArch::Ada).width == 0);
}

constexpr uint32_t kNvidia = 0x10DE;

// Every desktop RTX 50 part the NVIDIA display driver publishes, so the table
// is checked against the whole shipped line rather than against the two cards
// that happened to be to hand. The list is read off the driver's own INF; that
// is what decides which ids exist, not this file.
TEST_CASE("Blackwell device ids map to Blackwell", "[unit]") {
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2B85) == GpuArch::Blackwell);  // RTX 5090
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2B87) == GpuArch::Blackwell);  // RTX 5090 D
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2B8C) == GpuArch::Blackwell);  // RTX 5090 D v2
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2C02) == GpuArch::Blackwell);  // RTX 5080
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2C05) == GpuArch::Blackwell);  // RTX 5070 Ti
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2D04) == GpuArch::Blackwell);  // RTX 5060 Ti
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2D05) == GpuArch::Blackwell);  // RTX 5060
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2D83) == GpuArch::Blackwell);  // RTX 5050
  // A second RTX 5060 part, and the one the bug report's table left out: it
  // sits in the same block as the 5070 rather than beside the other 5060, so
  // it was falling through to Unsupported as well.
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2F06) == GpuArch::Blackwell);  // RTX 5060
}

// The RTX 5070 is the one part that does not continue the block its siblings
// sit in: GB205 is 0x2F04, past the 0x2C00-0x2DFF range that holds everything
// from the 5080 down to the 5050. The gap read as Unsupported, so the sidecar
// refused to run on a card it was built for.
TEST_CASE("the RTX 5070 is Blackwell despite sitting outside its siblings' block",
          "[unit]") {
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2F04) == GpuArch::Blackwell);
  REQUIRE(DefaultInternalHeight(ArchitectureFromDeviceId(kNvidia, 0x2F04)) == 2160);

  // Two cards live in that block, not one. The report that found this named
  // only the 5070 and called it the sole missing part; the driver's own INF
  // lists 0x2F06 there too. Both were refused, and a fix checked against the
  // report alone would have been tested one card short.
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2F06) == GpuArch::Blackwell);
  REQUIRE(DefaultInternalHeight(ArchitectureFromDeviceId(kNvidia, 0x2F06)) == 2160);
}

TEST_CASE("Ada device ids map to Ada", "[unit]") {
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2684) == GpuArch::Ada);  // RTX 4090
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2704) == GpuArch::Ada);  // RTX 4080
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2786) == GpuArch::Ada);  // RTX 4070
}

TEST_CASE("Ampere and Turing are recognised but not supported", "[unit]") {
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2204) == GpuArch::Ampere);  // RTX 3090
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x1E04) == GpuArch::Turing);  // RTX 2080 Ti
}

TEST_CASE("non-NVIDIA vendors are unsupported regardless of device id", "[unit]") {
  REQUIRE(ArchitectureFromDeviceId(0x1002, 0x744C) == GpuArch::Unsupported);  // AMD
  REQUIRE(ArchitectureFromDeviceId(0x8086, 0x56A0) == GpuArch::Unsupported);  // Intel
  // The vendor check must still come first for the range added for GB205:
  // another vendor reusing 0x2F04 is not an RTX 5070.
  REQUIRE(ArchitectureFromDeviceId(0x1002, 0x2F04) == GpuArch::Unsupported);
}

TEST_CASE("unknown NVIDIA device ids are unsupported rather than guessed", "[unit]") {
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x0001) == GpuArch::Unsupported);
  // 0x2E00-0x2EFF lies between the two Blackwell blocks and nothing published
  // occupies it. Widening the table into it to close the hole would be exactly
  // the guess the ranges are kept narrow to prevent, because a wrong guess
  // selects the wrong neural runtime variant.
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2E04) == GpuArch::Unsupported);
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2A00) == GpuArch::Unsupported);
}

TEST_CASE("default internal height follows the spec GPU matrix", "[unit]") {
  REQUIRE(DefaultInternalHeight(GpuArch::Blackwell) == 2160);
  REQUIRE(DefaultInternalHeight(GpuArch::Ada) == 1440);
  REQUIRE(DefaultInternalHeight(GpuArch::Ampere) == 0);
  REQUIRE(DefaultInternalHeight(GpuArch::Unsupported) == 0);
}
