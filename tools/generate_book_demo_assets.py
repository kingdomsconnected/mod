#!/usr/bin/env python3
"""Draw the pictures the gamemode's `/book` demo ships.

Everything is procedural and seeded, so the output is reproducible and owes
nothing to the game's own art:

- `seal.dds`: a red wax seal stamped KC, for an inline `<img>` on a page
  (DXT5, the alpha is the wax's edge).
- `parchment_diff.dds`: an aged parchment for the `texture` option (DXT1).
  It replaces the carrier's whole diffuse atlas, cover included, so the demo
  book comes out bound in plain parchment.

Output goes to resources/kcdc-gamemode/assets/book/.
"""

import argparse
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

DEFAULT_OUT = Path(__file__).resolve().parent.parent / "resources" / "kcdc-gamemode" / "assets" / "book"


def value_noise(size, cell, rng):
    """Smoothly interpolated random lattice, 0..1."""
    lattice = rng.random((size // cell + 2, size // cell + 2))
    coords = np.arange(size) / cell
    i = coords.astype(int)
    f = coords - i
    f = f * f * (3 - 2 * f)
    top = lattice[i][:, i] * (1 - f)[None, :] + lattice[i][:, i + 1] * f[None, :]
    bottom = lattice[i + 1][:, i] * (1 - f)[None, :] + lattice[i + 1][:, i + 1] * f[None, :]
    return top * (1 - f)[:, None] + bottom * f[:, None]


def fractal(size, rng, octaves=5, base=128):
    total = np.zeros((size, size))
    amplitude, weight = 1.0, 0.0
    cell = base
    for _ in range(octaves):
        total += value_noise(size, max(cell, 2), rng) * amplitude
        weight += amplitude
        amplitude *= 0.5
        cell //= 2
    return total / weight


def parchment(size, rng):
    grain = fractal(size, rng, octaves=6, base=size // 4)
    fibres = fractal(size, rng, octaves=3, base=8)
    stains = np.clip((fractal(size, rng, octaves=4, base=size // 3) - 0.58) * 4.0, 0, 1)

    y, x = np.mgrid[0:size, 0:size] / (size - 1)
    edge = np.minimum(np.minimum(x, 1 - x), np.minimum(y, 1 - y))
    vignette = np.clip(edge * 6.0, 0, 1) ** 0.6

    shade = 0.80 + 0.14 * grain + 0.05 * fibres
    shade *= 0.78 + 0.22 * vignette
    shade *= 1.0 - 0.18 * stains

    paper = np.array([0.93, 0.85, 0.68])
    rgb = np.clip(shade[..., None] * paper[None, None, :], 0, 1)
    return Image.fromarray((rgb * 255).astype(np.uint8), "RGB")


def seal(size, rng):
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    centre = size / 2

    # An irregular blob of wax: a circle whose radius wanders.
    points = []
    for step in range(180):
        angle = step / 180 * math.tau
        radius = size * 0.42 * (1 + 0.035 * math.sin(angle * 7 + rng.random()) + 0.02 * (rng.random() - 0.5))
        points.append((centre + radius * math.cos(angle), centre + radius * math.sin(angle)))
    draw.polygon(points, fill=(128, 18, 22, 255))

    # The stamped face, a shade lighter, with its rim.
    face = size * 0.31
    draw.ellipse([centre - face, centre - face, centre + face, centre + face], fill=(156, 28, 30, 255))
    draw.ellipse([centre - face, centre - face, centre + face, centre + face], outline=(96, 10, 14, 255), width=max(2, size // 64))
    inner = face * 0.86
    draw.ellipse([centre - inner, centre - inner, centre + inner, centre + inner], outline=(186, 52, 48, 255), width=max(1, size // 128))

    try:
        font = ImageFont.load_default(size=int(size * 0.24))
    except TypeError:
        font = ImageFont.load_default()
    text = "KC"
    box = draw.textbbox((0, 0), text, font=font)
    origin = (centre - (box[2] - box[0]) / 2 - box[0], centre - (box[3] - box[1]) / 2 - box[1])
    # Debossed: a dark impression with a light lip below it.
    draw.text((origin[0], origin[1] + size * 0.012), text, font=font, fill=(196, 70, 62, 255))
    draw.text(origin, text, font=font, fill=(84, 8, 12, 255))

    # Wax is never flat: modulate it with a little noise and soften the edge.
    noise = (fractal(size, rng, octaves=4, base=size // 4) - 0.5) * 0.35
    pixels = np.asarray(image).astype(np.float32)
    pixels[..., :3] *= (1.0 + noise)[..., None]
    pixels = np.clip(pixels, 0, 255).astype(np.uint8)
    image = Image.fromarray(pixels, "RGBA")
    alpha = image.getchannel("A").filter(ImageFilter.GaussianBlur(size / 256))
    image.putalpha(alpha)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    seal(256, np.random.default_rng(1347)).save(args.out / "seal.dds", pixel_format="DXT5")
    parchment(1024, np.random.default_rng(1403)).save(args.out / "parchment_diff.dds", pixel_format="DXT1")
    for name in ("seal.dds", "parchment_diff.dds"):
        print(f"{args.out / name}: {(args.out / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
