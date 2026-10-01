// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screen transitions (docs/ab-gui-plan.md, 7a; UIREV-48): how one screen's picture gives way to the next. Every screen
// declares an in and an out transition (ScreenTransitions; the out one defaults to the in one played backwards):
// None, Fade (through black), CrossFade (dissolve), Slide (the new one comes in from an edge over the old, which dims
// under it - Drop is a slide from the top) and Pop (95% -> 100% with a fade). The ScreenStack plays them: the old
// picture and the new one live in two render targets and the stack composes them (alpha, offset, scale) - no read-back
// from the GPU.
//
// This header is the part that needs no GPU: what a transition is, where its two pictures go at a given progress
// (composeTransition - pure), and TransitionPlayer, the state machine that times one transition on the stack's Tweens
// (so the DebugDriver is busy while it runs and a press can finish it at once).
//
#pragma once

#include <ab_gui/tween.h>

#include <ableem/ui/types.h>

namespace abgui {

enum class TransitionKind { None, Fade, CrossFade, Slide, Pop };
// where a Slide comes in from (Top is the Drop)
enum class SlideFrom { Top, Bottom, Left, Right };

// the default lengths, milliseconds - short, the picture must never feel slow
constexpr unsigned int FadeTransitionMs = 250;
constexpr unsigned int CrossFadeTransitionMs = 200;
constexpr unsigned int SlideTransitionMs = 250;
constexpr unsigned int PopTransitionMs = 160;
// how dark the picture under a Slide gets (Style::dimAlpha's value)
constexpr int TransitionDimAlpha = 110;
// the size a Pop starts at
constexpr float PopStartScale = 0.95f;

//********************
// Transition
//********************
struct Transition {
    TransitionKind kind = TransitionKind::None;
    SlideFrom from = SlideFrom::Top;
    unsigned int durationMs = 0; // 0 = the kind's default
    unsigned int delayMs = 0;    // the old picture stays this long before anything moves (the splash's settle)

    static Transition none() { return Transition(); }
    static Transition fade(unsigned int ms = 0, unsigned int delay = 0) {
        return make(TransitionKind::Fade, SlideFrom::Top, ms, delay);
    }
    static Transition crossFade(unsigned int ms = 0) { return make(TransitionKind::CrossFade, SlideFrom::Top, ms, 0); }
    static Transition slide(SlideFrom from, unsigned int ms = 0) { return make(TransitionKind::Slide, from, ms, 0); }
    static Transition drop(unsigned int ms = 0) { return slide(SlideFrom::Top, ms); }
    static Transition pop(unsigned int ms = 0) { return make(TransitionKind::Pop, SlideFrom::Top, ms, 0); }

    // durationMs, or the kind's default; 0 for None
    unsigned int duration() const;
    // anything to play
    bool moves() const { return kind != TransitionKind::None && duration() > 0; }

private:
    static Transition make(TransitionKind kind, SlideFrom from, unsigned int ms, unsigned int delay) {
        Transition t;
        t.kind = kind;
        t.from = from;
        t.durationMs = ms;
        t.delayMs = delay;
        return t;
    }
};

//********************
// ScreenTransitions
//********************
// what a screen declares (Screen::declareTransitions); a screen that declares nothing gets defaultScreenTransitions()
struct ScreenTransitions {
    Transition in;
    Transition out;
    bool outSet = false; // unset: `in` played backwards

    ScreenTransitions() = default;
    explicit ScreenTransitions(const Transition &inAndBack) : in(inAndBack) {}
    ScreenTransitions(const Transition &in_, const Transition &out_) : in(in_), out(out_), outSet(true) {}

    // what plays when the screen closes (always backwards: the closing picture is the one that leaves)
    const Transition &closing() const { return outSet ? out : in; }
};

// a screen that declares nothing: a cross-fade in, and back
ScreenTransitions defaultScreenTransitions();

//********************
// composeTransition
//********************
// One picture in a transition frame: where it goes (the canvas, moved or scaled), its alpha, and how much black is laid
// over it once drawn (a Slide's dim of the picture underneath).
struct TransitionLayer {
    bool drawn = false;
    ableem::FRect rect;
    int alpha = 255; // 0..255
    int dim = 0;     // 0..255, 0 = none
};

// A transition frame: the old picture (the one that leaves) and the new one (the one that arrives), drawn `under`
// first, then `over`, on black.
struct TransitionFrame {
    TransitionLayer oldPicture;
    TransitionLayer newPicture;
    bool oldOnTop = false;
};

// Where the two pictures are `progress` (0..1, linear time) into `t`. `backwards` plays it as a screen's out: the
// screen whose transition it is - the new picture going forwards, the old (closing) one backwards - is the one that
// moves, over the other. Fade ignores that: the old picture fades to black in the first half, the new one in from
// black in the second (with only one of them, that one over the whole time). `hasOld`/`hasNew` false = that picture is
// black (nothing under a first screen, nothing after the last). Slide and Pop move on easeOutCubic of the time, alphas
// follow the time. Pure.
TransitionFrame composeTransition(const Transition &t, bool backwards, float progress, float width, float height,
                                  bool hasOld = true, bool hasNew = true);

//********************
// TransitionPlayer
//********************
// Times one transition at a time on a Tweens (the stack's): arm() it for a screen's frames, and the first frame of
// that screen starts it; it runs as ONE non-ambient tween of the progress 0 -> 1 (linear, after the delay), so the
// DebugDriver is busy while it runs (wait_ready waits it out) and finish() - a press - jumps to the end. A frame of any
// other screen (or no screen's: a busy frame, Gui's own) finishes it first. Off (setEnabled(false), the Options row),
// nothing is ever armed: every screen change is instant.
class TransitionPlayer {
public:
    explicit TransitionPlayer(Tweens &tweens);
    TransitionPlayer(const TransitionPlayer &) = delete;
    TransitionPlayer &operator=(const TransitionPlayer &) = delete;

    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

    // `t` onto the frames of `target` (a screen's identity), backwards for a screen's out. Whatever was armed or running
    // is finished first. Returns false - nothing armed - for a transition that does not move, or when off
    bool arm(const void *target, const Transition &t, bool backwards);
    // an outermost frame of `screen` (nullptr: not a screen's) is about to be drawn, after the tweens' update: true =
    // compose it at progress(). The target's first frame starts the transition; another's finishes it
    bool frame(const void *screen);
    // to the end at once (the press, a busy job, the display's release): nothing armed afterwards
    void finish();

    // armed and not over (it may not have started yet)
    bool armed() const;
    // started and not over: the tween runs, the DebugDriver is busy
    bool running() const;
    float progress() const { return progress_; }
    const Transition &transition() const { return transition_; }
    bool backwards() const { return backwards_; }
    const void *target() const { return target_; }

private:
    void idle();

    Tweens &tweens_;
    bool enabled_ = true;
    bool armed_ = false;
    bool started_ = false;
    const void *target_ = nullptr;
    Transition transition_;
    bool backwards_ = false;
    float progress_ = 0.0f;
    TweenId tween_ = NoTween;
    TweenOwner owner_; // after progress_: its tweens stop when the player goes
};

} // namespace abgui
