#include "ableem/ui/audio_stream.h"
#include "sdl_common.h"
#include <atomic>
#include <cstring>
#include <ableem/engine/log.h>

namespace ableem {

//******************
// AudioStream
//******************
struct AudioStream::Impl {
    Source source;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};

    // the audio thread; SDL_mixer holds its audio lock around it, and Mix_HookMusic takes the same lock
    static void hook(void *udata, Uint8 *stream, int len) {
        Impl *self = static_cast<Impl *>(udata);
        size_t frames = static_cast<size_t>(len) / 4;
        size_t real = 0;
        if (!self->paused.load() && self->source)
            real = self->source(reinterpret_cast<int16_t *>(stream), frames);
        if (real > frames)
            real = frames;
        if (real < frames) // the source may leave the rest untouched: silence
            memset(stream + real * 4, 0, (frames - real) * 4);
    }
};

AudioStream::AudioStream() : impl(new Impl) {}

AudioStream::~AudioStream() {
    stop();
}

bool AudioStream::start(Source source) {
    stop();
    int frequency = 0, channels = 0;
    Uint16 format = 0;
    if (!Mix_QuerySpec(&frequency, &format, &channels)) {
        PLOG_ERROR << "AudioStream: the audio device is not open";
        return false;
    }
    if (frequency != 44100 || format != AUDIO_S16SYS || channels != 2) {
        PLOG_ERROR << "AudioStream: the device is " << frequency << " Hz, format " << format << ", " << channels
                   << " channels; 44100 Hz s16 stereo is needed";
        return false;
    }
    impl->source = std::move(source);
    impl->paused = false;
    impl->running = true;
    Mix_HookMusic(&Impl::hook, impl.get());
    return true;
}

void AudioStream::stop() {
    if (!impl->running.exchange(false))
        return;
    Mix_HookMusic(nullptr, nullptr); // takes the audio lock: the hook is not running when it returns
    impl->source = nullptr;
}

void AudioStream::pause() {
    impl->paused = true;
}

void AudioStream::resume() {
    impl->paused = false;
}

bool AudioStream::isRunning() const {
    return impl->running;
}

bool AudioStream::isPaused() const {
    return impl->paused;
}

} // namespace ableem
