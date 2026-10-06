//
// The 4:3 (CRT) output's frames on the renderer this environment has: a 720x480 window, the 1280x720 program, every
// frame through the frame target and the CRT margin (Renderer::mirrorMargin) - margins 0, 5 and 10 %, the rest canvas
// (800x600), a frame's own canvas (640x480), the program's 1280x720, a captured frame, and a display hand-off between.
//
// CRT 4:3 round 2 died here on the console: SDL 2.0.18's SDL_SetTextureScaleMode on one of our RGBA8888 targets (on
// GLES2 SDL keeps it behind a native texture of another format) read the stand-in's missing driver data - a SIGSEGV on
// the first 4:3 frame. The image's own SDL (2.26) has the fix, so on the native gate this suite passes either way; the
// GLES2 gate (the launcher's tests/gles2/crt43_gles2.sh) runs it against the console's SDL 2.0.18 on GLES2, where the
// old code crashed. The second case keeps the call out of the code on every gate.
//
// Like test_busy_input.cpp it needs a real GuiBase (a renderer); it skips itself where there is none, or where the
// window does not come up 720x480.
//
#include "doctest/doctest.h"

#include "ableem/ui/gui_base.h"
#include "ableem/ui/renderer.h"
#include "ableem/ui/texture.h"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>

using namespace std;
using ableem::Color;
using ableem::GuiBase;
using ableem::Rect;
using ableem::Renderer;
using ableem::Texture;

namespace {

struct CrtGui {
    unique_ptr<GuiBase> gui;

    CrtGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
        _putenv_s("AB_WINDOW_SIZE", "720x480");
#else
        setenv("AB_HEADLESS", "1", 1);
        setenv("AB_WINDOW_SIZE", "720x480", 1);
#endif
        try {
            const int w = GuiBase::ScreenWidth, h = GuiBase::ScreenHeight; // copies: make_unique takes references
            gui = make_unique<GuiBase>("ab_core_test_crt_frame", w, h);
        } catch (const exception &e) {
            MESSAGE("test_crt_frame: skipping - no usable renderer in this environment (" << e.what() << ")");
            return;
        }
        if (!gui->renderer().fourByThreeOutput()) {
            MESSAGE("test_crt_frame: skipping - the window is not a 4:3 output here");
            gui.reset();
            return;
        }
        MESSAGE("test_crt_frame: renderer " << gui->renderer().driverName());
        gui->renderer().setFrameCache(true);
    }
    bool available() const { return gui != nullptr; }
    Renderer &renderer() { return gui->renderer(); }
};

// one frame as the screens draw one: a light fill over the whole canvas, a target drawn into and copied, presented
void drawFrame(Renderer &r, Texture &layer) {
    r.setDrawColor(Color(0, 0, 0, 0));
    r.clear();
    r.setDrawColor(Color(200, 200, 200, 255));
    r.fillRect(Rect(0, 0, r.width(), r.height()));
    if (!layer.valid())
        layer = Texture::createTarget(r, 64, 64);
    r.pushTarget(&layer);
    r.setDrawColor(Color(40, 90, 200, 255));
    r.fillRect();
    r.popTarget();
    const Rect box(r.width() / 4, r.height() / 4, r.width() / 2, r.height() / 2);
    r.copy(layer, nullptr, &box);
    r.present();
}

// the output pixel at x, y of the last frame (ARGB8888): its red
int redAt(Renderer &r, int x, int y) {
    r.requestFrameCopy();
    Texture layer;
    drawFrame(r, layer);
    vector<unsigned char> pixels;
    int w = 0, h = 0, pitch = 0;
    if (r.copyLastFrame(pixels, w, h, pitch) == 0 || x >= w || y >= h)
        return -1;
    return pixels[static_cast<size_t>(y) * pitch + x * 4 + 2];
}

} // namespace

TEST_CASE("a 4:3 output presents its frames with the CRT margin 0, 5 and 10 % on every canvas, across a hand-off") {
    CrtGui cg;
    if (!cg.available())
        return;
    Renderer &r = cg.renderer();
    REQUIRE(r.setRestCanvas(800, 600));
    for (int round = 0; round < 2; round++) { // the second round after a display hand-off (a game, a display change)
        for (int margin : {5, 0, 10}) {
            r.setSafeMargin(margin);
            CHECK(r.safeMargin() == margin);
            Texture layer;
            for (int frame = 0; frame < 3; frame++) {
                drawFrame(r, layer); // the rest canvas
                CHECK(r.setCanvas(640, 480));
                drawFrame(r, layer); // a frame's own 4:3 canvas (the launcher's)
                CHECK(r.setCanvas(GuiBase::ScreenWidth, GuiBase::ScreenHeight));
                drawFrame(r, layer); // the program's 16:9 canvas, letterboxed
                r.captureNextFrame();
                drawFrame(r, layer); // a captured frame (a backdrop), also shown
            }
            CHECK(r.width() == 800);
            CHECK(r.height() == 600);
        }
        r.setSafeMargin(5);
        // the margin, 36 px wide at 5 % of 720, is the frame's edge mirrored and dimmed: never black; inside, the frame
        CHECK(redAt(r, 10, 240) > 40);
        CHECK(redAt(r, 360, 30) > 150);
        r.setSafeMargin(0); // no margin: the frame fills the output
        CHECK(redAt(r, 10, 240) > 150);
        if (round == 0) {
            cg.gui->releaseDisplay();
            cg.gui->acquireDisplay();
            REQUIRE(r.fourByThreeOutput());
            REQUIRE(r.setRestCanvas(800, 600));
        }
    }
}

TEST_CASE("nothing calls SDL_SetTextureScaleMode: SDL 2.0.18 (the console's) crashes in it on GLES2") {
    // a linear target is made under the scale-quality hint instead (renderer.cpp, createLinearTarget)
    const string root = AB_CORE_SOURCE_ROOT;
    vector<string> dirs = {root + "/lib_ableem/src", root + "/lib_ableem/include", root + "/ab_gui/src",
                           root + "/src/code"};
    int files = 0;
    while (!dirs.empty()) {
        const string dir = dirs.back();
        dirs.pop_back();
        DIR *d = opendir(dir.c_str());
        if (!d)
            continue;
        while (dirent *e = readdir(d)) {
            const string name = e->d_name;
            if (name == "." || name == "..")
                continue;
            const string path = dir + "/" + name;
            if (DIR *sub = opendir(path.c_str())) {
                closedir(sub);
                dirs.push_back(path);
                continue;
            }
            const size_t dot = name.rfind('.');
            const string ext = dot == string::npos ? "" : name.substr(dot);
            if (ext != ".cpp" && ext != ".h" && ext != ".c")
                continue;
            ifstream in(path, ios::binary);
            const string text((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
            files++;
            INFO(path);
            CHECK(text.find("SDL_SetTextureScaleMode(") == string::npos);
        }
        closedir(d);
    }
    CHECK(files > 100); // the tree was found
}
