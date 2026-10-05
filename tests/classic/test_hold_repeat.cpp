//
// HoldRepeat: a held direction repeating its step, sooner the longer it is held, one frame at a time.
//
#include "doctest/doctest.h"

#include "gui/hold_repeat.h"

#include <vector>

TEST_CASE("HoldRepeat waits its delay, repeats, then repeats faster") {
    HoldRepeat hold;
    CHECK(hold.due(1000) == 0); // nothing held
    hold.press(1, 1000);
    CHECK(hold.held());
    CHECK(hold.due(1000) == 0);
    CHECK(hold.due(1349) == 0); // the delay (350 ms) not over yet
    CHECK(hold.due(1350) == 1); // the first repeat
    CHECK(hold.due(1400) == 0);
    CHECK(hold.due(1430) == 1); // then every 80 ms
    // held for over 1.2 s: every 30 ms - a frame that took 60 ms gets the two it missed
    int moved = 0;
    for (uint32_t t = 1440; t <= 2200; t += 10)
        moved += hold.due(t);
    CHECK(moved > 0);
    CHECK(hold.due(2230) == 1);
    CHECK(hold.due(2290) == 2);
    hold.release();
    CHECK_FALSE(hold.held());
    CHECK(hold.due(5000) == 0);
}

TEST_CASE("HoldRepeat moves by its step, a page's too, and never bursts after a stall") {
    HoldRepeat hold;
    hold.press(-7, 0, HoldRepeat::pages());
    CHECK(hold.due(399) == 0);
    CHECK(hold.due(400) == -7);
    CHECK(hold.due(620) == -7); // 220 ms later
    // a ten-second stall (a dialog, a slow disk): four steps at most, then back to the normal pace
    CHECK(hold.due(10620) == -28);
    CHECK(hold.due(10620) == 0);
    CHECK(hold.due(10730) == -7); // held long enough for the fast pace (110 ms)
}

TEST_CASE("HoldRepeat with the Options value row's own timing (400/120/1500/60): a tap is one step, a hold "
          "scrolls without loading the value on every step") {
    // GuiOptions/GuiGameEditor's value rows (Theme, Music, Language, the font) use this exact Timing -
    // Left/Right steps the value once on ButtonDown (the caller's own moveSelection(), not due()) and then
    // starts the hold; only the release actually loads/applies the landed-on value, so due() itself must
    // stay quiet until the delay is up and then repeat at the slower, then faster, pace.
    const HoldRepeat::Timing valueHoldTiming{400, 120, 1500, 60};
    HoldRepeat hold;
    hold.press(1, 1000, valueHoldTiming);
    CHECK(hold.held());
    // nothing more due until the 400 ms delay elapses - a tap (press then release well inside it) never
    // gets a second step out of due()
    CHECK(hold.due(1000) == 0);
    CHECK(hold.due(1200) == 0);
    CHECK(hold.due(1399) == 0);
    CHECK(hold.due(1400) == 1); // the first repeat, exactly at the delay
    CHECK(hold.due(1450) == 0);
    CHECK(hold.due(1520) == 1); // then every 120 ms, short of the 1500 ms fast-pace mark

    // held on past the 1000 + 1500 ms fast-pace mark: called every 10 ms, as a real frame loop would, the
    // repeats keep coming and speed up - never a burst bigger than a stall deserves
    int moved = 0;
    for (uint32_t t = 1530; t <= 2600; t += 10)
        moved += hold.due(t);
    CHECK(moved > 0);

    hold.release();
    CHECK_FALSE(hold.held());
    // a release mid-hold stops the repeats for good until the next press - no leftover step sneaks in
    CHECK(hold.due(3000) == 0);
}

TEST_CASE("HoldRepeat copes with the tick counter wrapping") {
    HoldRepeat hold;
    const uint32_t nearEnd = 0xFFFFFF00u;
    hold.press(1, nearEnd);
    CHECK(hold.due(nearEnd + 100) == 0);
    CHECK(hold.due(nearEnd + 350) == 1); // wrapped past 0
}

