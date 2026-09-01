#pragma once
#include <d3d12.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/GpuProfile.h"
#include "neural/INeuralPass.h"

namespace sidecar {

// What a device-backed pass needs in order to exist.
//
// PassthroughPass needs none of it, which is why the factory can still be called
// without a device and still return something useful.
struct NeuralPassContext {
  ID3D12Device* device = nullptr;
  // Where nvngx_*.dll live, normally next to the executable.
  std::filesystem::path runtimeDir;
  uint32_t width = 0;
  uint32_t height = 0;
  // Decides the working resolution and which runtime build is acceptable. The
  // stock neural runtime is Blackwell-only, so a pass built for the wrong
  // architecture is refused before NGX is touched.
  GpuArch arch = GpuArch::Unsupported;

  // The DLSS render preset, by name. An unrecognised name warns and leaves the
  // pass on its own default rather than failing the pass -- a preset is a
  // quality dial, and nobody should lose the overlay over a typo in one.
  std::string dlssPreset = "cnn-f";

  // The constant written into the synthetic depth plane. A dial, not a
  // measurement: there is no real depth buffer to capture.
  float syntheticDepth = 0.5f;
};

// Builds the neural pass named in the config file.
//
// An unrecognised name is never fatal. The spec's fallback rule (section 11)
// is that a missing or unusable neural runtime degrades to passthrough rather
// than refusing to start, and a typo in a config file is the same situation
// from the operator's point of view: they get a working overlay and a warning
// telling them what was ignored. The same applies to a real pass that cannot
// initialise: the warning says why, and the overlay still runs.
std::unique_ptr<INeuralPass> MakeNeuralPass(std::string_view name,
                                            const NeuralPassContext& context,
                                            std::vector<std::string>& warnings);

// Convenience for callers with no device, which can only produce passthrough.
std::unique_ptr<INeuralPass> MakeNeuralPass(std::string_view name,
                                            std::vector<std::string>& warnings);

}  // namespace sidecar
