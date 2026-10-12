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
    // one READ CD for 'count' sectors (2352 bytes each, no subchannel)
    bool readSectors(uint32_t lba, uint32_t count, bool audio, uint8_t *data) override;
    bool hasSubchannel() const override { return subchannel_ == Subchannel::RawPW; }
    // SET CD SPEED: the top speed while ripping, 4x after (the CdDrive hook RipJob calls)
    bool setRipSpeed(bool maximum) override { return setSpeed(maximum ? MaxSpeed : Speed4x); }

    enum : unsigned {
        MaxSpeed = 0xffff, // "as fast as the drive goes"
        Speed4x = 706      // kB/s, 4 * 176.4
    };
    bool setSpeed(unsigned kbPerSecond);
    // the raw P-W subchannel of one sector, 96 bytes as the drive gives it (READ CD with sub-channel selection,
    // no shift applied - readSector() is the shifted Q). The 2352 main bytes are read and dropped.
    bool readSubcode(uint32_t lba, bool audio, uint8_t *subcode96);

    Subchannel subchannel() const { return subchannel_; }
    int shift() const { return shift_; }
    bool probeSubchannel(bool audio); // sectors 16-19, three tries; sets subchannel() and shift()

    // /sys/class/scsi_generic/sg*/device/type == "5" (a CD-ROM): the sg indexes, ascending
    static std::vector<int> findDrives(const std::string &sysfsRoot = "/sys/class/scsi_generic");

    // the pure parts
    // READ TOC format 0 response -> CdToc (the 0xAA descriptor is the lead-out); msf: the addresses are M:S:F
    static bool parseToc(const uint8_t *response, size_t size, bool msf, CdToc &toc, std::string &error);
    // the 12-byte READ CD for 'count' sectors: 2352 bytes of main channel each, and raw P-W after it when subchannel
    static void readCdCommand(uint8_t *cdb, int32_t lba, bool audio, bool subchannel, uint32_t count = 1);
    // count Q channels (12 bytes each, de-interleaved) of the sectors firstLba...: every one ADR 1 with an
    // address within +-2 of its sector's, all the same shift -> true and shift (the Q says sector + shift)
    static bool decideShift(const uint8_t *qs, int count, uint32_t firstLba, int &shift);
    // How readSector gets a sector. The emulator shifts the Q only: the main channel is read at lba, the Q of
    // the sector at lba - shift. shift 0 (or no Q wanted): one read at lba, with the subchannel when Q is wanted.
    // shift != 0: a read of the main channel at lba without the subchannel, then (extraSubRead) a read of
    // sector + subchannel at subLba, of which only the 96 subchannel bytes are kept.
    struct ReadPlan {
        int32_t mainLba = 0;
        bool mainWithSub = false;
        bool extraSubRead = false;
        int32_t subLba = 0;
    };
    static ReadPlan planRead(int32_t lba, int shift, bool wantQ);
    // the 12-byte SET CD SPEED (0xBB), read speed in kB/s
    static void setSpeedCommand(uint8_t *cdb, unsigned kbPerSecond);
    // 96 raw P-W bytes -> Q's 12 bytes (de-interleaved); true when its CRC is right
    static bool decodeQ(const uint8_t *subcode96, uint8_t *q12);

private:
    bool command(const uint8_t *cdb, int cdbSize, uint8_t *buffer, size_t size);
    bool rawRead(int32_t lba, bool audio, uint8_t *buffer, bool subchannel);

    int fd_ = -1;
    Subchannel subchannel_ = Subchannel::Unprobed;
    int shift_ = 0;
};
