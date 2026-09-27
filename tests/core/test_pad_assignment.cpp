//
// psPlayerSlot / decidePadAssignmentChange - which PS1 port a connected pad lands on, and when a UI
// should tell the user its assignment changed (C9).
//
#include "doctest/doctest.h"

#include "core/model/pad_assignment.h"
#include "core/model/timing.h"

TEST_CASE("the first two pads are Player 1 / Player 2") {
    CHECK(psPlayerSlot(0, 2) == PsPlayerSlot::Player1);
    CHECK(psPlayerSlot(1, 2) == PsPlayerSlot::Player2);
}

TEST_CASE("a third+ pad is not used by the PS1 emulator") {
    CHECK(psPlayerSlot(2, 3) == PsPlayerSlot::Unused);
    CHECK(psPlayerSlot(5, 6) == PsPlayerSlot::Unused);
}

TEST_CASE("an index outside the connected count is unused") {
    CHECK(psPlayerSlot(-1, 2) == PsPlayerSlot::Unused);
    CHECK(psPlayerSlot(2, 2) == PsPlayerSlot::Unused);
    CHECK(psPlayerSlot(0, 0) == PsPlayerSlot::Unused);
}

TEST_CASE("one pad connected is still Player 1") {
    CHECK(psPlayerSlot(0, 1) == PsPlayerSlot::Player1);
}

TEST_CASE("psPlayerSlot with swapped=false matches the two-argument form") {
    CHECK(psPlayerSlot(0, 2, false) == PsPlayerSlot::Player1);
    CHECK(psPlayerSlot(1, 2, false) == PsPlayerSlot::Player2);
    CHECK(psPlayerSlot(2, 3, false) == PsPlayerSlot::Unused);
    CHECK(psPlayerSlot(-1, 2, false) == PsPlayerSlot::Unused);
}

TEST_CASE("psPlayerSlot with swapped=true trades the first two positions (C11)") {
    CHECK(psPlayerSlot(0, 2, true) == PsPlayerSlot::Player2);
    CHECK(psPlayerSlot(1, 2, true) == PsPlayerSlot::Player1);
}

TEST_CASE("psPlayerSlot swapped: a third+ pad is still unused") {
    CHECK(psPlayerSlot(2, 3, true) == PsPlayerSlot::Unused);
    CHECK(psPlayerSlot(5, 6, true) == PsPlayerSlot::Unused);
}

TEST_CASE("psPlayerSlot swapped: an index outside the connected count is still unused") {
    CHECK(psPlayerSlot(-1, 2, true) == PsPlayerSlot::Unused);
    CHECK(psPlayerSlot(2, 2, true) == PsPlayerSlot::Unused);
    CHECK(psPlayerSlot(0, 0, true) == PsPlayerSlot::Unused);
}

TEST_CASE("psPlayerSlot swapped: a lone pad stays Player 1 regardless of the swap (Marcus's review fix)") {
    // the swap only takes effect with two or more pads connected - both emulators' own C11 code checks
    // the same thing (pcsx-ab counts the pads it is about to accept before applying pad_order,
    // pcsx-abnxt checks pads_changed()'s pad_count) so a player who left the row on and plays alone still
    // gets a game that responds, instead of a lone pad silently landing on PS1 port 2.
    CHECK(psPlayerSlot(0, 1, true) == PsPlayerSlot::Player1);
    CHECK(psPlayerSlot(0, 0, true) == PsPlayerSlot::Unused); // no pads at all: still unused, swap or not
}

TEST_CASE("psPlayerSlot swapped: two-plus pads is exactly where the swap takes effect") {
    CHECK(psPlayerSlot(0, 2, true) == PsPlayerSlot::Player2);
    CHECK(psPlayerSlot(1, 2, true) == PsPlayerSlot::Player1);
    CHECK(psPlayerSlot(0, 3, true) == PsPlayerSlot::Player2);
    CHECK(psPlayerSlot(1, 3, true) == PsPlayerSlot::Player1);
    CHECK(psPlayerSlot(2, 3, true) == PsPlayerSlot::Unused);
}

// C16: decidePadAssignmentChange()/checkPadAssignmentEmptyNotice() carry state in a PadAssignmentState
// (lastShown + emptySince) instead of the old single "was the previous reading a suppressed empty one"
// bool - the bool only ever worked when a *second* event arrived to ask again, which a lone pad's unplug
// never produces (one PadRemoved, then nothing). `now` is an opaque tick count in the same units as
// PadEmptyNoticeDelay (milliseconds in the app; the tests use round numbers).

TEST_CASE("decidePadAssignmentChange: unchanged from what was last shown is never shown") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A", "guidB|Pad B"}};
    PadAssignment current{{"guidA|Pad A", "guidB|Pad B"}};
    auto decision = decidePadAssignmentChange(current, state, 1000);
    CHECK(decision.show == false);
    CHECK(state.emptySince == 0);
}

TEST_CASE("decidePadAssignmentChange: a genuinely different assignment is shown at once") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A"}};
    PadAssignment current{{"guidA|Pad A", "guidB|Pad B"}};
    auto decision = decidePadAssignmentChange(current, state, 1000);
    CHECK(decision.show == true);
    CHECK(state.lastShown == current);
}

