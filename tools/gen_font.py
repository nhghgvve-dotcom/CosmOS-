#!/usr/bin/env python3
"""Generate an anti-aliased 7x12 ASCII font for the freestanding CosmOS UI."""
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
import sys
out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/font.h")
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
lines = ["/* Generated from DejaVu Sans. */", "#ifndef COSMOS_FONT_H", "#define COSMOS_FONT_H", "#include <stdint.h>", "static const uint8_t cosmos_font[95][12][8] = {"]
for code in range(32, 127):
    im = Image.new("L", (7, 12), 0)
    ImageDraw.Draw(im).text((0, 9), chr(code), font=font, fill=255, anchor="ls")
    lines.append(" {")
    for y in range(12):
        lines.append("  {" + ",".join(str(im.getpixel((x, y))) for x in range(7)) + ",0},")
    lines.append(" },")
lines += ["};", "#endif", ""]
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("\n".join(lines), encoding="utf-8")
print("Generated anti-aliased CosmOS UI font:", out)
