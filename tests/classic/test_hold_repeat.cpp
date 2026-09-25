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

TEST_CASE("HoldRepeat copes with the tick counter wrapping") {
    HoldRepeat hold;
    const uint32_t nearEnd = 0xFFFFFF00u;
    hold.press(1, nearEnd);
    CHECK(hold.due(nearEnd + 100) == 0);
    CHECK(hold.due(nearEnd + 350) == 1); // wrapped past 0
}
