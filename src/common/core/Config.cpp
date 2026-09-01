#include "core/Config.h"

#include <toml++/toml.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

namespace sidecar {
namespace {

// Every key the document may contain. Anything else earns a warning so a
// typo is visible rather than silently ignored.
constexpr std::array<std::string_view, 9> kKnownKeys = {
    "show_hud",     "show_overlay",   "flow_grid_size", "neural_pass",
    "dlss_preset",  "synthetic_depth", "ui_mask",       "ui_mask_feather",
    "neural"};

bool IsKnown(std::string_view key) {
  return std::find(kKnownKeys.begin(), kKnownKeys.end(), key) != kKnownKeys.end();
}

void ReadBool(const toml::table& root, std::string_view key, bool& target,
              std::vector<std::string>& warnings) {
  const auto node = root.get(key);
  if (!node) return;
  if (auto value = node->value<bool>()) {
    target = *value;
  } else {
    warnings.emplace_back(std::string(key) + ": expected a boolean; keeping the default");
  }
}

// Floats are clamped rather than rejected. A value out of range is a mistake
// worth reporting, but the nearest legal value is always a better outcome than
// silently reverting to a default the operator did not ask for.
void ReadFloat(const toml::table& root, std::string_view key, float& target,
               float low, float high, std::vector<std::string>& warnings) {
  const auto node = root.get(key);
  if (!node) return;
  const auto value = node->value<double>();
  if (!value) {
    warnings.emplace_back(std::string(key) + ": expected a number; keeping the default");
    return;
  }
  const auto clamped = std::clamp(static_cast<float>(*value), low, high);
  if (clamped != static_cast<float>(*value)) {
    warnings.emplace_back(std::string(key) + ": out of range; clamped");
  }
  target = clamped;
}

void ReadInt(const toml::table& root, std::string_view key, int& target,
             int low, int high, std::vector<std::string>& warnings) {
  const auto node = root.get(key);
  if (!node) return;
  const auto value = node->value<int64_t>();
  if (!value) {
    warnings.emplace_back(std::string(key) + ": expected an integer; keeping the default");
    return;
  }
  const auto clamped = std::clamp(static_cast<int>(*value), low, high);
  if (clamped != static_cast<int>(*value)) {
    warnings.emplace_back(std::string(key) + ": out of range; clamped");
  }
  target = clamped;
}

// The add-on's optional strengths, where negative means "not set". Reading has
// to preserve that, so this cannot go through ReadFloat's clamp.
void ReadOptionalStrength(const toml::table& root, std::string_view key, float& target,
                          std::vector<std::string>& warnings) {
  const auto node = root.get(key);
  if (!node) return;
  const auto value = node->value<double>();
  if (!value) {
    warnings.emplace_back(std::string(key) + ": expected a number; keeping the default");
    return;
  }
  target = *value < 0.0 ? -1.0f : std::clamp(static_cast<float>(*value), 0.0f, 4.0f);
}

// Two decimals, and a trailing ".0" on whole numbers so the file reads as TOML
// floats rather than integers -- which matters, because a bare 1 parses as an
// integer and would warn on the way back in.
std::string Number(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
}

const char* Boolean(bool value) { return value ? "true" : "false"; }

}  // namespace

Config ParseConfig(std::string_view text, std::vector<std::string>& warnings) {
  Config config;

  toml::table root;
  try {
    root = toml::parse(text);
  } catch (const toml::parse_error& error) {
    warnings.emplace_back(std::string("could not parse config: ") +
                          std::string(error.description()));
    return config;   // every default stays in place
  }

  ReadBool(root, "show_hud", config.showHud, warnings);
  ReadBool(root, "show_overlay", config.showOverlay, warnings);

  if (const auto node = root.get("flow_grid_size")) {
    const auto value = node->value<int64_t>();
    if (!value) {
      warnings.emplace_back("flow_grid_size: expected an integer; using 4");
    } else if (*value != 1 && *value != 2 && *value != 4) {
      warnings.emplace_back("flow_grid_size: must be 1, 2 or 4; using 4");
    } else {
      config.flowGridSize = static_cast<uint32_t>(*value);
    }
  }

  if (const auto node = root.get("neural_pass")) {
    if (auto value = node->value<std::string>()) {
      config.neuralPass = *value;
    } else {
      warnings.emplace_back("neural_pass: expected a string; using \"passthrough\"");
    }
  }

  if (const auto node = root.get("dlss_preset")) {
    if (auto value = node->value<std::string>()) {
      config.dlssPreset = *value;
    } else {
      warnings.emplace_back("dlss_preset: expected a string; using \"cnn-f\"");
    }
  }

  ReadFloat(root, "synthetic_depth", config.syntheticDepth, 0.0f, 1.0f, warnings);

  if (const auto node = root.get("ui_mask_feather")) {
    int feather = static_cast<int>(config.uiMaskFeather);
    ReadInt(root, "ui_mask_feather", feather, 0, 256, warnings);
    config.uiMaskFeather = static_cast<uint32_t>(feather);
  }

  if (const auto node = root.get("ui_mask")) {
    if (const auto* array = node->as_array()) {
      for (const auto& element : *array) {
        const auto* entry = element.as_table();
        if (!entry) {
          warnings.emplace_back("ui_mask: expected a table of edges; entry ignored");
          continue;
        }
        UiRect rect;
        rect.left = static_cast<int32_t>((*entry)["left"].value_or<int64_t>(0));
        rect.top = static_cast<int32_t>((*entry)["top"].value_or<int64_t>(0));
        rect.right = static_cast<int32_t>((*entry)["right"].value_or<int64_t>(0));
        rect.bottom = static_cast<int32_t>((*entry)["bottom"].value_or<int64_t>(0));
        config.uiMaskRects.push_back(rect);
      }
    } else {
      warnings.emplace_back("ui_mask: expected an array of tables; ignored");
    }
  }

  // The add-on's own settings live in their own table, so the top level stays
  // about the sidecar and it is obvious which knobs belong to somebody else.
  if (const auto node = root.get("neural")) {
    const auto* table = node->as_table();
    if (!table) {
      warnings.emplace_back("neural: expected a table; ignored");
    } else {
      auto& n = config.neural;
      ReadInt(*table, "enable_hooks", n.enableHooks, 0, 2, warnings);
      ReadFloat(*table, "intensity", n.intensity, 0.0f, 1.0f, warnings);
      ReadFloat(*table, "color_strength", n.colorStrength, 0.0f, 1.0f, warnings);
      ReadFloat(*table, "transfer_strength", n.transferStrength, 0.0f, 1.0f, warnings);
      ReadFloat(*table, "paper_white_scale", n.paperWhiteScale, 0.0f, 10.0f, warnings);
      ReadInt(*table, "preset", n.preset, 0, 3, warnings);
      ReadInt(*table, "style", n.style, 0, 3, warnings);
      ReadBool(*table, "upscaling", n.upscaling, warnings);
      ReadOptionalStrength(*table, "local_structure", n.localStructure, warnings);
      ReadOptionalStrength(*table, "local_tone", n.localTone, warnings);
      ReadOptionalStrength(*table, "skin_structure", n.skinStructure, warnings);
    }
  }

  for (const auto& [key, value] : root) {
    (void)value;
    if (!IsKnown(key.str())) {
      warnings.emplace_back(std::string("unknown key ignored: ") + std::string(key.str()));
    }
  }

  return config;
}

std::optional<Config> LoadConfig(const std::filesystem::path& path,
                                 std::vector<std::string>& warnings) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  return ParseConfig(text, warnings);
}

