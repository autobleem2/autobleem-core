//
// GuiMenuBase<LineDataType>'s cursor movement (gui_menu_base.h): Up/Down, L2/R2 (a page) and L1/R1 (first/last
// row) must never leave the cursor on a row skipSelectingThisLineWhenMovingByOne() marks unselectable (a
// heading, as GuiOptions' CFG_HEADING rows are) - no selection box is drawn there, so it looks like nothing
// happened. UIREV-2 fixed doHome()/doEnd(), BUG-25 doKeyUp()'s skip loop; UIREV-1's console pass then found L2
// at the top of Options still landing on the "Interface" heading at row 0, so every move now ends in one
// landOnSelectable(step): on in the move's direction, back the other way when that runs off the list, the
// page scrolled just enough to show the row.
//
// GuiMenuBase is header-only but its constructor chain (GuiScreen -> AppBase::get()/Gui::getInstance())
// needs a live AppBase built over a real theme/resources tree, which this repository does not ship (it is
// the launcher's) and no existing test in tests/classic/ constructs one for this reason (test_busy_input.cpp
// and test_input_flush.cpp use a bare ableem::GuiBase, never a GuiScreen subclass). So this suite drives a
// small local model, TestMenu, that reproduces the selection and page math of adjustPageBy(),
// landOnSelectable(), computePagePosition() and doKeyDown/Up/PageDown/PageUp/Home/End() line for line (the
// sounds aside) instead of an instance of the real class. Keep it in step with gui_menu_base.h.
//
#include "doctest/doctest.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

// mirrors GuiMenuBase<LineDataType>'s navigation with labelsOnly == false
struct TestMenu {
    std::vector<bool> heading; // true = a row skipSelectingThisLineWhenMovingByOne() would return true for
    int selected = 0;
    int firstVisibleIndex = 0;
    int lastVisibleIndex = 19; // the first page of maxVisible rows, as computePagePosition() leaves it
    int maxVisible = 20; // every list fits on one page unless a case says otherwise

    int size() const { return static_cast<int>(heading.size()); }
    bool skip(int i) const { return heading[static_cast<size_t>(i)]; }
    bool onHeading() const { return skip(selected); }
    bool selectedVisible() const { return selected >= firstVisibleIndex && selected <= lastVisibleIndex; }

    // what render() does on its first frame
    void start(int sel) {
        selected = sel;
        computePagePosition();
    }

    void adjustPageBy(int moveBy) {
        selected += moveBy;
        firstVisibleIndex += moveBy;
        lastVisibleIndex += moveBy;
    }

    void landOnSelectable(int step) {
        int i = selected;
        while (i >= 0 && i < size() && skip(i))
            i += step;
        if (i < 0 || i >= size()) {
            i = selected;
            while (i >= 0 && i < size() && skip(i))
                i -= step;
        }
        if (i < 0 || i >= size())
            return;
        selected = i;
        if (selected < firstVisibleIndex) {
            firstVisibleIndex = selected;
            lastVisibleIndex = selected + maxVisible - 1;
        } else if (selected > lastVisibleIndex) {
            lastVisibleIndex = selected;
            firstVisibleIndex = selected - maxVisible + 1;
        }
    }

    void computePagePosition() {
        if (size() == 0) {
            selected = 0;
            firstVisibleIndex = 0;
            lastVisibleIndex = 0;
        } else {
            if (size() <= maxVisible || selected < maxVisible)
                firstVisibleIndex = 0;
            else if (selected >= size() - maxVisible)
                firstVisibleIndex = size() - maxVisible;
            else
                firstVisibleIndex = selected - (maxVisible / 2);
            lastVisibleIndex = firstVisibleIndex + maxVisible - 1;
        }
    }

    void doKeyDown() {
        if (size() > 1) {
            int before = selected;
            if (selected < size() - 1) {
                if (selected == lastVisibleIndex)
                    adjustPageBy(1);
                else
                    ++selected;
                landOnSelectable(1);
            }
            if (selected == before) {
                selected = 0;
                computePagePosition();
                landOnSelectable(1);
            }
        }
    }

