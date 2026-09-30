//
// ab_gui icons (G5a of docs/ab-gui-plan.md): the 1x/@2x pick FrameSet and IconSet share, the IconSet's specs and halo
// switch - pure; then on a real renderer (a headless GuiBase, those cases skip themselves without one) the test theme's
// icons loading - the 1x at scale 1 in its logical size, an @2x-only icon at pixel scale 2 in the same logical size,
// the halo 5 px bigger, none with the halos off, a missing file no icon - the Context's icon and halo providers, and
// the frame helpers G5 adds to Style: drawFrame at an alpha and drawFirstFrame.
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>
#include <ab_gui/frame.h>
#include <ab_gui/icon.h>
#include <ab_gui/style.h>

#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::IconSet;
using abgui::IconSpec;
using ableem::Rect;

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
            gui = make_unique<ableem::GuiBase>("ab_gui_test_icon", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_icon: skipping - no usable renderer here (" << why << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

// the test theme's icons (tests/data/frame-test-theme/icons, make_test_icons.py): orange at 1x, sky blue at @2x
string testIcon(const string &file) {
    return string(AB_TEST_DATA_DIR) + "/frame-test-theme/icons/" + file;
}

string testFrame(const string &file) {
    return string(AB_TEST_DATA_DIR) + "/frame-test-theme/frames/" + file;
}

IconSpec spec(const string &file, const string &file2x = "") {
    IconSpec s;
    s.file = file;
    s.file2x = file2x;
    return s;
}

} // namespace

TEST_CASE("pickImageFile: frames and icons pick alike - the @2x above scale 1, else the 1x, else the @2x everywhere") {
    float scale = 0;
    CHECK(abgui::pickImageFile("a.png", "a@2x.png", 1.0f, scale) == "a.png");
    CHECK(scale == 1.0f);
    CHECK(abgui::pickImageFile("a.png", "a@2x.png", 1.5f, scale) == "a@2x.png");
    CHECK(scale == 2.0f);
    CHECK(abgui::pickImageFile("a.png", "", 1.5f, scale) == "a.png");
    CHECK(scale == 1.0f);
    CHECK(abgui::pickImageFile("", "a@2x.png", 1.0f, scale) == "a@2x.png");
    CHECK(scale == 2.0f);
    CHECK(abgui::pickImageFile("", "", 1.5f, scale).empty());
    CHECK(scale == 1.0f);

    // the two sets' own pickFile are the same rule
    const IconSpec both = spec("i.png", "i@2x.png");
    CHECK(IconSet::pickFile(both, 1.0f, scale) == "i.png");
    CHECK(IconSet::pickFile(both, 1.5f, scale) == "i@2x.png");
    CHECK(scale == 2.0f);
    abgui::FrameSpec frame;
    frame.file = "f.png";
    frame.file2x = "f@2x.png";
    CHECK(abgui::FrameSet::pickFile(frame, 1.5f, scale) == "f@2x.png");
    CHECK(abgui::FrameSet::pickFile(frame, 1.0f, scale) == "f.png");
}

TEST_CASE("IconSet: the specs by name, the halo switch; assign replaces both, release keeps them") {
    IconSet set;
    CHECK(set.empty());
    CHECK(set.haloOn());
    map<string, IconSpec> specs;
    specs["dpadUp"] = spec("/x/up.png", "/x/up@2x.png");
    specs["disc"] = spec("/x/cd.png");
    set.assign(specs);
    CHECK_FALSE(set.empty());
    CHECK(set.has("dpadUp"));
    CHECK(set.has("disc"));
    CHECK_FALSE(set.has("dpadDown"));
    CHECK(set.spec("dpadUp").file == "/x/up.png");
    CHECK(set.spec("dpadUp").file2x == "/x/up@2x.png");
    CHECK(set.spec("dpadDown").file.empty());
    CHECK(set.haloOn());
    set.release();
    CHECK(set.has("disc"));

    set.assign(specs, false);
    CHECK_FALSE(set.haloOn());
    set.assign(map<string, IconSpec>());
    CHECK(set.empty());
    CHECK(set.haloOn());
}

TEST_CASE("IconSet on a renderer: the test theme's icons in their logical size, their halos, a bad icon is none") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    REQUIRE(renderer.outputScale() == 1.0f);

    map<string, IconSpec> specs;
    specs["dpadUp"] = spec(testIcon("dpad_up.png"), testIcon("dpad_up@2x.png"));
    specs["battery"] = spec(testIcon("battery.png"));
    specs["only2x"] = spec("", testIcon("switch_on@2x.png"));
    specs["missing"] = spec(testIcon("no-such.png"));
    IconSet set;
    set.assign(specs);

    // scale 1: the 1x file, its own size
    const ableem::Texture up = set.icon(renderer, "dpadUp");
    REQUIRE(up.valid());
    CHECK(up.size().w == 28);
    CHECK(up.size().h == 28);
    CHECK(up.pixelScale() == 1.0f);
    const ableem::Texture battery = set.icon(renderer, "battery");
    REQUIRE(battery.valid());
    CHECK(battery.size().w == 29);
    CHECK(battery.size().h == 13);
    // an @2x-only icon is drawn everywhere at pixel scale 2: its size is still the logical 60x30
    const ableem::Texture two = set.icon(renderer, "only2x");
    REQUIRE(two.valid());
    CHECK(two.pixelScale() == 2.0f);
    CHECK(two.size().w == 60);
    CHECK(two.size().h == 30);

    // the halo: the icon's size plus 5 (Style::outlineOf), made from the 1x file
    const ableem::Texture halo = set.halo(renderer, "dpadUp");
    REQUIRE(halo.valid());
    CHECK(halo.size().w == 33);
    CHECK(halo.size().h == 33);

    CHECK_FALSE(set.icon(renderer, "missing").valid());
    CHECK_FALSE(set.icon(renderer, "missing").valid()); // remembered, not retried
    CHECK_FALSE(set.halo(renderer, "missing").valid());
    CHECK_FALSE(set.icon(renderer, "disc").valid()); // not in the set
    CHECK_FALSE(set.halo(renderer, "disc").valid());

    set.release();
    CHECK(set.icon(renderer, "dpadUp").valid()); // loaded again after a release
    CHECK(set.halo(renderer, "dpadUp").valid());

    // the halos off (a theme's "iconHalo": false): the icons stay, the halos go
    set.assign(specs, false);
    CHECK(set.icon(renderer, "dpadUp").valid());
    CHECK_FALSE(set.halo(renderer, "dpadUp").valid());
}

