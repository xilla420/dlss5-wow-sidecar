#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/Config.h"
#include "neural/RuntimeSelect.h"

using namespace sidecar;
namespace fs = std::filesystem;

namespace {

fs::path ScratchDir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / "dlss5-runtime-select" / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

void WriteFile(const fs::path& path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << contents;
}

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("With no runtime slots filled nothing is managed", "[unit]") {
  for (const auto mode : {RuntimeMode::Auto, RuntimeMode::Latest, RuntimeMode::Compatible}) {
    CHECK_FALSE(ChooseRuntime(mode, GpuArch::Blackwell, false, false).managed);
    CHECK_FALSE(ChooseRuntime(mode, GpuArch::Ada, false, false).managed);
  }
}

TEST_CASE("Automatic picks the latest on Blackwell and the compatible on Ada", "[unit]") {
  const auto blackwell = ChooseRuntime(RuntimeMode::Auto, GpuArch::Blackwell, true, true);
  CHECK(blackwell.managed);
  CHECK(blackwell.slot == RuntimeMode::Latest);
  CHECK(blackwell.note == nullptr);

  const auto ada = ChooseRuntime(RuntimeMode::Auto, GpuArch::Ada, true, true);
  CHECK(ada.slot == RuntimeMode::Compatible);
  CHECK(ada.note == nullptr);
}

TEST_CASE("Automatic falls back to whichever slot is filled", "[unit]") {
  const auto blackwell = ChooseRuntime(RuntimeMode::Auto, GpuArch::Blackwell, false, true);
  CHECK(blackwell.slot == RuntimeMode::Compatible);
  CHECK(blackwell.note == nullptr);

  // The only build there is will not run, and the operator has to hear why.
  const auto ada = ChooseRuntime(RuntimeMode::Auto, GpuArch::Ada, true, false);
  CHECK(ada.slot == RuntimeMode::Latest);
  CHECK(ada.note != nullptr);
}

TEST_CASE("Latest is never run on Ada when the compatible build is there", "[unit]") {
  const auto ada = ChooseRuntime(RuntimeMode::Latest, GpuArch::Ada, true, true);
  CHECK(ada.slot == RuntimeMode::Compatible);
  CHECK(ada.note != nullptr);

  const auto blackwell = ChooseRuntime(RuntimeMode::Latest, GpuArch::Blackwell, true, true);
  CHECK(blackwell.slot == RuntimeMode::Latest);
  CHECK(blackwell.note == nullptr);
}

TEST_CASE("Compatible is honoured on Blackwell, so the old build stays reachable", "[unit]") {
  const auto choice = ChooseRuntime(RuntimeMode::Compatible, GpuArch::Blackwell, true, true);
  CHECK(choice.slot == RuntimeMode::Compatible);
  CHECK(choice.note == nullptr);
}

TEST_CASE("A missing requested slot falls back and says so", "[unit]") {
  const auto latest = ChooseRuntime(RuntimeMode::Latest, GpuArch::Blackwell, false, true);
  CHECK(latest.slot == RuntimeMode::Compatible);
  CHECK(latest.note != nullptr);

  const auto compatible = ChooseRuntime(RuntimeMode::Compatible, GpuArch::Blackwell, true, false);
  CHECK(compatible.slot == RuntimeMode::Latest);
  CHECK(compatible.note != nullptr);
}

TEST_CASE("Runtime mode tags round-trip and refuse anything else", "[unit]") {
  for (const auto mode : {RuntimeMode::Auto, RuntimeMode::Latest, RuntimeMode::Compatible}) {
    RuntimeMode parsed = RuntimeMode::Auto;
    REQUIRE(ParseRuntimeModeTag(TagForRuntimeMode(mode), parsed));
    CHECK(parsed == mode);
  }
  RuntimeMode untouched = RuntimeMode::Compatible;
  CHECK_FALSE(ParseRuntimeModeTag("newest", untouched));
  CHECK(untouched == RuntimeMode::Compatible);
}

TEST_CASE("The slots are found on disk", "[unit]") {
  const auto dir = ScratchDir("slots");
  CHECK_FALSE(ChooseRuntime(RuntimeMode::Auto, GpuArch::Blackwell, dir).managed);

  WriteFile(RuntimeSlotPath(dir, RuntimeMode::Compatible), "ada");
  auto choice = ChooseRuntime(RuntimeMode::Auto, GpuArch::Blackwell, dir);
  CHECK(choice.managed);
  CHECK(choice.slot == RuntimeMode::Compatible);

  WriteFile(RuntimeSlotPath(dir, RuntimeMode::Latest), "stock");
  choice = ChooseRuntime(RuntimeMode::Auto, GpuArch::Blackwell, dir);
  CHECK(choice.slot == RuntimeMode::Latest);
  fs::remove_all(dir);
}

TEST_CASE("Staging copies the chosen build into place and switches back", "[unit]") {
  const auto dir = ScratchDir("stage");
  const auto latest = RuntimeSlotPath(dir, RuntimeMode::Latest);
  const auto compatible = RuntimeSlotPath(dir, RuntimeMode::Compatible);
  WriteFile(latest, "the latest build");
  WriteFile(compatible, "the compatible build, a different size");

  auto staged = StageRuntime(latest, dir);
  REQUIRE(staged.ok);
  CHECK(staged.copied);
  CHECK(ReadFile(dir / "nvngx_dlssnr.dll") == "the latest build");

  // Already there: nothing to copy, and the 266 MB build is not copied again.
  staged = StageRuntime(latest, dir);
  REQUIRE(staged.ok);
  CHECK_FALSE(staged.copied);

  staged = StageRuntime(compatible, dir);
  REQUIRE(staged.ok);
  CHECK(staged.copied);
  CHECK(ReadFile(dir / "nvngx_dlssnr.dll") == "the compatible build, a different size");
  CHECK_FALSE(fs::exists(dir / "nvngx_dlssnr.dll.staging"));
  fs::remove_all(dir);
}

TEST_CASE("Staging a missing build fails and leaves the old one alone", "[unit]") {
  const auto dir = ScratchDir("missing");
  WriteFile(dir / "nvngx_dlssnr.dll", "whatever was here");
  const auto staged = StageRuntime(RuntimeSlotPath(dir, RuntimeMode::Latest), dir);
  CHECK_FALSE(staged.ok);
  CHECK_FALSE(staged.message.empty());
  CHECK(ReadFile(dir / "nvngx_dlssnr.dll") == "whatever was here");
  fs::remove_all(dir);
}

TEST_CASE("neural_runtime survives a config round trip", "[unit]") {
  Config config;
  config.neuralRuntime = RuntimeMode::Compatible;
  std::vector<std::string> warnings;
  const Config back = ParseConfig(SerializeConfig(config), warnings);
  CHECK(warnings.empty());
  CHECK(back.neuralRuntime == RuntimeMode::Compatible);

  const Config bad = ParseConfig("neural_runtime = \"newest\"\n", warnings);
  CHECK(bad.neuralRuntime == RuntimeMode::Auto);
  CHECK_FALSE(warnings.empty());
}
