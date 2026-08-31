#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>
#include "core/Sha256.h"
#include "manager/Probes.h"
#include "neural/RuntimeManifest.h"

using namespace sidecar;

TEST_CASE("I7: WoW install paths are recognised and refused", "[unit]") {
  REQUIRE(PathLooksLikeWowInstall("C:/Program Files/World of Warcraft/_retail_"));
  REQUIRE(PathLooksLikeWowInstall("D:/Games/WoW/_classic_"));
  REQUIRE(PathLooksLikeWowInstall("D:/Games/WoW/_classic_era_"));
  REQUIRE(PathLooksLikeWowInstall("E:/wow/Wow.exe"));
}

TEST_CASE("I7: ordinary paths are allowed", "[unit]") {
  REQUIRE(PathLooksLikeWowInstall("C:/Tools/dlss5-sidecar") == false);
  REQUIRE(PathLooksLikeWowInstall("D:/Games/Skyrim") == false);
}

TEST_CASE("I7: matching is case-insensitive", "[unit]") {
  REQUIRE(PathLooksLikeWowInstall("C:/Games/WORLD OF WARCRAFT/_RETAIL_"));
  REQUIRE(PathLooksLikeWowInstall("C:/games/wow/wow.exe"));
}

TEST_CASE("I8: every known injector loader name is detected", "[unit]") {
  const auto found = FindInjectorLoaders({
      "Wow.exe", "dxgi.dll", "Data", "ReShade.ini", "WowVoiceProxy.exe"});
  REQUIRE(found.size() == 2);
  REQUIRE(std::find(found.begin(), found.end(), "dxgi.dll") != found.end());
  REQUIRE(std::find(found.begin(), found.end(), "ReShade.ini") != found.end());
}

TEST_CASE("I8: all seven loader filenames are covered", "[unit]") {
  for (const char* name : {"dxgi.dll", "d3d12.dll", "d3d11.dll", "dinput8.dll",
                           "winmm.dll", "version.dll", "opengl32.dll"}) {
    INFO("loader: " << name);
    REQUIRE(FindInjectorLoaders({name}).size() == 1);
  }
}

TEST_CASE("I8: a clean install directory yields nothing", "[unit]") {
  const auto found = FindInjectorLoaders({"Wow.exe", "Data", "Logs", "WTF", "Interface"});
  REQUIRE(found.empty());
}

TEST_CASE("I8: detection is case-insensitive", "[unit]") {
  REQUIRE(FindInjectorLoaders({"DXGI.DLL"}).size() == 1);
  REQUIRE(FindInjectorLoaders({"reshade.ini"}).size() == 1);
}

TEST_CASE("I9: a sidecar directory inside the WoW tree fails", "[unit]") {
  const auto r = ProbeSidecarPath("C:/Games/WoW/_retail_/sidecar", "C:/Games/WoW");
  REQUIRE(r.state == ProbeState::Fail);
  REQUIRE(r.remedy.empty() == false);
}

TEST_CASE("I9: a sidecar directory outside the WoW tree passes", "[unit]") {
  const auto r = ProbeSidecarPath("C:/Tools/dlss5-sidecar", "C:/Games/WoW");
  REQUIRE(r.state == ProbeState::Ok);
}

// M3 Task 5, step 3: the neural-runtime probe stops being a presence check.
//
// These use the real filesystem because that is the thing under test -- a
// hash-verifying probe that never opens a file would pass its own tests and
// still be useless. Each writes into a directory it owns and cleans up.

namespace {

std::filesystem::path ProbeScratchDir(const char* name) {
  auto dir = std::filesystem::temp_directory_path() / "sidecar_probe_tests" / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void WriteFile(const std::filesystem::path& path, std::string_view contents) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << contents;
}

}  // namespace

TEST_CASE("Neural runtime probe warns when the runtime is absent", "[unit]") {
  const auto dir = ProbeScratchDir("absent");
  const auto r = ProbeNeuralRuntime(dir);
  CHECK(r.state == ProbeState::Warn);
  CHECK(r.remedy.empty() == false);
  std::filesystem::remove_all(dir);
}

// An unrecognised digest is amber, never red: the operator may legitimately
// have a newer build than the manifest knows. But the message has to name the
// digest, or they cannot tell us which build it was.
TEST_CASE("Neural runtime probe reports an unknown build as amber with its digest",
          "[unit]") {
  const auto dir = ProbeScratchDir("unknown");
  WriteFile(dir / "nvngx_dlssnr.dll", "not really a runtime");
  const auto r = ProbeNeuralRuntime(dir);
  CHECK(r.state == ProbeState::Warn);
  CHECK(r.remedy.empty() == false);
  // "not really a runtime" hashed with SHA-256.
  CHECK(r.detail.find(Sha256Hex("not really a runtime")) != std::string::npos);
  std::filesystem::remove_all(dir);
}

// The recognised path, tested through the pure verdict. It cannot be reached by
// writing a file, because that would mean producing input that hashes to a
// manifest entry.
TEST_CASE("Neural runtime verdict accepts a known build and names its version",
          "[unit]") {
  const auto r = NeuralRuntimeVerdict(
      "nvngx_dlssnr.dll",
      "e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e");
  CHECK(r.state == ProbeState::Ok);
  CHECK(r.detail.find("310.8.0") != std::string::npos);
  // An Ok row needs no remedy; there is nothing to remedy.
  CHECK(r.remedy.empty());
}

TEST_CASE("Neural runtime verdict warns, with the digest, on an unknown build",
          "[unit]") {
  const std::string digest(64, 'c');
  const auto r = NeuralRuntimeVerdict("nvngx_dlssnr.dll", digest);
  CHECK(r.state == ProbeState::Warn);
  CHECK(r.detail.find(digest) != std::string::npos);
  CHECK(r.remedy.empty() == false);
}

TEST_CASE("Neural runtime verdict treats an unreadable file as its own case",
          "[unit]") {
  const auto r = NeuralRuntimeVerdict("nvngx_dlssnr.dll", "");
  CHECK(r.state == ProbeState::Warn);
  CHECK(r.detail.find("could not be read") != std::string::npos);
}

// Task 4 established that ReShade ships as a proxy DLL named after the API it
// fronts -- for a process that imports dxgi, that is dxgi.dll. ReShade64.dll is
// not what an install actually produces, so probing for it always failed.
TEST_CASE("ReShade probe recognises the proxy DLL an install actually produces",
          "[unit]") {
  const auto dir = ProbeScratchDir("reshade");
  WriteFile(dir / "dxgi.dll", "stand-in");
  const auto r = ProbeReshade(dir);
  CHECK(r.state == ProbeState::Ok);
  std::filesystem::remove_all(dir);
}

TEST_CASE("ReShade probe warns when no host is present", "[unit]") {
  const auto dir = ProbeScratchDir("noreshade");
  const auto r = ProbeReshade(dir);
  CHECK(r.state == ProbeState::Warn);
  CHECK(r.remedy.empty() == false);
  std::filesystem::remove_all(dir);
}

TEST_CASE("every probe result carries a remedy when it is not Ok", "[unit]") {
  const auto results = RunAllProbes("C:/Tools/dlss5-sidecar", "");
  REQUIRE(results.empty() == false);
  for (const auto& r : results) {
    INFO("probe: " << r.title);
    REQUIRE(r.title.empty() == false);
    if (r.state != ProbeState::Ok) REQUIRE(r.remedy.empty() == false);
  }
}
