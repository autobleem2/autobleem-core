//
// The screen transitions (UIREV-48, docs/ab-gui-plan.md 7a): what a transition is, where its two pictures go
// (composeTransition - pure), the player's state machine on a Tweens with a clock the test sets (armed -> started by
// the target's first frame -> busy while it runs -> over; a press finishes it at once; another frame finishes it; off =
// instant; frame-rate independent), and the stack on a real renderer (a headless GuiBase - those cases skip themselves
// without one): the old picture under a screen that opens, the composed frames, the press, a screen that closes, the
// frame probe, the animations off, and a last screen's fade to black.
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>
#include <ab_gui/screen.h>
#include <ab_gui/screen_stack.h>
#include <ab_gui/screen_transition.h>
#include <ab_gui/tween.h>

#include <ableem/ui/debug_driver.h>
#include <ableem/ui/gui_base.h>
#include <ableem/ui/gui_screen.h>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::composeTransition;
using abgui::Context;
using abgui::ScreenStack;
using abgui::ScreenTransitions;
using abgui::SlideFrom;
using abgui::Transition;
using abgui::TransitionFrame;
using abgui::TransitionKind;
using abgui::TransitionPlayer;
using abgui::Tweens;
using ableem::Color;
using ableem::DebugDriver;
using ableem::Event;
using ableem::GuiBase;

namespace {

const float W = 1280.0f, H = 720.0f;

bool fullCanvas(const abgui::TransitionLayer &l) {
    return l.rect.x == 0.0f && l.rect.y == 0.0f && l.rect.w == W && l.rect.h == H;
}

// a Tweens on a clock the test sets
struct Clocked {
    unsigned int now = 1000;
    Tweens tweens;
    Clocked() {
        tweens.clock = [this]() { return now; };
    }
    void at(unsigned int t) {
        now = t;
        tweens.update();
    }
};

} // namespace

//********************
// Transition
//********************
TEST_CASE("Transition: the kinds' default lengths, a set length, None moves nothing") {
    CHECK(Transition::none().duration() == 0);
    CHECK_FALSE(Transition::none().moves());
    CHECK(Transition::fade().duration() == abgui::FadeTransitionMs);
    CHECK(Transition::crossFade().duration() == abgui::CrossFadeTransitionMs);
    CHECK(Transition::slide(SlideFrom::Left).duration() == abgui::SlideTransitionMs);
    CHECK(Transition::drop().kind == TransitionKind::Slide);
    CHECK(Transition::drop().from == SlideFrom::Top);
    CHECK(Transition::pop().duration() == abgui::PopTransitionMs);
    CHECK(Transition::pop(90).duration() == 90);
    CHECK(Transition::fade(425, 300).delayMs == 300);
    // every default is short: the picture must never feel slow
    for (const Transition &t : {Transition::fade(), Transition::crossFade(), Transition::drop(), Transition::pop()}) {
        CHECK(t.moves());
        CHECK(t.duration() >= 150);
        CHECK(t.duration() <= 250);
    }
}

TEST_CASE("ScreenTransitions: the out one is the in one (played backwards) unless declared") {
    const ScreenTransitions popOnly(Transition::pop());
    CHECK(popOnly.closing().kind == TransitionKind::Pop);
    const ScreenTransitions both(Transition::fade(), Transition::none());
    CHECK(both.in.kind == TransitionKind::Fade);
    CHECK(both.closing().kind == TransitionKind::None);
    CHECK(abgui::defaultScreenTransitions().in.kind == TransitionKind::CrossFade);
    CHECK(abgui::defaultScreenTransitions().closing().kind == TransitionKind::CrossFade);
}

