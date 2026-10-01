"""Draws the two background fixtures of test_theme_color_deriver: dark.ppm, light.ppm and mono.ppm (64x36, binary PPM).

Our own pictures - flat blocks over a gentle gradient, a few hand-picked colours - so the test never needs a
community theme. Deterministic: no random numbers. Run:  python make_color_fixtures.py  (writes next to itself).
"""
import os

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


# dark navy art, a big orange emblem (about 20% of the picture), a small teal strip
dark = picture((14, 18, 36), (6, 8, 20), [(36, 6, 60, 26, (226, 112, 30)), (4, 28, 30, 32, (24, 150, 150))])
# light parchment art, a blue band (about 22%), a small green patch
light = picture((236, 226, 198), (214, 200, 168), [(0, 22, 64, 30, (40, 90, 170)), (8, 4, 18, 12, (70, 150, 80))])
write_ppm("dark.ppm", dark)
write_ppm("light.ppm", light)
# black-and-white art: greys only, no chromatic cluster anywhere (the monochrome fallback)
mono = picture((30, 30, 30), (90, 90, 90), [(36, 6, 60, 26, (210, 210, 210)), (4, 28, 30, 32, (8, 8, 8))])
write_ppm("mono.ppm", mono)
