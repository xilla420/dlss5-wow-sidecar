#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "manager/Probes.h"

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

TEST_CASE("every probe result carries a remedy when it is not Ok", "[unit]") {
  const auto results = RunAllProbes("C:/Tools/dlss5-sidecar", "");
  REQUIRE(results.empty() == false);
  for (const auto& r : results) {
    INFO("probe: " << r.title);
    REQUIRE(r.title.empty() == false);
    if (r.state != ProbeState::Ok) REQUIRE(r.remedy.empty() == false);
  }
}
