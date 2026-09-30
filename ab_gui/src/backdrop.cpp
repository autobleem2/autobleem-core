// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui backdrop snapshot: see the header.
//
#include <ab_gui/backdrop.h>

namespace abgui {

//*******************************
// BackdropSnapshot
//*******************************
void BackdropSnapshot::set(const ableem::Texture &frame, unsigned long targetsLost) {
    frame_ = frame;
    lostAt_ = targetsLost;
}

void BackdropSnapshot::clear() {
    frame_ = ableem::Texture();
    lostAt_ = 0;
}

bool BackdropSnapshot::draw(ableem::Renderer &renderer) const {
    if (!usable(renderer.targetsLost()))
        return false;
    renderer.copy(frame_, nullptr, nullptr);
    return true;
}

} // namespace abgui
