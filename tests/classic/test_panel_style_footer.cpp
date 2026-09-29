//
// PanelStyle::footer() (panel_style.cpp ~356-399) - UIREV-3: the largest-font-that-fits fallback (FONT_22_MED
// gap 36 -> gap 22 -> FONT_20_BOLD -> FONT_15_BOLD) never checked whether even the smallest font still fit,
// so a footer whose hints stayed too wide at FONT_15_BOLD just drew past `right` and overlapped whatever sat
// there - the counter, on the German Memory Cards screen (C1 High, !autobleem\out\ui-review\report.md:80:
// "footer runs under the counter ('Seite' under 'Karte 1/3')"). The fix adds one more step: when
// FONT_15_BOLD/gap 22 still does not fit, drop every hint's own label (not a button chip's own text such as
// "L2" - see the header's comment on HintItem) and draw icons alone, which are fixed width and so always fit
// (short of a pathological hint list for even bare icons, left to clip as before - out of this task's scope).
//
// PanelStyle::footer() is not header-only (unlike GuiMenuBase, see test_menu_base_navigation.cpp) - it needs
// a live Gui/ThemeAssets/TextRenderer with real loaded theme fonts to measure text width, which needs an
// AppBase over a real theme/resources tree this repository does not ship (autobleem-core is the library, not
// the launcher). So, the same way test_menu_base_navigation.cpp does for GuiMenuBase's selection math, this
// suite drives a small local model of widthAt()'s formula (mirrored line for line from panel_style.cpp,
// including the button-chip-width and icons-only branches) instead of a real PanelStyle/Gui.
//
// The one thing a local model cannot give us is real glyph metrics, so `charWidth(fontPx)` below is a
// documented *approximation* (character count times a fixed px-per-character for the point size), not
// Open Sans' actual advance widths - it exists only to drive widthAt()'s branches with plausible numbers, the
// same way TestMenu in test_menu_base_navigation.cpp stands in for GuiMenuBase without being it. The German
// Memory Cards case below (the exact hint list and status text GuiMemcards::getStatusLine() builds - see
// autobleem/src/code/gui/menus/gui_memcards_menu.cpp:23-27 - German labels from
// autobleem/src/resources/lang/Deutsch.txt, panel geometry from Gui::setCompactPanel()/classicFooter(),
// panel_style.h/gui.cpp) was independently confirmed overlapping on a real build in the UI review
// (report.md:80, shot 18/20/21/65/66) - this suite's job is to pin the *algorithm's* missing check (the
// structural bug: three shrink steps with no existence check after the last one), not to reproduce the exact
// pixel count a real font would give.
//
#include "doctest/doctest.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {

using std::string;
using std::vector;

struct HintItem {
    vector<string> icons;
    string label;
};

// mirrors panel_style.cpp's file-local buttonRank()
int buttonRank(const string &icon) {
    static const char *order[] = {"X", "O", "T", "S", "Start", "Select", "L1", "R1", "L2", "R2", "Enter", "Esc", "Tab"};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++)
        if (icon == order[i])
            return static_cast<int>(i);
    return 100;
}

// stand-in for TextRenderer::textWidth(font, s) - character count times an approximate px-per-character for
// the point size (see the file header comment: not real glyph metrics, just enough to drive the branches)
int charWidth(int fontPx) {
    return (fontPx * 3 + 2) / 5; // ~0.6 px per point, close to Open Sans Bold/Medium's rough average advance
}
// codepoints, not bytes - the German labels below carry UTF-8 umlauts ("Zurück", "Löschen"), and a byte
// count would over-count them (2 bytes per umlaut) against a per-character width
size_t utf8Length(const string &s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            n++;
    return n;
}
int textWidth(int fontPx, const string &s) {
    return static_cast<int>(utf8Length(s)) * charWidth(fontPx);
}

// mirrors panel_style.cpp's footer() local buttonWidth lambda: a face button (X/O/T/S) is the fixed 30 px
// icon, a named one (L2, R2, ...) is a chip - its uppercased name at FONT_15_BOLD (15 px) plus 14 px padding
const int Font15 = 15, Font20 = 20, Font22 = 22;
int buttonWidth(const string &key) {
    if (key == "X" || key == "O" || key == "T" || key == "S")
        return 30;
    string upper = key;
    for (char &c : upper)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return textWidth(Font15, upper) + 14;
}

// mirrors the fixed footer() panel geometry: RowInset (24), the 36 px gap after the status text
const int RowInset = 24;

// mirrors panel_style.cpp's widthAt() lambda, `font` given as its point size
int widthAt(const vector<HintItem> &hints, int fontPx, int gap, bool iconsOnly) {
    int w = 0;
    for (const HintItem &h : hints) {
        for (const string &icon : h.icons)
            w += buttonWidth(icon) + 6;
        w += iconsOnly ? gap : 2 + textWidth(fontPx, h.label) + gap;
    }
    return w - gap;
}

// mirrors footer()'s font/gap fallback chain plus the fix's icons-only step; returns the font used (0 for
// icons-only) and whether it ended up icons-only, the same decision footer() itself now makes
struct FooterPlan {
    int fontPx;
    int gap;
    bool iconsOnly;
};
const int IconOnlyGap = 16; // matches the fix's panel_style.cpp constant

FooterPlan planFooter(const vector<HintItem> &hints, int room) {
    int fontPx = Font22, gap = 36;
    if (widthAt(hints, fontPx, gap, false) > room) {
        gap = 22;
        if (widthAt(hints, fontPx, gap, false) > room) {
            fontPx = Font20;
            if (widthAt(hints, fontPx, gap, false) > room) {
                fontPx = Font15;
                if (widthAt(hints, fontPx, gap, false) > room)
                    return FooterPlan{0, IconOnlyGap, true};
            }
        }
    }
    return FooterPlan{fontPx, gap, false};
}

