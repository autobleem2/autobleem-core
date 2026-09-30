//
// abgui::ActionMenu (G3k of docs/ab-gui-plan.md): the row layout (heights, how many fit, the scroll), the cursor over
// headings and disabled rows, the wrap (pure), and the events - Cross picks with the Cursor sound, Circle leaves with
// Cancel, the d-pad steps by its live state and repeats while held at the shared pace, the window's Quit leaves with -1
// - through a headless GuiBase (those cases skip themselves without a renderer, like test_ab_gui_screen).
//
#include "doctest/doctest.h"

#include <ab_gui/action_menu.h>
#include <ab_gui/actions.h>
#include <ab_gui/context.h>

#include <ableem/ui/gui_base.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::ActionMenu;
using abgui::Context;
using abgui::Style;
using abgui::UiSound;
using ableem::Button;
using ableem::Event;
using ableem::GuiBase;
using ableem::Key;

namespace {

typedef vector<ActionMenu::Item> Items;

// n plain items
Items plain(int n) {
    Items items;
    for (int i = 0; i < n; i++)
        items.push_back({"item " + to_string(i), "about " + to_string(i), false, false});
    return items;
}

ActionMenu::Item heading(const string &title) {
    return {title, "", true, false};
}

ActionMenu::Item disabled(const string &title) {
    return {title, "switched off", false, true};
}

// a style with 10 px rows
Style tenPixelRows() {
    Style style;
    style.rowHeight = 10;
    return style;
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
            gui = make_unique<GuiBase>("ab_gui_test_action_menu", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_action_menu: skipping - no usable renderer here (" << why << ")");
        }
        if (gui)
            gui->input().flushEvents();
    }

    bool available() const { return gui != nullptr; }
};

// a Context whose panel has no header or footer and whose rows are a third of the canvas tall: room for exactly three
// item rows, whatever the canvas is
struct Side {
    Context ctx;
    vector<UiSound> sounds;
    explicit Side(GuiBase &gui) : ctx(gui.renderer(), gui.input(), gui.platform()) {
        ctx.soundPlayer = [this](UiSound s) { sounds.push_back(s); };
        Style style;
        style.margin = 0;
        style.headerHeight = 0;
        style.footerHeight = 0;
        style.rowHeight = gui.renderer().height() / 3;
        ctx.styleProvider = [style]() { return style; };
    }
};

