#include <catch2/catch_test_macros.hpp>
#include "core/Config.h"

using namespace sidecar;

TEST_CASE("defaults apply to an empty document", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig("", warnings);
  REQUIRE(cfg.showHud == true);
  REQUIRE(cfg.showOverlay == true);
  REQUIRE(cfg.flowGridSize == 4);
  REQUIRE(cfg.neuralPass == "passthrough");
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
