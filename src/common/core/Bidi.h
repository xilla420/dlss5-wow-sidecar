#pragma once
#include <string>
#include <string_view>

namespace sidecar {

// Just enough bidirectional-text handling to draw Arabic in a renderer that has
// none. ImGui draws codepoints left to right, one glyph each, so Arabic sent to
// it raw comes out as disconnected letters in reverse order.
//
// Two steps fix that. Shaping replaces each letter with the presentation form
// for its position in the word -- isolated, initial, medial or final -- which
// is how the letters join. Reordering then puts the line in visual order:
// runs of Arabic are reversed, runs of Latin text, digits and format
// specifiers are kept as they are, and the runs themselves are laid out right
// to left.
//
// This is not the Unicode bidirectional algorithm, and does not pretend to be.
// It has one paragraph direction (right to left), no embedding levels and no
// explicit marks, which is all a table of interface strings needs.

// Shaped, visual-order UTF-8 for one line of logical-order UTF-8. Text with no
// Arabic in it is returned unchanged.
std::string ArabicForDisplay(std::string_view logical);

bool ContainsArabic(std::string_view utf8);

}  // namespace sidecar
