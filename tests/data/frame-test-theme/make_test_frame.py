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
# badge (G5b): 40x40 - the 32x32 plate behind a meta-row badge with a 4 px glow, a 2 px rim, cut corners. Gold at 1x,
# violet at @2x. theme.json: slice 12, bleed 4.
#
# chip (G5d): 32x32 - the art spec's 28x28 body (2 px bleed) behind a named-key chip (START, L2+R2, ESC), a 2 px rim, all four
# corners cut, a faint glow. Mint (120, 255, 190) at 1x, maroon (128, 0, 40) at @2x. theme.json: slice 10, bleed 2.
#
# footer (G5r8): 64x62 - the footer band's 56x54 body (4 px bleed) under a panel screen's hints, a 2 px rim, the two bottom
# corners cut, a faint centre the hints read over. Copper (200, 120, 60) at 1x, steel blue (60, 140, 200) at @2x.
# theme.json: slice 16, bleed 4 (the band is footerHeight 54 tall and as wide as the panel: the middle stretches).
#
# hintBar (G5e): 80x80 - the art spec's 64x64 box (8 px bleed) behind the launcher's two hint lines, a 2 px rim, the
# top-right and bottom-left corners cut, a dark centre at about 70% the hints read over, an 8 px glow. Salmon
# (250, 128, 114) at 1x, olive (128, 128, 0) at @2x. theme.json: slice 28, bleed 8.
#
# tab (G5h): 48x48 - the set picker's current tab, the whole image is the box (no bleed), a 2 px rim, the two top corners
# cut, a faint centre the icon and label read over. Indigo (90, 80, 255) at 1x, bronze (170, 110, 0) at @2x.
# theme.json: slice 16, bleed 0.
#
# toast (G5f): 64x64 - the art spec's notification bubble: a 48x48 body (8 px bleed), a 2 px rim, the two top corners
# cut, a dark centre at about 80% the title and detail read over. Hot pink (255, 105, 180) at 1x, dark teal (0, 100, 100)
# at @2x. theme.json: slice 20, bleed 8.
#
# tile, tileSelected (G5i): 72x72 - the game menu's tile, a 64x64 body (4 px bleed), a 2 px rim, the top-left and
# bottom-right corners cut, a faint centre (tileSelected a stronger one and a wider glow) the glyph reads over. tile:
# teal (0, 200, 170) at 1x, plum (150, 40, 110) at @2x; tileSelected: amber (255, 190, 0) at 1x, crimson (200, 30, 60) at
# @2x. theme.json: slice 24, bleed 4.
#
# band (G5i): 64x64 - the resume-slot picker's strip, the whole image is the box (no bleed), a 2 px rim, square corners,
# a dark centre at about 80%. Lime (170, 255, 0) at 1x, navy (20, 40, 140) at @2x. theme.json: slice 24, bleed 0.
#
# Standard library only; writes frames/panel.png (64x64), panel@2x.png (128x128), selection.png (48x40),
# selection@2x.png (96x80), heading.png (40x24), heading@2x.png (80x48), key.png, key_function.png, key_lit.png,
# key_selected.png (48x48 each), field.png (56x56), badge.png (40x40), chip.png (32x32), footer.png (64x62), hint_bar.png (80x80), toast.png (64x64), progress_track.png and progress_fill.png (16x8) and the
# @2x of all of them next to this script.
#
# progressTrack, progressFill (G5g): 16x8, the whole image is the bar (no bleed), a 1 px rim, square corners, flat up
# and down as the art spec asks. The track has a faint white centre, hot pink (255, 105, 180) at 1x and dark green
# (0, 100, 0) at @2x; the fill a strong white centre, light blue (100, 180, 255) at 1x and burnt orange (205, 92, 0) at
# @2x. theme.json: slice left/right 4, top/bottom 2, bleed 0.
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


def badge_shape():
    size, bleed, cut = 40, 4, 6
    x0, y0, x1, y1 = bleed, bleed, size - bleed, size - bleed
    poly = [(x0 + cut, y0), (x1, y0), (x1, y1 - cut), (x1 - cut, y1), (x0, y1), (x0, y0 + cut)]
    return Shape(size, size, bleed, poly, 2, (255, 255, 255, 70), 120)


