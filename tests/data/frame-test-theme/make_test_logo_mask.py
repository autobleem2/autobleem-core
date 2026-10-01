#!/usr/bin/env python3
# The test theme's launcher logo and resume picture mask (ab_gui G5q, G5s - docs/ab-gui-plan.md decisions 14, 15)
#
# images/logo.png (+ @2x): the launcher logo, 317 x 75 logical px like the designer's ab2.0.0 one (placed at 22, 591 by
# theme.json). Colour-coded so a screenshot shows which file was drawn: the 1x file is ORANGE (255, 110, 0), the @2x file
# SKY BLUE (60, 170, 255), each with a 4 px near-black rim and a white square at the left end (so the orientation and
# the size show).
#
# images/resume_mask.png (+ @2x): the resume picture mask, 68 x 52 logical px (the resume icon's picture window). Only
# its alpha matters: opaque white with the four corners cut off by a diagonal, 16 px per side - the save-state picture
# then shows with visibly cut corners. The @2x file is the same shape at twice the pixels.
#
# Standard library only; writes into images/ next to this script.
import os
import struct
import zlib

ONE_X = (255, 110, 0)
TWO_X = (60, 170, 255)
RIM = (20, 20, 20)
WHITE = (255, 255, 255)


def png(path, w, h, pixel):
    """Writes an RGBA PNG; pixel(x, y) -> (r, g, b, a)."""
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            rows.extend(pixel(x, y))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def logo(scale, colour):
    w, h = 317 * scale, 75 * scale
    rim, square = 4 * scale, 30 * scale

    def pixel(x, y):
        if x < rim or y < rim or x >= w - rim or y >= h - rim:
            return RIM + (255,)
        if rim + 4 * scale <= x < rim + 4 * scale + square and (h - square) // 2 <= y < (h + square) // 2:
            return WHITE + (255,)
        return colour + (255,)

    return w, h, pixel


def mask(scale):
    w, h = 68 * scale, 52 * scale
    cut = 16 * scale

    def pixel(x, y):
        # distance from the nearest corner along both axes: inside the cut triangle = transparent
        dx = min(x, w - 1 - x)
        dy = min(y, h - 1 - y)
        alpha = 0 if dx + dy < cut else 255
        return (255, 255, 255, alpha)

    return w, h, pixel


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "images")
    os.makedirs(out, exist_ok=True)
    for scale, suffix, colour in ((1, "", ONE_X), (2, "@2x", TWO_X)):
        w, h, pixel = logo(scale, colour)
        png(os.path.join(out, "logo%s.png" % suffix), w, h, pixel)
        w, h, pixel = mask(scale)
        png(os.path.join(out, "resume_mask%s.png" % suffix), w, h, pixel)


main()
