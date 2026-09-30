//
// abgui::Tween, Timeline and Tweens (G5o1 of docs/ab-gui-plan.md, 7b): the easings at sample times against the old
// formulas (core/model/timing.h's easeOutCubic and pulseWave, the textbook cubic and back curves), the time rules
// (delay, loop, yoyo, the exact end value), the timeline's order and callbacks, cancel and finish, the owner token
// (a tween whose float went never writes again), the DebugDriver's busy count, the frame need, and the ScreenStack
// advancing the tweens before each outermost frame. Pure, on a settable clock and a recording display; the one
// case that needs a real Input (applyFrameNeed) builds a headless GuiBase and skips itself without a renderer.
//
#include "doctest/doctest.h"

#include "core/model/timing.h"

#include <ab_gui/context.h>
#include <ab_gui/screen_stack.h>
#include <ab_gui/tween.h>

#include <ableem/ui/debug_driver.h>
#include <ableem/ui/gui_base.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::NoTween;
using abgui::ScreenStack;
using abgui::Timeline;
using abgui::Tween;
using abgui::TweenId;
using abgui::TweenOwner;
using abgui::Tweens;
using ableem::DebugDriver;
using FrameNeed = ableem::Input::FrameNeed;

namespace {

// the textbook curves the other easings are (easings.net's), written out here on their own
float refInCubic(float t) {
    return t * t * t;
}
float refInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
float refOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

// the sample times: every 0.05 and a few odd ones
vector<float> sampleTimes() {
    vector<float> ts;
    for (int i = 0; i <= 20; ++i)
        ts.push_back(static_cast<float>(i) / 20.0f);
    for (float t : {0.001f, 0.123f, 0.333f, 0.499f, 0.5f, 0.501f, 0.777f, 0.999f})
        ts.push_back(t);
    return ts;
}

// a Tweens on a clock the test sets
struct Clocked {
    unsigned int now = 1000;
    Tweens tweens;
    Clocked() {
        tweens.clock = [this]() { return now; };
    }
    void at(unsigned int t) {
        now = t;
        tweens.update();
    }
};

// a display that counts the frames
struct Recorder : ScreenStack::Display {
    int clears = 0;
    int presents = 0;
    void setClearColor(const ableem::Color &) override {}
    void clear() override { ++clears; }
    void present() override { ++presents; }
};

} // namespace

//*******************************
// the easings
//*******************************
TEST_CASE("ease::outCubic is timing.h's easeOutCubic, value for value") {
    for (float t : sampleTimes())
        CHECK(abgui::ease::outCubic(t) == easeOutCubic(t));
    for (float t : {-1.0f, -0.01f, 1.01f, 2.0f})
        CHECK(abgui::ease::outCubic(t) == easeOutCubic(t));
    // and through a tween 0 -> 1: the time gone over the duration, exactly as the old code divided it
    float x = 0;
    const Tween tween(x, 0.0f, 1.0f, 110);
    for (unsigned int ms = 0; ms <= 110; ++ms)
        CHECK(tween.valueAt(ms) == easeOutCubic(static_cast<float>(ms) / 110.0f));
}

TEST_CASE("the other easings follow the textbook curves and start at 0, end at 1") {
    for (float t : sampleTimes()) {
        CHECK(abgui::ease::linear(t) == t);
        CHECK(abgui::ease::inCubic(t) == doctest::Approx(refInCubic(t)).epsilon(1e-6));
        CHECK(abgui::ease::inOutCubic(t) == doctest::Approx(refInOutCubic(t)).epsilon(1e-6));
        CHECK(abgui::ease::outBack(t) == doctest::Approx(refOutBack(t)).epsilon(1e-5));
    }
    for (abgui::Easing e :
         {&abgui::ease::outCubic, &abgui::ease::inCubic, &abgui::ease::inOutCubic, &abgui::ease::outBack}) {
        CHECK(e(0.0f) == 0.0f);
        CHECK(e(1.0f) == 1.0f);
        CHECK(e(-0.5f) == 0.0f); // clamped outside the time
        CHECK(e(1.5f) == 1.0f);
    }
    CHECK(abgui::ease::inOutCubic(0.5f) == doctest::Approx(0.5f));
}