//********************
// composeTransition
//********************
TEST_CASE("composeTransition: a cross-fade dissolves the new picture over the old one, and back") {
    TransitionFrame f = composeTransition(Transition::crossFade(), false, 0.0f, W, H);
    CHECK(f.oldPicture.drawn);
    CHECK(f.oldPicture.alpha == 255);
    CHECK_FALSE(f.oldOnTop);
    CHECK_FALSE(f.newPicture.drawn); // alpha 0: nothing to draw
    f = composeTransition(Transition::crossFade(), false, 0.5f, W, H);
    CHECK(f.newPicture.alpha == 128);
    CHECK(fullCanvas(f.newPicture));
    f = composeTransition(Transition::crossFade(), false, 1.0f, W, H);
    CHECK(f.newPicture.alpha == 255);
    // backwards (a screen closes): the closing picture is the one that fades, over the screen coming back
    f = composeTransition(Transition::crossFade(), true, 0.25f, W, H);
    CHECK(f.oldOnTop);
    CHECK(f.newPicture.alpha == 255);
    CHECK(f.oldPicture.alpha == 191);
    // out of range is clamped
    f = composeTransition(Transition::crossFade(), false, 3.0f, W, H);
    CHECK(f.newPicture.alpha == 255);
}

TEST_CASE("composeTransition: a pop grows from 95% about the centre while it fades in; backwards it shrinks away") {
    TransitionFrame f = composeTransition(Transition::pop(), false, 0.0f, W, H);
    CHECK_FALSE(f.newPicture.drawn);
    f = composeTransition(Transition::pop(), false, 0.5f, W, H);
    const float scale = abgui::PopStartScale + (1.0f - abgui::PopStartScale) * abgui::ease::outCubic(0.5f);
    CHECK(f.newPicture.rect.w == doctest::Approx(W * scale));
    CHECK(f.newPicture.rect.h == doctest::Approx(H * scale));
    CHECK(f.newPicture.rect.x == doctest::Approx((W - W * scale) / 2.0f));
    CHECK(f.newPicture.rect.y == doctest::Approx((H - H * scale) / 2.0f));
    CHECK(f.newPicture.alpha == 128);
    f = composeTransition(Transition::pop(), false, 1.0f, W, H);
    CHECK(f.newPicture.rect.w == doctest::Approx(W));
    CHECK(f.newPicture.alpha == 255);
    // backwards: at the start the closing dialog is whole, at the end gone at 95%
    f = composeTransition(Transition::pop(), true, 0.0f, W, H);
    CHECK(f.oldOnTop);
    CHECK(f.oldPicture.alpha == 255);
    CHECK(f.oldPicture.rect.w == doctest::Approx(W));
    f = composeTransition(Transition::pop(), true, 1.0f, W, H);
    CHECK_FALSE(f.oldPicture.drawn);
    CHECK(f.newPicture.alpha == 255);
}

TEST_CASE("composeTransition: a slide comes in from its edge on easeOutCubic, the picture under it dims") {
    const float e = abgui::ease::outCubic(0.5f);
    TransitionFrame f = composeTransition(Transition::drop(), false, 0.5f, W, H);
    CHECK(f.newPicture.rect.y == doctest::Approx(-H * (1.0f - e)));
    CHECK(f.newPicture.rect.x == 0.0f);
    CHECK(f.newPicture.alpha == 255);
    CHECK(f.oldPicture.dim == static_cast<int>(std::lround(abgui::TransitionDimAlpha * e)));
    CHECK(composeTransition(Transition::slide(SlideFrom::Bottom), false, 0.5f, W, H).newPicture.rect.y ==
          doctest::Approx(H * (1.0f - e)));
    CHECK(composeTransition(Transition::slide(SlideFrom::Left), false, 0.5f, W, H).newPicture.rect.x ==
          doctest::Approx(-W * (1.0f - e)));
    CHECK(composeTransition(Transition::slide(SlideFrom::Right), false, 0.5f, W, H).newPicture.rect.x ==
          doctest::Approx(W * (1.0f - e)));
    // at the start it is just off its edge, at the end in place with the full dim under it
    f = composeTransition(Transition::drop(), false, 0.0f, W, H);
    CHECK(f.newPicture.rect.y == doctest::Approx(-H));
    CHECK(f.oldPicture.dim == 0);
    f = composeTransition(Transition::drop(), false, 1.0f, W, H);
    CHECK(f.newPicture.rect.y == doctest::Approx(0.0f));
    CHECK(f.oldPicture.dim == abgui::TransitionDimAlpha);
    // backwards: the closing picture goes back up, the one coming back brightens
    f = composeTransition(Transition::drop(), true, 1.0f, W, H);
    CHECK(f.oldOnTop);
    CHECK(f.oldPicture.rect.y == doctest::Approx(-H));
    CHECK(f.newPicture.dim == 0);
    // over black (nothing under the first screen): no dim to draw
    f = composeTransition(Transition::drop(), false, 0.5f, W, H, false, true);
    CHECK_FALSE(f.oldPicture.drawn);
    CHECK(f.newPicture.drawn);
}

