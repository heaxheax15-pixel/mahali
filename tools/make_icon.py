#!/usr/bin/env python3
"""Generate the mahali app icon: a teal coin with an Arabic "م" glyph."""

import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "ui", "resources", "icons")
os.makedirs(OUT, exist_ok=True)

FONT = "/usr/share/fonts/truetype/noto/NotoKufiArabic-Bold.ttf"

TEAL = (7, 120, 116)          # 0x077874
TEAL_DARK = (4, 84, 84)       # 0x045454
COIN = (255, 255, 255)
ACCENT = (38, 166, 154)       # 0x26A69A
GOLD = (255, 193, 72)         # 0xFFC148


def draw_icon(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Rounded-square background with a subtle vertical gradient.
    radius = int(size * 0.22)
    steps = 24
    for i in range(steps):
        t = i / max(1, steps - 1)
        color = (int(TEAL[0] + (TEAL_DARK[0] - TEAL[0]) * t),
                 int(TEAL[1] + (TEAL_DARK[1] - TEAL[1]) * t),
                 int(TEAL[2] + (TEAL_DARK[2] - TEAL[2]) * t), 255)
        y0 = int(size * t)
        y1 = int(size * (t + 1 / steps)) + 1
        band = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        bd = ImageDraw.Draw(band)
        bd.rounded_rectangle([0, y0, size, y1], radius=radius if y0 == 0 else 0, fill=color)
        img.alpha_composite(band)

    # Coin: outer ring + inner circle.
    cx = size // 2
    coin_r = int(size * 0.42)
    d.ellipse([cx - coin_r, cx - coin_r, cx + coin_r, cx + coin_r], fill=GOLD)
    inner_r = int(coin_r * 0.88)
    d.ellipse([cx - inner_r, cx - inner_r, cx + inner_r, cx + inner_r], fill=COIN)

    # Centered Arabic "م" in teal.
    font_size = int(size * 0.52)
    font = ImageFont.truetype(FONT, font_size)
    glyph = "\u0645"  # م
    bbox = d.textbbox((0, 0), glyph, font=font, anchor="mm")
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = cx - w / 2 - bbox[0]
    y = cx - h / 2 - bbox[1]
    d.text((x, y), glyph, font=font, fill=TEAL_DARK)

    return img


def main():
    sizes = [512, 256, 128, 64, 48, 32]
    for size in sizes:
        draw_icon(size).save(os.path.join(OUT, f"app-{size}.png"))
    # Multi-size .ico for the Windows executable.
    icos = [draw_icon(s) for s in (256, 128, 64, 48, 32, 16)]
    icos[0].save(os.path.join(OUT, "mahali.ico"), format="ICO",
                 sizes=[(s, s) for s in (256, 128, 64, 48, 32, 16)],
                 append_images=icos[1:])
    print("icons written to", OUT)


if __name__ == "__main__":
    main()