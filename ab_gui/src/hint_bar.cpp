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

} // namespace abgui
