#!/usr/bin/env python3
"""Draws the Glide app icon and writes assets/glide.ico, assets/Glide.icns, assets/icon-256.png.

Requires Pillow. The .icns step uses macOS's iconutil and is skipped elsewhere.
"""
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
SS = 4  # supersampling factor for smooth edges

TOP_LEFT = (25, 190, 255)
BOTTOM_RIGHT = (36, 66, 214)


def gradient(size):
    """Diagonal gradient from TOP_LEFT to BOTTOM_RIGHT."""
    small = Image.new("RGB", (256, 256))
    px = small.load()
    for y in range(256):
        for x in range(256):
            t = (x + y) / 510
            px[x, y] = tuple(round(a + (b - a) * t) for a, b in zip(TOP_LEFT, BOTTOM_RIGHT))
    return small.resize((size, size), Image.BICUBIC)


def rounded_triangle(draw, points, radius, fill):
    """Triangle with rounded corners: shrink, then stroke with round joins."""
    cx = sum(p[0] for p in points) / 3
    cy = sum(p[1] for p in points) / 3
    inner = []
    for x, y in points:
        dx, dy = x - cx, y - cy
        length = (dx * dx + dy * dy) ** 0.5
        k = (length - radius * 2) / length
        inner.append((cx + dx * k, cy + dy * k))
    draw.polygon(inner, fill=fill)
    draw.line(inner + [inner[0]], fill=fill, width=int(radius * 2), joint="curve")
    for x, y in inner:
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=fill)


def render(canvas, box_origin, box_size, shadow):
    """Draw the icon tile inside a square box on a transparent canvas (all values in px)."""
    s = box_size * SS
    ox, oy = box_origin[0] * SS, box_origin[1] * SS
    image = Image.new("RGBA", (canvas * SS, canvas * SS), (0, 0, 0, 0))
    corner = int(s * 0.225)

    if shadow:
        shade = Image.new("RGBA", image.size, (0, 0, 0, 0))
        ImageDraw.Draw(shade).rounded_rectangle(
            (ox, oy + s * 0.02, ox + s, oy + s * 1.02), corner, fill=(0, 0, 0, 110))
        image.alpha_composite(shade.filter(ImageFilter.GaussianBlur(s * 0.025)))

    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, s - 1, s - 1), corner, fill=255)
    tile = gradient(s).convert("RGBA")

    # Soft light from the top so the tile doesn't look flat.
    sheen = Image.new("L", (1, 256))
    for y in range(256):
        sheen.putpixel((0, y), max(0, 60 - y // 2))
    tile.alpha_composite(Image.merge("RGBA", (
        Image.new("L", (s, s), 255), Image.new("L", (s, s), 255), Image.new("L", (s, s), 255),
        sheen.resize((s, s)))))

    art = ImageDraw.Draw(tile)
    rounded_triangle(art, [(s * 0.41, s * 0.28), (s * 0.78, s * 0.5), (s * 0.41, s * 0.72)],
                     s * 0.035, (255, 255, 255, 255))
    # Speed lines go on their own layer so their translucency blends with the gradient.
    lines = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    bar = s * 0.06
    for y, x0 in ((0.375, 0.19), (0.5, 0.11), (0.625, 0.22)):
        ImageDraw.Draw(lines).rounded_rectangle((s * x0, s * y - bar / 2, s * 0.335, s * y + bar / 2),
                                                bar / 2, fill=(255, 255, 255, 170))
    tile.alpha_composite(lines)

    image.paste(tile, (int(ox), int(oy)), mask)
    return image.resize((canvas, canvas), Image.LANCZOS)


def main():
    ASSETS.mkdir(exist_ok=True)

    # Windows: the tile fills the square; Explorer and the taskbar add no frame.
    windows = render(256, (4, 4), 248, shadow=False)
    windows.save(ASSETS / "icon-256.png")
    windows.save(ASSETS / "glide.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

    # macOS: Apple's grid puts an 824px tile with a shadow inside the 1024px canvas.
    mac = render(1024, (100, 92), 824, shadow=True)
    mac.save(ASSETS / "icon-1024.png")
    if shutil.which("iconutil"):
        iconset = ASSETS / "Glide.iconset"
        iconset.mkdir(exist_ok=True)
        for size in (16, 32, 128, 256, 512):
            mac.resize((size, size), Image.LANCZOS).save(iconset / f"icon_{size}x{size}.png")
            mac.resize((size * 2, size * 2), Image.LANCZOS).save(iconset / f"icon_{size}x{size}@2x.png")
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(ASSETS / "Glide.icns")], check=True)
        shutil.rmtree(iconset)
    print("Wrote", ", ".join(sorted(p.name for p in ASSETS.iterdir())))


if __name__ == "__main__":
    sys.exit(main())
