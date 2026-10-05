// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::HoldRepeat and DpadHold (G3f of docs/ab-gui-plan.md): the one shared hold-repeat pace. Moved here from
// the classic UI unchanged; gui/hold_repeat.h keeps the old global names as aliases.
#pragma once
//
// HoldRepeat - a held direction or button taking its step again and again, sooner the longer it is held:
// a list's Up/Down, a page's L2/R2. Not a loop of its own (as GuiScreen::fastForwardUntilAnotherEvent is):
// the screen calls due() once a frame and goes on drawing, so a progress bar or a spinner keeps moving
// while the list runs. Header-only on purpose - an extension uses it without a new export from the launcher.
//
//   on the press:    moveSelection(step); hold.press(step, now);
//   once a frame:    if (int steps = hold.due(now)) moveSelection(steps);
//   on the release:  hold.release();
//
#include <ableem/ui/input.h>

#include <cstdint>

namespace abgui {

class HoldRepeat {
public:
    // THE autorepeat of every screen - a classic list, a panel, the Options and the editor's values: one pair,
    // so a held key steps the same on old windows and new ones
    static const uint32_t RepeatDelayMs = 350;       // from the press to the first repeat
    static const uint32_t RepeatIntervalMs = 80;     // between repeats
    static const uint32_t RepeatFastAfterMs = 1200;  // held this long, they speed up...
    static const uint32_t RepeatFastIntervalMs = 30; // ...to this

    struct Timing {
        uint32_t delay;        // ms from the press to the first repeat
        uint32_t interval;     // ms between repeats
        uint32_t fastAfter;    // ms from the press after which...
        uint32_t fastInterval; // ...they come this often
    };
    // a row at a time: ~12 rows a second, ~33 once held for over a second
    static Timing rows() { return Timing{RepeatDelayMs, RepeatIntervalMs, RepeatFastAfterMs, RepeatFastIntervalMs}; }
    // a page at a time (L2/R2): slower, since each step is a whole screen
    static Timing pages() { return Timing{400, 220, 1500, 110}; }

    void press(int step, uint32_t now, Timing timing = rows()) {
        step_ = step;
        start_ = now;
        next_ = now + timing.delay;
        timing_ = timing;
    }
    void release() { step_ = 0; }
    bool held() const { return step_ != 0; }
    int step() const { return step_; }

    // the distance due by `now` - a multiple of the step, 0 when nothing is held or it is too soon. A slow
    // frame gets the repeats it missed (a few at most, so a stall does not throw the selection far).
    int due(uint32_t now) {
        if (step_ == 0)
            return 0;
        const uint32_t interval = now - start_ >= timing_.fastAfter ? timing_.fastInterval : timing_.interval;
        int repeats = 0;
        while (static_cast<int32_t>(now - next_) >= 0 && repeats < MaxPerFrame) {
            repeats++;
            next_ += interval;
        }
        if (static_cast<int32_t>(now - next_) >= 0)
            next_ = now + interval; // too far behind: the next one a step from now, not a burst
        return repeats * step_;
    }

private:
    static const int MaxPerFrame = 4;
    int step_ = 0;
    uint32_t start_ = 0;
    uint32_t next_ = 0;
    Timing timing_ = rows();
};

// THE end-of-list rule of every menu (the owner, 2026-10-05): a single PRESS at the end wraps (last row to the first,
// last value to the first, and the same backwards); a HELD direction's repeats stop at the end and never wrap.
// The next index for one step of `dir` (+1/-1) over `count` rows/values; `repeat` is true for a step that came from a
// hold's repeat (DpadHold/ValueHold::tick pass it), false for the press itself. `skip(i)` marks rows the cursor never
// rests on (a heading). With nowhere to go (empty, only headings, or a repeat at the end) it returns `index`.
template <class Skip> inline int stepIndex(int index, int dir, int count, bool repeat, Skip skip) {
    if (count <= 0)
        return index;
    int i = index;
    for (int tries = 0; tries < count; tries++) {
        i += dir;
        if (i < 0 || i >= count) {
            if (repeat)
                return index;          // held: stops at the end
            i = i < 0 ? count - 1 : 0; // pressed: wraps
        }
        if (!skip(i))
            return i;
    }
    return index;
}
inline int stepIndex(int index, int dir, int count, bool repeat) {
    return stepIndex(index, dir, count, repeat, [](int) { return false; });
}

namespace detail {
// a hold's step callback is `(int dir, bool repeat)` or, as before, `(int dir)` - the repeats of a hold are all `true`
template <class Step> auto callStep(Step &step, int dir, int) -> decltype(step(dir, true), void()) {
    step(dir, true);
}
template <class Step> void callStep(Step &step, int dir, long) {
    step(dir);
}
} // namespace detail

// A panel's loop (Up/Down held down a list): the screen keeps its own single step at the press and adds
//   on Dpad events:   hold.track(input, now);
//   once a pass:      hold.tick(input, now, [&](int dir, bool repeat) { move(dir, repeat); });
// The pass then runs at the full frame rate while a direction is held (the loop rests between presses otherwise)
// and every step is one row, at HoldRepeat's pace. Nothing held any more (a release lost during a dialog, a
// busy job) ends it at the next pass. The press steps with repeat = false (it wraps), every step tick() gives is a
// repeat (it stops at the end): stepIndex() above decides.
class DpadHold {
public:
    void track(ableem::Input &input, uint32_t now) {
        const int dir = input.dpadDown() ? 1 : (input.dpadUp() ? -1 : 0);
        if (dir == hold_.step())
            return;
        if (dir == 0) {
            stop(input);
            return;
        }
        hold_.press(dir, now);
        input.setFrameNeed(ableem::Input::FrameNeed::Active);
    }

    template <class Step> void tick(ableem::Input &input, uint32_t now, Step step) {
        if (!hold_.held())
            return;
        const int dir = hold_.step();
        if (dir > 0 ? !input.dpadDown() : !input.dpadUp()) {
            stop(input);
            return;
        }
        for (int n = hold_.due(now); n != 0; n -= dir)
            detail::callStep(step, dir, 0);
    }

private:
    void stop(ableem::Input &input) {
        hold_.release();
        input.setFrameNeed(ableem::Input::FrameNeed::Idle);
    }
    HoldRepeat hold_;
};

} // namespace abgui
