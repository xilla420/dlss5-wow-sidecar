#include "core/Hotkey.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>

namespace sidecar {
namespace {

std::string Lower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (c == ' ' || c == '\t') continue;
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

// Named keys beyond letters, digits and F-keys. Spelled the way a player would
// type them into a config file; the first spelling for a key is canonical.
constexpr std::array<std::pair<std::string_view, UINT>, 22> kNamedKeys = {{
    {"backspace", VK_BACK},  {"tab", VK_TAB},        {"enter", VK_RETURN},
    {"space", VK_SPACE},     {"pageup", VK_PRIOR},   {"pagedown", VK_NEXT},
    {"end", VK_END},         {"home", VK_HOME},      {"left", VK_LEFT},
    {"up", VK_UP},           {"right", VK_RIGHT},    {"down", VK_DOWN},
    {"insert", VK_INSERT},   {"delete", VK_DELETE},  {"pause", VK_PAUSE},
    {"scrolllock", VK_SCROLL}, {"numpad0", VK_NUMPAD0}, {"numpad1", VK_NUMPAD1},
    {"numpad2", VK_NUMPAD2}, {"numpad3", VK_NUMPAD3}, {"ins", VK_INSERT},
    {"del", VK_DELETE},
}};

constexpr std::array<std::string_view, 22> kCanonicalNames = {
    "Backspace", "Tab",  "Enter", "Space",  "PageUp",     "PageDown",
    "End",       "Home", "Left",  "Up",     "Right",      "Down",
    "Insert",    "Delete", "Pause", "ScrollLock", "Numpad0", "Numpad1",
    "Numpad2",   "Numpad3", "Insert", "Delete",
};

std::optional<UINT> ParseKey(const std::string& key) {
  if (key.size() == 1) {
    const char c = key[0];
    if (c >= 'a' && c <= 'z') return static_cast<UINT>('A' + (c - 'a'));
    if (c >= '0' && c <= '9') return static_cast<UINT>(c);
    return std::nullopt;
  }
  if (key[0] == 'f' && key.size() <= 3) {
    int n = 0;
    for (size_t i = 1; i < key.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(key[i]))) return std::nullopt;
      n = n * 10 + (key[i] - '0');
    }
    if (n >= 1 && n <= 24) return static_cast<UINT>(VK_F1 + n - 1);
    return std::nullopt;
  }
  for (const auto& [name, vk] : kNamedKeys) {
    if (key == name) return vk;
  }
  return std::nullopt;
}

}  // namespace

std::optional<Hotkey> ParseHotkey(std::string_view text) {
  const std::string flat = Lower(text);
  if (flat.empty()) return std::nullopt;

  Hotkey hotkey;
  bool haveKey = false;
  size_t start = 0;
  while (start <= flat.size()) {
    const size_t plus = flat.find('+', start);
    const std::string part =
        flat.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
    if (part.empty()) return std::nullopt;   // "Ctrl++D" or a trailing '+'

    if (part == "ctrl" || part == "control") {
      hotkey.modifiers |= MOD_CONTROL;
    } else if (part == "alt") {
      hotkey.modifiers |= MOD_ALT;
    } else if (part == "shift") {
      hotkey.modifiers |= MOD_SHIFT;
    } else if (part == "win") {
      hotkey.modifiers |= MOD_WIN;
    } else {
      if (haveKey) return std::nullopt;       // two keys in one chord
      const auto vk = ParseKey(part);
      if (!vk) return std::nullopt;
      hotkey.vk = *vk;
      haveKey = true;
    }

    if (plus == std::string::npos) break;
    start = plus + 1;
  }

  if (!haveKey) return std::nullopt;
  if ((hotkey.modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)) == 0) return std::nullopt;
  return hotkey;
}

std::string FormatHotkey(const Hotkey& hotkey) {
  std::string out;
  if (hotkey.modifiers & MOD_CONTROL) out += "Ctrl+";
  if (hotkey.modifiers & MOD_ALT) out += "Alt+";
  if (hotkey.modifiers & MOD_SHIFT) out += "Shift+";
  if (hotkey.modifiers & MOD_WIN) out += "Win+";

  const UINT vk = hotkey.vk;
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
    out.push_back(static_cast<char>(vk));
  } else if (vk >= VK_F1 && vk <= VK_F24) {
    out += "F" + std::to_string(vk - VK_F1 + 1);
  } else {
    for (size_t i = 0; i < kNamedKeys.size(); ++i) {
      if (kNamedKeys[i].second == vk) {
        out += kCanonicalNames[i];
        return out;
      }
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", vk);
    out += buffer;
  }
  return out;
}

bool RegisterThreadHotkey(int id, const Hotkey& hotkey) {
  return RegisterHotKey(nullptr, id, hotkey.modifiers | MOD_NOREPEAT, hotkey.vk) != 0;
}

}  // namespace sidecar
