//
// DiscReader over a fake drive: the fake game's real MODE2 image as the data track, a 2 s pregap and an audio
// track after it, a Q channel made clean by DiscReader::expectedQ - then marked the way LibCrypt marks it, or
// broken the way a poor drive returns it. The .bin, the .cue, the .sbi, the names, a two-disc game in one
// folder (and a library serving it as one game), a stop, an audio CD, an unreadable sector.
//
#include "doctest/doctest.h"
#include "support/fake_game.h"
#include "support/temp_dir.h"

#include "core/services/disc_reader.h"

#include <ableem/engine/crc32.h>
#include <ableem/lanserver/lan_library.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace std;

namespace {

const uint32_t S = DiscReader::SectorSize;

struct FakeDrive : CdDrive {
    string data; // the data track's sectors
    uint32_t dataSectors = 0;
    uint32_t pregap = 150; // the audio track's, as the disc has it (the TOC below does not say)
    uint32_t audioSectors = 30;
    bool subchannel = true;
    set<uint32_t> libcrypt; // sectors whose Q has another address and a bad CRC
    set<uint32_t> unreadable;
    bool noisyQ = false; // every Q with a bad CRC: a drive that does not really read the subchannel
    bool audioCd = false;

    explicit FakeDrive(const string &bin) {
        ifstream in(bin, ios::binary);
        data.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
        dataSectors = static_cast<uint32_t>(data.size() / S);
    }
    CdToc truth() const {
        CdToc toc;
        toc.tracks.push_back({1, audioCd, 0, 0});
        toc.tracks.push_back({2, true, dataSectors + pregap, pregap});
        toc.leadout = dataSectors + pregap + audioSectors;
        return toc;
    }
    bool readToc(CdToc &toc, string &) override {
        toc = truth();
        toc.tracks[1].pregap = 0; // as a drive's TOC reports it: INDEX 01 only
        return true;
    }
    bool readSector(uint32_t lba, bool audio, uint8_t *out, uint8_t *q) override {
        if (unreadable.count(lba))
            return false;
        const bool isData = lba < dataSectors;
        if (isData == audio)
            return false; // read as the wrong kind of sector
        if (isData)
            memcpy(out, data.data() + static_cast<size_t>(lba) * S, S);
        else
            for (uint32_t i = 0; i < S; i++)
                out[i] = static_cast<uint8_t>((lba * 7 + i) & 0xff); // "music"
        if (q != nullptr) {
            DiscReader::expectedQ(truth(), lba, q);
            if (libcrypt.count(lba)) {
                q[9] ^= 0x01; // another frame in the absolute address, and the CRC left as it was
            }
            if (noisyQ)
                q[11] ^= 0xff;
        }
        return true;
    }
    bool hasSubchannel() const override { return subchannel; }
};

struct Setup {
    Setup() : tmp("discreader") {
        tmp.makeSubDir("source");
        test_support::makeFakeGame(tmp.at("source"), "Fake");
        tmp.makeSubDir("Library");
    }
    string bin() const { return tmp.at("source/Fake/Fake.bin"); }
    DiscReader::Options options() const {
        DiscReader::Options o;
        o.libraryDir = tmp.at("Library");
        return o;
    }
    TempDir tmp;
};

} // namespace

TEST_CASE("DiscReader: the Q channel's parts") {
    // a clean Q verifies with its own CRC
    CdToc toc;
    toc.tracks.push_back({1, false, 0, 0});
    toc.tracks.push_back({2, true, 1000, 150});
    toc.leadout = 2000;
    uint8_t q[12];
    DiscReader::expectedQ(toc, 16, q);
    CHECK(q[0] == 0x41);
    CHECK(q[1] == 0x01);
    CHECK(q[2] == 0x01);
    CHECK(q[5] == 0x16); // 16 frames into the track, in BCD
    CHECK(q[9] == 0x16); // 150 + 16 = 00:02:16
    CHECK(q[8] == 0x02);
    CHECK(DiscReader::qCrc(q) == (q[10] << 8 | q[11]));
    DiscReader::expectedQ(toc, 900, q); // in track 2's pregap: index 0, counting down
    CHECK(q[1] == 0x02);
    CHECK(q[2] == 0x00);
    CHECK(q[4] == 0x01); // 100 frames = 00:01:25 before the track
    CHECK(q[5] == 0x25);

    // the 96 interleaved subcode bytes carry Q in bit 6
    uint8_t sub[96] = {};
    for (int i = 0; i < 12; i++)
        for (int b = 0; b < 8; b++)
            if (q[i] & (0x80 >> b))
                sub[i * 8 + b] |= 0x40;
    uint8_t back[12];
    DiscReader::deinterleaveQ(sub, back);
    CHECK(memcmp(back, q, 12) == 0);

    CHECK(DiscReader::msf(174) == "00:02:24");
    CHECK(DiscReader::msf(75 * 60 * 3 + 75 * 7 + 4) == "03:07:04");
}

