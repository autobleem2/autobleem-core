#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include "types.h"

namespace ableem {

//******************
// AudioStream
//******************
// A live PCM source played through the SDL_mixer device that Audio::open() opened: 44.1 kHz, signed 16-bit,
// stereo, interleaved. The source is a callback the audio thread calls for every block; it fills the buffer
// (typically from a ring buffer another thread keeps full) and returns how many frames of real sound it wrote -
// the rest of the block is silence. It must be quick and must not block: it runs on the audio thread.
//
// It is installed with Mix_HookMusic, the music slot: the hook's samples go through the mixer exactly like a
// Music's (Mix_VolumeMusic applies, sound effects - Sound::play - still mix over it, and the device's format is
// the mixer's own so nothing is converted). Mix_SetPostMix would also work but sits after the sum of every
// channel - volume and effects would not apply - and has to add into the mix by hand.
//
// A hook replaces the music player, so while a stream plays the theme's Music must be stopped: the CALLER calls
// Music::halt() before start() (a Music playing at the same time is not mixed: SDL_mixer has one music slot) and
// starts the theme's Music again after stop().
class ABLEEM_API AudioStream {
public:
    // out: 2 * frames samples; returns the frames of real sound written (0 = nothing yet: silence)
    using Source = std::function<size_t(int16_t *out, size_t frames)>;

    AudioStream();
    ~AudioStream(); // stops
    AudioStream(const AudioStream &) = delete;
    AudioStream &operator=(const AudioStream &) = delete;

    // false (and a log line): the device is not open, or is not 44100 Hz s16 stereo
    bool start(Source source);
    void stop(); // after it returns the source is never called again
    void pause();
    void resume();
    bool isRunning() const;
    bool isPaused() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace ableem
