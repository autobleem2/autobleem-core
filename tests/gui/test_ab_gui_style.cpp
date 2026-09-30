//
// abgui::Style on its own (ab_gui, G2 of docs/ab-gui-plan.md): the metrics' and colours' defaults, the colour
// roles resolved from a plain ColorRoles block (no theme.json, no AutoBleem types), the footer's hint protocol,
// and the footer's label shortening now that it is ab_gui's. Everything here is static or plain data - no
// renderer, no Context - so the real class is tested. AutoBleem's side (LauncherTheme -> ColorRoles, through
// PanelStyle::fromTheme) is test_panel_style_roles.cpp.
//
#include "doctest/doctest.h"

#include <ab_gui/footer_shorten.h>
#include <ab_gui/style.h>

#include <string>
#include <vector>

using abgui::ColorRoles;
using abgui::HintItem;
using abgui::OptionalColor;
using abgui::RoleColor;
using abgui::Style;
using ableem::Color;

namespace {
bool same(const Color &a, int r, int g, int b) {
    return a.r == r && a.g == g && a.b == b && a.a == 255;
}

RoleColor named(const char *name) {
    RoleColor role;
    role.ref = name;
    return role;
}

RoleColor colour(int r, int g, int b) {
    RoleColor role;
    role.color = OptionalColor(
        Color(static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b)));
    return role;
}

OptionalColor base(int r, int g, int b) {
    return OptionalColor(
        Color(static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b)));
}
} // namespace

TEST_CASE("a default Style has today's metrics") {
    const Style s;
    CHECK(s.headerHeight == 74);
    CHECK(s.footerHeight == 54);
    CHECK(s.rowHeight == 60);
    CHECK(s.rowInset == 24);
    CHECK(s.margin == 40);
    CHECK(s.selectionBar == 5);
    CHECK(s.titleTop == 18);
    CHECK(s.buttonHeight == 30);
    CHECK(s.footerTop == 14);
    CHECK(s.dimAlpha == 110);
    CHECK(s.sheetAlpha == 200);
    CHECK(s.edgeAlpha == 160);
    CHECK(s.bandAlpha == 38);
    CHECK(s.labelAlpha == 70);
    CHECK(s.disabledAlpha == 150);
    CHECK(s.textShadow);
    CHECK(Style::DefaultHeaderHeight == 74);
    CHECK(Style::DefaultFooterHeight == 54);
    CHECK(Style::DefaultRowHeight == 60);
    CHECK(Style::DefaultRowInset == 24);
    CHECK(Style::DefaultMargin == 40);
    CHECK(Style::DefaultSelectionBar == 5);
}

TEST_CASE("an empty block is the default look: white text, grey secondary, rows dim, the selected row bright") {
    const Style d;
    const Style s = Style::fromColors(ColorRoles());
    for (const Style *style : {&d, &s}) {
        CHECK(same(style->text, 255, 255, 255));
        CHECK(same(style->secondary, 100, 100, 100));
        CHECK(same(style->hint, 100, 100, 100));
        CHECK(same(style->row, 100, 100, 100));
        CHECK(same(style->rowSelected, 255, 255, 255));
        CHECK(same(style->heading, 100, 100, 100));
        CHECK(same(style->value, 100, 100, 100));
        CHECK(same(style->description, 100, 100, 100));
        CHECK(same(style->footerText, 255, 255, 255));
        CHECK(same(style->selectionBand, 255, 255, 255));
        CHECK(same(style->edge, 100, 100, 100));
    }
}

TEST_CASE("unset roles follow the base colour they fall back to") {
    ColorRoles roles;
    roles.text = base(200, 210, 220);
    roles.secondary = base(10, 20, 30);
    const Style s = Style::fromColors(roles);
    CHECK(same(s.hint, 10, 20, 30)); // hint -> secondary
    CHECK(same(s.row, 10, 20, 30));
    CHECK(same(s.heading, 10, 20, 30));
    CHECK(same(s.value, 10, 20, 30)); // value -> row -> secondary
    CHECK(same(s.description, 10, 20, 30));
    CHECK(same(s.edge, 10, 20, 30));
    CHECK(same(s.rowSelected, 200, 210, 220));
    CHECK(same(s.footerText, 200, 210, 220));
    CHECK(same(s.selectionBand, 200, 210, 220));
}

