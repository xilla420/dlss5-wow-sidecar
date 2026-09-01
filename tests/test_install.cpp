#include <catch2/catch_test_macros.hpp>

#include "manager/Install.h"

using namespace sidecar;

namespace {

const Component& ByName(std::string_view installedAs) {
  for (const auto& component : Components()) {
    if (component.installedAs == installedAs) return component;
  }
  FAIL("no such component: " << installedAs);
  return Components().front();
}

}  // namespace

TEST_CASE("every component names a file, a source and a purpose", "[unit]") {
  // A missing dependency that cannot tell the operator where to get it is not a
  // diagnostic, and this list is the only place that information exists.
  for (const auto& component : Components()) {
    REQUIRE_FALSE(component.installedAs.empty());
    REQUIRE_FALSE(component.title.empty());
    REQUIRE_FALSE(component.purpose.empty());
    REQUIRE_FALSE(component.source.empty());
    REQUIRE_FALSE(component.accepts.empty());
  }
}

TEST_CASE("the two nvngx runtimes are never confused for each other", "[unit]") {
  // nvngx_dlss.dll is a prefix of nvngx_dlssnr.dll. They do different jobs, and
  // installing one where the other belongs produces a runtime that loads and
  // then fails at feature creation with no diagnostic at all -- which is
  // exactly the failure this project spent a spike chasing.
  const auto& nr = ByName("nvngx_dlssnr.dll");
  const auto& upscaler = ByName("nvngx_dlss.dll");

  REQUIRE(FileMatchesComponent(nr, "nvngx_dlssnr.dll"));
  REQUIRE_FALSE(FileMatchesComponent(nr, "nvngx_dlss.dll"));
  REQUIRE(FileMatchesComponent(upscaler, "nvngx_dlss.dll"));
  REQUIRE_FALSE(FileMatchesComponent(upscaler, "nvngx_dlssnr.dll"));
}

TEST_CASE("matching ignores case, because Windows does", "[unit]") {
  const auto& nr = ByName("nvngx_dlssnr.dll");
  REQUIRE(FileMatchesComponent(nr, "NVNGX_DLSSNR.DLL"));
  REQUIRE(FileMatchesComponent(nr, "NvNgx_DlssNr.dll"));
}

TEST_CASE("ReShade is accepted under the names it actually ships as", "[unit]") {
  const auto& reshade = ByName("dxgi.dll");
  REQUIRE(FileMatchesComponent(reshade, "dxgi.dll"));
  REQUIRE(FileMatchesComponent(reshade, "ReShade64.dll"));
  REQUIRE(FileMatchesComponent(reshade, "d3d11.dll"));
  REQUIRE_FALSE(FileMatchesComponent(reshade, "ReShade32.dll"));
}

TEST_CASE("a file matching nothing reports no component", "[unit]") {
  REQUIRE(ComponentForFile("readme.txt") == static_cast<size_t>(-1));
  REQUIRE(ComponentForFile("") == static_cast<size_t>(-1));
  REQUIRE(ComponentForFile("renodx-dlss5.addon64") != static_cast<size_t>(-1));
}

TEST_CASE("the uninstall plan lists nothing for an empty directory", "[unit]") {
  // Whatever the temporary directory holds, it is not the sidecar's files, so
  // the plan has to come back empty -- an uninstaller that finds work to do in
  // a directory it was never installed into is the dangerous kind of bug.
  const auto plan = UninstallPlan(std::filesystem::temp_directory_path() /
                                      "dlss5-sidecar-not-installed-here",
                                  true);
  REQUIRE(plan.empty());
}
