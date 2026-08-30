#include <catch2/catch_test_macros.hpp>
#include "core/GpuProfile.h"

using namespace sidecar;

constexpr uint32_t kNvidia = 0x10DE;

TEST_CASE("Blackwell device ids map to Blackwell", "[unit]") {
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2B85) == GpuArch::Blackwell);  // RTX 5090
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x2C02) == GpuArch::Blackwell);  // RTX 5080
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
}

TEST_CASE("unknown NVIDIA device ids are unsupported rather than guessed", "[unit]") {
  REQUIRE(ArchitectureFromDeviceId(kNvidia, 0x0001) == GpuArch::Unsupported);
}

TEST_CASE("default internal height follows the spec GPU matrix", "[unit]") {
  REQUIRE(DefaultInternalHeight(GpuArch::Blackwell) == 2160);
  REQUIRE(DefaultInternalHeight(GpuArch::Ada) == 1440);
  REQUIRE(DefaultInternalHeight(GpuArch::Ampere) == 0);
  REQUIRE(DefaultInternalHeight(GpuArch::Unsupported) == 0);
}
