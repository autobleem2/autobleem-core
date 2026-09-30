//
// ab_gui's backdrop snapshot (G5r5 of docs/ab-gui-plan.md): the one launcher frame every screen opened from it draws
// over. The keep/lose rules first (pure: nothing kept, an invalid frame kept as nothing, a frame used only while the
// render targets' loss count stands where it was taken, clear), then on a real renderer (a headless GuiBase; those
// cases skip themselves without one) the draw - true over a good frame, false and nothing drawn once the targets were
// lost.
//
#include "doctest/doctest.h"

#include <ab_gui/backdrop.h>

#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

using namespace std;
using abgui::BackdropSnapshot;
using ableem::Texture;

namespace {

struct MaybeGui {
    unique_ptr<ableem::GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<ableem::GuiBase>("ab_gui_test_backdrop", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_backdrop: skipping - no usable renderer here (" << why << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

} // namespace

TEST_CASE("a snapshot starts empty and is never usable") {
    BackdropSnapshot snap;
    CHECK_FALSE(snap.held());
    CHECK_FALSE(snap.usable(0));
    CHECK_FALSE(snap.usable(7));
}

TEST_CASE("an invalid frame keeps nothing, so the screens fall back to the theme's background") {
    BackdropSnapshot snap;
    snap.set(Texture(), 3);
    CHECK_FALSE(snap.held());
    CHECK_FALSE(snap.usable(3));
}

TEST_CASE("a frame is held, usable at the loss count it was taken at and not after a loss") {
    MaybeGui g;
    if (!g.available())
        return;
    BackdropSnapshot snap;
    snap.set(Texture::createTarget(g.gui->renderer(), 64, 48), 5);
    REQUIRE(snap.held());
    CHECK(snap.usable(5));
    CHECK_FALSE(snap.usable(6)); // the targets were lost: the pixels are gone
    CHECK(snap.held());          // still kept - the screens above it still draw without the logo
    snap.clear();
    CHECK_FALSE(snap.held());
    CHECK_FALSE(snap.usable(5));
}

TEST_CASE("draw paints over a good frame and leaves the canvas alone once the targets were lost") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Renderer &renderer = g.gui->renderer();
    BackdropSnapshot snap;
    CHECK_FALSE(snap.draw(renderer)); // nothing kept

    snap.set(Texture::createTarget(renderer, 64, 48), renderer.targetsLost());
    REQUIRE(snap.held());
    renderer.clear();
    CHECK(snap.draw(renderer));

    // a loss since the frame was taken (what SDL_RENDER_TARGETS_RESET counts): draw refuses, never a garbage frame
    snap.set(Texture::createTarget(renderer, 64, 48), renderer.targetsLost() + 1);
    CHECK_FALSE(snap.draw(renderer));
    snap.clear();
    CHECK_FALSE(snap.draw(renderer));
}
