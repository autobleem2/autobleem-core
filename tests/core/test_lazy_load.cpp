//
// The launcher's lazy startup: MetadataLookup's deferred load, RetroArchService's background load, and the
// timing line (StartupTimer). The rule under test: a lookup that runs before its data is there answers "not yet"
// (no crash, no half-built answer, its output untouched) and the same lookup, asked again after ready(), answers.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/rdb_builder.h"
#include "../support/temp_dir.h"

#include "core/services/environment.h"
#include "core/services/retroarch.h"

#include <ableem/engine/game_metadata.h>
#include <ableem/engine/metadata_lookup.h>
#include <ableem/engine/startup_timer.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using ableem::GameMetadata;
using ableem::MetadataLookup;
using ableem::StartupTimer;
using std::string;
using std::vector;

namespace {

using namespace test_support;

string makeRdbFile(const TempDir &tmp) {
    Bytes records;
    appendGameRecord(records, "Crash Bandicoot (USA)", "SCUS-94900", "USA", "Sony Computer Entertainment.", 1996, 1);
    appendGameRecord(records, "Tekken 3 (Europe) (Disc 1)", "SCES-01237", "Europe", "Namco", 1998, 2);
    records.push_back(0xc0);
    return writeRdb(tmp, "Sony - PlayStation.rdb", makeRdb(0, records));
}

// waits (at most 10 s) for a worker to publish; a test that hangs here fails instead of stalling the suite
template <class Ready> bool waitFor(Ready ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace

TEST_CASE("StartupTimer: one grep-able line, durations from the steady clock") {
    CHECK(StartupTimer::line("rdb-read", 1234) == "startup: rdb-read 1234 ms");

    StartupTimer timer("unit-test-step");
    const long long ms = timer.stop();
    CHECK(ms >= 0);
    CHECK(ms < 5000);
    CHECK(timer.stop() == ms); // stopping twice neither logs twice nor changes the answer

    const long long first = StartupTimer::milestone("unit-test");
    const long long second = StartupTimer::milestone("unit-test");
    CHECK(second >= first); // monotonic, whatever the wall clock does
}

TEST_CASE("a deferred MetadataLookup reads nothing until asked, and answers 'not yet' meanwhile") {
    TempDir tmp("lazy-meta");
    tmp.makeSubDir("db");
    const string rdb = makeRdbFile(tmp);
    MetadataLookup lookup(tmp.at("db"), rdb, MetadataLookup::Load::Deferred);

    CHECK_FALSE(lookup.ready());
    CHECK_FALSE(lookup.hasRdb());
    CHECK_FALSE(lookup.hasAnyCovers());
    GameMetadata md;
    md.title = "untouched";
    CHECK_FALSE(lookup.findBySerial("SCES-01237", md));
    CHECK_FALSE(lookup.findByTitle("Tekken 3 (Europe) (Disc 1)", md));
    CHECK(md.title == "untouched"); // a miss before ready() leaves the caller's record alone
    CHECK_FALSE(md.valid);

    lookup.load();
    REQUIRE(lookup.ready());
    CHECK(lookup.hasRdb());
    REQUIRE(lookup.findBySerial("SCES-01237", md)); // the same question, asked again, now answers
    CHECK(md.title == "Tekken 3");
    CHECK(md.players == 2);

    lookup.load(); // once only: no second read, nothing breaks
    CHECK(lookup.ready());
}

TEST_CASE("startLoading publishes from a worker; a lookup polling meanwhile never sees a half-built rdb") {
    TempDir tmp("lazy-meta-thread");
    tmp.makeSubDir("db");
    const string rdb = makeRdbFile(tmp);
    MetadataLookup lookup(tmp.at("db"), rdb, MetadataLookup::Load::Deferred);
    lookup.startLoading();
    lookup.startLoading(); // a second call starts nothing

    int notYet = 0;
    GameMetadata md;
    const bool found = waitFor([&]() {
        if (lookup.findBySerial("SCUS-94900", md))
            return true;
        notYet++; // each miss is either "not yet" or a real miss; once ready() the game must be found
        REQUIRE_FALSE(lookup.ready());
        return false;
    });
    REQUIRE(found);
    CHECK(lookup.ready());
    CHECK(md.publisher == "Sony Computer Entertainment");
    CHECK(notYet >= 0);
}

TEST_CASE("destroying a lookup whose worker is still loading waits for it") {
    TempDir tmp("lazy-meta-destroy");
    tmp.makeSubDir("db");
    const string rdb = makeRdbFile(tmp);
    for (int i = 0; i < 20; i++) {
        MetadataLookup lookup(tmp.at("db"), rdb, MetadataLookup::Load::Deferred);
        lookup.startLoading();
    } // ~MetadataLookup joins: no thread outlives its object (a crash or a TSAN report here is the failure)
    SUCCEED();
}

TEST_CASE("sourcesPresent looks at files only - no database is opened") {
    TempDir tmp("lazy-sources");
    tmp.makeSubDir("db");
    CHECK_FALSE(MetadataLookup::sourcesPresent(tmp.at("db"), tmp.at("none.rdb")));
    tmp.writeFile("db/coversP.db", "not even a database");
    CHECK(MetadataLookup::sourcesPresent(tmp.at("db"), tmp.at("none.rdb")));
    CHECK(MetadataLookup::sourcesPresent(tmp.at("nodb"), makeRdbFile(tmp)));
}

namespace {

// a USB root with one core and a playlist of one game, the way test_retroarch.cpp lays them out
struct MiniRetroArch {
    MiniRetroArch() : tmp("lazy-ra") {
        env.setUsbRoot(tmp.path());
        env.setWorkingPath(tmp.path());
        tmp.writeFile("RetroArch/bin/info/snes9x_libretro.info",
                      "display_name = \"Nintendo - SNES (Snes9x)\"\nsupported_extensions = \"sfc\"\n"
                      "database = \"Nintendo - Super Nintendo Entertainment System\"\n");
        tmp.writeFile("RetroArch/bin/cores/snes9x_libretro.so", "core");
        tmp.writeFile("RetroArch/roms/snes/Chrono Trigger.sfc", "rom");
        tmp.writeFile("RetroArch/bin/playlists/Nintendo - Super Nintendo Entertainment System.lpl",
                      "{\n  \"version\": \"1.0\",\n  \"items\": [\n    {\n"
                      "      \"path\": \"/media/RetroArch/roms/snes/Chrono Trigger.sfc\",\n"
                      "      \"label\": \"Chrono Trigger\",\n"
                      "      \"core_path\": \"DETECT\",\n      \"core_name\": \"DETECT\",\n"
                      "      \"crc32\": \"00000000|crc\",\n"
                      "      \"db_name\": \"Nintendo - Super Nintendo Entertainment System.lpl\"\n    }\n  ]\n}\n");
    }
    EnvFixture env;
    TempDir tmp;
    RetroArchService service;
};

} // namespace

TEST_CASE("RetroArchService: tryPlaylistNames says 'not yet' until the background load has published") {
    MiniRetroArch ra;
    vector<string> names{"kept"};

    CHECK_FALSE(ra.service.ready());
    CHECK_FALSE(ra.service.tryPlaylistNames(names)); // nothing started: not yet, and it does not start a read itself
    CHECK(names == vector<string>{"kept"});

    ra.service.startBackgroundLoad();
    ra.service.startBackgroundLoad(); // once only
    REQUIRE(waitFor([&]() { return ra.service.ready(); }));
    REQUIRE(ra.service.tryPlaylistNames(names));
    REQUIRE(names.size() == 1);
    CHECK(names[0] == "Nintendo - Super Nintendo Entertainment System");
    CHECK(ra.service.gameCount(names[0]) == 1);
}

TEST_CASE("RetroArchService: a question asked while the worker runs waits and gets the full answer") {
    for (int round = 0; round < 10; round++) {
        MiniRetroArch ra;
        ra.service.startBackgroundLoad();
        // no polling: straight into the call that needs the data - it joins the worker, never sees half of it
        const PsGames games = ra.service.gamesInPlaylist("Nintendo - Super Nintendo Entertainment System");
        REQUIRE(games.size() == 1);
        CHECK(games[0]->title == "Chrono Trigger");
        CHECK(ra.service.ready());
    }
}

TEST_CASE("RetroArchService: destroyed with the worker still running, and reloading after the load, are safe") {
    {
        MiniRetroArch ra;
        ra.service.startBackgroundLoad();
    } // ~RetroArchService joins

    MiniRetroArch ra;
    ra.service.startBackgroundLoad();
    ra.service.reloadPlaylists(); // joins first, then reads again
    vector<string> names;
    REQUIRE(ra.service.tryPlaylistNames(names));
    CHECK(names.size() == 1);
}

TEST_CASE("RetroArchService: without a RetroArch tree the background load publishes an empty answer") {
    EnvFixture env;
    TempDir tmp("lazy-ra-none");
    env.setUsbRoot(tmp.path());
    RetroArchService service;
    service.startBackgroundLoad();
    REQUIRE(waitFor([&]() { return service.ready(); }));
    vector<string> names{"stale"};
    REQUIRE(service.tryPlaylistNames(names));
    CHECK(names.empty()); // "loaded, nothing there" is not "not yet": the caller must not retry forever
}
