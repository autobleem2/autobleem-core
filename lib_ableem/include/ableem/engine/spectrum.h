// lib_ableem - engine: a spectrum analyser for a music player's bars and scope. Pure maths, no SDL.
//
// analyse() takes the last fftSize stereo frames (s16, interleaved), mixes them to mono, windows (Hann) and runs a
// radix-2 FFT, then folds the bins into log-spaced bars of 0..1: 0 = -60 dB or less, 1 = a full-scale sine. Each
// bar falls back by `decay` per call from its last peak (a peak hold), so a bar drifts down instead of flickering.
// Meant to run 30 times a second on a Cortex-A35: one 1024-point FFT, a table-driven window and a log per bar.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ableem {

class Spectrum {
public:
    // fftSize: a power of two, 64..8192. bars: 1..64, spread log-wise from lowHz to highHz (clamped to Nyquist).
    explicit Spectrum(int bars = 24, int fftSize = 1024, int sampleRate = 44100, float lowHz = 40.0f,
                      float highHz = 16000.0f);

    // frames: the newest samples last; fewer than fftSize are zero-padded at the front, more: only the last used
    void analyse(const int16_t *stereo, size_t frames);
    const std::vector<float> &bars() const { return levels; }
    void reset(); // all bars to 0
    void setDecay(float perCall) { decay = perCall; } // default 0.04: a full bar is gone in under a second at 30/s

    int barCount() const { return static_cast<int>(levels.size()); }
    // band b covers [bandEdge(b), bandEdge(b + 1)) Hz
    float bandEdge(int b) const { return edges[static_cast<size_t>(b)]; }
    int bandOf(float hz) const; // the band holding hz; -1 outside them

    // A scope: `points` values in -1..1 from the stereo frames, mono; each is the sample of largest size in its
    // share of the frames, so a peak is never lost to the downsampling. Fewer frames than points: the rest 0.
    static void waveform(const int16_t *stereo, size_t frames, float *out, int points);

private:
    void fft();

    int n;
    int rate;
    float decay = 0.04f;
    std::vector<float> levels;
    std::vector<float> edges;       // bars + 1
    std::vector<int> binLo, binHi;  // per bar: [lo, hi) bins
    std::vector<float> window;
    std::vector<float> cosT, sinT;  // twiddles, n / 2
    std::vector<int> rev;           // bit reversal
    std::vector<float> re, im;
};

} // namespace ableem
