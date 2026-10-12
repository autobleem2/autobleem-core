//
// CddaPlayer over a fake drive: four audio tracks whose every sample says which sector it came from
// (sample = lba + 1, so a silent sector - 0 - is told from every real one). Track boundaries, seek, next/prev,
// repeat, shuffle covering every track once, pause, a sector that will not read, the ring's depth.
//
#include "doctest/doctest.h"

#include "core/services/cdda_player.h"

#include <chrono>
#include <cstring>
#include <map>
#include <set>
#include <thread>
#include <vector>

using namespace std;

namespace {

const uint32_t SF = CddaPlayer::SectorFrames;

struct FakeCdda : CdDrive {
    map<uint32_t, int> failures; // lba -> reads that fail (-1: always)
    map<uint32_t, int> attempts;
    CdToc toc;

    FakeCdda() {
        // 300 + 60 + 80 + 40 sectors, a 2 s lead-in none
        toc.tracks.push_back({1, true, 0, 0});
        toc.tracks.push_back({2, true, 300, 0});
        toc.tracks.push_back({3, true, 360, 0});
        toc.tracks.push_back({4, true, 440, 0});
        toc.leadout = 480;
    }
    bool readToc(CdToc &t, string &) override {
        t = toc;
        return true;
    }
    bool readSector(uint32_t lba, bool audio, uint8_t *out, uint8_t *) override {
        if (!audio || lba >= toc.leadout)
            return false;
        {
            lock_guard<mutex> lk(mu);
            attempts[lba]++;
            auto f = failures.find(lba);
            if (f != failures.end() && f->second != 0) {
                if (f->second > 0)
                    f->second--;
                return false;
            }
        }
        for (uint32_t i = 0; i < CddaPlayer::SectorBytes; i += 2) {
            int16_t v = static_cast<int16_t>(lba + 1);
            memcpy(out + i, &v, 2);
        }
        return true;
    }
    bool hasSubchannel() const override { return false; }
    int attemptsOf(uint32_t lba) {
        lock_guard<mutex> lk(mu);
        return attempts[lba];
    }
    mutex mu;
};

// n frames of real music (waits for the reader); the first sample of every frame
vector<int16_t> pull(CddaPlayer &p, size_t n) {
    vector<int16_t> got;
    vector<int16_t> buf(2 * 600);
    auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
    while (got.size() < n && chrono::steady_clock::now() < deadline) {
        size_t want = min<size_t>(600, n - got.size());
        size_t real = p.read(buf.data(), want);
        for (size_t i = 0; i < real; i++)
            got.push_back(buf[2 * i]);
        if (real == 0) {
            if (p.state() == CddaPlayer::State::Stopped)
                break; // the disc is over
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
    return got;
}

// the sector numbers of the next n sectors (each read in whole)
vector<int> sectors(CddaPlayer &p, size_t n) {
    vector<int16_t> s = pull(p, n * SF);
    vector<int> r;
    for (size_t i = 0; i + SF <= s.size(); i += SF)
        r.push_back(s[i] - 1);
    return r;
}

bool waitFor(const function<bool()> &cond) {
    auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
    while (chrono::steady_clock::now() < deadline) {
        if (cond())
            return true;
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return cond();
}

} // namespace

TEST_CASE("CddaPlayer plays a track from its first sector and the ring fills about 3 s deep") {
    FakeCdda drive;
    CddaPlayer p(drive, drive.toc);
    CHECK(p.trackCount() == 4);
    CHECK(p.total() == 480);
    CHECK(p.state() == CddaPlayer::State::Stopped);
    CHECK(p.track() == 0);

    CHECK(p.play(1));
    CHECK(p.state() == CddaPlayer::State::Playing);
    CHECK(p.track() == 1);
    CHECK(waitFor([&] { return p.buffered() == 225; })); // 3 s
    CHECK(sectors(p, 3) == vector<int>{0, 1, 2});
    CHECK(p.position() == 2);
}

TEST_CASE("CddaPlayer crosses a track boundary without a gap and reports the new track") {
    FakeCdda drive;
    CddaPlayer p(drive, drive.toc);
    REQUIRE(p.play(2)); // 60 sectors, 300..359
    vector<int> s = sectors(p, 62);
    CHECK(s[0] == 300);
    CHECK(s[59] == 359);
    CHECK(s[60] == 360);
    CHECK(s[61] == 361);
    CHECK(p.track() == 3);
    CHECK(p.position() == 1);
    CHECK(p.trackLength(2) == 60);
    CHECK(p.trackLength(9) == 0);
}

TEST_CASE("CddaPlayer stops at the end of the disc and a data track is not playable") {
    FakeCdda drive;
    drive.toc.tracks.push_back({5, false, 480, 0});
    drive.toc.leadout = 600;
    CddaPlayer p(drive, drive.toc);
    CHECK(p.trackCount() == 4);
    CHECK_FALSE(p.play(5));
    CHECK_FALSE(p.play(0));
    CHECK_FALSE(p.play(6));
    REQUIRE(p.play(4)); // 40 sectors, then the data track must not be read
    CHECK(sectors(p, 40).size() == 40);
    CHECK(waitFor([&] {
        int16_t b[8];
        p.read(b, 4);
        return p.state() == CddaPlayer::State::Stopped;
    }));
    int16_t b[8];
    CHECK(p.read(b, 4) == 0);
    CHECK(drive.attemptsOf(480) == 0);
}

TEST_CASE("CddaPlayer seeks inside a track and clamps at its end") {
    FakeCdda drive;
    CddaPlayer p(drive, drive.toc);
    CHECK_FALSE(p.seek(5)); // nothing playing
    REQUIRE(p.play(1));
    sectors(p, 2);
    CHECK(p.seek(250));
    CHECK(sectors(p, 2) == vector<int>{250, 251});
    CHECK(p.position() == 251);
    CHECK(p.seek(100000));
    CHECK(sectors(p, 1) == vector<int>{299}); // the last sector of the track
    CHECK(p.track() == 1);
    CHECK(sectors(p, 1) == vector<int>{300});   // and then the next track, as it always does
    CHECK(p.seek(0));
    CHECK(sectors(p, 1) == vector<int>{300});
}

TEST_CASE("CddaPlayer next and prev follow the disc; prev inside the first 3 s goes back a track") {
    FakeCdda drive;
    CddaPlayer p(drive, drive.toc);
    REQUIRE(p.play(2));
    CHECK(p.next());
    CHECK(p.track() == 3);
    CHECK(sectors(p, 1) == vector<int>{360});
    CHECK(p.prev()); // 1 sector in: the track before
    CHECK(p.track() == 2);
    CHECK(sectors(p, 1) == vector<int>{300});

    REQUIRE(p.play(4));
    CHECK_FALSE(p.next()); // the last, repeat off
    CHECK(p.track() == 4);
    p.setRepeat(CddaPlayer::Repeat::Disc);
    CHECK(p.next());
    CHECK(p.track() == 1);

    // more than 3 s (225 sectors) into a track: prev is "back to its start"
    CHECK(p.seek(260));
    sectors(p, 1);
    CHECK(p.prev());
    CHECK(p.track() == 1);
    CHECK(sectors(p, 1) == vector<int>{0});
    // at the first track, close to its start: with repeat Disc the last track, else its start again
    p.setRepeat(CddaPlayer::Repeat::Off);
    CHECK(p.prev());
    CHECK(p.track() == 1);
    CHECK(p.position() == 0);
}

TEST_CASE("CddaPlayer repeat: one track, the whole disc, none") {
    SUBCASE("track") {
        FakeCdda drive;
        CddaPlayer p(drive, drive.toc);
        p.setRepeat(CddaPlayer::Repeat::Track);
        CHECK(p.repeat() == CddaPlayer::Repeat::Track);
        REQUIRE(p.play(4)); // 40 sectors
        vector<int> s = sectors(p, 85);
        CHECK(s[39] == 479);
        CHECK(s[40] == 440);
        CHECK(s[84] == 444);
        CHECK(p.track() == 4);
    }
    SUBCASE("disc") {
        FakeCdda drive;
        CddaPlayer p(drive, drive.toc);
        p.setRepeat(CddaPlayer::Repeat::Disc);
        REQUIRE(p.play(4));
        vector<int> s = sectors(p, 42);
        CHECK(s[39] == 479);
        CHECK(s[40] == 0);
        CHECK(s[41] == 1);
        CHECK(p.track() == 1);
    }
    SUBCASE("off") {
        FakeCdda drive;
        CddaPlayer p(drive, drive.toc);
        REQUIRE(p.play(4));
        CHECK(sectors(p, 41).size() == 40); // only 40 exist
    }
}

TEST_CASE("CddaPlayer shuffle plays every track once, the current one first") {
    for (uint32_t seed = 1; seed <= 5; seed++) {
        FakeCdda drive;
        CddaPlayer p(drive, drive.toc);
        p.setShuffleSeed(seed);
        p.setShuffle(true);
        CHECK(p.shuffle());
        REQUIRE(p.play(3));
        // the whole disc is 480 sectors; ask for a few more than it has
        vector<int> s = sectors(p, 480);
        REQUIRE(s.size() == 480);
        vector<int> seen;
        set<int> distinct;
        for (size_t i = 0; i < s.size(); i++) {
            int t = s[i] < 300 ? 1 : s[i] < 360 ? 2 : s[i] < 440 ? 3 : 4;
            if (seen.empty() || seen.back() != t)
                seen.push_back(t);
            distinct.insert(s[i]);
        }
        CHECK(seen.size() == 4); // each track in one piece
        CHECK(seen[0] == 3);
        CHECK(set<int>(seen.begin(), seen.end()).size() == 4);
        CHECK(distinct.size() == 480); // every sector exactly once
    }
}

TEST_CASE("CddaPlayer shuffle: next walks the same order") {
    FakeCdda drive;
    CddaPlayer p(drive, drive.toc);
    p.setShuffleSeed(7);
    p.setShuffle(true);
    REQUIRE(p.play(1));
    set<int> tracks{p.track()};
    for (int i = 0; i < 3; i++) {
        REQUIRE(p.next());
        tracks.insert(p.track());
    }
    CHECK(tracks.size() == 4);
    CHECK_FALSE(p.next());
}

TEST_CASE("CddaPlayer: a sector that will not read is tried 3 times more, then it is silence") {
    FakeCdda drive;
    drive.failures[5] = -1;
    CddaPlayer p(drive, drive.toc);
    REQUIRE(p.play(1));
    vector<int16_t> s = pull(p, 8 * SF);
    REQUIRE(s.size() == 8 * SF);
    CHECK(s[4 * SF] == 5);                // sector 4
    CHECK(s[5 * SF] == 0);                // sector 5: silence
    CHECK(s[5 * SF + SF - 1] == 0);
    CHECK(s[6 * SF] == 7);                // and the player went on
    CHECK(drive.attemptsOf(5) == 4);
    CHECK(p.badSectors() == 1);
}

TEST_CASE("CddaPlayer: a sector that reads on the third try is music") {
    FakeCdda drive;
    drive.failures[2] = 2;
    CddaPlayer p(drive, drive.toc);
    REQUIRE(p.play(1));
    vector<int16_t> s = pull(p, 4 * SF);
    CHECK(s[2 * SF] == 3);
    CHECK(drive.attemptsOf(2) == 3);
    CHECK(p.badSectors() == 0);
}

TEST_CASE("CddaPlayer pause gives silence and resumes where it was; stop forgets") {
    FakeCdda drive;
    CddaPlayer p(drive, drive.toc);
    REQUIRE(p.play(1));
    sectors(p, 3);
    p.pause();
    CHECK(p.state() == CddaPlayer::State::Paused);
    vector<int16_t> b(2 * 100, 7);
    CHECK(p.read(b.data(), 100) == 0);
    CHECK(b[0] == 0);
    CHECK(b[199] == 0);
    CHECK(p.position() == 2);
    p.resume();
    CHECK(p.state() == CddaPlayer::State::Playing);
    CHECK(sectors(p, 1) == vector<int>{3});

    p.stop();
    CHECK(p.state() == CddaPlayer::State::Stopped);
    CHECK(p.track() == 0);
    CHECK(p.position() == 0);
    CHECK(p.read(b.data(), 100) == 0);
    p.pause(); // nothing to pause
    CHECK(p.state() == CddaPlayer::State::Stopped);
    p.stop(); // twice is fine
    REQUIRE(p.play(2));
    CHECK(sectors(p, 1) == vector<int>{300});
}

TEST_CASE("CddaPlayer keeps the last frames for the analyser") {
    FakeCdda drive;
    CddaPlayer p(drive, drive.toc);
    REQUIRE(p.play(1));
    vector<int16_t> s = pull(p, SF + 10);
    REQUIRE(s.size() == SF + 10);
    vector<int16_t> last(2 * 20);
    CHECK(p.lastFrames(last.data(), 20) == 20);
    CHECK(last[0] == 1); // the last 10 frames of sector 0 (sample 1), then 10 of sector 1 (sample 2)
    CHECK(last[2 * 9] == 1);
    CHECK(last[2 * 10] == 2);
    CHECK(last[2 * 19] == 2);
}
