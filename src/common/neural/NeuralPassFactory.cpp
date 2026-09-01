#include "neural/NeuralPassFactory.h"

#include "neural/NgxSession.h"
#include "neural/PassthroughPass.h"
#include "neural/ReshadeHostedPass.h"

namespace sidecar {

std::unique_ptr<INeuralPass> MakeNeuralPass(std::string_view name,
                                            const NeuralPassContext& context,
                                            std::vector<std::string>& warnings) {
  if (name == "passthrough") return PassthroughPass::Create();

  if (name == "reshade") {
    if (!context.device) {
      warnings.emplace_back("neural_pass \"reshade\" needs a graphics device; "
                            "using passthrough");
      return PassthroughPass::Create();
    }

    ReshadeHostedPass::Options options;
    options.runtimeDir = context.runtimeDir;
    options.width = context.width;
    options.height = context.height;
    options.arch = context.arch;
    options.syntheticDepth = context.syntheticDepth;
    if (auto preset = DlssPresetFromName(context.dlssPreset)) {
      options.preset = *preset;
    } else {
      warnings.emplace_back("dlss_preset \"" + context.dlssPreset +
                            "\" is not a known preset; using \"" +
                            DlssPresetName(options.preset) + "\"");
    }

    std::string reason;
    if (auto pass = ReshadeHostedPass::Create(context.device, options, reason)) {
      return pass;
    }
    // Spec section 11: a neural runtime that will not come up degrades to
    // passthrough and says why, rather than refusing to start.
    warnings.emplace_back("neural_pass \"reshade\" is unavailable (" + reason +
                          "); using passthrough");
    return PassthroughPass::Create();
  }

  if (name == "ngx") {
    // Route A. The M3 spikes established that the neural-rendering runtime
    // refuses an NGX session set up by anyone but the NGX core, through both the
    // core's feature registry and the runtime's own exports, so there is nothing
    // to build here. See docs/superpowers/spikes/2026-08-31-reshade-detour.md.
    warnings.emplace_back("neural_pass \"ngx\" is not reachable on any shipping "
                          "driver; using passthrough");
    return PassthroughPass::Create();
  }

  warnings.emplace_back("neural_pass \"" + std::string(name) +
                        "\" is not a known pass; using passthrough");
  return PassthroughPass::Create();
}

std::unique_ptr<INeuralPass> MakeNeuralPass(std::string_view name,
                                            std::vector<std::string>& warnings) {
  return MakeNeuralPass(name, NeuralPassContext{}, warnings);
}

}  // namespace sidecar