TEST_CASE("DiscReader reads a disc: the whole .bin, a .cue with the pregap the Q channel gave, the LibCrypt .sbi") {
    Setup s;
    FakeDrive drive(s.bin());
    REQUIRE(drive.dataSectors > 16);
    drive.libcrypt = {5, 9};
    uint32_t lastDone = 0, lastTotal = 0;
    const DiscReader::Result r = DiscReader::read(drive, s.options(), [&](uint32_t done, uint32_t total) {
        lastDone = done;
        lastTotal = total;
        return true;
    });
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(lastDone == lastTotal);
    CHECK(r.serial == "SLUS-01234");
    CHECK(r.title == "SLUS-01234"); // no covers database here: the serial names it
    CHECK(r.folder == s.tmp.at("Library/SLUS-01234"));
    CHECK(r.sectors == drive.dataSectors + 150 + 30);
    CHECK(r.badSectors.empty());
    CHECK(r.subchannel);
    CHECK_FALSE(r.subchannelUnreliable);
    CHECK(r.verified == DiscReader::Verified::Unknown);
    CHECK_FALSE(ableem::DirEntry::exists(s.tmp.at("Library/.reading-disc")));

    // the image: every sector, the data track exactly the source
    const string bin = s.tmp.readFile("Library/SLUS-01234/SLUS-01234.bin");
    CHECK(bin.size() == static_cast<size_t>(r.sectors) * S);
    CHECK(bin.compare(0, drive.data.size(), drive.data) == 0);
    uint32_t crc = 0;
    ableem::Crc32::ofFile(s.bin(), crc);
    CHECK(r.dataCrc == crc);

    // the cue: MODE2 (the first sector says), the audio track's pregap found in the Q channel
    const uint32_t start2 = drive.dataSectors + 150;
    CHECK(s.tmp.readFile("Library/SLUS-01234/SLUS-01234.cue") == "FILE \"SLUS-01234.bin\" BINARY\n"
                                                                 "  TRACK 01 MODE2/2352\n"
                                                                 "    INDEX 01 00:00:00\n"
                                                                 "  TRACK 02 AUDIO\n"
                                                                 "    INDEX 00 " +
                                                                     DiscReader::msf(drive.dataSectors) +
                                                                     "\n"
                                                                     "    INDEX 01 " +
                                                                     DiscReader::msf(start2) + "\n");

    // the .sbi: its header, then the two marked sectors - their address, a type byte, the Q as read
    CHECK(r.sbiSectors == 2);
    const string sbi = s.tmp.readFile("Library/SLUS-01234/SLUS-01234.sbi");
    REQUIRE(sbi.size() == 4 + 2 * 14);
    CHECK(sbi.compare(0, 4, string("SBI\0", 4)) == 0);
    CHECK(static_cast<uint8_t>(sbi[4]) == 0x00); // sector 5 = 00:02:05
    CHECK(static_cast<uint8_t>(sbi[5]) == 0x02);
    CHECK(static_cast<uint8_t>(sbi[6]) == 0x05);
    CHECK(static_cast<uint8_t>(sbi[7]) == 0x01);
    CHECK(static_cast<uint8_t>(sbi[18 + 2]) == 0x09); // sector 9's address
}

