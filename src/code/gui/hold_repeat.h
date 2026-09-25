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
#include <cstdint>

class HoldRepeat {
public:
    struct Timing {
        uint32_t delay;        // ms from the press to the first repeat
        uint32_t interval;     // ms between repeats
        uint32_t fastAfter;    // ms from the press after which...
        uint32_t fastInterval; // ...they come this often
    };
    // a row at a time: ~12 rows a second, ~33 once held for over a second
    static Timing rows() { return Timing{350, 80, 1200, 30}; }
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
