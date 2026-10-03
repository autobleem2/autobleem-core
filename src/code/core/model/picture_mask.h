//
// The resume picture's mask as pure numbers (ab_gui G5s, decision 15 of docs/ab-gui-plan.md): a theme's
// launcher.menuIcons.resumePictureMask is a PNG whose alpha is multiplied into the game's save-state screenshot before
// it is drawn in the resume icon's picture window and in the slot picker's 2.7x copy. Gui::maskedResumePicture does it
// on the GPU once per picture (the Mask blend mode: the picture's colours stay, its alpha becomes picture * mask);
// this header is the arithmetic that blend implements and the size it is composed at. No SDL - tested without a
// renderer. Header-only, like cover_light.h next to it.
//
#pragma once

#include <cstdint>

namespace PictureMask {

// The masked picture is composed at this many times the window's logical size, so the 2.7x copy in the slot picker
// (and the icon zoomed to 1.5) is drawn from real pixels and not stretched from the 1x window.
const int ComposeScale = 3;

struct Size {
    int w = 0, h = 0;
};

// the logical size the masked picture is composed at for a picture window of `w` x `h`
inline Size composeSize(int w, int h) {
    Size s;
    s.w = w * ComposeScale;
    s.h = h * ComposeScale;
    return s;
}

// one alpha value (0..255) through the mask (0..255): the product, rounded - 255 leaves it, 0 removes it
inline uint8_t multiplyAlpha(uint8_t picture, uint8_t mask) {
    return static_cast<uint8_t>((static_cast<unsigned>(picture) * mask + 127u) / 255u);
}

} // namespace PictureMask
