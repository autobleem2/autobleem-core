// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Tween, Timeline and Tweens: element animations (docs/ab-gui-plan.md, 7b; step G5o1) - the one timing base
// every animated piece of the UI moves onto, instead of a hand-written timer each.
//
// A Tween animates one float the caller owns (a position, a scale, an alpha, an angle) from one value to another
// over a duration, after a delay, along an easing: the UI's easeOutCubic (core/model/timing.h's, formula for
// formula), linear, cubic in and in/out, a small overshoot (outBack), and pulseWave as an easing (0 -> 1 -> 0 over
// the duration, a cosine - Play's pulse, the glow's breathing). It may loop (for as long as it runs) and yoyo (there
// and back, the way back the forward curve run backwards), is `ambient` when it is only decoration, and calls back
// at its end. A Timeline runs tweens (and timelines, and plain waits) one after the other or all at once, with its
// own delay and end callback.
//
// Tweens runs them. There is one per program, owned by the ScreenStack (ctx.stack().tweens()) and timed by one clock,
// the Context's ticks() (Context::setStack binds it). Every frame through the stack advances it first (outside the
// frame, so an end callback may open a screen), so the values a screen draws are the frame's. What the tweens change
// for the rest of the program:
//  - busy(): a non-ambient tween (or timeline) is running. The DebugDriver counts that as busy (`busy`, `wait_ready`
//    wait it out - no test grabs a half-way frame); an ambient one, and every loop, never holds it.
//  - frameNeed(): Active while a non-ambient one runs, Ambient while only ambient ones (loops) run, else Idle - what a
//    screen combines with its own need each pass (applyFrameNeed raises the Input's, never lowers it).
//
// Time: a run's clock starts when it is started; its values are a pure function of the time since (valueAt), so a
// late frame lands where it should instead of drifting, and the unsigned tick counter's wrap does no harm. A tween
// writes its float from its first update after its delay until its end (the end value exactly, `to` - or `from` for a
// yoyo and a pulse); before its delay it does not touch it. cancel() stops a run where it is (no write, no callback);
// finish() jumps it to its end (the end values, the callbacks - a loop, having no end, is dropped): what a press during
// a transition does (7a).
//
// Lifetime: a tween writes through a pointer, so a float that dies with its screen must never be written after.
// Start such tweens for a TweenOwner the screen holds next to its floats: when the owner dies (or cancel()s), every
// tween started for it stops at once - it never writes again and its callbacks are not called (they may capture the
// screen) - and it no longer counts as running. A tween with no owner is the program's own (a float that lives as
// long as the Tweens). (The DebugDriver's busy count such a run held is given back at the next update - the next
// frame through the stack - since the owner does not know the Tweens.)
//
// The start-up transitions (the plan's decision 12, later G5o steps) are tweens on the stack's floats like any other:
// the launcher's slide-down over the splash's last frame is a non-ambient Tween on its y offset (easeOutCubic), the
// fade from black one on an alpha - so the DebugDriver is busy while they run and a press may finish() them.
//
// No SDL, no AutoBleem code; pure apart from the DebugDriver's busy counter and the Input's frame need.
//
#pragma once

#include <ableem/ui/input.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace abgui {

class Context;

// An easing: the fraction of the time gone (0..1) -> the fraction of the way (0 at 0, 1 at 1 - but pulse, which
// comes back to 0, and outBack, which passes 1 on the way). A plain function, so a program may pass its own.
using Easing = float (*)(float);

namespace ease {
float linear(float t);
// timing.h's easeOutCubic, formula for formula: quick to leave, settling into the end (the UI's animations)
float outCubic(float t);
float inCubic(float t);
float inOutCubic(float t);
// the standard "back" ease-out: about 10% past the end a little after half time, then back to it
float outBack(float t);
constexpr float OvershootStrength = 1.70158f;
// timing.h's pulseWave(elapsed, period) as an easing, t = elapsed / period: 0 -> 1 at half time -> 0, a cosine
// (0 at t 0 and at t 1, as pulseWave is 0 at every period's start)
float pulse(float t);
} // namespace ease

