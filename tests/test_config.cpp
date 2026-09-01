#include <catch2/catch_test_macros.hpp>
#include "core/Config.h"

using namespace sidecar;

TEST_CASE("defaults apply to an empty document", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig("", warnings);
  REQUIRE(cfg.showHud == true);
  REQUIRE(cfg.showOverlay == true);
  REQUIRE(cfg.flowGridSize == 4);
  // Neural rendering out of the box. Safe as a default because every failure
  // path degrades to passthrough with a warning rather than refusing to start.
  REQUIRE(cfg.neuralPass == "reshade");
  REQUIRE(cfg.dlssPreset == "cnn-f");
  REQUIRE(cfg.uiMaskRects.empty());
  REQUIRE(warnings.empty());
}

TEST_CASE("values are read from the document", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig(R"(
    show_hud = false
    flow_grid_size = 2
    neural_pass = "reshade"
  )", warnings);
  REQUIRE(cfg.showHud == false);
  REQUIRE(cfg.flowGridSize == 2);
  REQUIRE(cfg.neuralPass == "reshade");
}

TEST_CASE("UI mask rectangles round-trip", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig(R"(
    [[ui_mask]]
    left = 0
    top = 900
    right = 1920
    bottom = 1080

    [[ui_mask]]
    left = 1600
    top = 0
    right = 1920
    bottom = 300
  )", warnings);
  REQUIRE(cfg.uiMaskRects.size() == 2);
  REQUIRE(cfg.uiMaskRects[0].top == 900);
  REQUIRE(cfg.uiMaskRects[1].left == 1600);
  REQUIRE(warnings.empty());
}

TEST_CASE("an invalid grid size warns and falls back", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig("flow_grid_size = 7", warnings);
  REQUIRE(cfg.flowGridSize == 4);
  REQUIRE(warnings.size() == 1);
  REQUIRE(warnings[0].find("flow_grid_size") != std::string::npos);
}

TEST_CASE("malformed TOML warns rather than throwing", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig("this is not = = toml", warnings);
  REQUIRE(cfg.showHud == true);          // all defaults
  REQUIRE(warnings.size() >= 1);
}

TEST_CASE("unknown keys warn but do not break the rest of the document", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig(R"(
    show_hud = false
    nonexistent_key = 42
  )", warnings);
  REQUIRE(cfg.showHud == false);
  REQUIRE(warnings.size() == 1);
  REQUIRE(warnings[0].find("nonexistent_key") != std::string::npos);
}

TEST_CASE("the add-on's settings are read from their own table", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig(R"(
    [neural]
    enable_hooks = 1
    intensity = 0.5
    preset = 2
    upscaling = true
  )", warnings);
  REQUIRE(cfg.neural.enableHooks == 1);
  REQUIRE(cfg.neural.intensity == 0.5f);
  REQUIRE(cfg.neural.preset == 2);
  REQUIRE(cfg.neural.upscaling == true);
  REQUIRE(warnings.empty());
}

TEST_CASE("an out-of-range value is clamped and reported, not discarded", "[unit]") {
  // Reverting to a default the operator did not ask for is worse than moving
  // them to the nearest legal value, as long as they are told.
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig(R"(
    [neural]
    intensity = 9.0
  )", warnings);
  REQUIRE(cfg.neural.intensity == 1.0f);
  REQUIRE(warnings.size() == 1);
}

TEST_CASE("an unset optional strength stays unset through a round trip", "[unit]") {
  // Negative means "leave the add-on's own default alone". If serialising and
  // reading back turned that into 0.0, saving settings would silently pin three
  // knobs nobody ever touched.
  Config config;
  REQUIRE(config.neural.localStructure < 0.0f);

  std::vector<std::string> warnings;
  const auto reread = ParseConfig(SerializeConfig(config), warnings);
  REQUIRE(reread.neural.localStructure < 0.0f);
  REQUIRE(reread.neural.localTone < 0.0f);
  REQUIRE(reread.neural.skinStructure < 0.0f);
  REQUIRE(warnings.empty());
}

TEST_CASE("a written config reads back as itself", "[unit]") {
  Config config;
  config.neuralPass = "reshade";
  config.dlssPreset = "cnn-e";
  config.showHud = false;
  config.flowGridSize = 2;
  config.syntheticDepth = 0.25f;
  config.uiMaskFeather = 8;
  config.neural.intensity = 0.6f;
  config.neural.style = 1;
  config.neural.paperWhiteScale = 2.5f;
  config.neural.skinStructure = 0.75f;
  config.uiMaskRects.push_back(UiRect{0, 900, 1920, 1080});

  std::vector<std::string> warnings;
  const auto reread = ParseConfig(SerializeConfig(config), warnings);

  REQUIRE(warnings.empty());
  REQUIRE(reread.neuralPass == config.neuralPass);
  REQUIRE(reread.dlssPreset == config.dlssPreset);
  REQUIRE(reread.showHud == config.showHud);
  REQUIRE(reread.flowGridSize == config.flowGridSize);
  REQUIRE(reread.syntheticDepth == config.syntheticDepth);
  REQUIRE(reread.uiMaskFeather == config.uiMaskFeather);
  REQUIRE(reread.neural.intensity == config.neural.intensity);
  REQUIRE(reread.neural.style == config.neural.style);
  REQUIRE(reread.neural.paperWhiteScale == config.neural.paperWhiteScale);
  REQUIRE(reread.neural.skinStructure == config.neural.skinStructure);
  REQUIRE(reread.uiMaskRects.size() == 1);
  REQUIRE(reread.uiMaskRects[0].bottom == 1080);
}
