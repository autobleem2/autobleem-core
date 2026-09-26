//
// psPlayerLabel - the PS1 port a connected pad's index lands on (C9).
//
#include "doctest/doctest.h"

#include "core/model/pad_assignment.h"

TEST_CASE("the first two pads are Player 1 / Player 2") {
    CHECK(psPlayerLabel(0, 2) == "Player 1");
    CHECK(psPlayerLabel(1, 2) == "Player 2");
}

TEST_CASE("a third+ pad is not used by the PS1 emulator") {
    CHECK(psPlayerLabel(2, 3) == "not used by the PS1 emulator");
    CHECK(psPlayerLabel(5, 6) == "not used by the PS1 emulator");
}

TEST_CASE("an index outside the connected count is empty") {
    CHECK(psPlayerLabel(-1, 2) == "");
    CHECK(psPlayerLabel(2, 2) == "");
    CHECK(psPlayerLabel(0, 0) == "");
}

TEST_CASE("one pad connected is still Player 1") {
    CHECK(psPlayerLabel(0, 1) == "Player 1");
}
