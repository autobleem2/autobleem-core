// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui hint bar: the pure layout of the launcher's two hint lines. See the header.
//
#include <ab_gui/hint_bar.h>

#include <algorithm>

using namespace std;

namespace abgui {

// the out-of-line definitions C++14 needs for the constants a caller binds by reference
constexpr int HintBar::Inset;
constexpr int HintBar::IconGap;
constexpr int HintBar::WidestGap;
constexpr int HintBar::TightestGap;
constexpr int HintBar::ChipHeight;
constexpr int HintBar::TwoLineMinHeight;
constexpr int HintBar::GridColumnPad;
constexpr int HintBar::GridTightPad;
constexpr int HintBar::GridItemInset;
constexpr int HintBar::FontSizeCount;
constexpr int HintBar::FontSizes[HintBar::FontSizeCount];

bool HintBar::oneLineOnly(const ableem::Rect &bar) {
    return bar.h < TwoLineMinHeight;
}

ableem::Rect HintBar::topLine(const ableem::Rect &bar) {
    return ableem::Rect(bar.x, bar.y, bar.w, bar.h / 2);
}

ableem::Rect HintBar::bottomLine(const ableem::Rect &bar) {
    return ableem::Rect(bar.x, bar.y + bar.h / 2, bar.w, bar.h - bar.h / 2);
}

//*******************************
// HintBar::layoutLine
//*******************************
// GuiLauncher::layoutHints()' layoutLine lambda (before G5e), step for step: the font from 22 down, then the gaps,
// then the drop, then the places. `total` is the line's width as the old code kept it - after the gaps close it is
// an estimate (4 px per hint a step, not 2 per gap), and the centring uses that estimate, as it always did.
HintLineLayout HintBar::layoutLine(size_t count, const ableem::Rect &rect, bool allowDrop, const HintMeasure &measure) {
    HintLineLayout line;
    const int room = rect.w - 2 * Inset;
    auto hintWidth = [&](size_t i, int size, int gap) {
        return measure.buttonsWidth(i) + IconGap + measure.labelWidth(size, i) + gap;
    };

    int gap = WidestGap;
    int total = 0;
    int size = FontSizes[0];
    for (int candidate : FontSizes) {
        size = candidate;
        total = count == 0 ? 0 : -gap;
        for (size_t i = 0; i < count; i++)
            total += hintWidth(i, size, gap);
        if (total <= room)
            break;
    }
    while (total > room && gap > TightestGap) { // the smallest font still too wide: closer together
        total -= static_cast<int>(count) * 4;
        gap -= 2;
    }
    size_t shown = count;
    while (allowDrop && total > room && shown > 1) {
        total -= hintWidth(shown - 1, size, gap);
        shown--;
    }

    line.fontSize = size;
    line.gap = gap;
    line.labelY = rect.y + (rect.h - measure.lineHeight(size)) / 2;
    line.chipY = rect.y + (rect.h - ChipHeight) / 2;
    int x = rect.x + max(Inset, (rect.w - total) / 2);
    line.places.reserve(shown);
    for (size_t i = 0; i < shown; i++) {
        HintPlace place;
        place.chipX = x;
        place.labelX = x + measure.buttonsWidth(i) + IconGap;
        x = place.labelX + measure.labelWidth(size, i) + gap;
        line.places.push_back(place);
    }
    return line;
}

//*******************************
// HintBar::layout
//*******************************
HintBarLayout HintBar::layout(const ableem::Rect &bar, size_t count1, const HintMeasure &measure1, size_t count2,
                              const HintMeasure &measure2) {
    HintBarLayout out;
    out.oneLine = oneLineOnly(bar);
    if (out.oneLine) {
        out.line1 = layoutLine(count1, bar, false, measure1);
    } else {
        out.line1 = layoutLine(count1, topLine(bar), false, measure1);
        out.line2 = layoutLine(count2, bottomLine(bar), true, measure2);
    }
    return out;
}

//*******************************
// HintBar::layoutGrid
//*******************************
HintGridLayout HintBar::layoutGrid(const ableem::Rect &bar, const HintGridMeasure &measure) {
    constexpr int columns = HintGridLayout::Columns;
    HintGridLayout out;
    out.oneLine = oneLineOnly(bar);
    const int room = bar.w - 2 * Inset;

    // font size and column padding, in the order they are tried: the roomy padding down to 14 px, then the tight one
    // down to 12 px
    struct Step {
        int size;
        int pad;
    };
    static const Step steps[] = {{22, GridColumnPad}, {20, GridColumnPad}, {18, GridColumnPad}, {16, GridColumnPad},
                                 {14, GridColumnPad}, {14, GridTightPad},  {13, GridTightPad},  {12, GridTightPad}};
    int size = steps[0].size;
    int pad = steps[0].pad;
    int widths[columns] = {0, 0, 0, 0};
    int total = 0;
    for (const Step &step : steps) {
        size = step.size;
        pad = step.pad;
        total = 0;
        for (int c = 0; c < columns; c++) {
            widths[c] = measure.columnWidth(size, c) + pad;
            total += widths[c];
        }
        if (total <= room)
            break;
    }
    if (total > room && total > 0) { // the safety net: in proportion, the labels elide
        for (int c = 0; c < columns; c++)
            widths[c] = widths[c] * room / total;
        total = 0;
        for (int c = 0; c < columns; c++)
            total += widths[c];
    }
    const int spare = max(0, room - total);

    out.fontSize = size;
    out.columnPad = pad;
    int x = bar.x + Inset;
    for (int c = 0; c < columns; c++) {
        const int width = widths[c] + spare / columns + (c < spare % columns ? 1 : 0);
        out.itemX[c] = x + GridItemInset;
        out.itemRoom[c] = max(0, width - pad);
        x += width;
    }

    const ableem::Rect lines[2] = {out.oneLine ? bar : topLine(bar), bottomLine(bar)};
    for (int l = 0; l < (out.oneLine ? 1 : 2); l++) {
        out.labelY[l] = lines[l].y + (lines[l].h - measure.lineHeight(size)) / 2;
        out.chipY[l] = lines[l].y + (lines[l].h - ChipHeight) / 2;
    }
    return out;
}

} // namespace abgui
