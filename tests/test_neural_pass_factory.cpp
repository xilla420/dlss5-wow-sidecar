#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "neural/NeuralPassFactory.h"

using namespace sidecar;

TEST_CASE("the default pass name builds passthrough without complaint", "[unit]") {
  std::vector<std::string> warnings;
  auto pass = MakeNeuralPass("passthrough", warnings);
  REQUIRE(pass != nullptr);
  REQUIRE(std::string(pass->Name()) == "passthrough");
  REQUIRE(warnings.empty());
}

TEST_CASE("a pass that is named but not yet built warns and falls back", "[unit]") {
  for (const char* name : {"reshade", "ngx"}) {
    INFO("pass: " << name);
    std::vector<std::string> warnings;
    auto pass = MakeNeuralPass(name, warnings);
    REQUIRE(pass != nullptr);
    REQUIRE(std::string(pass->Name()) == "passthrough");
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find(name) != std::string::npos);
    REQUIRE(warnings[0].find("not implemented") != std::string::npos);
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
