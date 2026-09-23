//
// FlasherJob - see the header.
//
#include "installer/flasher_job.h"
#include "installer/install_job_base.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/sha256.h>
#include <ableem/engine/update_catalog.h>
#include <ableem/engine/xz_file.h>

#include <cstring>
#include <fstream>

using namespace std;
using ableem::DirEntry;
using ableem::ReleaseCatalog;
using ableem::Sha256;
using ableem::UpdateFile;
using ableem::XzFile;

namespace {

const char *const Unusable = " - the stick is not usable until it is written again";

uint64_t roundUp(uint64_t value, uint64_t unit) {
    return unit == 0 ? value : (value + unit - 1) / unit * unit;
}

//******************
// Run
//******************
class Run : public InstallJobBase {
public:
    Run(const FlashOptions &options, Downloader &downloader, DiskTarget &target, InstallListener &listener,
        const InstallerJob::ShouldStop &shouldStop)
        : InstallJobBase(downloader, listener, shouldStop, options.repoUrl, options.scratchDir), opt(options),
          disk(target) {
        phases = FlasherJob::phasesFor(options);
    }

    bool go(string &error) {
        string image;
        if (!getImage(image, error))
            return false;
        uint64_t written = 0;
        string hash;
        if (!writeImage(image, written, hash, error))
            return fail(error);
        if (opt.verify && !verify(written, hash, error))
            return fail(error);
        if (!disk.close(error)) {
            error = "Could not release the stick: " + error;
            return false;
        }
        opened = false;
        say("The stick is ready: boot the PC from it (Secure Boot off). Its first boot sets AutoBleem up.");
        return true;
    }

private:
    //******************
    // 1. the image
    //******************
    bool getImage(string &image, string &error) {
        if (!opt.imageFile.empty())
            return checkLocal(image, error);
        phase("Getting the image");
        if (stopped(error))
            return false;
        ChannelImage ci;
        if (!FlasherJob::channelImage(repoUrl, opt.channel, dl, scratch, ci, error))
            return false;
        say("  the " + opt.channel + " channel: AutoBleem " + ci.version);
        image = scratch + "/" + ci.image.name;
        if (!downloadVerified(ci.image, image, error))
            return false;
        // the image is kept for the next stick; an older one is not
        for (const string &name : DirEntry::listNames(scratch))
            if (name != ci.image.name && name.size() > 7 && name.compare(name.size() - 7, 7, ".img.xz") == 0)
                DirEntry::removeFile(scratch + "/" + name);
        return true;
    }

    bool checkLocal(string &image, string &error) {
        phase("Checking the image");
        image = opt.imageFile;
        if (!DirEntry::exists(image) || DirEntry::isDirectory(image)) {
            error = "No such file: " + image;
            return false;
        }
        say("  " + DirEntry::getFileNameFromPath(image) + " (" +
            humanSize(static_cast<uint64_t>(DirEntry::fileSize(image))) + ")");
        const string sidecar = image + ".sha256";
        if (!DirEntry::exists(sidecar)) {
            say("  (no " + DirEntry::getFileNameFromPath(sidecar) +
                " next to it - the image is checked as it is "
                "decoded instead)");
            return true;
        }
        const string expected = sidecarHash(readText(sidecar));
        string actual;
        if (!hashFile(image, actual, error))
            return false;
        if (actual != expected) {
            error = DirEntry::getFileNameFromPath(image) + " does not match its .sha256 - download it again";
            return false;
        }
        say("  matches its .sha256");
        return true;
    }

    bool hashFile(const string &path, string &hash, string &error) {
        ifstream in(path, ios::binary);
        if (!in) {
            error = "cannot read " + path;
            return false;
        }
        const uint64_t total = static_cast<uint64_t>(DirEntry::fileSize(path));
        uint64_t done = 0;
        Sha256 sha;
        vector<char> buf(1 << 20);
        while (in) {
            in.read(buf.data(), static_cast<streamsize>(buf.size()));
            size_t n = static_cast<size_t>(in.gcount());
            if (n == 0)
                break;
            sha.update(reinterpret_cast<const unsigned char *>(buf.data()), n);
            done += n;
            out.onProgress(done, total);
            if (stopped(error))
                return false;
        }
        hash = sha.hexDigest();
        return true;
    }

