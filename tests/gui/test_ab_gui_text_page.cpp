//
// abgui::TextPage (G3h of docs/ab-gui-plan.md): the text wrapping and the line layout (pure), the scroll rule (pure),
// and the events - Circle/Escape close with the Cancel sound, the d-pad and the arrows scroll a line, L2/R2 and Page
// Up/Down a page, each with the Cursor sound only when the page moved - through a headless GuiBase (those cases skip
// themselves without a renderer, like test_ab_gui_screen).
//
#include "doctest/doctest.h"

#include <ab_gui/actions.h>
#include <ab_gui/context.h>
#include <ab_gui/text_page.h>

#include <ableem/ui/gui_base.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::Context;
using abgui::TextPage;
using abgui::UiSound;
using ableem::Button;
using ableem::Event;
using ableem::GuiBase;
using ableem::Key;

namespace {

// one column per character, so a row's width is its length
int chars(const string &s) {
    return static_cast<int>(s.size());
}

struct MaybeGui {
    unique_ptr<GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<GuiBase>("ab_gui_test_text_page", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_text_page: skipping - no usable renderer here (" << why << ")");
        }
        if (gui)
            gui->input().flushEvents();
    }

    bool available() const { return gui != nullptr; }
};

// a page with a fitted layout set by hand (draw() needs a real renderer and fonts): 10 lines, 4 rows a page
struct Page : TextPage {
    Page(GuiBase &gui, Context &ctx) : TextPage(gui, ctx) {
        for (int i = 0; i < 10; i++)
            lines.push_back("line " + to_string(i));
        rowsThatFit_ = 4;
        lastLineShown_ = 4; // lines 0..3 are on screen
    }
    // what a draw() from the current first line would have fitted: `rowsThatFit_` lines
    void refit() { lastLineShown_ = std::min(firstLine_ + rowsThatFit_, static_cast<int>(lines.size())); }
    void draw() override {}
};

struct Side {
    Context ctx;
    vector<UiSound> sounds;
    explicit Side(GuiBase &gui) : ctx(gui.renderer(), gui.input(), gui.platform()) {
        ctx.soundPlayer = [this](UiSound s) { sounds.push_back(s); };
    }
};

Event button(Button b) {
    Event e;
    e.type = Event::Type::ButtonDown;
    e.button = b;
    return e;
}

Event dpad(bool down, Button b) {
    Event e;
    e.type = down ? Event::Type::DpadDown : Event::Type::DpadUp;
    e.button = b;
    return e;
}

Event key(Key k) {
    Event e;
    e.type = Event::Type::KeyDown;
    e.key = k;
    return e;
}

// the event through the Input as the loop reads it (the d-pad state follows), then to the page
void feed(GuiBase &gui, Page &page, const Event &event) {
    gui.input().inject(event);
    Event polled;
    REQUIRE(gui.input().poll(polled));
    page.handle(polled);
    page.refit();
}

} // namespace

TEST_CASE("wrapText breaks at spaces and tabs and keeps an empty text as one row") {
    CHECK(abgui::wrapText("", 10, chars) == vector<string>{""});
    CHECK(abgui::wrapText("one two three", 20, chars) == vector<string>{"one two three"});
    CHECK(abgui::wrapText("one two three", 7, chars) == vector<string>{"one two", "three"});
    CHECK(abgui::wrapText("one\ttwo  three", 7, chars) == vector<string>{"one two", "three"});
}

TEST_CASE("wrapText cuts a word wider than the column into pieces that fit") {
    CHECK(abgui::wrapText("abcdefghij", 4, chars) == vector<string>{"abcd", "efgh", "ij"});
    // never in the middle of a UTF-8 character: "zaz\xC3\xB3\xC5\x82\xC4\x87" is 6 characters in 9 bytes; a column of
    // 5 bytes takes "zaz\xC3\xB3" (5), then the rest
    const auto rows = abgui::wrapText("zaz\xC3\xB3\xC5\x82\xC4\x87", 5, chars);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == "zaz\xC3\xB3");
    CHECK(rows[1] == "\xC5\x82\xC4\x87");
}

