// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::ListModel (G3m, part 1 of docs/ab-gui-plan.md): the selection and paging of a list of rows, pure - no drawing,
// no input, no sounds. The cursor (`selected`), the page (`firstVisible`..`lastVisible`, `maxVisible` rows), headings
// the cursor never rests on (a predicate on the row's index), a step down/up with the wrap round the ends, a page
// down/up, the first and the last row, and landOnSelectable() that takes the cursor off a heading and scrolls the page
// to show it.
//
// The model owns nothing: a View holds references to the caller's own numbers, so a class keeps the members it has (the
// classic GuiMenuBase is header-only and compiled into extensions - its data layout must not change) and the model
// works on them in place. The functions are inline templates over the skip predicate, so an extension that includes
// this header needs no symbol from the host.
//
// The rules are exactly those GuiMenuBase had before it moved here (UIREV-2, BUG-25 and the UIREV-1 console pass): a
// move ends on a row that can be picked, in the move's direction and back when that runs off the list; a list of one
// row or none does not move.
//
#pragma once

namespace abgui {

//********************
// ListModel
//********************
class ListModel {
public:
    // the caller's numbers, and how many rows the list has now
    struct View {
        int &selected;     // the cursor's row
        int &firstVisible; // the page: the first row shown
        int &lastVisible;  // ... and the last (firstVisible + maxVisible - 1 once the page is computed)
        int maxVisible;    // the rows a page holds
        int size;          // the rows in the list
    };

    // move the cursor and the page together by `moveBy` rows
    static void adjustPageBy(View v, int moveBy) {
        v.selected += moveBy;
        v.firstVisible += moveBy;
        v.lastVisible += moveBy;
    }

    // the page recomputed from the cursor: the first page, the last page, or the cursor in the middle
    static void computePagePosition(View v) {
        if (v.size == 0) {
            v.selected = 0;
            v.firstVisible = 0;
            v.lastVisible = 0;
            return;
        }
        const bool allFitOnOnePage = v.size <= v.maxVisible;
        const bool onTheFirstPage = v.selected < v.maxVisible;
        const bool onTheLastPage = v.selected >= (v.size - v.maxVisible);
        if (allFitOnOnePage)
            v.firstVisible = 0;
        else if (onTheFirstPage)
            v.firstVisible = 0;
        else if (onTheLastPage)
            v.firstVisible = v.size - v.maxVisible;
        else
            v.firstVisible = v.selected - (v.maxVisible / 2);
        v.lastVisible = v.firstVisible + v.maxVisible - 1;
    }

    // off a heading: from `selected` on in `step`'s direction (+1/-1), back the other way when that runs off the list,
    // and the page scrolls just enough to show the row; with no row to pick at all the cursor stays put
    template <typename Skip> static void landOnSelectable(View v, int step, Skip skip) {
        int i = v.selected;
        while (i >= 0 && i < v.size && skip(i))
            i += step;
        if (i < 0 || i >= v.size) {
            i = v.selected;
            while (i >= 0 && i < v.size && skip(i))
                i -= step;
        }
        if (i < 0 || i >= v.size)
            return;
        v.selected = i;
        if (v.selected < v.firstVisible) {
            v.firstVisible = v.selected;
            v.lastVisible = v.selected + v.maxVisible - 1;
        } else if (v.selected > v.lastVisible) {
            v.lastVisible = v.selected;
            v.firstVisible = v.selected - v.maxVisible + 1;
        }
    }

    // one row down: the page scrolls a row at the bottom edge; past the last pickable row the cursor wraps to the first
    template <typename Skip> static void stepDown(View v, Skip skip) {
        if (v.size <= 1)
            return;
        const int before = v.selected;
        if (v.selected < v.size - 1) {
            if (v.selected == v.lastVisible)
                adjustPageBy(v, 1);
            else
                ++v.selected;
            landOnSelectable(v, 1, skip);
        }
        if (v.selected == before) { // the last selectable row: wrap to the first
            v.selected = 0;
            computePagePosition(v);
            landOnSelectable(v, 1, skip);
        }
    }

    // one row up, the mirror of stepDown(): from the first pickable row it wraps to the last
    template <typename Skip> static void stepUp(View v, Skip skip) {
        if (v.size <= 1)
            return;
        const int before = v.selected;
        if (v.selected > 0) {
            if (v.selected == v.firstVisible)
                adjustPageBy(v, -1);
            else
                --v.selected;
            landOnSelectable(v, -1, skip);
        }
        if (v.selected == before) { // the first selectable row: wrap to the last
            v.selected = v.size - 1;
            computePagePosition(v);
            landOnSelectable(v, -1, skip);
        }
    }

    // a page down: the next page, or the last row when the last page is near
    template <typename Skip> static void pageDown(View v, Skip skip) {
        if (v.size <= 1)
            return;
        if (v.lastVisible + v.maxVisible >= v.size) {
            v.selected = v.size - 1;
            computePagePosition(v);
        } else {
            adjustPageBy(v, v.maxVisible);
        }
        landOnSelectable(v, 1, skip);
    }

    // a page up: the previous page, or the first row when the first page is near
    template <typename Skip> static void pageUp(View v, Skip skip) {
        if (v.size <= 1)
            return;
        if (v.firstVisible - v.maxVisible < 0) {
            v.selected = 0;
            computePagePosition(v);
        } else {
            adjustPageBy(v, -v.maxVisible);
        }
        landOnSelectable(v, -1, skip);
    }

    // the first row that can be picked
    template <typename Skip> static void home(View v, Skip skip) {
        if (v.size <= 1)
            return;
        v.selected = 0;
        computePagePosition(v);
        landOnSelectable(v, 1, skip);
    }

    // the last row that can be picked
    template <typename Skip> static void end(View v, Skip skip) {
        if (v.size <= 1)
            return;
        v.selected = v.size - 1;
        computePagePosition(v);
        landOnSelectable(v, -1, skip);
    }
};

} // namespace abgui
