//
// ab_gui's spinner strip (G5p of docs/ab-gui-plan.md): the busy spinner as a theme element - one image of N frames side
// by side. The pure rules first (the frame shown at a time, a frame's rect in the strip, the centred destination), the
// SpinnerStrip's specs, and the Context's provider (none, or an invalid strip = no strip: the ring of dots). Then, on a
// real renderer (a headless GuiBase; those cases skip themselves without one), the test theme's strip loading in its
// logical size, Style::spinnerStrip drawing the frame the clock names - told apart by a pixel only that frame has - and
// Style::spinner through a Context drawing the strip instead of the ring.
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>
#include <ab_gui/screen_stack.h>
#include <ab_gui/spinner.h>
#include <ab_gui/style.h>

#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::Context;
using abgui::ScreenStack;
using abgui::SpinnerAnim;
using abgui::SpinnerSpec;
using abgui::SpinnerStrip;
using abgui::Style;
using ableem::Color;
using ableem::Rect;
using ableem::Size;

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
            gui = make_unique<ableem::GuiBase>("ab_gui_test_spinner", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_spinner: skipping - no usable renderer here (" << why << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

// the test theme's strip (tests/data/frame-test-theme/spinner, make_test_spinner.py): 8 frames of 48x48, orange at 1x
SpinnerSpec testSpec() {
    const string dir = string(AB_TEST_DATA_DIR) + "/frame-test-theme/spinner/";
    SpinnerSpec spec;
    spec.file = dir + "spinner.png";
    spec.file2x = dir + "spinner@2x.png";
    spec.frames = 8;
    spec.fps = 8;
    return spec;
}

Size sizeOf(int w, int h) {
    Size s;
    s.w = w;
    s.h = h;
    return s;
}

bool sameRect(const Rect &a, const Rect &b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// the colour of pixel (x, y) of the frame `draw` presents on a black clear, as {r, g, b}; empty when the renderer
// gives no frame copy back
vector<int> pixelAfter(ableem::Renderer &renderer, ScreenStack &stack, const function<void()> &draw, int x, int y) {
    renderer.setFrameCache(true);
    const unsigned long asked = renderer.requestFrameCopy();
    stack.frame(Color(0, 0, 0, 255), draw);
    vector<unsigned char> pixels;
    int w = 0, h = 0, pitch = 0;
    const unsigned long copied = renderer.copyLastFrame(pixels, w, h, pitch);
    renderer.setFrameCache(false);
    if (copied <= asked || w != 320 || h != 240)
        return vector<int>();
    const size_t at = static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
    return {pixels[at + 2], pixels[at + 1], pixels[at + 0]};
}

bool isOrange(const vector<int> &p) {
    return p.size() == 3 && abs(p[0] - 255) <= 2 && abs(p[1] - 110) <= 2 && abs(p[2] - 0) <= 2;
}

bool isBlack(const vector<int> &p) {
    return p.size() == 3 && p[0] <= 2 && p[1] <= 2 && p[2] <= 2;
}

} // namespace

TEST_CASE("spinnerFrameIndex: (elapsed * fps / 1000) mod frames") {
    CHECK(abgui::spinnerFrameIndex(0, 24, 24) == 0);
    CHECK(abgui::spinnerFrameIndex(41, 24, 24) == 0);  // 0.984
    CHECK(abgui::spinnerFrameIndex(42, 24, 24) == 1);  // 1.008
    CHECK(abgui::spinnerFrameIndex(500, 24, 24) == 12);
    CHECK(abgui::spinnerFrameIndex(999, 24, 24) == 23);
    CHECK(abgui::spinnerFrameIndex(1000, 24, 24) == 0); // wraps
    CHECK(abgui::spinnerFrameIndex(1042, 24, 24) == 1);
    CHECK(abgui::spinnerFrameIndex(125, 8, 8) == 1);
    CHECK(abgui::spinnerFrameIndex(1000, 8, 8) == 0);
    CHECK(abgui::spinnerFrameIndex(3000, 1, 5) == 3);
    // a long job: a day of milliseconds does not overflow the product
    CHECK(abgui::spinnerFrameIndex(86400000ULL, 24, 24) == (86400000ULL * 24 / 1000) % 24);
    CHECK(abgui::spinnerFrameIndex(4000000000ULL, 60, 7) == static_cast<int>(4000000000ULL * 60 / 1000 % 7));
    // no frames or no rate: frame 0, never a division by zero
    CHECK(abgui::spinnerFrameIndex(500, 0, 8) == 0);
    CHECK(abgui::spinnerFrameIndex(500, 24, 0) == 0);
    CHECK(abgui::spinnerFrameIndex(500, -3, 8) == 0);
}

TEST_CASE("spinnerFrameRect: frame i of a strip of N, side by side from the left, the whole height") {
    const Size strip = sizeOf(1536, 64); // the ab2 strip: 24 frames of 64
    CHECK(sameRect(abgui::spinnerFrameRect(strip, 24, 0), Rect(0, 0, 64, 64)));
    CHECK(sameRect(abgui::spinnerFrameRect(strip, 24, 1), Rect(64, 0, 64, 64)));
    CHECK(sameRect(abgui::spinnerFrameRect(strip, 24, 23), Rect(1472, 0, 64, 64)));
    // an index past the end or below 0 wraps
    CHECK(sameRect(abgui::spinnerFrameRect(strip, 24, 24), Rect(0, 0, 64, 64)));
    CHECK(sameRect(abgui::spinnerFrameRect(strip, 24, 25), Rect(64, 0, 64, 64)));
    CHECK(sameRect(abgui::spinnerFrameRect(strip, 24, -1), Rect(1472, 0, 64, 64)));
    // a strip whose width is not a whole number of frames: the leftover columns are never drawn
    CHECK(sameRect(abgui::spinnerFrameRect(sizeOf(100, 30), 8, 7), Rect(84, 0, 12, 30)));
    CHECK(sameRect(abgui::spinnerFrameRect(sizeOf(48, 48), 1, 0), Rect(0, 0, 48, 48)));
    CHECK(sameRect(abgui::spinnerFrameRect(strip, 0, 0), Rect(0, 0, 0, 0)));
}

TEST_CASE("spinnerDestRect: the frame centred on the point, as the dots are") {
    CHECK(sameRect(abgui::spinnerDestRect(sizeOf(64, 64), 640, 340), Rect(608, 308, 64, 64)));
    CHECK(sameRect(abgui::spinnerDestRect(sizeOf(48, 48), 160, 120), Rect(136, 96, 48, 48)));
    // the odd half of a pixel goes right/down
    CHECK(sameRect(abgui::spinnerDestRect(sizeOf(25, 9), 100, 100), Rect(88, 96, 25, 9)));
}

TEST_CASE("SpinnerStrip: usable only with a file, a frame count and a rate; assign replaces, release keeps the spec") {
    SpinnerStrip strip;
    CHECK_FALSE(strip.has());

    SpinnerSpec spec;
    spec.file = "a.png";
    spec.frames = 8;
    spec.fps = 12;
    strip.assign(spec);
    CHECK(strip.has());
    CHECK(strip.spec().file == "a.png");
    CHECK(strip.spec().frames == 8);

    SpinnerSpec only2x = spec;
    only2x.file.clear();
    only2x.file2x = "a@2x.png";
    strip.assign(only2x);
    CHECK(strip.has());

    SpinnerSpec noFile = spec;
    noFile.file.clear();
    strip.assign(noFile);
    CHECK_FALSE(strip.has());
    SpinnerSpec noFrames = spec;
    noFrames.frames = 0;
    strip.assign(noFrames);
    CHECK_FALSE(strip.has());
    SpinnerSpec noRate = spec;
    noRate.fps = 0;
    strip.assign(noRate);
    CHECK_FALSE(strip.has());

    strip.assign(spec);
    strip.release();
    CHECK(strip.has()); // the spec stays: the next ask loads again
    CHECK(strip.spec().fps == 12);
    strip.assign(SpinnerSpec());
    CHECK_FALSE(strip.has());
}

TEST_CASE("SpinnerAnim: a texture that did not load, or no frames or rate, is not a strip") {
    SpinnerAnim anim;
    CHECK_FALSE(anim.valid());
    anim.frames = 8;
    anim.fps = 8;
    CHECK_FALSE(anim.valid()); // no texture
}

TEST_CASE("Context::spinnerAnim: no provider, no strip") {
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer());
    CHECK_FALSE(ctx.spinnerAnim().valid());
    ctx.spinnerProvider = []() { return SpinnerAnim(); };
    CHECK_FALSE(ctx.spinnerAnim().valid());
    // Style::spinnerStrip then draws nothing and says so - the caller draws its ring
    CHECK_FALSE(Style().spinnerStrip(ctx, 100, 100, 0));
}

TEST_CASE("SpinnerStrip on a renderer: the test theme's strip in its logical size, a bad file is no strip") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Renderer &renderer = g.gui->renderer();
    REQUIRE(renderer.outputScale() == 1.0f);

    SpinnerStrip strip;
    strip.assign(testSpec());
    const SpinnerAnim anim = strip.anim(renderer);
    REQUIRE(anim.valid());
    CHECK(anim.frames == 8);
    CHECK(anim.fps == 8);
    CHECK(anim.strip.size().w == 384); // 8 frames of 48
    CHECK(anim.strip.size().h == 48);
    CHECK(anim.strip.pixelScale() == 1.0f);

    // an @2x-only strip is drawn at pixel scale 2 and is still 384x48 logical
    SpinnerSpec only2x = testSpec();
    only2x.file.clear();
    strip.assign(only2x);
    const SpinnerAnim two = strip.anim(renderer);
    REQUIRE(two.valid());
    CHECK(two.strip.pixelScale() == 2.0f);
    CHECK(two.strip.size().w == 384);
    CHECK(two.strip.size().h == 48);

    SpinnerSpec bad = testSpec();
    bad.file = string(AB_TEST_DATA_DIR) + "/frame-test-theme/spinner/no-such.png";
    bad.file2x.clear();
    strip.assign(bad);
    CHECK_FALSE(strip.anim(renderer).valid());
    CHECK_FALSE(strip.anim(renderer).valid()); // remembered, not retried

    // a strip narrower than its frame count cannot be cut
    SpinnerSpec tooMany = testSpec();
    tooMany.frames = 1000;
    strip.assign(tooMany);
    CHECK_FALSE(strip.anim(renderer).valid());

    strip.assign(testSpec());
    CHECK(strip.anim(renderer).valid());
    strip.release();
    CHECK(strip.anim(renderer).valid()); // loaded again after a release
}

