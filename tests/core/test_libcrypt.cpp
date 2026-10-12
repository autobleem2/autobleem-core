//
// LibCrypt: the 16 places read through a fake drive's recorded Q buffers, and the .sbi written from them.
//
#include "doctest/doctest.h"
#include "support/fake_cd_drive.h"

#include "core/services/libcrypt.h"

#include <string>
#include <vector>

using namespace std;

namespace {

uint8_t bcd(uint32_t v) {
    return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
}

// a Q of a data track: control/ADR 41, track 01, index 01, relative and absolute MSF, the right CRC
vector<uint8_t> goodQ(uint32_t lba) {
    vector<uint8_t> q(12, 0);
    const uint32_t f = lba + 150;
    q[0] = 0x41;
    q[1] = 0x01;
    q[2] = 0x01;
    q[3] = bcd(lba / 4500);
    q[4] = bcd(lba / 75 % 60);
    q[5] = bcd(lba % 75);
    q[7] = bcd(f / 4500);
    q[8] = bcd(f / 75 % 60);
    q[9] = bcd(f % 75);
    const uint16_t crc = DiscReader::qCrc(q.data());
    q[10] = static_cast<uint8_t>(crc >> 8);
    q[11] = static_cast<uint8_t>(crc);
    return q;
}

vector<uint8_t> badQ(uint32_t lba) {
    vector<uint8_t> q = goodQ(lba);
    q[9] ^= 0x04; // a wrong address bit; the CRC is the old one
    return q;
}

void makeDisc(FakeCdDrive &d, uint32_t sectors) {
    d.toc.tracks.push_back({1, false, 0, 0});
    d.toc.leadout = sectors;
    d.sectors.assign(sectors, vector<uint8_t>(DiscReader::SectorSize, 1));
}

} // namespace

TEST_CASE("LibCrypt: the places, MSF minus the lead-in") {
    CHECK(LibCrypt::placeMsf(0) == 14105);
    CHECK(LibCrypt::placeLba(0) == 13955);
    CHECK(LibCrypt::placeLba(15) == 16017);
    CHECK(LibCrypt::placeMsf(16) == 0);
    CHECK(LibCrypt::crcOk(goodQ(100).data()));
    CHECK_FALSE(LibCrypt::crcOk(badQ(100).data()));
}

TEST_CASE("LibCrypt: a place is a mark when both of its Q CRCs are bad") {
    FakeCdDrive drive;
    makeDisc(drive, 17000);
    for (int p : {0, 3, 15})
        for (uint32_t d : {0u, 5u}) {
            const uint32_t lba = LibCrypt::placeLba(p) + d;
            drive.qs[lba] = badQ(lba);
        }
    drive.qs[LibCrypt::placeLba(5)] = badQ(LibCrypt::placeLba(5));         // only the first of a pair
    drive.qs[LibCrypt::placeLba(7) + 5] = badQ(LibCrypt::placeLba(7) + 5); // only the second

    int places = 0;
    const vector<LibCrypt::Mark> marks = LibCrypt::scan(drive, 17000, &places);
    CHECK(places == 3);
    REQUIRE(marks.size() == 6);
    CHECK(marks[0].lba == 13955);
    CHECK(marks[1].lba == 13960);
    CHECK(marks[0].q[9] == badQ(13955)[9]);

    const string sbi = LibCrypt::sbi(marks);
    REQUIRE(sbi.size() == 4 + 6 * 14);
    CHECK(sbi.compare(0, 4, string("SBI\0", 4)) == 0);
    CHECK(static_cast<uint8_t>(sbi[4]) == 0x03); // 14105 = 03:08:05
    CHECK(static_cast<uint8_t>(sbi[5]) == 0x08);
    CHECK(static_cast<uint8_t>(sbi[6]) == 0x05);
    CHECK(sbi[7] == 1);
    CHECK(static_cast<uint8_t>(sbi[8]) == 0x41); // the Q as read
    CHECK(static_cast<uint8_t>(sbi[8 + 9]) == badQ(13955)[9]);
}

TEST_CASE("LibCrypt: a clean disc, no Q channel, a short disc and unreadable sectors give no marks") {
    FakeCdDrive drive;
    makeDisc(drive, 17000);
    int places = -1;
    CHECK(LibCrypt::scan(drive, 17000, &places).empty());
    CHECK(places == 0);

    drive.qs[13955] = badQ(13955);
    drive.qs[13960] = badQ(13960);
    drive.subchannel = false;
    CHECK(LibCrypt::scan(drive, 17000).empty());
    drive.subchannel = true;
    CHECK(LibCrypt::scan(drive, 13000).empty()); // the disc ends before the place
    drive.failing.insert(13960);
    CHECK(LibCrypt::scan(drive, 17000).empty());
}

TEST_CASE("LibCrypt: a flaky Q that reads right once is not a mark") {
    struct Flaky : FakeCdDrive {
        int n = 0;
        bool readSector(uint32_t lba, bool audio, uint8_t *data, uint8_t *q) override {
            bool ok = FakeCdDrive::readSector(lba, audio, data, q);
            if (ok && q != nullptr && lba == 13955 && ++n == 2)
                memcpy(q, goodQ(lba).data(), 12);
            return ok;
        }
    } drive;
    makeDisc(drive, 17000);
    drive.qs[13955] = badQ(13955);
    drive.qs[13960] = badQ(13960);
    CHECK(LibCrypt::scan(drive, 17000).empty());
}