TEST_CASE("outBack overshoots by about 10% and comes back") {
    float peak = 0;
    for (int i = 0; i <= 1000; ++i)
        peak = std::max(peak, abgui::ease::outBack(static_cast<float>(i) / 1000.0f));
    CHECK(peak > 1.09f);
    CHECK(peak < 1.11f);
}

TEST_CASE("ease::pulse is pulseWave over one period, and a looping pulse tween is pulseWave itself") {
    for (long ms = 0; ms < 1000; ms += 13)
        CHECK(abgui::ease::pulse(static_cast<float>(ms) / 1000.0f) ==
              doctest::Approx(pulseWave(ms, 1000)).epsilon(1e-5));
    CHECK(abgui::ease::pulse(0.0f) == 0.0f);
    CHECK(abgui::ease::pulse(1.0f) == 0.0f); // the next period's start, as pulseWave's phase 0
    CHECK(abgui::ease::pulse(0.5f) == doctest::Approx(1.0f));

    // Play's pulse / the glow's breathing: base + amplitude x pulseWave(elapsed, period), for as long as it runs
    const long period = 1300;
    const float base = 1.0f;
    const float amp = 0.2f;
    float x = 0;
    Tween tween(x, base, base + amp, period);
    tween.ease(&abgui::ease::pulse).loop();
    for (long ms = 0; ms < 4 * period; ms += 37)
        CHECK(tween.valueAt(static_cast<unsigned int>(ms)) ==
              doctest::Approx(base + amp * pulseWave(ms, period)).epsilon(1e-5));
    CHECK(tween.valueAt(0) == base);
    CHECK(tween.valueAt(static_cast<unsigned int>(period)) == base); // every period's start exactly

    // one pulse, not looping: it ends where it started, exactly
    Tween once(x, base, base + amp, 400);
    once.ease(&abgui::ease::pulse);
    CHECK(once.valueAt(200) == doctest::Approx(base + amp));
    CHECK(once.endedAt(400));
    CHECK(once.valueAt(400) == base);
}

TEST_CASE("tweenLerp is exact at both ends") {
    CHECK(abgui::tweenLerp(0.1f, 0.3f, 0.0f) == 0.1f);
    CHECK(abgui::tweenLerp(0.1f, 0.3f, 1.0f) == 0.3f);
    CHECK(abgui::tweenLerp(-720.0f, 0.0f, 0.5f) == -360.0f);
}

//*******************************
// the time rules
//*******************************
TEST_CASE("a tween: from before its delay, the curve after it, `to` exactly at its end") {
    float x = 0;
    Tween tween(x, 0.1f, 0.3f, 200);
    tween.delay(50).ease(&abgui::ease::linear);
    CHECK(tween.length() == 250);
    CHECK_FALSE(tween.startedAt(49));
    CHECK(tween.startedAt(50));
    CHECK(tween.valueAt(0) == 0.1f);
    CHECK(tween.valueAt(49) == 0.1f);
    CHECK(tween.valueAt(150) == doctest::Approx(0.2f));
    CHECK_FALSE(tween.endedAt(249));
    CHECK(tween.endedAt(250));
    CHECK(tween.valueAt(250) == 0.3f);
    CHECK(tween.valueAt(100000) == 0.3f);
    CHECK(tween.holdsBusy());
    CHECK(tween.easing() == &abgui::ease::linear);
    CHECK(Tween(x, 0, 1, 10).easing() == &abgui::ease::outCubic); // the default
    CHECK(Tween(x, 0, 1, 10).ease(nullptr).easing() == &abgui::ease::linear);
}

TEST_CASE("a zero-length tween is at its end at once") {
    float x = 0;
    const Tween tween(x, 3.0f, 7.0f, 0);
    CHECK(tween.length() == 0);
    CHECK(tween.endedAt(0));
    CHECK(tween.valueAt(0) == 7.0f);
}

TEST_CASE("loop: the same values every duration, never ends, never holds busy") {
    float x = 0;
    Tween tween(x, 0.0f, 10.0f, 100);
    tween.loop().delay(30);
    CHECK(tween.length() == Tween::Forever);
    CHECK_FALSE(tween.holdsBusy());
    for (unsigned int ms = 30; ms < 130; ms += 7) {
        CHECK(tween.valueAt(ms + 100) == tween.valueAt(ms));
        CHECK(tween.valueAt(ms + 12300) == tween.valueAt(ms));
        CHECK_FALSE(tween.endedAt(ms + 1000000));
    }
    CHECK(tween.valueAt(30) == 0.0f); // the delay once, then from at every turn
    CHECK(tween.valueAt(130) == 0.0f);
}

