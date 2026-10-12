//
// LinuxCdDrive: a CdDrive over /dev/sgN with SG_IO (the kernel's SCSI generic interface; no SDL, no libretro
// code). TEST UNIT READY, READ TOC/PMA/ATIP (format 0) and READ CD (0xBE) with the raw P-W subchannel.
//
// The Q channel: a drive may hand the subchannel of a sector a few frames off. On open of a disc (readToc) the
// drive is probed the way the emulator's libretro-cdrom does it: on sectors 16-19 the Q must carry ADR 1 and an
// address within +-2 of the sector, the same shift on each; reads that want Q then ask for lba - shift. Three
// tries, because a spinning-up drive fails the first reads. The parsing and the decisions are static functions,
// tested without a drive.
//
#pragma once

#include "core/services/disc_reader.h"

#include <cstdint>
#include <string>
#include <vector>

//******************
// LinuxCdDrive
//******************
class LinuxCdDrive : public CdDrive {
public:
    enum class Subchannel {
        Unprobed, // no disc probed yet
        None,     // no usable Q channel
        RawPW     // raw P-W read, Q de-interleaved; shift() frames off
    };

    LinuxCdDrive() = default;
    ~LinuxCdDrive() override;
    LinuxCdDrive(const LinuxCdDrive &) = delete;
    LinuxCdDrive &operator=(const LinuxCdDrive &) = delete;

    bool open(int sgIndex, std::string &error); // /dev/sg<sgIndex>
    void close();
    bool isOpen() const { return fd_ >= 0; }
    bool testUnitReady(); // a disc is in and ready

    // CdDrive. readToc probes the subchannel (a new disc). q is filled only when subchannel() is RawPW.
    bool readToc(CdToc &toc, std::string &error) override;
    bool readSector(uint32_t lba, bool audio, uint8_t *data, uint8_t *q) override;
    bool hasSubchannel() const override { return subchannel_ == Subchannel::RawPW; }

    Subchannel subchannel() const { return subchannel_; }
    int shift() const { return shift_; }
    bool probeSubchannel(bool audio); // sectors 16-19, three tries; sets subchannel() and shift()

    // /sys/class/scsi_generic/sg*/device/type == "5" (a CD-ROM): the sg indexes, ascending
    static std::vector<int> findDrives(const std::string &sysfsRoot = "/sys/class/scsi_generic");

    // the pure parts
    // READ TOC format 0 response -> CdToc (the 0xAA descriptor is the lead-out); msf: the addresses are M:S:F
    static bool parseToc(const uint8_t *response, size_t size, bool msf, CdToc &toc, std::string &error);
    // the 12-byte READ CD for one sector: 2352 bytes of main channel, and raw P-W after it when subchannel
    static void readCdCommand(uint8_t *cdb, int32_t lba, bool audio, bool subchannel);
    // count Q channels (12 bytes each, de-interleaved) of the sectors firstLba...: every one ADR 1 with an
    // address within +-2 of its sector's, all the same shift -> true and shift (the Q says sector + shift)
    static bool decideShift(const uint8_t *qs, int count, uint32_t firstLba, int &shift);

private:
    bool command(const uint8_t *cdb, int cdbSize, uint8_t *buffer, size_t size);
    bool rawRead(int32_t lba, bool audio, uint8_t *buffer, bool subchannel);

    int fd_ = -1;
    Subchannel subchannel_ = Subchannel::Unprobed;
    int shift_ = 0;
};
