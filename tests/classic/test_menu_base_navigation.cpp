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

    // gui_menu_base.h ~226-240, unchanged by this fix - note the `selected > 1` bound (not `> 0`)
    void doKeyUp() {
        if (size() > 1) {
            if (selected <= 0) {
                selected = size() - 1;
            } else {
                --selected;
                while (skip(selected) && selected > 1)
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
// doKeyUp()/doKeyDown(): BUG-25 ("Options d-pad sometimes skips 2-3 rows", hub docs\todo.md - record only,
// not approved for a fix - hard rule 4: reproduce on current develop, don't fix speculatively)
//*******************************

TEST_CASE("doKeyDown: two consecutive headings mid-list are skipped cleanly (its bound is not suspect)") {
    TestMenu m;
    m.heading = {false, true, true, false, false}; // row 0 and 3/4 real, 1/2 headings
    m.selected = 0;
    m.doKeyDown();
    CHECK(m.selected == 3); // straight past both headings to the next real row
}

TEST_CASE("doKeyUp: two consecutive headings mid-list - the `selected > 1` bound stops one short of row 0"
          " (BUG-25-shaped - reported, not fixed here)") {
    // Same layout as the doKeyDown case above, driven from the other end. doKeyUp's skip loop is bounded by
    // `selected > 1`, not `selected > 0` like doKeyDown's mirror-image bound (`selected < getVerticalSize()
    // - 1`, which correctly reaches the last valid index) - so unlike doKeyDown, doKeyUp can never land the
    // cursor on index 0 through this loop.
    TestMenu m;
    m.heading = {false, true, true, false, false};
    m.selected = 3;
    m.doKeyUp();
    // reproduced: the cursor stops resting on row 1 - a heading, no selection box - one heading short of
    // the real row 0, instead of skipping through to it the way doKeyDown skips through to the last real row.
    CHECK(m.selected == 1);
    CHECK(m.heading[static_cast<size_t>(m.selected)]); // confirms it is indeed left sitting on a heading

    // one more Up recovers - it is not a permanent stall, just one call short each time this shape occurs
    m.doKeyUp();
    CHECK(m.selected == 0);
}

TEST_CASE("doKeyUp: three consecutive headings mid-list - the same one-short landing, not a growing skip") {
    TestMenu m;
    m.heading = {false, true, true, true, false};
    m.selected = 4;
    m.doKeyUp();
    CHECK(m.selected == 1); // stops on the heading nearest row 0, same as with two headings
    m.doKeyUp();
    CHECK(m.selected == 0);
}