TEST_CASE("decidePadAssignmentChange: order matters (P1/P2 swapped is a change), shown at once") {
    // "the two-pad case... unplugging one of two changes the assignment and is shown at once" (C16) -
    // still non-empty, so this path is untouched by the empty-notice delay.
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A", "guidB|Pad B"}};
    PadAssignment current{{"guidB|Pad B", "guidA|Pad A"}};
    auto decision = decidePadAssignmentChange(current, state, 1000);
    CHECK(decision.show == true);
}

TEST_CASE("decidePadAssignmentChange: unplugging one of two pads (still non-empty) is shown at once") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A", "guidB|Pad B"}};
    PadAssignment current{{"guidB|Pad B"}}; // A unplugged, B is still Player 1 now
    auto decision = decidePadAssignmentChange(current, state, 1000);
    CHECK(decision.show == true);
    CHECK(state.emptySince == 0);
}

TEST_CASE("decidePadAssignmentChange: a first empty reading is never shown immediately, but starts the clock") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A"}};
    PadAssignment current{}; // empty: the only pad was just unplugged
    auto decision = decidePadAssignmentChange(current, state, 1000);
    CHECK(decision.show == false);
    CHECK(state.emptySince == 1000); // recorded so checkPadAssignmentEmptyNotice() can pick it up later
    CHECK(state.lastShown == PadAssignment{{"guidA|Pad A"}}); // not shown yet, so not updated yet either
}

TEST_CASE("decidePadAssignmentChange: a repeated empty reading while pending does not restart the clock") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A"}};
    state.emptySince = 1000; // already pending from an earlier empty reading
    PadAssignment current{};
    auto decision = decidePadAssignmentChange(current, state, 1400); // another empty event, 400ms later
    CHECK(decision.show == false);
    CHECK(state.emptySince == 1000); // unchanged - the clock started at the *first* empty reading
}

TEST_CASE("checkPadAssignmentEmptyNotice: one pad unplugged and not back is shown after the delay, not before") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A"}};
    decidePadAssignmentChange(PadAssignment{}, state, 1000); // the single unplug event
    // just short of the delay: not shown yet
    CHECK(checkPadAssignmentEmptyNotice(state, 1000 + PadEmptyNoticeDelay - 1, PadEmptyNoticeDelay).show == false);
    CHECK(state.emptySince != 0); // still pending
    // the delay has now passed: shown, once
    auto decision = checkPadAssignmentEmptyNotice(state, 1000 + PadEmptyNoticeDelay, PadEmptyNoticeDelay);
    CHECK(decision.show == true);
    CHECK(state.emptySince == 0);
    CHECK(state.lastShown.empty());
}

TEST_CASE("checkPadAssignmentEmptyNotice: shown only once - a later frame with nothing new stays quiet") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A"}};
    decidePadAssignmentChange(PadAssignment{}, state, 1000);
    CHECK(checkPadAssignmentEmptyNotice(state, 1000 + PadEmptyNoticeDelay, PadEmptyNoticeDelay).show == true);
    // several frames later, still no pad: the notice does not repeat
    CHECK(checkPadAssignmentEmptyNotice(state, 1000 + PadEmptyNoticeDelay + 5000, PadEmptyNoticeDelay).show == false);
}

TEST_CASE("a pad that comes back within the delay is never shown as gone (a re-enumeration blip)") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A"}};
    decidePadAssignmentChange(PadAssignment{}, state, 1000); // unplug event
    // the pad reappears well inside the delay (SDL's own re-enumeration burst, or the flush/reopen
    // around a launch): a live event with the *same* assignment as before the blip
    auto backDecision = decidePadAssignmentChange(PadAssignment{{"guidA|Pad A"}}, state, 1100);
    CHECK(backDecision.show == false);
    CHECK(state.emptySince == 0); // the pending timer is cleared - back to "nothing pending"
    // even if a frame is checked past where the original deadline would have landed, nothing fires -
    // there is no pending timer left to expire
    CHECK(checkPadAssignmentEmptyNotice(state, 1000 + PadEmptyNoticeDelay, PadEmptyNoticeDelay).show == false);
}

TEST_CASE("a different pad plugged in within the delay is shown at once, not held back") {
    PadAssignmentState state;
    state.lastShown = PadAssignment{{"guidA|Pad A"}};
    decidePadAssignmentChange(PadAssignment{}, state, 1000); // unplug event
    // a *different* pad (not the same GUID+name) shows up before the deadline
    auto decision = decidePadAssignmentChange(PadAssignment{{"guidB|Pad B"}}, state, 1100);
    CHECK(decision.show == true);
    CHECK(state.emptySince == 0);
}

TEST_CASE("staying empty after already having shown 'no controllers' once is not repeated by a live event either") {
    PadAssignmentState state; // lastShown already empty, as after checkPadAssignmentEmptyNotice() fired
    auto decision = decidePadAssignmentChange(PadAssignment{}, state, 2000);
    CHECK(decision.show == false); // current == lastShown (both empty)
    CHECK(state.emptySince == 0);
}