    void doKeyUp() {
        if (size() > 1) {
            int before = selected;
            if (selected > 0) {
                if (selected == firstVisibleIndex)
                    adjustPageBy(-1);
                else
                    --selected;
                landOnSelectable(-1);
            }
            if (selected == before) {
                selected = size() - 1;
                computePagePosition();
                landOnSelectable(-1);
            }
        }
    }

    void doPageDown() {
        if (size() > 1) {
            if (lastVisibleIndex + maxVisible >= size()) {
                selected = size() - 1;
                computePagePosition();
            } else {
                adjustPageBy(maxVisible);
            }
            landOnSelectable(1);
        }
    }

    void doPageUp() {
        if (size() > 1) {
            if (firstVisibleIndex - maxVisible < 0) {
                selected = 0;
                computePagePosition();
            } else {
                adjustPageBy(-maxVisible);
            }
            landOnSelectable(-1);
        }
    }

    void doHome() {
        if (size() > 1) {
            selected = 0;
            computePagePosition();
            landOnSelectable(1);
        }
    }

    void doEnd() {
        if (size() > 1) {
            selected = size() - 1;
            computePagePosition();
            landOnSelectable(-1);
        }
    }
};

} // namespace

//*******************************
// doHome()/doEnd(): the fix itself
//*******************************

TEST_CASE("doHome/doEnd: no headings, no regression - land on row 0 and the last row as before") {
    TestMenu m;
    m.heading = {false, false, false, false};
    m.selected = 2;
    m.doHome();
    CHECK(m.selected == 0);

    m.selected = 1;
    m.doEnd();
    CHECK(m.selected == 3);
}

TEST_CASE("doHome: a heading at row 0 - lands on the first real row, not the heading (UIREV-1's Options bug)") {
    TestMenu m;
    m.heading = {true, false, false, false}; // row 0 = "Interface", like GuiOptions
    m.selected = 3;
    m.doHome();
    CHECK(m.selected == 1);
}

TEST_CASE("doEnd: a heading at the last row - lands on the last real row, not the heading") {
    TestMenu m;
    m.heading = {false, false, false, true};
    m.selected = 0;
    m.doEnd();
    CHECK(m.selected == 2);
}

TEST_CASE("doHome/doEnd: two consecutive headings on either side are both skipped in one call") {
    TestMenu m;
    m.heading = {true, true, false, true, true}; // one real row (2), two headings on each side of it
    m.selected = 2;
    m.doHome();
    CHECK(m.selected == 2); // the only real row, reached by skipping forward through both headings

    m.selected = 2;
    m.doEnd();
    CHECK(m.selected == 2); // ... and by skipping backward through the other two
}

TEST_CASE("doHome/doEnd: a single-row list is a no-op (the getVerticalSize() > 1 guard)") {
    TestMenu m;
    m.heading = {true};
    m.selected = 0;
    m.doHome();
    CHECK(m.selected == 0);
    m.doEnd();
    CHECK(m.selected == 0);
}

TEST_CASE("doHome/doEnd: every row a heading terminates instead of looping forever") {
    // not reachable through a real GuiOptions list (a heading always has real rows under it), but
    // landOnSelectable() must still terminate on its own if it ever happened: with no selectable row it leaves
    // the cursor where the move put it, nothing crashes or spins.
    TestMenu m;
    m.heading = {true, true, true};
    m.start(1);
    m.doHome();
    CHECK(m.selected == 0);

    m.start(1);
    m.doEnd();
    CHECK(m.selected == 2);

    m.start(1);
    m.doKeyDown();
    m.doKeyUp();
    m.doPageDown();
    m.doPageUp();
    CHECK(m.selected >= 0);
    CHECK(m.selected < 3);
}

