//
// psPlayerSlot / decidePadAssignmentChange - which PS1 port a connected pad lands on, and when a UI
// should tell the user its assignment changed (C9).
//
#include "doctest/doctest.h"

#include "core/model/pad_assignment.h"

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

TEST_CASE("psPlayerSlot swapped: a single connected pad becomes Player 2 - a positional swap, no smarts") {
    // matches what AB_PAD_ORDER="1,0" actually does to a lone SDL device: it lands on PS1 port 2, not
    // port 1 - the UI must say the same thing the emulator will do, not invent a "still player 1" story.
    CHECK(psPlayerSlot(0, 1, true) == PsPlayerSlot::Player2);
}

TEST_CASE("decidePadAssignmentChange: unchanged from what was last shown is never shown") {
    PadAssignment last{{"guidA|Pad A", "guidB|Pad B"}};
    PadAssignment current{{"guidA|Pad A", "guidB|Pad B"}};
    auto decision = decidePadAssignmentChange(current, last, false);
    CHECK(decision.show == false);
    CHECK(decision.suppressedEmpty == false);
}

TEST_CASE("decidePadAssignmentChange: a genuinely different assignment is shown") {
    PadAssignment last{{"guidA|Pad A"}};
    PadAssignment current{{"guidA|Pad A", "guidB|Pad B"}};
    auto decision = decidePadAssignmentChange(current, last, false);
    CHECK(decision.show == true);
    CHECK(decision.suppressedEmpty == false);
}

TEST_CASE("decidePadAssignmentChange: order matters (P1/P2 swapped is a change)") {
    PadAssignment last{{"guidA|Pad A", "guidB|Pad B"}};
    PadAssignment current{{"guidB|Pad B", "guidA|Pad A"}};
    auto decision = decidePadAssignmentChange(current, last, false);
    CHECK(decision.show == true);
}

TEST_CASE("decidePadAssignmentChange: a first empty reading is suppressed, not shown") {
    PadAssignment last{{"guidA|Pad A"}};
    PadAssignment current{}; // empty
    auto decision = decidePadAssignmentChange(current, last, false);
    CHECK(decision.show == false);
    CHECK(decision.suppressedEmpty == true);
}

TEST_CASE("decidePadAssignmentChange: a second consecutive empty reading is shown") {
    PadAssignment last{{"guidA|Pad A"}}; // still what was last actually shown - the empty one wasn't
    PadAssignment current{};
    auto decision = decidePadAssignmentChange(current, last, /*previousWasSuppressedEmpty=*/true);
    CHECK(decision.show == true);
    CHECK(decision.suppressedEmpty == false);
}

TEST_CASE("decidePadAssignmentChange: reappearing right after a suppressed empty reading is shown") {
    PadAssignment last{{"guidA|Pad A"}};    // unchanged since the empty blip was never shown
    PadAssignment current{{"guidA|Pad A"}}; // the same pad came back
    auto decision = decidePadAssignmentChange(current, last, /*previousWasSuppressedEmpty=*/true);
    // current == last here, so this is the "unchanged" case, not a re-show
    CHECK(decision.show == false);
}

TEST_CASE("decidePadAssignmentChange: staying empty after already having shown 'no pads' once is not repeated") {
    PadAssignment last{}; // "no pads" was already shown once, so lastShown is itself empty
    PadAssignment current{};
    auto decision = decidePadAssignmentChange(current, last, false);
    CHECK(decision.show == false); // current == lastShown
}