TEST_CASE("every screen's held key uses the one shared pair") {
    // copies: a static const member handed to CHECK by reference would need a definition
    const uint32_t delay = HoldRepeat::RepeatDelayMs, interval = HoldRepeat::RepeatIntervalMs;
    const uint32_t fastAfter = HoldRepeat::RepeatFastAfterMs, fastInterval = HoldRepeat::RepeatFastIntervalMs;
    const HoldRepeat::Timing rows = HoldRepeat::rows();
    CHECK(rows.delay == delay);
    CHECK(rows.interval == interval);
    CHECK(rows.fastAfter == fastAfter);
    CHECK(rows.fastInterval == fastInterval);
}

// THE end-of-list rule of every menu (the owner, 2026-10-05): a single press at the end wraps, a held key's repeats
// stop at the end - for a list's rows and for a value row's values alike.
TEST_CASE("stepIndex: a press wraps past the last and the first, a repeat stops there") {
    using abgui::stepIndex;
    // a press, in the middle and at both ends
    CHECK(stepIndex(1, 1, 4, false) == 2);
    CHECK(stepIndex(2, -1, 4, false) == 1);
    CHECK(stepIndex(3, 1, 4, false) == 0);  // the last to the first
    CHECK(stepIndex(0, -1, 4, false) == 3); // the first to the last
    // a repeat: the same in the middle, stays at both ends
    CHECK(stepIndex(1, 1, 4, true) == 2);
    CHECK(stepIndex(2, -1, 4, true) == 1);
    CHECK(stepIndex(3, 1, 4, true) == 3);
    CHECK(stepIndex(0, -1, 4, true) == 0);
    // one entry, none: nowhere to go
    CHECK(stepIndex(0, 1, 1, false) == 0);
    CHECK(stepIndex(0, -1, 1, true) == 0);
    CHECK(stepIndex(0, 1, 0, false) == 0);
    // a value row of two (on/off) wraps on a press too
    CHECK(stepIndex(1, 1, 2, false) == 0);
    CHECK(stepIndex(1, 1, 2, true) == 1);
}

TEST_CASE("stepIndex: headings are skipped, also round the ends; a repeat never goes round") {
    using abgui::stepIndex;
    const bool heading[] = {true, false, false, true, false, true}; // rows 1, 2 and 4 can be picked
    auto skip = [&](int i) { return heading[i]; };
    CHECK(stepIndex(1, 1, 6, false, skip) == 2);
    CHECK(stepIndex(2, 1, 6, false, skip) == 4);  // past the heading in the middle
    CHECK(stepIndex(4, 1, 6, false, skip) == 1);  // the last pickable row wraps to the first, past both headings
    CHECK(stepIndex(1, -1, 6, false, skip) == 4); // and back
    CHECK(stepIndex(4, 1, 6, true, skip) == 4);   // a repeat stays
    CHECK(stepIndex(1, -1, 6, true, skip) == 1);
    CHECK(stepIndex(2, -1, 6, true, skip) == 1);
    // nothing to pick at all
    const bool all[] = {true, true};
    CHECK(stepIndex(0, 1, 2, false, [&](int i) { return all[i]; }) == 0);
}

TEST_CASE("DpadHold: the press is the screen's own, every step tick() gives is a repeat (callbacks of either shape)") {
    HoldRepeat hold;
    hold.press(1, 1000);
    // the shape (dir, repeat): tick's steps are repeats
    std::vector<int> dirs;
    std::vector<bool> repeats;
    auto both = [&](int dir, bool repeat) {
        dirs.push_back(dir);
        repeats.push_back(repeat);
    };
    for (int n = hold.due(1350); n != 0; n -= 1)
        abgui::detail::callStep(both, 1, 0);
    CHECK(dirs == std::vector<int>{1});
    CHECK(repeats == std::vector<bool>{true});
    // the old shape (dir) still works
    int seen = 0;
    auto one = [&](int dir) { seen += dir; };
    abgui::detail::callStep(one, -1, 0);
    CHECK(seen == -1);
}