//*******************************
// doKeyUp()/doKeyDown(): BUG-25 ("Options d-pad sometimes skips 2-3 rows", hub docs\bugs.md) - fix approved by
// the owner 2026-09-29, in the UIREV series (commit 72e4763). Root cause confirmed: doKeyUp()'s skip loop was
// bounded by `selected > 1`, so with two (or more) consecutive skippable rows it stopped one row short of row
// 0 - a heading left selected, no selection box drawn - instead of skipping all the way through to the first
// real row, the way doKeyDown()'s correctly-bounded mirror (`selected < getVerticalSize() - 1`) always could.
// A player near the top of a list with adjacent headings would see the cursor stop on a row that draws
// nothing, then jump past it on the very next Up - reading as an inconsistent multi-row skip.
//*******************************

TEST_CASE("doKeyDown: two consecutive headings mid-list are skipped cleanly (its bound was never suspect)") {
    TestMenu m;
    m.heading = {false, true, true, false, false}; // row 0 and 3/4 real, 1/2 headings
    m.selected = 0;
    m.doKeyDown();
    CHECK(m.selected == 3); // straight past both headings to the next real row
}

TEST_CASE("doKeyUp: two consecutive headings mid-list - now skips all the way to row 0 in one call (BUG-25)") {
    // Same layout as the doKeyDown case above, driven from the other end. Before this fix, doKeyUp()'s skip
    // loop was bounded by `selected > 1` and would have stopped at row 1 (a heading, CHECK(m.selected == 1))
    // - one row short of the real row 0 - because the bound forbade decrementing selected past 1 even while
    // still sitting on a skippable row. With the bound now `selected > 0`, matching doKeyDown()'s symmetry,
    // it reaches row 0 directly, the same way doKeyDown reaches the last real row directly above.
    TestMenu m;
    m.heading = {false, true, true, false, false};
    m.selected = 3;
    m.doKeyUp();
    CHECK(m.selected == 0); // skips both headings in the one call - was row 1 (a heading) before the fix
    CHECK_FALSE(m.heading[static_cast<size_t>(m.selected)]);
}

TEST_CASE("doKeyUp: three consecutive headings mid-list - also reaches row 0 in one call") {
    TestMenu m;
    m.heading = {false, true, true, true, false};
    m.selected = 4;
    m.doKeyUp();
    CHECK(m.selected == 0); // was row 1 before the fix (three headings made the old bound's shortfall worse)
}

TEST_CASE("doKeyUp/doKeyDown: no headings - unaffected by the fix") {
    TestMenu m;
    m.heading = {false, false, false, false};
    m.selected = 2;
    m.doKeyUp();
    CHECK(m.selected == 1);
    m.doKeyDown();
    CHECK(m.selected == 2);
}

TEST_CASE("doKeyUp/doKeyDown: a single heading not at the very top or bottom - unaffected by the fix") {
    TestMenu m;
    m.heading = {false, true, false, false}; // one heading at row 1, same shape doHome/doEnd already cover
    m.selected = 2;
    m.doKeyUp();
    CHECK(m.selected == 0); // skips the lone heading at row 1, same as before this fix
    m.selected = 0;
    m.doKeyDown();
    CHECK(m.selected == 2);
}

//*******************************
// UIREV-1's console pass: L2/R2 and the wrap of Up/Down land on a real row too, and the page follows
//*******************************

TEST_CASE("doPageUp: L2 at the top of Options (a heading at row 0) lands on row 1, not the heading") {
    TestMenu m;
    m.heading = {true, false, false, false, true, false}; // "Interface" at row 0, like GuiOptions
    m.start(3);
    m.doPageUp();
    CHECK(m.selected == 1);
    m.doPageUp(); // again, already at the top
    CHECK(m.selected == 1);
}

TEST_CASE("doPageDown: R2 at the end with a heading last lands on the last real row") {
    TestMenu m;
    m.heading = {true, false, false, true};
    m.start(1);
    m.doPageDown();
    CHECK(m.selected == 2);
}

TEST_CASE("doKeyUp: from the first real row under a heading at row 0 wraps to the last real row") {
    TestMenu m;
    m.heading = {true, false, false, false, true};
    m.start(1);
    m.doKeyUp();
    CHECK(m.selected == 3);
}

TEST_CASE("doKeyDown: from the last real row above a trailing heading wraps to the first real row") {
    TestMenu m;
    m.heading = {true, false, false, false, true};
    m.start(3);
    m.doKeyDown();
    CHECK(m.selected == 1);
}

