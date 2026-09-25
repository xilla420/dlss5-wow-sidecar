#include <catch2/catch_test_macros.hpp>
#include "core/Config.h"

using namespace sidecar;

// The WoW folder is the one setting that holds a Windows path, which makes it
// the one setting where serialisation can produce a document that reads back as
// a different value: every separator is a backslash, and TOML reads a backslash
// as the start of an escape sequence.
//
// The paths below are written as raw string literals so that what the test
// means is what the test says. Spelling a Windows path as an escaped C string
// doubles every separator and makes a wrong expectation impossible to see.

TEST_CASE("an unset WoW folder is absent from the document", "[unit]") {
  Config config;
  REQUIRE(config.wowDir.empty());
  const std::string text = SerializeConfig(config);
  // Not written as an empty string: a key present and blank reads as
  // "configured to nothing", which is a different claim from "not configured".
  REQUIRE(text.find("wow_dir") == std::string::npos);
}

TEST_CASE("a Windows path round-trips with its backslashes intact", "[unit]") {
  Config config;
  config.wowDir = R"(D:\World of Warcraft\_retail_)";

  const std::string text = SerializeConfig(config);
  std::vector<std::string> warnings;
  const auto reread = ParseConfig(text, warnings);

  REQUIRE(warnings.empty());
  REQUIRE(reread.wowDir == config.wowDir);
}

TEST_CASE("a path with a quote or an apostrophe round-trips", "[unit]") {
  // Both are reachable through a user name, and each would break one of TOML's
  // two string forms if the other had been chosen.
  Config config;
  config.wowDir = R"(C:\Users\O'Brien\Games\"WoW"\_retail_)";

  std::vector<std::string> warnings;
  const auto reread = ParseConfig(SerializeConfig(config), warnings);

  REQUIRE(warnings.empty());
  REQUIRE(reread.wowDir == config.wowDir);
}

TEST_CASE("a non-ASCII path round-trips", "[unit]") {
  // An ordinary folder name on a Russian install, and the case that a
  // code-page conversion would destroy.
  Config config;
  config.wowDir = "D:\\\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B\\World of Warcraft";

  std::vector<std::string> warnings;
  const auto reread = ParseConfig(SerializeConfig(config), warnings);

  REQUIRE(warnings.empty());
  REQUIRE(reread.wowDir == config.wowDir);
}

TEST_CASE("wow_dir is a known key and a wrong type only warns", "[unit]") {
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig("wow_dir = 42\n", warnings);
  // The default survives and the operator is told, rather than left guessing.
  REQUIRE(cfg.wowDir.empty());
  REQUIRE(warnings.size() == 1);
  REQUIRE(warnings[0].find("wow_dir") != std::string::npos);
  // Specifically not the "unknown key ignored" warning.
  REQUIRE(warnings[0].find("unknown key") == std::string::npos);
}

TEST_CASE("a WoW folder that no longer exists is still loaded", "[unit]") {
  // Dropping it on load would make a moved install look like an unconfigured
  // one, and the operator would have to find the path again to correct a typo.
  std::vector<std::string> warnings;
  const auto cfg = ParseConfig(R"(wow_dir = "Q:\\gone\\_retail_")", warnings);
  REQUIRE(warnings.empty());
  REQUIRE(cfg.wowDir == R"(Q:\gone\_retail_)");
}
