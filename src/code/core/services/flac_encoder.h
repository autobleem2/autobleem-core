//
// FlacEncoder: a small FLAC encoder for 16-bit stereo 44.1 kHz audio, what the CHD "cdfl" codec carries.
//
// Frames only, no "fLaC" marker and no STREAMINFO: a CHD hunk holds just the frames (libchdr puts its own
// header in front of them before dr_flac reads them). Per block it tries the four stereo modes (left/right,
// left/side, right/side, mid/side) and, for each channel, a constant subframe, the fixed predictors of order 0-4
// with Rice-coded residuals (the partition order and the Rice parameters chosen per block), or verbatim; the
// smallest wins. No LPC: the fixed predictors are within a few percent of libFLAC's level 8 on CD audio and need
// no vendored encoder (libFLAC) on every target.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class FlacEncoder {
public:
    // 'samples' stereo sample pairs (interleaved left, right) in blocks of 'blockSize' (the last one may be
    // shorter); the frames of one stream, numbered from 0
    static std::vector<uint8_t> encode(const int16_t *interleaved, size_t samples, uint32_t blockSize);
};
