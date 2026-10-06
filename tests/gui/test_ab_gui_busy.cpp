//
// abgui::Busy (G3l of docs/ab-gui-plan.md): the spinner a long job shows, moved out of Gui unchanged. The pure rules
// against a copy of the old Gui formulas - the 40 ms pace, the 70 ms spinner step, the ring's and the message's
// places, the progress bar's track and its clamped share, the "please wait" picture's spinner under the logo. Then
// begin/tick/end on a recording display (a headless GuiBase for the renderer and the Input - those cases skip
// themselves without one, like test_busy_input): the backdrop's frame then the busy frame, the pace, a progress
// change drawn at once, a begin inside a job, a tick from inside a screen's drawing (a frame of its own), the input
// queued during the job flushed at its end and only then, and the DebugDriver's busy flag. On a real renderer the
// busy frame is the backdrop dimmed, pixel for pixel.
//
#include "doctest/doctest.h"

#include <ab_gui/busy.h>
#include <ab_gui/context.h>
#include <ab_gui/screen_stack.h>

#include <ableem/ui/debug_driver.h>
#include <ableem/ui/gui_base.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::Busy;
using abgui::Context;
using abgui::ScreenStack;
using ableem::Button;
using ableem::Color;
using ableem::DebugDriver;
using ableem::Event;
using ableem::GuiBase;
using ableem::Rect;