TEST_CASE("DiscReader puts a game's discs in one folder, which a library serves as one game") {
    Setup s;
    FakeDrive drive(s.bin());
    DiscReader::Options o = s.options();
    o.discNumber = 1;
    const DiscReader::Result one = DiscReader::read(drive, o, nullptr);
    REQUIRE_MESSAGE(one.ok, one.error);
    CHECK(ableem::DirEntry::exists(one.folder + "/SLUS-01234 (Disc 1).cue"));

    o.discNumber = 2;
    o.gameFolder = one.folder;
    o.title = one.title;
    const DiscReader::Result two = DiscReader::read(drive, o, nullptr);
    REQUIRE_MESSAGE(two.ok, two.error);
    CHECK(two.folder == one.folder);
    CHECK(ableem::DirEntry::exists(one.folder + "/SLUS-01234 (Disc 2).bin"));

    // the same disc again is refused, and leaves nothing behind
    const DiscReader::Result again = DiscReader::read(drive, o, nullptr);
    CHECK_FALSE(again.ok);
    CHECK(again.error.find("already") != string::npos);
    CHECK_FALSE(ableem::DirEntry::exists(s.tmp.at("Library/.reading-disc")));

    ableem::LanLibrary::Config c;
    c.gamesDir = s.tmp.at("Library");
    ableem::LanLibrary library(c);
    library.scan();
    const auto snap = library.snapshot();
    REQUIRE(snap->games.size() == 1);
    int discs = 0;
    for (const ableem::LanFile &f : snap->games.front().files)
        discs += f.disc > 0 ? 1 : 0;
    CHECK(discs == 2);
}

TEST_CASE("DiscReader: a second game of the same title gets a folder of its own") {
    Setup s;
    FakeDrive drive(s.bin());
    const DiscReader::Result a = DiscReader::read(drive, s.options(), nullptr);
    const DiscReader::Result b = DiscReader::read(drive, s.options(), nullptr);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(b.folder == s.tmp.at("Library/SLUS-01234 (2)"));
}

TEST_CASE("DiscReader: a drive whose Q channel is noise gives no .sbi, and says so") {
    Setup s;
    FakeDrive drive(s.bin());
    drive.dataSectors = drive.dataSectors; // the data track is small: pad it past the threshold
    drive.data += string(static_cast<size_t>(80) * S, '\0');
    drive.dataSectors += 80;
    drive.noisyQ = true;
    const DiscReader::Result r = DiscReader::read(drive, s.options(), nullptr);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.subchannelUnreliable);
    CHECK(r.sbiSectors == 0);
    CHECK_FALSE(ableem::DirEntry::exists(r.folder + "/SLUS-01234.sbi"));
}

TEST_CASE("DiscReader: without a subchannel, the usual 2 s pregap; an unreadable sector is zeros and said") {
    Setup s;
    FakeDrive drive(s.bin());
    drive.subchannel = false;
    drive.unreadable = {drive.dataSectors + 160};
    const DiscReader::Result r = DiscReader::read(drive, s.options(), nullptr);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(r.subchannel);
    CHECK(r.badSectors == vector<uint32_t>{drive.dataSectors + 160});
    const string cue = s.tmp.readFile("Library/SLUS-01234/SLUS-01234.cue");
    CHECK(cue.find("INDEX 00 " + DiscReader::msf(drive.dataSectors)) != string::npos);
    const string bin = s.tmp.readFile("Library/SLUS-01234/SLUS-01234.bin");
    CHECK(bin.substr(static_cast<size_t>(drive.dataSectors + 160) * S, S) == string(S, '\0'));
}

TEST_CASE("DiscReader: a stop leaves nothing; an audio CD is refused") {
    Setup s;
    FakeDrive drive(s.bin());
    const DiscReader::Result stopped = DiscReader::read(drive, s.options(), [](uint32_t, uint32_t) { return false; });
    CHECK_FALSE(stopped.ok);
    CHECK(stopped.error == "stopped");
    CHECK(ableem::DirEntry::diru(s.tmp.at("Library")).empty());

    drive.audioCd = true;
    const DiscReader::Result audio = DiscReader::read(drive, s.options(), nullptr);
    CHECK_FALSE(audio.ok);
    CHECK(audio.error.find("audio CD") != string::npos);
}
