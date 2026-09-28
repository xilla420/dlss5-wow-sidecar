#!/usr/bin/env python3
"""Draw the manager's theme art: three tileable backgrounds and an emblem.

Everything here is generated, not traced or borrowed. The look is World of
Warcraft's -- carved stone, a quest-log page, the slate of the modern UI -- but
not one pixel of Blizzard's art is in it, because that art is not ours to ship.

Backgrounds are built from noise synthesised in the frequency domain, which is
periodic by construction: the tile's right edge continues its left edge, so the
manager can repeat it across any window size without a visible seam.

    python tools/make_art.py

Writes assets/art/*.png. Deterministic: the same seed gives the same pixels, so
regenerating does not churn the repository.
"""

from __future__ import annotations

import math
import pathlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT = REPO / "assets" / "art"
SIZE = 512


def periodic_noise(size: int, rng: np.random.Generator, falloff: float,
                   low_cut: float = 0.0) -> np.ndarray:
    """Tileable fractal noise in [0, 1]. `falloff` is the spectral slope: about
    1 is grainy, 2 is cloudy."""
    fx = np.fft.fftfreq(size)[:, None]
    fy = np.fft.fftfreq(size)[None, :]
    radius = np.sqrt(fx * fx + fy * fy)
    radius[0, 0] = 1.0
    spectrum = (rng.normal(size=(size, size)) + 1j * rng.normal(size=(size, size)))
    spectrum /= radius ** falloff
    spectrum[radius < low_cut] = 0
    spectrum[0, 0] = 0
    field = np.real(np.fft.ifft2(spectrum))
    field -= field.min()
    return field / field.max()


def colorize(value: np.ndarray, dark: tuple, light: tuple) -> np.ndarray:
    dark_a = np.array(dark, dtype=np.float64)
    light_a = np.array(light, dtype=np.float64)
    return dark_a + (light_a - dark_a) * value[..., None]


def save(array: np.ndarray, name: str) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    image = Image.fromarray(np.clip(array, 0, 255).astype(np.uint8))
    image.save(OUT / name, optimize=True)
    print(f"wrote {OUT / name}")


def stone(rng: np.random.Generator) -> None:
    """Dark carved blocks with mortar joints, the ground of the game's menus.
    Kept very low in contrast: body text is drawn straight on top of it."""
    base = periodic_noise(SIZE, rng, 1.6) * 0.65 + periodic_noise(SIZE, rng, 0.9) * 0.35

    # Running-bond blocks, four courses to the tile so the pattern tiles.
    mortar = np.zeros((SIZE, SIZE))
    course = SIZE // 4
    block = SIZE // 2
    y = np.arange(SIZE)[:, None]
    x = np.arange(SIZE)[None, :]
    row = y // course
    offset = (row % 2) * (block // 2)
    in_x = (x + offset) % block
    in_y = y % course
    edge = np.minimum(np.minimum(in_x, block - in_x), np.minimum(in_y, course - in_y))
    mortar = np.clip(1.0 - edge / 3.0, 0, 1)
    # Each block a slightly different shade, as cut stone is.
    block_id = row * 7 + (x + offset) // block
    shade = (np.sin(block_id * 12.9898) * 43758.5453) % 1.0
    value = base * 0.55 + shade * 0.18
    value = value * (1.0 - mortar * 0.55)
    rgb = colorize(value, (9, 8, 7), (44, 38, 31))
    save(rgb, "stone.png")


def parchment(rng: np.random.Generator) -> None:
    """A quest-log page: warm vellum, cloudy mottling, faint fibres."""
    clouds = periodic_noise(SIZE, rng, 2.1)
    grain = periodic_noise(SIZE, rng, 0.6)
    fibres = periodic_noise(SIZE, rng, 1.2)
    # Fibres are stretched along one axis by blurring the spectrum's result.
    fibre_img = Image.fromarray((fibres * 255).astype(np.uint8)).filter(
        ImageFilter.BoxBlur(1))
    fibres = np.asarray(fibre_img, dtype=np.float64) / 255.0
    value = clouds * 0.6 + grain * 0.25 + fibres * 0.15
    rgb = colorize(value, (205, 178, 128), (240, 225, 186))
    # A few darker stains, soft-edged.
    stains = np.clip((periodic_noise(SIZE, rng, 2.6) - 0.72) * 4.0, 0, 1)
    rgb *= (1.0 - stains[..., None] * 0.12)
    save(rgb, "parchment.png")


def slate(rng: np.random.Generator) -> None:
    """The modern UI's dark slate, with a hairline hex lattice at the edge of
    visibility -- texture you feel rather than see."""
    value = periodic_noise(SIZE, rng, 1.8) * 0.7 + periodic_noise(SIZE, rng, 0.8) * 0.3
    rgb = colorize(value, (13, 15, 21), (27, 31, 42))
    image = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).convert("RGBA")
    lattice = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(lattice)
    # Hexagons on a grid that divides the tile, so the lattice tiles too.
    r = SIZE / 8 / math.sqrt(3)
    w = SIZE / 8
    for row in range(-1, 11):
        for col in range(-1, 10):
            cx = col * w + (row % 2) * w / 2
            cy = row * r * 1.5
            pts = [(cx + r * math.cos(math.radians(60 * k + 30)),
                    cy + r * math.sin(math.radians(60 * k + 30))) for k in range(7)]
            draw.line(pts, fill=(200, 170, 110, 14), width=1)
    image = Image.alpha_composite(image, lattice)
    save(np.asarray(image.convert("RGB"), dtype=np.float64), "slate.png")


