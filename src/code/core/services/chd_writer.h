//
// ChdWriter: a CD image as a CHD v5 file, laid out the way MAME's chdman writes one (libchdr, the reader in
// lib_ableem/third_party, opens it; this is the writer it does not have).
//
//   header      124 bytes ("MComprHD", version 5), written last when the sizes and SHA1s are known
//   hunks       8 frames of 2448 bytes (2352 sector + 96 subchannel). The subchannel is written as zeros: a
//               drive's raw subchannel carries bad CRCs on scratches, and they would fake LibCrypt marks.
//               The header lists the three CD codecs as chdman does (cdlz, cdzl, cdfl) and every hunk takes the
//               smallest of them: the 8 sectors with LZMA / deflate / FLAC (16-bit stereo, the bytes read
//               big-endian), the 8 subchannels always deflated, laid out as libchdr's decoders read them; a hunk
//               that none of them shrinks is stored as it is. A track of type AUDIO is stored big-endian too, as
//               chdman does: writeSector() takes the drive's little-endian sector and swaps its byte pairs.
//   map         the v5 compressed map (one huffman code for the compression types, then the length and the
//               CRC-16 of every hunk)
//   metadata    one CHT2 entry per track: TRACK / TYPE / SUBTYPE / FRAMES / PREGAP / PGTYPE / PGSUB / POSTGAP
// A track is padded with zero frames to a multiple of 4 (chdman's CD_TRACK_PADDING), the image to a whole
// hunk; the CHT2 FRAMES is the track without its padding. The raw SHA1 is that of the hunks as stored
// (subchannel zeros included), the overall one chdman's: SHA1(raw SHA1, then for each metadata entry that
// has the checksum flag its tag and the SHA1 of its text, sorted).
//
// No file is left half written on purpose: the caller writes <name>.chd.part and renames it (RipJob does).
//
#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//******************
// Sha1
//******************
class Sha1 {
public:
    Sha1();
    void update(const uint8_t *data, size_t size);
    void final(uint8_t *digest20); // the object is spent after this
    static std::string hex(const uint8_t *digest20);

private:
    void block(const uint8_t *p);
    uint32_t h_[5];
    uint8_t buffer_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

//******************
// ChdWriter
//******************
class ChdWriter {
public:
    static const uint32_t SectorBytes = 2352;
    static const uint32_t FrameBytes = 2448;
    static const uint32_t FramesPerHunk = 8;
    static const uint32_t HunkBytes = FrameBytes * FramesPerHunk;
    static const uint32_t TrackPadding = 4;
    static const uint32_t HeaderBytes = 124;

    struct Track {
        int number = 1;
        std::string type = "MODE2_RAW"; // MODE1_RAW, MODE2_RAW or AUDIO
        uint32_t frames = 0;            // the sectors of the track in the image, without the padding
    };

    ChdWriter() = default;
    ~ChdWriter();
    ChdWriter(const ChdWriter &) = delete;
    ChdWriter &operator=(const ChdWriter &) = delete;

    // Full hunks are compressed by CompressThreads worker threads (each hunk by one, the codecs' state is the
    // worker's own) and written in hunk order: the file is the same as a single-threaded write. At most
    // 2 * CompressThreads hunks are in flight (queued, being compressed, or waiting for their turn to be written);
    // the reader waits when that is reached. true = no thread, the writing thread does it all (for the tests).
    // Set before open().
    static const unsigned DefaultCompressThreads = 3;
    void setSynchronous(bool synchronous) { synchronous_ = synchronous; }
    void setCompressThreads(unsigned threads) { compressThreads_ = threads < 1 ? 1 : threads; }

    // creates the file; tracks in order, each with at least one frame
    bool open(const std::string &path, const std::vector<Track> &tracks, std::string &error);
    // the next sector of the image (2352 bytes), track after track; the padding is added by the writer
    bool writeSector(const uint8_t *sector, std::string &error);
    // the last hunk, the map, the metadata and the header; every sector of every track must be written. Waits for
    // the workers; an error of theirs is reported here or by the next writeSector().
    bool finish(std::string &error);
    // stops the workers (hunks still queued are dropped) and closes the file as it is (the caller deletes it)
    void abort();

