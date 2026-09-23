// FlasherJob: the PC stick image onto a disk in memory - from a local file and from a channel of a fake site
#include <doctest/doctest.h>

#include "installer/flasher_job.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/sha256.h>

#include "support/temp_dir.h"

#include <map>
#include <string>
#include <vector>

using namespace std;
using ableem::DirEntry;
using ableem::Sha256;

namespace {

const char *const Site = "http://site";
const string dataDir = AB_TEST_DATA_DIR;

// what tests/data/test_image.xz unpacks to (tests/data/make_xz_fixtures.py)
vector<uint8_t> imagePayload() {
    vector<uint8_t> out;
    for (int k = 0; k < 144; ++k)
        out.insert(out.end(), 65536, static_cast<uint8_t>(k & 0xff));
    for (int i = 0; i < 300; ++i)
        for (char c : string("tail"))
            out.push_back(static_cast<uint8_t>(c));
    return out;
}

//******************
// MemoryDisk
//******************
class MemoryDisk : public DiskTarget {
public:
    explicit MemoryDisk(uint64_t size) : data(size, 0xee) {}
    vector<uint8_t> data;
    vector<uint64_t> writes; // the offsets, in order
    bool opened = false, closed = false;
    bool corruptReads = false;
    uint64_t size() const override { return data.size(); }
    bool open(string &) override {
        opened = true;
        return true;
    }
    bool write(uint64_t offset, const uint8_t *bytes, size_t n, string &error) override {
        if (offset % 512 || n % 512 || offset + n > data.size()) {
            error = "unaligned or out of range";
            return false;
        }
        copy(bytes, bytes + n, data.begin() + static_cast<ptrdiff_t>(offset));
        writes.push_back(offset);
        return true;
    }
    bool read(uint64_t offset, uint8_t *bytes, size_t n, string &) override {
        copy(data.begin() + static_cast<ptrdiff_t>(offset), data.begin() + static_cast<ptrdiff_t>(offset + n), bytes);
        if (corruptReads && n > 0)
            bytes[n / 2] ^= 1;
        return true;
    }
    bool close(string &) override {
        closed = true;
        return true;
    }
};

//******************
// FakeSite
//******************
class FakeSite : public Downloader {
public:
    map<string, string> files; // url -> local file
    bool fetch(const string &url, const string &destFile, const Progress &progress, string &error) override {
        auto it = files.find(url);
        if (it == files.end()) {
            error = url + ": HTTP 404";
            return false;
        }
        if (!DirEntry::copyFile(it->second, destFile)) {
            error = "cannot copy " + it->second;
            return false;
        }
        if (progress)
            progress(1, 1);
        return true;
    }
};

class Recorder : public InstallListener {
public:
    vector<string> phases, lines;
    void onPhase(int, int, const string &title) override { phases.push_back(title); }
    void onProgress(uint64_t, uint64_t) override {}
    void onLine(const string &line) override { lines.push_back(line); }
};

string imageEntry(const string &name, const string &sha) {
    return R"({"name": ")" + name + R"(", "size": 1, "sha256": ")" + sha + R"(", "url": ")" + string(Site) + "/img/" +
           name + R"("})";
}

bool diskHoldsImage(const MemoryDisk &disk) {
    vector<uint8_t> payload = imagePayload();
    for (size_t i = 0; i < payload.size(); ++i)
        if (disk.data[i] != payload[i])
            return false;
    // the last sector padded with zeros, the rest of the disk untouched
    const size_t padded = (payload.size() + 511) / 512 * 512;
    for (size_t i = payload.size(); i < padded; ++i)
        if (disk.data[i] != 0)
            return false;
    return disk.data[padded] == 0xee;
}

} // namespace

TEST_CASE("a local image is written, the partition table last, and read back") {
    MemoryDisk disk(16u << 20);
    FakeSite site;
    Recorder rec;
    FlashOptions o;
    o.imageFile = dataDir + "/test_image.xz";
    string error;
    REQUIRE_MESSAGE(FlasherJob::run(o, site, disk, rec, nullptr, error), error);
    CHECK(rec.phases == vector<string>{"Checking the image", "Writing the stick", "Checking the stick"});
    CHECK(diskHoldsImage(disk));
    REQUIRE(disk.writes.size() == 3); // 4 MiB + 4 MiB + the rest; offset 0 kept back
    CHECK(disk.writes.back() == 0);
    CHECK(disk.writes.front() == FlasherJob::ChunkSize);
    CHECK(disk.opened);
    CHECK(disk.closed);
}

TEST_CASE("a local image is checked against the .sha256 next to it") {
    TempDir tmp("flasher_sidecar");
    const string image = tmp.path() + "/stick.img.xz";
    REQUIRE(DirEntry::copyFile(dataDir + "/test_image.xz", image));
    MemoryDisk disk(16u << 20);
    FakeSite site;
    Recorder rec;
    FlashOptions o;
    o.imageFile = image;
    string error;

    tmp.writeFile("stick.img.xz.sha256", Sha256::ofFile(image) + "  stick.img.xz\n");
    CHECK(FlasherJob::run(o, site, disk, rec, nullptr, error));

    MemoryDisk untouched(16u << 20);
    tmp.writeFile("stick.img.xz.sha256", string(64, '0') + "  stick.img.xz\n");
    CHECK_FALSE(FlasherJob::run(o, site, untouched, rec, nullptr, error));
    CHECK(error.find("does not match") != string::npos);
    CHECK_FALSE(untouched.opened);
}

