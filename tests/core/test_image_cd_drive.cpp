//
// ImageCdDrive: a disc image as a CdDrive. The round trip (DiscReader writes a FakeDrive's disc as bin/cue/sbi,
// ImageCdDrive reads it back sector- and Q-identical), a bin per track, a PREGAP, an .sbi named by hand, and the
// cue sheets, bins and .sbi files that are not right.
//
#include "doctest/doctest.h"
#include "support/fake_game.h"
#include "support/temp_dir.h"

#include "core/services/image_cd_drive.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace std;

namespace {

const uint32_t S = DiscReader::SectorSize;

// the disc of test_disc_reader's FakeDrive: the game's data track, a 2 s pregap and an audio track
struct FakeDrive : CdDrive {
    string data;
    uint32_t dataSectors = 0;
    uint32_t pregap = 150;
    uint32_t audioSectors = 30;
    set<uint32_t> libcrypt;

    explicit FakeDrive(const string &bin) {
        ifstream in(bin, ios::binary);
        data.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
        dataSectors = static_cast<uint32_t>(data.size() / S);
    }
    CdToc truth() const {
        CdToc toc;
        toc.tracks.push_back({1, false, 0, 0});
        toc.tracks.push_back({2, true, dataSectors + pregap, pregap});
        toc.leadout = dataSectors + pregap + audioSectors;
        return toc;
    }
    bool readToc(CdToc &toc, string &) override {
        toc = truth();
        toc.tracks[1].pregap = 0;
        return true;
    }
    bool readSector(uint32_t lba, bool audio, uint8_t *out, uint8_t *q) override {
        const bool isData = lba < dataSectors;
        if (isData == audio)
            return false;
        if (isData)
            memcpy(out, data.data() + static_cast<size_t>(lba) * S, S);
        else
            for (uint32_t i = 0; i < S; i++)
                out[i] = static_cast<uint8_t>((lba * 7 + i) & 0xff);
        if (q != nullptr) {
            DiscReader::expectedQ(truth(), lba, q);
            if (libcrypt.count(lba))
                q[9] ^= 0x01;
        }
        return true;
    }
    bool hasSubchannel() const override { return true; }
};

// sectors whose every byte is `mark` + the sector number in the file
string sectors(uint32_t count, uint8_t mark) {
    string out;
    for (uint32_t n = 0; n < count; n++)
        out.append(S, static_cast<char>(mark + n));
    return out;
}

bool sameSector(ImageCdDrive &drive, uint32_t lba, bool audio, uint8_t expected) {
    vector<uint8_t> data(S);
    if (!drive.readSector(lba, audio, data.data(), nullptr))
        return false;
    for (uint8_t b : data)
        if (b != expected)
            return false;
    return true;
}

} // namespace

TEST_CASE("ImageCdDrive reads back what DiscReader wrote: every sector and its Q, the LibCrypt ones too") {
    TempDir tmp("imagecd");
    tmp.makeSubDir("source");
    test_support::makeFakeGame(tmp.at("source"), "Fake");
    tmp.makeSubDir("Library");
    FakeDrive fake(tmp.at("source/Fake/Fake.bin"));
    REQUIRE(fake.dataSectors > 16);
    fake.libcrypt = {5, 9};
    DiscReader::Options o;
    o.libraryDir = tmp.at("Library");
    const DiscReader::Result r = DiscReader::read(fake, o, nullptr);
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(r.sbiSectors == 2);

    ImageCdDrive image;
    string error;
    REQUIRE_MESSAGE(image.open(r.cueFile, error), error);
    CHECK(image.hasSubchannel());
    CHECK(image.sbiEntries() == 2);

    CdToc toc, truth = fake.truth();
    REQUIRE(image.readToc(toc, error));
    REQUIRE(toc.tracks.size() == 2);
    CHECK(toc.leadout == truth.leadout);
    for (size_t i = 0; i < 2; i++) {
        CHECK(toc.tracks[i].number == truth.tracks[i].number);
        CHECK(toc.tracks[i].audio == truth.tracks[i].audio);
        CHECK(toc.tracks[i].start == truth.tracks[i].start);
    }
    CHECK(image.toc().tracks[1].pregap == 150); // INDEX 00, for the Q channel

    vector<uint8_t> a(S), b(S);
    uint8_t qa[12], qb[12];
    for (uint32_t lba = 0; lba < truth.leadout; lba++) {
        const bool audio = lba >= fake.dataSectors;
        REQUIRE(fake.readSector(lba, audio, a.data(), qa));
        REQUIRE(image.readSector(lba, audio, b.data(), qb));
        CHECK(a == b);
        CHECK(memcmp(qa, qb, 12) == 0);
    }
    // the marked sectors differ from the clean Q, and the image says so too
    uint8_t clean[12];
    DiscReader::expectedQ(truth, 5, clean);
    REQUIRE(image.readSector(5, false, b.data(), qb));
    CHECK(memcmp(clean, qb, 12) != 0);

    CHECK_FALSE(image.readSector(truth.leadout, true, b.data(), qb)); // past the lead-out
    CHECK_FALSE(image.readSector(0, true, b.data(), qb));             // data read as audio
}

