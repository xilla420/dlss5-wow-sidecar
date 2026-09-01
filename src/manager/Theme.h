#pragma once

struct ImFont;

namespace sidecar {

// The manager is a tool for one game, so it dresses like that game rather than
// like the graphics middleware underneath it.
//
// This is deliberately not the default ImGui skin. That skin is what ReShade
// wears, and a WoW tool that looks like ReShade invites exactly the comparison
// this project spends its whole design avoiding -- ReShade beside Wow.exe is
// the thing that gets people banned. Looking like the game's own interface is
// a small honesty about what this is for.
//
// The vocabulary is World of Warcraft's: near-black stone panels, a bronze-gold
// rule between them, parchment-toned text, and item-quality colours carrying
// status, because every WoW player already reads green/orange/red without a
// legend.
void ApplySidecarTheme(bool dark);

struct ThemeColors {
  unsigned int accent;      // bronze-gold, the interface's one accent
  unsigned int ok;          // uncommon green
  unsigned int warn;        // legendary orange
  unsigned int fail;        // a red that reads on near-black
  unsigned int goldBright;  // highlights and hovered gold
  unsigned int epic;        // epic purple, for the neural pass being live
  unsigned int parchment;   // body text
};

ThemeColors CurrentThemeColors(bool dark);

// Faces loaded from the system font directory. Nothing is redistributed: these
// are Windows' own fonts, looked up by path, and any that is missing simply
// falls back to ImGui's built-in face rather than failing.
//
// Friz Quadrata, the game's own display face, is not a Windows font and is not
// ours to ship. Georgia is the closest thing every Windows machine already has.
struct ThemeFonts {
  ImFont* body = nullptr;
  ImFont* heading = nullptr;
  ImFont* title = nullptr;
  ImFont* caption = nullptr;   // `small` is a windows.h macro
  ImFont* mono = nullptr;
};

// Call once, after ImGui::CreateContext and before the backend builds its font
// texture.
ThemeFonts LoadThemeFonts();

}  // namespace sidecar
