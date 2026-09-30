//
// abgui::ScreenStack (G3c of docs/ab-gui-plan.md): "screens draw, the stack presents". frame(draw) is clear, the
// screen's drawing, present - in that order, one present a frame, a nested frame (a busy spinner's tick from inside
// a screen's drawing) a frame of its own, and a drawing that throws never presented. The contract is checked on a
// recording display (pure); then on a real renderer (a headless GuiBase, those cases skip themselves without one,
// like test_busy_input): the frames it counts, the DebugDriver's frame cache seeing the stack's frame, and a
// captureNextFrame() before a frame (Gui::beginBusy's backdrop) capturing exactly that frame.
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>
#include <ab_gui/screen_stack.h>

#include <ableem/ui/gui_base.h>
#include <ableem/ui/texture.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using abgui::Context;
using abgui::ScreenStack;
using ableem::Color;
using ableem::GuiBase;
using ableem::Rect;

namespace {

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
            gui = make_unique<GuiBase>("ab_gui_test_screen_stack", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_screen_stack: skipping - no usable renderer here (" << why << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

bool sameColor(const Color &a, const Color &b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// the colour of output pixel (x, y) in a frame copied as ARGB8888
Color pixelAt(const vector<unsigned char> &pixels, int pitch, int x, int y) {
    const size_t at = static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
    // ARGB8888 as a 32-bit word: B, G, R, A in memory on a little-endian machine
    return Color(pixels[at + 2], pixels[at + 1], pixels[at + 0], 255);
}

bool rgbEqual(const Color &a, const Color &b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// the frame the DebugDriver's cache kept of the next present (its `shot`); false when none was copied
bool copyNextFrame(ableem::Renderer &renderer, ScreenStack &stack, const Color &clearColor,
                   const ScreenStack::Draw &draw, vector<unsigned char> &pixels, int &pitch) {
    renderer.setFrameCache(true);
    const unsigned long asked = renderer.requestFrameCopy();
    stack.frame(clearColor, draw);
    int w = 0, h = 0;
    const unsigned long copied = renderer.copyLastFrame(pixels, w, h, pitch);
    renderer.setFrameCache(false);
    return copied > asked && w == 320 && h == 240;
}

} // namespace

TEST_CASE("a frame is clear, the drawing, present - one present") {
    Recorder display;
    ScreenStack stack(display);
    CHECK(stack.depth() == 0);
    CHECK(stack.presented() == 0);

    int depthInside = -1;
    stack.frame([&]() {
        display.calls.push_back("draw");
        depthInside = stack.depth();
    });
    CHECK(display.calls == vector<string>{"clear", "draw", "present"});
    CHECK(depthInside == 1);
    CHECK(stack.depth() == 0);
    CHECK(stack.presented() == 1);

    // one present per frame, every frame
    display.calls.clear();
    stack.frame([&]() { display.calls.push_back("draw"); });
    stack.frame([&]() { display.calls.push_back("draw"); });
    CHECK(display.calls == vector<string>{"clear", "draw", "present", "clear", "draw", "present"});
    CHECK(stack.presented() == 3);
}

TEST_CASE("a frame with a clear colour sets it before the clear") {
    Recorder display;
    ScreenStack stack(display);
    stack.frame(Color(1, 2, 3, 4), [&]() { display.calls.push_back("draw"); });
    CHECK(display.calls == vector<string>{"color", "clear", "draw", "present"});
    CHECK(sameColor(display.lastColor, Color(1, 2, 3, 4)));

    // without one the colour is left alone: the clear is in whatever the draw colour is (a screen's own clear())
    display.calls.clear();
    stack.frame([]() {});
    CHECK(display.calls == vector<string>{"clear", "present"});
}

TEST_CASE("an empty drawing is still a frame: a black screen shown once") {
    Recorder display;
    ScreenStack stack(display);
    stack.frame(Color(0, 0, 0, 255), ScreenStack::Draw());
    CHECK(display.calls == vector<string>{"color", "clear", "present"});
    CHECK(stack.presented() == 1);
}

TEST_CASE("a frame started inside another's drawing is a frame of its own; the outer presents at its end") {
    // Gui::drawBusyFrame from a busy tick inside a load a screen's drawing started: the spinner's frame is shown
    // at once, as it always was, and the screen's frame still presents once when its drawing is done
    Recorder display;
    ScreenStack stack(display);
    int depthInner = -1, depthAfterInner = -1;
    stack.frame([&]() {
        display.calls.push_back("outer");
        stack.frame(Color(0, 0, 0, 255), [&]() {
            display.calls.push_back("inner");
            depthInner = stack.depth();
        });
        depthAfterInner = stack.depth();
        display.calls.push_back("outer again");
    });
    CHECK(display.calls ==
          vector<string>{"clear", "outer", "color", "clear", "inner", "present", "outer again", "present"});
    CHECK(depthInner == 2);
    CHECK(depthAfterInner == 1);
    CHECK(stack.depth() == 0);
    CHECK(stack.presented() == 2);
}

TEST_CASE("a drawing that throws is not presented, and the stack is between frames again") {
    Recorder display;
    ScreenStack stack(display);
    CHECK_THROWS_AS(stack.frame([]() { throw runtime_error("drawing failed"); }), runtime_error);
    CHECK(display.calls == vector<string>{"clear"});
    CHECK(stack.depth() == 0);
    CHECK(stack.presented() == 0);

    // the next frame is a whole one
    stack.frame([]() {});
    CHECK(display.calls == vector<string>{"clear", "clear", "present"});
    CHECK(stack.presented() == 1);
}

TEST_CASE("a Context hands out the stack it was given") {
    Recorder display;
    ScreenStack stack(display);
    MaybeGui g;
    if (!g.available())
        return;
    Context ctx(g.gui->renderer());
    CHECK_FALSE(ctx.hasStack());
    ctx.setStack(stack);
    REQUIRE(ctx.hasStack());
    CHECK(&ctx.stack() == &stack);
}

TEST_CASE("on a renderer: one presented frame per frame(), none while drawing, the clear colour left set") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Renderer &renderer = g.gui->renderer();
    ScreenStack stack(renderer);
    const unsigned long before = renderer.frameCount();
    unsigned long during = 0;
    stack.frame(Color(10, 20, 30, 255), [&]() { during = renderer.frameCount(); });
    CHECK(during == before);
    CHECK(renderer.frameCount() == before + 1);
    CHECK(sameColor(renderer.drawColor(), Color(10, 20, 30, 255)));

    // a nested frame is one more present, not a double one of the outer
    stack.frame([&]() { stack.frame([]() {}); });
    CHECK(renderer.frameCount() == before + 3);
    CHECK(stack.presented() == 3);
}

TEST_CASE("on a renderer: the DebugDriver's frame copy is the stack's frame - the clear under the drawing") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Renderer &renderer = g.gui->renderer();
    ScreenStack stack(renderer);
    const Color blue(0, 0, 255, 255), green(0, 255, 0, 255);
    vector<unsigned char> pixels;
    int pitch = 0;
    const bool copied = copyNextFrame(
        renderer, stack, blue,
        [&]() {
            renderer.setDrawColor(green);
            renderer.fillRect(Rect(100, 100, 50, 50));
        },
        pixels, pitch);
    if (!copied) {
        MESSAGE("test_ab_gui_screen_stack: no frame copy from this renderer - pixel checks skipped");
        return;
    }
    CHECK(rgbEqual(pixelAt(pixels, pitch, 5, 5), blue));      // the clear
    CHECK(rgbEqual(pixelAt(pixels, pitch, 120, 120), green)); // the drawing, over it
    CHECK(rgbEqual(pixelAt(pixels, pitch, 310, 230), blue));
}

TEST_CASE("on a renderer: captureNextFrame() before a frame captures that frame (a busy backdrop)") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Renderer &renderer = g.gui->renderer();
    ScreenStack stack(renderer);
    const Color red(255, 0, 0, 255), white(255, 255, 255, 255);

    // Gui::beginBusy: capture, then the screen's own frame through the stack
    renderer.captureNextFrame();
    stack.frame(red, [&]() {
        renderer.setDrawColor(white);
        renderer.fillRect(Rect(0, 0, 40, 40));
    });
    const ableem::Texture backdrop = renderer.lastCapture();
    REQUIRE(backdrop.valid());

    // Gui::drawBusyFrame: black, the backdrop over the whole canvas - the same picture again
    vector<unsigned char> pixels;
    int pitch = 0;
    const bool copied = copyNextFrame(
        renderer, stack, Color(0, 0, 0, 255), [&]() { renderer.copy(backdrop, nullptr, nullptr); }, pixels, pitch);
    if (!copied) {
        MESSAGE("test_ab_gui_screen_stack: no frame copy from this renderer - pixel checks skipped");
        return;
    }
    CHECK(rgbEqual(pixelAt(pixels, pitch, 20, 20), white));
    CHECK(rgbEqual(pixelAt(pixels, pitch, 200, 200), red));
}