TEST_CASE("a hint colour of its own is kept") {
    ColorRoles roles;
    roles.secondary = base(10, 20, 30);
    roles.hint = base(40, 50, 60);
    const Style s = Style::fromColors(roles);
    CHECK(same(s.hint, 40, 50, 60));
    CHECK(same(s.row, 10, 20, 30));
}

TEST_CASE("a role may be a colour or name another colour, a role included") {
    ColorRoles roles;
    roles.row = named("text");
    roles.value = named("rowSelected");
    roles.rowSelected = colour(1, 2, 3);
    roles.edge = named("nosuchcolour"); // a typo counts as unset
    const Style s = Style::fromColors(roles);
    CHECK(same(s.row, 255, 255, 255));
    CHECK(same(s.value, 1, 2, 3));
    CHECK(same(s.rowSelected, 1, 2, 3));
    CHECK(same(s.edge, 100, 100, 100));
}

TEST_CASE("a colour wins over a name in the same role") {
    ColorRoles roles;
    roles.heading = colour(7, 8, 9);
    roles.heading.ref = "text";
    const Style s = Style::fromColors(roles);
    CHECK(same(s.heading, 7, 8, 9));
}

TEST_CASE("the base names hint and selection resolve too") {
    ColorRoles roles;
    roles.hint = base(40, 50, 60);
    roles.description = named("hint");
    roles.edge = named("selection"); // selection unset: text
    roles.selectionBand = named("selection");
    const Style unsetSelection = Style::fromColors(roles);
    CHECK(same(unsetSelection.description, 40, 50, 60));
    CHECK(same(unsetSelection.edge, 255, 255, 255));

    roles.selection = base(90, 80, 70);
    const Style s = Style::fromColors(roles);
    CHECK(same(s.edge, 90, 80, 70));
    CHECK(same(s.selectionBand, 90, 80, 70));
}

TEST_CASE("names that loop end in the text colour, not a hang") {
    ColorRoles roles;
    roles.text = base(11, 22, 33);
    roles.row = named("value");
    roles.value = named("row");
    const Style s = Style::fromColors(roles);
    CHECK(same(s.row, 11, 22, 33));
    CHECK(same(s.value, 11, 22, 33));
}

TEST_CASE("a role naming itself counts as unset") {
    ColorRoles roles;
    roles.secondary = base(10, 20, 30);
    roles.row = named("row");
    const Style s = Style::fromColors(roles);
    CHECK(same(s.row, 10, 20, 30));
}

TEST_CASE("rowColor and valueColor pick by selection") {
    ColorRoles roles;
    roles.row = colour(1, 1, 1);
    roles.value = colour(2, 2, 2);
    roles.rowSelected = colour(3, 3, 3);
    const Style s = Style::fromColors(roles);
    CHECK(same(s.rowColor(false), 1, 1, 1));
    CHECK(same(s.rowColor(true), 3, 3, 3));
    CHECK(same(s.valueColor(false), 2, 2, 2));
    CHECK(same(s.valueColor(true), 3, 3, 3));
}

TEST_CASE("fromColors leaves the metrics and the shadow at their defaults") {
    ColorRoles roles;
    roles.text = base(1, 2, 3);
    const Style s = Style::fromColors(roles);
    CHECK(s.headerHeight == Style::DefaultHeaderHeight);
    CHECK(s.rowInset == Style::DefaultRowInset);
    CHECK(s.textShadow);
}

TEST_CASE("parseHints: the status before the first marker, icons joined across a separator") {
    std::string status;
    const std::vector<HintItem> items =
        Style::parseHints("Card 1/12   |@L1|/|@R1| Page  |@X| Rename  |@O| Go back |", status);
    CHECK(status == "Card 1/12");
    REQUIRE(items.size() == 3);
    REQUIRE(items[0].icons.size() == 2);
    CHECK(items[0].icons[0] == "L1");
    CHECK(items[0].icons[1] == "R1");
    CHECK(items[0].label == "Page");
    REQUIRE(items[1].icons.size() == 1);
    CHECK(items[1].icons[0] == "X");
    CHECK(items[1].label == "Rename");
    REQUIRE(items[2].icons.size() == 1);
    CHECK(items[2].icons[0] == "O");
    CHECK(items[2].label == "Go back");
}

