//
// Input's virtual pads (the DebugDriver's `@n ...` commands) and SIGTERM, through a real GuiBase and SDL. Like
// test_input_flush, a case skips itself where no renderer can be made; the pad cases also skip on an SDL older
// than 2.24 (Input::virtualPadsSupported) - the console's.
//
#include "doctest/doctest.h"

#include "ableem/ui/gui_base.h"
#include "ableem/ui/input.h"
#include "ableem/ui/pad_script.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace ableem;

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
            gui = make_unique<GuiBase>("ab_core_test_virtual_pads", 320, 240);
        } catch (const exception &e) {
            MESSAGE("test_virtual_pads: skipping - no usable renderer in this environment (" << e.what() << ")");
        }
    }

    bool available() const { return gui != nullptr; }
    Input &input() { return gui->input(); }
};

// polls for up to ms, collecting every event type seen
vector<Event> pollFor(Input &input, int ms) {
    vector<Event> seen;
    const auto end = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (chrono::steady_clock::now() < end) {
        Event e;
        while (input.poll(e)) {
            if (e.type != Event::Type::None)
                seen.push_back(e);
        }
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    return seen;
}

bool has(const vector<Event> &v, Event::Type t, Button b = Button::None) {
    for (const Event &e : v) {
        if (e.type == t && (b == Button::None || e.button == b))
            return true;
    }
    string seen;
    for (const Event &e : v)
        seen += to_string(static_cast<int>(e.type)) + "/" + to_string(static_cast<int>(e.button)) + " ";
    MESSAGE("seen: " << seen);
    return false;
}

} // namespace

TEST_CASE("a virtual x360 is a game controller: plugged in, its A is a Cross, pulled out") {
    MaybeGui mg;
    if (!mg.available())
        return;
    if (!Input::virtualPadsSupported()) {
        MESSAGE("test_virtual_pads: skipping - SDL older than 2.24");
        return;
    }
    Input &in = mg.input();
    in.probePads();
    pollFor(in, 50);
    const int before = in.activePadCount();

    PadScript::Profile x360;
    REQUIRE(PadScript::findProfile("x360", x360));
    REQUIRE(in.plugVirtualPad(0, PadScript::specFor(x360)));
    CHECK(in.virtualPadPlugged(0));
    vector<Event> seen = pollFor(in, 100);
    CHECK(has(seen, Event::Type::PadAdded));
    CHECK(in.activePadCount() == before + 1);

    REQUIRE(in.setVirtualPadControl(0, 0, true)); // SDL_CONTROLLER_BUTTON_A
    seen = pollFor(in, 100);
    CHECK(has(seen, Event::Type::ButtonDown, Button::Cross));
    REQUIRE(in.setVirtualPadControl(0, 0, false));
    seen = pollFor(in, 100);
    CHECK(has(seen, Event::Type::ButtonUp, Button::Cross));

    // the d-pad buttons arrive as the d-pad
    REQUIRE(in.setVirtualPadControl(0, 12, true)); // SDL_CONTROLLER_BUTTON_DPAD_DOWN
    seen = pollFor(in, 100);
    CHECK(has(seen, Event::Type::DpadDown));
    CHECK(in.dpadDown());
    in.setVirtualPadControl(0, 12, false);
    pollFor(in, 50);

    // a game launch takes the pads away and brings them back - the virtual one too
    in.flushPads();
    in.probePads();
    pollFor(in, 100);
    CHECK(in.activePadCount() == before + 1);

    REQUIRE(in.unplugVirtualPad(0));
    CHECK_FALSE(in.virtualPadPlugged(0));
    seen = pollFor(in, 100);
    CHECK(has(seen, Event::Type::PadRemoved));
    CHECK(in.activePadCount() == before);
}

TEST_CASE("the generic virtual pad has no mapping: a raw joystick, not a game controller") {
    MaybeGui mg;
    if (!mg.available() || !Input::virtualPadsSupported())
        return;
    Input &in = mg.input();
    in.probePads();
    pollFor(in, 50);
    const int pads = in.activePadCount();
    const int joysticks = in.joystickCount();
    PadScript::Profile generic;
    REQUIRE(PadScript::findProfile("generic", generic));
    REQUIRE(in.plugVirtualPad(1, PadScript::specFor(generic)));
    pollFor(in, 100);
    CHECK(in.joystickCount() == joysticks + 1);
    CHECK(in.activePadCount() == pads); // not a game controller: not one of Input's pads
    CHECK(in.unplugVirtualPad(1));
    pollFor(in, 50);
}

TEST_CASE("the virtual pads work from another thread (the DebugDriver's) while this one polls") {
    MaybeGui mg;
    if (!mg.available() || !Input::virtualPadsSupported())
        return;
    Input &in = mg.input();
    in.probePads();
    PadScript::Profile ds4;
    REQUIRE(PadScript::findProfile("ds4", ds4));
    bool plugged = false, pressed = false;
    thread driver([&]() {
        plugged = in.plugVirtualPad(2, PadScript::specFor(ds4));
        this_thread::sleep_for(chrono::milliseconds(100));
        pressed = in.setVirtualPadControl(2, 1, true); // B: a Circle
    });
    vector<Event> seen = pollFor(in, 400);
    driver.join();
    CHECK(plugged);
    CHECK(pressed);
    CHECK(has(seen, Event::Type::PadAdded));
    CHECK(has(seen, Event::Type::ButtonDown, Button::Circle));
    in.unplugVirtualPad(2);
    pollFor(in, 50);
}

#ifndef _WIN32
TEST_CASE("SIGTERM is a quit request: every screen's loop sees Quit, and quitRequested() says so") {
    MaybeGui mg;
    if (!mg.available())
        return;
    Input &in = mg.input();
    CHECK_FALSE(in.quitRequested());
    raise(SIGTERM); // Input's own handler: the process goes on
    Event e;
    bool quit = false;
    for (int i = 0; i < 4 && !quit; i++) {
        if (in.poll(e) && e.type == Event::Type::Quit)
            quit = true;
    }
    CHECK(quit);
    CHECK(in.quitRequested());
}
#endif
