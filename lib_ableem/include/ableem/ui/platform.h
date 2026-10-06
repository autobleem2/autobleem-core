#pragma once

#include <string>
#include <functional>
#include <vector>
#include "types.h"

namespace ableem {

//******************
// Platform
//******************
// Owns process-wide SDL setup (video/audio subsystems, the window). One instance lives inside GuiBase.
// Also carries the small pile of "which device am I running on" policy that used to be scattered #ifdefs:
// on a dev host (PC/Mac/Raspberry Pi debug build/Windows) the cursor is not grabbed, and the app is expected
// to translate keyboard input into pad events (see Input::setKeyboardAsPad).
class ABLEEM_API Platform {
public:
    Platform(const Platform &) = delete;
    Platform &operator=(const Platform &) = delete;
    ~Platform();

    // ticks in milliseconds since the platform was created (SDL_GetTicks)
    unsigned int ticks() const;
    void delay(unsigned int ms) const;

    // human readable "compiled against X, linked against Y" style string, for logging
    std::string versionString() const;
    // the SDL the process is running with, as "2.32.4", the video driver it picked ("x11", "KMSDRM",
    // "windows", "wayland"; "" without a display) and the window's display mode as "1920x1080 @ 60 Hz" ("" without
    // a window) - for an information screen
    std::string linkedVersion() const;
    // SDL's name for the OS ("Linux", "Windows", "Mac OS X") - the platform: value of a pad mapping line
    static std::string osName();
    std::string videoDriverName() const;
    std::string displayModeString() const;
    // the mode the window is in right now (what displayModeString prints), {0, 0} without a window: what
    // "Auto" really is on the screen, which the desktop's own mode is not after a mode was chosen
    Size windowDisplaySize() const;

    // true when running on a development machine rather than the real target (console/RPi image).
    bool isDevHost() const;

    // AB_HEADLESS=1 in the environment: an automated test run wants the window created with
    // SDL_WINDOW_HIDDEN from its very first frame (never flashes visible, never takes the input focus)
    // and audio sent through SDL's dummy driver - so a tester's desktop shows and hears nothing. `shot`/
    // `grab` (DebugDriver) keep producing real frames: SDL_RenderReadPixels reads the renderer's own
    // back buffer/render target, never the screen, so a window that starts hidden is no different from
    // one hidden mid-run by the DebugDriver's existing `window hide` (see Renderer::present()'s frame
    // cache, filled before SDL_RenderPresent). Read fresh each call - cheap, and lets a test toggle it.
    static bool headlessRequested();

    // AB_WINDOW_SIZE=<w>x<h> in the environment: the window is made that size and never full screen, whatever
    // the target would choose - a headless sandbox (SDL's offscreen driver says its "desktop" is 1024x768) runs
    // at 1280x720 like the VM's screen. This is its parser, pure: "1280x720" -> true, 1280, 720; anything but
    // two whole numbers (16..16384) around an 'x' is false and leaves w/h alone.
    static bool parseWindowSize(const std::string &text, int &w, int &h);

    // pure policy, exposed for testing without SDL: does a headless run start its window hidden?
    static bool startsHidden(bool headless);
    // pure policy, exposed for testing without SDL: the SDL_AUDIODRIVER to force for a headless run
    // ("dummy"), or "" to leave SDL's own probing alone.
    static std::string audioDriverOverride(bool headless);

    // hides the mouse cursor and grabs/relative-mode it. no-op on a dev host.
    void hideAndGrabCursor();

    // 0/1/2 forwarded to the SDL_HINT_RENDER_SCALE_QUALITY hint
    void setScaleQuality(int quality);

    // Give the display up and take it back. On a bare KMS/DRM system (a Raspberry Pi with no compositor)
    // the window *is* the DRM master and only one process can hold it: an emulator started while the
    // window exists cannot open the display at all. releaseDisplay() destroys the window and quits SDL's
    // video subsystem (which is what actually drops the DRM master); acquireDisplay() brings both back with
    // the same title and size. The Renderer must be released before and recreated after - GuiBase does
    // the whole sequence, see GuiBase::releaseDisplay()/acquireDisplay(). hasDisplay() tells which state
    // it is in. No-ops when already in the requested state.
    void releaseDisplay();
    void acquireDisplay();
    bool hasDisplay() const;

