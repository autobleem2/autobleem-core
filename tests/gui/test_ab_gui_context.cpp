//
// abgui::Context's non-drawing half (G3a of docs/ab-gui-plan.md): the input the screens read, the clock they time
// by and the UI sounds they play - what the widgets moving into ab_gui (G3) reach instead of Gui/AppBase. A
// Context needs a real Renderer (only a GuiBase makes one), so like test_busy_input.cpp this builds a headless
// GuiBase and skips itself where the environment has no renderer.
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>

#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

using namespace std;
using abgui::Context;
using abgui::UiSound;
using ableem::GuiBase;

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
            gui = make_unique<GuiBase>("ab_gui_test_context", 320, 240);
        } catch (const exception &e) {
            MESSAGE("test_ab_gui_context: skipping - no usable renderer in this environment (" << e.what() << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

} // namespace

TEST_CASE("a Context built from a GuiBase hands out its input and times by its platform") {
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer(), g.gui->input(), g.gui->platform());
    REQUIRE(ctx.hasInput());
    CHECK(&ctx.input() == &g.gui->input());
    CHECK(&ctx.renderer() == &g.gui->renderer());

    const unsigned int before = g.gui->platform().ticks();
    CHECK(ctx.ticks() >= before);
    ctx.delay(20);
    // SDL_Delay waits at least as long as asked; a little slack for the tick counter's rounding
    CHECK(ctx.ticks() - before >= 15);
}

TEST_CASE("a clock of the program's wins over the platform's ticks") {
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer(), g.gui->input(), g.gui->platform());
    unsigned int now = 123456;
    ctx.clock = [&now]() { return now; };
    CHECK(ctx.ticks() == 123456);
    now += 40;
    CHECK(ctx.ticks() == 123496);
}

TEST_CASE("a drawing-only Context has no input, no ticks but a set clock's, and waits for nothing") {
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer());
    CHECK_FALSE(ctx.hasInput());
    CHECK(ctx.ticks() == 0);
    ctx.delay(10000); // no platform: returns at once (the test would time out otherwise)
    ctx.clock = []() { return 7u; };
    CHECK(ctx.ticks() == 7);
}

TEST_CASE("play hands the sound to the program's player; without one it is silent") {
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer(), g.gui->input(), g.gui->platform());
    ctx.play(UiSound::Cursor); // no player: nothing, no crash

    vector<UiSound> played;
    ctx.soundPlayer = [&played](UiSound sound) { played.push_back(sound); };
    ctx.play(UiSound::Cursor);
    ctx.play(UiSound::Cancel);
    ctx.play(UiSound::HomeUp);
    ctx.play(UiSound::HomeDown);
    ctx.play(UiSound::Resume);
    const vector<UiSound> expected{UiSound::Cursor, UiSound::Cancel, UiSound::HomeUp, UiSound::HomeDown,
                                   UiSound::Resume};
    CHECK(played == expected);
}
