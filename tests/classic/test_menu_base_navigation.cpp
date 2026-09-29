//
// GuiMenuBase<LineDataType>::doHome()/doEnd() (gui_menu_base.h) - UIREV-2: L1/R1 must not land the cursor on
// a row skipSelectingThisLineWhenMovingByOne() marks unselectable (a heading, as GuiOptions' CFG_HEADING rows
// are), the same way doKeyDown()/doKeyUp() already skip past one after a normal one-step move. Before this
// fix doHome()/doEnd() set `selected` straight to 0 / getVerticalSize()-1 with no such check, which is what
// UIREV-1 exposed once GuiOptionsMenuBase stopped overriding them: L1 on Options lands exactly on its
// "Interface" heading at row 0 - no selection box drawn, looking like nothing happened (Harriet's QA fail,
// !autobleem\out\ui-review\qa-report.md, shots 08/11/12).
//
// GuiMenuBase is header-only but its constructor chain (GuiScreen -> AppBase::get()/Gui::getInstance())
// needs a live AppBase built over a real theme/resources tree, which this repository does not ship (it is
// the launcher's) and no existing test in tests/classic/ constructs one for this reason (test_busy_input.cpp
// and test_input_flush.cpp use a bare ableem::GuiBase, never a GuiScreen subclass). So this suite drives a
// small local model, TestMenu, that reproduces doKeyDown()/doKeyUp()/doHome()/doEnd()'s selection math line
// for line (the audio/page-scroll side effects aside - they don't affect which row ends up selected for a
// list that fits on one page, which is all every case here uses) instead of an instance of the real class.
// Keep it in step with gui_menu_base.h if that logic changes again.
//
#include "doctest/doctest.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

// mirrors GuiMenuBase<LineDataType> for a list that fits on one page (firstVisibleIndex == 0,
// lastVisibleIndex == size()-1 always, so the "== firstVisibleIndex/lastVisibleIndex" scroll branches in the
// real doKeyDown()/doKeyUp() never fire and are left out here - they don't change which row is selected).
struct TestMenu {
    std::vector<bool> heading; // true = a row skipSelectingThisLineWhenMovingByOne() would return true for
    int selected = 0;

    int size() const { return static_cast<int>(heading.size()); }
    bool skip(int i) const { return heading[static_cast<size_t>(i)]; }

    // gui_menu_base.h ~207-221, unchanged by this fix
    void doKeyDown() {
        if (size() > 1) {
            if (selected >= size() - 1) {
                selected = 0;
            } else {
                ++selected;
                while (skip(selected) && selected < size() - 1)
                    ++selected;
            }
        }
    }

    // gui_menu_base.h ~226-240, fixed by BUG-25: the skip loop's bound was `selected > 1`, which could never
    // land on row 0 - now `selected > 0`, mirroring doKeyDown()'s `selected < size() - 1`
    void doKeyUp() {
        if (size() > 1) {
            if (selected <= 0) {
                selected = size() - 1;
            } else {
                --selected;
                while (skip(selected) && selected > 0)
                    --selected;
            }
        }
    }

    // gui_menu_base.h doHome(), fixed by UIREV-2
    void doHome() {
        if (size() > 1) {
            selected = 0;
            while (skip(selected) && selected < size() - 1)
                ++selected;
        }
    }

    // gui_menu_base.h doEnd(), fixed by UIREV-2
    void doEnd() {
        if (size() > 1) {
            selected = size() - 1;
            while (skip(selected) && selected > 0)
                --selected;
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
    // not reachable through a real GuiOptions list (a heading always has real rows under it), but the bound
    // in the fix (`selected < getVerticalSize() - 1` / `selected > 0`) must still terminate on its own if it
    // ever happened, rather than relying on that being impossible - this is exactly that: the loop stops at
    // the opposite end and leaves the cursor there, nothing crashes or spins.
    TestMenu m;
    m.heading = {true, true, true};
    m.selected = 1;
    m.doHome();
    CHECK(m.selected == 2); // ran out of rows to skip forward through, stopped at the last one

    m.selected = 1;
    m.doEnd();
    CHECK(m.selected == 0); // same, running backward
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
