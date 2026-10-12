//
// Spectrum: a sine lands in its band, silence is all zero, the peaks fall back, the scope keeps the peaks.
//
#include "doctest/doctest.h"

#include <ableem/engine/spectrum.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace std;
using ableem::Spectrum;

namespace {

vector<int16_t> sine(double hz, double amplitude, size_t frames, double phase = 0.0) {
    vector<int16_t> s(2 * frames);
    for (size_t i = 0; i < frames; i++) {
        double v = amplitude * 32767.0 * sin(2 * M_PI * hz * static_cast<double>(i) / 44100.0 + phase);
        s[2 * i] = s[2 * i + 1] = static_cast<int16_t>(lrint(v));
    }
    return s;
}

int loudest(const vector<float> &b) {
    return static_cast<int>(max_element(b.begin(), b.end()) - b.begin());
}

} // namespace

TEST_CASE("Spectrum: a 1 kHz sine lands in the band that holds 1 kHz") {
    Spectrum sp(24);
    vector<int16_t> s = sine(1000, 0.8, 1024);
    sp.analyse(s.data(), 1024);
    const vector<float> &b = sp.bars();
    REQUIRE(b.size() == 24);
    int want = sp.bandOf(1000.0f);
    REQUIRE(want >= 0);
    int got = loudest(b);
    CHECK(abs(got - want) <= 1);
    CHECK(b[static_cast<size_t>(got)] > 0.8f);
    CHECK(b[static_cast<size_t>(got)] <= 1.0f);
    for (int i = 0; i < 24; i++)
        if (abs(i - want) >= 4)
            CHECK(b[static_cast<size_t>(i)] < 0.2f);
}

TEST_CASE("Spectrum: other tones land elsewhere, in order") {
    int last = -1;
    for (double hz : {100.0, 300.0, 1000.0, 4000.0, 12000.0}) {
        Spectrum sp(32);
        vector<int16_t> s = sine(hz, 0.7, 1024, 0.3);
        sp.analyse(s.data(), 1024);
        int got = loudest(sp.bars());
        CHECK(abs(got - sp.bandOf(static_cast<float>(hz))) <= 1);
        CHECK(got > last);
        last = got;
    }
}

TEST_CASE("Spectrum: silence is all zero") {
    Spectrum sp(24);
    vector<int16_t> s(2 * 1024, 0);
    sp.analyse(s.data(), 1024);
    for (float v : sp.bars())
        CHECK(v == 0.0f);
    sp.analyse(nullptr, 0);
    for (float v : sp.bars())
        CHECK(v == 0.0f);
}

TEST_CASE("Spectrum: levels stay within 0..1, a full-scale sine reads about 1") {
    Spectrum sp(16);
    vector<int16_t> s = sine(2000, 1.0, 1024);
    sp.analyse(s.data(), 1024);
    for (float v : sp.bars()) {
        CHECK(v >= 0.0f);
        CHECK(v <= 1.0f);
    }
    CHECK(sp.bars()[static_cast<size_t>(loudest(sp.bars()))] == doctest::Approx(1.0f).epsilon(0.05));
}

TEST_CASE("Spectrum: a peak falls back by the decay each call and ends at zero") {
    Spectrum sp(24);
    vector<int16_t> s = sine(1000, 0.8, 1024);
    sp.analyse(s.data(), 1024);
    int band = loudest(sp.bars());
    float peak = sp.bars()[static_cast<size_t>(band)];
    vector<int16_t> quiet(2 * 1024, 0);
    sp.analyse(quiet.data(), 1024);
    CHECK(sp.bars()[static_cast<size_t>(band)] == doctest::Approx(peak - 0.04f));
    int calls = 0;
    while (sp.bars()[static_cast<size_t>(band)] > 0.0f && calls < 100) {
        sp.analyse(quiet.data(), 1024);
        calls++;
    }
    CHECK(calls < 40);
    CHECK(sp.bars()[static_cast<size_t>(band)] == 0.0f);
    sp.analyse(s.data(), 1024);
    sp.reset();
    for (float v : sp.bars())
        CHECK(v == 0.0f);
}

TEST_CASE("Spectrum: fewer frames than the window are zero-padded, more use the last ones") {
    Spectrum sp(24);
    vector<int16_t> s = sine(1000, 0.8, 300);
    sp.analyse(s.data(), 300);
    CHECK(sp.bars()[static_cast<size_t>(sp.bandOf(1000.0f))] > 0.0f);

    Spectrum a(24), b(24);
    vector<int16_t> hum = sine(200, 0.5, 4096);
    vector<int16_t> tail(hum.end() - 2 * 1024, hum.end());
    a.analyse(hum.data(), 4096);
    b.analyse(tail.data(), 1024);
    for (size_t i = 0; i < 24; i++)
        CHECK(a.bars()[i] == doctest::Approx(b.bars()[i]));
}

TEST_CASE("Spectrum::waveform keeps the peaks of each share") {
    vector<int16_t> s(2 * 1000, 0);
    s[2 * 130] = s[2 * 130 + 1] = 16384;   // a +0.5 peak in the second share
    s[2 * 560] = s[2 * 560 + 1] = -32768;  // a -1 peak in the sixth
    float out[10];
    Spectrum::waveform(s.data(), 1000, out, 10);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == doctest::Approx(0.5f));
    CHECK(out[5] == doctest::Approx(-1.0f));
    CHECK(out[9] == 0.0f);

    float few[4] = {9, 9, 9, 9};
    Spectrum::waveform(s.data(), 2, few, 4);
    CHECK(few[2] == 0.0f);
    CHECK(few[3] == 0.0f);
}

TEST_CASE("Spectrum: cost of a call (printed, not asserted)") {
    Spectrum sp(24);
    vector<int16_t> s = sine(1000, 0.8, 1024);
    const int runs = 2000;
    auto t0 = chrono::steady_clock::now();
    for (int i = 0; i < runs; i++)
        sp.analyse(s.data(), 1024);
    auto us = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - t0).count() / 1000.0 / runs;
    printf("Spectrum::analyse(1024 frames, 24 bars): %.1f us per call\n", us);
    CHECK(us < 5000.0); // not a benchmark: only a guard against something gone badly wrong
}
