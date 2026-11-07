#!/usr/bin/env python3
"""Generate three anti-aliased DejaVu Sans sizes for the CosmOS framebuffer UI."""
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
import sys

out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/font.h")
sizes = (12, 18, 24)
cell_w = cell_h = 32
fonts = [ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", size) for size in sizes]

lines = [
    "/* Generated high-resolution anti-aliased DejaVu Sans UI fonts. */",
    "#ifndef COSMOS_FONT_H",
    "#define COSMOS_FONT_H",
    "#include <stdint.h>",
    "static const uint8_t cosmos_font[3][95][32][32] = {",
]
for font in fonts:
    lines.append("{")
    for code in range(32, 127):
        im = Image.new("L", (cell_w, cell_h), 0)
        ImageDraw.Draw(im).text((1, 28), chr(code), font=font, fill=255, anchor="ls")
        lines.append("{")
        for y in range(cell_h):
            lines.append("{" + ",".join(str(im.getpixel((x, y))) for x in range(cell_w)) + "},")
        lines.append("},")
    lines.append("},")
lines.append("};")
lines.append("static const uint8_t cosmos_advance[3][95] = {")
for font in fonts:
    advances = [max(4, min(31, int(font.getlength(chr(code)) + 2))) for code in range(32, 127)]
    lines.append("{" + ",".join(str(v) for v in advances) + "},")
lines += ["};", "#endif", ""]
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("\n".join(lines), encoding="utf-8")
print("Generated three anti-aliased CosmOS UI font sizes:", out)
