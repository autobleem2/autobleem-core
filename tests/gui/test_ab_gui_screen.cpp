//
// abgui::Screen (G3g of docs/ab-gui-plan.md): its loop reads the events through the Context's ActionMap and its
// default onAction() is the adapter under the old doCross_Pressed()-style hooks - so a screen on it must get the same
// hooks, in the same order, as on ableem::GuiScreen's own loop. Every case plays one list of events into both loops (a
// screen of each kind recording its hook calls) and checks the two lists are the same, and against the expected hooks:
// every button's press and release, every d-pad direction, the d-pad's priority while two are held, every key (the
// keyboard-as-pad off, as the typing screens have it, and on), a key held (its repeats), typed text, events no hook
// takes, a screen closing in the middle of a batch, and a hold-repeat running inside a hook (the same steps at the
// same pace, ended by the release). Then the new path itself: the Confirm/Back swap, a rebinding, a screen that
// overrides onAction()/onUnmapped(), and render() going through the Context's stack.
//
// Like test_busy_input.cpp this needs a real GuiBase (the Input the loops poll); it skips itself where there is none.
//
#include "doctest/doctest.h"

#include <ab_gui/actions.h>
#include <ab_gui/context.h>
#include <ab_gui/screen.h>
#include <ab_gui/screen_stack.h>

#include <ableem/ui/gui_base.h>
#include <ableem/ui/gui_screen.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using abgui::Action;
using abgui::ActionEvent;
using abgui::Context;
using abgui::ScreenStack;
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
            gui = make_unique<GuiBase>("ab_gui_test_screen", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_screen: skipping - no usable renderer here (" << why << ")");
        }
        if (gui)
            gui->input().flushEvents(); // whatever SDL queued while the window came up
    }

    bool available() const { return gui != nullptr; }
};

// a display that counts the frames the stack presents
struct CountingDisplay : ScreenStack::Display {
    int clears = 0, presents = 0;
    void setClearColor(const ableem::Color &) override {}
    void clear() override { clears++; }
    void present() override { presents++; }
};

Event button(bool down, Button b) {
    Event e;
    e.type = down ? Event::Type::ButtonDown : Event::Type::ButtonUp;
    e.button = b;
    return e;
}

Event dpad(bool down, Button b) {
    Event e;
    e.type = down ? Event::Type::DpadDown : Event::Type::DpadUp;
    e.button = b;
    return e;
}

Event key(bool down, Key k, int code = 0) {
    Event e;
    e.type = down ? Event::Type::KeyDown : Event::Type::KeyUp;
    e.key = k;
    e.code = code;
    return e;
}

Event text(const string &t) {
    Event e;
    e.type = Event::Type::TextInput;
    e.text = t;
    return e;
}

Event ofType(Event::Type type) {
    Event e;
    e.type = type;
    return e;
}

string joined(const vector<string> &calls) {
    string s;
    for (const string &c : calls)
        s += (s.empty() ? "" : " ") + c;
    return s;
}