    //******************
    // 2. the write
    //******************
    // decoded a chunk at a time and written straight to the disk; the first chunk - the partition table -
    // is kept back and written last, so the system sees no new partition before the rest is there
    bool writeImage(const string &image, uint64_t &written, string &hash, string &error) {
        phase("Writing the stick");
        uint64_t unpacked = 0;
        if (!XzFile::unpackedSize(image, unpacked)) {
            error = DirEntry::getFileNameFromPath(image) + " is not a complete .xz image - download it again";
            return false;
        }
        const uint64_t sector = disk.sectorSize();
        const uint64_t padded = roundUp(unpacked, sector);
        if (padded > disk.size()) {
            error = "The image needs " + humanSize(padded) + " and the stick has " + humanSize(disk.size()) +
                    " - use a bigger stick";
            return false;
        }
        say("  " + humanSize(unpacked) + " onto a " + humanSize(disk.size()) + " stick");
        if (stopped(error))
            return false;
        if (!disk.open(error)) {
            error = "Could not take the stick for writing: " + error;
            return false;
        }
        opened = true;

        vector<uint8_t> chunk(FlasherJob::ChunkSize);
        vector<uint8_t> first;
        size_t fill = 0;
        uint64_t offset = 0;
        Sha256 sha;
        string writeError;
        auto flush = [&](bool last) {
            size_t n = fill;
            if (last) {
                n = static_cast<size_t>(roundUp(fill, sector));
                memset(chunk.data() + fill, 0, n - fill);
            }
            sha.update(chunk.data(), n);
            if (offset == 0) {
                first.assign(chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(n));
            } else {
                touched = true;
                if (!disk.write(offset, chunk.data(), n, writeError))
                    return false;
            }
            offset += n;
            fill = 0;
            out.onProgress(offset, padded);
            return !stopped(writeError);
        };
        string decodeError;
        bool ok = XzFile::decode(
            image,
            [&](const uint8_t *data, size_t size) {
                while (size > 0) {
                    size_t take = min(size, chunk.size() - fill);
                    memcpy(chunk.data() + fill, data, take);
                    fill += take;
                    data += take;
                    size -= take;
                    if (fill == chunk.size() && !flush(false))
                        return false;
                }
                return true;
            },
            decodeError);
        if (ok && fill > 0)
            ok = flush(true);
        if (!ok) {
            error = !writeError.empty() ? writeError : "the image cannot be read: " + decodeError;
            return false;
        }
        touched = true;
        if (!first.empty() && !disk.write(0, first.data(), first.size(), error))
            return false;
        written = offset;
        hash = sha.hexDigest();
        say("  " + humanSize(written) + " written");
        return true;
    }

    //******************
    // 3. the read-back
    //******************
    bool verify(uint64_t size, const string &expected, string &error) {
        phase("Checking the stick");
        vector<uint8_t> chunk(FlasherJob::ChunkSize);
        Sha256 sha;
        for (uint64_t offset = 0; offset < size;) {
            size_t n = static_cast<size_t>(min<uint64_t>(chunk.size(), size - offset));
            if (!disk.read(offset, chunk.data(), n, error)) {
                error = "Reading the stick back failed: " + error;
                return false;
            }
            sha.update(chunk.data(), n);
            offset += n;
            out.onProgress(offset, size);
            if (stopped(error))
                return false;
        }
        if (sha.hexDigest() != expected) {
            error = "What was read back differs from what was written - the stick may be failing, or smaller "
                    "than it claims";
            return false;
        }
        say("  read back and compared: identical");
        return true;
    }

    // the disk is given back; after the first write a failure leaves a broken stick, and the error says so
    bool fail(string &error) {
        if (opened) {
            string closeError;
            disk.close(closeError);
            opened = false;
        }
        if (touched)
            error += Unusable;
        return false;
    }

    FlashOptions opt;
    DiskTarget &disk;
    bool opened = false;
    bool touched = false; // something reached the disk
};

} // namespace

const size_t FlasherJob::ChunkSize; // C++14: the in-class value needs a definition once it is bound to a reference

//*******************************
// FlasherJob::channelLists
//*******************************
vector<string> FlasherJob::channelLists(const string &channel) {
    if (channel == "nightly")
        return {"nightly/latest.json", "pc/images/testing.json", "pc/images/release.json"};
    if (channel == "testing")
        return {"pc/images/testing.json", "pc/images/release.json"};
    return {"pc/images/release.json"};
}

//*******************************
// FlasherJob::channelImage
//*******************************
bool FlasherJob::channelImage(const string &repoUrl, const string &channel, Downloader &downloader,
                              const string &scratchDir, ChannelImage &out, string &error) {
    out = ChannelImage();
    out.channel = channel;
    string lastError;
    for (const string &list : channelLists(channel)) {
        string text;
        ReleaseCatalog catalog;
        if (!downloader.fetchText(repoUrl + "/" + list, scratchDir + "/channel.json", text, lastError) ||
            !catalog.parse(text))
            continue;
        // a nightly names it pc-i386 among the Pi images, pc/images names it by its architecture alone
        const UpdateFile *image = catalog.imageFor("pc-i386");
        if (!image)
            image = catalog.imageFor("i386");
        if (!image)
            continue; // this build made no stick image - the next list stands in
        out.version = catalog.version;
        out.image = *image;
        return true;
    }
    error =
        "The " + channel + " channel has no PC stick image" + (lastError.empty() ? string() : " (" + lastError + ")");
    return false;
}

//*******************************
// FlasherJob::phasesFor / run
//*******************************
vector<string> FlasherJob::phasesFor(const FlashOptions &options) {
    vector<string> phases = {options.imageFile.empty() ? "Getting the image" : "Checking the image",
                             "Writing the stick"};
    if (options.verify)
        phases.push_back("Checking the stick");
    return phases;
}

bool FlasherJob::run(const FlashOptions &options, Downloader &downloader, DiskTarget &disk, InstallListener &listener,
                     const InstallerJob::ShouldStop &shouldStop, string &error) {
    FlashOptions o = options;
    if (o.imageFile.empty() && o.scratchDir.empty()) {
        error = "No folder to download the image to";
        return false;
    }
    if (!o.scratchDir.empty())
        DirEntry::createDirs(o.scratchDir);
    Run run(o, downloader, disk, listener, shouldStop);
    return run.go(error);
}
