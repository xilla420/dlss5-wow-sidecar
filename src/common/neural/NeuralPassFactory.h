#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "neural/INeuralPass.h"

namespace sidecar {

// Builds the neural pass named in the config file.
//
// An unrecognised name is never fatal. The spec's fallback rule (section 11)
// is that a missing or unusable neural runtime degrades to passthrough rather
// than refusing to start, and a typo in a config file is the same situation
// from the operator's point of view: they get a working overlay and a warning
// telling them what was ignored.
std::unique_ptr<INeuralPass> MakeNeuralPass(std::string_view name,
                                            std::vector<std::string>& warnings);

}  // namespace sidecar
