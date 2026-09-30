#!/usr/bin/env python3
# The test theme's icons (ab_gui G5a, launcher.icons - docs/ab-gui-evoui-art-spec.md, 3.)
#
# Every icon of the art spec's table at its 1x size and its @2x twin, colour-coded so a screenshot shows which file was
# drawn: the 1x files are ORANGE (255, 110, 0), the @2x files SKY BLUE (60, 170, 255) - neither is a frame colour of
# this theme. The d-pad arrows are triangles pointing their way; the switches a pill with the knob on the right (on)
# or the left (off); the battery an outline with a nub; the covers and the big box a square with a 7 px rim; every
# other icon a disc. Each shape keeps 1 px clear all round (the halo needs it), and theme.json leaves "iconHalo" at its
# default, so the code's dark halo shows under the 1x/@2x colours.
#
# Standard library only; writes icons/<name>.png and icons/<name>@2x.png next to this script.
import math
import os
import struct
import zlib

ONE_X = (255, 110, 0)
TWO_X = (60, 170, 255)

# name -> (file stem, 1x width, 1x height, shape)
ICONS = {
    "players": ("players", 30, 30, "disc"),
    "disc": ("disc", 30, 30, "disc"),
    "usb": ("usb", 30, 30, "disc"),
    "internal": ("internal", 30, 30, "disc"),
    "hd": ("hd", 30, 30, "disc"),
    "sd": ("sd", 30, 30, "disc"),
    "lock": ("lock", 30, 30, "disc"),
    "unlock": ("unlock", 30, 30, "disc"),
    "favorite": ("favorite", 30, 30, "disc"),
    "retroarch": ("retroarch", 30, 30, "disc"),
    "lightgun": ("lightgun", 30, 30, "disc"),
    "lightgun2": ("lightgun2", 30, 30, "disc"),
    "dpadUp": ("dpad_up", 28, 28, "up"),
    "dpadDown": ("dpad_down", 28, 28, "down"),
    "dpadLeft": ("dpad_left", 28, 28, "left"),
    "dpadRight": ("dpad_right", 28, 28, "right"),
    "tabPlayStation": ("tab_playstation", 56, 56, "disc"),
    "tabRetroArch": ("tab_retroarch", 56, 56, "disc"),
    "tabApps": ("tab_apps", 56, 56, "disc"),
    "raCover": ("ra_cover", 226, 226, "box"),
    "appCover": ("app_cover", 226, 226, "box"),
    "bigBox": ("big_box", 226, 226, "rim"),
    "extension": ("extension", 56, 56, "disc"),
    "battery": ("battery", 29, 13, "battery"),
    "play": ("play", 28, 28, "right"),
    "switchOn": ("switch_on", 60, 30, "on"),
    "switchOff": ("switch_off", 60, 30, "off"),
}


def inside(shape, x, y, w, h):
    """Whether the logical point (x, y) is in the shape drawn in a w x h canvas (1 px clear all round)."""
    x0, y0, x1, y1 = 1.0, 1.0, w - 1.0, h - 1.0
    cx, cy = w / 2.0, h / 2.0
    if shape == "disc":
        return math.hypot(x - cx, y - cy) <= min(w, h) / 2.0 - 1.0
    if shape in ("up", "down", "left", "right"):
        # a triangle filling the canvas' inner square, pointing its way
        u, v = (x - x0) / (x1 - x0), (y - y0) / (y1 - y0)
        if not (0 <= u <= 1 and 0 <= v <= 1):
            return False
        if shape == "up":
            return abs(u - 0.5) <= v / 2
        if shape == "down":
            return abs(u - 0.5) <= (1 - v) / 2
        if shape == "left":
            return abs(v - 0.5) <= u / 2
        return abs(v - 0.5) <= (1 - u) / 2
    if shape == "box":
        return x0 <= x <= x1 and y0 <= y <= y1
    if shape == "rim":
        return x0 <= x <= x1 and y0 <= y <= y1 and not (7 <= x <= w - 7 and 7 <= y <= h - 7)
    if shape == "battery":
        body = x0 <= x <= w - 4 and y0 <= y <= y1 and not (2 <= x <= w - 5 and 2 <= y <= h - 2)
        nub = w - 4 <= x <= x1 and h / 2 - 3 <= y <= h / 2 + 3
        return body or nub
    if shape in ("on", "off"):
        r = (y1 - y0) / 2
        pill = (x0 + r <= x <= x1 - r and y0 <= y <= y1) or math.hypot(x - (x0 + r), y - cy) <= r or math.hypot(
            x - (x1 - r), y - cy) <= r
        # the knob: a clear 2 px ring cut round it, on the right for on, on the left for off
        knob_x = x1 - r if shape == "on" else x0 + r
        return pill and not (r - 3 < math.hypot(x - knob_x, y - cy) <= r - 1)
    return False


def render(shape, w, h, scale, colour):
    pw, ph = w * scale, h * scale
    ss = 4
    rows = []
    for py in range(ph):
        row = bytearray()
        for px in range(pw):
            hits = 0
            for j in range(ss):
                for i in range(ss):
                    if inside(shape, (px + (i + 0.5) / ss) / scale, (py + (j + 0.5) / ss) / scale, w, h):
                        hits += 1
            a = int(round(255 * hits / (ss * ss)))
            row += bytes(colour + (a,)) if a else bytes((0, 0, 0, 0))
        rows.append(bytes(row))
    return pw, ph, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")
    os.makedirs(out, exist_ok=True)
    for stem, w, h, shape in ICONS.values():
        write_png(os.path.join(out, stem + ".png"), *render(shape, w, h, 1, ONE_X))
        write_png(os.path.join(out, stem + "@2x.png"), *render(shape, w, h, 2, TWO_X))


if __name__ == "__main__":
    main()
