#!/usr/bin/env python3
# The test theme's panel frame (ab_gui G4a, docs/ab-gui-frames-spec.md): a dark panel with its top-left and
# bottom-right corners cut, a 2 px rim and an 8 px glow. The 1x image has a cyan rim, the @2x one an orange rim, so a
# screenshot shows which of the two the output scale picked. Standard library only; writes frames/panel.png (64x64)
# and frames/panel@2x.png (128x128) next to this script. theme.json: slice 24 (8 bleed + 16 corner), bleed 8.
import math
import os
import struct
import zlib

SIZE = 64    # the 1x image, logical px
BLEED = 8    # the glow's reach outside the box
CUT = 10     # the cut corners
RIM = 2      # the rim's width inside the box
CENTRE = (11, 22, 34, 215)

BOX = (BLEED, BLEED, SIZE - BLEED, SIZE - BLEED)
x0, y0, x1, y1 = BOX
POLY = [(x0 + CUT, y0), (x1, y0), (x1, y1 - CUT), (x1 - CUT, y1), (x0, y1), (x0, y0 + CUT)]


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def signed_dist(px, py):
    # negative inside the (convex, clockwise in screen coordinates) polygon
    inside = True
    for i in range(len(POLY)):
        ax, ay = POLY[i]
        bx, by = POLY[(i + 1) % len(POLY)]
        if (bx - ax) * (py - ay) - (by - ay) * (px - ax) < 0:
            inside = False
            break
    d = min(seg_dist(px, py, *POLY[i], *POLY[(i + 1) % len(POLY)]) for i in range(len(POLY)))
    return -d if inside else d


def colour_at(px, py, rim):
    d = signed_dist(px, py)
    if d <= -RIM:
        return CENTRE
    if d <= 0:
        return rim + (255,)
    if d < BLEED:
        return rim + (int(round(110 * (1 - d / BLEED) ** 2)),)
    return (0, 0, 0, 0)


def render(scale, rim):
    n = SIZE * scale
    ss = 4
    rows = []
    for y in range(n):
        row = bytearray()
        for x in range(n):
            r = g = b = a = 0.0
            for j in range(ss):
                for i in range(ss):
                    c = colour_at((x + (i + 0.5) / ss) / scale, (y + (j + 0.5) / ss) / scale, rim)
                    ca = c[3] / 255.0
                    r += c[0] * ca
                    g += c[1] * ca
                    b += c[2] * ca
                    a += ca
            k = ss * ss
            if a > 0:
                row += bytes((int(round(r / a)), int(round(g / a)), int(round(b / a)), int(round(255 * a / k))))
            else:
                row += bytes((0, 0, 0, 0))
        rows.append(bytes(row))
    return n, rows


def write_png(path, n, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frames")
    os.makedirs(out, exist_ok=True)
    write_png(os.path.join(out, "panel.png"), *render(1, (0, 229, 255)))
    write_png(os.path.join(out, "panel@2x.png"), *render(2, (255, 152, 0)))


if __name__ == "__main__":
    main()
