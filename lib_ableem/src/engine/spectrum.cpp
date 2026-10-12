#include "ableem/engine/spectrum.h"

#include <algorithm>
#include <cmath>

namespace ableem {

namespace {
const float Pi = 3.14159265358979f;
const float FloorDb = -60.0f;
} // namespace

Spectrum::Spectrum(int bars, int fftSize, int sampleRate, float lowHz, float highHz) : rate(sampleRate) {
    n = 64;
    while (n < fftSize && n < 8192)
        n <<= 1;
    bars = std::max(1, std::min(bars, 64));
    levels.assign(static_cast<size_t>(bars), 0.0f);

    window.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; i++)
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * Pi * static_cast<float>(i) / static_cast<float>(n));
    cosT.resize(static_cast<size_t>(n / 2));
    sinT.resize(static_cast<size_t>(n / 2));
    for (int i = 0; i < n / 2; i++) {
        cosT[static_cast<size_t>(i)] = std::cos(2.0f * Pi * static_cast<float>(i) / static_cast<float>(n));
        sinT[static_cast<size_t>(i)] = -std::sin(2.0f * Pi * static_cast<float>(i) / static_cast<float>(n));
    }
    rev.resize(static_cast<size_t>(n));
    int bits = 0;
    while ((1 << bits) < n)
        bits++;
    for (int i = 0; i < n; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        rev[static_cast<size_t>(i)] = r;
    }
    re.assign(static_cast<size_t>(n), 0.0f);
    im.assign(static_cast<size_t>(n), 0.0f);

    // log-spaced band edges; every bin belongs to the band its centre frequency falls in. A band too narrow to
    // hold a bin (the lowest ones, at 43 Hz a bin) takes the bin nearest its middle, so no bar is dead.
    float nyquist = static_cast<float>(rate) / 2.0f;
    highHz = std::min(highHz, nyquist);
    lowHz = std::max(1.0f, std::min(lowHz, highHz / 2.0f));
    edges.resize(levels.size() + 1);
    float ratio = std::pow(highHz / lowHz, 1.0f / static_cast<float>(levels.size()));
    for (size_t i = 0; i < edges.size(); i++)
        edges[i] = lowHz * std::pow(ratio, static_cast<float>(i));
    float binHz = static_cast<float>(rate) / static_cast<float>(n);
    int maxBin = n / 2 - 1;
    binLo.resize(levels.size());
    binHi.resize(levels.size());
    for (size_t b = 0; b < levels.size(); b++) {
        int lo = static_cast<int>(std::ceil(edges[b] / binHz - 0.5f));
        int hi = static_cast<int>(std::ceil(edges[b + 1] / binHz - 0.5f)); // exclusive
        lo = std::max(1, lo);
        if (hi <= lo) {
            int mid = static_cast<int>(std::lround(std::sqrt(edges[b] * edges[b + 1]) / binHz));
            lo = std::max(1, std::min(mid, maxBin));
            hi = lo + 1;
        }
        hi = std::min(hi, maxBin + 1);
        binLo[b] = lo;
        binHi[b] = std::max(hi, lo + 1);
    }
}

int Spectrum::bandOf(float hz) const {
    for (size_t b = 0; b < levels.size(); b++)
        if (hz >= edges[b] && hz < edges[b + 1])
            return static_cast<int>(b);
    return -1;
}

void Spectrum::reset() {
    std::fill(levels.begin(), levels.end(), 0.0f);
}

void Spectrum::fft() {
    // the input is in re/im already in bit-reversed order
    for (int len = 2; len <= n; len <<= 1) {
        int half = len / 2;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0, k = 0; j < half; j++, k += step) {
                float wr = cosT[static_cast<size_t>(k)], wi = sinT[static_cast<size_t>(k)];
                size_t a = static_cast<size_t>(i + j), c = static_cast<size_t>(i + j + half);
                float tr = re[c] * wr - im[c] * wi;
                float ti = re[c] * wi + im[c] * wr;
                re[c] = re[a] - tr;
                im[c] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

void Spectrum::analyse(const int16_t *stereo, size_t frames) {
    size_t used = std::min(frames, static_cast<size_t>(n));
    if (stereo == nullptr)
        used = 0;
    const int16_t *src = stereo + 2 * (frames - used);
    size_t pad = static_cast<size_t>(n) - used;
    // mono, windowed, straight into bit-reversed places; the missing front is zeros
    const float scale = 1.0f / 65536.0f; // (L + R) / 2 / 32768
    for (size_t i = 0; i < static_cast<size_t>(n); i++) {
        float v = 0.0f;
        if (i >= pad) {
            size_t f = i - pad;
            v = static_cast<float>(src[2 * f] + src[2 * f + 1]) * scale * window[i];
        }
        size_t r = static_cast<size_t>(rev[i]);
        re[r] = v;
        im[r] = 0.0f;
    }
    fft();

    // a full-scale sine through a Hann window peaks at n / 4 in magnitude
    const float norm = 4.0f / static_cast<float>(n);
    for (size_t b = 0; b < levels.size(); b++) {
        float peak = 0.0f;
        for (int k = binLo[b]; k < binHi[b]; k++) {
            size_t u = static_cast<size_t>(k);
            peak = std::max(peak, re[u] * re[u] + im[u] * im[u]);
        }
        float mag = std::sqrt(peak) * norm;
        float level = 0.0f;
        if (mag > 1e-6f) {
            float db = 20.0f * std::log10(mag);
            level = std::max(0.0f, std::min(1.0f, (db - FloorDb) / -FloorDb));
        }
        levels[b] = std::max(level, levels[b] - decay);
        if (levels[b] < 0.0f)
            levels[b] = 0.0f;
    }
}

void Spectrum::waveform(const int16_t *stereo, size_t frames, float *out, int points) {
    for (int p = 0; p < points; p++) {
        size_t from = frames * static_cast<size_t>(p) / static_cast<size_t>(points);
        size_t to = frames * static_cast<size_t>(p + 1) / static_cast<size_t>(points);
        float best = 0.0f;
        for (size_t f = from; f < to; f++) {
            float v = static_cast<float>(stereo[2 * f] + stereo[2 * f + 1]) / 65536.0f;
            if (std::fabs(v) > std::fabs(best))
                best = v;
        }
        out[p] = best;
    }
}

} // namespace ableem
