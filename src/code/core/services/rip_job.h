//
// RipJob: a PS1 disc from a CD drive into one CHD file (the CD-drive ripping of AUTOBLEEM-21; the launcher's
// screen drives it from a worker thread).
//
//   RipJob job;
//   RipJob::Result r = job.start(drive, "/media/games/Spyro.chd", [](const RipJob::Progress &p) { ... });
//   // from the UI thread: job.cancel();
//
// start() blocks until the rip is done, stopped or failed - that is the worker thread's one call; the progress
// callback runs on that thread. The image is every sector from LBA 0 to the lead-out, raw 2352 bytes (a data
// sector with its sync, header and EDC/ECC, an audio sector as it is), written as <name>.chd.part by ChdWriter and
// renamed to <name>.chd when it is complete. A failed or cancelled rip leaves no file behind. The drive runs at
// its top speed (CdDrive::setRipSpeed) while ripping and is put back to 4x after, whatever the outcome.
//
// A sector the drive cannot read is tried 3 times more, then written as zeros and counted: the result says how
// many and which was the first. The subchannel is not stored (zeros in the CHD); LibCrypt's marks are a separate
// read (libcrypt.h).
//
#pragma once

#include "core/services/disc_reader.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

//******************
// RipJob
//******************
class RipJob {
public:
    struct Progress {
        uint32_t sectorsDone = 0;
        uint32_t sectorsTotal = 0;
        double bytesPerSecond = 0; // over the last stretch, not the whole rip
        uint32_t badSectors = 0;   // so far, written as zeros
    };
    using ProgressCallback = std::function<void(const Progress &)>;

    struct Result {
        bool ok = false;
        bool cancelled = false;
        std::string error;        // plain words, when !ok and not cancelled
        std::string file;         // the finished .chd, when ok
        uint32_t sectors = 0;     // in the image (the lead-out's LBA)
        uint32_t badSectors = 0;  // unreadable after the retries, written as zeros
        int64_t firstBadLba = -1; // -1: none
    };

    static const int Retries = 3;

    // Rips the disc in 'drive' (it reads the TOC itself) to outPath ("....chd"). progress may be empty; it is called
    // a few times a second and at the end.
    Result start(CdDrive &drive, const std::string &outPath, const ProgressCallback &progress);
    // any thread: stops at the next hunk (8 sectors), start() then deletes the .part and returns cancelled
    void cancel() { cancelled_ = true; }

    // The space a rip of this disc needs: the raw size (leadout * 2352), conservative - the CHD is smaller.
    static uint64_t neededBytes(const CdToc &toc);
    // true when the filesystem of 'dir' has room for needed bytes; also true when the free space cannot be
    // asked (no guess is better than a refusal). freeBytes: what it asked, 0 when unknown.
    static bool hasFreeSpace(const std::string &dir, uint64_t needed, uint64_t *freeBytes = nullptr);
    // CHT2 TYPE of a data track from its first sector: MODE1_RAW or MODE2_RAW (MODE2_RAW when it has no sync)
    static std::string dataTrackType(const uint8_t *sector2352);

private:
    std::atomic<bool> cancelled_{false};
};