TEST_CASE("a stick smaller than the image is refused before anything is written") {
    MemoryDisk disk(4u << 20);
    FakeSite site;
    Recorder rec;
    FlashOptions o;
    o.imageFile = dataDir + "/test_image.xz";
    string error;
    CHECK_FALSE(FlasherJob::run(o, site, disk, rec, nullptr, error));
    CHECK(error.find("bigger stick") != string::npos);
    CHECK_FALSE(disk.opened);
}

TEST_CASE("a read-back that differs fails and says the stick is not usable") {
    MemoryDisk disk(16u << 20);
    disk.corruptReads = true;
    FakeSite site;
    Recorder rec;
    FlashOptions o;
    o.imageFile = dataDir + "/test_image.xz";
    string error;
    CHECK_FALSE(FlasherJob::run(o, site, disk, rec, nullptr, error));
    CHECK(error.find("differs") != string::npos);
    CHECK(error.find("not usable") != string::npos);
    CHECK(disk.closed);
}

TEST_CASE("a stop in the middle of the write leaves a stick that says so") {
    MemoryDisk disk(16u << 20);
    FakeSite site;
    Recorder rec;
    FlashOptions o;
    o.imageFile = dataDir + "/test_image.xz";
    string error;
    CHECK_FALSE(FlasherJob::run(o, site, disk, rec, [&disk]() { return !disk.writes.empty(); }, error));
    CHECK(error.find("Stopped") == 0);
    CHECK(error.find("not usable") != string::npos);
    CHECK(disk.closed);
}

TEST_CASE("each channel finds its image, standing in for one another as the installers' do") {
    TempDir tmp("flasher_channels");
    FakeSite site;
    const string imageSha = Sha256::ofFile(dataDir + "/test_image.xz");
    // release has one; testing has none of its own (no testing.json); the nightly made no PC image
    tmp.writeFile("release.json", R"({"version": "v2.0.0", "i386": )" +
                                      imageEntry("autobleem-v2.0.0-pcusb-i386.img.xz", imageSha) + "}");
    tmp.writeFile("nightly.json", R"({"version": "v2.0.0-3-gabc1234", "files": {}, "images": {"armhf": )" +
                                      imageEntry("a.img.xz", "aa") + "}}");
    site.files[string(Site) + "/pc/images/release.json"] = tmp.path() + "/release.json";
    site.files[string(Site) + "/nightly/latest.json"] = tmp.path() + "/nightly.json";
    site.files[string(Site) + "/img/autobleem-v2.0.0-pcusb-i386.img.xz"] = dataDir + "/test_image.xz";

    ChannelImage ci;
    string error;
    REQUIRE(FlasherJob::channelImage(Site, "nightly", site, tmp.path() + "/scratch", ci, error));
    CHECK(ci.version == "v2.0.0");
    CHECK(ci.image.name == "autobleem-v2.0.0-pcusb-i386.img.xz");

    // a nightly that did make one is its own
    tmp.writeFile("nightly.json", R"({"version": "v2.0.0-3-gabc1234", "images": {"pc-i386": )" +
                                      imageEntry("autobleem-v2.0.0-3-gabc1234-pcusb-i386.img.xz", "bb") + "}}");
    REQUIRE(FlasherJob::channelImage(Site, "nightly", site, tmp.path() + "/scratch", ci, error));
    CHECK(ci.version == "v2.0.0-3-gabc1234");

    // and the whole run from the release channel: downloaded, checked, written, kept for the next stick
    MemoryDisk disk(16u << 20);
    Recorder rec;
    FlashOptions o;
    o.channel = "release";
    o.repoUrl = Site;
    o.scratchDir = tmp.path() + "/scratch";
    tmp.writeFile("scratch/autobleem-v1.0.0-pcusb-i386.img.xz", "an older image");
    REQUIRE_MESSAGE(FlasherJob::run(o, site, disk, rec, nullptr, error), error);
    CHECK(rec.phases.front() == "Getting the image");
    CHECK(diskHoldsImage(disk));
    CHECK(DirEntry::exists(o.scratchDir + "/autobleem-v2.0.0-pcusb-i386.img.xz"));
    CHECK_FALSE(DirEntry::exists(o.scratchDir + "/autobleem-v1.0.0-pcusb-i386.img.xz"));

    // no channel list names an image: said so
    FakeSite empty;
    CHECK_FALSE(FlasherJob::channelImage(Site, "testing", empty, tmp.path() + "/scratch", ci, error));
    CHECK(error.find("no PC stick image") != string::npos);
}