TEST_CASE("loadIcon / loadIconHalo: what ThemeAssets loads the d-pad arrows with") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    const ableem::Texture icon =
        abgui::loadIcon(renderer, spec(testIcon("dpad_left.png"), testIcon("dpad_left@2x.png")));
    REQUIRE(icon.valid());
    CHECK(icon.pixelScale() == 1.0f); // scale 1: the 1x file
    CHECK(icon.size().w == 28);
    const ableem::Texture halo = abgui::loadIconHalo(renderer, spec(testIcon("dpad_left.png")));
    REQUIRE(halo.valid());
    CHECK(halo.size().w == 33);
    CHECK_FALSE(abgui::loadIcon(renderer, IconSpec()).valid());
    CHECK_FALSE(abgui::loadIconHalo(renderer, IconSpec()).valid());
}

TEST_CASE("Context::icon / iconHalo: the providers when set, else no icon") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    CHECK_FALSE(ctx.icon("dpadUp").valid());
    CHECK_FALSE(ctx.iconHalo("dpadUp").valid());

    IconSet set;
    map<string, IconSpec> specs;
    specs["dpadUp"] = spec(testIcon("dpad_up.png"));
    set.assign(specs);
    vector<string> asked;
    ctx.iconProvider = [&](const string &name) {
        asked.push_back(name);
        return set.icon(renderer, name);
    };
    ctx.iconHaloProvider = [&](const string &name) { return set.halo(renderer, name); };
    CHECK(ctx.icon("dpadUp").valid());
    CHECK(ctx.iconHalo("dpadUp").valid());
    CHECK_FALSE(ctx.icon("tabApps").valid());
    REQUIRE(asked.size() == 2);
    CHECK(asked[0] == "dpadUp");
    CHECK(asked[1] == "tabApps");
}

TEST_CASE("Style::drawFrame at an alpha and drawFirstFrame: the first frame the Context has, else none") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect box(20, 20, 200, 120);

    // no provider: nothing drawn, whatever is asked
    CHECK_FALSE(style.drawFrame(ctx, "panel", box, 128));
    CHECK_FALSE(style.drawFirstFrame(ctx, {"toast", "panel"}, box));

    abgui::FrameSet set;
    abgui::FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = abgui::Insets::all(24);
    panel.bleed = abgui::Insets::all(8);
    map<string, abgui::FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };

    CHECK(style.drawFrame(ctx, "panel", box, 100));
    CHECK(style.drawFrame(ctx, "panel", box)); // and back at full alpha
    CHECK_FALSE(style.drawFrame(ctx, "toast", box, 100));

    // the toast falls back to the panel: both asked, in order, the panel drawn
    asked.clear();
    CHECK(style.drawFirstFrame(ctx, {"toast", "panel"}, box));
    REQUIRE(asked.size() == 2);
    CHECK(asked[0] == "toast");
    CHECK(asked[1] == "panel");
    // the first one there is drawn, the rest not asked
    asked.clear();
    CHECK(style.drawFirstFrame(ctx, {"panel", "toast"}, box));
    REQUIRE(asked.size() == 1);
    // none there: false
    CHECK_FALSE(style.drawFirstFrame(ctx, {"toast", "chip"}, box));
    CHECK_FALSE(style.drawFirstFrame(ctx, {}, box));
}
