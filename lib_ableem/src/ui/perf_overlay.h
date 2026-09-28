// Internal-only header: the performance overlay Renderer::present() draws (Renderer::setPerfOverlay).
#pragma once

#include "sdl_common.h"
#include <ableem/engine/process_stats.h>

#include <string>
#include <vector>

namespace ableem {

// the time the program waited for input or for its next frame (Input::waitForEvent), in performance-counter
// ticks: the overlay's "work" leaves it out, so a screen that rests does not look busy
void noteIdleWait(Uint64 ticks);

//******************
// PerfOverlay
//******************
// Two lines of small white text on black in the bottom-left corner: the frame rate, the frame time (average
// and worst), the time the program spent on a frame before presenting it, the draw calls, this process's and
// the machine's CPU load, the cores, the threads, the memory, the temperature and the render driver. Its own
// 5x7 bitmap font scaled up by whole pixels, so it depends on no theme and no TTF; the text is rebuilt once
// a second, and a frame costs one copy.
class PerfOverlay {
public:
    bool enabled = false;

    // just before SDL_RenderPresent: counts the frame and draws the overlay onto the window
    void beforePresent(SDL_Renderer *renderer, long copies, float outputScale);
    // just after it: where the next frame's work starts
    void afterPresent();
    // the texture belongs to the renderer - gone with it (Renderer::release)
    void release();

private:
    void rebuildText(SDL_Renderer *renderer);
    void rebuildTexture(SDL_Renderer *renderer);

    Uint64 lastPresent = 0, workStart = 0, idleAtWorkStart = 0;
    long frames = 0;
    double sumFrameMs = 0, maxFrameMs = 0, sumWorkMs = 0, maxWorkMs = 0;
    long sumCopies = 0;
    Uint64 windowStart = 0;
    ProcessSample previous;
    bool havePrevious = false;
    std::string driver;

    std::vector<std::string> lines;
    SDL_Texture *texture = nullptr;
    int texW = 0, texH = 0;
    bool textDirty = true;
};

} // namespace ableem
