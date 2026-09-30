#!/usr/bin/env python3
# The test theme's frames (ab_gui G4a/G4c, docs/ab-gui-frames-spec.md)
#
# panel (G4a): a dark panel with its top-left and bottom-right corners cut, a 2 px rim and an 8 px glow. The 1x image
# has a cyan rim, the @2x one an orange rim, so a screenshot shows which of the two the output scale picked.
# theme.json: slice 24 (8 bleed + 16 corner), bleed 8.
#
# selection (G4c): a 48x40 image - the row's 40x32 box with a 4 px glow, a 2 px rim and a translucent centre the row's
# text reads over. The 1x rim is magenta, the @2x one lime (the panel's are cyan and orange).
# theme.json: slice left/right 12, top/bottom 10, bleed 4.
#
# heading (G4d): a 40x24 band, the whole image is the box (no bleed), a 2 px rim and a faint white centre. The 1x rim is
# yellow, the @2x one blue.
# theme.json: slice left/right 12, top/bottom 6, bleed 0.
#
# key, keyFunction, keyLit, keySelected (G4e): 48x48 - the key's 40x40 box with a 4 px glow, a 2 px rim, a cut top-left
# and bottom-right corner. Rim colours 1x / @2x (none is a panel, selection or heading colour):
#   key          white / grey        (centre: faint white)
#   keyFunction  lilac / violet      (a quieter centre than key)
#   keyLit       cream / brown       (a strong centre)
#   keySelected  red / pink          (the strongest centre, the widest glow)
# theme.json: slice 16, bleed 4, for all four.
#
# field (G4e): 56x56 - the field's 48x48 box with a 4 px glow, a 2 px rim, square corners. Green at 1x, teal at @2x.
# theme.json: slice 16, bleed 4.
#
# Standard library only; writes frames/panel.png (64x64), panel@2x.png (128x128), selection.png (48x40),
# selection@2x.png (96x80), heading.png (40x24), heading@2x.png (80x48), key.png, key_function.png, key_lit.png,
# key_selected.png (48x48 each), field.png (56x56) and the @2x of all of them next to this script.
import math
import os
import struct
import zlib


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


class Shape:
    """A convex polygon (clockwise in screen coordinates) with a rim, a glow and a centre colour."""

    def __init__(self, width, height, bleed, poly, rim_width, centre, glow):
        self.width, self.height, self.bleed = width, height, bleed
        self.poly, self.rim_width, self.centre, self.glow = poly, rim_width, centre, glow

    def signed_dist(self, px, py):
        # negative inside the polygon
        n = len(self.poly)
        inside = True
        for i in range(n):
            ax, ay = self.poly[i]
            bx, by = self.poly[(i + 1) % n]
            if (bx - ax) * (py - ay) - (by - ay) * (px - ax) < 0:
                inside = False
                break
        d = min(seg_dist(px, py, *self.poly[i], *self.poly[(i + 1) % n]) for i in range(n))
        return -d if inside else d

    def colour_at(self, px, py, rim):
        d = self.signed_dist(px, py)
        if d <= -self.rim_width:
            return self.centre
        if d <= 0:
            return rim + (255,)
        if d < self.bleed:
            return rim + (int(round(self.glow * (1 - d / self.bleed) ** 2)),)
        return (0, 0, 0, 0)


def panel_shape():
    size, bleed, cut = 64, 8, 10
    x0, y0, x1, y1 = bleed, bleed, size - bleed, size - bleed
    poly = [(x0 + cut, y0), (x1, y0), (x1, y1 - cut), (x1 - cut, y1), (x0, y1), (x0, y0 + cut)]
    return Shape(size, size, bleed, poly, 2, (11, 22, 34, 215), 110)


def selection_shape():
    w, h, bleed = 48, 40, 4
    poly = [(bleed, bleed), (w - bleed, bleed), (w - bleed, h - bleed), (bleed, h - bleed)]
    return Shape(w, h, bleed, poly, 2, (255, 255, 255, 60), 140)


def heading_shape():
    w, h = 40, 24
    poly = [(0, 0), (w, 0), (w, h), (0, h)]
    return Shape(w, h, 0, poly, 2, (255, 255, 255, 30), 0)


def key_shape(centre_alpha, glow):
    size, bleed, cut = 48, 4, 6
    x0, y0, x1, y1 = bleed, bleed, size - bleed, size - bleed
    poly = [(x0 + cut, y0), (x1, y0), (x1, y1 - cut), (x1 - cut, y1), (x0, y1), (x0, y0 + cut)]
    return Shape(size, size, bleed, poly, 2, (255, 255, 255, centre_alpha), glow)


def field_shape():
    size, bleed = 56, 4
    poly = [(bleed, bleed), (size - bleed, bleed), (size - bleed, size - bleed), (bleed, size - bleed)]
    return Shape(size, size, bleed, poly, 2, (255, 255, 255, 45), 90)


def render(shape, scale, rim):
    w, h = shape.width * scale, shape.height * scale
    ss = 4
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            r = g = b = a = 0.0
            for j in range(ss):
                for i in range(ss):
                    c = shape.colour_at((x + (i + 0.5) / ss) / scale, (y + (j + 0.5) / ss) / scale, rim)
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
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frames")
    os.makedirs(out, exist_ok=True)
    panel, selection, heading = panel_shape(), selection_shape(), heading_shape()
    write_png(os.path.join(out, "panel.png"), *render(panel, 1, (0, 229, 255)))
    write_png(os.path.join(out, "panel@2x.png"), *render(panel, 2, (255, 152, 0)))
    write_png(os.path.join(out, "selection.png"), *render(selection, 1, (255, 0, 200)))
    write_png(os.path.join(out, "selection@2x.png"), *render(selection, 2, (140, 255, 0)))
    write_png(os.path.join(out, "heading.png"), *render(heading, 1, (255, 220, 0)))
    write_png(os.path.join(out, "heading@2x.png"), *render(heading, 2, (0, 90, 255)))
    keys = [
        ("key", key_shape(40, 60), (255, 255, 255), (150, 150, 150)),
        ("key_function", key_shape(20, 40), (190, 160, 255), (120, 60, 200)),
        ("key_lit", key_shape(110, 80), (255, 240, 200), (140, 90, 40)),
        ("key_selected", key_shape(150, 170), (255, 40, 40), (255, 150, 200)),
        ("field", field_shape(), (0, 220, 80), (0, 160, 160)),
    ]
    for name, shape, rim1, rim2 in keys:
        write_png(os.path.join(out, name + ".png"), *render(shape, 1, rim1))
        write_png(os.path.join(out, name + "@2x.png"), *render(shape, 2, rim2))


if __name__ == "__main__":
    main()
