#pragma once

#include "canvas.h"
#include "types.h"

#include <string>
#include <vector>

namespace ableem {

class Platform;
class Texture;

//******************
// Renderer
//******************
// Wraps the one SDL_Renderer the app uses. Owned by GuiBase; screens receive a reference.
//
// Coordinates are logical: the app draws on a GuiBase::ScreenWidth x ScreenHeight canvas whatever the window
// is. The window may be bigger by `outputScale()` (a 1920x1080 window for a 1280x720 canvas is 1.5), and
// every drawing call here maps logical to output pixels, so nothing in the app changes with the window. What
// gets sharper at a scale above 1: textures loaded from files (drawn from more of their pixels), Fonts (loaded
// bigger, see Font::load) and render targets (Texture::createTarget allocates output pixels). At scale 1 the
// mapping is the identity on integers.
class ABLEEM_API Renderer {
public:
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    ~Renderer();

    void clear();
    // shows the frame; never returns sooner than a frame after the previous one (60 fps, AB_MAX_FPS another
    // rate, 0 none) - so a screen's loop cannot spin where the display does not wait for vsync
    void present();

    // the frame cache (the DebugDriver's): with it on, the next present() after a requestFrameCopy() keeps a
    // copy of the frame it shows (copiedFrame() is its number); saveLastFrame() writes the newest copy as BMP
    // or PNG (by extension) and frameCount() says how many frames were presented - all callable from another
    // thread. Only on request, since reading a frame back costs a few ms (in a VM's software GL, most of it).
    void setFrameCache(bool enabled);
    // asks for a copy of the next frame presented; returns frameCount() at the time of asking
    unsigned long requestFrameCopy();
    unsigned long copiedFrame() const;
    bool saveLastFrame(const std::string &path);
    // like saveLastFrame, but the PNG comes back as bytes instead of a file - what the DebugDriver's `grab`
    // command sends over the socket, so a test never writes to the device's own storage. Same "no frame
    // cached yet" false as saveLastFrame.
    bool encodeLastFramePng(std::vector<unsigned char> &out);
    // the newest copy's pixels (ARGB8888, `pitch` bytes a row) handed out, so a caller encodes them without
    // holding the renderer up (the DebugDriver's clips); returns the copy's frame number, 0 when there is none
    unsigned long copyLastFrame(std::vector<unsigned char> &pixels, int &w, int &h, int &pitch) const;
    unsigned long frameCount() const;

    // the next present() keeps its frame as a texture (output pixels, drawn over the whole target with
    // copy(tex)) - the backdrop a busy overlay draws on while a long job runs. A frame that starts with
    // clear() is drawn straight into a target on the GPU (no read-back); one that does not is read back
    void captureNextFrame();
    // the same capture, but a snapshot of something that is not on the screen: when the frame goes into a target
    // (it starts with clear()) present() keeps it as lastCapture() and leaves the window alone - no copy, no buffer
    // swap, no frame-rate wait, no frame-cache copy - so what the window shows does not change for the snapshot
    // (the launcher's bare carousel under the System menu, BUG-31). A frame that does not start with clear() is read
    // back and presented as with captureNextFrame()
    void captureNextFrameSilently();
    // a silent capture is asked for: the next frame is a snapshot, never shown (the screen transitions leave it alone)
    bool silentCapturePending() const;
    Texture lastCapture() const;

    void setDrawColor(Color c);
    Color drawColor() const;
    void setBlendMode(BlendMode mode);

    void fillRect(const Rect &r);
    void fillRect(); // fills the entire current render target (screen or whatever setTarget() pointed at)
    // fills every rect in one draw call, all in the current draw color - far cheaper than looping fillRect()
    // for things like a starfield with hundreds of small rects on weak hardware.
    void fillRects(const Rect *rects, int count);
    void drawRect(const Rect &r);
    void drawLine(Point a, Point b);

    // src/dst nullptr means "whole texture" / "whole render target"
    void copy(const Texture &tex, const Rect *src = nullptr, const Rect *dst = nullptr);
    // the same to a fractional destination - no rounding, so an animated size or position moves smoothly
    // instead of a whole pixel at a time (SDL 2.0.10+; older SDLs round)
    void copy(const Texture &tex, const Rect *src, const FRect &dst);