TEST_CASE("yoyo: back along the same curve, ending at `from`; with loop, forever") {
    float x = 0;
    Tween tween(x, 2.0f, 6.0f, 100);
    tween.yoyo();
    CHECK(tween.length() == 200);
    for (unsigned int ms = 0; ms <= 100; ms += 5)
        CHECK(tween.valueAt(200 - ms) == tween.valueAt(ms));
    CHECK(tween.valueAt(100) == 6.0f);
    CHECK(tween.endedAt(200));
    CHECK(tween.valueAt(200) == 2.0f);
    CHECK(tween.valueAt(5000) == 2.0f);

    Tween both(x, 2.0f, 6.0f, 100);
    both.yoyo().loop();
    CHECK(both.length() == Tween::Forever);
    for (unsigned int ms = 0; ms < 200; ms += 9)
        CHECK(both.valueAt(ms + 400) == both.valueAt(ms));
}

TEST_CASE("the timeline's length: a sequence adds up, parallel takes the longest, a loop makes it endless") {
    float a = 0, b = 0;
    Timeline seq = Timeline::sequence();
    seq.add(Tween(a, 0, 1, 100)).wait(50).add(Tween(b, 0, 1, 30).delay(20)).delay(10);
    CHECK(seq.length() == 10 + 100 + 50 + 50);
    Timeline par = Timeline::parallel();
    par.add(Tween(a, 0, 1, 100)).add(Tween(b, 0, 1, 250)).add(seq);
    CHECK(par.length() == 250);
    Timeline endless;
    endless.add(Tween(a, 0, 1, 100).loop()).add(Tween(b, 0, 1, 10));
    CHECK(endless.length() == Tween::Forever);
    CHECK(Timeline().length() == 0);
}

//*******************************
// Tweens
//*******************************
TEST_CASE("Tweens writes the value for the clock's time, and nothing before the delay") {
    Clocked c;
    float x = -1.0f;
    const TweenId id = c.tweens.start(Tween(x, 0.0f, 100.0f, 200).delay(100).ease(&abgui::ease::linear));
    CHECK(id != NoTween);
    CHECK(c.tweens.running(id));
    c.at(1050);
    CHECK(x == -1.0f); // untouched in its delay
    c.at(1200);
    CHECK(x == 50.0f);
    c.at(1300);
    CHECK(x == 100.0f);
    CHECK_FALSE(c.tweens.running(id));
    CHECK(c.tweens.count() == 0);
    x = 7.0f;
    c.at(1400);
    CHECK(x == 7.0f); // an ended tween writes no more
}

TEST_CASE("the end callback is called once, after the end value is written") {
    Clocked c;
    float x = 0;
    int calls = 0;
    float seen = -1;
    c.tweens.start(Tween(x, 0.0f, 1.0f, 100).onEnd([&]() {
        ++calls;
        seen = x;
    }));
    c.at(1099);
    CHECK(calls == 0);
    c.at(1100);
    CHECK(calls == 1);
    CHECK(seen == 1.0f);
    c.at(1100);
    c.at(1500);
    CHECK(calls == 1);
}

