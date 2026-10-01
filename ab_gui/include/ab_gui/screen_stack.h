// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::ScreenStack: where a program's frames are presented - "screens draw, the stack presents"
// (docs/ab-gui-plan.md, 7a; step G3c). A screen's render() hands its drawing to frame(): the stack clears the
// canvas, the screen draws, the stack presents - one present per frame, and no screen clears or presents itself.
// Being the one place a frame is shown is what the transitions (7a) need later: the stack will then send a frame
// into an off-screen target instead of the screen, with no screen changing. For now it only puts the three steps
// in order; each screen still runs its own loop (abgui::Screen's, or its own).
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
#include <ab_gui/tween.h>

#include <ableem/ui/renderer.h>
#include <ableem/ui/types.h>

#include <functional>
#include <memory>

namespace abgui {

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
    void run(const ableem::Color *clearColor, const Draw &draw);

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
};

} // namespace abgui
