// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui spinner strip (docs/ab-gui-plan.md, G5p): the busy spinner as a theme element - ONE image with N animation
// frames side by side (1x + @2x, the pick every theme image makes: pickImageFile, frame.h), played centred where the
// code-drawn ring of dots is (Style::spinner through a Context, so Busy's spinner and waitScreen's). Every number is
// logical: the 1x strip is `frames` frames of its size().w / frames wide, the @2x one twice the pixels, loaded above
// output scale 1 at pixel scale 2 (loadIcon), so a caller never knows which was drawn. The frame shown is
// (elapsed * fps / 1000) mod frames. A theme without a strip keeps the ring of dots, call for call.
//
// A program describes its strip as a SpinnerSpec and keeps it in a SpinnerStrip, which loads the texture on the first
// ask and drops it when the display goes (release()); the Context hands it out through its spinnerProvider.
//
#pragma once

#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>
#include <ableem/ui/types.h>

#include <string>

namespace abgui {

// a strip as a program describes it: the files are absolute paths ("" = none of that size)
struct SpinnerSpec {
    std::string file;   // the 1x strip
    std::string file2x; // the @2x strip (twice the pixels)
    int frames = 0;     // the frames side by side; below 1 = no strip
    int fps = 0;        // frames per second; below 1 = no strip
};

// what the Context's spinnerProvider hands out: the loaded strip and its play numbers; invalid = draw the ring
struct SpinnerAnim {
    ableem::Texture strip;
    int frames = 0;
    int fps = 0;

    // a strip that loaded, holds at least one whole logical pixel column per frame and has a positive rate
    bool valid() const;
};

//*******************************
// the pure rules (tested)
//*******************************
// the frame shown `elapsedMs` into the animation: (elapsed * fps / 1000) mod frames; 0 for frames or fps below 1
int spinnerFrameIndex(unsigned long long elapsedMs, int fps, int frames);
// frame `index` (wrapped into 0..frames-1) of a strip `stripSize` logical px big: frames of stripSize.w / frames wide
// (the odd leftover columns are not drawn) and the strip's whole height, side by side from the left
ableem::Rect spinnerFrameRect(const ableem::Size &stripSize, int frames, int index);
// where a frame of `frame` size is drawn to sit centred on (cx, cy): the size's odd half a pixel goes right/down
ableem::Rect spinnerDestRect(const ableem::Size &frame, int cx, int cy);

// The strip's texture at the renderer's output scale (loadIcon's rule): the @2x file above scale 1 when there is one,
// else the 1x file. Invalid when neither loads.
ableem::Texture loadSpinnerStrip(ableem::Renderer &renderer, const SpinnerSpec &spec);

//********************
// SpinnerStrip
//********************
class SpinnerStrip {
public:
    // the theme's strip (the old one and its texture dropped); a spec with no file or a rate/count below 1 is none
    void assign(const SpinnerSpec &spec);
    // drops the texture and keeps the spec - before the renderer goes (a display release)
    void release();
    // whether there is a strip to try (the texture may still fail to load)
    bool has() const { return usable_; }
    const SpinnerSpec &spec() const { return spec_; }

    // the strip, loaded on the first call (one try per load: a bad file is logged once); an invalid SpinnerAnim when
    // there is no strip or it does not load
    SpinnerAnim anim(ableem::Renderer &renderer);

private:
    SpinnerSpec spec_;
    bool usable_ = false;
    bool tried_ = false;
    ableem::Texture texture_;
};

} // namespace abgui
