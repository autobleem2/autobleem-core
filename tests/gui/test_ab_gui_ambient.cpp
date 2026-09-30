//
// The ambient motion on tweens (G5o2 of docs/ab-gui-plan.md): Play's pulse, the arrow's bob and the cover glow's
// breathing, each the old hand-written value (core/model/timing.h's pulseWave, and sin(ticks / 900)) against the tween
// that replaced it, at the same times; and that an ambient loop never holds the DebugDriver's busy, only the frame need
// Ambient, and stops with its owner. Pure, on a settable clock.
//
#include "doctest/doctest.h"

#include "core/model/timing.h"

#include <ab_gui/ambient.h>
#include <ab_gui/tween.h>

#include <cmath>
#include <vector>

using namespace std;
using abgui::Tweens;
using abgui::TweenOwner;
using FrameNeed = ableem::Input::FrameNeed;

namespace {

// the times after a start: every 37 ms over three Play periods, plus the turns themselves
vector<unsigned int> sampleOffsets(unsigned int periodMs) {
    vector<unsigned int> ts;
    for (unsigned int t = 0; t <= 3 * periodMs; t += 37)
        ts.push_back(t);
    for (unsigned int k = 0; k <= 6; ++k) {
        ts.push_back(k * periodMs / 2);
        ts.push_back(k * periodMs / 2 + 1);
        ts.push_back(k * periodMs / 2 - (k > 0 ? 1 : 0));
    }
    return ts;
}

} // namespace

TEST_CASE("Play's pulse: zoom = 1 + 0.2 x pulseWave, at every sample time") {
    const unsigned int period = 2000;
    for (unsigned int start : {0u, 1u, 731u, 123456u}) {
        Tweens tweens;
        tweens.clock = [&]() { return start; };
        float zoom = 1.0f;
        TweenOwner owner;
        tweens.start(abgui::ambient::pulse(zoom, 1.0f, 1.2f, period), owner);
        for (unsigned int dt : sampleOffsets(period)) {
            tweens.update(start + dt);
            const float oldZoom = 1.0f + (1.20f - 1.0f) * pulseWave(static_cast<long>(dt), static_cast<long>(period));
            CHECK(zoom == doctest::Approx(oldZoom).epsilon(1e-6));
        }
    }
}

TEST_CASE("the arrow's bob: originaly + 15 x pulseWave, at every sample time") {
    const unsigned int period = 1000;
    const int originaly = 360, maxMove = 15;
    Tweens tweens;
    unsigned int now = 4000;
    tweens.clock = [&]() { return now; };
    float drawY = static_cast<float>(originaly);
    TweenOwner owner;
    tweens.start(abgui::ambient::pulse(drawY, static_cast<float>(originaly), static_cast<float>(originaly + maxMove),
                                       period),
                 owner);
    for (unsigned int dt : sampleOffsets(period)) {
        now = 4000 + dt;
        tweens.update();
        const float oldY = originaly + maxMove * pulseWave(static_cast<long>(dt), static_cast<long>(period));
        CHECK(drawY == doctest::Approx(oldY).epsilon(1e-6));
    }
}

TEST_CASE("the glow's breathing: 0.9 + 0.1 x sin(ticks / 900), from any start, over hours of uptime") {
    // the old code followed the clock itself: sin(float(now) / 900), so the tween has to land on the same phase whenever
    // it started, and stay there (15 turns come to 84823.002 ms - the wrap is 2 microseconds off)
    for (unsigned int start : {0u, 1u, 5000u, 84822u, 84823u, 90000u, 3600000u, 7200123u}) {
        Tweens tweens;
        unsigned int now = start;
        tweens.clock = [&]() { return now; };
        float phase = abgui::ambient::clockPhaseStart(start);
        TweenOwner owner;
        tweens.start(abgui::ambient::clockPhase(phase, start), owner);
        // the first value, before any update, is already the old one
        // 1e-4: a large tick count as a float (the old formula) loses digits the wrapped phase keeps
        CHECK(0.9f + 0.1f * sin(phase / 900.0f) ==
              doctest::Approx(0.9f + 0.1f * sin(static_cast<float>(start) / 900.0f)).epsilon(1e-4));
        // and every sample after it, past two wraps
        for (unsigned int dt = 1; dt <= 200000; dt += 997) {
            now = start + dt;
            tweens.update();
            const float oldBreath = 0.9f + 0.1f * sin(static_cast<float>(now) / 900.0f);
            const float newBreath = 0.9f + 0.1f * sin(phase / 900.0f);
            CHECK(std::fabs(newBreath - oldBreath) < 2e-4f);
            CHECK(newBreath >= 0.8f - 1e-6f);
            CHECK(newBreath <= 1.0f + 1e-6f);
        }
    }
}

TEST_CASE("an ambient loop is never busy, asks for the Ambient frame need and stops with its owner") {
    Tweens tweens;
    unsigned int now = 0;
    tweens.clock = [&]() { return now; };
    float zoom = 1.0f, y = 0.0f, phase = 0.0f;
    {
        TweenOwner owner;
        tweens.start(abgui::ambient::pulse(zoom, 1.0f, 1.2f, 2000), owner);
        tweens.start(abgui::ambient::pulse(y, 360.0f, 375.0f, 1000), owner);
        tweens.start(abgui::ambient::clockPhase(phase, 0), owner);
        now = 500;
        tweens.update();
        CHECK_FALSE(tweens.busy());
        CHECK(tweens.animating());
        CHECK(tweens.frameNeed() == FrameNeed::Ambient);
        CHECK(zoom > 1.0f);
    }
    // the owner went: nothing writes its floats any more, and nothing runs
    const float zoomThen = zoom, yThen = y, phaseThen = phase;
    now = 900;
    tweens.update();
    CHECK(zoom == zoomThen);
    CHECK(y == yThen);
    CHECK(phase == phaseThen);
    CHECK_FALSE(tweens.animating());
    CHECK(tweens.frameNeed() == FrameNeed::Idle);
}