TEST_CASE("Style::spinnerStrip and Style::spinner(ctx): the frame the clock names, centred; no strip = the ring") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Renderer &renderer = g.gui->renderer();
    ScreenStack stack(renderer);
    Context ctx(renderer, g.gui->input(), g.gui->platform());
    unsigned int now = 0;
    ctx.clock = [&now]() { return now; };
    ctx.setStack(stack);
    SpinnerStrip strip;
    strip.assign(testSpec());
    ctx.spinnerProvider = [&]() { return strip.anim(renderer); };
    const Style style;

    // frame 0 is drawn centred on (160, 120) - 136..184 x 96..144 - and has one pip at 4..7 x 3..6; frame 1 has a
    // second one at 9..12: the pixel (136 + 10, 96 + 4) is lit in frame 1 only
    const int px = 136 + 10, py = 96 + 4;
    auto frame0 = pixelAfter(renderer, stack, [&]() { REQUIRE(style.spinnerStrip(ctx, 160, 120, 0)); }, px, py);
    if (frame0.empty()) {
        MESSAGE("test_ab_gui_spinner: no frame copy from this renderer - pixel checks skipped");
        return;
    }
    CHECK(isBlack(frame0));
    const auto frame1 = pixelAfter(renderer, stack, [&]() { REQUIRE(style.spinnerStrip(ctx, 160, 120, 125)); }, px, py);
    CHECK(isOrange(frame1));
    // the first pip is in both
    const int firstX = 136 + 5;
    CHECK(isOrange(pixelAfter(renderer, stack, [&]() { style.spinnerStrip(ctx, 160, 120, 0); }, firstX, py)));
    // a full second later the strip has wrapped to frame 0 again
    CHECK(isBlack(pixelAfter(renderer, stack, [&]() { style.spinnerStrip(ctx, 160, 120, 1000); }, px, py)));

    // Style::spinner through the Context plays the strip from the Context's clock, instead of the ring of dots
    now = 125;
    CHECK(isOrange(pixelAfter(renderer, stack, [&]() { style.spinner(ctx, 160, 120, 30, 8, 0); }, px, py)));
    // ... and fitted into a box it is centred in the box
    CHECK(isOrange(pixelAfter(renderer, stack, [&]() { style.spinner(ctx, Rect(60, 20, 200, 200), 0); }, px, py)));

    // without a strip the same call draws the ring: its dots are white text-colour squares, not the strip's orange
    strip.assign(SpinnerSpec());
    now = 0;
    const auto ring = pixelAfter(renderer, stack, [&]() { style.spinner(ctx, 160, 120, 30, 8, 0); }, 160 + 30 - 2, 120);
    REQUIRE_FALSE(ring.empty());
    CHECK(ring[0] >= 250); // the leading dot, opaque white
    CHECK(ring[1] >= 250);
    CHECK(ring[2] >= 250);
}
