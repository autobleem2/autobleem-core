//
// abgui::Panel (G3b of docs/ab-gui-plan.md): the classic panel's geometry, moved out of Gui. Every number must be
// the one Gui computed before the move (nothing may move on screen), so each case compares the real class with
// a copy of the old Gui formulas (`old::` below, as they stood at G3a) - for the default theme's full panel and
// the compact panels on the 1280x720 canvas, and on a 1920x1080 one. That half is pure (no renderer). The
// Context's half - the backdrop drawer, the panel rect provider and their fallbacks, Panel::full/compact over a
// Context - needs a real Renderer, so like test_ab_gui_context.cpp it builds a headless GuiBase and skips itself
// where the environment has none.
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>
#include <ab_gui/panel.h>

#include <ableem/ui/gui_base.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::Context;
using abgui::FontRole;
using abgui::Panel;
using abgui::Style;
using ableem::GuiBase;
using ableem::Rect;

namespace {

// Gui's formulas before G3b (gui.cpp at G3a), with PanelStyle's constants spelled out: HeaderHeight 74,
// FooterHeight 54, RowInset 24
namespace old {
const int HeaderHeight = 74, FooterHeight = 54, RowInset = 24;

// Gui::classicPanel's full panel: the theme's menu panel, its bottom at least at the status line's foot
Rect fullPanel(const Rect &menuPanel, int statusTextY) {
    Rect panel = menuPanel;
    const int statusFoot = statusTextY + FooterHeight - 14;
    if (statusFoot > panel.y + panel.h)
        panel.h = statusFoot - panel.y;
    return panel;
}

// Gui::setCompactPanel
Rect compactPanel(int rows, int lineHeight, int screenWidth, int screenHeight) {
    const int width = 800;
    const int height = HeaderHeight + std::max(1, rows) * lineHeight + 8 + FooterHeight;
    return Rect((screenWidth - width) / 2, (screenHeight - height) / 2, width, height);
}

Rect content(const Rect &panel) {
    return Rect(panel.x, panel.y + HeaderHeight, panel.w, panel.h - HeaderHeight - FooterHeight);
}

Rect footer(const Rect &panel) {
    return Rect(panel.x, panel.y + panel.h - FooterHeight, panel.w, FooterHeight);
}

int rowsThatFit(const Rect &panel, int lineHeight) {
    return std::max(1, (content(panel).h - 4) / std::max(1, lineHeight));
}

// Gui::renderScrollMarkers' centre x and the two points' y
int markerX(const Rect &panel) {
    const Rect c = content(panel);
    return c.x + c.w - RowInset;
}
int markerAboveY(const Rect &panel) {
    return content(panel).y - 4;
}
int markerBelowY(const Rect &panel) {
    const Rect c = content(panel);
    return c.y + c.h - 6;
}
} // namespace old

bool same(const Rect &a, const Rect &b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// every number a Panel over `rect` gives against the old formulas, for rows of each of `lineHeights`
void checkAgainstOld(const Rect &rect, const vector<int> &lineHeights) {
    const Panel panel(rect, Style());
    CHECK(same(panel.rect(), rect));
    CHECK(same(panel.content(), old::content(rect)));
    CHECK(same(panel.footer(), old::footer(rect)));
    CHECK(panel.scrollMarkerX() == old::markerX(rect));
    CHECK(panel.scrollMarkerAboveY() == old::markerAboveY(rect));
    CHECK(panel.scrollMarkerBelowY() == old::markerBelowY(rect));
    for (int lineHeight : lineHeights)
        CHECK(panel.rowsThatFit(lineHeight) == old::rowsThatFit(rect, lineHeight));
}

// the classic fonts' line heights the themes give (Saira/Selawik at their sizes), 0 = an invalid font, and a
// few odd ones for the rounding
const vector<int> LineHeights{0, 1, 17, 25, 29, 31, 33, 37, 41, 44, 60};

struct MaybeGui {
    unique_ptr<GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<GuiBase>("ab_gui_test_panel", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_panel: skipping - no usable renderer in this environment (" << why << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

} // namespace

TEST_CASE("the full panel's geometry is the old Gui's, for the shipped themes' panel and a short one") {
    // default/ab2/aergb/evolution: menuPanel (30, 10, 1220, 615), the status line's text at 662 - the panel runs
    // down to 692
    const Rect shipped = old::fullPanel(Rect(30, 10, 1220, 615), 662);
    CHECK(same(shipped, Rect(30, 10, 1220, 692)));
    checkAgainstOld(shipped, LineHeights);
    // a panel the status line does not extend, and one too short for a row (at least one row fits)
    checkAgainstOld(old::fullPanel(Rect(40, 40, 1200, 640), 500), LineHeights);
    checkAgainstOld(Rect(100, 100, 400, 130), LineHeights);
}

TEST_CASE("a compact panel is where the old Gui put it, on the 1280x720 canvas and on a 1920x1080 one") {
    const Style style;
    for (int canvas = 0; canvas < 2; canvas++) {
        const int w = canvas == 0 ? 1280 : 1920;
        const int h = canvas == 0 ? 720 : 1080;
        for (int lineHeight : LineHeights) {
            for (int rows : {-1, 0, 1, 2, 3, 5, 8, 12, 20}) {
                const Rect rect = Panel::compactRect(style, rows, lineHeight, w, h);
                CHECK(same(rect, old::compactPanel(rows, lineHeight, w, h)));
                checkAgainstOld(rect, {lineHeight});
            }
        }
    }
    // the numbers themselves once: Memory Cards' 3 rows of a 33 px font on the 1280x720 canvas
    CHECK(same(Panel::compactRect(style, 3, 33, 1280, 720), Rect(240, 242, 800, 235)));
    // taller than the canvas: the same truncation toward zero as before
    CHECK(same(Panel::compactRect(style, 30, 33, 1280, 720), Rect(240, -203, 800, 1126)));
}

TEST_CASE("a compact panel widens for a long footer - one row, never two: max(800, what it needs), at most the canvas "
          "less the margins") {
    const Style style;
    // a footer that fits 800 changes nothing
    CHECK(Panel::compactWidth(style, 0, 1280) == 800);
    CHECK(Panel::compactWidth(style, 800, 1280) == 800);
    // a longer one widens the panel to it
    CHECK(Panel::compactWidth(style, 1000, 1280) == 1000);
    // ... up to the full panel's width (1280 - 2 x 40), then it stays (the footer then shrinks its font / labels)
    CHECK(Panel::compactWidth(style, 1200, 1280) == 1200);
    CHECK(Panel::compactWidth(style, 5000, 1280) == 1280 - 2 * style.margin);
    CHECK(Panel::compactWidth(style, 5000, 1920) == 1920 - 2 * style.margin);
    // a canvas narrower than the compact width shrinks it: the canvas less the margins (a 4:3 output's 800x600, 640x480)
    CHECK(Panel::compactWidth(style, 0, 800) == 800 - 2 * style.margin);
    CHECK(Panel::compactWidth(style, 5000, 640) == 640 - 2 * style.margin);
    CHECK(Panel::compactWidth(style, 5000, 600) == 600 - 2 * style.margin);
    // the rect keeps the rows' height and is centred at the wider width
    const Rect wide = Panel::compactRect(style, 3, 33, 1280, 720, 1000);
    CHECK(wide.w == 1000);
    CHECK(wide.x == 140);
    CHECK(wide.y == 242);
    CHECK(wide.h == 235);
}

TEST_CASE("without a provider the panel is the canvas inset by the margin, and the backdrop a black clear") {
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer());
    const int m = Style::DefaultMargin;
    CHECK(same(ctx.panelRect(), Rect(m, m, 320 - 2 * m, 240 - 2 * m)));
    ctx.drawBackdrop(); // no drawer: clears, no crash
}

TEST_CASE("the program's panel rect and backdrop win; Panel::full and compact go through the Context") {
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer(), g.gui->input(), g.gui->platform());
    const Rect themePanel = old::fullPanel(Rect(30, 10, 1220, 615), 662);
    int asked = 0;
    ctx.panelProvider = [&]() {
        asked++;
        return themePanel;
    };
    int drawn = 0;
    ctx.backdropDrawer = [&]() { drawn++; };

    CHECK(same(ctx.panelRect(), themePanel));
    CHECK(asked == 1);
    ctx.drawBackdrop();
    CHECK(drawn == 1);

    const Panel full = Panel::full(ctx);
    CHECK(same(full.rect(), themePanel));
    CHECK(same(full.content(), old::content(themePanel)));

    // centred on the Context's canvas (the renderer's logical size: 320x240 here); an invalid font counts 0
    const ableem::Font none;
    const Panel compact = Panel::compact(ctx, 2, none);
    CHECK(same(compact.rect(), old::compactPanel(2, 0, 320, 240)));
}

TEST_CASE("rowsThatFit with an invalid font asks the Context for the Classic font") {
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer());
    vector<FontRole> roles;
    const ableem::Font none;
    ctx.fontProvider = [&](FontRole role) -> const ableem::Font & {
        roles.push_back(role);
        return none;
    };
    const Rect rect(30, 10, 1220, 692);
    const Panel panel(rect, Style());
    CHECK(panel.rowsThatFit(ctx, none) == old::rowsThatFit(rect, 0));
    REQUIRE(roles.size() == 1);
    CHECK(roles[0] == FontRole::Classic);
}