TEST_CASE("composeTransition: a fade goes through black; with one picture it fades over the whole time") {
    TransitionFrame f = composeTransition(Transition::fade(), false, 0.25f, W, H);
    CHECK(f.oldPicture.alpha == 128);
    CHECK_FALSE(f.newPicture.drawn);
    f = composeTransition(Transition::fade(), false, 0.5f, W, H);
    CHECK_FALSE(f.oldPicture.drawn);
    CHECK_FALSE(f.newPicture.drawn); // black half-way
    f = composeTransition(Transition::fade(), false, 0.75f, W, H);
    CHECK(f.newPicture.alpha == 128);
    // nothing before it (the splash in): in from black over the whole time
    f = composeTransition(Transition::fade(), false, 0.5f, W, H, false, true);
    CHECK(f.newPicture.alpha == 128);
    CHECK_FALSE(f.oldPicture.drawn);
    // nothing after it (the splash out): out to black over the whole time
    f = composeTransition(Transition::fade(), true, 0.75f, W, H, true, false);
    CHECK(f.oldPicture.alpha == 64);
    CHECK_FALSE(f.newPicture.drawn);
}

//********************
// TransitionPlayer
//********************
TEST_CASE("TransitionPlayer: off (the Options row), nothing is armed - every screen change is instant") {
    Clocked c;
    TransitionPlayer player(c.tweens);
    player.setEnabled(false);
    int screen = 0;
    CHECK_FALSE(player.arm(&screen, Transition::crossFade(), false));
    CHECK_FALSE(player.armed());
    CHECK_FALSE(player.frame(&screen));
    CHECK_FALSE(c.tweens.busy());
    // None or a zero length arms nothing either
    player.setEnabled(true);
    CHECK_FALSE(player.arm(&screen, Transition::none(), false));
    CHECK_FALSE(player.armed());
    // switching off mid-way ends it
    REQUIRE(player.arm(&screen, Transition::crossFade(), false));
    CHECK(player.frame(&screen));
    CHECK(player.running());
    player.setEnabled(false);
    CHECK_FALSE(player.armed());
    CHECK_FALSE(c.tweens.busy());
}

TEST_CASE("TransitionPlayer: the target's first frame starts it; busy while it runs; its time is the clock's") {
    Clocked c;
    TransitionPlayer player(c.tweens);
    int screen = 0;
    const int level = DebugDriver::busyLevel();
    REQUIRE(player.arm(&screen, Transition::crossFade(200), false));
    // armed, waiting for the new screen to be ready: not busy yet, nothing moves
    CHECK(player.armed());
    CHECK_FALSE(player.running());
    CHECK_FALSE(c.tweens.busy());
    c.at(1500); // however long the screen takes to load
    CHECK(player.frame(&screen));
    CHECK(player.running());
    CHECK(player.progress() == doctest::Approx(0.0f));
    CHECK(c.tweens.busy());
    CHECK(DebugDriver::busyLevel() == level + 1); // wait_ready waits for it
    // the progress follows the clock, not the frames: one frame or ten, 50 ms in is a quarter
    c.at(1550);
    CHECK(player.progress() == doctest::Approx(0.25f));
    for (int i = 0; i < 10; i++)
        c.at(1550);
    CHECK(player.progress() == doctest::Approx(0.25f));
    CHECK(player.frame(&screen));
    c.at(1650);
    CHECK(player.progress() == doctest::Approx(0.75f));
    c.at(1700); // the end: over, not busy, nothing composed any more
    CHECK_FALSE(player.armed());
    CHECK_FALSE(player.frame(&screen));
    CHECK_FALSE(c.tweens.busy());
    CHECK(DebugDriver::busyLevel() == level);
}

