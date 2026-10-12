//
// DiscIdentity over a fake drive: a PS1 Mode 2 disc with a SYSTEM.CNF (spaced and unspaced BOOT lines), one
// with only a PS-looking volume label, an audio CD, a PC Mode 1 disc, a drive whose reads fail. And the pure
// parts of LinuxCdDrive: findDrives over a temp sysfs tree, the TOC bytes -> CdToc, the READ CD command and
// the subchannel probe's decision from given Q channels (the SG_IO calls themselves need a real drive).
//
#include "doctest/doctest.h"
#include "support/temp_dir.h"

#include "core/services/disc_identity.h"
#include "core/services/disc_reader.h"
#ifndef _WIN32
#include "core/services/linux_cd_drive.h"
#endif

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace std;

namespace {

const uint32_t S = DiscReader::SectorSize;
const uint32_t RootLba = 20;
const uint32_t CnfLba = 22;

void put32(vector<uint8_t> &b, size_t at, uint32_t v) { // both-endian, as ISO 9660 stores it
    for (int i = 0; i < 4; i++) {
        b[at + i] = static_cast<uint8_t>(v >> (8 * i));
        b[at + 4 + 3 - i] = static_cast<uint8_t>(v >> (8 * i));
    }
}

size_t record(vector<uint8_t> &b, size_t at, const string &name, uint32_t extent, uint32_t size, bool dir) {
    const size_t len = (33 + name.size() + 1) & ~static_cast<size_t>(1);
    b[at] = static_cast<uint8_t>(len);
    put32(b, at + 2, extent);
    put32(b, at + 10, size);
    b[at + 25] = dir ? 2 : 0;
    b[at + 32] = static_cast<uint8_t>(name.size());
    memcpy(&b[at + 33], name.data(), name.size());
    return at + len;
}

struct FakeDrive : CdDrive {
    map<uint32_t, vector<uint8_t>> sectors; // raw 2352 bytes
    CdToc toc;
    bool tocFails = false;
    bool readsFail = false;

    // the 2048 bytes of a data sector in the raw 2352, as Mode 1 (user data at 16) or Mode 2 Form 1 (at 24)
    void putUser(uint32_t lba, const vector<uint8_t> &user, bool mode2) {
        vector<uint8_t> raw(S, 0);
        raw[0] = 0;
        for (int i = 1; i < 11; i++)
            raw[i] = 0xff;
        raw[15] = mode2 ? 2 : 1;
        if (mode2)
            raw[18] = raw[22] = 0x08; // subheader: form 1
        memcpy(&raw[mode2 ? 24 : 16], user.data(), min<size_t>(2048, user.size()));
        sectors[lba] = raw;
    }

    // an ISO 9660 volume: label, the root directory, and SYSTEM.CNF when cnf is not empty
    void makeDisc(bool mode2, const string &label, const string &cnf) {
        vector<uint8_t> pvd(2048, 0);
        pvd[0] = 1;
        memcpy(&pvd[1], "CD001", 5);
        pvd[6] = 1;
        memset(&pvd[40], ' ', 32);
        memcpy(&pvd[40], label.data(), min<size_t>(32, label.size()));
        record(pvd, 156, string(1, '\0'), RootLba, 2048, true);
        putUser(16, pvd, mode2);
        vector<uint8_t> root(2048, 0);
        size_t at = record(root, 0, string(1, '\0'), RootLba, 2048, true);
        at = record(root, at, string(1, '\x01'), RootLba, 2048, true);
        at = record(root, at, "README.TXT;1", 30, 5, false);
        if (!cnf.empty())
            at = record(root, at, "SYSTEM.CNF;1", CnfLba, static_cast<uint32_t>(cnf.size()), false);
        putUser(RootLba, root, mode2);
        if (!cnf.empty())
            putUser(CnfLba, vector<uint8_t>(cnf.begin(), cnf.end()), mode2);
        toc.tracks.clear();
        toc.tracks.push_back({1, false, 0, 0});
        toc.leadout = 300;
    }

