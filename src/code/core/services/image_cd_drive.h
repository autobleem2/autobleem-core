//
// ImageCdDrive: a CdDrive backed by a disc image - a cue sheet with its bin(s), and optionally an .sbi with the Q
// channel of the LibCrypt sectors - so the disc-drive flow can be run with no drive (unit tests, and the VM UI
// tests through a dev-only switch).
//
// The cue: FILE "x.bin" BINARY, TRACK nn MODE1/2352 | MODE2/2352 | AUDIO, INDEX 00 / 01 mm:ss:ff, PREGAP. One bin
// for the disc, or one per track (a file's sectors follow the previous file's on the disc). INDEX 00 is the
// track's pregap, as DiscReader::cue writes it; a PREGAP line is silence that is not in any file.
// The Q channel of a sector is DiscReader::expectedQ for it - what a clean disc says there - and where the .sbi has
// an entry for the sector, that entry's 10 bytes, the CRC left as the clean Q has it (so it does not fit, which is
// how LibCrypt's sectors look to a drive). The .sbi is "<cue name>.sbi" next to the cue when there is one.
//
#pragma once

#include "disc_reader.h"

#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ImageCdDrive : public CdDrive {
public:
    // false and a plain-words error when the cue, a bin or the .sbi is missing or makes no sense. sbiPath "": the
    // .sbi next to the cue, if any; a path given must exist.
    bool open(const std::string &cuePath, std::string &error, const std::string &sbiPath = "");

    bool readToc(CdToc &toc, std::string &error) override;
    // Like a real drive, a sector is read as the kind it is: false when audio does not match its track.
    bool readSector(uint32_t lba, bool audio, uint8_t *data, uint8_t *q) override;
    bool hasSubchannel() const override { return true; }

    const CdToc &toc() const { return toc_; }
    size_t sbiEntries() const { return sbi_.size(); }

private:
    struct Segment {
        uint32_t start = 0; // disc LBA
        uint32_t count = 0;
        int file = -1; // index into files_; -1: silence, not in a file
    };
    struct File {
        std::string path;
        uint32_t sectors = 0;
        std::unique_ptr<std::ifstream> stream;
    };

    bool loadSbi(const std::string &path, std::string &error);

    CdToc toc_;
    std::vector<File> files_;
    std::vector<Segment> segments_;
    std::map<uint32_t, std::vector<uint8_t>> sbi_; // lba -> Q's first 10 bytes
};