// every hook of ableem::GuiScreen, writing its name down; doJoyDown can run a hold-repeat, doCircle_Pressed can close
// the screen
template <class Base> struct Recording : Base {
    template <class... A> explicit Recording(A &&...a) : Base(std::forward<A>(a)...) {}

    vector<string> calls;
    int holdSteps = 0;     // doJoyDown: this many repeats (fastForwardUntilAnotherEvent), then the d-pad let go of
    unsigned holdPace = 0; // ... one every holdPace ms
    vector<unsigned> stepTimes;
    bool circleCloses = false;

    void rec(const char *name) { calls.push_back(name); }

    void doJoyUp() override { rec("doJoyUp"); }
    void doJoyDown() override {
        rec("doJoyDown");
        if (holdSteps == 0)
            return;
        int n = 0;
        do {
            stepTimes.push_back(this->gui.platform().ticks());
            rec("step");
            if (++n == holdSteps)
                this->gui.input().inject(dpad(false, Button::DpadDown)); // the player lets go
        } while (this->fastForwardUntilAnotherEvent(holdPace));
    }
    void doJoyRight() override { rec("doJoyRight"); }
    void doJoyLeft() override { rec("doJoyLeft"); }
    void doJoyCenter() override { rec("doJoyCenter"); }

    void doCross_Pressed() override { rec("doCross_Pressed"); }
    void doCircle_Pressed() override {
        rec("doCircle_Pressed");
        if (circleCloses)
            this->menuVisible = false;
    }
    void doTriangle_Pressed() override { rec("doTriangle_Pressed"); }
    void doSquare_Pressed() override { rec("doSquare_Pressed"); }
    void doStart_Pressed() override { rec("doStart_Pressed"); }
    void doSelect_Pressed() override { rec("doSelect_Pressed"); }
    void doL1_Pressed() override { rec("doL1_Pressed"); }
    void doR1_Pressed() override { rec("doR1_Pressed"); }
    void doL2_Pressed() override { rec("doL2_Pressed"); }
    void doR2_Pressed() override { rec("doR2_Pressed"); }

    void doCircle_Released() override { rec("doCircle_Released"); }
    void doCross_Released() override { rec("doCross_Released"); }
    void doTriangle_Released() override { rec("doTriangle_Released"); }
    void doSquare_Released() override { rec("doSquare_Released"); }
    void doStart_Released() override { rec("doStart_Released"); }
    void doSelect_Released() override { rec("doSelect_Released"); }
    void doL1_Released() override { rec("doL1_Released"); }
    void doR1_Released() override { rec("doR1_Released"); }
    void doL2_Released() override { rec("doL2_Released"); }
    void doR2_Released() override { rec("doR2_Released"); }

    void doKeyUp() override { rec("doKeyUp"); }
    void doKeyDown() override { rec("doKeyDown"); }
    void doKeyRight() override { rec("doKeyRight"); }
    void doKeyLeft() override { rec("doKeyLeft"); }
    void doPageDown() override { rec("doPageDown"); }
    void doPageUp() override { rec("doPageUp"); }
    void doHome() override { rec("doHome"); }
    void doEnd() override { rec("doEnd"); }
    void doEnter() override { rec("doEnter"); }
    void doDelete() override { rec("doDelete"); }
    void doBackspace() override { rec("doBackspace"); }
    void doTab() override { rec("doTab"); }
    void doEscape() override { rec("doEscape"); }
    void doTextInput(const string &t) override { calls.push_back("doTextInput:" + t); }
};

// a screen on the old loop (ableem::GuiScreen::loop)
struct OldScreen : Recording<ableem::GuiScreen> {
    explicit OldScreen(GuiBase &gui) : Recording<ableem::GuiScreen>(gui) {}
    int frames = 0;
    void render() override { frames++; }
};

// a screen on the new one (abgui::Screen::loop, the default onAction/onUnmapped)
struct NewScreen : Recording<abgui::Screen> {
    NewScreen(GuiBase &gui, Context &ctx) : Recording<abgui::Screen>(gui, ctx) {}
    int frames = 0;
    void draw() override { frames++; }
};

// the program's side of a new screen: a Context over the GuiBase with a stack on a counting display
struct NewSide {
    CountingDisplay display;
    ScreenStack stack{display};
    Context ctx;
    explicit NewSide(GuiBase &gui) : ctx(gui.renderer(), gui.input(), gui.platform()) { ctx.setStack(stack); }
};

// the events, then a Quit (which closes the screen at the end of the batch), into the Input; then the screen's loop
template <class S> void play(GuiBase &gui, S &screen, const vector<Event> &events) {
    for (const Event &e : events)
        gui.input().inject(e);
    gui.input().inject(ofType(Event::Type::Quit));
    screen.loop();
}

struct Setup {
    int holdSteps = 0;
    unsigned holdPace = 0;
    bool circleCloses = false;
};

template <class S> void prepare(S &screen, const Setup &setup) {
    screen.holdSteps = setup.holdSteps;
    screen.holdPace = setup.holdPace;
    screen.circleCloses = setup.circleCloses;
}

// both loops over the same events: the same hooks, in the same order, and as many frames; returns the hooks
string both(GuiBase &gui, const vector<Event> &events, const Setup &setup = Setup()) {
    OldScreen before(gui);
    prepare(before, setup);
    play(gui, before, events);

    NewSide side(gui);
    NewScreen after(gui, side.ctx);
    prepare(after, setup);
    play(gui, after, events);

    CHECK(joined(after.calls) == joined(before.calls));
    CHECK(after.frames == before.frames);
    CHECK(side.display.presents == after.frames); // every frame through the stack: one clear, one present
    CHECK(side.display.clears == after.frames);
    CHECK(after.stepTimes.size() == before.stepTimes.size());
    return joined(after.calls);
}