TEST_CASE("TransitionPlayer: a press finishes it at once") {
    Clocked c;
    TransitionPlayer player(c.tweens);
    int screen = 0;
    const int level = DebugDriver::busyLevel();
    REQUIRE(player.arm(&screen, Transition::drop(), false));
    CHECK(player.frame(&screen));
    c.at(1050);
    CHECK(player.running());
    player.finish(); // the press
    CHECK_FALSE(player.armed());
    CHECK(player.progress() == doctest::Approx(1.0f));
    CHECK_FALSE(c.tweens.busy());
    CHECK(DebugDriver::busyLevel() == level);
    CHECK_FALSE(player.frame(&screen)); // the next frame is the new screen alone
    // a press before it started (armed, the new screen still loading) drops it as well
    REQUIRE(player.arm(&screen, Transition::drop(), false));
    player.finish();
    CHECK_FALSE(player.armed());
    CHECK_FALSE(player.frame(&screen));
}

TEST_CASE("TransitionPlayer: a frame of another screen (or no screen's - a busy frame) finishes it") {
    Clocked c;
    TransitionPlayer player(c.tweens);
    int screen = 0, other = 0;
    REQUIRE(player.arm(&screen, Transition::pop(), false));
    CHECK(player.frame(&screen));
    CHECK_FALSE(player.frame(&other));
    CHECK_FALSE(player.armed());
    CHECK_FALSE(c.tweens.busy());
    REQUIRE(player.arm(&screen, Transition::pop(), false));
    CHECK_FALSE(player.frame(nullptr));
    CHECK_FALSE(player.armed());
    // a new arm finishes the one before
    REQUIRE(player.arm(&screen, Transition::pop(), false));
    CHECK(player.frame(&screen));
    REQUIRE(player.arm(&other, Transition::crossFade(), true));
    CHECK(player.target() == &other);
    CHECK(player.backwards());
    CHECK_FALSE(player.running());
}

TEST_CASE("TransitionPlayer: a delay keeps the old picture (busy meanwhile), then it moves") {
    Clocked c;
    TransitionPlayer player(c.tweens);
    int screen = 0;
    REQUIRE(player.arm(&screen, Transition::fade(400, 300), false));
    CHECK(player.frame(&screen));
    c.at(1200);
    CHECK(player.running());
    CHECK(c.tweens.busy());
    CHECK(player.progress() == doctest::Approx(0.0f));
    c.at(1500);
    CHECK(player.progress() == doctest::Approx(0.5f));
    c.at(1700);
    CHECK_FALSE(player.armed());
}

TEST_CASE("TransitionPlayer: a run cleared behind its back (Tweens::clear) counts as over") {
    Clocked c;
    TransitionPlayer player(c.tweens);
    int screen = 0;
    REQUIRE(player.arm(&screen, Transition::pop(), false));
    CHECK(player.frame(&screen));
    c.tweens.clear();
    CHECK_FALSE(player.armed());
    CHECK_FALSE(player.running());
    CHECK_FALSE(player.frame(&screen));
}