TEST_CASE("the timeline: a sequence one after another, parallel together, callbacks in the order things end") {
    Clocked c;
    float a = 0, b = 0, p = 0;
    vector<string> log;
    Timeline inner = Timeline::parallel();
    inner.add(Tween(p, 0, 1, 40).ease(&abgui::ease::linear).onEnd([&]() { log.push_back("p"); }))
        .add(Tween(b, 0, 1, 80).ease(&abgui::ease::linear).onEnd([&]() { log.push_back("b"); }))
        .onEnd([&]() { log.push_back("inner"); });
    Timeline seq = Timeline::sequence();
    seq.add(Tween(a, 0, 1, 100).ease(&abgui::ease::linear).onEnd([&]() { log.push_back("a"); }))
        .wait(20)
        .add(inner)
        .onEnd([&]() { log.push_back("seq"); });
    const TweenId id = c.tweens.start(seq);

    c.at(1050);
    CHECK(a == doctest::Approx(0.5f));
    CHECK(b == 0.0f); // not started
    CHECK(p == 0.0f);
    c.at(1110); // a over, the wait
    CHECK(a == 1.0f);
    CHECK(log == vector<string>{"a"});
    CHECK(b == 0.0f);
    c.at(1140); // 20 ms into the parallel part
    CHECK(p == doctest::Approx(0.5f));
    CHECK(b == doctest::Approx(0.25f));
    CHECK(c.tweens.running(id));
    c.at(1300); // everything at once: p (1160), then b (1200), then inner and seq (1200 - inner first)
    CHECK(p == 1.0f);
    CHECK(b == 1.0f);
    CHECK(log == vector<string>{"a", "p", "b", "inner", "seq"});
    CHECK_FALSE(c.tweens.running(id));
}

TEST_CASE("in a sequence on one float the later tween takes over from the earlier") {
    Clocked c;
    float y = 0;
    Timeline seq;
    seq.add(Tween(y, 0, 10, 100).ease(&abgui::ease::linear)).add(Tween(y, 10, 0, 100).ease(&abgui::ease::linear));
    c.tweens.start(seq);
    c.at(1050);
    CHECK(y == 5.0f);
    c.at(1150);
    CHECK(y == 5.0f); // on the way back
    c.at(1100);       // (the clock is the caller's: the values are the time's)
    CHECK(y == 10.0f);
    c.at(1250);
    CHECK(y == 0.0f);
}

TEST_CASE("runs ending in the same update call back in the order they ended") {
    Clocked c;
    float a = 0, b = 0;
    vector<string> log;
    c.tweens.start(Tween(a, 0, 1, 100).onEnd([&]() { log.push_back("long"); }));
    c.tweens.start(Tween(b, 0, 1, 50).onEnd([&]() { log.push_back("short"); }));
    c.at(1500);
    CHECK(log == vector<string>{"short", "long"});
}

TEST_CASE("an end callback may start the next tween; it starts at that time") {
    Clocked c;
    float x = 0, y = 0;
    TweenId next = NoTween;
    c.tweens.start(
        Tween(x, 0, 1, 100).onEnd([&]() { next = c.tweens.start(Tween(y, 0, 10, 100).ease(&abgui::ease::linear)); }));
    c.at(1100);
    CHECK(next != NoTween);
    CHECK(c.tweens.running(next));
    c.at(1150);
    CHECK(y == 5.0f);
}

TEST_CASE("startAt: a run started at a moment already gone is timed from it, owned and busy like start") {
    const int level = DebugDriver::busyLevel();
    {
        Clocked c;
        c.now = 1025;
        float x = -1.0f;
        TweenOwner owner;
        // started 25 ms ago: the first update writes the value 25 ms in, and it ends 100 ms after 1000, not after 1025
        const TweenId id =
            c.tweens.startAt(1000, Timeline().add(Tween(x, 0.0f, 100.0f, 100).ease(&abgui::ease::linear)), owner);
        CHECK(c.tweens.running(id));
        CHECK(c.tweens.busy());
        CHECK(DebugDriver::busyLevel() == level + 1);
        CHECK(x == -1.0f); // nothing written before the first update
        c.at(1025);
        CHECK(x == 25.0f);
        c.at(1075);
        CHECK(x == 75.0f);
        CHECK(c.tweens.running(id));
        c.at(1100);
        CHECK(x == 100.0f);
        CHECK_FALSE(c.tweens.running(id));
        CHECK(DebugDriver::busyLevel() == level);
        // the owner stops it as it stops a started one
        const TweenId other = c.tweens.startAt(1090, Timeline().add(Tween(x, 0.0f, 1.0f, 100)), owner);
        owner.cancel();
        x = 7.0f;
        c.at(1120);
        CHECK(x == 7.0f);
        CHECK_FALSE(c.tweens.running(other));
        CHECK(DebugDriver::busyLevel() == level);
        // across the clock's wrap
        c.now = 0x00000010u;
        const TweenId wrapped =
            c.tweens.startAt(0xFFFFFFF0u, Timeline().add(Tween(x, 0.0f, 64.0f, 64).ease(&abgui::ease::linear)), owner);
        c.at(0x00000010u); // 32 ms after the start
        CHECK(x == 32.0f);
        CHECK(c.tweens.running(wrapped));
    }
    CHECK(DebugDriver::busyLevel() == level);
}

