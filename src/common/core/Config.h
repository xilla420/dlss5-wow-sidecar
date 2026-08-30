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

struct Config {
  bool showHud = true;
  bool showOverlay = true;
  uint32_t flowGridSize = 4;
  std::string neuralPass = "passthrough";
  std::vector<UiRect> uiMaskRects;
};

// Never throws. A malformed document, a bad value or an unrecognised key all
// produce a warning and leave the corresponding default in place: a typo in a
// config file must not stop the overlay from starting.
Config ParseConfig(std::string_view toml, std::vector<std::string>& warnings);

// Returns nullopt only when the file cannot be read at all. A file that exists
// but parses badly yields defaults plus warnings, like ParseConfig.
std::optional<Config> LoadConfig(const std::filesystem::path& path,
                                 std::vector<std::string>& warnings);

}  // namespace sidecar