TEST_CASE("parseHints: back-to-back markers share a hint, a trailing marker keeps its icon") {
    std::string status = "stale";
    const std::vector<HintItem> items = Style::parseHints("|@L2||@R2| Page |@Start|", status);
    CHECK(status.empty());
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].icons.size() == 2);
    CHECK(items[0].icons[0] == "L2");
    CHECK(items[0].icons[1] == "R2");
    CHECK(items[0].label == "Page");
    REQUIRE(items[1].icons.size() == 1);
    CHECK(items[1].icons[0] == "Start");
    CHECK(items[1].label.empty());
}

TEST_CASE("parseHints: a line with no markers is all status") {
    std::string status;
    const std::vector<HintItem> items = Style::parseHints("  Game 3/21 ", status);
    CHECK(status == "Game 3/21");
    CHECK(items.empty());
}

TEST_CASE("the footer's label shortening is ab_gui's") {
    std::vector<std::string> labels = {"Back", "Select"};
    auto width = [](const std::vector<std::string> &l) {
        int w = 0;
        for (const std::string &s : l)
            w += static_cast<int>(abgui::footerUtf8Length(s));
        return w;
    };
    CHECK(abgui::shortenFooterLabels(labels, 100, width)); // fits at once: unchanged
    CHECK(labels[0] == "Back");
    CHECK(labels[1] == "Select");
    CHECK(abgui::shortenFooterLabels(labels, 9, width)); // "Select" (6) -> "Sel.." (5): 4 + 5
    CHECK(labels[0] == "Back");
    CHECK(labels[1] == "Sel..");
}

TEST_CASE("the keyboard, spinner, progress and tab metrics default to what the callers drew") {
    const Style s;
    CHECK(s.keyAlpha == 18);
    CHECK(s.keyFunctionAlpha == 8);
    CHECK(s.keyLitAlpha == 50);
    CHECK(s.keySelectedAlpha == 60);
    CHECK(s.keyEdgeAlpha == 110);
    CHECK(s.fieldAlpha == 14);
    CHECK(s.fieldEdgeAlpha == 160);
    CHECK(s.caretWidth == 2);
    CHECK(s.spinnerDots == 12);
    CHECK(s.spinnerRadius == 30);
    CHECK(s.spinnerDot == 8);
    CHECK(s.spinnerFade == 19);
    CHECK(s.progressTrackAlpha == 120);
    CHECK(s.tabHeight == 3);
}

TEST_CASE("tone: a role's colour, its own alpha unless one is given, nothing for None") {
    Style s;
    s.text = Color(10, 20, 30, 255);
    s.secondary = Color(40, 50, 60, 200);
    s.edge = Color(70, 80, 90, 255);
    s.selectionBand = Color(1, 2, 3, 255);
    const Color text = s.tone(abgui::Tone::Text);
    CHECK((text.r == 10 && text.g == 20 && text.b == 30 && text.a == 255));
    CHECK(s.tone(abgui::Tone::Secondary).a == 200); // its own
    CHECK(s.tone(abgui::Tone::Secondary, 120).a == 120);
    CHECK(s.tone(abgui::Tone::Edge, 999).a == 255); // clamped
    const Color band = s.tone(abgui::Tone::SelectionBand, 38);
    CHECK((band.r == 1 && band.g == 2 && band.b == 3 && band.a == 38));
    const Color black = s.tone(abgui::Tone::Black, 235);
    CHECK((black.r == 0 && black.g == 0 && black.b == 0 && black.a == 235));
    const Color white = s.tone(abgui::Tone::White);
    CHECK((white.r == 255 && white.g == 255 && white.b == 255 && white.a == 255));
    CHECK(s.tone(abgui::Tone::None).a == 0);
}
