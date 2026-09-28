//
// GuiTextPage's line layout: the indent and a numbered item's hanging marker.
//
#include "doctest/doctest.h"

#include "gui/screens/gui_text_page.h"

TEST_CASE("GuiTextPage::splitItem keeps a plain line whole") {
    const GuiTextPage::Item item = GuiTextPage::splitItem("To pair a controller of this kind");
    CHECK(item.indent == 0);
    CHECK(item.marker.empty());
    CHECK(item.text == "To pair a controller of this kind");
}

TEST_CASE("GuiTextPage::splitItem takes the indent off an indented line") {
    const GuiTextPage::Item item = GuiTextPage::splitItem("  NOTE 1: Genuine SONY DualShock3");
    CHECK(item.indent == 2);
    CHECK(item.marker.empty()); // "NOTE 1:" is not a numbered item
    CHECK(item.text == "NOTE 1: Genuine SONY DualShock3");
}

TEST_CASE("GuiTextPage::splitItem hangs a numbered item's marker") {
    GuiTextPage::Item item = GuiTextPage::splitItem("  1. Using a micro USB charging cable");
    CHECK(item.indent == 2);
    CHECK(item.marker == "1. ");
    CHECK(item.text == "Using a micro USB charging cable");

    item = GuiTextPage::splitItem("12. Twelve");
    CHECK(item.indent == 0);
    CHECK(item.marker == "12. ");
    CHECK(item.text == "Twelve");
}

TEST_CASE("GuiTextPage::splitItem leaves what only looks like a number") {
    CHECK(GuiTextPage::splitItem("1.5 seconds").marker.empty()); // no space after the dot
    CHECK(GuiTextPage::splitItem("2020 was the year").marker.empty());
    CHECK(GuiTextPage::splitItem("1. ").marker.empty()); // a marker with nothing after it stays text
    const GuiTextPage::Item blank = GuiTextPage::splitItem("   ");
    CHECK(blank.indent == 3);
    CHECK(blank.text.empty());
}
