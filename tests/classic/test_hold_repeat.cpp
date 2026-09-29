//
// HoldRepeat: a held direction repeating its step, sooner the longer it is held, one frame at a time.
//
#include "doctest/doctest.h"

#include "gui/hold_repeat.h"

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