// a menu that draws nothing and shows what its frames saw
struct Menu : ActionMenu {
    Menu(GuiBase &gui, Context &ctx) : ActionMenu(gui, ctx) {}
    vector<int> seen;            // `selected` at each frame
    function<void(int)> onFrame; // called with the frame number (from 1)
    void draw() override {
        seen.push_back(selected);
        if (onFrame)
            onFrame(static_cast<int>(seen.size()));
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

// the event through the Input as the loop reads it (the d-pad state follows), then to the menu
void feed(GuiBase &gui, ActionMenu &menu, const Event &event) {
    gui.input().inject(event);
    Event polled;
    REQUIRE(gui.input().poll(polled));
    menu.handle(polled);
}

} // namespace

TEST_CASE("ActionMenu::rowHeight: an item is the style's row tall, a heading a thin band") {
    const Style style = tenPixelRows();
    CHECK(ActionMenu::rowHeight(style, plain(1)[0]) == 10);
    CHECK(ActionMenu::rowHeight(style, heading("Library")) == 24);
    CHECK(ActionMenu::rowHeight(style, disabled("Network")) == 10);
    CHECK(int(ActionMenu::Width) == 800);
    CHECK(ActionMenu::rowHeight(Style(), plain(1)[0]) == 60);
}

TEST_CASE("ActionMenu::roomForRows: the canvas less the margins, the header and the footer") {
    const Style style;
    CHECK(ActionMenu::roomForRows(style, 1080) == 1080 - 2 * 40 - 74 - 54);
    Style other;
    other.margin = 10;
    other.headerHeight = 20;
    other.footerHeight = 30;
    CHECK(ActionMenu::roomForRows(other, 500) == 500 - 20 - 20 - 30);
}

TEST_CASE("ActionMenu::visibleCount: as many rows as fit from the first, at least one") {
    const Style style = tenPixelRows();
    const Items items = plain(10);
    CHECK(ActionMenu::visibleCount(style, items, 0, 35) == 3);
    CHECK(ActionMenu::visibleCount(style, items, 0, 30) == 3);
    CHECK(ActionMenu::visibleCount(style, items, 8, 100) == 2); // only two left
    CHECK(ActionMenu::visibleCount(style, items, 0, 4) == 1);   // taller than the room: still one
    CHECK(ActionMenu::visibleCount(style, items, 0, 0) == 1);
    CHECK(ActionMenu::visibleCount(style, Items(), 0, 100) == 1);

    // headings are thinner: 10 + 24 + 10 fits 50
    Items mixed{plain(1)[0], heading("H"), plain(2)[1], plain(3)[2]};
    CHECK(ActionMenu::visibleCount(style, mixed, 0, 50) == 3);
    CHECK(ActionMenu::visibleCount(style, mixed, 0, 44) == 3);
    CHECK(ActionMenu::visibleCount(style, mixed, 0, 43) == 2);
}

TEST_CASE("ActionMenu: over plain items the cursor and the scroll are the old menu's, step for step") {
    const Style style = tenPixelRows();
    for (int count = 1; count <= 12; count++) {
        for (int fit = 1; fit <= 6; fit++) {
            const Items items = plain(count);
            const int room = fit * 10 + 5;
            int oldSelected = 0, oldFirst = 0;
            int selected = 0, first = 0;
            // the old menu: rows = min(count, room / row), one step with a wrap, scrolled into view
            auto oldMove = [&](int step) {
                oldSelected = (oldSelected + step + count) % count;
                const int rows = max(1, min(count, room / 10));
                if (oldSelected < oldFirst)
                    oldFirst = oldSelected;
                else if (oldSelected >= oldFirst + rows)
                    oldFirst = oldSelected - rows + 1;
            };
            for (int n = 0; n < 3 * count + 2; n++) {
                for (int step : {1, 1, -1, 1, 1, 1, -1, -1}) {
                    oldMove(step);
                    selected = ActionMenu::moved(items, selected, step, true);
                    first = ActionMenu::scrolledTo(style, items, selected, first, room);
                    REQUIRE(selected == oldSelected);
                    REQUIRE(first == oldFirst);
                }
            }
        }
    }
}

TEST_CASE("ActionMenu::moved: headings and disabled rows are skipped, at the ends too") {
    Items items{heading("Top"), plain(1)[0], heading("Mid"), disabled("Off"), plain(2)[1], plain(3)[2], heading("End")};
    // selectable: 1, 4, 5
    CHECK(ActionMenu::moved(items, 1, 1, true) == 4);
    CHECK(ActionMenu::moved(items, 4, 1, true) == 5);
    CHECK(ActionMenu::moved(items, 5, 1, true) == 1); // past the heading at the end, round to the first
    CHECK(ActionMenu::moved(items, 1, -1, true) == 5);
    CHECK(ActionMenu::moved(items, 4, -1, true) == 1);
    CHECK(ActionMenu::moved(items, 0, 1, true) == 1); // from a heading
    // without the wrap the cursor stops at the ends
    CHECK(ActionMenu::moved(items, 5, 1, false) == 5);
    CHECK(ActionMenu::moved(items, 1, -1, false) == 1);
    CHECK(ActionMenu::moved(items, 1, 1, false) == 4);
    CHECK(ActionMenu::moved(items, 5, -1, false) == 4);
    // nothing to land on, or nothing at all
    CHECK(ActionMenu::moved(Items{heading("A"), disabled("B")}, 0, 1, true) == 0);
    CHECK(ActionMenu::moved(Items(), 0, 1, true) == 0);
    // one selectable row stays put
    CHECK(ActionMenu::moved(Items{plain(1)[0]}, 0, 1, true) == 0);
    CHECK(ActionMenu::moved(Items{heading("A"), plain(1)[0]}, 1, -1, true) == 1);
}

TEST_CASE("ActionMenu::scrolledTo: up to a row brings the heading above it along, down goes a row at a time") {
    const Style style = tenPixelRows();
    Items items{plain(1)[0], plain(2)[1], heading("Library"), plain(3)[2], plain(4)[3], plain(5)[4]};
    // room for 50: rows of 10, 10, 24 fit (44), a fourth (54) does not
    CHECK(ActionMenu::visibleCount(style, items, 0, 50) == 3);
    // the cursor down to row 3 (below the heading): the first row goes, one at a time
    CHECK(ActionMenu::scrolledTo(style, items, 3, 0, 50) == 1);
    CHECK(ActionMenu::scrolledTo(style, items, 5, 0, 50) == 3);
    // scrolled to row 3 (first = 3), back up to row 3 itself: unchanged; up to row 1: the first is 1
    CHECK(ActionMenu::scrolledTo(style, items, 3, 3, 50) == 3);
    CHECK(ActionMenu::scrolledTo(style, items, 1, 3, 50) == 1);
    // scrolled up to the row below the heading, the heading comes along
    CHECK(ActionMenu::scrolledTo(style, items, 3, 4, 50) == 2);
    // already visible: unchanged
    CHECK(ActionMenu::scrolledTo(style, items, 1, 0, 50) == 0);
}

TEST_CASE("ActionMenu::open: clamps a kept selection, lands on a row that can be picked, scrolls it into view") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.items = plain(8);
    menu.selected = 20;
    menu.result = 3;
    menu.open();
    CHECK(menu.selected == 7);
    CHECK(menu.result == -1);
    CHECK(menu.firstVisible() == 5); // three rows fit: 5, 6, 7