struct KeyboardAsPad {
    GuiBase &gui;
    bool was;
    KeyboardAsPad(GuiBase &g, bool on) : gui(g), was(g.input().keyboardAsPad()) { gui.input().setKeyboardAsPad(on); }
    ~KeyboardAsPad() { gui.input().setKeyboardAsPad(was); }
};

} // namespace

TEST_CASE("every pad button's press and release reaches the same hook on the new loop") {
    MaybeGui g;
    if (!g.available())
        return;
    const pair<Button, const char *> buttons[] = {
        {Button::Cross, "Cross"},   {Button::Circle, "Circle"}, {Button::Triangle, "Triangle"},
        {Button::Square, "Square"}, {Button::Start, "Start"},   {Button::Select, "Select"},
        {Button::L1, "L1"},         {Button::R1, "R1"},         {Button::L2, "L2"},
        {Button::R2, "R2"}};
    for (const auto &b : buttons) {
        CAPTURE(b.second);
        const string name = b.second;
        CHECK(both(*g.gui, {button(true, b.first), button(false, b.first)}) ==
              "do" + name + "_Pressed do" + name + "_Released");
    }
    // a button no hook takes (a pad's Guide), a device coming and going: nothing
    CHECK(both(*g.gui, {button(true, Button::None), button(false, Button::None)}) == "");
    CHECK(both(*g.gui, {ofType(Event::Type::PadAdded), ofType(Event::Type::PadRemoved)}) == "");
    // all of them at once, overlapping
    CHECK(both(*g.gui, {button(true, Button::L2), button(true, Button::R2), button(false, Button::L2),
                        button(false, Button::R2), button(true, Button::Cross), button(false, Button::Cross)}) ==
          "doL2_Pressed doR2_Pressed doL2_Released doR2_Released doCross_Pressed doCross_Released");
}

TEST_CASE("the d-pad reaches the same hooks by its live state, press and release, with the old priority") {
    MaybeGui g;
    if (!g.available())
        return;
    CHECK(both(*g.gui, {dpad(true, Button::DpadUp), dpad(false, Button::DpadUp)}) == "doJoyUp doJoyCenter");
    CHECK(both(*g.gui, {dpad(true, Button::DpadDown), dpad(false, Button::DpadDown)}) == "doJoyDown doJoyCenter");
    CHECK(both(*g.gui, {dpad(true, Button::DpadLeft), dpad(false, Button::DpadLeft)}) == "doJoyLeft doJoyCenter");
    CHECK(both(*g.gui, {dpad(true, Button::DpadRight), dpad(false, Button::DpadRight)}) == "doJoyRight doJoyCenter");
    // two held: whichever the priority says (up, down, right, left), not the one the event changed
    CHECK(both(*g.gui, {dpad(true, Button::DpadUp), dpad(true, Button::DpadLeft), dpad(false, Button::DpadUp),
                        dpad(false, Button::DpadLeft)}) == "doJoyUp doJoyUp doJoyLeft doJoyCenter");
    CHECK(both(*g.gui, {dpad(true, Button::DpadRight), dpad(true, Button::DpadDown), dpad(false, Button::DpadRight),
                        dpad(false, Button::DpadDown)}) == "doJoyRight doJoyDown doJoyDown doJoyCenter");
    // a button pressed while the d-pad is held
    CHECK(both(*g.gui, {dpad(true, Button::DpadUp), button(true, Button::Cross), button(false, Button::Cross),
                        dpad(false, Button::DpadUp)}) == "doJoyUp doCross_Pressed doCross_Released doJoyCenter");
}

