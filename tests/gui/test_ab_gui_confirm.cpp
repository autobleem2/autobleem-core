//
// abgui::Confirm (G3j of docs/ab-gui-plan.md): the dialog's layout (pure), and the events - Cross and Enter answer yes
// with the Cursor sound, Circle and Escape no with Cancel, the d-pad and the other buttons nothing, the window's Quit
// closes it with the answer unchanged - through a headless GuiBase (those cases skip themselves without a renderer,
// like test_ab_gui_screen).
//
#include "doctest/doctest.h"

#include <ab_gui/actions.h>
#include <ab_gui/confirm.h>
#include <ab_gui/context.h>

#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::Confirm;
using abgui::Context;
using abgui::Style;
using abgui::UiSound;
using ableem::Button;
using ableem::Event;
using ableem::GuiBase;
using ableem::Key;

namespace {

struct MaybeGui {
    unique_ptr<GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<GuiBase>("ab_gui_test_confirm", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_confirm: skipping - no usable renderer here (" << why << ")");
        }
        if (gui)
            gui->input().flushEvents();
    }

    bool available() const { return gui != nullptr; }
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

Event dpad(Button b) {
    Event e;
    e.type = Event::Type::DpadDown;
    e.button = b;
    return e;
}

Event key(Key k) {
    Event e;
    e.type = Event::Type::KeyDown;
    e.key = k;
    return e;
}

// the event through the Input as the loop reads it, then to the dialog
void feed(GuiBase &gui, Confirm &dialog, const Event &event) {
    gui.input().inject(event);
    Event polled;
    REQUIRE(gui.input().poll(polled));
    dialog.handle(polled);
}

} // namespace

TEST_CASE("Confirm::textWidth leaves the rows' inset and 8 px on each side of the 800 px dialog") {
    const Style style;
    CHECK(Confirm::Width == 800);
    CHECK(Confirm::textWidth(style) == 800 - 2 * (24 + 8));
    Style wide;
    wide.rowInset = 40;
    CHECK(Confirm::textWidth(wide) == 800 - 2 * 48);
}

TEST_CASE("Confirm::panelRect: header, gap, text, gap and footer tall, centred on the canvas") {
    const Style style; // header 74, footer 54
    const ableem::Rect one = Confirm::panelRect(style, 30, 1920, 1080);
    CHECK(one.w == 800);
    CHECK(one.h == 74 + 12 + 30 + 24 + 54);
    CHECK(one.x == (1920 - 800) / 2);
    CHECK(one.y == (1080 - one.h) / 2);

    // a taller question makes the dialog taller by as much
    const ableem::Rect three = Confirm::panelRect(style, 90, 1920, 1080);
    CHECK(three.h == one.h + 60);

    // other canvas, other metrics
    Style other;
    other.headerHeight = 60;
    other.footerHeight = 40;
    const ableem::Rect small = Confirm::panelRect(other, 0, 1280, 720);
    CHECK(small.h == 60 + 12 + 0 + 24 + 40);
    CHECK(small.x == 240);
    CHECK(small.y == (720 - small.h) / 2);
}

TEST_CASE("Confirm: Cross says yes with the Cursor sound") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Confirm dialog(*g.gui, side.ctx);
    dialog.menuVisible = true;
    feed(*g.gui, dialog, button(Button::Cross));
    CHECK(dialog.result);
    CHECK(!dialog.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});
}

TEST_CASE("Confirm: Circle says no with the Cancel sound, over an earlier yes") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Confirm dialog(*g.gui, side.ctx);
    dialog.result = true;
    dialog.menuVisible = true;
    feed(*g.gui, dialog, button(Button::Circle));
    CHECK(!dialog.result);
    CHECK(!dialog.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cancel});
}

TEST_CASE("Confirm: the d-pad, the other buttons and their releases do nothing") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Confirm dialog(*g.gui, side.ctx);
    dialog.menuVisible = true;
    for (Button b : {Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight})
        feed(*g.gui, dialog, dpad(b));
    for (Button b : {Button::Triangle, Button::Square, Button::L1, Button::R1, Button::L2, Button::R2, Button::Start,
                     Button::Select})
        feed(*g.gui, dialog, button(b));
    Event up;
    up.type = Event::Type::ButtonUp;
    up.button = Button::Cross;
    feed(*g.gui, dialog, up);
    CHECK(dialog.menuVisible);
    CHECK(!dialog.result);
    CHECK(side.sounds.empty());
}

TEST_CASE("Confirm: Enter says yes and Escape no, as keys") {
    MaybeGui g;
    if (!g.available())
        return;
    // the keys as keys (the keyboard-as-pad off, as a typing screen has it)
    g.gui->input().setKeyboardAsPad(false);
    Side side(*g.gui);
    Confirm dialog(*g.gui, side.ctx);
    dialog.menuVisible = true;
    feed(*g.gui, dialog, key(Key::Return));
    CHECK(dialog.result);
    CHECK(!dialog.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});

    dialog.menuVisible = true;
    side.sounds.clear();
    feed(*g.gui, dialog, key(Key::Escape));
    CHECK(!dialog.result);
    CHECK(!dialog.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cancel});
}

TEST_CASE("Confirm: no other key answers") {
    MaybeGui g;
    if (!g.available())
        return;
    g.gui->input().setKeyboardAsPad(false);
    Side side(*g.gui);
    Confirm dialog(*g.gui, side.ctx);
    dialog.menuVisible = true;
    for (Key k : {Key::Down, Key::Up, Key::Left, Key::Right, Key::PageDown, Key::PageUp, Key::Backspace, Key::Tab})
        feed(*g.gui, dialog, key(k));
    CHECK(dialog.menuVisible);
    CHECK(!dialog.result);
    CHECK(side.sounds.empty());
}

TEST_CASE("Confirm: with the keyboard-as-pad on (the default) Enter and Escape reach it as Cross and Circle") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Confirm dialog(*g.gui, side.ctx);
    dialog.menuVisible = true;
    g.gui->input().inject(key(Key::Return));
    Event polled;
    REQUIRE(g.gui->input().poll(polled));
    dialog.handle(polled);
    CHECK(dialog.result);
    CHECK(!dialog.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});

    // Backspace is Circle with the keyboard-as-pad on
    dialog.menuVisible = true;
    side.sounds.clear();
    g.gui->input().inject(key(Key::Backspace));
    REQUIRE(g.gui->input().poll(polled));
    dialog.handle(polled);
    CHECK(!dialog.result);
    CHECK(!dialog.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cancel});
}

TEST_CASE("Confirm: Confirm and Back swapped, Circle says yes") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    side.ctx.actions.setSwapConfirmBack(true);
    Confirm dialog(*g.gui, side.ctx);
    dialog.menuVisible = true;
    feed(*g.gui, dialog, button(Button::Circle));
    CHECK(dialog.result);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});
}

TEST_CASE("Confirm::loop: the window's Quit closes it and leaves the answer as it was") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Confirm dialog(*g.gui, side.ctx);
    Event quit;
    quit.type = Event::Type::Quit;
    g.gui->input().inject(quit);
    dialog.loop();
    CHECK(!dialog.menuVisible);
    CHECK(!dialog.result);
    CHECK(side.sounds.empty());
}

TEST_CASE("Confirm::loop: a press in the queue answers") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Confirm dialog(*g.gui, side.ctx);
    g.gui->input().inject(button(Button::Cross));
    dialog.loop();
    CHECK(dialog.result);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});
}
