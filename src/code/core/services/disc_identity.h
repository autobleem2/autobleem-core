//
// DiscIdentity: what a disc in a CD drive is - a PS1 game (its serial, region and volume label), an audio CD
// (its tracks), another disc, or nothing readable. Pure: it works on any CdDrive (LinuxCdDrive, a fake one).
//
// A data first track is read at sector 16 (the ISO 9660 primary volume descriptor), then the root directory and
// SYSTEM.CNF; "BOOT = cdrom:\SCES_028.35;1" is the serial. No SYSTEM.CNF but a volume label that is a serial
// ("SLUS_005.94") gives the serial too. BOOT2 is a PS2 disc, not a PS1 one.
//
#pragma once

#include "core/services/disc_reader.h"

#include <cstdint>
#include <string>
#include <vector>

//******************
// DiscIdentity
//******************
struct DiscIdentity {
    enum class Kind {
        Ps1Game,   // serial, region, volumeLabel
        AudioCd,   // trackCount, totalFrames, trackLengths
        OtherDisc, // a data disc without a PS1 SYSTEM.CNF (volumeLabel when it has an ISO 9660 one)
        Unreadable // no TOC, or the data could not be read
    };

    Kind kind = Kind::Unreadable;
    std::string serial;      // "SCES-02835"
    std::string region;      // SerialScanner::serialToRegion: "US", "Europe-Aus", "Japan" or ""
    std::string volumeLabel; // the primary volume descriptor's, trailing spaces cut
    int trackCount = 0;
    uint32_t totalFrames = 0;           // the first track's start to the lead-out, in 1/75 s
    std::vector<uint32_t> trackLengths; // frames, in track order

    static DiscIdentity identify(CdDrive &drive);

    // the parts, for the tests
    // SYSTEM.CNF's text -> "SCES-02835" from its BOOT line; "" when there is no BOOT line or no serial in it
    static std::string serialFromSystemCnf(const std::string &text);
    // a volume label that is a serial ("SLUS_005.94", "SLUS00594") -> "SLUS-00594"; "" otherwise
    static std::string serialFromLabel(const std::string &label);
};
