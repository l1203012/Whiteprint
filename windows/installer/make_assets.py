"""Generates windows/app/AppIcon.ico (from Resources/App/AppIcon.icns) and the installer wizard
bitmaps (blueprint style). Needs Pillow: python -m pip install --user Pillow"""
import os
from PIL import Image, ImageDraw, ImageFont

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", ".."))
BLUE = (0x1E, 0x4D, 0x8C)
ACCENT = (0x9F, 0xD3, 0xFF)

icon = Image.open(os.path.join(root, "Resources", "App", "AppIcon.icns")).convert("RGBA")
icon = icon.resize((1024, 1024), Image.LANCZOS) if icon.size != (1024, 1024) else icon
icon.save(os.path.join(root, "windows", "app", "AppIcon.ico"),
          sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

def font(size, bold=False):
    for n in (("segoeuib.ttf" if bold else "segoeui.ttf"), "arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(n, size)
        except OSError:
            pass
    return ImageFont.load_default()

def blueprint(w, h):
    im = Image.new("RGB", (w, h), BLUE)
    d = ImageDraw.Draw(im, "RGBA")
    step = max(8, w // 10)
    for x in range(0, w, step):
        d.line([(x, 0), (x, h)], fill=(255, 255, 255, 18))
    for y in range(0, h, step):
        d.line([(0, y), (w, y)], fill=(255, 255, 255, 18))
    return im

def paste_icon(im, size, xy):
    im.paste(icon.resize((size, size), Image.LANCZOS), xy, icon.resize((size, size), Image.LANCZOS))

for scale, suffix in ((1, ""), (2, "@2x")):
    w, h = 164 * scale, 314 * scale
    im = blueprint(w, h)
    paste_icon(im, 96 * scale, (34 * scale, 40 * scale))
    d = ImageDraw.Draw(im)
    d.text((w // 2, 170 * scale), "Whiteprint", font=font(24 * scale, True), fill="white", anchor="mm")
    d.text((w // 2, 198 * scale), "Blueprint notes", font=font(11 * scale), fill=ACCENT, anchor="mm")
    im.save(os.path.join(here, f"wizard{suffix}.bmp"))
    s = 55 * scale
    sm = blueprint(s, s)
    paste_icon(sm, 40 * scale, (8 * scale, 8 * scale))
    sm.save(os.path.join(here, f"wizard-small{suffix}.bmp"))
print("ok")
