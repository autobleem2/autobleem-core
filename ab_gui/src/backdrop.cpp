// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui backdrop snapshot: see the header.
//
#include <ab_gui/backdrop.h>

#include <ableem/engine/ext_trace.h>
#include <string>

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
    if (ableem::ext_trace::active()) {
        const ableem::Size size = frame_.valid() ? frame_.size() : ableem::Size();
        ableem::ext_trace::note(std::string("backdrop snapshot ") + (frame_.valid() ? "valid " : "INVALID ") +
                                std::to_string(size.w) + "x" + std::to_string(size.h) +
                                (usable(renderer.targetsLost()) ? " usable" : " NOT-USABLE (targets lost)"));
    }
    if (!usable(renderer.targetsLost()))
        return false;
    renderer.copy(frame_, nullptr, nullptr);
    return true;
}

} // namespace abgui