// from + (to - from) x eased, exactly `from` at 0 and exactly `to` at 1 (a float lerp need not be)
float tweenLerp(float from, float to, float eased);

//********************
// Tween
//********************
class Tween {
public:
    // "never": the length of a loop, the start of what follows one in a sequence
    static constexpr unsigned int Forever = 0xFFFFFFFFu;

    // `target` from `from` to `to` in durationMs (0 = straight to `to`), easeOutCubic, no delay, once
    Tween(float &target, float from, float to, unsigned int durationMs);

    // the builder - each returns the tween, so they chain: Tween(y, -720, 0, 250).ease(ease::outBack).delay(100)
    Tween &delay(unsigned int ms);
    Tween &ease(Easing curve); // nullptr = linear
    Tween &loop();             // again and again until cancelled (never ends; counts as ambient)
    Tween &yoyo();             // there and back: the way back is the forward curve run backwards; ends at `from`
    Tween &ambient();          // decoration: never holds the DebugDriver's busy, the frame need only Ambient
    Tween &onEnd(std::function<void()> callback);

    float *target() const { return target_; }
    float from() const { return from_; }
    float to() const { return to_; }
    unsigned int duration() const { return duration_; }
    unsigned int delay() const { return delay_; }
    Easing easing() const { return easing_; }
    bool loops() const { return loop_; }
    bool yoyos() const { return yoyo_; }
    bool isAmbient() const { return ambient_; }
    const std::function<void()> &endCallback() const { return onEnd_; }
    // whether it holds the DebugDriver's busy and the Active frame need while it runs: not ambient, not a loop
    bool holdsBusy() const { return !ambient_ && !loop_; }

    // The pure rules, by the time since the tween was started (its delay included).
    // the delay and one pass (two with yoyo); Forever when it loops
    unsigned int length() const;
    // the delay is over: from now on it writes its float
    bool startedAt(unsigned int elapsedMs) const { return elapsedMs >= delay_; }
    // it has ended (never, when it loops)
    bool endedAt(unsigned int elapsedMs) const;
    // its value then: `from` before the delay is over; once it has ended exactly the curve's end - `to` (`from` for a
    // yoyo, and for `pulse`, which comes back)
    float valueAt(unsigned int elapsedMs) const;

private:
    float *target_;
    float from_;
    float to_;
    unsigned int duration_;
    unsigned int delay_ = 0;
    Easing easing_;
    bool loop_ = false;
    bool yoyo_ = false;
    bool ambient_ = false;
    std::function<void()> onEnd_;
};

//********************
// Timeline
//********************
// Tweens, timelines and waits in order (Sequence: each starts when the one before ends) or all at once (Parallel:
// all start together, it ends with the last). A step after an endless loop in a sequence never starts, and a
// timeline with an endless loop never ends. Built by value; Tweens::start copies it.
class Timeline {
public:
    enum class Order { Sequence, Parallel };

    explicit Timeline(Order order = Order::Sequence) : order_(order) {}
    static Timeline sequence() { return Timeline(Order::Sequence); }
    static Timeline parallel() { return Timeline(Order::Parallel); }

    Timeline &add(const Tween &tween);
    Timeline &add(const Timeline &timeline);
    Timeline &wait(unsigned int ms); // a pause: a step that animates nothing
    Timeline &delay(unsigned int ms);
    Timeline &onEnd(std::function<void()> callback);

    Order order() const { return order_; }
    unsigned int delay() const { return delay_; }
    std::size_t size() const { return steps_.size(); }
    // its delay and its steps' time (their sum in a sequence, the longest in parallel); Forever when a loop makes it
    // endless
    unsigned int length() const;

    // one step: exactly one of the three
    struct Step {
        std::shared_ptr<Tween> tween;
        std::shared_ptr<Timeline> timeline;
        unsigned int wait = 0;
    };
    const std::vector<Step> &steps() const { return steps_; }
    const std::function<void()> &endCallback() const { return onEnd_; }

private:
    Order order_;
    unsigned int delay_ = 0;
    std::vector<Step> steps_;
    std::function<void()> onEnd_;
};

