//
// Input::flushInputEvents (CONSOLE-11): a game started by itself right after Options, because the busy
// spinner drawn while settings were reloaded (Gui::beginBusy/endBusy, see gui.cpp's comment) never reads
// input, so a Cross pressed because the spinner looked stuck stayed queued and was handled as a real press
// the moment the launcher polled again. This drives a real ableem::Input (owned by a real GuiBase) with
// raw SDL events to prove: an input event queued during the busy period does not survive it, a device
// hotplug event does, and a button pressed after the flush is delivered normally.
//
// Building a real GuiBase needs a working SDL renderer (SDL_RENDERER_ACCELERATED - see gui_base.cpp /
// renderer.cpp), which this suite's actual target - a real dev machine, where CONSOLE-11 was found and is
// verified - has, but a display-less CI container may not. AB_HEADLESS=1 keeps the window off-screen either
// way (Platform's own policy); when the renderer still cannot be created at all, every TEST_CASE here
// reports why and skips itself rather than failing the whole suite over an environment that cannot run it.
//
#include "doctest/doctest.h"

#include "ableem/ui/gui_base.h"
#include <SDL2/SDL.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

using namespace std;
using ableem::Button;
using ableem::Event;
using ableem::GuiBase;
using ableem::Input;

namespace {

// SDL's own name for the button PSC-style code maps to Button::Cross - see psc_event_filter.h
#ifndef SDL_BTN_CROSS
#define SDL_BTN_CROSS SDL_CONTROLLER_BUTTON_A
#endif

// See the file's opening comment: available() is false when this environment cannot make a real GuiBase
// (no display/renderer), and every TEST_CASE below skips itself in that case instead of failing.
struct MaybeGui {
    unique_ptr<GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<GuiBase>("ab_core_test_input_flush", 320, 240);
        } catch (const exception &e) {
            MESSAGE("test_input_flush: skipping - no usable renderer in this environment (" << e.what() << ")");
        }
    }

    bool available() const { return gui != nullptr; }
    Input &input() { return gui->input(); }
};

// pushes a raw SDL controller-button event straight onto SDL's own queue, the way a real pad's press
// arrives - poll() only looks at e.cbutton.button, so no real controller is needed
void pushButton(Uint32 sdlEventType, int sdlButton) {
    SDL_Event e{};
    e.type = sdlEventType;
    e.cbutton.button = static_cast<Uint8>(sdlButton);
    e.cbutton.state = sdlEventType == SDL_CONTROLLERBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    SDL_PushEvent(&e);
}

// drains poll() until it is empty, collecting every event type seen (Quit aside, poll() never blocks)
vector<Event::Type> drain(Input &input) {
    vector<Event::Type> seen;
    Event e;
    while (input.poll(e)) {
        if (e.type != Event::Type::None)
            seen.push_back(e.type);
    }
    return seen;
}

bool contains(const vector<Event::Type> &v, Event::Type t) {
    for (Event::Type x : v)
        if (x == t)
            return true;
    return false;
}

} // namespace

TEST_CASE("flushInputEvents: a pad button pushed during the busy period is not delivered after it") {
    MaybeGui mg;
    if (!mg.available())
        return;

    mg.input().flushEvents(); // start from an empty queue, whatever SDL queued while the window came up

    // "during beginBusy...endBusy": the button lands on the queue while drawBusyFrame() is looping and
    // reading nothing (see gui.cpp) - simulated here by simply not polling before the flush
    pushButton(SDL_CONTROLLERBUTTONDOWN, SDL_BTN_CROSS);
    pushButton(SDL_CONTROLLERBUTTONUP, SDL_BTN_CROSS);

    mg.input().flushInputEvents(); // what Gui::endBusy() now calls on the busy -> not busy transition

    vector<Event::Type> seen = drain(mg.input());
    CHECK_FALSE(contains(seen, Event::Type::ButtonDown));
    CHECK_FALSE(contains(seen, Event::Type::ButtonUp));
}

TEST_CASE("flushInputEvents: a device-added event queued during the busy period is still delivered") {
    MaybeGui mg;
    if (!mg.available())
        return;

    mg.input().flushEvents();

    SDL_Event added{};
    added.type = SDL_JOYDEVICEADDED;
    added.jdevice.which = 99; // no real joystick at that index - registerPad() just no-ops on it
    SDL_PushEvent(&added);

    mg.input().flushInputEvents();

    vector<Event::Type> seen = drain(mg.input());
    CHECK(contains(seen, Event::Type::PadAdded));
}

TEST_CASE("flushInputEvents: a button pressed after the flush is delivered normally") {
    MaybeGui mg;
    if (!mg.available())
        return;

    mg.input().flushEvents();
    mg.input().flushInputEvents(); // as if a busy job had just ended, with nothing queued

    pushButton(SDL_CONTROLLERBUTTONDOWN, SDL_BTN_CROSS);
    pushButton(SDL_CONTROLLERBUTTONUP, SDL_BTN_CROSS);

    vector<Event::Type> seen = drain(mg.input());
    CHECK(contains(seen, Event::Type::ButtonDown));
    CHECK(contains(seen, Event::Type::ButtonUp));
}

TEST_CASE("flushInputEvents: a pending Quit survives the flush") {
    MaybeGui mg;
    if (!mg.available())
        return;

    mg.input().flushEvents();

    SDL_Event quit{};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);

    mg.input().flushInputEvents();

    vector<Event::Type> seen = drain(mg.input());
    CHECK(contains(seen, Event::Type::Quit));
}
