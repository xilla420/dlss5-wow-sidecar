#pragma once
#include "core/Config.h"
#include <iterator>

namespace sidecar {
struct Preset {
  const char* name;
  const char* summary;
  const char* detail;
  void (*apply)(Config&);
};

inline const Preset kPresets[] = {
    {"Recommended",
     "The tuned default. Start here.",
     "Full neural intensity with the CNN F render preset, which clamps temporal "
     "history hard -- the right choice when motion vectors are estimated from "
     "colour rather than rendered by the game.",
     [](Config& c) {
       c.neuralPass = "reshade";
       c.dlssPreset = "cnn-f";
       c.flowGridSize = 4;
       c.syntheticDepth = 0.5f;
       c.neural = NeuralSettings{};   // every add-on knob at its own default
     }},
    {"Softer",
     "Half strength. Use if the picture looks over-processed.",
     "The same pipeline with the neural result mixed in at 60%. Cheaper on the "
     "eyes for interface-heavy scenes, and the first thing to try if faces or "
     "text look waxy.",
     [](Config& c) {
       c.neuralPass = "reshade";
       c.dlssPreset = "cnn-f";
       c.flowGridSize = 4;
       c.syntheticDepth = 0.5f;
       c.neural = NeuralSettings{};
       c.neural.intensity = 0.60f;
     }},
    {"Most stable",
     "For smearing, or flicker on flames and lights.",
     "Switches to CNN E, which clamps temporal history hardest, and eases the "
     "intensity. This is the preset for when motion looks smeared -- the "
     "estimated motion vectors are being confidently wrong and this contains "
     "them.",
     [](Config& c) {
       c.neuralPass = "reshade";
       c.dlssPreset = "cnn-e";
       // Grid 2 rather than 4: a finer motion field is the one thing that
       // genuinely helps a smearing complaint, and it is worth the millisecond
       // in the preset whose whole job is stability.
       c.flowGridSize = 2;
       c.syntheticDepth = 0.5f;
       c.neural = NeuralSettings{};
       c.neural.intensity = 0.85f;
     }},
    {"Off (A/B baseline)",
     "Capture and present, untouched.",
     "No neural work at all, on the same capture and present path. This is the "
     "honest comparison: whatever you see here is what the overlay costs you "
     "before any neural rendering happens.",
     [](Config& c) { c.neuralPass = "passthrough"; }},
};

// Which preset the current config corresponds to, or npos when the operator has
// hand-edited their way off the map. Compared on the fields the presets set, so
// an unrelated change -- the HUD toggle, a mask rectangle -- does not read as
// "custom".
inline size_t MatchingPreset(const Config& config) {
  for (size_t i = 0; i < std::size(kPresets); ++i) {
    Config candidate;
    kPresets[i].apply(candidate);
    if (candidate.neuralPass != config.neuralPass) continue;
    if (candidate.neuralPass == "passthrough") return i;
    if (candidate.dlssPreset == config.dlssPreset &&
        candidate.flowGridSize == config.flowGridSize &&
        candidate.syntheticDepth == config.syntheticDepth &&
        candidate.neural.intensity == config.neural.intensity &&
        candidate.neural.colorStrength == config.neural.colorStrength &&
        candidate.neural.enableHooks == config.neural.enableHooks &&
        candidate.neural.transferStrength == config.neural.transferStrength &&
        candidate.neural.paperWhiteScale == config.neural.paperWhiteScale &&
        candidate.neural.localStructure == config.neural.localStructure &&
        candidate.neural.localTone == config.neural.localTone &&
        candidate.neural.skinStructure == config.neural.skinStructure &&
        candidate.neural.preset == config.neural.preset &&
        candidate.neural.style == config.neural.style &&
        candidate.neural.upscaling == config.neural.upscaling) {
      return i;
    }
  }
  return static_cast<size_t>(-1);
}

}  // namespace sidecar
