"""Render the editable SVG to native app icons. Development only; no app dependency.

Install scripts/icon-requirements.txt, then run this script from any directory.
"""
from io import BytesIO
from pathlib import Path

import cairosvg
from PIL import Image


def main():
    resources = Path(__file__).resolve().parents[1] / "resources"
    svg = (resources / "mapwright.svg").read_bytes()
    icons = resources / "icons"
    icons.mkdir(exist_ok=True)
    rendered = {}
    for size in (16, 24, 32, 48, 64, 128, 256, 512, 1024):
        # Supersample small icons so narrow walls stay legible on taskbars.
        raster = cairosvg.svg2png(bytestring=svg, output_width=size * 4, output_height=size * 4)
        image = Image.open(BytesIO(raster)).convert("RGBA").resize((size, size), Image.Resampling.LANCZOS)
        target = resources / "mapwright.png" if size == 128 else icons / f"mapwright-{size}.png"
        image.save(target, optimize=True)
        rendered[size] = image
    # ICO carries independent source resolutions, including a 256 px PNG layer.
    rendered[256].save(resources / "mapwright.ico", format="ICO",
                       sizes=[(size, size) for size in (16, 24, 32, 48, 64, 128, 256)],
                       append_images=[rendered[size] for size in (16, 24, 32, 48, 64, 128)])
    # Leave macOS's customary breathing room around the rounded tile.
    mac = Image.new("RGBA", (1024, 1024))
    art = rendered[1024].resize((896, 896), Image.Resampling.LANCZOS)
    mac.alpha_composite(art, (64, 64))
    mac.save(icons / "mapwright-macos.png", optimize=True)
    mac.save(resources / "mapwright.icns", format="ICNS")
    print("Generated PNG, Windows ICO and macOS ICNS icons")


if __name__ == "__main__":
    main()
