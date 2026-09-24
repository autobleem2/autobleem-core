//
// DiscReader: a PS1 disc from a CD drive into a game folder a library can serve - pc-tools' LAN Share "Read a
// disc" (autobleem-pc-tools docs/lan-share-plan.md, step 3).
//
// What it writes, in <library>/<Title>/:
//   <Title>.bin  the whole disc, every track, 2352 bytes a sector from LBA 0 to the lead-out (a data sector raw,
//                with its sync and header; an audio sector as it is)
//   <Title>.cue  its tracks: MODE1/2352 or MODE2/2352 for data (the first sector's mode byte says which),
//                AUDIO for the rest, INDEX 00 where a pregap is known and INDEX 01 at each track's start
//   <Title>.sbi  when the subchannel carries LibCrypt's marks: the sectors whose Q channel is not what their
//                place on the disc says (a bad CRC, or another address), in the .sbi layout pcsx reads
// The title comes from the serial (SerialScanner over the new image, MetadataLookup over the covers databases and
// the rdb). A game on several discs goes into one folder as "<Title> (Disc N)", the layout a library serves as
// one game with its discs in order.
//
// Nothing is decrypted or bypassed: a PS1 disc is plain data, and the Q channel is read as it is on the disc.
// The drive is an interface - Windows' IOCTLs are pc-tools' back end (WinCdDrive), the tests have a fake one.
//
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

//******************
// CdTrack / CdToc / CdDrive
//******************
struct CdTrack {
    int number = 0;
    bool audio = false;
    uint32_t start = 0;  // its INDEX 01, as an LBA (0 = the first sector after the 2 s lead-in)
    uint32_t pregap = 0; // sectors of INDEX 00 before start, when the drive says; 0 = none known
};

struct CdToc {
    std::vector<CdTrack> tracks; // in order
    uint32_t leadout = 0;        // the LBA after the last sector
};

class CdDrive {
public:
    virtual ~CdDrive() = default;
    virtual bool readToc(CdToc &toc, std::string &error) = 0;
    // One sector: 2352 bytes into data (read as an audio sector or as a data sector), and when the drive gives
    // the subchannel (hasSubchannel), its Q channel - 12 bytes, de-interleaved, CRC included - into q. false:
    // the sector could not be read.
    virtual bool readSector(uint32_t lba, bool audio, uint8_t *data, uint8_t *q) = 0;
    virtual bool hasSubchannel() const = 0;
};

//******************
// DiscReader
//******************
class DiscReader {
public:
    static const uint32_t SectorSize = 2352;

    struct Options {
        std::string libraryDir; // where the game's folder goes
        std::string coversDir;  // covers{U,P,J}.db, for the title
        std::string rdbFile;    // "Sony - PlayStation.rdb", for the title and the check
        int discNumber = 0;     // 0: a game on one disc; 1, 2, ...: that disc of a game on several
        std::string gameFolder; // disc 2 and on: the folder disc 1 went into (Result::folder)
        std::string title;      // disc 2 and on: the title disc 1 got (Result::title)
        int retries = 3;        // a sector that will not read is tried this many times more
    };

    enum class Verified {
        Unknown, // no rdb, or it has no checksum for this disc
        Matches, // the data track is the known good dump's
        Differs  // it is not: a scratch the drive read around, or another release of the game
    };

    struct Result {
        bool ok = false;
        std::string error;    // plain words, when !ok
        std::string folder;   // <library>/<Title>
        std::string cueFile;  // the .cue in it
        std::string title;    // what the folder is named after
        std::string serial;   // "SLUS-00594"; "" when the disc has none
        uint32_t sectors = 0; // read, the whole disc
        uint32_t dataCrc = 0; // the first track's CRC-32, as a per-track dump names it
        Verified verified = Verified::Unknown;
        std::vector<uint32_t> badSectors;  // not readable after the retries - written as zeros
        bool subchannel = false;           // the drive gave the Q channel
        bool subchannelUnreliable = false; // so many Q mismatches that it is the drive, not LibCrypt - no .sbi
        int sbiSectors = 0;                // the LibCrypt sectors written to the .sbi
    };

    // Reads the whole disc. progress(done, total) is asked every few hundred sectors; returning false stops the
    // read (the result says "stopped", nothing is left behind). Works in <library>/.reading-disc/ and moves the
    // files into the game's folder when the disc is done.
    static Result read(CdDrive &drive, const Options &options,
                       const std::function<bool(uint32_t done, uint32_t total)> &progress);

    // the parts, for the tests and for pc-tools
    static uint16_t qCrc(const uint8_t *q10);                          // CRC-16/CCITT of Q's first 10 bytes, inverted
    static void expectedQ(const CdToc &toc, uint32_t lba, uint8_t *q); // what a clean Q says there (12 bytes)
    static std::string msf(uint32_t frames);                           // "mm:ss:ff" for a cue
    static std::string cue(const CdToc &toc, const std::string &binName, bool firstTrackMode1);
    static std::string sbi(const std::vector<std::pair<uint32_t, std::vector<uint8_t>>> &entries); // lba, Q
    // the Q channel's 96 interleaved subcode bytes (P-W, one bit each) -> Q's 12 bytes
    static void deinterleaveQ(const uint8_t *subcode96, uint8_t *q);
};
