//
// PanelStyle::fromTheme's style roles (UIREV-29): each role is its own colour, the colour it names, or its
// fallback; a name survives the merge over the default theme and is resolved against the merged theme's own
// palette, so a theme that sets only `secondary` moves every role that names it. fromTheme is static and
// needs no Gui, so the real class is tested here.
//
#include "doctest/doctest.h"

#include "gui/panel_style.h"

#include <ableem/engine/theme_spec.h>

using ableem::Color;
using ableem::LauncherTheme;
using ableem::ThemeColor;
using ableem::ThemeSpec;

namespace {
bool same(const Color &a, int r, int g, int b) {
    return a.r == r && a.g == g && a.b == b;
}

ableem::ThemeColorRole named(const char *name) {
    ableem::ThemeColorRole role;
    role.ref = name;
    return role;
}
} // namespace

TEST_CASE("an empty theme: the Quick menu look - rows dim, the selected row bright") {
    const PanelStyle s = PanelStyle::fromTheme(LauncherTheme());
    CHECK(same(s.row, 100, 100, 100));
    CHECK(same(s.rowSelected, 255, 255, 255));
    CHECK(same(s.heading, 100, 100, 100));
    CHECK(same(s.value, 100, 100, 100));
    CHECK(same(s.description, 100, 100, 100));
    CHECK(same(s.footerText, 255, 255, 255));
    CHECK(same(s.selectionBand, 255, 255, 255));
    CHECK(same(s.edge, 100, 100, 100));
}

TEST_CASE("a role follows the base colour it falls back to") {
    LauncherTheme t;
    t.colors.secondary = ThemeColor(10, 20, 30);
    t.colors.text = ThemeColor(200, 210, 220);
    const PanelStyle s = PanelStyle::fromTheme(t);
    CHECK(same(s.row, 10, 20, 30));
    CHECK(same(s.value, 10, 20, 30)); // value -> row -> secondary
    CHECK(same(s.edge, 10, 20, 30));
    CHECK(same(s.rowSelected, 200, 210, 220));
    CHECK(same(s.selectionBand, 200, 210, 220));
}

TEST_CASE("a role may be a colour or name another colour, a role included") {
    LauncherTheme t;
    t.colors.row = named("text");
    t.colors.value = named("rowSelected");
    t.colors.rowSelected.color = ThemeColor(1, 2, 3);
    t.colors.edge = named("nosuchcolour"); // a typo counts as unset
    const PanelStyle s = PanelStyle::fromTheme(t);
    CHECK(same(s.row, 255, 255, 255));
    CHECK(same(s.value, 1, 2, 3));
    CHECK(same(s.edge, 100, 100, 100));
}

TEST_CASE("names that loop end in the text colour, not a hang") {
    LauncherTheme t;
    t.colors.row = named("value");
    t.colors.value = named("row");
    const PanelStyle s = PanelStyle::fromTheme(t);
    CHECK(same(s.row, 255, 255, 255));
}

TEST_CASE("a name inherited from the default theme resolves against the theme's own palette") {
    ThemeSpec base;
    base.launcher.colors.text = ThemeColor(255, 255, 255);
    base.launcher.colors.secondary = ThemeColor(100, 100, 100);
    base.launcher.colors.row = named("secondary");
    ThemeSpec theme;
    theme.launcher.colors.secondary = ThemeColor(40, 50, 60);
    theme.mergeOver(base);
    CHECK(theme.launcher.colors.row.ref == "secondary");
    const PanelStyle s = PanelStyle::fromTheme(theme.launcher);
    CHECK(same(s.row, 40, 50, 60));
}