std::string SerializeConfig(const Config& config) {
  std::ostringstream out;
  out << "# DLSS 5 sidecar. Written by the manager; hand edits are read back on\n"
         "# the next launch and overwritten on the next save.\n\n";

  out << "neural_pass = \"" << config.neuralPass << "\"\n";
  out << "dlss_preset = \"" << config.dlssPreset << "\"\n";
  out << "show_hud = " << Boolean(config.showHud) << "\n";
  out << "show_overlay = " << Boolean(config.showOverlay) << "\n";
  out << "flow_grid_size = " << config.flowGridSize << "\n";
  out << "synthetic_depth = " << Number(config.syntheticDepth) << "\n";
  out << "ui_mask_feather = " << config.uiMaskFeather << "\n";

  const auto& n = config.neural;
  out << "\n# Passed through to the RenoDX DLSS 5 add-on's [RenoDX.DLSS5]\n"
         "# section in ReShade.ini. A negative strength means \"leave the\n"
         "# add-on's own default alone\".\n";
  out << "[neural]\n";
  out << "enable_hooks = " << n.enableHooks << "\n";
  out << "intensity = " << Number(n.intensity) << "\n";
  out << "color_strength = " << Number(n.colorStrength) << "\n";
  out << "transfer_strength = " << Number(n.transferStrength) << "\n";
  out << "paper_white_scale = " << Number(n.paperWhiteScale) << "\n";
  out << "preset = " << n.preset << "\n";
  out << "style = " << n.style << "\n";
  out << "upscaling = " << Boolean(n.upscaling) << "\n";
  out << "local_structure = " << Number(n.localStructure) << "\n";
  out << "local_tone = " << Number(n.localTone) << "\n";
  out << "skin_structure = " << Number(n.skinStructure) << "\n";

  // The mask goes last: it is the only unbounded section, and a long one would
  // otherwise push everything readable off the top of the file.
  for (const auto& rect : config.uiMaskRects) {
    out << "\n[[ui_mask]]\n";
    out << "left = " << rect.left << "\n";
    out << "top = " << rect.top << "\n";
    out << "right = " << rect.right << "\n";
    out << "bottom = " << rect.bottom << "\n";
  }

  return out.str();
}

bool SaveConfig(const std::filesystem::path& path, const Config& config) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return false;
  const std::string text = SerializeConfig(config);
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  return file.good();
}

}  // namespace sidecar
