//
// The light on the launcher's selected cover as pure geometry: where the glow goes round the cover's face, and
// which slice of the shine texture lands where while the shine crosses it (the launcher's Carousel::drawGlow/
// drawShine draw them). Rects in, rects out - no SDL, no texture - so it is tested without a renderer.
// Header-only, like pad_assignment.h next to it.
//
// "The face" is the cover's box on screen: a jewel case's whole square, a big box's art at its own aspect
// (PsCarouselGame::content times the slot's scale). Both lights follow its real width AND height, so a tall or a
// wide box is lit round its own shape (until 2026-09-30 both took the width for the height too).
//
#pragma once

#include <algorithm>
#include <cmath>

namespace CoverLight {

// a rect in logical screen pixels
struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};

// The glow: a soft light behind the face reaching GlowMargin past each of its edges at the cover's scale 1 (the
// selected cover's; smaller with it on the way out of the middle slot).
const float GlowMargin = 44.0f;

inline Box glowBox(const Box &face, float scale) {
    const float margin = GlowMargin * scale;
    Box box;
    box.x = face.x - margin;
    box.y = face.y - margin;
    box.w = face.w + 2 * margin;
    box.h = face.h + 2 * margin;
    return box;
}

// The shine: evoimg/sheen.png, a square of white whose alpha is one soft diagonal band (its centre line from 0.63
// of the side at the top to 0.27 at the bottom, leaning left going down, peak alpha 40/255), drawn over the face
// at the face's HEIGHT - scaled uniformly, so the band keeps its angle and softness on every aspect - and clipped
// to the face's width. It crosses the face once, left to right: at t = 0 the band has not reached the face's
// left edge yet, at t = 1 it has just left its right one.
// The texture's columns that hold any of the band, as a share of its side (the file has alpha in 0.078..0.821):
const float SheenBandFrom = 0.07f, SheenBandTo = 0.83f;

struct ShineSlice {
    bool visible = false;
    int srcX = 0, srcW = 0; // the texture's columns drawn (its whole height)
    Box dst;                // where they land: face.y and face.h tall, inside the face's width
};

// `t` is the crossing's progress, 0..1 (eased by the caller); `texSide` the texture's side in the units a source
// rect is given in. Only whole texture columns that land inside the face are taken - the slice never spills past
// the face's edges, and each column is face.h / texSide wide, the band's true scale.
inline ShineSlice shineSlice(const Box &face, float t, int texSide) {
    ShineSlice slice;
    const float side = face.h;
    if (side <= 0 || face.w <= 0 || texSide <= 0)
        return slice;
    const float first = face.x - SheenBandTo * side;           // the texture's left edge at t = 0
    const float last = face.x + face.w - SheenBandFrom * side; // ... and at t = 1
    const float left = first + (last - first) * t;
    const float x0 = std::max(face.x, left), x1 = std::min(face.x + face.w, left + side);
    const float column = side / texSide;
    const float slack = 1e-3f; // a column exactly on the face's edge stays in
    const int c0 = std::max(0, static_cast<int>(std::ceil((x0 - left) / column - slack)));
    const int c1 = std::min(texSide, static_cast<int>(std::floor((x1 - left) / column + slack)));
    if (c1 <= c0)
        return slice;
    slice.visible = true;
    slice.srcX = c0;
    slice.srcW = c1 - c0;
    slice.dst.x = left + c0 * column;
    slice.dst.y = face.y;
    slice.dst.w = (c1 - c0) * column;
    slice.dst.h = side;
    return slice;
}

} // namespace CoverLight
