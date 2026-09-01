#include "neural/AddonSettings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace sidecar {
namespace {

constexpr std::string_view kAddonSection = "ADDON";
constexpr std::string_view kGeneralSection = "GENERAL";
constexpr std::string_view kNeuralSection = "RenoDX.DLSS5";

std::string Lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string_view Trim(std::string_view text) {
  const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  auto begin = std::find_if(text.begin(), text.end(), notSpace);
  auto end = std::find_if(text.rbegin(), text.rend(), notSpace).base();
  if (begin >= end) return {};
  return text.substr(static_cast<size_t>(begin - text.begin()),
                     static_cast<size_t>(end - begin));
}

std::string Float(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.6f", value);
  return buffer;
}

// One key the manager owns. Order is the order they are written in a section we
// have to create from nothing, so it is chosen to read top-down: what is on,
// then how strong, then the colour handling.
//
// An absent value means "remove this key", which is not the same as skipping it
// -- see NeuralKeys.
struct Setting {
  std::string key;
  std::optional<std::string> value;
};

std::vector<Setting> NeuralKeys(const NeuralSettings& s) {
  std::vector<Setting> out{
      {"EnableHooks", std::to_string(s.enableHooks)},
      {"NREnableUpscaling", std::string(s.upscaling ? "1" : "0")},
      {"NRPreset", std::to_string(s.preset)},
      {"NRStyle", std::to_string(s.style)},
      {"NRIntensity", Float(s.intensity)},
      {"NRColorStrength", Float(s.colorStrength)},
      {"NRTransferStrength", Float(s.transferStrength)},
      {"NRPaperWhiteScale", Float(s.paperWhiteScale)},
  };
  // A negative strength means the operator never set it, and the add-on's own
  // default should stand. That requires *deleting* the key, not skipping it.
  //
  // This transform is a merge into a file we do not own, so skipping a key
  // leaves whatever was there before. A value written during an earlier session
  // -- or by the add-on's own overlay, which ReShade persists on exit -- then
  // survives every subsequent save, and the operator is told the setting is at
  // its default while the add-on reads 1.29. Removing the line is the only way
  // to actually mean "untouched".
  const auto strength = [](float value) {
    return value >= 0.0f ? std::optional<std::string>(Float(value)) : std::nullopt;
  };
  out.push_back({"NRLocalStructure", strength(s.localStructure)});
  out.push_back({"NRLocalTone", strength(s.localTone)});
  out.push_back({"NRSkinStructure", strength(s.skinStructure)});
  return out;
}

// A parsed line, kept alongside its original text so anything we do not touch
// can be written back exactly as it came in.
struct Line {
  std::string text;
  std::string section;   // the section this line is inside, uppercased-as-is
  std::string key;       // empty unless the line is a key=value
};

std::string SectionName(std::string_view line) {
  const auto trimmed = Trim(line);
  if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') return {};
  return std::string(Trim(trimmed.substr(1, trimmed.size() - 2)));
}

std::string KeyName(std::string_view line) {
  const auto trimmed = Trim(line);
  if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') return {};
  const auto equals = trimmed.find('=');
  if (equals == std::string_view::npos) return {};
  return std::string(Trim(trimmed.substr(0, equals)));
}

bool SameName(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }

// Applies one section's worth of settings to the parsed document, in place where
// the keys already exist and appended to the end of the section where they do
// not. Returns false when the section is absent altogether, which the caller
// answers by appending a whole new one.
bool UpdateSection(std::vector<Line>& lines, std::string_view section,
                   const std::vector<Setting>& settings) {
  bool sectionSeen = false;
  for (const auto& line : lines) {
    if (SameName(line.section, section)) { sectionSeen = true; break; }
  }
  if (!sectionSeen) return false;

  for (const auto& setting : settings) {
    if (!setting.value) {
      // Remove every line for this key in this section. Erasing rather than
      // leaving it is the whole point: the caller means "restore the add-on's
      // own default", and a stale line does the opposite.
      lines.erase(std::remove_if(lines.begin(), lines.end(),
                                 [&](const Line& line) {
                                   return SameName(line.section, section) &&
                                          SameName(line.key, setting.key);
                                 }),
                  lines.end());
      continue;
    }

    bool replaced = false;
    for (auto& line : lines) {
      if (!SameName(line.section, section) || !SameName(line.key, setting.key)) continue;
      line.text = setting.key + "=" + *setting.value;
      replaced = true;
      break;
    }
    if (replaced) continue;

    // Insert after the last line of the section rather than at the first blank,
    // so a key we add lands with its own kind instead of ahead of the section's
    // existing contents.
    size_t insertAt = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
      if (SameName(lines[i].section, section)) insertAt = i + 1;
    }
    Line added;
    added.text = setting.key + "=" + *setting.value;
    added.section = std::string(section);
    added.key = setting.key;
    lines.insert(lines.begin() + static_cast<ptrdiff_t>(insertAt), std::move(added));
  }
  return true;
}

