//
// abgui::FactsPage (G3i of docs/ab-gui-plan.md): the rows of the sections, the paging rule, the value column, the
// elision and the refresh rule (pure), and the events - the d-pad by its live state (up, down, left, right), L1/R1 the
// first and the last row, L2/R2 a page, Circle closing unless the page's own onButton() takes it, the Cursor sound only
// when the page moved and Cancel when it could not - through a headless GuiBase (those cases skip themselves without a
// renderer, like test_ab_gui_screen).
//
#include "doctest/doctest.h"

#include <ab_gui/actions.h>
#include <ab_gui/context.h>
#include <ab_gui/facts_page.h>

#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::Context;
using abgui::FactsPage;
using abgui::FactsRow;
using abgui::FactsSection;
using abgui::UiSound;
using ableem::Button;
using ableem::Event;
using ableem::GuiBase;
using ableem::Key;

namespace {

// one column per character, so a text's width is its length
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
            gui = make_unique<GuiBase>("ab_gui_test_facts_page", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_facts_page: skipping - no usable renderer here (" << why << ")");
        }
        if (gui)
            gui->input().flushEvents();
    }

    bool available() const { return gui != nullptr; }
};

// two sections of four rows each: 10 lines (2 headings), a page of 4 rows
struct Page : FactsPage {
    Page(GuiBase &gui, Context &ctx) : FactsPage(gui, ctx) {
        open();
        rowsThatFit_ = 4;
    }
    int collected = 0;
    Button taken = Button::None; // the button onButton() takes
    vector<Button> offered;      // every button onButton() was asked about

    void draw() override {}
    string title() override { return "Facts"; }
    vector<FactsSection> collect() override {
        collected++;
        vector<FactsSection> sections;
        for (const char *name : {"One", "Two"}) {
            FactsSection section;
            section.title = name;
            for (int i = 0; i < 4; i++)
                section.rows.push_back({"label " + to_string(i), "value " + to_string(i)});
            sections.push_back(section);
        }
        return sections;
    }
    bool onButton(Button button) override {
        offered.push_back(button);
        return button == taken;
    }
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
}

} // namespace

TEST_CASE("FactsPage::linesOf gives each section a heading and its rows") {
    FactsSection a{"System", {{"OS", "Linux"}, {"Host", "box"}}};
    FactsSection b{"Empty", {}};
    const vector<FactsPage::Line> lines = FactsPage::linesOf({a, b});
    REQUIRE(lines.size() == 4);
    CHECK(lines[0].heading);
    CHECK(lines[0].label == "System");
    CHECK(!lines[1].heading);
    CHECK(lines[1].label == "OS");
    CHECK(lines[1].value == "Linux");
    CHECK(lines[2].label == "Host");
    CHECK(lines[3].heading);
    CHECK(lines[3].label == "Empty");
    CHECK(FactsPage::linesOf({}).empty());
}

TEST_CASE("FactsPage paging: the last top row, the clamped scroll, the counter") {
    CHECK(FactsPage::maxFirstVisible(10, 4) == 6);
    CHECK(FactsPage::maxFirstVisible(3, 4) == 0);
    CHECK(FactsPage::maxFirstVisible(0, 4) == 0);

    CHECK(FactsPage::scrolled(0, 1, 10, 4) == 1);
    CHECK(FactsPage::scrolled(0, -1, 10, 4) == 0);
    CHECK(FactsPage::scrolled(0, 4, 10, 4) == 4);
    CHECK(FactsPage::scrolled(4, 4, 10, 4) == 6); // a page down stops where the page is full
    CHECK(FactsPage::scrolled(6, 1, 10, 4) == 6);
    CHECK(FactsPage::scrolled(5, -10, 10, 4) == 0);
    CHECK(FactsPage::scrolled(0, 10, 10, 4) == 6);
    CHECK(FactsPage::scrolled(0, 1, 3, 4) == 0); // it all fits

    FactsPage::Counter c = FactsPage::counter(0, 10, 4);
    CHECK(c.page == 1);
    CHECK(c.pages == 3);
    c = FactsPage::counter(4, 10, 4);
    CHECK(c.page == 2);
    c = FactsPage::counter(6, 10, 4);
    CHECK(c.page == 2);
    CHECK(c.pages == 3);
    c = FactsPage::counter(8, 12, 4);
    CHECK(c.page == 3);
    CHECK(c.pages == 3);
}

