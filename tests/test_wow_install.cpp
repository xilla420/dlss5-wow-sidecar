#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "manager/WowInstall.h"

using namespace sidecar;

// WowFolderCandidates is the half of detection that can be tested: it turns two
// registry strings into an ordered list of folders, and touches neither the
// filesystem nor the registry doing it. What cannot be tested is whether those
// folders exist on the machine running the test, which is exactly why that part
// lives in a separate function.
//
// Paths are raw string literals throughout. A Windows path spelled as an
// escaped C string doubles every separator, and a wrong expectation written
// that way is invisible on review.

namespace {

bool Contains(const std::vector<std::string>& list, std::string_view wanted) {
  return std::find(list.begin(), list.end(), wanted) != list.end();
}

// Position of a candidate, or npos when absent, so ordering assertions read as
// comparisons rather than as index arithmetic.
size_t IndexOf(const std::vector<std::string>& list, std::string_view wanted) {
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i] == wanted) return i;
  }
  return static_cast<size_t>(-1);
}

// The values Battle.net actually wrote on the machine this was developed
// against, rather than an invented shape.
constexpr const char* kInstallLocation = R"(D:\World of Warcraft)";
constexpr const char* kDisplayIcon = R"(D:\World of Warcraft\_retail_\WoW.exe)";

}  // namespace

TEST_CASE("the executable's own folder is preferred over the parent", "[unit]") {
  const auto candidates = WowFolderCandidates(kInstallLocation, kDisplayIcon);
  REQUIRE_FALSE(candidates.empty());
  // An injector has to sit beside Wow.exe to be loaded by it, so the folder
  // holding the executable is the only one whose scan means anything.
  CHECK(candidates.front() == R"(D:\World of Warcraft\_retail_)");
  CHECK(IndexOf(candidates, R"(D:\World of Warcraft\_retail_)") <
        IndexOf(candidates, R"(D:\World of Warcraft)"));
}

TEST_CASE("every branch folder is offered, retail first", "[unit]") {
  const auto candidates = WowFolderCandidates(kInstallLocation, "");
  CHECK(Contains(candidates, R"(D:\World of Warcraft\_retail_)"));
  CHECK(Contains(candidates, R"(D:\World of Warcraft\_classic_)"));
  CHECK(Contains(candidates, R"(D:\World of Warcraft\_classic_era_)"));
  CHECK(IndexOf(candidates, R"(D:\World of Warcraft\_retail_)") <
        IndexOf(candidates, R"(D:\World of Warcraft\_classic_)"));
  // The parent is the last resort: it holds no Wow.exe, so scanning it proves
  // nothing about injectors.
  CHECK(IndexOf(candidates, R"(D:\World of Warcraft)") == candidates.size() - 1);
}

TEST_CASE("the icon's index suffix and quotes are stripped", "[unit]") {
  // How the shell usually writes DisplayIcon.
  const auto candidates =
      WowFolderCandidates("", R"("D:\World of Warcraft\_retail_\WoW.exe",0)");
  REQUIRE_FALSE(candidates.empty());
  CHECK(candidates.front() == R"(D:\World of Warcraft\_retail_)");
}

TEST_CASE("a comma inside a folder name is not mistaken for an icon index",
          "[unit]") {
  // Only a trailing run of digits counts as an index, so this path keeps its
  // comma instead of being cut in half.
  const auto candidates = WowFolderCandidates("", R"(D:\Games, old\WoW\WoW.exe)");
  REQUIRE_FALSE(candidates.empty());
  CHECK(candidates.front() == R"(D:\Games, old\WoW)");
}

TEST_CASE("a non-ASCII install path is carried through intact", "[unit]") {
  // The reason detection goes through the UTF-8 helpers rather than path's
  // narrow accessors: this is an ordinary folder name on a Russian install.
  const std::string root = "D:\\\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B\\World of Warcraft";
  const auto candidates = WowFolderCandidates(root, "");
  REQUIRE_FALSE(candidates.empty());
  CHECK(candidates.front() == root + "\\_retail_");
}

TEST_CASE("no registry values yields no candidates rather than a guess",
          "[unit]") {
  // Detection that invented a path would put the checks board into a confident
  // wrong state, which is worse than the honest empty one.
  CHECK(WowFolderCandidates("", "").empty());
}

// The other half: the registry read and the existence check. Tagged [device]
// for the same reason the GPU tests are -- it asserts something about the
// machine it runs on, and CI has no game installed. A machine without WoW is
// not a failure, so the only thing asserted unconditionally is that detection
// answers honestly rather than inventing a directory.
TEST_CASE("detection returns only folders that exist", "[device]") {
  const auto found = DetectWowFolders();
  for (const auto& path : found) {
    INFO("detected " << path.string());
    CHECK(std::filesystem::is_directory(path));
  }
  // And the best guess is the first of them, never something else.
  const auto best = DetectWowFolder();
  CHECK(best.has_value() == !found.empty());
  if (best) CHECK(*best == found.front());
}

TEST_CASE("a duplicate between the two values is offered once", "[unit]") {
  const auto candidates =
      WowFolderCandidates(R"(D:\World of Warcraft\_retail_)",
                          R"(D:\World of Warcraft\_retail_\WoW.exe)");
  size_t seen = 0;
  for (const auto& candidate : candidates) {
    if (candidate == R"(D:\World of Warcraft\_retail_)") ++seen;
  }
  CHECK(seen == 1);
}