    menu.selected = -4;
    menu.open();
    CHECK(menu.selected == 0);
    CHECK(menu.firstVisible() == 0);

    menu.items = {heading("A"), disabled("B"), plain(1)[0]};
    menu.selected = 0;
    menu.open();
    CHECK(menu.selected == 2);

    menu.items.clear();
    menu.open();
    CHECK(menu.selected == 0);
    CHECK(menu.firstVisible() == 0);
}

TEST_CASE("ActionMenu: Cross picks the row with the Cursor sound, Circle leaves with Cancel") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.items = plain(4);
    menu.open();
    menu.menuVisible = true;
    feed(*g.gui, menu, dpad(true, Button::DpadDown));
    feed(*g.gui, menu, dpad(false, Button::DpadDown));
    side.sounds.clear();
    feed(*g.gui, menu, button(Button::Cross));
    CHECK(menu.result == 1);
    CHECK(!menu.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});

    menu.result = 1;
    menu.menuVisible = true;
    side.sounds.clear();
    feed(*g.gui, menu, button(Button::Circle));
    CHECK(menu.result == -1);
    CHECK(!menu.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cancel});
}

TEST_CASE("ActionMenu: Cross on an empty menu does nothing, the other buttons neither") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.open();
    menu.menuVisible = true;
    feed(*g.gui, menu, button(Button::Cross));
    CHECK(menu.menuVisible);
    CHECK(menu.result == -1);
    CHECK(side.sounds.empty());

    menu.items = plain(3);
    menu.open();
    for (Button b : {Button::Triangle, Button::Square, Button::Start, Button::Select, Button::L1, Button::R1,
                     Button::L2, Button::R2})
        feed(*g.gui, menu, button(b));
    CHECK(menu.menuVisible);
    CHECK(menu.selected == 0);
    CHECK(side.sounds.empty());
}

TEST_CASE("ActionMenu: the d-pad steps by its live state, up first, with the Cursor sound, wrapping") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.items = plain(4);
    menu.open();

    feed(*g.gui, menu, dpad(true, Button::DpadUp)); // the first row up: the last
    CHECK(menu.selected == 3);
    feed(*g.gui, menu, dpad(false, Button::DpadUp)); // the release reads the centred state: nothing
    CHECK(menu.selected == 3);
    feed(*g.gui, menu, dpad(true, Button::DpadDown));
    CHECK(menu.selected == 0);
    feed(*g.gui, menu, dpad(false, Button::DpadDown));
    CHECK(menu.selected == 0);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor, UiSound::Cursor});

    // both held: up wins
    feed(*g.gui, menu, dpad(true, Button::DpadDown));
    feed(*g.gui, menu, dpad(true, Button::DpadUp));
    CHECK(menu.selected == 0); // down (+1), then up (-1)
    // ... and letting go of down while up is still held steps up again
    feed(*g.gui, menu, dpad(false, Button::DpadDown));
    CHECK(menu.selected == 3);
    feed(*g.gui, menu, dpad(false, Button::DpadUp));
    CHECK(menu.selected == 3);

    // Left and Right do nothing
    side.sounds.clear();
    feed(*g.gui, menu, dpad(true, Button::DpadLeft));
    feed(*g.gui, menu, dpad(true, Button::DpadRight));
    CHECK(menu.selected == 3);
    CHECK(side.sounds.empty());
}