def emblem() -> None:
    """A round seal: runic ring, a four-pointed star, the numeral. White on
    transparent, so the manager can tint it to each theme's accent."""
    n = 512
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = n / 2
    white = (255, 255, 255, 255)
    d.ellipse([12, 12, n - 12, n - 12], outline=white, width=14)
    d.ellipse([46, 46, n - 46, n - 46], outline=white, width=5)
    # Rune ticks round the ring: short strokes and dots, like carved glyphs.
    for k in range(36):
        a = math.radians(k * 10)
        r0, r1 = c - 64, c - 44
        if k % 3 == 0:
            d.line([(c + r0 * math.cos(a), c + r0 * math.sin(a)),
                    (c + r1 * math.cos(a), c + r1 * math.sin(a))], fill=white, width=7)
        else:
            rr = c - 54
            d.ellipse([c + rr * math.cos(a) - 4, c + rr * math.sin(a) - 4,
                       c + rr * math.cos(a) + 4, c + rr * math.sin(a) + 4], fill=white)
    d.ellipse([84, 84, n - 84, n - 84], outline=white, width=4)
    # Four-pointed star behind the numeral.
    long_r, short_r = c - 100, 38
    star = []
    for k in range(8):
        a = math.radians(k * 45 - 90)
        r = long_r if k % 2 == 0 else short_r
        star.append((c + r * math.cos(a), c + r * math.sin(a)))
    d.polygon(star, fill=(255, 255, 255, 70), outline=white)
    font = None
    for name in ("georgiab.ttf", "georgia.ttf"):
        try:
            font = ImageFont.truetype(f"C:/Windows/Fonts/{name}", 200)
            break
        except OSError:
            continue
    if font:
        d.text((c, c + 6), "5", font=font, fill=white, anchor="mm",
               stroke_width=10, stroke_fill=(0, 0, 0, 0))
        d.text((c, c + 6), "5", font=font, fill=white, anchor="mm")
    OUT.mkdir(parents=True, exist_ok=True)
    img.resize((256, 256), Image.LANCZOS).save(OUT / "emblem.png", optimize=True)
    print(f"wrote {OUT / 'emblem.png'}")


def main() -> None:
    rng = np.random.default_rng(0x5EED)
    stone(rng)
    parchment(rng)
    slate(rng)
    emblem()


if __name__ == "__main__":
    main()
