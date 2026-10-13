//
// LibCrypt: the marks of the copy protection of some European PS1 discs, read through a CdDrive's Q channel and
// written as the .sbi file pcsx reads (the emulator needs them to run those games).
//
// The disc carries 16 places (MSF 14105 ... 16167, minus the 2 s lead-in = LBA); at each one the Q channel of the
// sector and of the one 5 sectors on is deliberately wrong (a bad CRC). A place where both sectors have a bad Q CRC
// is a mark - one of the 16 bits of the key; pcsx-abnxt's drive_key reads the same sectors. Nothing is decrypted
// or bypassed here: the Q channel is read as it is on the disc.
//
#pragma once

#include "core/services/disc_reader.h"

#include <cstdint>
#include <string>
#include <vector>

//******************
// LibCrypt
//******************
class LibCrypt {
public:
    static const int Places = 16;
    static const uint32_t PairDistance = 5; // the second sector of a place
    static const uint32_t LeadIn = 150;
    static const int Reads = 2; // a Q is bad when it reads bad this many times in a row

    struct Mark {
        uint32_t lba = 0;
        uint8_t q[12] = {0}; // as the drive gave it, the bad CRC included
    };

    // the place's first sector: MSF frames (minutes * 4500 + seconds * 75 + frames) of place 0..15, and as an LBA
    static uint32_t placeMsf(int place);
    static uint32_t placeLba(int place) { return placeMsf(place) - LeadIn; }

    // true when the Q's CRC (its last 2 bytes) is right
    static bool crcOk(const uint8_t *q12);

    // Reads the 16 places of a disc with 'leadout' sectors; the marks in LBA order, both sectors of a marked place.
    // Empty without a drive Q channel (drive.hasSubchannel() false), on a disc too short, or when nothing is marked.
    // 'places' (optional) gets the number of marked places.
    static std::vector<Mark> scan(CdDrive &drive, uint32_t leadout, int *places = nullptr);

    // the marks as an .sbi ("SBI\0", then per mark 3 BCD MSF bytes, 0x01, the first 10 bytes of the Q)
    static std::string sbi(const std::vector<Mark> &marks);
    static bool writeSbi(const std::string &path, const std::vector<Mark> &marks);
};
