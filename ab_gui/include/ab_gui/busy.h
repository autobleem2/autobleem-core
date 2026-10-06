// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Busy: the spinner a long job on the main thread shows (applying settings, reloading the theme, deleting a
// game, an extension's own job) - step G3l of docs/ab-gui-plan.md. begin() keeps the screen as it is: `redraw`
// renders and presents it once, and that frame, captured, is the backdrop. tick(), called from inside the job
// wherever it loops, draws the backdrop dimmed with the ring of dots and the message over it - at most every
// FrameInterval ms, so a tight loop is not slowed - and setProgress() adds a bar under the message. end() drops
// the backdrop and flushes the input the pads and the keyboard queued meanwhile.
//
// Nothing reads the input while the job runs (a busy frame never polls), and the busy rule itself - what is
// dropped, what is kept, the presses released - stays in ableem::Input::flushInputEvents(), which end() calls on
// the busy -> not busy step only. The DebugDriver's `busy` / `wait_ready` read DebugDriver::setBusy, which
// begin() and end() set on the same steps.
//
// A program reaches its one Busy as ctx.stack().busy(): the ScreenStack owns it, and Context::setStack() binds it
// to that Context (the renderer, the input, the clock, the style, the fonts and the text drawing). Every busy
// frame goes through the stack, so a tick from a load inside a screen's drawing is a frame of its own, presented
// at once, as it always was.
//
// A theme with its own spinner strip (spinner.h, G5p) has it played in place of the ring, from the job's start.
//
// waitScreen() is the other "please wait" picture: for a blocking call nothing can tick from (a network scan, the
// flasher, the power-off), one frame of the program's background and logo with the spinner and the message.
//
#pragma once

#include <ableem/ui/texture.h>
#include <ableem/ui/types.h>

#include <functional>
#include <string>
#include <vector>

namespace abgui {

class Context;
class ScreenStack;

//********************
// Busy
//********************
class Busy {
public:
    // today's numbers
    static constexpr unsigned int FrameInterval = 40; // ms: a tick draws at most this often
    static constexpr unsigned int SpinnerStep = 70;   // ms: the leading dot moves one place
    static constexpr int SpinnerDots = 12;
    static constexpr int SpinnerRadius = 30;
    static constexpr int SpinnerDot = 8;
    static constexpr int SpinnerRise = 20; // the ring's centre above the canvas' middle
    static constexpr int MessageGap = 24;  // the message's top below the ring (its radius)
    static constexpr int BarWidth = 400;   // the progress bar, centred
    static constexpr int BarHeight = 6;
    static constexpr int BarGap = 12;          // the bar's top below the message's line
    static constexpr int ToastPad = 24;        // the toast frame behind the ring and message: room round them
    static constexpr int WaitSpinnerGap = 60;  // waitScreen: the ring at least this far below the logo
    static constexpr int WaitSpinnerFoot = 90; // ... and at least this far above the canvas' bottom
    static constexpr int WaitTopLineY = 12;    // waitScreen: the top line's y

    explicit Busy(ScreenStack &stack);
    Busy(const Busy &) = delete;
    Busy &operator=(const Busy &) = delete;

    // the Context whose renderer, input, clock, style and fonts it draws and flushes with (Context::setStack)
    void bind(Context &ctx) { ctx_ = &ctx; }
    bool bound() const { return ctx_ != nullptr; }

    // Starts (or, while one runs, restarts) a job: `message` under the spinner, no bar; `redraw` is the screen as
    // it is, presented once and captured as the backdrop; then the first busy frame. Only the first begin() of a
    // job tells the DebugDriver - a begin() inside a running job is the same job with a new message. Nothing
    // without a bound Context.
    void begin(const std::string &message, const std::function<void()> &redraw);
    // a busy frame when one is due (FrameInterval since the last, or the first after setProgress); nothing when no
    // job runs
    void tick();
    // the bar under the message, done/total (total 0 = no bar); drawn by the next tick()
    void setProgress(int done, int total);
    // Ends the job: on the busy -> not busy step the input queued meanwhile is flushed (Input::flushInputEvents,
    // the busy rule) and the DebugDriver told; the backdrop is dropped either way. Called when no job runs (a
    // screen's every frame does), it changes nothing else.
    void end();

    bool active() const { return active_; }
    const std::string &message() const { return message_; }
    int done() const { return done_; }
    int total() const { return total_; }
    // when the job began, and when the last busy frame was drawn (0: the next tick draws), by the Context's clock
    unsigned int started() const { return started_; }
    unsigned int lastFrame() const { return lastFrame_; }

    // one frame of the "please wait" picture: the backdrop (Context::drawBackdrop), the logo (Context::drawLogo),
    // the spinner under it with `message`, `topLine` across the top when given. No job, no input touched.
    void waitScreen(const std::string &message, const std::string &topLine = "");

    //*******************************
    // the pure rules (tested)
    //*******************************
    // whether a tick at `now` draws: none drawn yet (lastFrame 0), or FrameInterval since the last
    static bool frameDue(unsigned int now, unsigned int lastFrame);
    // the leading dot at `ticks`: one place every SpinnerStep ms, round the SpinnerDots
    static int spinnerLead(unsigned int ticks);
    // the ring's centre on a canvasWidth x canvasHeight canvas
    static ableem::Point spinnerCentre(int canvasWidth, int canvasHeight);
    // the message's top, `cy` being the ring's centre
    static int messageTop(int cy);
    // the progress bar's track: BarWidth x BarHeight, centred, BarGap under the message's text (`lineHeight` tall:
    // one line's height, or all the lines' of a wrapped message)
    static ableem::Rect barRect(int canvasWidth, int canvasHeight, int lineHeight);
    // the toast frame the busy frame draws behind the ring, the message (`messageWidth` x `lineHeight`, the height of
    // all its lines) and the bar when there is one: centred on the canvas' middle column, ToastPad round the widest
    // and the lowest of them
    static ableem::Rect toastRect(int canvasWidth, int canvasHeight, int messageWidth, int lineHeight, bool hasBar);
    // the width a message gets before it is wrapped: the canvas less a ToastPad of frame and one of air each side
    // (1280 canvas: a line of 1184 px; a 4:3 output's 640: 544 px)
    static int messageRoom(int canvasWidth);
    // the share the bar shows: done clamped to 0..total
    static int barDone(int done, int total);
    // waitScreen's ring centre y: WaitSpinnerGap below the logo (or two thirds down, whichever is lower), but
    // WaitSpinnerFoot above the bottom at most
    static int waitSpinnerY(int canvasHeight, const ableem::Rect &logo);

private:
    void drawFrame();
    // the ring about (cx, cy) - or the theme's spinner strip (G5p), `elapsed` ms into its animation - with `message`
    // centred under it
    void drawSpinner(int cx, int cy, const std::string &message, unsigned int elapsed);
    // `message` in rows no wider than messageRoom (one row when it fits - everything on a 16:9 canvas)
    std::vector<std::string> messageRows(const std::string &message, int canvasWidth) const;

    ScreenStack &stack_;
    Context *ctx_ = nullptr;
    bool active_ = false;
    int done_ = 0, total_ = 0;
    std::string message_;
    ableem::Texture backdrop_;
    unsigned int started_ = 0, lastFrame_ = 0;
};

} // namespace abgui