TEST_CASE("TextPage::splitItem keeps a plain line whole and hangs a numbered item's marker") {
    TextPage::Item item = TextPage::splitItem("To pair a controller of this kind");
    CHECK(item.indent == 0);
    CHECK(item.marker.empty());
    CHECK(item.text == "To pair a controller of this kind");

    item = TextPage::splitItem("  1. Using a micro USB charging cable");
    CHECK(item.indent == 2);
    CHECK(item.marker == "1. ");
    CHECK(item.text == "Using a micro USB charging cable");

    item = TextPage::splitItem("  NOTE 1: Genuine SONY DualShock3");
    CHECK(item.indent == 2);
    CHECK(item.marker.empty());

    CHECK(TextPage::splitItem("1.5 seconds").marker.empty());
    CHECK(TextPage::splitItem("1. ").marker.empty());
    CHECK(TextPage::splitItem("   ").indent == 3);
}

TEST_CASE("TextPage::scrolled moves only while there is somewhere to go") {
    // 10 lines, lines 0..3 shown (lastShown = 4)
    CHECK(TextPage::scrolled(0, 1, 4, 10) == 1);
    CHECK(TextPage::scrolled(0, 4, 4, 10) == 4);
    CHECK(TextPage::scrolled(0, -1, 4, 10) == 0); // at the top
    CHECK(!TextPage::canScroll(0, -4, 4, 10));
    // the end shows (lastShown = count): no more down
    CHECK(TextPage::scrolled(6, 1, 10, 10) == 6);
    CHECK(!TextPage::canScroll(6, 4, 10, 10));
    // a page down never passes the last line; a page up never passes the first
    CHECK(TextPage::scrolled(5, 40, 9, 10) == 9);
    CHECK(TextPage::scrolled(2, -4, 6, 10) == 0);
    CHECK(TextPage::canScroll(2, -4, 6, 10));
}

TEST_CASE("TextPage: Circle and Escape close it with the Cancel sound") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);
    page.menuVisible = true;
    feed(*g.gui, page, button(Button::Circle));
    CHECK(!page.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cancel});

    page.menuVisible = true;
    side.sounds.clear();
    feed(*g.gui, page, key(Key::Escape));
    CHECK(!page.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cancel});
}

TEST_CASE("TextPage: buttons that mean nothing to a page do nothing") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);
    page.menuVisible = true;
    for (Button b :
         {Button::Cross, Button::Triangle, Button::Square, Button::Start, Button::Select, Button::L1, Button::R1})
        feed(*g.gui, page, button(b));
    feed(*g.gui, page, key(Key::Backspace));
    CHECK(page.menuVisible);
    CHECK(page.firstLine() == 0);
    CHECK(side.sounds.empty());
}

TEST_CASE("TextPage: the d-pad and the arrows scroll a line, L2/R2 and Page Up/Down a page") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);

    feed(*g.gui, page, dpad(true, Button::DpadDown));
    CHECK(page.firstLine() == 1);
    feed(*g.gui, page, dpad(false, Button::DpadDown)); // the release does not scroll
    CHECK(page.firstLine() == 1);
    feed(*g.gui, page, dpad(true, Button::DpadUp));
    CHECK(page.firstLine() == 0);
    feed(*g.gui, page, dpad(false, Button::DpadUp));

    feed(*g.gui, page, button(Button::R2));
    CHECK(page.firstLine() == 4);
    feed(*g.gui, page, key(Key::PageDown));
    CHECK(page.firstLine() == 8);
    feed(*g.gui, page, key(Key::Down)); // lines 8 and 9 show: the end, nothing below
    CHECK(page.firstLine() == 8);
    feed(*g.gui, page, button(Button::L2));
    CHECK(page.firstLine() == 4);
    feed(*g.gui, page, key(Key::PageUp));
    CHECK(page.firstLine() == 0);
    feed(*g.gui, page, key(Key::Down));
    feed(*g.gui, page, key(Key::Up));
    CHECK(page.firstLine() == 0);
}

TEST_CASE("TextPage: the Cursor sound plays only when the page moved") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);
    feed(*g.gui, page, key(Key::Up)); // already at the top
    CHECK(side.sounds.empty());
    feed(*g.gui, page, key(Key::Down));
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});

    // to the end: from line 1 two pages down show lines 9 and the end, nothing more below
    feed(*g.gui, page, key(Key::PageDown));
    feed(*g.gui, page, key(Key::PageDown));
    CHECK(page.firstLine() == 9);
    side.sounds.clear();
    feed(*g.gui, page, key(Key::Down));
    CHECK(side.sounds.empty());
}
