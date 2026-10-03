//
// The one-shot transitions on tweens (G5o4 of docs/ab-gui-plan.md): the launcher's notification bubble - its slide in, its
// hold, its slide out, and a show()/hide() arriving part-way - and the fade from black, each the old hand-written value
// against the tween that replaced it, at the same times; and that only the slides are busy, never the hold. Pure, on a
// settable clock.
//
// OldBubble is NotificationBubble's state machine as it was (timestamps compared in render()), NewBubble the same
// decisions with the slides on tweens exactly as the launcher's NotificationBubble now has them (startSlide,
// the hold looked at in render()); both are fed one script of frames, shows and hides.
//
#include "doctest/doctest.h"

#include "core/model/timing.h"

#include <ab_gui/transitions.h>
#include <ab_gui/tween.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace std;
using abgui::Tweens;
using abgui::TweenOwner;
namespace transition = abgui::transition;
using FrameNeed = ableem::Input::FrameNeed;

namespace {

const long SlideMs = 250;

// the bubble before G5o4
struct OldBubble {
    enum State { Hidden, SlidingIn, Shown, FadingOut };
    State state = Hidden;
    long since = 0, hideAt = 0, now_ = 0;

    void show(long now, long holdMs) {
        now_ = now;
        if (state == Hidden || state == FadingOut) {
            const long elapsed = state == FadingOut ? max(0L, SlideMs - (now_ - since)) : 0;
            state = SlidingIn;
            since = now_ - elapsed;
        }
        hideAt = holdMs > 0 ? now_ + holdMs : 0;
    }
    void hide() {
        if (state == Hidden || state == FadingOut)
            return;
        state = FadingOut;
        since = now_;
    }
    // how far in it is drawn; -1 = nothing drawn
    float render(long now) {
        now_ = now;
        if (state == Hidden)
            return -1.0f;
        if (state == SlidingIn && now - since >= SlideMs) {
            state = Shown;
            since = now;
        }
        if (state == Shown && hideAt != 0 && now >= hideAt)
            hide();
        if (state == FadingOut && now - since >= SlideMs) {
            state = Hidden;
            return -1.0f;
        }
        float progress = 1.0f;
        if (state == SlidingIn)
            progress = easeOutCubic(static_cast<float>(now - since) / SlideMs);
        else if (state == FadingOut)
            progress = 1.0f - easeOutCubic(static_cast<float>(now - since) / SlideMs);
        return progress;
    }
};

// the bubble on tweens (the launcher's NotificationBubble, without the drawing)
struct NewBubble {
    enum State { Hidden, SlidingIn, Shown, FadingOut };
    Tweens &tweens;
    unsigned int &clock;
    State state = Hidden;
    long since = 0, hideAt = 0, now_ = 0;
    float slideMs = 0;
    TweenOwner owner; // after the float

    NewBubble(Tweens &t, unsigned int &c) : tweens(t), clock(c) {}

    void startSlide(State slide, long sinceTicks) {
        owner.cancel();
        state = slide;
        since = sinceTicks;
        const long gone = max(0L, static_cast<long>(clock) - sinceTicks);
        slideMs = static_cast<float>(min(gone, SlideMs));
        const State ends = slide == SlidingIn ? Shown : Hidden;
        tweens.start(transition::slideClock(slideMs, slideMs).onEnd([this, ends]() { state = ends; }), owner);
    }
    void show(long now, long holdMs) {
        now_ = now;
        if (state == Hidden || state == FadingOut) {
            const long elapsed = state == FadingOut ? max(0L, SlideMs - (now_ - since)) : 0;
            startSlide(SlidingIn, now_ - elapsed);
        }
        hideAt = holdMs > 0 ? now_ + holdMs : 0;
    }
    void hide() {
        if (state == Hidden || state == FadingOut)
            return;
        startSlide(FadingOut, now_);
    }
    float render(long now) {
        now_ = now;
        if (state == Shown && hideAt != 0 && now >= hideAt)
            hide();
        if (state == Hidden)
            return -1.0f;
        float progress = 1.0f;
        if (state != Shown)
            progress = transition::bubbleProgress(state == FadingOut, slideMs);
        return progress;
    }
};

struct Event {
    enum Kind { Frame, Show, Hide };
    long time;
    Kind kind;
    long hold;
};

// frames every `step` ms to `end` (the last one a little past it), the shows and hides among them in time order
// (a show or a hide at a frame's time comes first, as the launcher handles its events before it draws)
vector<Event> script(long step, long end, vector<Event> actions) {
    vector<Event> events = actions;
    for (long t = 0; t <= end; t += step)
        events.push_back({t, Event::Frame, 0});
    stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        if (a.time != b.time)
            return a.time < b.time;
        return a.kind > b.kind; // Show/Hide (1, 2) before Frame (0)
    });
    return events;
}

