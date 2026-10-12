//
// RipJob over a fake drive: the finished CHD, cancel leaving nothing, unreadable sectors as zeros with their count,
// the retries, the drive's speed, the .sbi on request.
//
#include "doctest/doctest.h"
#include "support/fake_cd_drive.h"
#include "support/temp_dir.h"

#include "core/services/rip_job.h"

#include <fstream>
#include <string>
#include <vector>

#ifndef AB_NO_CHD
#include <libchdr/chd.h>
#endif

using namespace std;

namespace {

const uint32_t S = DiscReader::SectorSize;

// a data track of 'data' sectors (Mode 2 raw, sync and header) and an audio track of 'audio' sectors
void makeDisc(FakeCdDrive &d, uint32_t data, uint32_t audio) {
    d.toc.tracks.clear();
    d.toc.tracks.push_back({1, false, 0, 0});
    d.toc.tracks.push_back({2, true, data, 0});
    d.toc.leadout = data + audio;
    d.sectors.clear();
    for (uint32_t lba = 0; lba < data + audio; lba++) {
        vector<uint8_t> s(S);
        for (uint32_t i = 0; i < S; i++)
            s[i] = static_cast<uint8_t>(lba * 31 + i * 7 + 1); // never all zero
        if (lba < data) {
            s[0] = 0;
            for (int i = 1; i <= 10; i++)
                s[i] = 0xff;
            s[11] = 0;
            s[15] = 2;
        }
        d.sectors.push_back(s);
    }
}

bool exists(const string &path) {
    return ifstream(path).good();
}

} // namespace

TEST_CASE("RipJob: a clean disc becomes a .chd and the drive is put back to 4x") {
    TempDir dir;
    FakeCdDrive drive;
    makeDisc(drive, 40, 33);
    RipJob job;
    int calls = 0;
    uint32_t last = 0;
    const string out = dir.path() + "/Game.chd";
    RipJob::Result r = job.start(drive, out, [&](const RipJob::Progress &p) {
        calls++;
        last = p.sectorsDone;
    });
    CHECK_MESSAGE(r.ok, r.error);
    CHECK(r.sectors == 73);
    CHECK(r.badSectors == 0);
    CHECK(r.firstBadLba == -1);
    CHECK(r.file == out);
    CHECK(exists(out));
    CHECK_FALSE(exists(out + ".part"));
    CHECK(calls >= 1);
    CHECK(last == 73);
    CHECK(drive.speedCalls == 2);
    CHECK_FALSE(drive.lastSpeedMaximum);
    CHECK(RipJob::dataTrackType(drive.sectors[0].data()) == "MODE2_RAW");
}

TEST_CASE("RipJob: cancel leaves no file") {
    TempDir dir;
    FakeCdDrive drive;
    makeDisc(drive, 100, 60);
    RipJob job;
    const string out = dir.path() + "/Game.chd";
    drive.onRead = [&](uint32_t lba) {
        if (lba == 20)
            job.cancel();
    };
    RipJob::Result r = job.start(drive, out, nullptr);
    CHECK(r.cancelled);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(exists(out));
    CHECK_FALSE(exists(out + ".part"));
    CHECK_FALSE(drive.lastSpeedMaximum);
    CHECK(drive.reads < 60); // it stopped at the next hunk, not at the end
    // the job can be used again
    drive.onRead = nullptr;
    r = job.start(drive, out, nullptr);
    CHECK_MESSAGE(r.ok, r.error);
    CHECK(exists(out));
}

TEST_CASE("RipJob: unreadable sectors are zeros, counted, with the first one's LBA") {
    TempDir dir;
    FakeCdDrive drive;
    makeDisc(drive, 40, 33);
    drive.failing = {17, 50, 51};
    drive.failFirst[5] = RipJob::Retries;     // fails 3 times, the 4th read works: not bad
    drive.failFirst[6] = RipJob::Retries + 1; // fails all 4 tries: bad
    RipJob job;
    const string out = dir.path() + "/Game.chd";
    RipJob::Result r = job.start(drive, out, nullptr);
    CHECK_MESSAGE(r.ok, r.error);
    CHECK(r.badSectors == 4);
    CHECK(r.firstBadLba == 6);
#ifndef AB_NO_CHD
    chd_file *chd = nullptr;
    REQUIRE(chd_open(out.c_str(), CHD_OPEN_READ, nullptr, &chd) == CHDERR_NONE);
    const chd_header *h = chd_get_header(chd);
    vector<uint8_t> hunk(h->hunkbytes);
    for (uint32_t lba = 0; lba < 73; lba++) {
        REQUIRE(chd_read(chd, lba / 8, hunk.data()) == CHDERR_NONE);
        const uint8_t *frame = &hunk[(lba % 8) * 2448];
        const bool bad = drive.failing.count(lba) || lba == 6;
        INFO("lba " << lba);
        if (bad) {
            bool zero = true;
            for (uint32_t i = 0; i < S; i++)
                zero = zero && frame[i] == 0;
            CHECK(zero);
        } else {
            CHECK(memcmp(frame, drive.sectors[lba].data(), S) == 0);
        }
    }
    chd_close(chd);
#endif
}

TEST_CASE("RipJob: nothing is left when the disc cannot be ripped") {
    TempDir dir;
    FakeCdDrive drive; // no tracks
    RipJob job;
    RipJob::Result r = job.start(drive, dir.path() + "/Game.chd", nullptr);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK_FALSE(exists(dir.path() + "/Game.chd.part"));
}

TEST_CASE("RipJob: free space helpers") {
    TempDir dir;
    CdToc toc;
    toc.leadout = 1000;
    CHECK(RipJob::neededBytes(toc) == 1000u * S);
    CHECK(RipJob::hasFreeSpace(dir.path(), 1));
    CHECK_FALSE(RipJob::hasFreeSpace(dir.path(), UINT64_MAX));
}

TEST_CASE("RipJob: the .sbi is written on request") {
    TempDir dir;
    FakeCdDrive drive;
    makeDisc(drive, 16500, 20);
    // place 0 (LBA 13955 and 13960) has bad Q CRCs
    for (uint32_t lba : {13955u, 13960u}) {
        vector<uint8_t> q(12);
        DiscReader::expectedQ(drive.toc, lba, q.data());
        q[10] ^= 0xff;
        drive.qs[lba] = q;
    }
    RipJob job;
    job.writeSbi = true;
    const string out = dir.path() + "/Game.chd";
    RipJob::Result r = job.start(drive, out, nullptr);
    CHECK_MESSAGE(r.ok, r.error);
    CHECK(r.libcryptPlaces == 1);
    CHECK(r.sbiFile == dir.path() + "/Game.sbi");
    ifstream in(r.sbiFile, ios::binary);
    string bytes((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    CHECK(bytes.size() == 4 + 2 * 14);

    RipJob plain;
    drive.qs.clear();
    r = plain.start(drive, dir.path() + "/Other.chd", nullptr);
    CHECK(r.sbiFile.empty());
    CHECK_FALSE(exists(dir.path() + "/Other.sbi"));
}