//********************
// TweenOwner
//********************
// What a screen holds next to the floats its tweens write (a member declared with them): tweens started for it stop
// for good when it is destroyed or cancel()s - no write, no callback, no longer running. It needs no Tweens: each
// tween keeps a weak reference to the owner's token and looks at it before it writes or calls back.
class TweenOwner {
public:
    TweenOwner();
    TweenOwner(const TweenOwner &) = delete;
    TweenOwner &operator=(const TweenOwner &) = delete;

    // every tween started for this owner so far stops (the owner stays usable for new ones)
    void cancel();

private:
    friend class Tweens;
    std::shared_ptr<char> token_;
};

// the id of a started tween or timeline; 0 is none (ids are never reused)
using TweenId = unsigned long;
constexpr TweenId NoTween = 0;

// the stronger of two frame needs (Active > Ambient > Idle)
ableem::Input::FrameNeed strongerFrameNeed(ableem::Input::FrameNeed a, ableem::Input::FrameNeed b);

//********************
// Tweens
//********************
class Tweens {
public:
    using Clock = std::function<unsigned int()>;

    Tweens();
    // a run that still holds the DebugDriver's busy gives it back
    ~Tweens();
    Tweens(const Tweens &) = delete;
    Tweens &operator=(const Tweens &) = delete;

    // the clock: `clock` when set, else the bound Context's ticks() (Context::setStack binds it), else the time of the
    // last update(now)
    void bind(const Context &ctx);
    Clock clock;
    unsigned int now() const;

    // starts a tween or a timeline now; with an owner it stops when the owner goes (see TweenOwner)
    TweenId start(const Tween &tween);
    TweenId start(const Tween &tween, const TweenOwner &owner);
    TweenId start(const Timeline &timeline);
    TweenId start(const Timeline &timeline, const TweenOwner &owner);
    // start() as if it had been started at `startedAt` on the clock - a moment already gone (not after now()), for a
    // run that must follow on exactly from the end of another: the carousel's held-stick step starts where the last
    // one ended, part of a frame ago (G5o5). Its first update writes the values for the time since `startedAt`
    TweenId startAt(unsigned int startedAt, const Timeline &timeline, const TweenOwner &owner);

    // writes every running tween's value for now() (or `now`), then calls the callbacks of what ended, in the order
    // it ended (at the same time: the inner before the outer, then the order started). ScreenStack::frame calls it
    // before each outermost frame; calling it again at the same time changes nothing.
    void update();
    void update(unsigned int now);

    // stops it where it is: no write, no callback. false when it is not running
    bool cancel(TweenId id);
    // jumps it to its end: every tween's end value, then the callbacks; a loop in it is dropped. false when it is
    // not running
    bool finish(TweenId id);
    // finish() for every run that holds busy (a press during a transition); ambient ones go on
    void finishNonAmbient();
    // cancel() for every run
    void clear();

    // it is still running (started, not ended, cancelled or orphaned)
    bool running(TweenId id) const;
    // a non-ambient run is running - the DebugDriver's busy while it is. A run is non-ambient when one of its tweens
    // holds busy (Tween::holdsBusy), and then it holds it until it is over (its timelines' end callbacks included)
    bool busy() const;
    // anything is running (loops included)
    bool animating() const;
    // how many runs are running
    std::size_t count() const;
    // Active while busy(), Ambient while only ambient runs are, else Idle
    ableem::Input::FrameNeed frameNeed() const;
    // raises the input's frame need to frameNeed() when that is stronger - never lowers it: a screen calls it right
    // after it sets its own need for the pass
    void applyFrameNeed(ableem::Input &input) const;

private:
    struct Impl;
    TweenId add(const Timeline &timeline, const TweenOwner *owner, unsigned int startedAt);
    void syncBusy();

    std::unique_ptr<Impl> impl_;
    const Context *ctx_ = nullptr;
    bool busyHeld_ = false;
};

} // namespace abgui
