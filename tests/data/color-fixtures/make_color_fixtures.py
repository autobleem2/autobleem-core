"""Draws the two background fixtures of test_theme_color_deriver: dark.ppm, light.ppm and mono.ppm (64x36, binary PPM).

Our own pictures - flat blocks over a gentle gradient, a few hand-picked colours - so the test never needs a
community theme. Deterministic: no random numbers. Run:  python make_color_fixtures.py  (writes next to itself).
"""
import os
import struct
import zlib

W, H = 64, 36
HERE = os.path.dirname(os.path.abspath(__file__))


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def picture(top, bottom, blocks):
    """A vertical gradient top -> bottom with solid rectangles (x0, y0, x1, y1, colour) over it."""
    px = []
    for y in range(H):
        for x in range(W):
            colour = lerp(top, bottom, y / (H - 1))
            for x0, y0, x1, y1, c in blocks:
                if x0 <= x < x1 and y0 <= y < y1:
                    colour = c
            px.append(colour)
    return px


def write_ppm(name, px):
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        f.write(bytes(v for p in px for v in p))


def write_png(name, px):
    """A plain 8-bit RGB PNG (test_theme_converter reads its theme's background through the engine's image reader)."""
    raw = b"".join(b"\x00" + bytes(v for p in px[y * W:(y + 1) * W] for v in p) for y in range(H))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    with open(os.path.join(HERE, name), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


# dark navy art, a big orange emblem (about 20% of the picture), a small teal strip
dark = picture((14, 18, 36), (6, 8, 20), [(36, 6, 60, 26, (226, 112, 30)), (4, 28, 30, 32, (24, 150, 150))])
# light parchment art, a blue band (about 22%), a small green patch
light = picture((236, 226, 198), (214, 200, 168), [(0, 22, 64, 30, (40, 90, 170)), (8, 4, 18, 12, (70, 150, 80))])
write_ppm("dark.ppm", dark)
write_png("dark.png", dark)
write_ppm("light.ppm", light)
write_png("light.png", light)
# black-and-white art: greys only, no chromatic cluster anywhere (the monochrome fallback)
mono = picture((30, 30, 30), (90, 90, 90), [(36, 6, 60, 26, (210, 210, 210)), (4, 28, 30, 32, (8, 8, 8))])
write_ppm("mono.ppm", mono)
write_png("mono.png", mono)


def write_rgba_png(name, w, h, rgba):
    """A plain 8-bit RGBA PNG of w x h; rgba(x, y) -> (r, g, b, a). The converter's image-size and visibility checks."""
    raw = b"".join(b"\x00" + bytes(v for x in range(w) for v in rgba(x, y)) for y in range(h))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    with open(os.path.join(HERE, name), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def dot(x, y):
    return (230, 230, 230, 255) if (x - 15) ** 2 + (y % 30 - 15) ** 2 < 100 else (0, 0, 0, 0)


write_rgba_png("glyph30x30.png", 30, 30, dot)  # a button glyph: a white disc on nothing
write_rgba_png("strip30x200.png", 30, 200, dot)  # a 1.0 sprite strip of such glyphs, 30 x 200
write_rgba_png("blank200x68.png", 200, 68, lambda x, y: (0, 0, 0, 0))  # the stock Play button: nothing in it
write_rgba_png("button200x68.png", 200, 68, lambda x, y: (200, 40, 40, 255) if 4 < y < 64 else (0, 0, 0, 0))
