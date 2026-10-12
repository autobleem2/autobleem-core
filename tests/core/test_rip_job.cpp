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
#include <utility>
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
    TempDir dir("rip0");
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
    TempDir dir("rip1");
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
    TempDir dir("rip2");
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
            vector<uint8_t> want = drive.sectors[lba];
            if (lba >= 40) // the audio track is stored big-endian, as chdman does
                for (uint32_t i = 0; i + 1 < S; i += 2)
                    swap(want[i], want[i + 1]);
            CHECK(memcmp(frame, want.data(), S) == 0);
        }
    }
    chd_close(chd);
#endif
}

TEST_CASE("RipJob: nothing is left when the disc cannot be ripped") {
    TempDir dir("rip3");
    FakeCdDrive drive; // no tracks
    RipJob job;
    RipJob::Result r = job.start(drive, dir.path() + "/Game.chd", nullptr);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK_FALSE(exists(dir.path() + "/Game.chd.part"));
}

TEST_CASE("RipJob: free space helpers") {
    TempDir dir("rip4");
    CdToc toc;
    toc.leadout = 1000;
    CHECK(RipJob::neededBytes(toc) == 1000u * S);
    CHECK(RipJob::hasFreeSpace(dir.path(), 1));
    CHECK_FALSE(RipJob::hasFreeSpace(dir.path(), UINT64_MAX));
}

TEST_CASE("RipJob: the .sbi is written on request") {
    TempDir dir("rip5");
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

TEST_CASE("RipJob: the drive is read in batches that stay inside a track and the lead-out") {
    TempDir dir("rip6");
    FakeCdDrive drive;
    makeDisc(drive, 40, 33);
    RipJob job;
    RipJob::Result r = job.start(drive, dir.path() + "/Game.chd", nullptr);
    CHECK_MESSAGE(r.ok, r.error);
    REQUIRE(drive.batches.size() == 4);
    const uint32_t want[4][3] = {{0, 24, 0}, {24, 16, 0}, {40, 24, 1}, {64, 9, 1}}; // lba, count, audio
    for (size_t i = 0; i < 4; i++) {
        INFO("batch " << i);
        CHECK(drive.batches[i].lba == want[i][0]);
        CHECK(drive.batches[i].count == want[i][1]);
        CHECK(drive.batches[i].audio == (want[i][2] != 0));
    }
    CHECK(drive.singleReads <= 2); // only the look at each track's first sector
    CHECK(RipJob::BatchSectors == 24);

    // any track layout: no batch crosses a track start or the lead-out, together they cover every sector once
    FakeCdDrive odd;
    makeDisc(odd, 5, 100);
    odd.toc.tracks.push_back({3, true, 77, 0}); // a third track inside the audio one
    RipJob again;
    r = again.start(odd, dir.path() + "/Odd.chd", nullptr);
    CHECK_MESSAGE(r.ok, r.error);
    uint32_t next = 0;
    for (const FakeCdDrive::Batch &b : odd.batches) {
        CHECK(b.lba == next);
        CHECK(b.count >= 1);
        CHECK(b.count <= RipJob::BatchSectors);
        for (const CdTrack &t : odd.toc.tracks)
            CHECK_FALSE((t.start > b.lba && t.start < b.lba + b.count));
        next = b.lba + b.count;
    }
    CHECK(next == odd.toc.leadout);
}

TEST_CASE("RipJob: a failing batch is read again sector by sector with the same bad sectors as before") {
    TempDir dir("rip7");
    string files[2];
    int badSectors[2] = {0, 0};
    int64_t firstBad[2] = {0, 0};
    for (int mode = 0; mode < 2; mode++) {
        FakeCdDrive drive;
        drive.batchRead = mode == 0; // 1: the default readSectors(), which stops at the first failing sector
        makeDisc(drive, 40, 33);
        drive.failing = {17, 50, 51};
        drive.failFirst[5] = RipJob::Retries;
        drive.failFirst[6] = RipJob::Retries + 1;
        RipJob job;
        const string out = dir.path() + "/Game" + to_string(mode) + ".chd";
        RipJob::Result r = job.start(drive, out, nullptr);
        CHECK_MESSAGE(r.ok, r.error);
        badSectors[mode] = r.badSectors;
        firstBad[mode] = r.firstBadLba;
        ifstream in(out, ios::binary);
        files[mode].assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
        if (mode == 0) {
            // the batches with a bad sector (0-23 and 40-63) were read again one by one, the others were not
            CHECK(drive.singleReads >= 48);
            CHECK(drive.singleReads < 48 + 2 + 8 + 8); // plus the track probes and the retries
        }
    }
    CHECK(badSectors[0] == 4);
    CHECK(firstBad[0] == 6);
    CHECK(badSectors[1] == badSectors[0]);
    CHECK(firstBad[1] == firstBad[0]);
    CHECK(files[0] == files[1]);
    CHECK_FALSE(files[0].empty());
}

TEST_CASE("RipJob: a cancel during a rip stops within a few hunks") {
    TempDir dir("rip8");
    FakeCdDrive drive;
    makeDisc(drive, 300, 100);
    RipJob job;
    drive.onRead = [&](uint32_t lba) {
        if (lba == 100)
            job.cancel();
    };
    const string out = dir.path() + "/Game.chd";
    RipJob::Result r = job.start(drive, out, nullptr);
    CHECK(r.cancelled);
    CHECK_FALSE(exists(out));
    CHECK_FALSE(exists(out + ".part"));
    // the batch under way is finished, the next one is not started
    CHECK(drive.reads <= 100 + 2 * static_cast<int>(RipJob::BatchSectors) + 2);
}

TEST_CASE("RipJob: a cancel during the sector-by-sector fallback stops at a hunk too") {
    TempDir dir("rip9");
    FakeCdDrive drive;
    makeDisc(drive, 300, 100);
    drive.failing = {3}; // the first batch fails and is read one by one
    RipJob job;
    drive.onRead = [&](uint32_t lba) {
        if (lba == 5)
            job.cancel();
    };
    RipJob::Result r = job.start(drive, dir.path() + "/Game.chd", nullptr);
    CHECK(r.cancelled);
    CHECK(drive.batches.size() == 1);
}
