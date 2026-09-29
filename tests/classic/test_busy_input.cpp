//
// The busy rule (CONSOLE-13, the owner's rule of 2026-09-27 in autobleem-main's docs/decisions.md): while a
// spinner shows (Gui::beginBusy...endBusy) every pad and key input is ignored, and when the job ends the input
// starts clean - nothing held, no hold-repeat carried over - so nothing acts on an input made before or during
// the wait. It is kept in one place, Input::flushInputEvents() (what Gui::endBusy() calls) and poll(): every
// press a screen was handed and not yet the release of is released at the end of the job, and a release or a
// key repeat of anything not handed out as pressed never reaches a screen.
//
// The first half drives Input with raw SDL events, the second half three kinds of consumer the screens are
// built as - a hold that ends on the release event (GuiScreen's loop, Game Manager, the launcher's L1/R1), a
// repeat loop that runs until another pad event is pending (GuiMenuBase's fastForwardUntilAnotherEvent) and a
// hold that reads the live d-pad state once a frame (Options' and the game editor's holdTick) - each across a
// busy job. CONSOLE-11 (a press during the job never acts) and CONSOLE-12 (a hold waiting for its release
// stops) are test_input_flush.cpp's; they must stay green with this.
//
// Like test_input_flush.cpp this needs a real GuiBase (a renderer); it skips itself where there is none.
//
#include "doctest/doctest.h"

#include "ableem/ui/gui_base.h"
#include "gui/hold_repeat.h"
#include <SDL2/SDL.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using ableem::Button;
using ableem::Event;
using ableem::GuiBase;
using ableem::Input;
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
            gui = make_unique<GuiBase>("ab_core_test_busy_input", 320, 240);
        } catch (const exception &e) {
            MESSAGE("test_busy_input: skipping - no usable renderer in this environment (" << e.what() << ")");
        }
        if (gui)
            gui->input().flushEvents(); // whatever SDL queued while the window came up
    }

    bool available() const { return gui != nullptr; }
    Input &input() { return gui->input(); }
};

const Uint32 HatDown = SDL_LASTEVENT - 1; // psc_event_filter.h's SDL_CONTROLLERHATMOTIONDOWN
const Uint32 HatUp = SDL_LASTEVENT - 2;   // ... and SDL_CONTROLLERHATMOTIONUP

void pushButton(bool down, int sdlButton) {
    SDL_Event e{};
    e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
    e.cbutton.button = static_cast<Uint8>(sdlButton);
    e.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
    SDL_PushEvent(&e);
}

void pushHat(bool down, int sdlButton) {
    SDL_Event e{};
    e.type = down ? HatDown : HatUp;
    e.cbutton.button = static_cast<Uint8>(sdlButton);
    e.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
    SDL_PushEvent(&e);
}

void pushKey(bool down, SDL_Scancode scancode, SDL_Keycode sym, bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.scancode = scancode;
    e.key.keysym.sym = sym;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.repeat = repeat ? 1 : 0;
    SDL_PushEvent(&e);
}

void pushText(const char *text) {
    SDL_Event e{};
    e.type = SDL_TEXTINPUT;
    SDL_strlcpy(e.text.text, text, sizeof(e.text.text));
    SDL_PushEvent(&e);
}

vector<Event> drain(Input &input) {
    vector<Event> seen;
    Event e;
    while (input.poll(e)) {
        if (e.type != Event::Type::None)
            seen.push_back(e);
    }
    return seen;
}

int count(const vector<Event> &v, Event::Type t, Button b = Button::None) {
    int n = 0;
    for (const Event &e : v)
        if (e.type == t && (b == Button::None || e.button == b))
            n++;
    return n;
}

// the job: everything the pads do meanwhile is queued, nothing polls (drawBusyFrame), and Gui::endBusy()
// calls flushInputEvents() when it ends
template <typename F> void busyJob(Input &input, F whileBusy) {
    whileBusy();
    input.flushInputEvents();
}

} // namespace

//*******************************
// the input itself
//*******************************

