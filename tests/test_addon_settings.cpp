#include <catch2/catch_test_macros.hpp>
#include <string>

#include "neural/AddonSettings.h"

using namespace sidecar;

namespace {

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// How many times a key appears. The interesting failure is a settings write
// that appends a second copy of a key instead of replacing the first, because
// the add-on reads one of them and it is not obvious which.
size_t CountOf(const std::string& haystack, const std::string& needle) {
  size_t count = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos;
       at = haystack.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

}  // namespace

TEST_CASE("an empty file gains both sections the add-on needs", "[unit]") {
  NeuralSettings settings;
  const std::string ini = ApplyNeuralSettings("", settings);

  REQUIRE(Contains(ini, "[ADDON]"));
  REQUIRE(Contains(ini, "AddonPath=."));
  REQUIRE(Contains(ini, "[RenoDX.DLSS5]"));
  REQUIRE(Contains(ini, "EnableHooks=2"));
  REQUIRE(Contains(ini, "NRIntensity=1.000000"));
}

TEST_CASE("existing keys are replaced rather than duplicated", "[unit]") {
  const std::string before =
      "[RenoDX.DLSS5]\r\n"
      "EnableHooks=0\r\n"
      "NRIntensity=0.100000\r\n";

  NeuralSettings settings;
  settings.enableHooks = 2;
  settings.intensity = 0.75f;
  const std::string after = ApplyNeuralSettings(before, settings);

  REQUIRE(CountOf(after, "EnableHooks=") == 1);
  REQUIRE(CountOf(after, "NRIntensity=") == 1);
  REQUIRE(Contains(after, "EnableHooks=2"));
  REQUIRE(Contains(after, "NRIntensity=0.750000"));
  REQUIRE_FALSE(Contains(after, "NRIntensity=0.100000"));
}

TEST_CASE("sections we do not own survive untouched", "[unit]") {
  // ReShade owns most of this file, and the operator may have set things
  // through its own overlay. Losing any of that to a slider move would be a
  // silent, confusing regression.
  const std::string before =
      "[GENERAL]\r\n"
      "EffectSearchPaths=.\\reshade-shaders\\Shaders\r\n"
      "\r\n"
      "[OVERLAY]\r\n"
      "TutorialProgress=4\r\n";

  const std::string after = ApplyNeuralSettings(before, NeuralSettings{});

  REQUIRE(Contains(after, "[GENERAL]"));
  REQUIRE(Contains(after, "EffectSearchPaths=.\\reshade-shaders\\Shaders"));
  REQUIRE(Contains(after, "[OVERLAY]"));
  REQUIRE(Contains(after, "TutorialProgress=4"));
  REQUIRE(Contains(after, "[RenoDX.DLSS5]"));
}

TEST_CASE("a key added to an existing section lands inside it", "[unit]") {
  const std::string before =
      "[RenoDX.DLSS5]\r\n"
      "EnableHooks=2\r\n"
      "\r\n"
      "[OVERLAY]\r\n"
      "TutorialProgress=4\r\n";

  const std::string after = ApplyNeuralSettings(before, NeuralSettings{});

  // NRIntensity was not in the file, so it had to be inserted. It must land
  // above [OVERLAY]; below it, the add-on would never see it and ReShade would
  // read a key it does not know.
  const size_t intensity = after.find("NRIntensity=");
  const size_t overlay = after.find("[OVERLAY]");
  REQUIRE(intensity != std::string::npos);
  REQUIRE(overlay != std::string::npos);
  REQUIRE(intensity < overlay);
}

TEST_CASE("a byte-order mark is carried, not parsed", "[unit]") {
  const std::string before = "\xEF\xBB\xBF[ADDON]\r\nAddonPath=.\r\n";
  const std::string after = ApplyNeuralSettings(before, NeuralSettings{});

  REQUIRE(after.compare(0, 3, "\xEF\xBB\xBF") == 0);
  REQUIRE(CountOf(after, "[ADDON]") == 1);
  REQUIRE(CountOf(after, "AddonPath=") == 1);
}

TEST_CASE("the file's own line ending is kept", "[unit]") {
  const std::string lf = ApplyNeuralSettings("[ADDON]\nAddonPath=.\n", NeuralSettings{});
  REQUIRE(lf.find("\r\n") == std::string::npos);

  const std::string crlf = ApplyNeuralSettings("[ADDON]\r\nAddonPath=.\r\n", NeuralSettings{});
  REQUIRE(crlf.find("\r\n") != std::string::npos);
}

TEST_CASE("unset strengths are omitted rather than guessed", "[unit]") {
  NeuralSettings settings;   // the three optional strengths default to -1
  const std::string omitted = ApplyNeuralSettings("", settings);
  REQUIRE_FALSE(Contains(omitted, "NRLocalStructure"));
  REQUIRE_FALSE(Contains(omitted, "NRLocalTone"));
  REQUIRE_FALSE(Contains(omitted, "NRSkinStructure"));

  settings.localStructure = 0.5f;
  const std::string written = ApplyNeuralSettings("", settings);
  REQUIRE(Contains(written, "NRLocalStructure=0.500000"));
  REQUIRE_FALSE(Contains(written, "NRLocalTone"));
}

TEST_CASE("applying twice changes nothing the second time", "[unit]") {
  const NeuralSettings settings;
  const std::string once = ApplyNeuralSettings("[OVERLAY]\r\nTutorialProgress=4\r\n", settings);
  const std::string twice = ApplyNeuralSettings(once, settings);
  REQUIRE(once == twice);
}
