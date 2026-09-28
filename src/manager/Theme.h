#pragma once
#include <string_view>

#include <imgui.h>

struct ID3D11Device;

namespace sidecar {

enum class Language;

// The manager is a tool for one game, so it dresses like that game rather than
// like the graphics middleware underneath it.
//
// This is deliberately not the default ImGui skin. That skin is what ReShade
// wears, and a WoW tool that looks like ReShade invites exactly the comparison
// this project spends its whole design avoiding -- ReShade beside Wow.exe is
// the thing that gets people banned.
//
// Three looks, all in the game's vocabulary and chosen by the operator:
//
//   Stormwind     the game's Esc menu: carved stone, a double gold frame,
//                 red buttons with a gold rim. Cinzel over Alegreya Sans.
//   Quest log     a parchment page in dark ink, headings in a deep red, a
//                 wax-seal start button. IM Fell English over Alegreya.
//   Dragonflight  the modern retail UI: dark slate, thin gold hairlines,
//                 corner brackets. Marcellus over Inter.
//
// Status keeps the item-quality colours in every theme, because every WoW
// player already reads green/orange/red/purple without a legend -- only their
// exact shade moves, so they still read on a light page.
enum class ThemeId { Stormwind, QuestLog, Dragonflight, Count };

const char* TagForTheme(ThemeId theme);
bool ParseThemeTag(std::string_view tag, ThemeId& out);
// English, and passed through Tr() by the caller.
const char* ThemeDisplayName(ThemeId theme);

struct ThemeColors {
  unsigned int accent;      // the theme's one accent: bronze-gold, or brown ink
  unsigned int ok;          // uncommon green
  unsigned int warn;        // legendary orange
  unsigned int fail;        // a red that reads on this ground
  unsigned int goldBright;  // headings, highlights, the selected row
  unsigned int epic;        // epic purple, for the neural pass being live
  unsigned int parchment;   // body text
  unsigned int muted;       // captions and disabled text
  unsigned int window;      // the ground everything sits on
  unsigned int panel;       // a panel over that ground
  unsigned int buttonFace;  // the primary button
  unsigned int buttonRim;
  unsigned int buttonText;
  bool light;               // a light page, which flips a few decisions
};

ThemeColors CurrentThemeColors(ThemeId theme);

// Faces for one theme. Display faces are the theme's own, embedded in the
// executable under the SIL Open Font License; anything they lack -- Cyrillic,
// Turkish, Arabic, CJK -- is merged in from the fonts every Windows install
// has, so a heading in Russian or Japanese still renders rather than showing
// boxes.
struct ThemeFonts {
  ImFont* body = nullptr;
  ImFont* heading = nullptr;
  ImFont* title = nullptr;
  ImFont* caption = nullptr;   // `small` is a windows.h macro
  ImFont* mono = nullptr;
};

// Call after ImGui::CreateContext and before the backend builds its font
// texture. `scale` multiplies every face; `language` decides which glyphs the
// atlas carries, because a CJK font is tens of thousands of glyphs and only the
// few hundred the interface actually says are worth rasterising.
//
// Callable again to change theme, language or scale: it clears the atlas
// first. The caller then has to drop the backend's font texture so it is
// rebuilt on the next frame.
ThemeFonts LoadThemeFonts(ThemeId theme, float scale, Language language);

// Colours and metrics into ImGui's style. Call ScaleAllSizes afterwards.
void ApplySidecarTheme(ThemeId theme);

// The textures: one background per theme and the emblem. Decoded from the
// executable's own resources. False only if Windows' image decoder is missing,
// in which case the themes draw on flat colour instead.
bool LoadThemeArt(ID3D11Device* device);

// The theme's ground across [min, max]: its texture tiled at `scale`, then a
// vignette so the edges recede.
void DrawThemeBackdrop(ImDrawList* draw, ImVec2 min, ImVec2 max, ThemeId theme, float scale);

// A panel's frame in the theme's style -- a double gold rule with corner
// studs, an inked border with flourishes, or hairline brackets.
void DrawThemeFrame(ImDrawList* draw, ImVec2 min, ImVec2 max, ThemeId theme, float scale);

// The emblem, tinted to the theme's accent, as a square of side `size`.
void DrawEmblem(ImDrawList* draw, ImVec2 at, float size, ThemeId theme);

// Original scenic art, with a theme-owned scrim for readable overlaid text.
void DrawWelcomeArt(ImDrawList* draw, ImVec2 min, ImVec2 max, ThemeId theme);

// The primary action, drawn the way the theme draws its big buttons. `danger`
// is the stop variant. Returns true when pressed, like ImGui::Button.
bool ThemedPrimaryButton(const char* label, ImVec2 size, ThemeId theme, bool danger,
                         float scale);

}  // namespace sidecar
