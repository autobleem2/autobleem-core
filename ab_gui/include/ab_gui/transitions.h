// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one-shot transitions as tweens (docs/ab-gui-plan.md, step G5o4): the recipes of the notification bubble's slide
// in and out and of the launcher's fade from black, so the code that uses them and the test that holds them to the old
// hand-written values share one definition. Unlike the ambient motion (ambient.h) these are NON-ambient tweens: the
// DebugDriver is busy while one runs, so a test never grabs a half-way frame.
//
// Both run a float of *milliseconds gone* on a linear tween and turn it into the picture's value with the pure
// function below - the same curve, the same integer arithmetic as the code they replaced. (A tween on the eased value
// itself could not resume a slide part-way, which a bubble that is shown again on its way out does: its curve then
// continues from the time it had left, not from the place it had reached.)
//
// The bubble's HOLD is not a tween: it is a timestamp the bubble looks at when it draws, so a bubble that stays for
// seconds never holds the DebugDriver's busy - only its two slides do.
//
#pragma once

#include <ab_gui/tween.h>

#include <cmath>

namespace abgui {
namespace transition {

// the notification bubble's slide, in from the edge and out to it
constexpr unsigned int BubbleSlideMs = 250;

// A slide's clock: `elapsedMs` runs linearly from `fromMs` (how far into the slide it already is - 0 for a fresh one,
// more for a slide that begins late or resumes) to `slideMs`, in the time that is left. Non-ambient. The caller sets
// the float to `fromMs` itself (a tween writes only from its first update on). `fromMs` is clamped to 0..slideMs.
inline Tween slideClock(float &elapsedMs, float fromMs, unsigned int slideMs = BubbleSlideMs) {
    const float slide = static_cast<float>(slideMs);
    const float from = fromMs < 0.0f ? 0.0f : (fromMs > slide ? slide : fromMs);
    return Tween(elapsedMs, from, slide, static_cast<unsigned int>(slide - from)).ease(ease::linear);
}

// how far in the bubble is, 0 (past the edge) .. 1 (in place), `elapsedMs` into its slide: easeOutCubic of the time
// for a slide in, its mirror for a slide out (the old NotificationBubble::render, formula for formula)
inline float bubbleProgress(bool slidingOut, float elapsedMs, unsigned int slideMs = BubbleSlideMs) {
    const float t = elapsedMs / static_cast<float>(slideMs);
    return slidingOut ? 1.0f - ease::outCubic(t) : ease::outCubic(t);
}

// The fade from black: `fadeMs` runs linearly from 0 to `durationMs`. Non-ambient.
inline Tween fadeInClock(float &fadeMs, unsigned int durationMs) {
    return Tween(fadeMs, 0.0f, static_cast<float>(durationMs), durationMs).ease(ease::linear);
}

// the black overlay's alpha `fadeMs` into a fade of `durationMs`: 255 - 255 x ms / duration in whole milliseconds and
// integer arithmetic (the old GuiLauncher::render), 0 from the end on
inline int fadeInAlpha(float fadeMs, unsigned int durationMs) {
    const long ms = std::lround(fadeMs);
    if (ms >= static_cast<long>(durationMs))
        return 0;
    return 255 - static_cast<int>(255 * ms / static_cast<long>(durationMs));
}

} // namespace transition
} // namespace abgui