    bool readToc(CdToc &out, string &error) override {
        if (tocFails) {
            error = "no disc";
            return false;
        }
        out = toc;
        return true;
    }
    bool readSector(uint32_t lba, bool audio, uint8_t *data, uint8_t *) override {
        if (readsFail || audio)
            return false;
        auto it = sectors.find(lba);
        if (it == sectors.end())
            memset(data, 0, S);
        else
            memcpy(data, it->second.data(), S);
        return true;
    }
    bool hasSubchannel() const override { return false; }
};

} // namespace

TEST_CASE("a PS1 disc with a SYSTEM.CNF is a game with its serial, region and label") {
    FakeDrive d;
    d.makeDisc(true, "SCES_028.35", "BOOT = cdrom:\\SCES_028.35;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n");
    DiscIdentity id = DiscIdentity::identify(d);
    CHECK(id.kind == DiscIdentity::Kind::Ps1Game);
    CHECK(id.serial == "SCES-02835");
    CHECK(id.region == "Europe-Aus");
    CHECK(id.volumeLabel == "SCES_028.35");
}

TEST_CASE("BOOT without spaces, lower case, and a label that is not a serial") {
    FakeDrive d;
    d.makeDisc(true, "MY GAME", "BOOT=cdrom:\\SLUS_005.94;1\nTCB = 4\n");
    DiscIdentity id = DiscIdentity::identify(d);
    CHECK(id.kind == DiscIdentity::Kind::Ps1Game);
    CHECK(id.serial == "SLUS-00594");
    CHECK(id.region == "US");
    CHECK(id.volumeLabel == "MY GAME");

    FakeDrive lower;
    lower.makeDisc(true, "X", "boot = cdrom:/slpm_860.01;1\n");
    id = DiscIdentity::identify(lower);
    CHECK(id.kind == DiscIdentity::Kind::Ps1Game);
    CHECK(id.serial == "SLPM-86001");
    CHECK(id.region == "Japan");
}

TEST_CASE("BOOT2 is a PS2 disc, not a PS1 game") {
    FakeDrive d;
    d.makeDisc(true, "PS2DISC", "BOOT2 = cdrom0:\\SLUS_200.62;1\nVER = 1.00\n");
    CHECK(DiscIdentity::identify(d).kind == DiscIdentity::Kind::OtherDisc);
}

TEST_CASE("no SYSTEM.CNF: a PS-looking label gives the serial, any other label is another disc") {
    FakeDrive d;
    d.makeDisc(true, "SLUS_005.94", "");
    DiscIdentity id = DiscIdentity::identify(d);
    CHECK(id.kind == DiscIdentity::Kind::Ps1Game);
    CHECK(id.serial == "SLUS-00594");

    FakeDrive other;
    other.makeDisc(true, "HOLIDAY PHOTOS", "");
    id = DiscIdentity::identify(other);
    CHECK(id.kind == DiscIdentity::Kind::OtherDisc);
    CHECK(id.volumeLabel == "HOLIDAY PHOTOS");
}

TEST_CASE("a PC Mode 1 disc without SYSTEM.CNF is another disc") {
    FakeDrive d;
    d.makeDisc(false, "INSTALL", "");
    DiscIdentity id = DiscIdentity::identify(d);
    CHECK(id.kind == DiscIdentity::Kind::OtherDisc);
    CHECK(id.serial.empty());
}

TEST_CASE("a Mode 1 disc with a PS1 SYSTEM.CNF is read at byte 16") {
    FakeDrive d;
    d.makeDisc(false, "X", "BOOT = cdrom:\\SCUS_944.00;1\n");
    DiscIdentity id = DiscIdentity::identify(d);
    CHECK(id.kind == DiscIdentity::Kind::Ps1Game);
    CHECK(id.serial == "SCUS-94400");
}