TEST_CASE("ImageCdDrive: a bin for each track follows the one before on the disc") {
    TempDir tmp("imagecd");
    tmp.writeFile("d1.bin", sectors(20, 0x10));
    tmp.writeFile("d2.bin", sectors(40, 0x80));
    tmp.writeFile("d.cue", "FILE \"d1.bin\" BINARY\r\n"
                           "  TRACK 01 MODE1/2352\r\n"
                           "    INDEX 01 00:00:00\r\n"
                           "FILE \"d2.bin\" BINARY\r\n"
                           "  TRACK 02 AUDIO\r\n"
                           "    INDEX 00 00:00:00\r\n"
                           "    INDEX 01 00:00:30\r\n");
    ImageCdDrive image;
    string error;
    REQUIRE_MESSAGE(image.open(tmp.at("d.cue"), error), error);
    const CdToc &toc = image.toc();
    REQUIRE(toc.tracks.size() == 2);
    CHECK(toc.leadout == 60);
    CHECK_FALSE(toc.tracks[0].audio);
    CHECK(toc.tracks[1].audio);
    CHECK(toc.tracks[1].start == 50);
    CHECK(toc.tracks[1].pregap == 30);
    CHECK(sameSector(image, 0, false, 0x10));
    CHECK(sameSector(image, 19, false, 0x10 + 19));
    CHECK(sameSector(image, 20, true, 0x80)); // the second bin's first sector, in track 2's pregap
    CHECK(sameSector(image, 59, true, 0x80 + 39));

    uint8_t q[12], clean[12];
    vector<uint8_t> data(S);
    REQUIRE(image.readSector(25, true, data.data(), q));
    DiscReader::expectedQ(toc, 25, clean);
    CHECK(memcmp(q, clean, 12) == 0);
    CHECK(q[2] == 0x00); // index 0: in the pregap
}

TEST_CASE("ImageCdDrive: a PREGAP is silence that no file has") {
    TempDir tmp("imagecd");
    tmp.writeFile("p.bin", sectors(10, 0x40));
    tmp.writeFile("p.cue", "FILE \"p.bin\" BINARY\n"
                           "  TRACK 01 MODE2/2352\n"
                           "    PREGAP 00:00:05\n"
                           "    INDEX 01 00:00:00\n");
    ImageCdDrive image;
    string error;
    REQUIRE_MESSAGE(image.open(tmp.at("p.cue"), error), error);
    CHECK(image.toc().leadout == 15);
    CHECK(image.toc().tracks[0].start == 5);
    CHECK(image.toc().tracks[0].pregap == 5);
    CHECK(sameSector(image, 2, false, 0x00));
    CHECK(sameSector(image, 5, false, 0x40));
    CHECK(sameSector(image, 14, false, 0x40 + 9));
}

