"""Shrinks the screenshot tour's PNGs into docs/images: at most 1600 px wide, then reduced to palette images
(224 colours for opaque pixels, black at 32 alpha levels for shadows and rounded corners), like the macOS
Scripts/make-screenshots.sh. Also writes icon.png from the app icon.

    python windows/optimize-images.py <raw dir> <docs/images dir>

Needs Pillow (pip install --user Pillow).
"""
import pathlib
import sys

from PIL import Image

COLORS, LEVELS, MAX_WIDTH = 224, 31, 1600


def quantize(im):
    im = im.convert("RGBA")
    alpha = im.getchannel("A")
    q = im.convert("RGB").quantize(colors=COLORS, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
    palette = q.getpalette()[: COLORS * 3]
    palette += [0] * (COLORS * 3 - len(palette))
    index = Image.frombytes("L", im.size, q.tobytes())
    step = 255 / LEVELS
    shade = alpha.point(lambda v: COLORS + int(round(v / step)))
    opaque = alpha.point(lambda v: 255 if v == 255 else 0)
    out = Image.frombytes("P", im.size, Image.composite(index, shade, opaque).tobytes())
    out.putpalette(palette + [0, 0, 0] * (LEVELS + 1))
    return out, bytes([255] * COLORS + [int(round(i * step)) for i in range(LEVELS + 1)])


def main(raw, out):
    out.mkdir(parents=True, exist_ok=True)
    for path in sorted(raw.glob("*.png")):
        im = Image.open(path)
        if im.width > MAX_WIDTH:
            im = im.resize((MAX_WIDTH, round(im.height * MAX_WIDTH / im.width)), Image.Resampling.LANCZOS)
        small, transparency = quantize(im)
        small.save(out / path.name, optimize=True, transparency=transparency)
        print(f"{path.name}: {(out / path.name).stat().st_size // 1024} KB")
    icon = Image.open(pathlib.Path(__file__).parent / "app" / "AppIcon.ico")
    icon.size = max(icon.info.get("sizes", {icon.size}))  # the largest frame
    icon.load()
    icon.convert("RGBA").resize((256, 256), Image.Resampling.LANCZOS).save(out / "icon.png", optimize=True)
    print("icon.png")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
