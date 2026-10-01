//
// ab_gui G5e (docs/ab-gui-plan.md): the launcher's hint bar layout, abgui::HintBar - pure. The two line rects and the
// one-line rule, a few hand-worked lines (a short line at 22, a long one shrinking, the gaps closing, line 2 dropping
// from the right, an empty line), and the whole layout against a frozen copy of GuiLauncher::layoutHints() as it was
// before G5e, over many generated lines, so the launcher's hints stay where they were.
//
#include "doctest/doctest.h"

#include <ab_gui/hint_bar.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

using namespace std;
using abgui::HintBar;
using abgui::HintBarLayout;
using abgui::HintGridLayout;
using abgui::HintGridMeasure;
using abgui::HintLineLayout;
using abgui::HintMeasure;
using ableem::Rect;

namespace {

// a fake line of hints: each one's buttons width, and its label as a character count; a label is `chars` x a width
// per character that grows with the size (a fixed-pitch stand-in for the launcher's fonts)
struct FakeHint {
    int buttons;
    int chars;
};

int fakeLabelWidth(int size, int chars) {
    return chars * (size * 11 / 20); // 22 -> 12 px a character, 14 -> 7
}

int fakeLineHeight(int size) {
    return size + size / 3 + 1;
}

HintMeasure measureOf(const vector<FakeHint> &hints) {
    HintMeasure m;
    m.buttonsWidth = [&hints](size_t i) { return hints[i].buttons; };
    m.labelWidth = [&hints](int size, size_t i) { return fakeLabelWidth(size, hints[i].chars); };
    m.lineHeight = [](int size) { return fakeLineHeight(size); };
    return m;
}

//*******************************
// the frozen copy: GuiLauncher::layoutHints() before G5e (launcher evoui_launcher_screen.cpp), with the fonts replaced
// by their size and the text/buttons measurers by the fake ones - nothing else changed
//*******************************
struct OldHint {
    FakeHint in;
    int labelX = 0, chipX = 0;
};

struct OldLine {
    vector<OldHint> items;
    int fontSize = 0;
    int labelY = 0, chipY = 0;
};

void oldLayoutLine(vector<OldHint> &items, const Rect &rect, bool allowDrop, int &outFont, int &outLabelY,
                   int &outChipY) {
    const int inset = 16;
    const int iconGap = 6;
    static const int sizes[] = {22, 20, 18, 16, 14};
    auto iconWidth = [&](const OldHint &h) { return h.in.buttons; };
    auto textWidth = [&](int font, const OldHint &h) { return fakeLabelWidth(font, h.in.chars); };
    int gap = 28;
    int total = 0;
    for (int size : sizes) {
        outFont = size;
        total = items.empty() ? 0 : -gap;
        for (const OldHint &h : items)
            total += iconWidth(h) + iconGap + textWidth(outFont, h) + gap;
        if (total <= rect.w - 2 * inset)
            break;
    }
    while (total > rect.w - 2 * inset && gap > 10) {
        total -= static_cast<int>(items.size()) * 4;
        gap -= 2;
    }
    while (allowDrop && total > rect.w - 2 * inset && items.size() > 1) {
        const OldHint dropped = items.back();
        total -= iconWidth(dropped) + iconGap + textWidth(outFont, dropped) + gap;
        items.pop_back();
    }
    int x = rect.x + max(inset, (rect.w - total) / 2);
    outLabelY = rect.y + (rect.h - fakeLineHeight(outFont)) / 2;
    outChipY = rect.y + (rect.h - 30) / 2;
    for (OldHint &h : items) {
        const int iconW = iconWidth(h);
        h.chipX = x;
        h.labelX = x + iconW + iconGap;
        x = h.labelX + textWidth(outFont, h) + gap;
    }
}

// the old layoutHints() body: the one-line rule, the halves
void oldLayout(const Rect &bar, OldLine &line1, OldLine &line2, bool &oneLine) {
    oneLine = bar.h < 48;
    if (oneLine)
        line2.items.clear();
    if (oneLine) {
        oldLayoutLine(line1.items, bar, false, line1.fontSize, line1.labelY, line1.chipY);
    } else {
        const Rect top(bar.x, bar.y, bar.w, bar.h / 2);
        const Rect bottom(bar.x, bar.y + bar.h / 2, bar.w, bar.h - bar.h / 2);
        oldLayoutLine(line1.items, top, false, line1.fontSize, line1.labelY, line1.chipY);
        oldLayoutLine(line2.items, bottom, true, line2.fontSize, line2.labelY, line2.chipY);
    }
}

OldLine oldLineOf(const vector<FakeHint> &hints) {
    OldLine line;
    for (const FakeHint &h : hints)
        line.items.push_back({h});
    return line;
}

// the new line against the old one: the same hints shown, at the same places, font and heights
void checkSame(const HintLineLayout &now, const OldLine &old) {
    REQUIRE(now.places.size() == old.items.size());
    CHECK(now.fontSize == old.fontSize);
    CHECK(now.labelY == old.labelY);
    CHECK(now.chipY == old.chipY);
    for (size_t i = 0; i < old.items.size(); i++) {
        CHECK(now.places[i].chipX == old.items[i].chipX);
        CHECK(now.places[i].labelX == old.items[i].labelX);
    }
}

bool sameRect(const Rect &a, const Rect &b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// a tiny deterministic generator (no <random>: the same numbers on every host)
struct Lcg {
    unsigned int state;
    explicit Lcg(unsigned int seed) : state(seed) {}
    int next(int lo, int hi) { // lo..hi inclusive
        state = state * 1664525u + 1013904223u;
        return lo + static_cast<int>((state >> 8) % static_cast<unsigned int>(hi - lo + 1));
    }
};

} // namespace

TEST_CASE("the bar's two line rects: the top half, the rest below it (an odd px goes to line 2)") {
    const Rect bar(360, 642, 900, 69);
    CHECK(sameRect(HintBar::topLine(bar), Rect(360, 642, 900, 34)));
    CHECK(sameRect(HintBar::bottomLine(bar), Rect(360, 676, 900, 35)));
    const Rect even(560, 624, 680, 72);
    CHECK(sameRect(HintBar::topLine(even), Rect(560, 624, 680, 36)));
    CHECK(sameRect(HintBar::bottomLine(even), Rect(560, 660, 680, 36)));
}

TEST_CASE("a bar under 48 px shows line 1 alone, at the bar's full height; 48 and over, two lines") {
    CHECK(HintBar::oneLineOnly(Rect(0, 0, 600, 47)));
    CHECK_FALSE(HintBar::oneLineOnly(Rect(0, 0, 600, 48)));

    const vector<FakeHint> one = {{30, 4}, {30, 9}};
    const vector<FakeHint> two = {{50, 11}, {30, 6}};
    const HintBarLayout low =
        HintBar::layout(Rect(100, 600, 700, 40), one.size(), measureOf(one), two.size(), measureOf(two));
    CHECK(low.oneLine);
    CHECK(low.line2.places.empty());
    CHECK(low.line1.chipY == 600 + (40 - 30) / 2);
    CHECK(low.line1.labelY == 600 + (40 - fakeLineHeight(22)) / 2);

    const HintBarLayout tall =
        HintBar::layout(Rect(100, 600, 700, 72), one.size(), measureOf(one), two.size(), measureOf(two));
    CHECK_FALSE(tall.oneLine);
    CHECK(tall.line1.chipY == 600 + (36 - 30) / 2);
    CHECK(tall.line2.chipY == 636 + (36 - 30) / 2);
    CHECK(tall.line2.places.size() == 2);
}

TEST_CASE("a short line: the largest font, the widest gap, centred in its rect") {
    // X Play, Down Game menu: 30 + 6 + 4*12 + 28 + 30 + 6 + 9*12 = 256 wide in a 680 rect
    const vector<FakeHint> hints = {{30, 4}, {30, 9}};
    const Rect rect(560, 624, 680, 36);
    const HintLineLayout line = HintBar::layoutLine(hints.size(), rect, false, measureOf(hints));
    CHECK(line.fontSize == 22);
    CHECK(line.gap == HintBar::WidestGap);
    REQUIRE(line.places.size() == 2);
    const int total = 30 + 6 + 48 + 28 + 30 + 6 + 108;
    CHECK(line.places[0].chipX == 560 + (680 - total) / 2);
    CHECK(line.places[0].labelX == line.places[0].chipX + 30 + 6);
    CHECK(line.places[1].chipX == line.places[0].labelX + 48 + 28);
    CHECK(line.places[1].labelX == line.places[1].chipX + 30 + 6);
    CHECK(line.labelY == 624 + (36 - fakeLineHeight(22)) / 2);
    CHECK(line.chipY == 624 + 3);
}

TEST_CASE("a long line shrinks its font first, then closes its gaps; line 1 never drops a hint") {
    // three hints of 30 chars: at 22 = 3 * (36 + 360) + 2 * 28 = 1244, at 16 (8 px a char) 3 * 276 + 56 = 884,
    // at 14 (7 px) 3 * 246 + 56 = 794: a 900 rect (868 room) takes 14 as the first that fits
    const vector<FakeHint> hints = {{30, 30}, {30, 30}, {30, 30}};
    HintLineLayout line = HintBar::layoutLine(hints.size(), Rect(0, 0, 900, 34), false, measureOf(hints));
    CHECK(line.fontSize == 14);
    CHECK(line.gap == 28);
    CHECK(line.places.size() == 3);

    // a 780 rect (748 room): 14 is still too wide (794), the gaps close - 794 - 12 per 2 px step: 782 (26), 770 (24),
    // 758 (22), 746 (20) - at gap 20 it fits
    line = HintBar::layoutLine(hints.size(), Rect(0, 0, 780, 34), false, measureOf(hints));
    CHECK(line.fontSize == 14);
    CHECK(line.gap == 20);
    CHECK(line.places.size() == 3);
    CHECK(line.places[0].chipX == max(16, (780 - 746) / 2));

    // far too narrow: the gaps stop at 10, line 1 keeps all its hints, starting at the inset
    line = HintBar::layoutLine(hints.size(), Rect(40, 0, 300, 34), false, measureOf(hints));
    CHECK(line.fontSize == 14);
    CHECK(line.gap == HintBar::TightestGap);
    CHECK(line.places.size() == 3);
    CHECK(line.places[0].chipX == 40 + HintBar::Inset);
}

TEST_CASE("line 2 drops hints from the right until it fits, never its first") {
    // four hints of 20 chars at 14: 4 * (36 + 140) + 3 * 28 = 788; the gaps close to 10 (9 steps of 16: 644); in a
    // 400 rect (368 room) it drops from the right, 186 a hint: 458, then 272 - two left
    const vector<FakeHint> hints = {{30, 20}, {30, 20}, {30, 20}, {30, 20}};
    HintLineLayout line = HintBar::layoutLine(hints.size(), Rect(0, 0, 400, 34), true, measureOf(hints));
    CHECK(line.fontSize == 14);
    CHECK(line.gap == HintBar::TightestGap);
    CHECK(line.places.size() == 2);
    CHECK(line.places[0].chipX == max(16, (400 - 272) / 2));

    // so narrow that even one does not fit: the first stays
    line = HintBar::layoutLine(hints.size(), Rect(0, 0, 100, 34), true, measureOf(hints));
    CHECK(line.places.size() == 1);
    CHECK(line.places[0].chipX == HintBar::Inset);
}

TEST_CASE("an empty line: nothing placed, the heights still set") {
    const vector<FakeHint> none;
    const HintLineLayout line = HintBar::layoutLine(0, Rect(10, 20, 600, 36), true, measureOf(none));
    CHECK(line.places.empty());
    CHECK(line.fontSize == 22);
    CHECK(line.chipY == 20 + 3);
}

TEST_CASE("the launcher's lines in its bars lay out exactly as the old layoutHints() did") {
    // the shapes of buildHintLines()' lines: the Games state, the menu row, the resume picker, an empty set - with
    // English-length and long (German-length) labels, in the default bar, ab2's and ab2.0.0's, and a one-line bar
    struct Case {
        vector<FakeHint> line1, line2;
    };
    const vector<Case> cases = {
        {{{30, 4}, {30, 17}, {30, 9}, {30, 10}}, {{62, 11}, {56, 6}, {30, 5}, {60, 6}}},   // Games, RetroArch
        {{{30, 4}, {30, 9}, {30, 10}}, {{62, 11}, {56, 6}, {30, 5}, {60, 6}}},             // Games
        {{{30, 7}, {30, 22}, {30, 12}, {30, 14}}, {{62, 24}, {56, 16}, {30, 9}, {60, 7}}}, // Games, German
        {{{30, 14}, {66, 6}, {30, 13}}, {{30, 5}, {60, 6}}},                               // the menu row
        {{{30, 12}, {30, 17}, {30, 10}}, {{30, 11}, {60, 6}}},                             // the menu row, German
        {{{30, 13}, {30, 11}, {66, 4}, {30, 4}}, {{60, 6}}},                               // resume: load
        {{{30, 24}, {30, 18}, {66, 11}, {30, 7}}, {{60, 6}}},                              // resume: load, German
        {{{30, 12}, {66, 4}, {30, 10}}, {{60, 6}}},                                        // resume: save
        {{{30, 10}}, {{62, 11}, {60, 6}}},                                                 // an empty set
        {{{30, 14}}, {{62, 23}, {60, 7}}},                                                 // an empty set, German
        {{}, {}},                                                                          // nothing at all
    };
    const vector<Rect> bars = {Rect(560, 624, 680, 72), Rect(330, 636, 930, 72), Rect(360, 642, 900, 68),
                               Rect(560, 650, 680, 40), Rect(600, 640, 420, 64)};
    for (const Rect &bar : bars) {
        for (const Case &c : cases) {
            CAPTURE(bar.x);
            CAPTURE(bar.w);
            CAPTURE(bar.h);
            OldLine old1 = oldLineOf(c.line1), old2 = oldLineOf(c.line2);
            bool oldOneLine = false;
            oldLayout(bar, old1, old2, oldOneLine);
            const HintBarLayout now =
                HintBar::layout(bar, c.line1.size(), measureOf(c.line1), c.line2.size(), measureOf(c.line2));
            CHECK(now.oneLine == oldOneLine);
            checkSame(now.line1, old1);
            if (!oldOneLine)
                checkSame(now.line2, old2);
            else
                CHECK(now.line2.places.empty());
        }
    }
}

TEST_CASE("generated lines and bars: the same places, fonts and drops as the old layoutHints()") {
    Lcg rng(20260930u);
    for (int round = 0; round < 3000; round++) {
        const Rect bar(rng.next(0, 700), rng.next(560, 700), rng.next(60, 1280), rng.next(20, 110));
        vector<FakeHint> line1, line2;
        const int n1 = rng.next(0, 5), n2 = rng.next(0, 6);
        for (int i = 0; i < n1; i++)
            line1.push_back({rng.next(24, 110), rng.next(1, 40)});
        for (int i = 0; i < n2; i++)
            line2.push_back({rng.next(24, 110), rng.next(1, 40)});
        CAPTURE(round);
        OldLine old1 = oldLineOf(line1), old2 = oldLineOf(line2);
        bool oldOneLine = false;
        oldLayout(bar, old1, old2, oldOneLine);
        const HintBarLayout now = HintBar::layout(bar, line1.size(), measureOf(line1), line2.size(), measureOf(line2));
        REQUIRE(now.oneLine == oldOneLine);
        checkSame(now.line1, old1);
        if (!oldOneLine)
            checkSame(now.line2, old2);
    }
}

//*******************************
// UIREV-36: the fixed 4 x 2 grid
//*******************************
namespace {

// the four columns' widest items as fixed numbers of the 22 px size, scaling with the size like fakeLabelWidth
HintGridMeasure gridMeasure(const vector<int> &widthsAt22) {
    HintGridMeasure m;
    m.columnWidth = [widthsAt22](int size, int column) { return widthsAt22[static_cast<size_t>(column)] * size / 22; };
    m.lineHeight = [](int size) { return fakeLineHeight(size); };
    return m;
}

} // namespace

TEST_CASE("the grid: the largest font that fits, the spare shared evenly, items at column left + 12") {
    const Rect bar(360, 624, 900, 64);
    const HintGridLayout g = HintBar::layoutGrid(bar, gridMeasure({150, 100, 120, 130}));
    CHECK_FALSE(g.oneLine);
    CHECK(g.fontSize == 22); // 500 + 4 x 22 = 588 <= 868
    const int spare = (900 - 32) - (500 + 4 * 22);
    CHECK(g.itemX[0] == 360 + 16 + 12);
    CHECK(g.itemX[1] - g.itemX[0] == 150 + 22 + spare / 4);
    CHECK(g.itemX[2] - g.itemX[1] == 100 + 22 + spare / 4);
    CHECK(g.itemX[3] - g.itemX[2] == 120 + 22 + spare / 4);
    CHECK(g.itemRoom[0] == 150 + spare / 4);
}

TEST_CASE("the grid: a wider language steps the font down, one font for both lines") {
    const Rect bar(360, 624, 900, 64);
    const HintGridLayout g = HintBar::layoutGrid(bar, gridMeasure({250, 200, 220, 230}));
    CHECK(g.fontSize == 18); // 22: 900 + 88 = 988 > 868; 20: 817 + 88 = 905 > 868; 18: 735 + 88 = 823 fits
    CHECK(g.labelY[0] < g.labelY[1]);
    CHECK(g.chipY[0] < g.chipY[1]);
}

TEST_CASE("the grid: the columns never leave the bar (the last item room ends inside the bar less the inset)") {
    for (int w : {640, 680, 900, 1000, 1280}) {
        const Rect bar(100, 600, w, 72);
        const HintGridLayout g = HintBar::layoutGrid(bar, gridMeasure({160, 110, 130, 140}));
        CAPTURE(w);
        CHECK(g.itemX[0] >= bar.x + HintBar::Inset);
        CHECK(g.itemX[3] + g.itemRoom[3] <= bar.x + bar.w - HintBar::Inset);
    }
}

TEST_CASE("the grid: when even 14 px does not fit the columns shrink and an item is told its room") {
    const Rect bar(0, 600, 400, 72);
    const HintGridLayout g = HintBar::layoutGrid(bar, gridMeasure({400, 400, 400, 400}));
    CHECK(g.fontSize == 14);
    for (int c = 0; c < HintGridLayout::Columns; c++)
        CHECK(g.itemRoom[c] >= 0);
    CHECK(g.itemX[3] + g.itemRoom[3] <= bar.w - HintBar::Inset);
}

TEST_CASE("the grid: a bar under 48 px has line 1 only, at the bar's full height") {
    const Rect bar(0, 650, 900, 40);
    const HintGridLayout g = HintBar::layoutGrid(bar, gridMeasure({150, 100, 120, 130}));
    CHECK(g.oneLine);
    CHECK(g.chipY[0] == 650 + (40 - HintBar::ChipHeight) / 2);
}

TEST_CASE("the grid does not depend on the state: the same measure gives the same layout") {
    const Rect bar(360, 634, 896, 76);
    const HintGridLayout a = HintBar::layoutGrid(bar, gridMeasure({150, 100, 120, 130}));
    const HintGridLayout b = HintBar::layoutGrid(bar, gridMeasure({150, 100, 120, 130}));
    for (int c = 0; c < HintGridLayout::Columns; c++) {
        CHECK(a.itemX[c] == b.itemX[c]);
        CHECK(a.itemRoom[c] == b.itemRoom[c]);
    }
    CHECK(a.fontSize == b.fontSize);
}