// runs the script on both and compares at every frame; returns the number of frames that drew the bubble
int compare(const vector<Event> &events) {
    OldBubble oldBubble;
    Tweens tweens;
    unsigned int clock = 0;
    tweens.clock = [&]() { return clock; };
    NewBubble newBubble(tweens, clock);
    int drawn = 0;
    for (const Event &e : events) {
        clock = static_cast<unsigned int>(e.time);
        switch (e.kind) {
        case Event::Show:
            oldBubble.show(e.time, e.hold);
            newBubble.show(e.time, e.hold);
            break;
        case Event::Hide:
            oldBubble.hide();
            newBubble.hide();
            break;
        case Event::Frame: {
            tweens.update(); // the stack advances the tweens before each frame
            const float was = oldBubble.render(e.time);
            const float now = newBubble.render(e.time);
            INFO("frame at " << e.time);
            CHECK((was < 0.0f) == (now < 0.0f));
            if (was >= 0.0f && now >= 0.0f) {
                CHECK(now == doctest::Approx(was).epsilon(1e-4).scale(1.0));
                ++drawn;
            }
            break;
        }
        }
    }
    return drawn;
}

} // namespace

TEST_CASE("bubble: slide in, hold, slide out - every frame as the old timers drew it") {
    for (long step : {1L, 7L, 16L, 17L, 33L, 100L}) {
        INFO("frame step " << step);
        // shown at 100 for 1000 ms
        const int drawn = compare(script(step, 2000, {{100, Event::Show, 1000}}));
        CHECK(drawn > 0);
        // a hold of 0 (until hide()) and a hide() in the middle of the hold
        compare(script(step, 2000, {{100, Event::Show, 0}, {900, Event::Hide, 0}}));
        // a very short hold, ending while the bubble is still sliding in
        compare(script(step, 2000, {{100, Event::Show, 60}}));
    }
}

TEST_CASE("bubble: a hide() after the last frame starts the slide from that frame's time, as before") {
    // frames only every 300 ms: hide() arrives up to 299 ms after the last render, the old slide counting from it
    for (long hideAt : {950L, 1001L, 1199L, 1250L, 1290L}) {
        compare(script(300, 3000, {{100, Event::Show, 0}, {hideAt, Event::Hide, 0}}));
        compare(script(37, 3000, {{100, Event::Show, 0}, {hideAt, Event::Hide, 0}}));
    }
}

TEST_CASE("bubble: a hide() while still sliding in, at every moment of the slide") {
    for (long hideAt = 100; hideAt <= 400; hideAt += 9)
        compare(script(16, 1200, {{100, Event::Show, 0}, {hideAt, Event::Hide, 0}}));
}

TEST_CASE("bubble: a show() on the way out brings it back from the time it had left, as before") {
    // hold 400 from 100: the slide out begins at the first frame at or after 500; a new show at every moment of it
    for (long showAt = 480; showAt <= 800; showAt += 3)
        for (long step : {16L, 33L})
            compare(script(step, 2000, {{100, Event::Show, 400}, {showAt, Event::Show, 300}}));
}

TEST_CASE("bubble: a show() while sliding in or shown only moves the hold (a new setText)") {
    for (long showAt = 100; showAt <= 900; showAt += 11)
        compare(script(16, 2500, {{100, Event::Show, 500}, {showAt, Event::Show, 700}}));
    // and a show after it is gone starts it afresh
    compare(script(16, 4000, {{100, Event::Show, 300}, {2000, Event::Show, 300}}));
}

TEST_CASE("bubble: the slide's first value is where the old code drew it, also from a resumed slide") {
    // show at 1000 (a slide in, from 0) and a hide at 1100 (out from the last frame's time, a slide of 100 ms gone)
    Tweens tweens;
    unsigned int clock = 1000;
    tweens.clock = [&]() { return clock; };
    NewBubble bubble(tweens, clock);
    bubble.show(1000, 0);
    CHECK(bubble.slideMs == 0.0f);
    clock = 1100;
    tweens.update();
    bubble.render(1100);
    CHECK(bubble.slideMs == doctest::Approx(100.0f));
    bubble.hide(); // now_ is 1100: elapsed 0, out from the place it is
    CHECK(bubble.slideMs == 0.0f);
    CHECK(transition::bubbleProgress(true, bubble.slideMs) == 1.0f);
}

