#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/GpuProfile.h"

namespace sidecar {

enum class ProbeState { Ok, Warn, Fail };

// A row on the manager's dependency board. A probe that is not Ok always
// carries a remedy: telling someone their system is wrong without telling them
// what to do about it is not a diagnostic.
struct ProbeResult {
  ProbeState state = ProbeState::Fail;
  std::string title;
  std::string detail;
  std::string remedy;
};

// Pure predicates, so the safety invariants they enforce are unit-testable
// without a real WoW install anywhere near the test machine.

// I7: refuses to treat anything that looks like a WoW installation as a place
// to put the sidecar.
bool PathLooksLikeWowInstall(const std::filesystem::path& path);

// I8: names of loaders that side-load into a game process by masquerading as
// system DLLs the game already imports. Matching is on filenames only; none of
// these files is ever opened.
std::vector<std::string> FindInjectorLoaders(const std::vector<std::string>& filenames);

// The neural-runtime verdict, given a digest rather than a file. Split out
// because the recognised path cannot otherwise be tested: producing a file that
// hashes to a manifest entry would mean inverting SHA-256. An empty digest means
// the file was found but could not be read.
//
// arch is the card the runtime would have to run on. A recognised runtime that
// cannot run on this architecture is still amber, but it says so -- that pairing
// is the difference between a readable diagnostic and an unexplained failure
// deep inside NGX.
ProbeResult NeuralRuntimeVerdict(std::string_view fileName, std::string_view sha256Hex,
                                 GpuArch arch);

ProbeResult ProbeGpu();
ProbeResult ProbeDriver();
ProbeResult ProbeWindows();
ProbeResult ProbeRefreshRate();
ProbeResult ProbeNeuralRuntime(const std::filesystem::path& sidecarDir);
ProbeResult ProbeReshade(const std::filesystem::path& sidecarDir);
ProbeResult ProbeWowWindow();
ProbeResult ProbeInjectorScan(const std::filesystem::path& wowDir);
ProbeResult ProbeSidecarPath(const std::filesystem::path& sidecarDir,
                             const std::filesystem::path& wowDir);

std::vector<ProbeResult> RunAllProbes(const std::filesystem::path& sidecarDir,
                                      const std::filesystem::path& wowDir);

}  // namespace sidecar