TEST_CASE("cancel stops a run where it is: no write, no callback") {
    Clocked c;
    float x = 0;
    bool called = false;
    const TweenId id = c.tweens.start(Tween(x, 0, 100, 100).ease(&abgui::ease::linear).onEnd([&]() { called = true; }));
    c.at(1040);
    CHECK(x == 40.0f);
    CHECK(c.tweens.cancel(id));
    CHECK_FALSE(c.tweens.running(id));
    c.at(1500);
    CHECK(x == 40.0f);
    CHECK_FALSE(called);
    CHECK_FALSE(c.tweens.cancel(id)); // gone
    CHECK_FALSE(c.tweens.cancel(12345));
}

TEST_CASE("finish jumps a run to its end: the end values, the callbacks in order; a loop is dropped") {
    Clocked c;
    float a = 0, b = 0, glow = 0;
    vector<string> log;
    Timeline seq;
    seq.add(Tween(a, 0, 5, 100).onEnd([&]() { log.push_back("a"); }))
        .add(Tween(b, 0, 7, 100).yoyo().onEnd([&]() { log.push_back("b"); }))
        .onEnd([&]() { log.push_back("seq"); });
    const TweenId id = c.tweens.start(seq);
    const TweenId loopId = c.tweens.start(Tween(glow, 0, 1, 500).loop());
    c.at(1010);
    CHECK(c.tweens.finish(id));
    CHECK(a == 5.0f);
    CHECK(b == 0.0f); // a yoyo ends where it started
    CHECK(log == vector<string>{"a", "b", "seq"});
    CHECK_FALSE(c.tweens.running(id));
    CHECK_FALSE(c.tweens.finish(id));

    CHECK(c.tweens.running(loopId));
    const float before = glow;
    CHECK(c.tweens.finish(loopId)); // no end to jump to: dropped where it is
    CHECK(glow == before);
    CHECK_FALSE(c.tweens.running(loopId));
}

TEST_CASE("finishNonAmbient finishes the transitions and leaves the ambient loops running") {
    Clocked c;
    float slide = 0, pulse = 0;
    const TweenId slideId = c.tweens.start(Tween(slide, -720, 0, 250));
    const TweenId pulseId = c.tweens.start(Tween(pulse, 1, 1.2f, 1300).ease(&abgui::ease::pulse).loop());
    c.at(1100);
    CHECK(c.tweens.busy());
    c.tweens.finishNonAmbient();
    CHECK(slide == 0.0f);
    CHECK_FALSE(c.tweens.running(slideId));
    CHECK(c.tweens.running(pulseId));
    CHECK_FALSE(c.tweens.busy());
    CHECK(c.tweens.animating());
    c.tweens.clear();
    CHECK_FALSE(c.tweens.animating());
    CHECK(c.tweens.count() == 0);
}

//*******************************
// lifetime: the owner token
//*******************************
TEST_CASE("a tween whose owner is gone never writes again and does not call back") {
    Clocked c;
    float y = 0; // stands for a screen's member: kept alive here so a stray write would show
    bool called = false;
    TweenId id;
    {
        TweenOwner screen;
        id = c.tweens.start(Tween(y, 0, 100, 100).ease(&abgui::ease::linear).onEnd([&]() { called = true; }), screen);
        c.at(1025);
        CHECK(y == 30.0f);
        CHECK(c.tweens.running(id));
    } // the screen is popped and destroyed
    CHECK_FALSE(c.tweens.running(id));
    CHECK(c.tweens.count() == 0);
    CHECK_FALSE(c.tweens.busy());
    y = -5.0f;
    c.at(1050);
    c.at(1200);
    CHECK(y == -5.0f);
    CHECK_FALSE(called);
    CHECK_FALSE(c.tweens.finish(id)); // nothing to jump to either
}