TEST_CASE("bubble: only the slides are busy - the hold never is") {
    Tweens tweens;
    unsigned int clock = 0;
    tweens.clock = [&]() { return clock; };
    NewBubble bubble(tweens, clock);
    CHECK_FALSE(tweens.busy());
    bubble.show(0, 5000);
    CHECK(tweens.busy()); // sliding in
    clock = 100;
    tweens.update();
    CHECK(tweens.busy());
    CHECK(tweens.frameNeed() == FrameNeed::Active);
    clock = 250;
    tweens.update();
    CHECK(bubble.state == NewBubble::Shown);
    // the hold: seconds with nothing running, so the DebugDriver's walk is not held
    for (unsigned int t = 250; t < 5000; t += 250) {
        clock = t;
        tweens.update();
        bubble.render(t);
        CHECK_FALSE(tweens.busy());
        CHECK_FALSE(tweens.animating());
        CHECK(tweens.frameNeed() == FrameNeed::Idle);
    }
    // the hold ends: the slide out is busy again, until it is over
    clock = 5000;
    tweens.update();
    bubble.render(5000);
    CHECK(bubble.state == NewBubble::FadingOut);
    CHECK(tweens.busy());
    clock = 5249;
    tweens.update();
    CHECK(tweens.busy());
    clock = 5250;
    tweens.update();
    CHECK(bubble.state == NewBubble::Hidden);
    CHECK_FALSE(tweens.busy());
}

TEST_CASE("bubble: a slide stops with its bubble") {
    Tweens tweens;
    unsigned int clock = 0;
    tweens.clock = [&]() { return clock; };
    {
        NewBubble bubble(tweens, clock);
        bubble.show(0, 0);
        CHECK(tweens.busy());
    }
    clock = 100;
    tweens.update(); // the bubble is gone: nothing writes its float or calls back into it
    CHECK_FALSE(tweens.busy());
    CHECK_FALSE(tweens.animating());
}

TEST_CASE("the slide's curve is easeOutCubic, in and out") {
    for (float ms = 0.0f; ms <= 250.0f; ms += 1.0f) {
        const float t = ms / static_cast<float>(SlideMs);
        CHECK(transition::bubbleProgress(false, ms) == easeOutCubic(t));
        CHECK(transition::bubbleProgress(true, ms) == 1.0f - easeOutCubic(t));
    }
    CHECK(transition::bubbleProgress(false, 250.0f) == 1.0f);
    CHECK(transition::bubbleProgress(true, 250.0f) == 0.0f);
}

TEST_CASE("fade-in: the alpha at every millisecond is the old integer formula") {
    const unsigned int duration = 300; // LauncherFadeInDuration
    for (unsigned int start : {0u, 1u, 5000u, 0xFFFFFF00u}) { // the last one wraps the tick counter during the fade
        Tweens tweens;
        unsigned int clock = start;
        tweens.clock = [&]() { return clock; };
        float fadeMs = 0.0f;
        TweenOwner owner;
        tweens.start(transition::fadeInClock(fadeMs, duration), owner);
        CHECK(transition::fadeInAlpha(fadeMs, duration) == 255);
        for (unsigned int dt = 0; dt <= 420; ++dt) {
            clock = start + dt;
            tweens.update();
            const int oldAlpha = dt >= duration ? 0 : 255 - (255 * static_cast<int>(dt) / static_cast<int>(duration));
            INFO("start " << start << " dt " << dt);
            CHECK(transition::fadeInAlpha(fadeMs, duration) == oldAlpha);
        }
    }
}

TEST_CASE("fade-in: busy while it runs, done at its end, restarted by a new one, stopped with its owner") {
    Tweens tweens;
    unsigned int clock = 1000;
    tweens.clock = [&]() { return clock; };
    float fadeMs = 0.0f;
    {
        TweenOwner owner;
        tweens.start(transition::fadeInClock(fadeMs, 300), owner);
        CHECK(tweens.busy());
        CHECK(tweens.frameNeed() == FrameNeed::Active);
        clock = 1200;
        tweens.update();
        CHECK(tweens.busy());
        CHECK(fadeMs == doctest::Approx(200.0f));
        // again: what GuiLauncher::startFadeIn does (cancel, then a fresh fade from black)
        owner.cancel();
        fadeMs = 0.0f;
        tweens.start(transition::fadeInClock(fadeMs, 300), owner);
        clock = 1300;
        tweens.update();
        CHECK(fadeMs == doctest::Approx(100.0f));
        CHECK(tweens.busy());
        clock = 1500;
        tweens.update();
        CHECK(fadeMs == 300.0f);
        CHECK_FALSE(tweens.busy());
        CHECK(transition::fadeInAlpha(fadeMs, 300) == 0);
        // another duration fits the same recipe (the start-up transitions of decision 12)
        fadeMs = 0.0f;
        tweens.start(transition::fadeInClock(fadeMs, 600), owner);
        clock = 1800;
        tweens.update();
        CHECK(transition::fadeInAlpha(fadeMs, 600) == 255 - 255 * 300 / 600);
    }
    // the owner went with its float: no write afterwards
    const float then = fadeMs;
    clock = 2500;
    tweens.update();
    CHECK(fadeMs == then);
    CHECK_FALSE(tweens.busy());
}
