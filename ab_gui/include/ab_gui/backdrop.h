// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui backdrop snapshot (docs/ab-gui-plan.md, G5r5): ONE frame of the program's home screen (AutoBleem's launcher,
// taken without its hint band and bubbles) that every screen opened from it draws over, through the Context's
// backdropDrawer, instead of the theme's plain background. The program takes the frame, hands it over with set() and
// drops it with clear() when the screen it opened is gone.
//
// The frame is a render target, and SDL may lose a target's pixels (Renderer::targetsLost() counts the losses): a
// snapshot taken at one count is used only while the count is unchanged, so the screens never draw a black or garbage
// frame - draw() then says false and the caller draws its own plain background.
//
#pragma once

#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>

namespace abgui {

class BackdropSnapshot {
public:
    // keeps `frame`, taken when the renderer's targetsLost() was `targetsLost`; an invalid frame keeps nothing
    void set(const ableem::Texture &frame, unsigned long targetsLost);
    // forgets it
    void clear();
    // one is kept (even if its pixels were lost since): the screens opened over it draw without the theme's logo
    bool held() const { return frame_.valid(); }
    // the frame is kept and its pixels still stand, the renderer's targetsLost() being `targetsLost` now
    bool usable(unsigned long targetsLost) const { return frame_.valid() && targetsLost == lostAt_; }
    // draws the frame over the whole target and says true; false (nothing drawn) when it is not usable
    bool draw(ableem::Renderer &renderer) const;

private:
    ableem::Texture frame_;
    unsigned long lostAt_ = 0;
};

} // namespace abgui