TEST_CASE("with the keyboard-as-pad off every key reaches its own hook, and a release none") {
    MaybeGui g;
    if (!g.available())
        return;
    KeyboardAsPad off(*g.gui, false);
    const pair<Key, const char *> keys[] = {{Key::Up, "doKeyUp"},
                                            {Key::Down, "doKeyDown"},
                                            {Key::Right, "doKeyRight"},
                                            {Key::Left, "doKeyLeft"},
                                            {Key::PageDown, "doPageDown"},
                                            {Key::PageUp, "doPageUp"},
                                            {Key::Home, "doHome"},
                                            {Key::End, "doEnd"},
                                            {Key::Return, "doEnter"},
                                            {Key::Delete, "doDelete"},
                                            {Key::Backspace, "doBackspace"},
                                            {Key::Tab, "doTab"},
                                            {Key::Escape, "doEscape"},
                                            {Key::Other, ""},
                                            {Key::Sleep, ""},
                                            {Key::Reset, ""},
                                            {Key::Open, ""},
                                            {Key::Insert, ""},
                                            {Key::F1, ""},
                                            {Key::F2, ""},
                                            {Key::F3, ""},
                                            {Key::F4, ""},
                                            {Key::F5, ""},
                                            {Key::F6, ""},
                                            {Key::F7, ""},
                                            {Key::F8, ""},
                                            {Key::F9, ""},
                                            {Key::F10, ""},
                                            {Key::F11, ""},
                                            {Key::F12, ""}};
    for (const auto &k : keys) {
        CAPTURE(static_cast<int>(k.first));
        CHECK(both(*g.gui, {key(true, k.first), key(false, k.first)}) == k.second);
    }
    // the Space bar is Extra in the map, but as a key it has no hook; a letter neither
    CHECK(both(*g.gui, {key(true, Key::Other, ' '), key(false, Key::Other, ' ')}) == "");
    CHECK(both(*g.gui, {key(true, Key::Other, 'a'), key(false, Key::Other, 'a')}) == "");
    // a key held: each of its repeats is a press of its hook, the release none
    CHECK(both(*g.gui, {key(true, Key::Down), key(true, Key::Down), key(true, Key::Down), key(false, Key::Down)}) ==
          "doKeyDown doKeyDown doKeyDown");
    // typed text
    CHECK(both(*g.gui, {key(true, Key::Other, 'z'), text("z"), key(false, Key::Other, 'z'), text("\xc5\xbc")}) ==
          "doTextInput:z doTextInput:\xc5\xbc");
}

TEST_CASE("with the keyboard-as-pad on the keys reach the same hooks on both loops") {
    MaybeGui g;
    if (!g.available())
        return;
    KeyboardAsPad on(*g.gui, true);
    const Key keys[] = {Key::Up,     Key::Down, Key::Left,   Key::Right,    Key::Return, Key::Backspace,
                        Key::Escape, Key::Tab,  Key::PageUp, Key::PageDown, Key::Home,   Key::End,
                        Key::Delete, Key::F1,   Key::F2,     Key::F10,      Key::Insert};
    for (Key k : keys) {
        CAPTURE(static_cast<int>(k));
        both(*g.gui, {key(true, k), key(false, k)});
    }
    both(*g.gui, {key(true, Key::Other, ' '), key(false, Key::Other, ' ')});
    both(*g.gui, {key(true, Key::Other, 'x'), text("x"), key(false, Key::Other, 'x')});
}

TEST_CASE("a hold-repeat inside a hook runs the same steps at the same pace and ends on the release") {
    MaybeGui g;
    if (!g.available())
        return;
    Setup hold;
    hold.holdSteps = 3;
    hold.holdPace = 40;
    CHECK(both(*g.gui, {dpad(true, Button::DpadDown)}, hold) == "doJoyDown step step step doJoyCenter");

    // the pace: every step at least holdPace after the one before, on both loops
    OldScreen before(*g.gui);
    prepare(before, hold);
    play(*g.gui, before, {dpad(true, Button::DpadDown)});
    NewSide side(*g.gui);
    NewScreen after(*g.gui, side.ctx);
    prepare(after, hold);
    play(*g.gui, after, {dpad(true, Button::DpadDown)});
    REQUIRE(before.stepTimes.size() == 3);
    REQUIRE(after.stepTimes.size() == 3);
    for (size_t i = 1; i < 3; i++) {
        CHECK(before.stepTimes[i] - before.stepTimes[i - 1] >= hold.holdPace);
        CHECK(after.stepTimes[i] - after.stepTimes[i - 1] >= hold.holdPace);
    }
}

TEST_CASE("a screen closed by a hook still gets the rest of the batch, as on the old loop") {
    MaybeGui g;
    if (!g.available())
        return;
    Setup closes;
    closes.circleCloses = true;
    CHECK(both(*g.gui,
               {button(true, Button::Circle), button(false, Button::Circle), button(true, Button::Cross),
                button(false, Button::Cross)},
               closes) == "doCircle_Pressed doCircle_Released doCross_Pressed doCross_Released");
}

