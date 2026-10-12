//
// The pure parts of LinuxCdDrive (not built on Windows): findDrives over a temp sysfs tree, the TOC bytes ->
// CdToc, the READ CD command, the subchannel probe's decision from given Q channels and how a sector with a
// shifted Q is read. The SG_IO calls themselves need a real drive.
//
#include "doctest/doctest.h"
#include "support/temp_dir.h"

#include "core/services/linux_cd_drive.h"

#include <string>
#include <vector>

using namespace std;

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

TEST_CASE("a shifted Q is read from another sector, the main channel stays at the sector") {
    // no shift: one read at lba, with the subchannel
    LinuxCdDrive::ReadPlan p = LinuxCdDrive::planRead(1000, 0, true);
    CHECK(p.mainLba == 1000);
    CHECK(p.mainWithSub);
    CHECK_FALSE(p.extraSubRead);

    // the Q says sector + 2: the main channel at lba without the subchannel, the sub from lba - 2
    p = LinuxCdDrive::planRead(1000, 2, true);
    CHECK(p.mainLba == 1000);
    CHECK_FALSE(p.mainWithSub);
    CHECK(p.extraSubRead);
    CHECK(p.subLba == 998);

    p = LinuxCdDrive::planRead(1000, -1, true);
    CHECK(p.mainLba == 1000);
    CHECK_FALSE(p.mainWithSub);
    CHECK(p.extraSubRead);
    CHECK(p.subLba == 1001);

    // no Q wanted: one plain read, whatever the shift
    p = LinuxCdDrive::planRead(1000, 2, false);
    CHECK(p.mainLba == 1000);
    CHECK_FALSE(p.mainWithSub);
    CHECK_FALSE(p.extraSubRead);
}
