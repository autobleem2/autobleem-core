// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::ScreenStack: where a program's frames are presented - "screens draw, the stack presents"
// (docs/ab-gui-plan.md, 7a; step G3c). A screen's render() hands its drawing to frame(): the stack clears the
// canvas, the screen draws, the stack presents - one present per frame, and no screen clears or presents itself.
// Being the one place a frame is shown is what the transitions (7a, UIREV-48 - the appended part at the end) use:
// while one runs, the stack sends the screen's frame into an off-screen target and composes it with the old picture,
// with no screen changing. Each screen still runs its own loop (abgui::Screen's, or its own).
//
// A frame started while another is being drawn (a busy spinner's tick from inside a load that a screen's drawing
// started) is a frame of its own: cleared, drawn and presented at once, as such a frame always was; the outer one
// goes on and presents at its own end. depth() tells the two apart.
//
// The stack also owns the program's one busy spinner (busy(), step G3l): its frames are the stack's too.
//
// And the program's one set of running tweens (tweens(), step G5o1 - tween.h): every outermost frame advances them
// to the clock first, before the frame is begun (so an end callback that opens a screen does not run inside it), and
// the screen draws the frame's values. With no tween running that is nothing.
//
#pragma once

#include <ab_gui/busy.h>
#include <ab_gui/screen_transition.h>
#include <ab_gui/tween.h>

#include <ableem/ui/renderer.h>
#include <ableem/ui/types.h>

#include <functional>
#include <memory>

namespace ableem {
class GuiScreen;
class Input;
} // namespace ableem

namespace abgui {

class Screen;

//********************
// ScreenStack
//********************
class ScreenStack {
public:
    using Draw = std::function<void()>;

    // What a frame is shown on: the renderer's clear and present (the program's) - or a test's recorder.
    class Display {
    public:
        virtual ~Display() = default;
        // the colour the next clear() clears to (and, on a renderer, the draw colour from then on)
        virtual void setClearColor(const ableem::Color &color) = 0;
        virtual void clear() = 0;
        virtual void present() = 0;
    };

    // frames on the renderer (it outlives the stack: GuiBase keeps it across the display's release)
    explicit ScreenStack(ableem::Renderer &renderer);
    // frames on another display, which outlives the stack (the tests')
    explicit ScreenStack(Display &display);
    ~ScreenStack();
    ScreenStack(const ScreenStack &) = delete;
    ScreenStack &operator=(const ScreenStack &) = delete;

    // one frame: the canvas cleared to opaque black (never the colour a last drawing left set, BUG-31), `draw`,
    // presented - what a screen's render() did with its own clear() and present(). A `draw` that throws leaves the
    // frame unpresented.
    void frame(const Draw &draw);
    // the same with the canvas cleared to `clearColor`: the draw colour is set to it first and stays so, as the
    // setDrawColor() + clear() it replaces left it
    void frame(const ableem::Color &clearColor, const Draw &draw);

    // how many frames are being drawn right now: 0 between frames, 1 inside one, more inside a nested one
    int depth() const { return depth_; }
    // the frames presented through the stack so far
    unsigned long presented() const { return presented_; }

    // the busy spinner a long job shows (abgui::Busy, step G3l) - bound to the Context this stack is set on
    // (Context::setStack); its frames go through this stack
    Busy &busy() { return busy_; }
    const Busy &busy() const { return busy_; }

private:
    std::unique_ptr<Display> own_; // the renderer's display, when the stack made it
    Display *display_;
    int depth_ = 0;
    unsigned long presented_ = 0;
    Busy busy_;

    // Appended (step G5o1), after every member above so their offsets stay: the program's running tweens (tween.h),
    // timed by the Context this stack is set on (Context::setStack binds them) and advanced before every outermost
    // frame. Held through a pointer, so the Tweens can grow without the stack's size changing again.
public:
    Tweens &tweens() { return *tweens_; }
    const Tweens &tweens() const { return *tweens_; }

private:
    std::unique_ptr<Tweens> tweens_;

    // Appended (UIREV-48, the plan's 7a): the screen transitions - screen_transition.h. Every screen declares an in and
    // an out transition (Screen::declareTransitions; none declared = a cross-fade). When a screen opens (GuiScreen::show
    // tells the stack through its observer, attach()) the picture of the screen under it is drawn into a render target
    // (the old picture), and the new screen's frames go into a second target while the transition runs: the stack
    // composes the two on the screen (alpha, offset, scale - no read-back from the GPU). When a screen closes, its last
    // picture is the old one and the screen under it is drawn live; a screen with nothing under it plays only a Fade
    // (to black), on its own frames, before show() returns; with any other out its last picture stays as the old one,
    // which the next start transition comes in over (the launcher over the splash). The transition starts with the new screen's
    // first frame (while it loads, the old picture stays), runs as one non-ambient tween (the DebugDriver is busy - its
    // wait_ready waits it out), and a press finishes it at once (attach()'s press observer). Any other frame (a busy
    // job's, Gui's own) finishes it first. Off (setAnimations(false), the Options row): every change is instant and
    // nothing is drawn twice. Held through one pointer, so the stack's layout grows by that only.
public:
    // what `screen` plays when it opens and closes; kept until it closes (or is destroyed)
    void declare(const ableem::GuiScreen &screen, const ScreenTransitions &transitions);
    void forget(const ableem::GuiScreen &screen);
    // what `screen` declared, else defaultScreenTransitions()
    ScreenTransitions declared(const ableem::GuiScreen &screen) const;
    // a screen's frame (Screen::render): frame() with its drawing and its clear colour - composed with the old picture
    // while the screen's transition runs
    void screenFrame(Screen &screen);

    // on (the default) / off: off finishes a running transition and arms none from then on
    void setAnimations(bool on);
    bool animations() const;
    // the next screen that opens with no screen under it comes in with `t` instead of its own in (once): after the
    // splash the launcher drops in from the top (the plan's decision 12)
    void setStartTransition(const Transition &t);
    // `screen` (with nothing under it, and no Fade out) leaves its last picture as the old one when it closes, for the
    // start transition to come in over (the splash); drawn once more as it closes, so it must still be able to draw
    void keepPictureOnClose(const ableem::GuiScreen &screen);
    // a transition will bring `screen` in or is bringing it in (armed for its frames, not over) - the launcher then
    // leaves out its own fade from black
    bool bringsIn(const ableem::GuiScreen &screen) const;
    // a transition is running (started, not over): the frame probe attach() gives the input, and the DebugDriver busy
    bool transitioning() const;
    // finishes a running or armed transition at once (a press, a busy job starting)
    void finishTransition();
    // the program's input and screens: from now on GuiScreen::show() tells this stack when screens open and close,
    // every press finishes a running transition, and every pass draws a frame while one runs. Once, by the program
    // (AutoBleem's Gui); the destructor (or detach()) undoes it
    void attach(ableem::Input &input);
    void detach();
    // the two render targets dropped (the display is released for a game): a transition in progress ends
    void releaseTargets();

private:
    struct Transitions;
    void run(const ableem::Color *clearColor, const Draw &draw, Screen *screen);
    std::unique_ptr<Transitions> transitions_;
};

} // namespace abgui