//********************
// ScreenStack on a renderer
//********************
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
            gui = make_unique<GuiBase>("ab_gui_test_screen_transitions", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_screen_transitions: skipping - no usable renderer here (" << why << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

// a screen of one colour
struct Solid : abgui::Screen {
    Color color;
    int drawn = 0;
    Solid(GuiBase &gui, Context &ctx, const Color &c) : abgui::Screen(gui, ctx), color(c) {}
    void draw() override {
        drawn++;
        ctx.renderer().setDrawColor(color);
        ctx.renderer().fillRect();
    }
};

// the program's side: a stack on the renderer, attached to the input, its tweens on a clock the test sets
struct Program {
    GuiBase &gui;
    ScreenStack stack;
    Context ctx;
    unsigned int now = 1000;
    explicit Program(GuiBase &g) : gui(g), stack(g.renderer()), ctx(g.renderer(), g.input(), g.platform()) {
        ctx.setStack(stack);
        stack.tweens().clock = [this]() { return now; };
        stack.attach(g.input());
    }
    ableem::GuiScreenObserver &screens() { return *ableem::GuiScreen::observer(); }
};

// the centre pixel of the frame `screen` presents next
Color centreOf(GuiBase &gui, abgui::Screen &screen) {
    ableem::Renderer &renderer = gui.renderer();
    renderer.setFrameCache(true);
    const unsigned long asked = renderer.requestFrameCopy();
    screen.render();
    vector<unsigned char> pixels;
    int w = 0, h = 0, pitch = 0;
    const unsigned long copied = renderer.copyLastFrame(pixels, w, h, pitch);
    renderer.setFrameCache(false);
    REQUIRE(copied > asked);
    const size_t at = static_cast<size_t>(h / 2) * pitch + static_cast<size_t>(w / 2) * 4;
    return Color(pixels[at + 2], pixels[at + 1], pixels[at + 0], 255);
}

bool near(const Color &a, int r, int g, int b, int tolerance = 3) {
    return abs(a.r - r) <= tolerance && abs(a.g - g) <= tolerance && abs(a.b - b) <= tolerance;
}

void press(GuiBase &gui) {
    Event e;
    e.type = Event::Type::ButtonDown;
    e.button = ableem::Button::Cross;
    gui.input().inject(e);
    Event got;
    while (gui.input().poll(got)) {
    }
}

} // namespace

TEST_CASE("ScreenStack: a screen that opens comes in over the picture under it; a press finishes it") {
    MaybeGui g;
    if (!g.available())
        return;
    Program p(*g.gui);
    REQUIRE(ableem::GuiScreen::observer() != nullptr);
    Solid red(*g.gui, p.ctx, Color(255, 0, 0, 255));
    red.declareTransitions(ScreenTransitions(Transition::none()));
    Solid blue(*g.gui, p.ctx, Color(0, 0, 255, 255)); // declares nothing: a cross-fade
    p.screens().screenOpens(red);
    CHECK_FALSE(p.stack.bringsIn(red));
    CHECK(near(centreOf(*g.gui, red), 255, 0, 0));

    p.screens().screenOpens(blue);
    CHECK(p.stack.bringsIn(blue));
    CHECK_FALSE(p.stack.transitioning()); // armed: it starts with blue's first frame
    const int level = DebugDriver::busyLevel();
    // the first frame is the old picture (blue's alpha 0) - the screen changes only once the new one draws
    CHECK(near(centreOf(*g.gui, blue), 255, 0, 0));
    CHECK(p.stack.transitioning());
    CHECK(DebugDriver::busyLevel() == level + 1);
    // half-way through the 200 ms: half and half - drawn from the two render targets
    p.now += 100;
    CHECK(near(centreOf(*g.gui, blue), 127, 0, 128, 4));
    // a screen resting between presses still gets every frame while it runs
    g.gui->input().setFrameNeed(ableem::Input::FrameNeed::Idle);
    g.gui->input().frameDue(); // (the "event since the last frame" one)
    CHECK(g.gui->input().frameDue());
    const unsigned int waitFrom = g.gui->platform().ticks();
    g.gui->input().waitForEvent(1000); // at once, no waiting
    CHECK(g.gui->platform().ticks() - waitFrom < 500);
    g.gui->input().setFrameNeed(ableem::Input::FrameNeed::Active);
    // a press: to the end at once, and the next frame is blue alone
    press(*g.gui);
    CHECK_FALSE(p.stack.transitioning());
    CHECK_FALSE(p.stack.bringsIn(blue));
    CHECK(DebugDriver::busyLevel() == level);
    CHECK(near(centreOf(*g.gui, blue), 0, 0, 255));

    // blue closes: its last picture leaves (backwards), red is drawn live under it
    p.screens().screenCloses(blue);
    CHECK(near(centreOf(*g.gui, red), 0, 0, 255));
    p.now += 100;
    CHECK(near(centreOf(*g.gui, red), 128, 0, 127, 4));
    p.now += 100;
    CHECK(near(centreOf(*g.gui, red), 255, 0, 0));
    CHECK_FALSE(p.stack.transitioning());
    p.screens().screenCloses(red);
}

TEST_CASE("ScreenStack: with the animations off every change is instant and nothing is drawn twice") {
    MaybeGui g;
    if (!g.available())
        return;
    Program p(*g.gui);
    p.stack.setAnimations(false);
    CHECK_FALSE(p.stack.animations());
    Solid red(*g.gui, p.ctx, Color(255, 0, 0, 255));
    Solid blue(*g.gui, p.ctx, Color(0, 0, 255, 255));
    p.screens().screenOpens(red);
    CHECK(near(centreOf(*g.gui, red), 255, 0, 0));
    const int redDrawn = red.drawn;
    p.screens().screenOpens(blue);
    CHECK_FALSE(p.stack.bringsIn(blue));
    CHECK(red.drawn == redDrawn); // no old picture taken
    CHECK(near(centreOf(*g.gui, blue), 0, 0, 255));
    CHECK_FALSE(p.stack.transitioning());
    p.screens().screenCloses(blue);
    CHECK(near(centreOf(*g.gui, red), 255, 0, 0));
    p.screens().screenCloses(red);
}

TEST_CASE("ScreenStack: a frame that is not the screen's (a busy job, Gui's own) ends the transition first") {
    MaybeGui g;
    if (!g.available())
        return;
    Program p(*g.gui);
    Solid red(*g.gui, p.ctx, Color(255, 0, 0, 255));
    red.declareTransitions(ScreenTransitions(Transition::none()));
    Solid blue(*g.gui, p.ctx, Color(0, 0, 255, 255));
    p.screens().screenOpens(red);
    red.render();
    p.screens().screenOpens(blue);
    blue.render();
    CHECK(p.stack.transitioning());
    p.stack.frame([]() {});
    CHECK_FALSE(p.stack.transitioning());
    // the busy job's own start finishes one too
    p.screens().screenCloses(blue);
    red.render();
    CHECK(p.stack.transitioning());
    p.stack.busy().begin("x", [&]() { red.render(); });
    CHECK_FALSE(p.stack.transitioning());
    p.stack.busy().end();
    p.screens().screenCloses(red);
}

TEST_CASE("ScreenStack: the next screen over nothing takes the start transition once (the launcher after the splash)") {
    MaybeGui g;
    if (!g.available())
        return;
    Program p(*g.gui);
    Solid launcher(*g.gui, p.ctx, Color(0, 255, 0, 255));
    launcher.declareTransitions(ScreenTransitions(Transition::none()));
    p.stack.setStartTransition(Transition::drop());
    p.screens().screenOpens(launcher);
    CHECK(p.stack.bringsIn(launcher));
    // dropping in from the top over black: the top half is the launcher's bottom half first
    CHECK(near(centreOf(*g.gui, launcher), 0, 0, 0));
    p.now += 250;
    CHECK(near(centreOf(*g.gui, launcher), 0, 255, 0));
    p.screens().screenCloses(launcher);
    // once only
    p.screens().screenOpens(launcher);
    CHECK_FALSE(p.stack.bringsIn(launcher));
    p.screens().screenCloses(launcher);
}

TEST_CASE("ScreenStack: a last screen's Fade out plays to black on its own frames before it is gone") {
    MaybeGui g;
    if (!g.available())
        return;
    Program p(*g.gui);
    // a clock that moves 50 ms each time it is asked: the fade's own loop advances it
    p.stack.tweens().clock = [&p]() { return p.now += 50; };
    Solid splash(*g.gui, p.ctx, Color(255, 255, 255, 255));
    splash.declareTransitions(ScreenTransitions(Transition::fade(400), Transition::fade(400)));
    p.screens().screenOpens(splash);
    p.stack.finishTransition(); // (its fade in is the first case's matter)
    splash.render();
    const unsigned long before = p.stack.presented();
    const int drawn = splash.drawn;
    p.screens().screenCloses(splash);
    CHECK(p.stack.presented() > before + 2); // frames of its own, the splash drawn live in each
    CHECK(splash.drawn > drawn + 2);
    CHECK_FALSE(p.stack.transitioning());
    // a screen with another out (the default cross-fade) plays nothing with nothing under it
    Solid plain(*g.gui, p.ctx, Color(255, 255, 255, 255));
    p.screens().screenOpens(plain);
    p.stack.finishTransition();
    plain.render();
    const unsigned long plainBefore = p.stack.presented();
    p.screens().screenCloses(plain);
    CHECK(p.stack.presented() == plainBefore);
}
