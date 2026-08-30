#include "core/Config.h"

#include <toml++/toml.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

namespace sidecar {
namespace {

// Every key the document may contain. Anything else earns a warning so a
// typo is visible rather than silently ignored.
constexpr std::array<std::string_view, 5> kKnownKeys = {
    "show_hud", "show_overlay", "flow_grid_size", "neural_pass", "ui_mask"};

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

}  // namespace sidecar
