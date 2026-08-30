#pragma once

namespace sidecar {

// The manager follows the spec page's palette so the tool and its
// documentation read as one thing: teal accent, 4 px corners, a monospace face
// for anything the operator is meant to compare against a number.
void ApplySidecarTheme(bool dark);

// Accent colours, exposed so probe rows can tint without duplicating literals.
struct ThemeColors {
  unsigned int accent;
  unsigned int ok;
  unsigned int warn;
  unsigned int fail;
};

ThemeColors CurrentThemeColors(bool dark);

}  // namespace sidecar