TEST_CASE("an all-audio TOC is an audio CD with its track lengths") {
    FakeDrive d;
    d.toc.tracks = {{1, true, 0, 0}, {2, true, 15000, 0}, {3, true, 30000, 0}};
    d.toc.leadout = 40500;
    DiscIdentity id = DiscIdentity::identify(d);
    CHECK(id.kind == DiscIdentity::Kind::AudioCd);
    CHECK(id.trackCount == 3);
    CHECK(id.totalFrames == 40500);
    REQUIRE(id.trackLengths.size() == 3);
    CHECK(id.trackLengths[0] == 15000);
    CHECK(id.trackLengths[1] == 15000);
    CHECK(id.trackLengths[2] == 10500);
}

TEST_CASE("a drive that cannot read is unreadable") {
    FakeDrive noToc;
    noToc.tocFails = true;
    CHECK(DiscIdentity::identify(noToc).kind == DiscIdentity::Kind::Unreadable);

    FakeDrive empty; // a TOC without tracks
    CHECK(DiscIdentity::identify(empty).kind == DiscIdentity::Kind::Unreadable);

    FakeDrive d;
    d.makeDisc(true, "SCES_028.35", "BOOT = cdrom:\\SCES_028.35;1\n");
    d.readsFail = true;
    CHECK(DiscIdentity::identify(d).kind == DiscIdentity::Kind::Unreadable);
}

TEST_CASE("BOOT line parsing") {
    CHECK(DiscIdentity::serialFromSystemCnf("BOOT = cdrom:\\SCES_028.35;1") == "SCES-02835");
    CHECK(DiscIdentity::serialFromSystemCnf("TCB=4\r\nBOOT=cdrom:SLUS_005.94;1\r\n") == "SLUS-00594");
    CHECK(DiscIdentity::serialFromSystemCnf("BOOT2 = cdrom0:\\SLUS_200.62;1") == "");
    CHECK(DiscIdentity::serialFromSystemCnf("BOOT = cdrom:\\PSX.EXE;1") == "");
    CHECK(DiscIdentity::serialFromSystemCnf("") == "");
}

#ifndef _WIN32
TEST_CASE("findDrives lists the CD-ROM generic devices of a sysfs tree") {
    TempDir tmp("sysfs");
    const char *names[] = {"sg0", "sg1", "sg2", "sg10", "sg3"};
    const char *types[] = {"0\n", "5\n", "5\n", "5\n", "1\n"};
    for (int i = 0; i < 5; i++) {
        tmp.makeSubDir(string(names[i]) + "/device");
        tmp.writeFile(string(names[i]) + "/device/type", types[i]);
    }
    tmp.makeSubDir("sg7/device"); // no type file
    vector<int> found = LinuxCdDrive::findDrives(tmp.path());
    REQUIRE(found.size() == 3);
    CHECK(found[0] == 1);
    CHECK(found[1] == 2);
    CHECK(found[2] == 10);
    CHECK(LinuxCdDrive::findDrives(tmp.at("nowhere")).empty());
}

