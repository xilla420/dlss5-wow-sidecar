#!/usr/bin/env python3
"""Draw the application icon.

Kept as a script rather than as a hand-authored .ico so the icon can be
regenerated when the palette moves, and so the palette itself is stated once:
the colours below are the manager's own, taken from Theme.cpp and from the
window caption it paints in main.cpp.

The design has to survive 16 pixels, which rules out most of what looks good at
256. What is left is a dark plaque, a bronze edge, and one large serif digit --
the same Georgia the interface uses for its headings, so the icon and the window
are recognisably the same program.

    python tools/make_icon.py

Writes assets/icon.ico with every size Windows asks for.
"""

from __future__ import annotations

import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT = REPO / "assets" / "icon.ico"

# The manager's own colours. PANEL is the window background it paints into the
# caption; BRONZE is the rule between its panels; GOLD is the accent it puts on
# headings and on the selected row.
PANEL = (0x0B, 0x0C, 0x12, 255)
BRONZE = (0xC8, 0xAA, 0x6E, 255)
GOLD = (0xE8, 0xC8, 0x7A, 255)

# Georgia stands in for the game's own display face throughout the interface,
# and it is on every Windows install. The fallbacks are the same ones Theme.cpp
# falls back to.
FONT_CANDIDATES = ["georgiab.ttf", "georgia.ttf", "palab.ttf", "pala.ttf"]

SIZES = [256, 128, 64, 48, 32, 24, 16]


def load_font(pixels: int) -> ImageFont.FreeTypeFont:
    fonts = pathlib.Path("C:/Windows/Fonts")
    for name in FONT_CANDIDATES:
        path = fonts / name
        if path.exists():
            return ImageFont.truetype(str(path), pixels)
    return ImageFont.load_default()


def draw(size: int) -> Image.Image:
    # Drawn at four times the final size and reduced, which is the cheapest way
    # to get a clean edge on the rounded corners and the digit at small sizes.
    scale = 4
    side = size * scale
    image = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    pen = ImageDraw.Draw(image)

    # The plaque. Corner radius and border weight are fractions of the side, so
    # every size is the same drawing rather than seven different ones.
    radius = side * 0.18
    border = max(scale, int(side * 0.055))
    pen.rounded_rectangle([0, 0, side - 1, side - 1], radius=radius, fill=PANEL)
    pen.rounded_rectangle(
        [border // 2, border // 2, side - 1 - border // 2, side - 1 - border // 2],
        radius=radius - border // 2,
        outline=BRONZE,
        width=border,
    )

    # One digit, as large as it can be without touching the border. Positioned
    # from its own measured box rather than from the font's metrics: the ascent
    # of a serif face leaves a digit visibly high in the square otherwise.
    font = load_font(int(side * 0.62))
    box = pen.textbbox((0, 0), "5", font=font)
    width = box[2] - box[0]
    height = box[3] - box[1]
    pen.text(
        ((side - width) / 2 - box[0], (side - height) / 2 - box[1]),
        "5",
        font=font,
        fill=GOLD,
    )

    return image.resize((size, size), Image.LANCZOS)


def main() -> int:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    images = [draw(size) for size in SIZES]
    # Pillow writes every size given in `sizes` into the one file, which is what
    # Windows wants: the shell picks 16 for the title bar and 256 for the large
    # icon view, and a file with only one of them is scaled badly for the other.
    images[0].save(OUT, format="ICO", sizes=[(s, s) for s in SIZES])
    print(f"wrote {OUT.relative_to(REPO)} with sizes {SIZES}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
