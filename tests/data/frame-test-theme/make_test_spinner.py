#!/usr/bin/env python3
# The test theme's spinner strip (ab_gui G5p, launcher.spinner - docs/ab-gui-plan.md decision 13)
#
# ONE image of 8 frames side by side, each 48x48 logical: a ring with a spoke turned by 45 degrees per frame (frame 0
# points up) and, along the top, one pip more per frame (1 pip in frame 0, 8 in the last) - so a screenshot shows which
# frame was drawn and that the strip plays in order. The 1x strip is ORANGE (255, 110, 0), its @2x twin (twice the pixels
# each way) SKY BLUE (60, 170, 255), as the test theme's icons are. theme.json: fps 8, so a full turn is one second.
#
# Standard library only; writes spinner/spinner.png and spinner/spinner@2x.png next to this script.
import math
import os
import struct
import zlib

ONE_X = (255, 110, 0)
TWO_X = (60, 170, 255)
FRAMES = 8
SIZE = 48  # a frame's logical width and height


def inside(frame, x, y):
    """Whether the logical point (x, y) of frame `frame` is inside the mark."""
    c = SIZE / 2.0
    d = math.hypot(x - c, y - c)
    if 20.0 <= d <= 22.5:  # the ring
        return True
    # the spoke: from the centre to radius 19, 3 px wide, turned frame * 45 degrees clockwise from up
    a = math.radians(frame * 360.0 / FRAMES)
    dx, dy = math.sin(a), -math.cos(a)
    along = (x - c) * dx + (y - c) * dy
    across = abs((x - c) * -dy + (y - c) * dx)
    if 0.0 <= along <= 19.0 and across <= 1.5:
        return True
    # the pips: frame + 1 of them, 3x3 px, 5 px apart, along the top
    for k in range(frame + 1):
        px = 4 + k * 5
        if px <= x <= px + 3 and 3 <= y <= 6:
            return True
    return False


def render(scale, colour):
    ss = 4
    rows = []
    for py in range(SIZE * scale):
        row = bytearray()
        for px in range(SIZE * scale * FRAMES):
            frame, fx = divmod(px, SIZE * scale)
            hits = 0
            for j in range(ss):
                for i in range(ss):
                    if inside(frame, (fx + (i + 0.5) / ss) / scale, (py + (j + 0.5) / ss) / scale):
                        hits += 1
            a = int(round(255 * hits / (ss * ss)))
            row += bytes(colour + (a,)) if a else bytes((0, 0, 0, 0))
        rows.append(bytes(row))
    return SIZE * scale * FRAMES, SIZE * scale, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "spinner")
    os.makedirs(out, exist_ok=True)
    write_png(os.path.join(out, "spinner.png"), *render(1, ONE_X))
    write_png(os.path.join(out, "spinner@2x.png"), *render(2, TWO_X))


if __name__ == "__main__":
    main()