TEST_CASE("busy rule: a button held through the job is released when it ends") {
    MaybeGui mg;
    if (!mg.available())
        return;
    pushButton(true, SDL_CONTROLLER_BUTTON_LEFTSHOULDER); // L1 - the launcher's letter jump repeats while held
    REQUIRE(count(drain(mg.input()), Event::Type::ButtonDown, Button::L1) == 1);

    busyJob(mg.input(), [] {}); // still held when the job ends, nothing queued

    CHECK(mg.input().padEventPending());
    vector<Event> after = drain(mg.input());
    CHECK(count(after, Event::Type::ButtonUp, Button::L1) == 1);

    // the real release, when the player lets go, is the release of a press already ended: nobody sees it
    pushButton(false, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    CHECK(count(drain(mg.input()), Event::Type::ButtonUp) == 0);
}

TEST_CASE("busy rule: a d-pad direction held through the job is released when it ends") {
    MaybeGui mg;
    if (!mg.available())
        return;
    pushHat(true, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    REQUIRE(count(drain(mg.input()), Event::Type::DpadDown, Button::DpadRight) == 1);

    busyJob(mg.input(), [] {});

    CHECK(mg.input().dpadCentered());
    CHECK(mg.input().padEventPending());
    vector<Event> after = drain(mg.input());
    CHECK(count(after, Event::Type::DpadUp, Button::DpadRight) == 1);
    CHECK(mg.input().dpadCentered());

    pushHat(false, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    CHECK(count(drain(mg.input()), Event::Type::DpadUp) == 0);
}

TEST_CASE("busy rule: released during the job, a hold is released exactly once (CONSOLE-12)") {
    MaybeGui mg;
    if (!mg.available())
        return;
    pushHat(true, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    REQUIRE(count(drain(mg.input()), Event::Type::DpadDown) == 1);

    busyJob(mg.input(), [] { pushHat(false, SDL_CONTROLLER_BUTTON_DPAD_RIGHT); });

    CHECK(mg.input().padEventPending());
    vector<Event> after = drain(mg.input());
    CHECK(count(after, Event::Type::DpadUp, Button::DpadRight) == 1);
    CHECK(count(after, Event::Type::DpadDown) == 0);
}

TEST_CASE("busy rule: a button pressed during the job and let go after it never reaches a screen") {
    MaybeGui mg;
    if (!mg.available())
        return;
    busyJob(mg.input(), [] { pushButton(true, SDL_CONTROLLER_BUTTON_A); }); // Cross, still down at the end
    CHECK(drain(mg.input()).empty());                                       // CONSOLE-11

    pushButton(false, SDL_CONTROLLER_BUTTON_A); // let go after the spinner went
    CHECK(drain(mg.input()).empty());

    // the next press is a press again
    pushButton(true, SDL_CONTROLLER_BUTTON_A);
    pushButton(false, SDL_CONTROLLER_BUTTON_A);
    vector<Event> next = drain(mg.input());
    CHECK(count(next, Event::Type::ButtonDown, Button::Cross) == 1);
    CHECK(count(next, Event::Type::ButtonUp, Button::Cross) == 1);
}

TEST_CASE("busy rule: a d-pad direction pushed during the job and let go after it never reaches a screen") {
    MaybeGui mg;
    if (!mg.available())
        return;
    busyJob(mg.input(), [] { pushHat(true, SDL_CONTROLLER_BUTTON_DPAD_DOWN); });
    CHECK(drain(mg.input()).empty());
    CHECK(mg.input().dpadCentered());

    pushHat(false, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    CHECK(drain(mg.input()).empty());
    CHECK(mg.input().dpadCentered());
}

TEST_CASE("busy rule: a key held through the job neither repeats nor types afterwards") {
    MaybeGui mg;
    if (!mg.available())
        return;
    // 'z' is in neither keyboard map: a plain key, as a terminal extension reads it
    pushKey(true, SDL_SCANCODE_Z, SDLK_z);
    pushText("z");
    vector<Event> before = drain(mg.input());
    REQUIRE(count(before, Event::Type::KeyDown) == 1);

    busyJob(mg.input(), [] {
        pushKey(true, SDL_SCANCODE_Z, SDLK_z, true); // the key's own repeats while the spinner shows
        pushText("z");
    });
    vector<Event> atEnd = drain(mg.input());
    CHECK(count(atEnd, Event::Type::KeyUp) == 1); // what the screen was handed is let go of
    CHECK(count(atEnd, Event::Type::KeyDown) == 0);

    // still held: the system's key repeat goes on
    pushKey(true, SDL_SCANCODE_Z, SDLK_z, true);
    pushText("z");
    pushKey(true, SDL_SCANCODE_Z, SDLK_z, true);
    pushText("z");
    pushKey(false, SDL_SCANCODE_Z, SDLK_z);
    vector<Event> after = drain(mg.input());
    CHECK(count(after, Event::Type::KeyDown) == 0);
    CHECK(count(after, Event::Type::TextInput) == 0);
    CHECK(count(after, Event::Type::KeyUp) == 0);

    // a new press types again
    pushKey(true, SDL_SCANCODE_Z, SDLK_z);
    pushText("z");
    vector<Event> next = drain(mg.input());
    CHECK(count(next, Event::Type::KeyDown) == 1);
    CHECK(count(next, Event::Type::TextInput) == 1);
}

TEST_CASE("busy rule: a key pressed during the job and let go after it never reaches a screen") {
    MaybeGui mg;
    if (!mg.available())
        return;
    busyJob(mg.input(), [] {
        pushKey(true, SDL_SCANCODE_Z, SDLK_z);
        pushText("z");
    });
    pushKey(true, SDL_SCANCODE_Z, SDLK_z, true);
    pushText("z");
    pushKey(false, SDL_SCANCODE_Z, SDLK_z);
    CHECK(drain(mg.input()).empty());
}

TEST_CASE("busy rule: outside a busy job, presses, repeats and releases are delivered as before") {
    MaybeGui mg;
    if (!mg.available())
        return;
    pushKey(true, SDL_SCANCODE_Z, SDLK_z);
    pushKey(true, SDL_SCANCODE_Z, SDLK_z, true);
    pushKey(false, SDL_SCANCODE_Z, SDLK_z);
    pushButton(true, SDL_CONTROLLER_BUTTON_B);
    pushButton(false, SDL_CONTROLLER_BUTTON_B);
    pushHat(true, SDL_CONTROLLER_BUTTON_DPAD_UP);
    pushHat(false, SDL_CONTROLLER_BUTTON_DPAD_UP);
    vector<Event> seen = drain(mg.input());
    CHECK(count(seen, Event::Type::KeyDown) == 2); // a held key's repeat reaches a screen that is not waiting
    CHECK(count(seen, Event::Type::KeyUp) == 1);
    CHECK(count(seen, Event::Type::ButtonDown, Button::Circle) == 1);
    CHECK(count(seen, Event::Type::ButtonUp, Button::Circle) == 1);
    CHECK(count(seen, Event::Type::DpadDown, Button::DpadUp) == 1);
    CHECK(count(seen, Event::Type::DpadUp, Button::DpadUp) == 1);
}

//*******************************
// the screens' kinds of hold
//*******************************
namespace {

// a hold that ends on its release event: GuiScreen's loop (doJoyCenter on a DpadUp with nothing held), the
// launcher's L1/R1 letter jump, Game Manager's rows - here with HoldRepeat as the repeat
struct EventHoldScreen {
    Input &input;
    HoldRepeat hold;
    bool l1Held = false;
    int moved = 0;

    explicit EventHoldScreen(Input &in) : input(in) {}

    void readInput(uint32_t now) {
        Event e;
        while (input.poll(e)) {
            if (e.type == Event::Type::DpadDown || e.type == Event::Type::DpadUp) {
                if (input.dpadDown()) {
                    if (!hold.held()) {
                        moved += 1;
                        hold.press(1, now);
                    }
                } else if (input.dpadCentered()) {
                    hold.release();
                }
            } else if (e.type == Event::Type::ButtonDown && e.button == Button::L1) {
                l1Held = true;
            } else if (e.type == Event::Type::ButtonUp && e.button == Button::L1) {
                l1Held = false;
            }
        }
    }
    void frame(uint32_t now) {
        readInput(now);
        moved += hold.due(now);
    }
};

} // namespace

TEST_CASE("busy rule: a hold that waits for its release event does not run on after the job") {
    MaybeGui mg;
    if (!mg.available())
        return;
    EventHoldScreen screen(mg.input());
    pushHat(true, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pushButton(true, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    screen.frame(1000);
    REQUIRE(screen.hold.held());
    REQUIRE(screen.l1Held);
    REQUIRE(screen.moved == 1);

    busyJob(mg.input(), [] {}); // a delete, say; the player keeps both held through it

    // the screen's next frames: nothing it holds may go on
    for (uint32_t t = 3000; t <= 5000; t += 16)
        screen.frame(t);
    CHECK_FALSE(screen.hold.held());
    CHECK_FALSE(screen.l1Held);
    CHECK(screen.moved == 1);
}

TEST_CASE("busy rule: a repeat loop that runs until another pad event is pending stops at the job's end") {
    MaybeGui mg;
    if (!mg.available())
        return;
    // GuiMenuBase::doJoyDown: do { step; render(); } while (fastForwardUntilAnotherEvent()) - the loop ends
    // when padEventPending() says so. Held through a job that ran inside the loop, it must say so at once.
    pushHat(true, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    REQUIRE(count(drain(mg.input()), Event::Type::DpadDown) == 1);
    CHECK_FALSE(mg.input().padEventPending()); // held, nothing new: the loop repeats

    busyJob(mg.input(), [] {});

    CHECK(mg.input().padEventPending()); // the loop ends here, not whenever the player lets go
}

TEST_CASE("busy rule: a hold that reads the live d-pad state each frame stops after the job") {
    MaybeGui mg;
    if (!mg.available())
        return;
    // Options' and the game editor's holdTick: the hold goes on while input.dpadRight() says it is held
    pushHat(true, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    drain(mg.input());
    HoldRepeat hold;
    hold.press(1, 1000);
    REQUIRE(mg.input().dpadRight());

    busyJob(mg.input(), [] {});

    const bool stillDown = mg.input().dpadRight();
    if (!stillDown)
        hold.release();
    CHECK_FALSE(stillDown);
    CHECK(hold.due(5000) == 0);
    // and reading the input first, as a screen's loop does, changes nothing
    drain(mg.input());
    CHECK_FALSE(mg.input().dpadRight());
}