TEST_CASE("the Confirm/Back swap exchanges Cross and Circle's hooks for the pad only") {
    MaybeGui g;
    if (!g.available())
        return;
    NewSide side(*g.gui);
    side.ctx.actions.setSwapConfirmBack(true);
    NewScreen screen(*g.gui, side.ctx);
    play(*g.gui, screen,
         {button(true, Button::Circle), button(false, Button::Circle), button(true, Button::Cross),
          button(false, Button::Cross), button(true, Button::Triangle), button(false, Button::Triangle)});
    CHECK(joined(screen.calls) ==
          "doCross_Pressed doCross_Released doCircle_Pressed doCircle_Released doTriangle_Pressed doTriangle_Released");

    // Enter and Esc as keys keep their hooks
    KeyboardAsPad off(*g.gui, false);
    NewScreen typing(*g.gui, side.ctx);
    play(*g.gui, typing,
         {key(true, Key::Return), key(false, Key::Return), key(true, Key::Escape), key(false, Key::Escape)});
    CHECK(joined(typing.calls) == "doEnter doEscape");
}

TEST_CASE("a rebound button reaches the hook of its new action; First and Last are L1 and R1's") {
    MaybeGui g;
    if (!g.available())
        return;
    NewSide side(*g.gui);
    side.ctx.actions.bind(Button::Square, Action::Menu);
    side.ctx.actions.bind(Button::L1, Action::First);
    side.ctx.actions.bind(Button::R1, Action::Last);
    side.ctx.actions.bind(Button::Select, Action::None); // unbound: its old hook, as an unmapped event
    NewScreen screen(*g.gui, side.ctx);
    play(*g.gui, screen,
         {button(true, Button::Square), button(false, Button::Square), button(true, Button::L1),
          button(false, Button::L1), button(true, Button::R1), button(false, Button::R1), button(true, Button::Select),
          button(false, Button::Select)});
    CHECK(joined(screen.calls) == "doStart_Pressed doStart_Released doL1_Pressed doL1_Released doR1_Pressed "
                                  "doR1_Released doSelect_Pressed doSelect_Released");
}

namespace {

// a screen that reads actions itself
struct ActionScreen : NewScreen {
    using NewScreen::NewScreen;
    vector<string> seen;
    void onAction(const ActionEvent &a) override {
        seen.push_back(to_string(static_cast<int>(a.action)) + (a.pressed ? "+" : "") + (a.released ? "-" : ""));
    }
    void onUnmapped(const Event &e) override { seen.push_back("other:" + to_string(static_cast<int>(e.type))); }
};

} // namespace

TEST_CASE("a screen that overrides onAction gets the actions, from the pad and the keys alike, and no hooks") {
    MaybeGui g;
    if (!g.available())
        return;
    KeyboardAsPad off(*g.gui, false);
    NewSide side(*g.gui);
    ActionScreen screen(*g.gui, side.ctx);
    play(*g.gui, screen,
         {button(true, Button::Cross), button(false, Button::Cross), key(true, Key::Return), key(false, Key::Return),
          dpad(true, Button::DpadLeft), dpad(false, Button::DpadLeft), key(true, Key::Delete), key(false, Key::Delete),
          text("q")});
    const string confirm = to_string(static_cast<int>(Action::Confirm));
    const string left = to_string(static_cast<int>(Action::Left));
    const string keyDown = to_string(static_cast<int>(Event::Type::KeyDown));
    const string keyUp = to_string(static_cast<int>(Event::Type::KeyUp));
    const string textInput = to_string(static_cast<int>(Event::Type::TextInput));
    CHECK(joined(screen.seen) == confirm + "+ " + confirm + "- " + confirm + "+ " + confirm + "- " + left + "+ " +
                                     left + "- other:" + keyDown + " other:" + keyUp + " other:" + textInput);
    CHECK(screen.calls.empty());
}

TEST_CASE("render() is one frame through the Context's stack") {
    MaybeGui g;
    if (!g.available())
        return;
    NewSide side(*g.gui);
    NewScreen screen(*g.gui, side.ctx);
    screen.render();
    screen.render();
    CHECK(screen.frames == 2);
    CHECK(side.display.clears == 2);
    CHECK(side.display.presents == 2);
    CHECK(side.stack.presented() == 2);
}
