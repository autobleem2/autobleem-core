#pragma once
// The canvas math of the Renderer, pure (no SDL): where a logical canvas lands in the window, at which scale.

#include "types.h"

namespace ableem {

// A 4:3 output's own canvas (CRT 480p): 640x480 square pixels. A 720x480 output carries it stretched 1.125x
// horizontally (pixel aspect 8:9), which a 4:3 screen shows undistorted.
constexpr int FourByThreeCanvasW = 640;
constexpr int FourByThreeCanvasH = 480;

//******************
// CanvasMapping
//******************
// How a canvasW x canvasH canvas is shown in an outputW x outputH window (mapCanvas):
//  - a wide output (width / height over 1.5 - 720p, 1080p, ...) has square pixels: the canvas is drawn straight to
//    the window at `scale`, as big as fits and centred (`display` is the viewport; black bars around a canvas of
//    another shape). This is the launcher's 1280x720 path as it always was.
//  - a narrower output is taken as a 4:3 picture (a CRT through a converter at 720x480, a 4:3 monitor): every frame is
//    drawn at `scale` into a frameW x frameH target, square pixels, and present() stretches that target into `display`
//    - the canvas at its own shape on a 4:3 screen: a 4:3 canvas fills the window, a 16:9 one is letterboxed (720x360
//    at y 60 on a 720x480 output). `scale` is output height / 480, the same for every canvas, so fonts and render
//    targets made once stay sharp whichever canvas a frame uses.
// scaleX/scaleY are the output pixels per logical pixel overall (display / canvas): 1.125 x 1 for 640x480 at 720x480.
struct CanvasMapping {
    bool fourByThree = false;
    float scale = 1.0f;
    int frameW = 0, frameH = 0; // 4:3 only
    Rect display;
    float scaleX = 1.0f, scaleY = 1.0f;
};

// the output is taken as a 4:3 picture: both sides positive, width / height 1.5 or less (720x480 is 1.5)
ABLEEM_API bool isFourByThreeOutput(int outputW, int outputH);
// whether the Renderer takes the 4:3 path for a program whose own canvas is canvasW x canvasH: a 4:3 output and a
// canvas wider than 4:3 (the launcher's 1280x720) - a canvas of the output's own shape is drawn straight, as it always
// was
ABLEEM_API bool usesFrameTarget(int outputW, int outputH, int canvasW, int canvasH);
// see CanvasMapping; a zero or negative canvas size gives the default mapping (scale 1, empty display)
ABLEEM_API CanvasMapping mapCanvas(int outputW, int outputH, int canvasW, int canvasH);
// the wide outputs' mapping on any output: the canvas as big as fits, centred, square pixels (mapCanvas's first case)
ABLEEM_API CanvasMapping fitCanvas(int outputW, int outputH, int canvasW, int canvasH);
// the part of a textureW x textureH picture that fills a canvasW x canvasH canvas at its own shape: the whole picture
// when the two shapes agree (within 1 %), else its middle, full height for a wider picture, full width for a taller
// one. What Renderer::copy(tex) draws on a 4:3 output when a frame of one canvas is the backdrop of another (the
// launcher's 4:3 snapshot under a 16:9 menu shows its middle band, not squeezed)
ABLEEM_API Rect coverCrop(int textureW, int textureH, int canvasW, int canvasH);

} // namespace ableem