    // Pseudo-3D: draws `src` (nullptr = the whole texture) into the trapezoid whose vertical sides are `left`
    // and `right` - what a rectangle standing in 3D and turned about its vertical axis looks like on screen.
    // Each side's height is taken as its depth cue (the taller side is the nearer one), and the texture's
    // columns are spread across the width perspective-correctly, so a cover turned 60 degrees does not
    // "swim". `tint` multiplies the texture's colours (the caller's shading), white leaves them alone.
    // With SDL 2.0.18 or newer the whole thing is one SDL_RenderGeometry call - two vertices per output
    // column, the tint in the vertex colours; older SDLs (the console's) get one SDL copy per column, to a
    // fraction of a pixel from 2.0.10 on. Either way a multisampled window (GuiBase's multisampleSamples)
    // smooths the sloping edges. With left.x > right.x the back of the rectangle is showing and the
    // texture is drawn mirrored.
    void copyTrapezoid(const Texture &tex, const Rect *src, VerticalEdge left, VerticalEdge right,
                       Color tint = Color());
    // the same with a vertical gradient: `topTint` along the edges' tops, `bottomTint` along their bottoms
    // (alpha included), and with `flipVertically` the source upside down - a reflection on the floor under a
    // box is its bottom slice flipped, fading out. The gradient needs SDL_RenderGeometry (2.0.18+); an older
    // SDL draws the strips in the two tints' average.
    void copyTrapezoidFaded(const Texture &tex, const Rect *src, VerticalEdge left, VerticalEdge right, Color topTint,
                            Color bottomTint, bool flipVertically);

    // nullptr switches back to rendering to the screen
    void setTarget(Texture *target);
    // the nesting way: pushTarget draws into `target` (nullptr the screen) until the matching popTarget, which
    // goes back to whatever was the target before - so a text run cached while a layer is being drawn does not
    // send the rest of the layer to the screen. Use these for anything that may run inside another target.
    void pushTarget(Texture *target);
    void popTarget();
    // how many times the render targets' contents were lost (SDL_RENDER_TARGETS_RESET/DEVICE_RESET, a new
    // renderer): a cache drawn into a target keeps the value it was drawn at and draws again when it changed
    unsigned long targetsLost() const;

    // the logical canvas, in the app's coordinates
    int width() const;
    int height() const;
    // A 4:3 output (CanvasMapping, canvas.h - a 720x480 CRT mode): the canvas of the frame about to be drawn, until
    // the next present() - a screen laid out for 4:3 asks for FourByThreeCanvasW x FourByThreeCanvasH before its
    // frame's clear() (GuiScreen::prepareFrame), and present() goes back to the program's own (the Platform's logical
    // size, 1280x720), letterboxed at its shape. False, and nothing changes, on a wide output or for a size <= 0.
    bool setCanvas(int w, int h);
    // the output is taken as a 4:3 picture: frames go through a frame target that present() stretches (canvas.h)
    bool fourByThreeOutput() const;
    // output pixels per logical pixel (1 unless the window is bigger than the canvas)
    float outputScale() const;
    // the SDL render driver in use ("opengl", "opengles2", "direct3d"; "" without a renderer)
    std::string driverName() const;

    // Frame statistics, on when AB_FRAME_STATS is in the environment (read once, at construction): every
    // 5 s a PLOG_INFO line with the frame time (average and worst), how many frames took over 20 ms, and
    // the copies and texture switches per frame. copies() is what present() will report for the frame in
    // progress; Font adds its glyphs through countCopies() since it draws past this class.
    static bool statsEnabled();
    void countCopies(int n);

    // the performance overlay (Options -> Diagnostics -> "Show performance", or AB_PERF_OVERLAY=1 in the
    // environment, which keeps it on whatever the setting says): two lines of small white text on black in
    // the bottom-left corner of every frame - the frame rate, frame and work times, draw calls, CPU load
    // (this process and the machine), cores, threads, memory, temperature and the render driver
    void setPerfOverlay(bool on);
    bool perfOverlay() const;
    // a logical rect in output pixels, edges rounded so that neighbouring rects still tile
    Rect toOutput(const Rect &r) const;

private:
    friend class Platform;
    friend class GuiBase;
    friend class Texture;
    friend class Font;
    explicit Renderer(Platform &platform);
    // the display hand-off (GuiBase::releaseDisplay()/acquireDisplay()): destroy the SDL renderer while
    // keeping this object - everything holds a reference to it - and make a new one on the new window.
    // Every Texture and Font made with the old renderer must be gone before release(): SDL frees them with
    // the renderer, and a handle destroyed later would free them twice.
    void release();
    void recreate(Platform &platform);
    struct Impl;
    Impl *impl;

public:
    void *native() const; // internal use by Texture/Font implementations
};

} // namespace ableem