    // On a desktop, where another program opens its own window over ours instead of needing the display
    // to itself: ours back on top, with the focus, once that program has gone (SDL_RaiseWindow); and the
    // window out of the way (SDL_MinimizeWindow) and back (SDL_RestoreWindow + raise) - the DebugDriver's
    // `window min|restore`. No-ops without a window.
    void raiseWindow();
    void minimizeWindow();
    void restoreWindow();
    // the window off the screen and back (SDL_HideWindow/SDL_ShowWindow): the DebugDriver's way of testing
    // without a window in the way - rendering and the frame cache go on while hidden
    void hideWindow();
    void showWindow();

    // called by Input::poll() when the console power button or Esc is seen. the app is expected to show a
    // message and actually power off/exit; the library has no policy of its own here.
    void setPowerOffHandler(std::function<void()> handler);

    // calls SDL_Quit(). must run after every ableem object (this Platform included) has already been
    // destroyed - register it with atexit() from main(), before constructing the first GuiBase.
    static void shutdownSDL();

private:
    friend class GuiBase;
    // a window of outputWidth x outputHeight pixels showing a logicalWidth x logicalHeight canvas, with
    // multisampleSamples-x MSAA on its GL context when that is not 0 (see GuiBase)
    Platform(const std::string &windowTitle, int logicalWidth, int logicalHeight, int outputWidth, int outputHeight,
             int multisampleSamples, bool fullscreen);
    struct Impl;
    Impl *impl;

public:
    // the canvas the app draws on (see Renderer); the window may be bigger
    int logicalWidth() const;
    int logicalHeight() const;
    // the size of the display the window will go on, before any window exists (initialises SDL's video
    // subsystem to ask); {0, 0} if it cannot be told. What GuiBase's outputScale can be decided from.
    static Size desktopDisplaySize();
    // the size of the window the display really gave (SDL_GetWindowSize), {0, 0} without a window - a full-screen
    // window is the display's mode, whatever was asked for
    Size windowSize() const;
    // the MSAA the window actually got (0 when it was not asked for or the driver refused it)
    int multisampleSamples() const;

    // The display's modes (its EDID, as SDL lists them) a launcher can offer: one per size - the TV (16:9) modes
    // first, then the others (VESA), each group from the smallest - each
    // at the refresh rate nearest 60 Hz of those at 50 Hz or more - a 4K TV's 24/30 Hz modes are left out, a
    // game would stutter in them. Initialises SDL's video subsystem to ask; empty when it cannot be told.
    static std::vector<DisplayMode> displayModes();
    // what displayModes() makes of every mode the display lists (sizes repeated at each refresh rate): the
    // filter, the one refresh rate per size and the order - pure, no SDL
    static std::vector<DisplayMode> listableModes(const std::vector<DisplayMode> &all);
    // the biggest of those modes (by area; {0, 0} for none): the display's own mode, which "Auto" picks - pure, no SDL
    static Size largestMode(const std::vector<DisplayMode> &modes);
    // The mode full-screen windows are made in from now on - the next acquireDisplay() (or the first window):
    // 0x0 is the desktop's own mode (SDL_WINDOW_FULLSCREEN_DESKTOP, no modeset), anything else a real modeset
    // to that size at displayModes()' refresh rate. A size the display does not list falls back to the
    // desktop's. Process-wide, so it can be set before the GuiBase that makes the first window exists.
    static void setOutputMode(int w, int h);

    // internal: used by Input to invoke the app's power-off handler and by Renderer/Texture/Font/Audio to
    // reach the underlying SDL objects without exposing them in a public header.
    void *nativeWindow() const;
    void invokePowerOffHandler() const;
};

} // namespace ableem