TEST_CASE("TweenOwner::cancel stops what it started so far; it stays usable") {
    Clocked c;
    float a = 0, b = 0;
    TweenOwner owner;
    c.tweens.start(Tween(a, 0, 100, 100).ease(&abgui::ease::linear), owner);
    c.tweens.start(Tween(b, 0, 100, 100).ease(&abgui::ease::linear)); // the program's own: unaffected
    c.at(1010);
    owner.cancel();
    const TweenId again = c.tweens.start(Tween(a, 0, 1, 100), owner);
    c.at(1050);
    CHECK(b == 50.0f);
    CHECK(c.tweens.running(again));
    CHECK(a != 10.0f); // the new one writes (from 0 to 1), the cancelled one would have made it 50
    CHECK(a < 1.0f);
}

TEST_CASE("a callback that closes a screen skips that screen's callbacks still pending") {
    Clocked c;
    float a = 0, b = 0;
    bool second = false;
    unique_ptr<TweenOwner> screen(new TweenOwner);
    c.tweens.start(Tween(a, 0, 1, 50).onEnd([&]() { screen.reset(); })); // ends first
    c.tweens.start(Tween(b, 0, 1, 100).onEnd([&]() { second = true; }), *screen);
    c.at(1200);
    CHECK_FALSE(second);
}

//*******************************
// busy and the frame need
//*******************************
TEST_CASE("the DebugDriver is busy while a non-ambient tween runs, never for ambient ones or loops") {
    const int level = DebugDriver::busyLevel();
    {
        Clocked c;
        float a = 0, b = 0, glow = 0;
        c.tweens.start(Tween(glow, 0, 1, 100).loop());
        c.tweens.start(Tween(b, 0, 1, 100).ambient());
        CHECK(DebugDriver::busyLevel() == level);
        CHECK(c.tweens.frameNeed() == FrameNeed::Ambient);

        c.tweens.start(Tween(a, 0, 1, 100));
        CHECK(DebugDriver::busyLevel() == level + 1);
        c.tweens.start(Tween(a, 0, 1, 300));
        CHECK(DebugDriver::busyLevel() == level + 1); // one step for the whole set
        CHECK(c.tweens.frameNeed() == FrameNeed::Active);
        c.at(1100);
        CHECK(DebugDriver::busyLevel() == level + 1);
        c.at(1300);
        CHECK(DebugDriver::busyLevel() == level);
        CHECK(c.tweens.frameNeed() == FrameNeed::Ambient);

        const TweenId id = c.tweens.start(Tween(a, 0, 1, 100));
        CHECK(DebugDriver::busyLevel() == level + 1);
        c.tweens.cancel(id);
        CHECK(DebugDriver::busyLevel() == level);

        c.tweens.clear();
        CHECK(c.tweens.frameNeed() == FrameNeed::Idle);

        c.tweens.start(Tween(a, 0, 1, 100)); // still running when the Tweens goes
        CHECK(DebugDriver::busyLevel() == level + 1);
    }
    CHECK(DebugDriver::busyLevel() == level);
}

TEST_CASE("a timeline holds busy until its end callback, and an orphaned run gives it back at the next update") {
    const int level = DebugDriver::busyLevel();
    Clocked c;
    float a = 0;
    bool ended = false;
    Timeline t;
    t.add(Tween(a, 0, 1, 100)).wait(100).onEnd([&]() { ended = true; });
    c.tweens.start(t);
    c.at(1150); // the tween is over, the wait is not
    CHECK(c.tweens.busy());
    CHECK(DebugDriver::busyLevel() == level + 1);
    c.at(1200);
    CHECK(ended);
    CHECK(DebugDriver::busyLevel() == level);

    {
        TweenOwner screen;
        c.tweens.start(Tween(a, 0, 1, 100), screen);
        CHECK(DebugDriver::busyLevel() == level + 1);
    }
    CHECK_FALSE(c.tweens.busy());
    c.at(1210);
    CHECK(DebugDriver::busyLevel() == level);
}