    // the parts, for the tests
    static uint32_t paddedFrames(uint32_t frames);
    static uint32_t totalFrames(const std::vector<Track> &tracks); // padded, and rounded up to a whole hunk
    static std::string trackMetadata(const Track &track);          // the CHT2 text, without its NUL
    static uint16_t crc16(const uint8_t *data, size_t size);       // CRC-16/CCITT, start 0xffff, no final xor
    // one hunk (HunkBytes) as cdlz, cdzl or cdfl; false when it does not get smaller than the hunk
    static bool cdlzHunk(const uint8_t *hunk, std::vector<uint8_t> &out);
    static bool cdzlHunk(const uint8_t *hunk, std::vector<uint8_t> &out);
    static bool cdflHunk(const uint8_t *hunk, std::vector<uint8_t> &out);
    // the smallest of the three in 'out' and its type (0 cdlz, 1 cdzl, 2 cdfl), or 4 (stored) and 'out' untouched
    static uint8_t smallestHunk(const uint8_t *hunk, std::vector<uint8_t> &out);
    // the v5 map of the hunks: type 0-2 (the header's cdlz, cdzl, cdfl) or 4 (stored), the lengths and CRCs;
    // firstOffset = where hunk 0 is
    struct HunkEntry {
        uint8_t type = 4;
        uint32_t length = 0;
        uint16_t crc = 0;
    };
    static std::vector<uint8_t> packMap(const std::vector<HunkEntry> &hunks, uint64_t firstOffset);
    // how many hunks of the image got this type (valid after finish)
    uint32_t hunksOfType(uint8_t type) const { return type < hunksOfType_.size() ? hunksOfType_[type] : 0; }

private:
    bool pushFrame(const uint8_t *sector, bool swapBytes, std::string &error);
    bool flushHunk(std::string &error);
    // one hunk compressed, with what its map entry and the file need
    struct Packed {
        std::vector<uint8_t> raw;    // the hunk as it came
        std::vector<uint8_t> packed; // the smallest codec's bytes, unless stored
        HunkEntry entry;
    };
    struct Job {
        uint32_t index;
        std::vector<uint8_t> hunk;
    };
    static void compressHunk(Packed &hunk); // pure: touches nothing of the writer
    bool writeHunk(uint32_t index, const Packed &hunk, std::string &error); // in hunk order only
    bool processHunk(std::vector<uint8_t> hunk, std::string &error);       // synchronous: compress and write
    void workerLoop();
    void writeReady();            // writes the results that are next in line, if no one else is doing it
    void fail(const std::string &error);
    bool stopWorkers(bool drain); // joins the workers; drain: they write what is queued first
    bool put(const uint8_t *data, size_t size, std::string &error);

    std::ofstream out_;
    std::vector<Track> tracks_;
    size_t track_ = 0;
    uint32_t inTrack_ = 0;
    std::vector<uint8_t> hunk_;
    uint32_t frameInHunk_ = 0;
    uint32_t hunksDone_ = 0, hunkCount_ = 0;
    uint64_t logicalBytes_ = 0; // the padded tracks, as chdman: the last hunk may be only partly logical
    std::vector<HunkEntry> entries_;
    std::array<uint32_t, 8> hunksOfType_{};
    uint64_t offset_ = 0;
    Sha1 raw_;

    // the workers (out_, raw_, entries_, hunksOfType_ and offset_ belong to whoever is writing, see writing_)
    bool synchronous_ = false;
    unsigned compressThreads_ = DefaultCompressThreads;
    bool open_ = false; // the writing thread's view of "the file is open"
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable notEmpty_, notFull_;
    std::deque<Job> queue_;
    std::map<uint32_t, Packed> results_; // compressed, waiting for their turn
    uint32_t pushed_ = 0;                // hunks handed to the queue; hunksDone_ of them are written
    bool writing_ = false;               // some worker is writing, outside the mutex
    bool stop_ = false;                  // abort: leave at once
    bool closing_ = false;               // finish: leave when the queue is empty
    bool failed_ = false;
    std::string workerError_;
};