TEST_CASE("FactsPage::refreshDue follows the interval, and the clock wrapping") {
    CHECK(!FactsPage::refreshDue(1999, 1000, 1000));
    CHECK(FactsPage::refreshDue(2000, 1000, 1000));
    CHECK(FactsPage::refreshDue(5000, 1000, 1000));
    CHECK(!FactsPage::refreshDue(1000, 1000, 1000 + 1));
    CHECK(FactsPage::refreshDue(1000, 1000, 0));
    // the tick counter wrapped past zero: 20 ms have passed
    CHECK(!FactsPage::refreshDue(10, 0xFFFFFFF6u, 1000));
    CHECK(FactsPage::refreshDue(1000, 0xFFFFFFF6u, 1000));
}

TEST_CASE("FactsPage::valueColumn keeps values short of the scroll markers") {
    const FactsPage::ValueColumn c = FactsPage::valueColumn(ableem::Rect(100, 50, 1000, 600), 24);
    CHECK(c.offset == 350);            // 35 % of the panel
    CHECK(c.right == 100 + 1000 - 44); // the row inset, the text's 8 and the markers' 12 short of the edge
    CHECK(c.width == c.right - 100 - 350);
}

TEST_CASE("elideText leaves what fits and cuts the rest to whole characters with dots") {
    CHECK(abgui::elideText("short", 10, chars) == "short");
    CHECK(abgui::elideText("exactly ten", 11, chars) == "exactly ten");
    CHECK(abgui::elideText("/media/games/some/long/path", 12, chars) == "/media/ga...");
    CHECK(abgui::elideText("abcdef", 2, chars) == "...");
    // a text that fits is returned whole, whatever its bytes
    CHECK(abgui::elideText("zaz\xC3\xB3\xC5\x82", 7, chars) == "zaz\xC3\xB3\xC5\x82");
}

TEST_CASE("FactsPage: open reads the rows from the top, refresh keeps the top row where it can") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);
    CHECK(page.collected == 1);
    CHECK(page.lines().size() == 10);
    CHECK(page.firstVisible() == 0);

    feed(*g.gui, page, button(Button::R1));
    CHECK(page.firstVisible() == 6);
    page.refresh();
    CHECK(page.collected == 2);
    CHECK(page.firstVisible() == 6);
    // a top row past the last one that fills a page is pulled back
    page.restore(page.lines(), 9, 4, 0);
    page.refresh();
    CHECK(page.firstVisible() == 6);
}

TEST_CASE("FactsPage: the refresh interval is read at the Context's clock") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    unsigned int now = 5000;
    side.ctx.clock = [&now]() { return now; };
    Page page(*g.gui, side.ctx);
    CHECK(page.lastRefresh() == 5000);
    now = 5999;
    CHECK(!FactsPage::refreshDue(side.ctx.ticks(), page.lastRefresh(), page.refreshInterval));
    now = 6000;
    CHECK(FactsPage::refreshDue(side.ctx.ticks(), page.lastRefresh(), page.refreshInterval));
    page.refresh();
    CHECK(page.lastRefresh() == 6000);
}

TEST_CASE("FactsPage: Circle closes it with the Cancel sound, unless the page takes it") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);
    page.menuVisible = true;
    feed(*g.gui, page, button(Button::Circle));
    CHECK(!page.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cancel});
    CHECK(page.offered == vector<Button>{Button::Circle}); // the page is asked first
    CHECK(page.collected == 1);

    // taken by the page: it stays, and the rows are read again
    page.menuVisible = true;
    page.taken = Button::Circle;
    side.sounds.clear();
    feed(*g.gui, page, button(Button::Circle));
    CHECK(page.menuVisible);
    CHECK(side.sounds.empty());
    CHECK(page.collected == 2);
}