def chip_shape():
    size, bleed, cut = 32, 2, 4
    x0, y0, x1, y1 = bleed, bleed, size - bleed, size - bleed
    poly = [(x0 + cut, y0), (x1 - cut, y0), (x1, y0 + cut), (x1, y1 - cut), (x1 - cut, y1), (x0 + cut, y1),
            (x0, y1 - cut), (x0, y0 + cut)]
    return Shape(size, size, bleed, poly, 2, (255, 255, 255, 50), 100)


def footer_shape():
    w, h, bleed, cut = 64, 62, 4, 8
    x0, y0, x1, y1 = bleed, bleed, w - bleed, h - bleed
    poly = [(x0, y0), (x1, y0), (x1, y1 - cut), (x1 - cut, y1), (x0 + cut, y1), (x0, y1 - cut)]
    return Shape(w, h, bleed, poly, 2, (255, 255, 255, 28), 100)


def hint_bar_shape():
    size, bleed, cut = 80, 8, 12
    x0, y0, x1, y1 = bleed, bleed, size - bleed, size - bleed
    poly = [(x0, y0), (x1 - cut, y0), (x1, y0 + cut), (x1, y1), (x0 + cut, y1), (x0, y1 - cut)]
    return Shape(size, size, bleed, poly, 2, (24, 20, 30, 180), 110)


def tab_shape():
    size, cut = 48, 8
    poly = [(cut, 0), (size - cut, 0), (size, cut), (size, size), (0, size), (0, cut)]
    return Shape(size, size, 0, poly, 2, (255, 255, 255, 40), 0)


def tile_shape(centre_alpha, glow):
    size, bleed, cut = 72, 4, 10
    x0, y0, x1, y1 = bleed, bleed, size - bleed, size - bleed
    poly = [(x0 + cut, y0), (x1, y0), (x1, y1 - cut), (x1 - cut, y1), (x0, y1), (x0, y0 + cut)]
    return Shape(size, size, bleed, poly, 2, (255, 255, 255, centre_alpha), glow)


def band_shape():
    size = 64
    poly = [(0, 0), (size, 0), (size, size), (0, size)]
    return Shape(size, size, 0, poly, 2, (10, 8, 16, 204), 0)


def toast_shape():
    size, bleed, cut = 64, 8, 10
    x0, y0, x1, y1 = bleed, bleed, size - bleed, size - bleed
    poly = [(x0 + cut, y0), (x1 - cut, y0), (x1, y0 + cut), (x1, y1), (x0, y1), (x0, y0 + cut)]
    return Shape(size, size, bleed, poly, 2, (20, 16, 28, 204), 100)


def progress_shape(centre_alpha):
    w, h = 16, 8
    poly = [(0, 0), (w, 0), (w, h), (0, h)]
    return Shape(w, h, 0, poly, 1, (255, 255, 255, centre_alpha), 0)


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
        ("badge", badge_shape(), (255, 200, 0), (170, 80, 255)),
        ("chip", chip_shape(), (120, 255, 190), (128, 0, 40)),
        ("footer", footer_shape(), (200, 120, 60), (60, 140, 200)),
        ("hint_bar", hint_bar_shape(), (250, 128, 114), (128, 128, 0)),
        ("tab", tab_shape(), (90, 80, 255), (170, 110, 0)),
        ("tile", tile_shape(40, 60), (0, 200, 170), (150, 40, 110)),
        ("tile_selected", tile_shape(110, 170), (255, 190, 0), (200, 30, 60)),
        ("band", band_shape(), (170, 255, 0), (20, 40, 140)),
        ("toast", toast_shape(), (255, 105, 180), (0, 100, 100)),
        ("progress_track", progress_shape(40), (255, 105, 180), (0, 100, 0)),
        ("progress_fill", progress_shape(200), (100, 180, 255), (205, 92, 0)),
    ]
    for name, shape, rim1, rim2 in keys:
        write_png(os.path.join(out, name + ".png"), *render(shape, 1, rim1))
        write_png(os.path.join(out, name + "@2x.png"), *render(shape, 2, rim2))


if __name__ == "__main__":
    main()
