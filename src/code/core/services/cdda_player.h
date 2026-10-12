//
// CddaPlayer: an Audio CD played from a CdDrive - the engine only, no screen and no SDL (disc-drive-plan piece 8).
//
// A reader thread pulls the audio sectors (2352 bytes = 588 stereo frames of 16-bit PCM = 1/75 s) of the current
// track into a lock-protected ring buffer about 3 s deep; the sound side pulls PCM out with read() from its own
// thread (ableem::AudioStream's callback). Nothing here blocks the audio thread for longer than a memcpy.
//
// A sector that will not read is tried again `retries` times, then it is a sector of silence and the player goes
// on (badSectors() counts them). stop() joins the thread; the destructor stops.
//
// Order: the audio tracks of the TOC in disc order, or - with shuffle - the current track first, then every other
// one once in a random order. When the order runs out the disc ends (state Stopped) unless repeat is Disc (it
// starts over) or Track (the same track again, forever). next()/prev() follow the order; they ignore Repeat::Track.
//
#pragma once

#include "disc_reader.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

class CddaPlayer {
public:
    static const uint32_t SectorBytes = 2352;
    static const uint32_t SectorFrames = 588;      // stereo sample frames in a sector
    static const uint32_t SectorsPerSecond = 75;
    static const uint32_t SampleRate = 44100;

    enum class State { Stopped, Playing, Paused };
    enum class Repeat { Off, Track, Disc };

    // the drive and the TOC are borrowed: they must outlive the player
    CddaPlayer(CdDrive &drive, const CdToc &toc, int retries = 3, size_t bufferSectors = 225);
    ~CddaPlayer();
    CddaPlayer(const CddaPlayer &) = delete;
    CddaPlayer &operator=(const CddaPlayer &) = delete;

    //******************
    // transport
    //******************
    bool play(int trackNumber); // false: no such track, or not an audio track. Starts at its first sector.
    void pause();
    void resume();
    void stop(); // joins the reader thread; position and track are forgotten
    bool next(); // false: there is none (the last of the order, repeat not Disc)
    bool prev(); // more than 3 s into the track: back to its start; else the one before (the first: its start)
    bool seek(uint32_t frames); // CD frames (1/75 s) from the track's start; clamped to its last sector
    void setRepeat(Repeat repeat);
    void setShuffle(bool on);
    void setShuffleSeed(uint32_t seed) { rng.seed(seed); } // the tests

    //******************
    // for the UI
    //******************
    State state() const;
    Repeat repeat() const;
    bool shuffle() const;
    int track() const;                          // the track number being heard; 0 when stopped
    uint32_t position() const;                  // frames into that track
    uint32_t trackLength(int trackNumber) const; // frames; 0 for an unknown or non-audio track
    uint32_t total() const { return totalFrames; } // frames of all the audio tracks
    int trackCount() const { return static_cast<int>(tracks.size()); } // audio tracks
    int badSectors() const { return bad.load(); }
    uint32_t buffered() const; // sectors in the ring

    //******************
    // for the sound side
    //******************
    // Writes `frames` stereo frames (interleaved s16, so 2 * frames samples) into out. Always fills all of it:
    // what the ring cannot give (paused, stopped, buffer underrun) is silence. Returns the frames of real music.
    size_t read(int16_t *out, size_t frames);
    // the most recent frames handed out by read(), oldest first, for the Spectrum; returns how many (<= frames)
    size_t lastFrames(int16_t *out, size_t frames) const;

private:
    struct Slot {
        std::vector<uint8_t> data; // SectorBytes
        int index = 0;             // into tracks
        uint32_t frame = 0;        // sector within the track
    };
    struct Track {
        int number;
        uint32_t start;
        uint32_t length;
    };

    void run();
    void startAt(int index, uint32_t frame); // with the lock held
    int orderPos(int index) const;            // with the lock held
    int successor(int index, bool manual) const; // with the lock held; -1: the order is over
    void rebuildOrder(int first);
    void readSector(uint32_t lba, uint8_t *data);

    CdDrive &drive;
    const int retries;
    const size_t capacity;
    std::vector<Track> tracks; // the audio tracks
    uint32_t totalFrames = 0;

    mutable std::mutex m;
    std::condition_variable wake;
    std::thread reader;
    bool quit = false;
    State st = State::Stopped;
    Repeat rep = Repeat::Off;
    bool shuf = false;
    std::vector<int> order; // indices into tracks
    std::mt19937 rng{12345};

    uint64_t generation = 0; // bumped by every jump; a sector read for an older one is dropped
    // the reader's cursor
    bool active = false;     // there is something left to read
    int curIndex = 0;
    uint32_t curFrame = 0;
    // the ring
    std::vector<Slot> ring;
    size_t head = 0, count = 0;
    uint32_t offset = 0; // bytes of the front slot already handed out
    // what is being heard
    int nowIndex = -1;
    uint32_t nowFrame = 0;
    // the last frames out, for the analyser
    std::vector<int16_t> history; // HistoryFrames * 2, a ring
    size_t historyPos = 0, historyFill = 0;
    std::atomic<int> bad{0};
};
