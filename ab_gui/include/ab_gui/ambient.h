// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ambient motion as tweens (docs/ab-gui-plan.md, step G5o2): the recipes of the decoration that loops for as long
// as it is shown - Play's pulse, the arrow's bob, the cover glow's breathing - so the code that used them as hand-written
// clocks and the test that holds them to the old values share one definition. Each is an ambient loop: never the
// DebugDriver's busy, the frame need only Ambient (Tweens::applyFrameNeed).
//
#pragma once

#include <ab_gui/tween.h>

namespace abgui {
namespace ambient {

// core/model/timing.h's pulseWave(elapsed, periodMs) as a loop on `target`: `from` at the start, `to` at half the
// period, `from` again at its end - a cosine, slowing into both turns (Play's pulse, the arrow's bob)
inline Tween pulse(float &target, float from, float to, unsigned int periodMs) {
    return Tween(target, from, to, periodMs).ease(ease::pulse).loop().ambient();
}

// One wrap of clockPhase: 15 turns of sin(ms / 900) (2 pi x 900 ms each, 5654.8668 ms) come to 84823.002 ms - the
// nearest whole number of milliseconds to a whole number of turns, so the wrap is 2 microseconds off a sine
constexpr unsigned int ClockWrapMs = 84823;

// A float that runs with the clock in milliseconds: `startTicks` (the clock's value now, wrapped at ClockWrapMs)
// growing one per millisecond up to ClockWrapMs later, and round again. sin(phase / 900) is then what
// sin(ticks / 900) was, at any time, to a few thousandths of a millisecond (the cover glow's breathing, which
// followed the clock itself, not the time since it began). The caller sets the float to its first value
// (clockPhaseStart) - a tween writes only from its first update on.
inline float clockPhaseStart(unsigned int startTicks) {
    return static_cast<float>(startTicks % ClockWrapMs);
}
inline Tween clockPhase(float &phaseMs, unsigned int startTicks) {
    const float from = clockPhaseStart(startTicks);
    const float to = from + static_cast<float>(ClockWrapMs);
    return Tween(phaseMs, from, to, ClockWrapMs).ease(ease::linear).loop().ambient();
}

} // namespace ambient
} // namespace abgui
