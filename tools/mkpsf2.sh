#!/bin/sh
set -eu

output=$1

python3 - "$output" <<'PY'
import struct
import sys
from PIL import Image, ImageDraw, ImageFont

output = sys.argv[1]
width = height = 16
font = ImageFont.truetype("/usr/share/fonts/TTF/DejaVuSansMono.ttf", 16)

with open(output, "wb") as stream:
    stream.write(struct.pack("<8I", 0x864AB572, 0, 32, 0, 256,
                             height * ((width + 7) // 8), height, width))
    for codepoint in range(256):
        image = Image.new("1", (width, height), 0)
        if 32 <= codepoint <= 126:
            ImageDraw.Draw(image).text((0, -1), chr(codepoint), font=font, fill=1)
        for y in range(height):
            bits = 0
            for x in range(width):
                if image.getpixel((x, y)):
                    bits |= 1 << (7 - (x & 7))
                if x % 8 == 7:
                    stream.write(bytes((bits,)))
                    bits = 0
PY
