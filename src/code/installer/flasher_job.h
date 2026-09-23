//
// FlasherJob: the PC USB stick's image onto a stick - the job behind autobleem-pc-tools' AutoBleemFlasher.
// The image is a channel's (release: pc/images/release.json, testing: pc/images/testing.json, nightly:
// nightly/latest.json's "pc-i386" image - each falling back as the installers' channels do) or a local
// .img.xz; it is downloaded and checked against its published sha256, decoded as it is written (ableem::XzFile
// - the 4 GB image is never unpacked to a file), written with the partition table last so Windows does not
// mount the new partitions half way, and read back and compared.
//
// The disk itself is a DiskTarget the program supplies: the raw physical drive on Windows (locked and
// dismounted, autobleem-pc-tools' apps/flasher), a memory image in the tests.
//
#pragma once

#include "installer/installer_job.h"

#include <cstdint>
#include <string>
#include <vector>

//******************
// DiskTarget
//******************
// a whole disk, written and read at offsets that are multiples of sectorSize(), in sizes that are too
// (FlasherJob pads the image's last sector with zeros)
class DiskTarget {
public:
    virtual ~DiskTarget() = default;
    virtual uint64_t size() const = 0;
    virtual uint32_t sectorSize() const { return 512; }
    // takes the disk for writing: its volumes locked and dismounted, nothing else may use it
    virtual bool open(std::string &error) = 0;
    virtual bool write(uint64_t offset, const uint8_t *data, size_t size, std::string &error) = 0;
    virtual bool read(uint64_t offset, uint8_t *data, size_t size, std::string &error) = 0;
    // flushed, released, and the system told to read the new partition table
    virtual bool close(std::string &error) = 0;
};

//******************
// FlashOptions
//******************
struct FlashOptions {
    std::string channel = "release"; // release | testing | nightly, when imageFile is empty
    std::string imageFile;           // a local .img.xz instead (checked against <file>.sha256 when that is there)
    std::string repoUrl = "https://autobleem.retromenele.pl";
    std::string scratchDir; // where a channel's image is downloaded to (and kept, for the next stick)
    bool verify = true;     // read the stick back and compare
};

//******************
// ChannelImage
//******************
struct ChannelImage {
    std::string channel;
    std::string version;
    ableem::UpdateFile image; // autobleem-<version>-pcusb-i386.img.xz
};

//******************
// FlasherJob
//******************
class FlasherJob {
public:
    // the bytes decoded, written and read back at a time (a multiple of every sector size)
    static const size_t ChunkSize = 4u << 20;

    // the catalogs a channel reads on the site, in order (its own, then what stands in when it has none)
    static std::vector<std::string> channelLists(const std::string &channel);
    // what the channel offers now - for the window before the run, and the run itself
    static bool channelImage(const std::string &repoUrl, const std::string &channel, Downloader &downloader,
                             const std::string &scratchDir, ChannelImage &out, std::string &error);
    // the phases a run with these options has
    static std::vector<std::string> phasesFor(const FlashOptions &options);
    // the whole run; false with `error` on the first failure. A stop or a failure after the first write
    // leaves the stick unusable until it is written again (the error says so).
    static bool run(const FlashOptions &options, Downloader &downloader, DiskTarget &disk, InstallListener &listener,
                    const InstallerJob::ShouldStop &shouldStop, std::string &error);
};