void AppendSection(std::vector<Line>& lines, std::string_view section,
                   const std::vector<Setting>& settings) {
  if (!lines.empty() && !Trim(lines.back().text).empty()) {
    lines.push_back(Line{"", std::string(section), {}});
  }
  lines.push_back(Line{"[" + std::string(section) + "]", std::string(section), {}});
  for (const auto& setting : settings) {
    if (!setting.value) continue;   // nothing to remove from a section being created
    lines.push_back(
        Line{setting.key + "=" + *setting.value, std::string(section), setting.key});
  }
}

}  // namespace

std::string ApplyNeuralSettings(std::string_view ini, const NeuralSettings& settings) {
  // The BOM is carried, not parsed. ReShade writes one, and stripping it would
  // be a gratuitous change to a file we are only visiting.
  std::string_view bom;
  if (ini.size() >= 3 && ini.compare(0, 3, "\xEF\xBB\xBF") == 0) {
    bom = ini.substr(0, 3);
    ini.remove_prefix(3);
  }
  // Match the file rather than the platform: a file already using LF stays on
  // LF, and an empty one gets ReShade's own CRLF.
  const std::string_view eol =
      (ini.find("\r\n") != std::string_view::npos || ini.empty()) ? "\r\n" : "\n";

  std::vector<Line> lines;
  std::string currentSection;
  size_t start = 0;
  while (start <= ini.size()) {
    const size_t end = ini.find('\n', start);
    std::string_view raw =
        ini.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);

    if (auto name = SectionName(raw); !name.empty()) currentSection = name;
    lines.push_back(Line{std::string(raw), currentSection, KeyName(raw)});

    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  // A file ending in a newline parses to a trailing empty line. Dropping it here
  // and letting the writer put the newline back keeps the round trip stable.
  if (!lines.empty() && lines.back().text.empty() && lines.back().key.empty() &&
      SectionName(lines.back().text).empty()) {
    lines.pop_back();
  }

  // AddonPath is what makes ReShade look beside the executable for the add-on at
  // all. Without it there is no neural rendering and no error either, which is
  // the worst of both.
  const std::vector<Setting> addon{{"AddonPath", std::string(".")}};
  if (!UpdateSection(lines, kAddonSection, addon)) {
    AppendSection(lines, kAddonSection, addon);
  }

  // ReShade's effect pipeline is pinned off, and pointed somewhere that cannot
  // match anything.
  //
  // This sidecar hosts ReShade for exactly one reason: to load the add-on that
  // substitutes neural-rendered output for our own NGX call. Effects are a
  // separate mechanism -- ordinary .fx shaders applied to the swapchain at
  // present -- and none is wanted. A fresh install has no .fx files, so by
  // default nothing loads, but ReShade's own defaults point the search at the
  // executable's directory. Anything dropped in beside the sidecar would then
  // be compiled and blended into every frame, silently and on top of the neural
  // pass, and the operator would have no way to tell which was responsible for
  // what they were looking at.
  //
  // Naming a directory that does not exist is stronger than leaving the value
  // empty, because it does not depend on how ReShade treats an empty path.
  const std::vector<Setting> effects{
      {"EffectSearchPaths", std::string(".\\no-effects\\")},
      {"TextureSearchPaths", std::string(".\\no-effects\\")},
  };
  if (!UpdateSection(lines, kGeneralSection, effects)) {
    AppendSection(lines, kGeneralSection, effects);
  }
  const auto neural = NeuralKeys(settings);
  if (!UpdateSection(lines, kNeuralSection, neural)) {
    AppendSection(lines, kNeuralSection, neural);
  }

  std::string out(bom);
  for (const auto& line : lines) {
    out += line.text;
    out += eol;
  }
  return out;
}

bool WriteNeuralSettings(const std::filesystem::path& iniPath,
                         const NeuralSettings& settings) {
  std::string existing;
  if (std::ifstream in(iniPath, std::ios::binary); in) {
    existing.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  const std::string updated = ApplyNeuralSettings(existing, settings);

  std::ofstream out(iniPath, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(updated.data(), static_cast<std::streamsize>(updated.size()));
  return out.good();
}

}  // namespace sidecar
