//
// ChdWriter: a CD image as a CHD v5 file, laid out the way MAME's chdman writes one (libchdr, the reader in
// lib_ableem/third_party, opens it; this is the writer it does not have).
//
//   header      124 bytes ("MComprHD", version 5), written last when the sizes and SHA1s are known
//   hunks       8 frames of 2448 bytes (2352 sector + 96 subchannel). The subchannel is written as zeros: a
//               drive's raw subchannel carries bad CRCs on scratches, and they would fake LibCrypt marks.
//               Each hunk is "cdzl": a deflate of the 8 sectors and a deflate of the 8 subchannels (chdman's
//               own codec); a hunk that does not shrink is stored as it is
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

#include <cstdint>
#include <fstream>
#include <string>
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

    // creates the file; tracks in order, each with at least one frame
    bool open(const std::string &path, const std::vector<Track> &tracks, std::string &error);
    // the next sector of the image (2352 bytes), track after track; the padding is added by the writer
    bool writeSector(const uint8_t *sector, std::string &error);
    // the last hunk, the map, the metadata and the header; every sector of every track must be written
    bool finish(std::string &error);
    // closes the file as it is (the caller deletes it)
    void abort();

    // the parts, for the tests
    static uint32_t paddedFrames(uint32_t frames);
    static uint32_t totalFrames(const std::vector<Track> &tracks); // padded, and rounded up to a whole hunk
    static std::string trackMetadata(const Track &track);          // the CHT2 text, without its NUL
    static uint16_t crc16(const uint8_t *data, size_t size);       // CRC-16/CCITT, start 0xffff, no final xor
    // one hunk (HunkBytes) as cdzl; false when it does not get smaller than the hunk
    static bool cdzlHunk(const uint8_t *hunk, std::vector<uint8_t> &out);
    // the v5 map of the hunks: type 0 (cdzl) or 4 (stored), the lengths and CRCs; firstOffset = where hunk 0 is
    struct HunkEntry {
        uint8_t type = 4;
        uint32_t length = 0;
        uint16_t crc = 0;
    };
    static std::vector<uint8_t> packMap(const std::vector<HunkEntry> &hunks, uint64_t firstOffset);

private:
    bool pushFrame(const uint8_t *sector, std::string &error);
    bool flushHunk(std::string &error);
    bool put(const uint8_t *data, size_t size, std::string &error);

    std::ofstream out_;
    std::vector<Track> tracks_;
    size_t track_ = 0;
    uint32_t inTrack_ = 0;
    std::vector<uint8_t> hunk_;
    uint32_t frameInHunk_ = 0;
    uint32_t hunksDone_ = 0, hunkCount_ = 0;
    std::vector<HunkEntry> entries_;
    uint64_t offset_ = 0;
    Sha1 raw_;
};
