#include "neural/NeuralPassFactory.h"

#include "neural/PassthroughPass.h"

namespace sidecar {

std::unique_ptr<INeuralPass> MakeNeuralPass(std::string_view name,
                                            std::vector<std::string>& warnings) {
  if (name == "passthrough") return PassthroughPass::Create();

  // ReshadeHostedPass and DirectNgxPass arrive in M3. Until then they are
  // named but not built, and asking for one has to say so plainly rather than
  // silently behaving like passthrough.
  if (name == "reshade" || name == "ngx") {
    warnings.emplace_back("neural_pass \"" + std::string(name) +
                          "\" is not implemented yet; using passthrough");
    return PassthroughPass::Create();
  }

  warnings.emplace_back("neural_pass \"" + std::string(name) +
                        "\" is not a known pass; using passthrough");
  return PassthroughPass::Create();
}

}  // namespace sidecar