// GuiMemcards::getStatusLine()'s hints, sorted the way footer() sorts them (X, O, T, S, then L2/R2 - the
// "|@L2|/|@R2|" marker joins into one hint, its icons() = {"L2","R2"}), German labels from Deutsch.txt
vector<HintItem> germanMemCardsHints() {
    vector<HintItem> hints = {
        {{"X"}, "Umbenennen"},   // Rename
        {{"O"}, "Zurück"},       // Back
        {{"T"}, "Löschen"},      // Delete
        {{"S"}, "Neue Karte"},   // New card
        {{"L2", "R2"}, "Seite"}, // Page
    };
    std::stable_sort(hints.begin(), hints.end(), [](const HintItem &a, const HintItem &b) {
        return buttonRank(a.icons.empty() ? "" : a.icons[0]) < buttonRank(b.icons.empty() ? "" : b.icons[0]);
    });
    return hints;
}

// GuiMemcards is a compact panel here (3 cards <= GuiMenuBase::CompactRows, no right-hand pane - the report's
// own C2 finding): Gui::setCompactPanel() centres an 800-wide panel (Gui::classicFooter()'s width when
// compact_ is set, gui.cpp:369-402), and footer()'s `room` is what is left of that width after RowInset on
// both edges and the status text ("Karte 1/3" - Card/Karte, FONT_22_MED) plus its 36 px gap.
int germanMemCardsRoom() {
    const int panelW = 800;
    int right = panelW - RowInset; // footer.x folded out - only the width matters for `room`
    const string status = "Karte 1/3";
    right -= textWidth(Font22, status) + 36;
    return right - RowInset;
}

} // namespace

//*******************************
// the bug: FONT_15_BOLD is still too wide for the German Memory Cards footer (report.md:80)
//*******************************

TEST_CASE("German Memory Cards footer: FONT_22_MED gap 36 does not fit in the compact panel's room") {
    const vector<HintItem> hints = germanMemCardsHints();
    const int room = germanMemCardsRoom();
    CHECK(widthAt(hints, Font22, 36, false) > room);
}

TEST_CASE("German Memory Cards footer: FONT_22_MED gap 22 does not fit either") {
    const vector<HintItem> hints = germanMemCardsHints();
    const int room = germanMemCardsRoom();
    CHECK(widthAt(hints, Font22, 22, false) > room);
}

TEST_CASE("German Memory Cards footer: FONT_20_BOLD does not fit") {
    const vector<HintItem> hints = germanMemCardsHints();
    const int room = germanMemCardsRoom();
    CHECK(widthAt(hints, Font20, 22, false) > room);
}

TEST_CASE("German Memory Cards footer: even FONT_15_BOLD, the smallest step, still does not fit - the bug"
          " (pre-fix, footer() had no step after this one, so it drew past `room` here)") {
    const vector<HintItem> hints = germanMemCardsHints();
    const int room = germanMemCardsRoom();
    const int w = widthAt(hints, Font15, 22, false);
    CHECK(w > room); // reproduced: the fallback is exhausted and the hints are still too wide
}

//*******************************
// the fix: icons-only always fits this case (and does not depend on the label-width approximation at all -
// it carries no h.label term, only the fixed icon/chip widths)
//*******************************

TEST_CASE("German Memory Cards footer: planFooter() falls through to icons-only") {
    const vector<HintItem> hints = germanMemCardsHints();
    const int room = germanMemCardsRoom();
    const FooterPlan plan = planFooter(hints, room);
    CHECK(plan.iconsOnly);
    CHECK(plan.fontPx == 0);
}

TEST_CASE("German Memory Cards footer: icons-only fits in the same room the labelled passes overflowed") {
    const vector<HintItem> hints = germanMemCardsHints();
    const int room = germanMemCardsRoom();
    const int w = widthAt(hints, Font15, IconOnlyGap, true);
    CHECK(w <= room);
    // icons-only never draws past `right` for this case even generously - comfortably under room, not a
    // near-miss the way the labelled passes were (only the fixed icon/chip widths matter here, no label)
    CHECK(w < room - 100);
}

//*******************************
// the three-font fallback is unchanged for a case that already fits - no regression
//*******************************

TEST_CASE("A short English footer still fits at FONT_22_MED gap 36 - the normal path is untouched") {
    const vector<HintItem> hints = {
        {{"X"}, "Select"},
        {{"O"}, "Back"},
    };
    const int room = 400; // a generously wide room, well clear of either step
    const FooterPlan plan = planFooter(hints, room);
    CHECK_FALSE(plan.iconsOnly);
    CHECK(plan.fontPx == Font22);
    CHECK(plan.gap == 36);
}

//*******************************
// scope limit (task step 3): a genuinely pathological hint list - too many hints for even bare icons in a
// tiny room - is left to clip as before; icons-only does not loop or crash, it just also reports > room
//*******************************

TEST_CASE("Pathological case: too many hints for even icons-only in a tiny room - clips, does not crash/loop") {
    vector<HintItem> hints;
    for (int i = 0; i < 20; i++)
        hints.push_back(HintItem{{"X"}, "Some Fairly Long Label " + std::to_string(i)});
    const int room = 50; // far too small for 20 hints under any fallback
    const FooterPlan plan = planFooter(hints, room);
    CHECK(plan.iconsOnly); // every earlier step also failed first
    const int w = widthAt(hints, Font15, IconOnlyGap, true);
    CHECK(w > room); // still overflows - out of this task's scope (UIREV-3 step 3), left to clip as before
}
