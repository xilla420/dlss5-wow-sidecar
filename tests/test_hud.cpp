#include <catch2/catch_test_macros.hpp>
#include "present/Hud.h"

using namespace sidecar;

// M4: which runtime build is live has to be visible, because two builds share a
// version string and a byte count and only one of them runs on Ada.
TEST_CASE("the HUD names the live runtime variant", "[unit]") {
  HudModel model;
  model.gpuName = "RTX 4080";
  model.passName = "reshade-hosted DLSS 5 NR";
  model.runtimeVariant = "Ada-patched";
  const std::string text = FormatHud(model);
  CHECK(text.find("reshade-hosted DLSS 5 NR") != std::string::npos);
  CHECK(text.find("Ada-patched") != std::string::npos);
}

// Passthrough has no runtime behind it, so the field must vanish rather than
// leave an empty bracket on screen.
TEST_CASE("the HUD omits the variant when there is none", "[unit]") {
  HudModel model;
  model.gpuName = "RTX 4080";
  model.passName = "passthrough";
  const std::string text = FormatHud(model);
  CHECK(text.find("passthrough") != std::string::npos);
  CHECK(text.find("[") == std::string::npos);
}

TEST_CASE("hud line carries every number the gate decision needs", "[unit]") {
  HudModel m;
  m.p50Ms = 21.4; m.p99Ms = 33.8;
  m.frames = 1234; m.drops = 7;
  m.passName = "passthrough";
  m.gpuName = "RTX 4080";

  const std::string line = FormatHud(m);
  REQUIRE(line.find("21.4") != std::string::npos);
  REQUIRE(line.find("33.8") != std::string::npos);
  REQUIRE(line.find("1234") != std::string::npos);
  REQUIRE(line.find("7") != std::string::npos);
  REQUIRE(line.find("passthrough") != std::string::npos);
  REQUIRE(line.find("RTX 4080") != std::string::npos);
}

TEST_CASE("gate verdict follows the spec M1 threshold", "[unit]") {
  REQUIRE(JudgeGate(12.0) == GateVerdict::Playable);
  REQUIRE(JudgeGate(39.9) == GateVerdict::Playable);
  REQUIRE(JudgeGate(40.0) == GateVerdict::Marginal);
  REQUIRE(JudgeGate(79.9) == GateVerdict::Marginal);
  REQUIRE(JudgeGate(80.0) == GateVerdict::Failed);
  REQUIRE(JudgeGate(250.0) == GateVerdict::Failed);
}

TEST_CASE("hud line states the verdict in words", "[unit]") {
  HudModel m;
  m.passName = "passthrough";
  m.gpuName = "RTX 4080";

  m.p99Ms = 20.0;
  REQUIRE(FormatHud(m).find("playable") != std::string::npos);
  m.p99Ms = 60.0;
  REQUIRE(FormatHud(m).find("marginal") != std::string::npos);
  m.p99Ms = 120.0;
  REQUIRE(FormatHud(m).find("too slow") != std::string::npos);
}