TEST_CASE("FactsPage: a button of the page's own is offered, the paging buttons are not") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);
    page.menuVisible = true;
    page.taken = Button::Square;
    for (Button b : {Button::L1, Button::R1, Button::L2, Button::R2})
        feed(*g.gui, page, button(b));
    CHECK(page.offered.empty());
    feed(*g.gui, page, button(Button::Square));
    feed(*g.gui, page, button(Button::Select));
    feed(*g.gui, page, button(Button::None)); // no action is bound to it: it still reaches the page
    CHECK(page.offered == vector<Button>{Button::Square, Button::Select, Button::None});
    CHECK(page.collected == 2); // only Square was taken
    CHECK(page.menuVisible);
}

TEST_CASE("FactsPage: the d-pad scrolls a row or a page by its live state, up first") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);

    feed(*g.gui, page, dpad(true, Button::DpadDown));
    CHECK(page.firstVisible() == 1);
    feed(*g.gui, page, dpad(false, Button::DpadDown)); // the release reads the (now centred) state: nothing
    CHECK(page.firstVisible() == 1);
    feed(*g.gui, page, dpad(true, Button::DpadRight));
    CHECK(page.firstVisible() == 5);
    feed(*g.gui, page, dpad(false, Button::DpadRight));
    feed(*g.gui, page, dpad(true, Button::DpadLeft));
    CHECK(page.firstVisible() == 1);
    feed(*g.gui, page, dpad(false, Button::DpadLeft));
    feed(*g.gui, page, dpad(true, Button::DpadUp));
    CHECK(page.firstVisible() == 0);
    feed(*g.gui, page, dpad(false, Button::DpadUp));

    // up and down held together: up wins
    page.restore(page.lines(), 3, 4, 0);
    feed(*g.gui, page, dpad(true, Button::DpadDown));
    feed(*g.gui, page, dpad(true, Button::DpadUp));
    CHECK(page.firstVisible() == 3); // down (+1), then up (-1)
}

TEST_CASE("FactsPage: L1 and R1 go to the first and the last row, L2 and R2 page") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);

    feed(*g.gui, page, button(Button::R2));
    CHECK(page.firstVisible() == 4);
    feed(*g.gui, page, button(Button::R2));
    CHECK(page.firstVisible() == 6); // the last page is full
    feed(*g.gui, page, button(Button::L2));
    CHECK(page.firstVisible() == 2);
    feed(*g.gui, page, button(Button::R1));
    CHECK(page.firstVisible() == 6);
    feed(*g.gui, page, button(Button::L1));
    CHECK(page.firstVisible() == 0);
}

TEST_CASE("FactsPage: the Cursor sound plays when the page moved, Cancel when it could not") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);
    feed(*g.gui, page, button(Button::L1)); // already at the top
    CHECK(side.sounds == vector<UiSound>{UiSound::Cancel});
    side.sounds.clear();
    feed(*g.gui, page, button(Button::R2));
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});
    side.sounds.clear();
    feed(*g.gui, page, button(Button::R1));
    feed(*g.gui, page, button(Button::R1)); // already at the end
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor, UiSound::Cancel});
}

TEST_CASE("FactsPage: the keyboard's own keys do nothing here") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Page page(*g.gui, side.ctx);
    page.menuVisible = true;
    for (Key k : {Key::Escape, Key::Down, Key::Up, Key::PageDown, Key::PageUp, Key::Return, Key::Backspace})
        feed(*g.gui, page, key(k));
    CHECK(page.menuVisible);
    CHECK(page.firstVisible() == 0);
    CHECK(page.offered.empty());
    CHECK(side.sounds.empty());
}