TEST_CASE("ImageCdDrive: the .sbi next to the cue, one named by hand, and a Q that is not the clean one") {
    TempDir tmp("imagecd");
    tmp.writeFile("s.bin", sectors(10, 0x40));
    const string cue = "FILE \"s.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
    tmp.writeFile("s.cue", cue);
    vector<uint8_t> q10 = {0x41, 0x01, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x02, 0x44}; // sector 3, another frame
    tmp.writeFile("other.sbi", DiscReader::sbi({{3, q10}}));

    ImageCdDrive plain, byHand;
    string error;
    REQUIRE_MESSAGE(plain.open(tmp.at("s.cue"), error), error);
    CHECK(plain.sbiEntries() == 0);
    REQUIRE_MESSAGE(byHand.open(tmp.at("s.cue"), error, tmp.at("other.sbi")), error);
    CHECK(byHand.sbiEntries() == 1);

    vector<uint8_t> data(S);
    uint8_t q[12], clean[12];
    REQUIRE(byHand.readSector(3, false, data.data(), q));
    DiscReader::expectedQ(byHand.toc(), 3, clean);
    CHECK(memcmp(q, q10.data(), 10) == 0);
    CHECK(memcmp(q + 10, clean + 10, 2) == 0); // the CRC is the clean Q's
    REQUIRE(byHand.readSector(4, false, data.data(), q));
    DiscReader::expectedQ(byHand.toc(), 4, clean);
    CHECK(memcmp(q, clean, 12) == 0);

    // "s.sbi" beside the cue is found without being named
    tmp.writeFile("s.sbi", DiscReader::sbi({{3, q10}, {4, q10}}));
    ImageCdDrive beside;
    REQUIRE_MESSAGE(beside.open(tmp.at("s.cue"), error), error);
    CHECK(beside.sbiEntries() == 2);
}

TEST_CASE("ImageCdDrive says what is wrong with an image that is not right") {
    TempDir tmp("imagecd");
    tmp.writeFile("g.bin", sectors(10, 0x40));
    const string head = "FILE \"g.bin\" BINARY\n  TRACK 01 MODE2/2352\n";
    ImageCdDrive image;
    string error;

    CHECK_FALSE(image.open(tmp.at("nothing.cue"), error));
    CHECK(error.find("cannot be opened") != string::npos);

    tmp.writeFile("nobin.cue", "FILE \"gone.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
    error.clear();
    CHECK_FALSE(image.open(tmp.at("nobin.cue"), error));
    CHECK(error.find("gone.bin") != string::npos);

    tmp.writeFile("junk.cue", head + "    FROBNICATE 01 00:00:00\n    INDEX 01 00:00:00\n");
    error.clear();
    CHECK_FALSE(image.open(tmp.at("junk.cue"), error));
    CHECK(error.find("line 3") != string::npos);

    tmp.writeFile("badmsf.cue", head + "    INDEX 01 00:99:00\n");
    error.clear();
    CHECK_FALSE(image.open(tmp.at("badmsf.cue"), error));
    CHECK(error.find("line 3") != string::npos);

    tmp.writeFile("badmode.cue", "FILE \"g.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
    error.clear();
    CHECK_FALSE(image.open(tmp.at("badmode.cue"), error));
    CHECK(error.find("line 2") != string::npos);

    tmp.writeFile("noindex.cue", head);
    error.clear();
    CHECK_FALSE(image.open(tmp.at("noindex.cue"), error));
    CHECK(error.find("INDEX 01") != string::npos);

    tmp.writeFile("empty.cue", "REM nothing\n");
    CHECK_FALSE(image.open(tmp.at("empty.cue"), error));

    tmp.writeFile("odd.bin", string(S + 5, 'x'));
    tmp.writeFile("odd.cue", "FILE \"odd.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
    error.clear();
    CHECK_FALSE(image.open(tmp.at("odd.cue"), error));
    CHECK(error.find("odd.bin") != string::npos);

    tmp.writeFile("ok.cue", head + "    INDEX 01 00:00:00\n");
    error.clear();
    CHECK_FALSE(image.open(tmp.at("ok.cue"), error, tmp.at("missing.sbi")));
    CHECK(error.find("missing.sbi") != string::npos);
    tmp.writeFile("bad.sbi", "not an sbi file");
    error.clear();
    CHECK_FALSE(image.open(tmp.at("ok.cue"), error, tmp.at("bad.sbi")));
    CHECK(error.find("bad.sbi") != string::npos);

    // a drive that never opened an image has no TOC
    ImageCdDrive closed;
    CdToc toc;
    CHECK_FALSE(closed.readToc(toc, error));
}
