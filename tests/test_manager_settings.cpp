#include <catch2/catch_test_macros.hpp>
#include "core/Config.h"
#include "manager/Presets.h"

using namespace sidecar;

TEST_CASE("interface mode migrates old configs and rejects malformed values", "[unit]") {
  std::vector<std::string> warnings;
  REQUIRE_FALSE(ParseConfig("", warnings).advancedMode);
  REQUIRE(warnings.empty());
  REQUIRE(ParseConfig("advanced_mode = true", warnings).advancedMode);
  REQUIRE(warnings.empty());
  REQUIRE_FALSE(ParseConfig("advanced_mode = \"yes\"", warnings).advancedMode);
  REQUIRE(warnings.size() == 1);
}

TEST_CASE("switching interface modes preserves custom rendering across saves", "[unit]") {
  Config config;
  config.neural.localStructure = 1.25f;
  config.neuralPasses = 3;
  config.flowGridSize = 2;
  config.hotkeys.startStop = "Ctrl+Alt+F9";
  config.wowDir = "D:\\Games\\World of Warcraft\\_retail_";
  for (bool advanced : {true, false}) {
    config.advancedMode = advanced;
    std::vector<std::string> warnings;
    config = ParseConfig(SerializeConfig(config), warnings);
    REQUIRE(warnings.empty());
    REQUIRE(config.advancedMode == advanced);
    REQUIRE(config.neural.localStructure == 1.25f);
    REQUIRE(config.neuralPasses == 3);
    REQUIRE(config.flowGridSize == 2);
    REQUIRE(config.hotkeys.startStop == "Ctrl+Alt+F9");
    REQUIRE(config.wowDir == "D:\\Games\\World of Warcraft\\_retail_");
  }
}

TEST_CASE("render reset preserves installation preferences and calibration", "[unit]") {
  Config config;
  config.language = "ja";
  config.theme = "questlog";
  config.advancedMode = true;
  config.uiScale = 1.5f;
  config.wowDir = "D:\\WoW";
  config.hotkeys.toggleHud = "Ctrl+Alt+F8";
  config.uiMaskRects.push_back({0, 900, 1920, 1080});
  config.uiMaskFeather = 8;
  config.neuralPasses = 3;
  config.neural.intensity = 0.2f;
  config.neural.enableHooks = 0;
  ResetRenderingSettings(config);
  REQUIRE(config.language == "ja");
  REQUIRE(config.theme == "questlog");
  REQUIRE(config.advancedMode);
  REQUIRE(config.uiScale == 1.5f);
  REQUIRE(config.wowDir == "D:\\WoW");
  REQUIRE(config.hotkeys.toggleHud == "Ctrl+Alt+F8");
  REQUIRE(config.uiMaskRects.size() == 1);
  REQUIRE(config.uiMaskRects[0].bottom == 1080);
  REQUIRE(config.uiMaskFeather == 8);
  REQUIRE(config.neuralPasses == 1);
  REQUIRE(config.neural.intensity == 1.0f);
  REQUIRE(config.neural.enableHooks == 2);
}

TEST_CASE("preset matching accounts for every owned neural value", "[unit]") {
  for (size_t i = 0; i < std::size(kPresets); ++i) {
    Config config;
    kPresets[i].apply(config);
    REQUIRE(MatchingPreset(config) == i);
  }
  const auto custom = [](auto edit) {
    Config config;
    kPresets[0].apply(config);
    edit(config.neural);
    REQUIRE(MatchingPreset(config) == static_cast<size_t>(-1));
  };
  custom([](auto& n) { n.enableHooks = 0; });
  custom([](auto& n) { n.transferStrength = 0.5f; });
  custom([](auto& n) { n.paperWhiteScale = 2.0f; });
  custom([](auto& n) { n.localStructure = 1.0f; });
  custom([](auto& n) { n.localTone = 1.0f; });
  custom([](auto& n) { n.skinStructure = 1.0f; });
}