TEST_CASE("doPageDown/doPageUp: a page landing on a heading moves on and keeps the row on screen") {
    TestMenu m; // 12 rows, 4 a page, headings at 0, 4 and 8
    m.heading = {true, false, false, false, true, false, false, false, true, false, false, false};
    m.maxVisible = 4;
    m.start(0);
    m.doPageDown(); // 0 -> 4 (a heading) -> 5
    CHECK(m.selected == 5);
    CHECK(m.selectedVisible());

    m.doPageDown(); // 5 -> 9
    CHECK(m.selected == 9);
    CHECK(m.selectedVisible());

    m.doPageDown(); // the last page: the last row
    CHECK(m.selected == 11);
    CHECK(m.selectedVisible());

    m.doPageUp(); // 11 -> 7
    CHECK(m.selected == 7);
    CHECK(m.selectedVisible());

    m.doPageUp(); // 7 -> 3
    CHECK(m.selected == 3);
    CHECK(m.selectedVisible());

    m.doPageUp(); // the first page: row 0 is a heading -> row 1
    CHECK(m.selected == 1);
    CHECK(m.selectedVisible());
}

TEST_CASE("doKeyDown/doKeyUp: a row at a time through a paged list never rests on a heading or off screen") {
    TestMenu m;
    m.heading = {true, false, false, false, true, false, false, false, true, false, false, false};
    m.maxVisible = 4;
    m.start(1);
    for (int i = 0; i < 20; ++i) {
        m.doKeyDown();
        CHECK_FALSE(m.onHeading());
        CHECK(m.selectedVisible());
    }
    for (int i = 0; i < 20; ++i) {
        m.doKeyUp();
        CHECK_FALSE(m.onHeading());
        CHECK(m.selectedVisible());
    }
}

//*******************************
// getStatusLine(): UIREV-28 - every footer that handles L1/R1 (first/last row) must say so, paired with the
// existing L2/R2 (page) hint the owner approved ("L1/R1 First/last" / "L2/R2 Page").
//
// GuiMenuBase<LineDataType>::getStatusLine() only builds a std::string from `selected`/`getVerticalSize()`
// through `_()` (ableem::translate) - no Gui/Renderer/Platform involved - so unlike doHome()/doEnd() above it
// really could be called on a live instance without much trouble, except the constructor still takes a real
// `ableem::GuiBase&`, which needs a live SDL window this repository's test harness does not stand up (see the
// file banner). `_()` itself is safe with no Lang loaded (lib_ableem/src/engine/lang.cpp: translate() returns
// its input unchanged when Lang::current() is null), so what is mirrored here is only the format string
// gui_menu_base.h builds - kept byte-for-byte in step with it, the same honesty rule the rest of this file
// follows for the class it cannot construct.
//*******************************

namespace {
// mirrors GuiMenuBase<LineDataType>::getStatusLine() (gui_menu_base.h ~199-202) exactly; `_()` is the
// identity here (no Lang loaded), which is also true for every English-language test run
std::string statusLine(int selected, int verticalSize) {
    return "Entry" + std::string(" ") + std::to_string(selected + 1) + "/" + std::to_string(verticalSize) +
           "    |@L1|/|@R1| " + "First/last" + "   |@L2|/|@R2| " + "Page" + "   |@X| " + "Select" + "   |@O| " +
           "Back" + " |";
}
} // namespace

TEST_CASE("getStatusLine: names L1/R1 First/last paired with L2/R2 Page, in that order") {
    const std::string status = statusLine(2, 8);
    CHECK(status.find("|@L1|/|@R1| First/last") != std::string::npos);
    CHECK(status.find("|@L2|/|@R2| Page") != std::string::npos);
    // the owner-approved pairing order: L1/R1 before L2/R2 (PanelStyle::footer re-sorts by button rank
    // regardless, but the source order is worth pinning so a future edit doesn't silently drop one)
    CHECK(status.find("|@L1|/|@R1|") < status.find("|@L2|/|@R2|"));
}
