#!/usr/bin/env python3
# The high-resolution test theme's images (ab_gui G4f, docs/ab-gui-plan.md "G4 sub-steps")
#
# Each image comes as a 1x file and an @2x one of exactly twice the pixels, drawn from the same logical shapes in a
# different colour, so a screenshot shows which of the two the output scale picked - and, since the shapes sit at the
# same logical places, that the @2x one lands where the 1x one would:
#   background.png (1280x720)  navy with a 4 px cyan rim and a 1-pixel checkerboard in a 64x64 square at (32, 32)
#   background@2x.png          maroon with an orange rim; the checkerboard is 1 @2x pixel, so it reads finer
#   cross.png (30x30)          a cyan disc with a dark ring              @2x: orange
#   on.png, off.png (60x30)    a pill 48x26 at (2, 2) - 10 px of transparent margin at the right, which the check
#                              switch's margin measures (ThemeAssets::checkIconRightMargin, on the 1x file)
#                              on: green / @2x magenta; off: grey / @2x yellow
# theme.json names them as the classic background, the classic cross, check and uncheck, the launcher's background and
# its cross hint, over the default theme.
#
# Standard library only; writes the files next to this script.
import math
import os
import struct
import zlib

CLEAR = (0, 0, 0, 0)
DARK = (10, 10, 10, 255)


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def rgba(c):
    return bytes(c if len(c) == 4 else (c[0], c[1], c[2], 255))


def background(scale, fill, rim):
    w, h = 1280 * scale, 720 * scale
    border = 4 * scale
    fill_px, rim_px = rgba(fill), rgba(rim)
    black, white = rgba((0, 0, 0)), rgba((255, 255, 255))
    rim_row = rim_px * w
    rows = []
    for y in range(h):
        if y < border or y >= h - border:
            rows.append(rim_row)
            continue
        row = rim_px * border + fill_px * (w - 2 * border) + rim_px * border
        if 32 * scale <= y < 96 * scale:  # the checkerboard square, one image pixel per cell
            x0 = 32 * scale
            cells = b"".join(black if (x + y) % 2 == 0 else white for x in range(x0, 96 * scale))
            row = row[: x0 * 4] + cells + row[96 * scale * 4:]
        rows.append(row)
    return w, h, rows


def shape(width, height, scale, colour_at):
    w, h = width * scale, height * scale
    rows = []
    for y in range(h):
        ly = (y + 0.5) / scale
        rows.append(b"".join(rgba(colour_at((x + 0.5) / scale, ly)) for x in range(w)))
    return w, h, rows


def disc(colour):
    def at(x, y):
        d = math.hypot(x - 15, y - 15)
        if d <= 11:
            return colour
        if d <= 13:
            return DARK
        return CLEAR

    return at


def pill(colour):
    # 48x26 at (2, 2): a rectangle with round ends, a 1 px dark rim
    def at(x, y):
        if not (2 <= x < 50 and 2 <= y < 28):
            return CLEAR
        r = 13
        cx = min(max(x, 2 + r), 50 - r)
        d = math.hypot(x - cx, y - 15)
        if d > r:
            return CLEAR
        return colour if d <= r - 1 else DARK

    return at


def main():
    out = os.path.dirname(os.path.abspath(__file__))
    write_png(os.path.join(out, "background.png"), *background(1, (20, 30, 90), (0, 229, 255)))
    write_png(os.path.join(out, "background@2x.png"), *background(2, (90, 30, 20), (255, 152, 0)))
    write_png(os.path.join(out, "cross.png"), *shape(30, 30, 1, disc((0, 229, 255))))
    write_png(os.path.join(out, "cross@2x.png"), *shape(30, 30, 2, disc((255, 152, 0))))
    write_png(os.path.join(out, "on.png"), *shape(60, 30, 1, pill((0, 200, 80))))
    write_png(os.path.join(out, "on@2x.png"), *shape(60, 30, 2, pill((255, 0, 200))))
    write_png(os.path.join(out, "off.png"), *shape(60, 30, 1, pill((120, 120, 120))))
    write_png(os.path.join(out, "off@2x.png"), *shape(60, 30, 2, pill((255, 220, 0))))


if __name__ == "__main__":
    main()