TEST_CASE("ActionMenu: without the wrap the cursor stops at the ends; headings are never landed on or picked") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.items = {plain(1)[0], heading("Library"), plain(2)[1], disabled("Off")};
    menu.wrap = false;
    menu.open();
    feed(*g.gui, menu, dpad(true, Button::DpadUp));
    CHECK(menu.selected == 0);
    feed(*g.gui, menu, dpad(false, Button::DpadUp));
    feed(*g.gui, menu, dpad(true, Button::DpadDown));
    CHECK(menu.selected == 2); // past the heading
    feed(*g.gui, menu, dpad(false, Button::DpadDown));
    feed(*g.gui, menu, dpad(true, Button::DpadDown));
    CHECK(menu.selected == 2); // the disabled row is not landed on, and there is no wrap
    feed(*g.gui, menu, dpad(false, Button::DpadDown));
    menu.menuVisible = true;
    feed(*g.gui, menu, button(Button::Cross));
    CHECK(menu.result == 2);

    // a menu with nothing to pick: Cross does nothing
    menu.items = {heading("A"), disabled("B")};
    menu.open();
    menu.menuVisible = true;
    menu.result = -1;
    feed(*g.gui, menu, button(Button::Cross));
    CHECK(menu.menuVisible);
    CHECK(menu.result == -1);
}

TEST_CASE("ActionMenu: the keyboard's own keys do nothing when they reach it as keys") {
    MaybeGui g;
    if (!g.available())
        return;
    // the keys as keys (the keyboard-as-pad off, as a typing screen has it)
    g.gui->input().setKeyboardAsPad(false);
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.items = plain(4);
    menu.open();
    menu.menuVisible = true;
    for (Key k : {Key::Escape, Key::Down, Key::Up, Key::PageDown, Key::PageUp, Key::Return, Key::Backspace})
        feed(*g.gui, menu, key(k));
    CHECK(menu.menuVisible);
    CHECK(menu.selected == 0);
    CHECK(menu.result == -1);
    CHECK(side.sounds.empty());
}

TEST_CASE("ActionMenu: with the keyboard-as-pad on (the default) the arrows step, Enter picks, Backspace leaves") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.items = plain(4);
    menu.open();
    menu.menuVisible = true;
    feed(*g.gui, menu, key(Key::Down)); // Input makes it the d-pad's down
    CHECK(menu.selected == 1);
    feed(*g.gui, menu, key(Key::Return)); // ... and Enter Cross
    CHECK(menu.result == 1);
    CHECK(!menu.menuVisible);

    menu.menuVisible = true;
    feed(*g.gui, menu, key(Key::Backspace)); // ... and Backspace Circle
    CHECK(menu.result == -1);
    CHECK(!menu.menuVisible);
}

TEST_CASE("ActionMenu::loop: a held d-pad repeats at the shared pace, and the release ends it") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.items = plain(10);
    menu.open();
    // every frame takes 100 ms; the d-pad is pressed before the loop and let go at frame 8, Back at frame 9
    menu.onFrame = [&](int frame) {
        g.gui->platform().delay(100);
        if (frame == 8)
            g.gui->input().inject(dpad(false, Button::DpadDown));
        if (frame == 9)
            g.gui->input().inject(button(Button::Circle));
    };
    g.gui->input().inject(dpad(true, Button::DpadDown));
    menu.loop();
    REQUIRE(menu.seen.size() == 9);
    // frame 1 is before the press; the press steps once; the first repeat is 350 ms after it (frame 6), then every
    // 80 ms
    CHECK(vector<int>(menu.seen.begin(), menu.seen.begin() + 5) == vector<int>{0, 1, 1, 1, 1});
    CHECK(menu.seen[5] == 2);
    CHECK(menu.seen[6] == 3);
    // the release (queued during frame 8, read after that pass's repeat) stops it: nothing moves after frame 9
    CHECK(menu.seen[8] - menu.seen[7] <= 1);
    CHECK(menu.selected == menu.seen[8]);
    CHECK(menu.result == -1);
    CHECK(!menu.menuVisible);
}

TEST_CASE("ActionMenu::loop: the window's Quit leaves with -1, and a press behind it is still read") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Menu menu(*g.gui, side.ctx);
    menu.items = plain(3);
    menu.open();
    Event quit;
    quit.type = Event::Type::Quit;
    g.gui->input().inject(quit);
    menu.loop();
    CHECK(menu.result == -1);
    CHECK(!menu.menuVisible);

    // a Cross queued behind the Quit in the same batch still picks, as the old loop's switch went on
    menu.selected = 2;
    g.gui->input().inject(quit);
    g.gui->input().inject(button(Button::Cross));
    menu.loop();
    CHECK(menu.result == 2);
}
