#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sidecar {

// A screen-space rectangle the neural pass should leave alone. WoW's UI is
// drawn into the same frame the sidecar captures, so masking it out is the
// only way to keep text and icons from being reprocessed.
struct UiRect {
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;
};

// The knobs the RenoDX DLSS 5 add-on reads out of ReShade.ini's
// [RenoDX.DLSS5] section.
//
// These are not our settings. We carry them from the manager's UI to the file
// the add-on reads, and the add-on is the authority on what any of them mean.
// The names here are the add-on's own key names, taken from its binary, so a
// reader can grep for them in both places.
struct NeuralSettings {
  // 0 = every hook off (no neural rendering at all), 1 = NGX plus Streamline,
  // 2 = NGX only. Two is what this sidecar wants: we make the NGX calls
  // ourselves and there is no Streamline in the process to contest.
  int enableHooks = 2;

  float intensity = 1.0f;          // NRIntensity
  float colorStrength = 1.0f;      // NRColorStrength
  float transferStrength = 1.0f;   // NRTransferStrength
  float paperWhiteScale = 1.0f;    // NRPaperWhiteScale
  int preset = 0;                  // NRPreset, the add-on's own #1..#3
  int style = 0;                   // NRStyle
  bool upscaling = false;          // NREnableUpscaling; the add-on marks it WIP

  // The add-on exposes these but does not print them in its status line, so we
  // do not know its defaults and will not invent them. Negative means "leave it
  // alone": the key is omitted from the file entirely rather than pinned to a
  // number we guessed.
  float localStructure = -1.0f;    // NRLocalStructure
  float localTone = -1.0f;         // NRLocalTone
  float skinStructure = -1.0f;     // NRSkinStructure
};

struct Config {
  bool showHud = true;
  bool showOverlay = true;
  uint32_t flowGridSize = 4;

  // Neural rendering by default, because that is what anyone installing this
  // came for. It is safe as a default precisely because it cannot fail hard:
  // a missing runtime, a bad hash or a refused feature all degrade to
  // PassthroughPass with a warning (spec section 11), so a first run on a
  // machine with none of the operator-supplied files still produces a working
  // overlay rather than an error.
  std::string neuralPass = "reshade";

  // Which DLSS render preset the feature is created with. Named rather than
  // numbered because the numbers are an SDK detail; DlssPresetFromName maps it.
  std::string dlssPreset = "cnn-f";

  // The constant written into the synthetic depth plane. There is no real depth
  // buffer to capture, so this is a dial, not a measurement.
  float syntheticDepth = 0.5f;

  std::vector<UiRect> uiMaskRects;
  uint32_t uiMaskFeather = 0;

  NeuralSettings neural;
};

// Never throws. A malformed document, a bad value or an unrecognised key all
// produce a warning and leave the corresponding default in place: a typo in a
// config file must not stop the overlay from starting.
Config ParseConfig(std::string_view toml, std::vector<std::string>& warnings);

// Returns nullopt only when the file cannot be read at all. A file that exists
// but parses badly yields defaults plus warnings, like ParseConfig.
std::optional<Config> LoadConfig(const std::filesystem::path& path,
                                 std::vector<std::string>& warnings);

// The document ParseConfig would read back unchanged. Written whole rather than
// edited in place, because the manager owns this file and hand-edits to it are
// expected to survive only as far as the next save.
std::string SerializeConfig(const Config& config);

// Writes SerializeConfig to disk. False means the file could not be written --
// a read-only directory, most likely -- and the caller has to say so.
bool SaveConfig(const std::filesystem::path& path, const Config& config);

}  // namespace sidecar