namespace {

// the old Gui's numbers, copied as they were before the move (Gui::drawBusyFrame, drawSpinner, drawText)
namespace old {
const int ScreenWidth = 1280, ScreenHeight = 720;
bool due(unsigned int now, unsigned int lastFrame) {
    return !(lastFrame != 0 && now - lastFrame < 40);
}
int lead(unsigned int ticks) {
    return static_cast<int>(ticks / 70) % 12;
}
Rect track(int lineHeight) {
    const int width = 400, height = 6;
    const int messageY = ScreenHeight / 2 - 20 + 30 + 24;
    return Rect(ScreenWidth / 2 - width / 2, messageY + lineHeight + 12, width, height);
}
int waitY(const Rect &logo) {
    const int below = logo.y + logo.h;
    return std::min(ScreenHeight - 90, std::max(below + 60, ScreenHeight * 2 / 3));
}
} // namespace old

bool sameRect(const Rect &a, const Rect &b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

bool sameColor(const Color &a, const Color &b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// a display that writes down every call, in order
struct Recorder : ScreenStack::Display {
    vector<string> calls;
    Color lastColor;
    void setClearColor(const Color &color) override {
        lastColor = color;
        calls.push_back("color");
    }
    void clear() override { calls.push_back("clear"); }
    void present() override { calls.push_back("present"); }
};

struct MaybeGui {
    unique_ptr<GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<GuiBase>("ab_gui_test_busy", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_busy: skipping - no usable renderer here (" << why << ")");
        }
        if (gui)
            gui->input().flushEvents(); // whatever SDL queued while the window came up
    }

    bool available() const { return gui != nullptr; }
};

// a program's Context on a recording stack, with a clock the test moves
struct Rig {
    Recorder display;
    ScreenStack stack{display};
    unique_ptr<Context> ctx;
    unsigned int now = 1000;

    explicit Rig(GuiBase &gui) {
        ctx.reset(new Context(gui.renderer(), gui.input(), gui.platform()));
        ctx->clock = [this]() { return now; };
        ctx->setStack(stack);
    }
    Busy &busy() { return ctx->stack().busy(); }
    // what the screen as it is presents: one frame through the stack
    function<void()> screen() {
        return [this]() { stack.frame([this]() { display.calls.push_back("screen"); }); };
    }
};

Event button(Button b, bool down) {
    Event e;
    e.type = down ? Event::Type::ButtonDown : Event::Type::ButtonUp;
    e.button = b;
    return e;
}

// every event poll() hands out now, the consumed ones (Type::None) left out
vector<Event> drain(ableem::Input &input) {
    vector<Event> out;
    Event e;
    while (input.poll(e)) {
        if (e.type != Event::Type::None)
            out.push_back(e);
    }
    return out;
}

} // namespace

//*******************************
// the pure rules
//*******************************
TEST_CASE("the pace: a tick draws the first time, then once 40 ms have passed - as the old busyTick") {
    CHECK(Busy::FrameInterval == 40);
    CHECK(Busy::frameDue(1000, 0));
    CHECK_FALSE(Busy::frameDue(1039, 1000));
    CHECK(Busy::frameDue(1040, 1000));
    CHECK(Busy::frameDue(5000, 1000));
    for (unsigned int last : {0u, 1u, 999u, 1000u, 0xFFFFFFF0u}) {
        for (unsigned int step : {0u, 1u, 20u, 39u, 40u, 41u, 100u, 5000u}) {
            const unsigned int now = last + step;
            CHECK(Busy::frameDue(now, last) == old::due(now, last));
        }
    }
}

TEST_CASE("the spinner turns a dot every 70 ms, round twelve dots") {
    CHECK(Busy::spinnerLead(0) == 0);
    CHECK(Busy::spinnerLead(69) == 0);
    CHECK(Busy::spinnerLead(70) == 1);
    CHECK(Busy::spinnerLead(839) == 11);
    CHECK(Busy::spinnerLead(840) == 0);
    for (unsigned int t = 0; t < 3000; t += 7)
        CHECK(Busy::spinnerLead(t) == old::lead(t));
    CHECK(Busy::spinnerLead(0xFFFFFFFFu) == old::lead(0xFFFFFFFFu));
}

TEST_CASE("the ring, the message and the bar sit where the old busy frame put them") {
    const ableem::Point centre = Busy::spinnerCentre(old::ScreenWidth, old::ScreenHeight);
    CHECK(centre.x == 640);
    CHECK(centre.y == 340);                   // ScreenHeight / 2 - 20
    CHECK(Busy::messageTop(centre.y) == 394); // 24 px below the ring of radius 30
    for (int lineHeight : {0, 22, 26, 31, 40})
        CHECK(sameRect(Busy::barRect(old::ScreenWidth, old::ScreenHeight, lineHeight), old::track(lineHeight)));
    CHECK(sameRect(Busy::barRect(1280, 720, 26), Rect(440, 432, 400, 6)));
}

TEST_CASE("the toast frame surrounds the ring, the message and the bar") {
    // 1280 x 720: ring centre y 340, ring top 310, message top 394
    const Rect plain = Busy::toastRect(1280, 720, 200, 26, false);
    CHECK(sameRect(plain, Rect(640 - 124, 310 - 24, 248, (394 + 26 - 310) + 48)));
    // a short message: the ring's diameter is the minimum width
    CHECK(Busy::toastRect(1280, 720, 10, 26, false).w == 60 + 48);
    // a bar makes it as wide as the bar and as tall as the bar's foot
    const Rect bar = Busy::toastRect(1280, 720, 200, 26, true);
    CHECK(bar.w == 400 + 48);
    CHECK(bar.y + bar.h == 394 + 26 + 12 + 6 + 24);
}

TEST_CASE("a message wider than the canvas is wrapped: the room is the canvas less two toast pads each side") {
    CHECK(Busy::messageRoom(1280) == 1280 - 96);
    CHECK(Busy::messageRoom(640) == 544); // a 4:3 output's canvas
    // the frame follows the text's height: three rows of 26 reach as far below the message's top as three
    const Rect three = Busy::toastRect(640, 480, 544, 3 * 26, false);
    const Rect one = Busy::toastRect(640, 480, 544, 26, false);
    CHECK(three.h == one.h + 2 * 26);
    CHECK(three.w == one.w);
    CHECK(Busy::barRect(640, 480, 3 * 26).y == Busy::barRect(640, 480, 26).y + 2 * 26);
}

TEST_CASE("the bar shows done clamped to 0..total") {
    CHECK(Busy::barDone(0, 10) == 0);
    CHECK(Busy::barDone(4, 10) == 4);
    CHECK(Busy::barDone(10, 10) == 10);
    CHECK(Busy::barDone(12, 10) == 10);
    CHECK(Busy::barDone(-3, 10) == 0);
    CHECK(Busy::barDone(5, 0) == 0);
}

TEST_CASE("the please-wait spinner: under the logo, at least two thirds down, never below the foot") {
    for (int bottom : {0, 100, 300, 419, 420, 421, 500, 569, 570, 571, 700}) {
        const Rect logo(400, bottom / 2, 480, bottom - bottom / 2);
        CHECK(Busy::waitSpinnerY(old::ScreenHeight, logo) == old::waitY(logo));
    }
    CHECK(Busy::waitSpinnerY(720, Rect()) == 480);                // no logo: two thirds down
    CHECK(Busy::waitSpinnerY(720, Rect(0, 400, 10, 100)) == 560); // 60 under the logo
    CHECK(Busy::waitSpinnerY(720, Rect(0, 600, 10, 100)) == 630); // 90 above the bottom at most
}

//*******************************
// begin / tick / end
//*******************************
TEST_CASE("a Context's stack hands out its busy spinner, bound to that Context") {
    MaybeGui g;
    if (!g.available())
        return;
    Recorder display;
    ScreenStack stack(display);
    CHECK_FALSE(stack.busy().bound());
    Context ctx(g.gui->renderer(), g.gui->input(), g.gui->platform());
    ctx.setStack(stack);
    CHECK(stack.busy().bound());
    CHECK(&ctx.stack().busy() == &stack.busy());
}

TEST_CASE("unbound, a busy spinner does nothing") {
    Recorder display;
    ScreenStack stack(display);
    Busy &busy = stack.busy();
    int redraws = 0;
    const int level = DebugDriver::busyLevel();
    busy.begin("Loading...", [&]() { redraws++; });
    busy.tick();
    busy.end();
    CHECK(redraws == 0);
    CHECK_FALSE(busy.active());
    CHECK(display.calls.empty());
    CHECK(DebugDriver::busyLevel() == level);
}

TEST_CASE("begin: the screen as it is presented once, then the first busy frame on black") {
    MaybeGui g;
    if (!g.available())
        return;
    Rig rig(*g.gui);
    Busy &busy = rig.busy();
    const int level = DebugDriver::busyLevel();
    busy.begin("Applying settings...", rig.screen());
    CHECK(rig.display.calls == vector<string>{"color", "clear", "screen", "present", "color", "clear", "present"});
    CHECK(sameColor(rig.display.lastColor, Color(0, 0, 0, 255)));
    CHECK(rig.stack.presented() == 2);
    CHECK(busy.active());
    CHECK(busy.message() == "Applying settings...");
    CHECK(busy.done() == 0);
    CHECK(busy.total() == 0);
    CHECK(busy.started() == 1000);
    CHECK(busy.lastFrame() == 1000);
    // the DebugDriver's `busy` answers 1 until the job ends
    CHECK(DebugDriver::busy());
    CHECK(DebugDriver::busyLevel() == level + 1);
    busy.end();
    CHECK_FALSE(busy.active());
    CHECK(DebugDriver::busyLevel() == level);
}

TEST_CASE("the pace: a tick within 40 ms of the last frame draws nothing; a progress change is drawn at once") {
    MaybeGui g;
    if (!g.available())
        return;
    Rig rig(*g.gui);
    Busy &busy = rig.busy();
    busy.begin("Deleting...", rig.screen());
    const unsigned long first = rig.stack.presented();

    rig.now = 1039;
    busy.tick();
    CHECK(rig.stack.presented() == first);
    rig.now = 1040;
    busy.tick();
    CHECK(rig.stack.presented() == first + 1);
    CHECK(busy.lastFrame() == 1040);
    rig.now = 1041;
    busy.tick();
    busy.tick();
    CHECK(rig.stack.presented() == first + 1);

    // a tight loop over 1 s draws 25 frames, not a thousand
    for (unsigned int t = 1041; t < 2041; t++) {
        rig.now = t;
        busy.tick();
    }
    CHECK(rig.stack.presented() == first + 1 + 25);

    // setProgress: the next tick draws, whatever the pace
    rig.now = 2041;
    busy.setProgress(3, 10);
    CHECK(busy.done() == 3);
    CHECK(busy.total() == 10);
    CHECK(busy.lastFrame() == 0);
    const unsigned long before = rig.stack.presented();
    busy.tick();
    CHECK(rig.stack.presented() == before + 1);
    CHECK(busy.lastFrame() == 2041);
    busy.end();

    // no job: a tick draws nothing
    rig.now = 9000;
    const unsigned long idle = rig.stack.presented();
    busy.tick();
    CHECK(rig.stack.presented() == idle);
}

TEST_CASE("a begin inside a job is the same job: new message, the bar gone, the DebugDriver told once") {
    MaybeGui g;
    if (!g.available())
        return;
    Rig rig(*g.gui);
    Busy &busy = rig.busy();
    const int level = DebugDriver::busyLevel();
    busy.begin("Applying settings...", rig.screen());
    busy.setProgress(5, 8);
    rig.now = 1500;
    busy.begin("Loading...", rig.screen());
    CHECK(busy.active());
    CHECK(busy.message() == "Loading...");
    CHECK(busy.done() == 0);
    CHECK(busy.total() == 0);
    CHECK(busy.started() == 1500);
    CHECK(DebugDriver::busyLevel() == level + 1);
    CHECK(rig.stack.presented() == 4); // each begin: the screen, then a busy frame

    // one end() ends it; another (a screen's every idle frame) changes nothing
    busy.end();
    CHECK_FALSE(busy.active());
    CHECK(DebugDriver::busyLevel() == level);
    busy.end();
    CHECK(DebugDriver::busyLevel() == level);
}

TEST_CASE("a tick from inside a screen's drawing is a frame of its own, presented at once") {
    MaybeGui g;
    if (!g.available())
        return;
    Rig rig(*g.gui);
    Busy &busy = rig.busy();
    busy.begin("Loading...", rig.screen());
    rig.display.calls.clear();
    rig.now = 2000;
    // a load inside a screen's drawing ticks the spinner (the theme loader, the carousel's texture loads)
    rig.stack.frame([&]() {
        rig.display.calls.push_back("outer");
        busy.tick();
        rig.display.calls.push_back("outer again");
    });
    CHECK(rig.display.calls ==
          vector<string>{"color", "clear", "outer", "color", "clear", "present", "outer again", "present"});
    busy.end();
}

TEST_CASE("the input queued during the job is dropped at its end, a held press released once - the busy rule") {
    MaybeGui g;
    if (!g.available())
        return;
    Rig rig(*g.gui);
    ableem::Input &input = g.gui->input();
    Busy &busy = rig.busy();

    // Square pressed before the job and handed to a screen: held
    input.inject(button(Button::Square, true));
    REQUIRE(drain(input).size() == 1);

    busy.begin("Applying settings...", rig.screen());
    // meanwhile the player presses Cross (the spinner looked stuck) and taps Circle; nothing polls
    input.inject(button(Button::Cross, true));
    input.inject(button(Button::Circle, true));
    input.inject(button(Button::Circle, false));
    rig.now = 1100;
    busy.tick();
    busy.end();

    // none of it reaches a screen; the held Square is released, once
    const vector<Event> after = drain(input);
    REQUIRE(after.size() == 1);
    CHECK(after[0].type == Event::Type::ButtonUp);
    CHECK(after[0].button == Button::Square);
}

TEST_CASE("an end() with no job flushes nothing") {
    MaybeGui g;
    if (!g.available())
        return;
    Rig rig(*g.gui);
    ableem::Input &input = g.gui->input();
    Busy &busy = rig.busy();
    input.inject(button(Button::Cross, true));
    busy.end(); // a screen's idle frame
    const vector<Event> after = drain(input);
    REQUIRE(after.size() == 1);
    CHECK(after[0].type == Event::Type::ButtonDown);
    CHECK(after[0].button == Button::Cross);
    input.flushInputEvents(); // nothing held into the next case
    drain(input);
}

TEST_CASE("on a renderer: the busy frame is the backdrop dimmed") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Renderer &renderer = g.gui->renderer();
    ScreenStack stack(renderer);
    Context ctx(renderer, g.gui->input(), g.gui->platform());
    unsigned int now = 1000;
    ctx.clock = [&now]() { return now; };
    ctx.setStack(stack);
    Busy &busy = stack.busy();

    const unsigned long before = renderer.frameCount();
    busy.begin("", [&]() { stack.frame(Color(255, 255, 255, 255), []() {}); });
    CHECK(renderer.frameCount() == before + 2);

    // the next busy frame's copy (the DebugDriver's `shot`)
    renderer.setFrameCache(true);
    const unsigned long asked = renderer.requestFrameCopy();
    now = 1040;
    busy.tick();
    vector<unsigned char> pixels;
    int w = 0, h = 0, pitch = 0;
    const unsigned long copied = renderer.copyLastFrame(pixels, w, h, pitch);
    renderer.setFrameCache(false);
    busy.end();
    if (copied <= asked || w != 320 || h != 240) {
        MESSAGE("test_ab_gui_busy: no frame copy from this renderer - pixel checks skipped");
        return;
    }
    // a corner, away from the ring: white under black at the style's dim (110) - 145 each
    const size_t at = static_cast<size_t>(5) * pitch + static_cast<size_t>(5) * 4;
    const int r = pixels[at + 2], gr = pixels[at + 1], b = pixels[at + 0];
    CHECK(std::abs(r - 145) <= 2);
    CHECK(std::abs(gr - 145) <= 2);
    CHECK(std::abs(b - 145) <= 2);
}