TEST_CASE("READ TOC bytes become a CdToc") {
    // LBA form: tracks 1 (data) and 2 (audio), lead-out 0xAA
    const uint8_t lba[] = {0x00, 0x1a, 1,    2,    0, 0x14, 1,    0, 0, 0, 0, 0,
                           0,    0x10, 2,    0,    0, 0,    0x03, 0xe8, 0, 0x10, 0xaa, 0, 0, 0, 0x07, 0xd0};
    CdToc toc;
    string error;
    REQUIRE(LinuxCdDrive::parseToc(lba, sizeof(lba), false, toc, error));
    REQUIRE(toc.tracks.size() == 2);
    CHECK(toc.tracks[0].number == 1);
    CHECK_FALSE(toc.tracks[0].audio);
    CHECK(toc.tracks[0].start == 0);
    CHECK(toc.tracks[1].audio);
    CHECK(toc.tracks[1].start == 1000);
    CHECK(toc.leadout == 2000);

    // MSF form: 00:02:00 is LBA 0, 03:00:00 is LBA 13350
    const uint8_t msf[] = {0, 0x12, 1, 1, 0, 0x10, 1, 0, 0, 0, 2, 0, 0, 0x10, 0xaa, 0, 0, 3, 0, 0};
    REQUIRE(LinuxCdDrive::parseToc(msf, sizeof(msf), true, toc, error));
    REQUIRE(toc.tracks.size() == 1);
    CHECK(toc.tracks[0].audio);
    CHECK(toc.tracks[0].start == 0);
    CHECK(toc.leadout == 13350);

    CHECK_FALSE(LinuxCdDrive::parseToc(lba, 3, false, toc, error));
    const uint8_t noLeadout[] = {0, 10, 1, 1, 0, 0x14, 1, 0, 0, 0, 0, 0};
    CHECK_FALSE(LinuxCdDrive::parseToc(noLeadout, sizeof(noLeadout), false, toc, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("READ CD command bytes") {
    uint8_t cdb[12];
    LinuxCdDrive::readCdCommand(cdb, 0x012345, true, true);
    CHECK(cdb[0] == 0xbe);
    CHECK(cdb[1] == 0x04); // CD-DA
    CHECK(cdb[3] == 0x01);
    CHECK(cdb[4] == 0x23);
    CHECK(cdb[5] == 0x45);
    CHECK(cdb[8] == 1);
    CHECK(cdb[9] == 0xf8);
    CHECK(cdb[10] == 0x01);
    LinuxCdDrive::readCdCommand(cdb, 16, false, false);
    CHECK(cdb[1] == 0x00);
    CHECK(cdb[10] == 0x00);
}

namespace {
// 4 Q channels for sectors first..first+3 that say address (lba + shift)
vector<uint8_t> qs(uint32_t first, int shift, uint8_t adr = 0x01) {
    vector<uint8_t> out;
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t a = first + i + 150 + shift;
        auto bcd = [](uint32_t v) { return static_cast<uint8_t>((v / 10) << 4 | v % 10); };
        uint8_t q[12] = {adr, 1, 1, 0, 0, 0, 0, bcd(a / 4500), bcd(a / 75 % 60), bcd(a % 75), 0, 0};
        out.insert(out.end(), q, q + 12);
    }
    return out;
}
} // namespace

TEST_CASE("the subchannel probe decides from four Q channels") {
    int shift = 99;
    vector<uint8_t> q = qs(16, 0);
    CHECK(LinuxCdDrive::decideShift(q.data(), 4, 16, shift));
    CHECK(shift == 0);

    q = qs(16, 2);
    CHECK(LinuxCdDrive::decideShift(q.data(), 4, 16, shift));
    CHECK(shift == 2);

    q = qs(16, -1);
    CHECK(LinuxCdDrive::decideShift(q.data(), 4, 16, shift));
    CHECK(shift == -1);

    q = qs(16, 3); // too far
    CHECK_FALSE(LinuxCdDrive::decideShift(q.data(), 4, 16, shift));

    q = qs(16, 0, 0x02); // ADR 2 (a catalogue number), not a position
    CHECK_FALSE(LinuxCdDrive::decideShift(q.data(), 4, 16, shift));

    q = qs(16, 0);
    q[12 + 9] = static_cast<uint8_t>(q[12 + 9] + 1); // one sector says another shift than the rest
    CHECK_FALSE(LinuxCdDrive::decideShift(q.data(), 4, 16, shift));

    vector<uint8_t> zeros(48, 0); // a drive that gives no subchannel
    CHECK_FALSE(LinuxCdDrive::decideShift(zeros.data(), 4, 16, shift));
}
#endif
