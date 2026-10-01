// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui hint bar (docs/ab-gui-plan.md, G5e): the layout of the launcher's two lines of button hints in the theme's
// hint bar - pure, no drawing. Each line is fitted into its half of the bar (the whole bar when it is under
// TwoLineMinHeight px tall: one line only) at the largest font size of FontSizes that fits its width less Inset on
// each side; at the smallest size the gaps between the hints close up, 2 px a step, down to TightestGap; after that a
// line that may drop (line 2) loses hints from the right until it fits (never its first). The line is centred (never
// closer than Inset to the bar's left), its label text and its 30 px buttons centred on the line's height.
//
// The numbers are those of GuiLauncher::layoutHints() before G5e, rule for rule - including its estimate of the width
// once the gaps close (the total loses 4 px per hint for each 2 px step), which the centring then uses - so the
// launcher's hint lines stay where they were (tested against a frozen copy of the old code). The caller measures:
// its buttons and fonts are its own (HintMeasure). What the bar's frame (the theme's `hintBar`, G5e) is drawn into is
// the bar itself; this file only places the hints.
//
#pragma once

#include <ableem/ui/types.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace abgui {

// what the layout asks the caller about the hints of one line (hint = the index in the line, from 0)
struct HintMeasure {
    // the width of hint `hint`'s button(s) - its glyphs and chips, with no gap after them; the same at every size
    std::function<int(std::size_t hint)> buttonsWidth;
    // the width of hint `hint`'s label in the font of `size` px
    std::function<int(int size, std::size_t hint)> labelWidth;
    // the line height of the font of `size` px
    std::function<int(int size)> lineHeight;
};

// where one hint of a line goes: its buttons at chipX, its label at labelX (the line's chipY/labelY)
struct HintPlace {
    int chipX = 0;
    int labelX = 0;
};

// one line, laid out
struct HintLineLayout {
    int fontSize = 0;              // the label font's size (one of HintBar::FontSizes)
    int gap = 0;                   // between one hint's label and the next hint's buttons
    int labelY = 0;                // the labels' top
    int chipY = 0;                 // the buttons' top (a ChipHeight px line centred like the labels)
    std::vector<HintPlace> places; // the hints shown, from the first: fewer than asked when the line dropped some
};

// the whole bar, laid out
struct HintBarLayout {
    bool oneLine = false; // the bar is under TwoLineMinHeight px: line 2 is not shown at all
    HintLineLayout line1; // in topLine(bar), or the whole bar with oneLine
    HintLineLayout line2; // in bottomLine(bar); empty with oneLine
};

// UIREV-36: the launcher's fixed grid - 4 columns x 2 lines, every item at a home slot. What the layout asks the
// caller: the width of each column's widest item over EVERY state (the item's buttons, IconGap and its label at that
// font size) - so nothing moves when the state changes - and the font's line height.
struct HintGridMeasure {
    std::function<int(int size, int column)> columnWidth;
    std::function<int(int size)> lineHeight;
};

// the grid, laid out once per (bar, language)
struct HintGridLayout {
    static constexpr int Columns = 4;
    bool oneLine = false; // the bar is under TwoLineMinHeight px: line 1 only
    int fontSize = 0;     // ONE for both lines
    int labelY[2] = {0, 0};
    int chipY[2] = {0, 0};
    int itemX[Columns] = {0, 0, 0, 0};    // where an item's buttons start, absolute
    int itemRoom[Columns] = {0, 0, 0, 0}; // the width an item may take (a label wider than this is elided)
};

class HintBar {
public:
    static constexpr int GridColumnPad = 22; // a column is its widest item + this
    static constexpr int GridItemInset = 12; // an item starts this far in from its column's left edge

    static constexpr int Inset = 16;            // the free room at each end of a line
    static constexpr int IconGap = 6;           // a hint's buttons to its label
    static constexpr int WidestGap = 28;        // hint to hint, as long as the line fits at some font size
    static constexpr int TightestGap = 10;      // the closest the gaps close up to
    static constexpr int ChipHeight = 30;       // the buttons' height (Style::buttons' line)
    static constexpr int TwoLineMinHeight = 48; // a bar lower than this shows line 1 only, at the bar's full height
    static constexpr int FontSizeCount = 5;
    static constexpr int FontSizes[FontSizeCount] = {22, 20, 18, 16, 14}; // tried largest first

    // a bar too low for two lines (bar.h < TwoLineMinHeight)
    static bool oneLineOnly(const ableem::Rect &bar);
    // line 1's rect: the bar's top half (h / 2)
    static ableem::Rect topLine(const ableem::Rect &bar);
    // line 2's rect: the rest of the bar under the top half (an odd px goes here)
    static ableem::Rect bottomLine(const ableem::Rect &bar);

    // lays `count` hints out in `rect` (see the file comment); allowDrop lets the line lose hints from the right
    static HintLineLayout layoutLine(std::size_t count, const ableem::Rect &rect, bool allowDrop,
                                     const HintMeasure &measure);
    // the two lines in `bar`: line 1 (`count1` hints, never dropping) in the top half, line 2 (`count2`, dropping)
    // in the bottom half - or line 1 alone in the whole bar when it is too low for two (line 2 then has no places)
    static HintBarLayout layout(const ableem::Rect &bar, std::size_t count1, const HintMeasure &measure1,
                                std::size_t count2, const HintMeasure &measure2);

    // the fixed grid (UIREV-36): the largest of FontSizes at which the four columns (widest item + GridColumnPad each)
    // fit the bar's width less Inset on each side, the spare width shared evenly; an item at its column's left +
    // GridItemInset. When even the smallest size does not fit, the columns shrink in proportion and `itemRoom` says
    // how much an item may take - the caller elides a label, never drops an item.
    static HintGridLayout layoutGrid(const ableem::Rect &bar, const HintGridMeasure &measure);
};

} // namespace abgui
