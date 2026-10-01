//
// ab_gui G5t (docs/ab-gui-plan.md, decision 16): the two rect rules the Store's installed badge and letter-jump box
// use (pure), and the `disabled` role - the DisabledVeil's defaults, the Context's provider, and the colour a disabled
// row's text takes (the `description` role once a theme has the role, else the row's own).
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>
#include <ab_gui/layout.h>
#include <ab_gui/style.h>

#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

using namespace std;
using ableem::Color;
using ableem::Rect;

namespace {

struct MaybeGui {
    unique_ptr<ableem::GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<ableem::GuiBase>("ab_gui_test_layout", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_layout: skipping - no usable renderer here (" << why << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

bool same(const Color &a, const Color &b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

} // namespace

TEST_CASE("trailingBadgeRect: 24 px in from the list's inner right edge, centred on the row") {
    // the Store's row: 60 tall, the list's inner right edge at 891 (panel 40 + list 852 - 1), a 32x32 badge
    const Rect r = abgui::trailingBadgeRect(891, 300, 60, 32, 32);
    CHECK(r.w == 32);
    CHECK(r.h == 32);
    CHECK(r.x == 891 - 24 - 32);
    CHECK(r.y == 300 + 14);
    CHECK(r.x + r.w == 891 - abgui::BadgeInset); // its right edge is the inset from the panel's

    // the fallback mark, 24x24
    const Rect mark = abgui::trailingBadgeRect(891, 300, 60, 24, 24);
    CHECK(mark.x + mark.w == 867);
    CHECK(mark.y == 318);

    // an odd leftover pixel goes below; a badge taller than its row overhangs both ends
    CHECK(abgui::trailingBadgeRect(100, 0, 61, 10, 10).y == 25);
    CHECK(abgui::trailingBadgeRect(100, 10, 20, 10, 30).y == 5);
    // another inset
    CHECK(abgui::trailingBadgeRect(100, 0, 20, 10, 10, 8).x == 82);
}

TEST_CASE("switchRect: the switch image ends at the value's right edge, centred on the row (G5m)") {
    // the test theme's 60 x 30 switch in a 28 px classic row whose value ends at 868
    const Rect r = abgui::switchRect(868, 124, 28, 60, 30);
    CHECK(r.x == 808);
    CHECK(r.x + r.w == 868);
    CHECK(r.y == 123); // 1 px above the row: the image is taller than the line
    CHECK(abgui::switchRect(100, 0, 61, 10, 10).y == 25); // an odd leftover px goes below
}

TEST_CASE("centredIn: a box centred on the list panel, the odd pixel right and below") {
    const Rect list(40, 100, 852, 480);
    const Rect box = abgui::centredIn(list, 88, 88);
    CHECK(box.x == 40 + (852 - 88) / 2);
    CHECK(box.y == 100 + (480 - 88) / 2);
    CHECK(box.w == 88);
    CHECK(box.h == 88);
    // the box's centre is the panel's (to a pixel), so it is nowhere near the tabs on the header's right
    CHECK(abs((box.x + box.w / 2) - (list.x + list.w / 2)) <= 1);
    CHECK(abs((box.y + box.h / 2) - (list.y + list.h / 2)) <= 1);
    CHECK(box.y > list.y);

    const Rect odd = abgui::centredIn(Rect(0, 0, 101, 101), 50, 50);
    CHECK(odd.x == 25);
    CHECK(odd.y == 25);
    // bigger than the panel: hangs out on both sides
    CHECK(abgui::centredIn(Rect(10, 10, 20, 20), 40, 40).x == 0);
}

TEST_CASE("DisabledVeil: unset by default, black at 150 - today's veil") {
    const abgui::DisabledVeil veil;
    CHECK_FALSE(veil.set);
    CHECK(same(veil.color, Color(0, 0, 0, 255)));
    CHECK(veil.alpha == 150);
    CHECK(same(veil.drawn(), Color(0, 0, 0, 150)));
    CHECK(veil.alpha == abgui::Style().disabledAlpha);

    abgui::DisabledVeil themed;
    themed.set = true;
    themed.color = Color(20, 40, 60, 255);
    themed.alpha = 90;
    CHECK(same(themed.drawn(), Color(20, 40, 60, 90)));
}

TEST_CASE("Context::disabledVeil and Style::disabledColor: no role = the row's own colour, a role = description") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    abgui::Context ctx(maybe.gui->renderer());
    abgui::Style style;
    style.row = Color(1, 2, 3, 255);
    style.description = Color(9, 8, 7, 255);
    const Color normal(1, 2, 3, 255);

    // no provider, then a provider that says unset: nothing changes
    CHECK_FALSE(ctx.disabledVeil().set);
    CHECK(same(style.disabledColor(ctx, normal), normal));
    ctx.veilProvider = [] { return abgui::DisabledVeil(); };
    CHECK_FALSE(ctx.disabledVeil().set);
    CHECK(same(style.disabledColor(ctx, normal), normal));

    // the theme's role: the row's text moves to the description role, the veil is the theme's
    ctx.veilProvider = [] {
        abgui::DisabledVeil veil;
        veil.set = true;
        veil.color = Color(200, 210, 220, 255);
        veil.alpha = 70;
        return veil;
    };
    CHECK(ctx.disabledVeil().set);
    CHECK(ctx.disabledVeil().alpha == 70);
    CHECK(same(style.disabledColor(ctx, normal), style.description));
}

TEST_CASE("InactiveAlphas: every value unset = the caller's own alpha; a theme's value wins, clamped (G5r9)") {
    const abgui::InactiveAlphas none;
    CHECK(none.resume == abgui::InactiveAlphas::Unset);
    CHECK(none.tab == abgui::InactiveAlphas::Unset);
    CHECK(none.barTrack == abgui::InactiveAlphas::Unset);
    CHECK(abgui::InactiveAlphas::orToday(none.resume, 120) == 120);
    CHECK(abgui::InactiveAlphas::orToday(none.barTrack, 77) == 77);

    CHECK(abgui::InactiveAlphas::orToday(40, 120) == 40);
    CHECK(abgui::InactiveAlphas::orToday(0, 120) == 0); // 0 is a value, not unset
    CHECK(abgui::InactiveAlphas::orToday(255, 120) == 255);
    CHECK(abgui::InactiveAlphas::orToday(900, 120) == 255);
}

TEST_CASE("Context::inactiveAlphas: no provider = all unset; the provider's values come through (G5r9)") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    abgui::Context ctx(maybe.gui->renderer());
    CHECK(ctx.inactiveAlphas().resume == abgui::InactiveAlphas::Unset);
    CHECK(ctx.inactiveAlphas().tab == abgui::InactiveAlphas::Unset);
    CHECK(ctx.inactiveAlphas().barTrack == abgui::InactiveAlphas::Unset);

    ctx.inactiveProvider = [] {
        abgui::InactiveAlphas a;
        a.tab = 50;
        return a;
    };
    CHECK(ctx.inactiveAlphas().tab == 50);
    CHECK(ctx.inactiveAlphas().resume == abgui::InactiveAlphas::Unset);
    CHECK(abgui::InactiveAlphas::orToday(ctx.inactiveAlphas().resume, 120) == 120);
    CHECK(abgui::InactiveAlphas::orToday(ctx.inactiveAlphas().tab, 120) == 50);
}
