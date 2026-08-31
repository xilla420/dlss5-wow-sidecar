#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"
#include "neural/NeuralPassFactory.h"

using namespace sidecar;

TEST_CASE("the default pass name builds passthrough without complaint", "[unit]") {
  std::vector<std::string> warnings;
  auto pass = MakeNeuralPass("passthrough", warnings);
  REQUIRE(pass != nullptr);
  REQUIRE(std::string(pass->Name()) == "passthrough");
  REQUIRE(warnings.empty());
}

// "reshade" is real now, but it needs a device. Asked without one it must say
// so rather than pretending, because the caller that forgot the device would
// otherwise silently get passthrough and no clue why.
TEST_CASE("the reshade pass reports that it needs a device", "[unit]") {
  std::vector<std::string> warnings;
  auto pass = MakeNeuralPass("reshade", warnings);
  REQUIRE(pass != nullptr);
  REQUIRE(std::string(pass->Name()) == "passthrough");
  REQUIRE(warnings.size() == 1);
  REQUIRE(warnings[0].find("reshade") != std::string::npos);
  REQUIRE(warnings[0].find("device") != std::string::npos);
}

// Route A is closed: the M3 spikes established that the neural-rendering
// runtime refuses an NGX session set up by anything but the NGX core, through
// both the core's registry and the runtime's own exports.
TEST_CASE("the ngx pass reports that it is unreachable", "[unit]") {
  std::vector<std::string> warnings;
  auto pass = MakeNeuralPass("ngx", warnings);
  REQUIRE(pass != nullptr);
  REQUIRE(std::string(pass->Name()) == "passthrough");
  REQUIRE(warnings.size() == 1);
  REQUIRE(warnings[0].find("ngx") != std::string::npos);
}

// With a device but no runtime beside it, the failure has to name a reason an
// operator can act on -- this is the path anyone without the optional runtime
// takes, so it is the one worth pinning.
TEST_CASE("the reshade pass explains itself when the runtime is missing",
          "[device]") {
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }
  auto bridge = DeviceBridge::Create(gpu->luid, 256, 256);
  REQUIRE(bridge != nullptr);

  NeuralPassContext ctx;
  ctx.device = bridge->D3d12();
  ctx.runtimeDir = std::filesystem::current_path();
  ctx.width = 256;
  ctx.height = 256;

  std::vector<std::string> warnings;
  auto pass = MakeNeuralPass("reshade", ctx, warnings);
  REQUIRE(pass != nullptr);
  INFO("warning: " << (warnings.empty() ? std::string("<none>") : warnings[0]));
  // Either it came up -- a runtime really is present -- or it explained why not.
  if (std::string(pass->Name()) == "passthrough") {
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("reshade") != std::string::npos);
    REQUIRE(warnings[0].length() > std::string("neural_pass \"reshade\"").length());
  } else {
    REQUIRE(warnings.empty());
  }
}

TEST_CASE("an unknown pass name never returns null", "[unit]") {
  // Spec section 11: a bad neural runtime degrades to passthrough rather than
  // refusing to start. A typo must not cost the operator their overlay.
  std::vector<std::string> warnings;
  auto pass = MakeNeuralPass("dlss6-ultra", warnings);
  REQUIRE(pass != nullptr);
  REQUIRE(std::string(pass->Name()) == "passthrough");
  REQUIRE(warnings.size() == 1);
  REQUIRE(warnings[0].find("dlss6-ultra") != std::string::npos);
}

TEST_CASE("an empty pass name still yields a usable pass", "[unit]") {
  std::vector<std::string> warnings;
  auto pass = MakeNeuralPass("", warnings);
  REQUIRE(pass != nullptr);
  REQUIRE(warnings.size() == 1);
}