TEST_CASE("strongerFrameNeed: Active over Ambient over Idle") {
    CHECK(abgui::strongerFrameNeed(FrameNeed::Idle, FrameNeed::Ambient) == FrameNeed::Ambient);
    CHECK(abgui::strongerFrameNeed(FrameNeed::Active, FrameNeed::Ambient) == FrameNeed::Active);
    CHECK(abgui::strongerFrameNeed(FrameNeed::Ambient, FrameNeed::Idle) == FrameNeed::Ambient);
    CHECK(abgui::strongerFrameNeed(FrameNeed::Idle, FrameNeed::Idle) == FrameNeed::Idle);
}

TEST_CASE("applyFrameNeed raises the Input's frame need and never lowers it") {
#ifdef _WIN32
    _putenv_s("AB_HEADLESS", "1");
#else
    setenv("AB_HEADLESS", "1", 1);
#endif
    unique_ptr<ableem::GuiBase> gui;
    try {
        gui.reset(new ableem::GuiBase("ab_gui_test_tween", 320, 240));
    } catch (const exception &e) {
        MESSAGE("test_ab_gui_tween: skipping - no usable renderer here (" << e.what() << ")");
        return;
    }
    ableem::Input &input = gui->input();
    Clocked c;
    float a = 0, glow = 0;
    input.setFrameNeed(FrameNeed::Idle);
    c.tweens.applyFrameNeed(input);
    CHECK(input.frameNeed() == FrameNeed::Idle);
    const TweenId loopId = c.tweens.start(Tween(glow, 0, 1, 1000).loop());
    c.tweens.applyFrameNeed(input);
    CHECK(input.frameNeed() == FrameNeed::Ambient);
    c.tweens.start(Tween(a, 0, 1, 100));
    c.tweens.applyFrameNeed(input);
    CHECK(input.frameNeed() == FrameNeed::Active);
    c.tweens.clear();
    c.tweens.applyFrameNeed(input);
    CHECK(input.frameNeed() == FrameNeed::Active); // lowering it is the screen's own
    (void)loopId;

    // a Context's clock times the stack's tweens once the Context is set on it
    abgui::Context ctx(gui->renderer(), input, gui->platform());
    unsigned int now = 5000;
    ctx.clock = [&now]() { return now; };
    ScreenStack stack(gui->renderer());
    ctx.setStack(stack);
    CHECK(stack.tweens().now() == 5000);
}

//*******************************
// the ScreenStack drives them
//*******************************
TEST_CASE("the ScreenStack owns the tweens and advances them before each outermost frame") {
    const int level = DebugDriver::busyLevel();
    Recorder display;
    unsigned int now = 2000;
    {
        ScreenStack stack(display);
        Tweens &tweens = stack.tweens();
        tweens.clock = [&now]() { return now; };
        float y = -720.0f;
        int depthInCallback = -1;
        tweens.start(Tween(y, -720.0f, 0.0f, 250).onEnd([&]() { depthInCallback = stack.depth(); }));
        CHECK(DebugDriver::busyLevel() == level + 1);

        float drawn = 1;
        now = 2100;
        stack.frame([&]() { drawn = y; });
        CHECK(drawn == -720.0f + 720.0f * easeOutCubic(100.0f / 250.0f)); // the frame's value, drawn in it
        CHECK(display.presents == 1);

        // a nested frame (a busy tick inside the drawing) does not move them
        now = 2200;
        float inner = 1, outer = 1;
        stack.frame([&]() {
            outer = y;
            now = 2240;
            stack.frame([&]() { inner = y; });
        });
        CHECK(inner == outer);
        CHECK(outer == -720.0f + 720.0f * easeOutCubic(200.0f / 250.0f));

        now = 2300;
        stack.frame([&]() { drawn = y; });
        CHECK(drawn == 0.0f);
        CHECK(depthInCallback == 0); // called outside the frame
        CHECK(DebugDriver::busyLevel() == level);

        // with nothing running a frame is what it always was
        const int presents = display.presents;
        stack.frame([]() {});
        CHECK(display.presents == presents + 1);
    }
    CHECK(DebugDriver::busyLevel() == level);
}

TEST_CASE("the clock's wrap does no harm") {
    Clocked c;
    c.now = 0xFFFFFF00u;
    float x = 0;
    c.tweens.start(Tween(x, 0, 512, 512).ease(&abgui::ease::linear));
    c.at(0x00000010u); // 0x110 ms later
    CHECK(x == 272.0f);
    c.at(0x00000100u);
    CHECK(x == 512.0f);
}
