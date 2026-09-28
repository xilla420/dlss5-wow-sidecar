---
version: alpha
name: "DLSS 5 Sidecar"
description: "A readable fantasy companion panel for a Windows overlay"
colors:
  primary: "#C8AA6E"
  background: "#0B0A08"
  panel: "#13110D"
  text: "#E8E0CE"
  muted: "#9C9380"
  danger: "#FF4A4A"
typography:
  sans:
    fontFamily: "Alegreya Sans, Segoe UI"
  display:
    fontFamily: "Cinzel, Georgia"
  mono:
    fontFamily: "Consolas"
rounded:
  DEFAULT: "1px"
  modern: "3px"
spacing:
  panel-padding: "22px"
  item-gap: "10px"
components:
  button: {}
  card: {}
  dialog: {}
---

# DLSS 5 Sidecar Design System

## Overview

### Creative North Star
A traveller's instrument panel: antique metal frames and a distant citadel,
with practical controls kept separate from the illustration. The welcome
panorama is the expressive element; forms remain quiet and legible.

### Product context and register
Windows desktop product for WoW players. The primary task is selecting a look,
checking readiness and starting the overlay. Product evidence is README.md and
docs/design/2026-08-30-dlss5-wow-sidecar-design.md. Preserve all existing runtime
safety gates and the three themes. Do not imply FPS gains or a game integration.
Ten locales are supported through core/I18n; no Japan-specific business rules
are inferred from the Japanese locale. New translations need native review.

Runtime token authority is `src/manager/Theme.cpp`: CurrentThemeColors owns
palettes, LoadThemeFonts owns faces and ApplySidecarTheme owns ImGui metrics.
The frontmatter mirrors the default Stormwind theme, not a second generator.
Quest log and Dragonflight retain their existing runtime palettes and fonts.

## Colors
Bronze primary, stone background, parchment text. Semantic green/orange/red
accompany words for positive/warning/failure states. Quest log darkens these
on its light background. The welcome image receives a theme-owned scrim.

## Typography
Retain embedded Cinzel/Alegreya Sans, IM Fell/Alegreya and Marcellus/Inter theme
pairs. Windows fonts provide Arabic and CJK fallback. Labels stay live text,
never baked into art. Japanese uses the existing CJK atlas and no italic prose.

## Layout
Header identity and start/stop action occupy the first row; theme and language
occupy a second row. Navigation owns mode selection. Content scrolls independently
above a persistent save/discard region. Presets use two columns above 680 design
pixels of content width, otherwise one. Values scale with the existing DPI logic.
Minimum native window remains 900 by 620 pixels; this is not a mobile website.

## Elevation & Depth
Tinted panels, fine metal borders and the original generated citadel image.
No looping particles, expensive blur, or animation in a GPU companion utility.

## Shapes
Retain theme-specific squared stone, parchment flourishes and slate brackets.
Primary actions use bevels with clear hover, pressed, disabled and focus states.

## Components
`Theme.cpp` owns primary buttons, frames, backgrounds and welcome art.
ImGui Button/RadioButton/Combo own keyboard interaction and popup behavior.
Never replace a setting control with a mouse-only clickable child window.
The shared save bar owns dirty/success/failure feedback on every page.
Easy mode keeps presets, strength, setup and checks; Advanced reveals detailed
tuning, editable hotkeys, live diagnostics and logs. Mode changes never reset
rendering values. Setup errors stay on Setup; configuration failures stay visible.
File and folder pickers are intentionally Windows-owned dialogs.
Scrollbars remain visible and use the global ImGui palette.

## Do's and Don'ts
- Keep Start/Stop in the same place and preserve panic hotkeys.
- Keep user preferences, paths and calibration when resetting rendering.
- Use words as well as colors for selection and status.
- Do not claim native screen-reader support: Dear ImGui does not provide it here.
- Do not animate purely decorative effects or obscure readable controls with art.
