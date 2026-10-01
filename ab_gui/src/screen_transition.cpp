// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screen transitions: the layer math and the player. See the header.
//
#include <ab_gui/screen_transition.h>

#include <algorithm>
#include <cmath>

namespace abgui {

//********************
// Transition
//********************
unsigned int Transition::duration() const {
    if (kind == TransitionKind::None)
        return 0;
    if (durationMs > 0)
        return durationMs;
    switch (kind) {
    case TransitionKind::Fade:
        return FadeTransitionMs;
    case TransitionKind::CrossFade:
        return CrossFadeTransitionMs;
    case TransitionKind::Slide:
        return SlideTransitionMs;
    case TransitionKind::Pop:
        return PopTransitionMs;
    default:
        return 0;
    }
}

ScreenTransitions defaultScreenTransitions() {
    return ScreenTransitions(Transition::crossFade());
}

//********************
// composeTransition
//********************
namespace {

float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

int alphaOf(float fraction) {
    return static_cast<int>(std::lround(255.0f * clamp01(fraction)));
}

TransitionLayer whole(float w, float h, int alpha = 255) {
    TransitionLayer layer;
    layer.drawn = alpha > 0;
    layer.rect = ableem::FRect(0.0f, 0.0f, w, h);
    layer.alpha = alpha;
    return layer;
}

} // namespace

TransitionFrame composeTransition(const Transition &t, bool backwards, float progress, float width, float height,
                                  bool hasOld, bool hasNew) {
    TransitionFrame f;
    const float p = clamp01(progress);
    if (t.kind == TransitionKind::Fade) {
        // through black, whichever way it plays
        if (hasOld && hasNew) {
            if (p < 0.5f)
                f.oldPicture = whole(width, height, alphaOf(1.0f - 2.0f * p));
            else
                f.newPicture = whole(width, height, alphaOf(2.0f * p - 1.0f));
        } else if (hasNew) {
            f.newPicture = whole(width, height, alphaOf(p));
        } else if (hasOld) {
            f.oldPicture = whole(width, height, alphaOf(1.0f - p));
        }
        return f;
    }
    if (t.kind == TransitionKind::None) {
        if (hasNew)
            f.newPicture = whole(width, height);
        return f;
    }

    // the screen whose transition it is moves, over the other: the arriving one forwards, the leaving one backwards.
    // `shown` is how far in it is (1 = in place), `eased` the same on easeOutCubic of the time
    const float eased = ease::outCubic(p);
    const float shown = backwards ? 1.0f - p : p;
    const float shownEased = backwards ? 1.0f - eased : eased;
    TransitionLayer &mover = backwards ? f.oldPicture : f.newPicture;
    TransitionLayer &other = backwards ? f.newPicture : f.oldPicture;
    const bool hasMover = backwards ? hasOld : hasNew;
    const bool hasOther = backwards ? hasNew : hasOld;
    f.oldOnTop = backwards;

    if (hasOther)
        other = whole(width, height);
    if (!hasMover)
        return f;

    switch (t.kind) {
    case TransitionKind::CrossFade:
        mover = whole(width, height, alphaOf(shown));
        break;
    case TransitionKind::Pop: {
        const float scale = PopStartScale + (1.0f - PopStartScale) * shownEased;
        mover = whole(width, height, alphaOf(shown));
        mover.rect = ableem::FRect((width - width * scale) / 2.0f, (height - height * scale) / 2.0f, width * scale,
                                   height * scale);
        break;
    }
    case TransitionKind::Slide: {
        mover = whole(width, height);
        const float away = 1.0f - shownEased; // 1 = all the way off its edge
        switch (t.from) {
        case SlideFrom::Top:
            mover.rect.y = -height * away;
            break;
        case SlideFrom::Bottom:
            mover.rect.y = height * away;
            break;
        case SlideFrom::Left:
            mover.rect.x = -width * away;
            break;
        case SlideFrom::Right:
            mover.rect.x = width * away;
            break;
        }
        // the picture underneath darkens as the mover comes over it
        if (hasOther)
            other.dim = static_cast<int>(std::lround(TransitionDimAlpha * clamp01(shownEased)));
        break;
    }
    default:
        break;
    }
    return f;
}

//********************
// TransitionPlayer
//********************
TransitionPlayer::TransitionPlayer(Tweens &tweens) : tweens_(tweens) {}

void TransitionPlayer::setEnabled(bool on) {
    if (!on)
        finish();
    enabled_ = on;
}

bool TransitionPlayer::arm(const void *target, const Transition &t, bool backwards) {
    finish();
    if (!enabled_ || !t.moves())
        return false;
    armed_ = true;
    started_ = false;
    target_ = target;
    transition_ = t;
    backwards_ = backwards;
    progress_ = 0.0f;
    return true;
}

bool TransitionPlayer::frame(const void *screen) {
    if (!armed())
        return false;
    if (screen == nullptr || screen != target_) {
        finish(); // another picture takes the screen: this one is over
        return false;
    }
    if (!started_) {
        started_ = true;
        progress_ = 0.0f;
        tween_ = startTween(); // busy from this frame on; presented() starts its time again
        clockFromPresent_ = true;
    }
    return armed_;
}

TweenId TransitionPlayer::startTween() {
    return tweens_.start(Tween(progress_, 0.0f, 1.0f, transition_.duration())
                             .delay(transition_.delayMs)
                             .ease(ease::linear)
                             .onEnd([this]() { idle(); }),
                         owner_);
}

void TransitionPlayer::presented() {
    if (!clockFromPresent_)
        return;
    clockFromPresent_ = false;
    if (!running())
        return;
    // the new run first, then the old one cancelled (no write, no callback): the tweens stay busy throughout
    const TweenId first = tween_;
    progress_ = 0.0f;
    tween_ = startTween();
    tweens_.cancel(first);
}

void TransitionPlayer::finish() {
    if (started_ && tweens_.running(tween_))
        tweens_.finish(tween_); // its end callback goes idle
    idle();
}

bool TransitionPlayer::armed() const {
    if (!armed_)
        return false;
    // a run cancelled behind the player's back (Tweens::clear) counts as over
    return !started_ || tweens_.running(tween_);
}

bool TransitionPlayer::running() const {
    return armed_ && started_ && tweens_.running(tween_);
}

void TransitionPlayer::idle() {
    armed_ = false;
    started_ = false;
    clockFromPresent_ = false;
    target_ = nullptr;
    tween_ = NoTween;
    progress_ = 1.0f;
}

} // namespace abgui
