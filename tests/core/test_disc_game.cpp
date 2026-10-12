//
// DiscGameService: a disc in a drive as a game whose saves belong to its serial.
//
#include "doctest/doctest.h"

#include "../support/game_library_fixture.h"
#include "../support/rdb_builder.h"

#include "core/services/disc_game.h"

#include <ableem/engine/environment.h>

#include <string>

using namespace test_support;
using ableem::DirEntry;
using std::string;

namespace {

// Tekken 3 on two discs, Crash on one; Metal Gear's disc 1 is missing from the rdb
string makeRdbFile(const TempDir &tmp) {
    Bytes records;
    appendGameRecord(records, "Tekken 3 (Europe) (Disc 1)", "SCES-01237", "Europe", "Namco", 1998, 2);
    appendGameRecord(records, "Tekken 3 (Europe) (Disc 2)", "SCES-11237", "Europe", "Namco", 1998, 2);
    appendGameRecord(records, "Crash Bandicoot (USA)", "SCUS-94900", "USA", "Sony", 1996, 1);
    appendGameRecord(records, "Metal Gear Solid (USA) (Disc 2)", "SLUS-00776", "USA", "Konami", 1998, 1);
    records.push_back(0xc0);
    return writeRdb(tmp, "Sony - PlayStation.rdb", makeRdb(0, records));
}

struct Rig {
    GameLibraryFixture lib;
    ableem::MetadataLookup metadata;
    DiscGameService service;
    Rig() : metadata(lib.tmp.at("db"), makeRdbFile(lib.tmp)), service(lib.library, metadata) {}

    void addLibraryGame(int id, const string &title, const string &serial) {
        lib.addUsbGame(id, title, "MyCard");
        lib.tmp.writeFile("Games/" + title + "/" + ableem::GAME_INI,
                          "[Game]\nAutomation=1\nSerial=" + serial + "\n");
    }
};

} // namespace

TEST_CASE("a disc whose serial is in the library plays with that game's folder, card and title row") {
    Rig rig;
    rig.addLibraryGame(1, "Crash Bandicoot", "SCUS-94900");

    DiscGame disc = rig.service.make("SCUS-94900", "U", "CRASH", 3);
    CHECK(disc.sharesLibraryGame);
    CHECK(disc.physicalDisc);
    CHECK(disc.sgIndex == 3);
    CHECK(disc.game->ssFolder == rig.lib.tmp.at("Games/Crash Bandicoot/sstates"));
    CHECK(disc.game->memcard == "MyCard");
    CHECK(disc.game->gameId == 1);
}

TEST_CASE("disc 2 of a set shares the folder of the library game holding disc 1") {
    Rig rig;
    rig.addLibraryGame(7, "Tekken 3", "SCES-01237");

    CHECK(rig.service.keySerial("SCES-11237") == "SCES-01237");
    CHECK(rig.service.keySerial("SCES-01237") == "SCES-01237");

    DiscGame disc2 = rig.service.make("SCES-11237", "P", "TEKKEN3_2", 0);
    CHECK(disc2.sharesLibraryGame);
    CHECK(disc2.keySerial == "SCES-01237");
    CHECK(disc2.game->ssFolder == rig.lib.tmp.at("Games/Tekken 3/sstates"));
    CHECK(disc2.game->serial == "SCES-11237"); // its own serial is still what the disc says
}

TEST_CASE("an unknown serial gets disc-<serial> in the save states dir, created only by ensureFolder") {
    Rig rig;
    rig.addLibraryGame(1, "Crash Bandicoot", "SCUS-94900");

    DiscGame disc = rig.service.make("SLES-55555", "P", "SOMEGAME", 1);
    CHECK_FALSE(disc.sharesLibraryGame);
    string expected = ableem::Environment::getPathToSaveStatesDir() + ableem::sep + "disc-SLES-55555/";
    CHECK(disc.game->ssFolder == expected);
    CHECK_FALSE(DirEntry::exists(expected));

    REQUIRE(disc.ensureFolder());
    CHECK(DirEntry::exists(expected));
}

TEST_CASE("a new set's disc 2 uses the folder named for disc 1's serial") {
    Rig rig;
    DiscGame disc2 = rig.service.make("SCES-11237", "P", "TEKKEN3_2", 0);
    CHECK_FALSE(disc2.sharesLibraryGame);
    CHECK(disc2.game->ssFolder == DiscGameService::folderForKey("SCES-01237"));
    CHECK_FALSE(DirEntry::exists(disc2.game->ssFolder));
}

TEST_CASE("a disc 2 whose disc 1 the rdb lacks is its own set") {
    Rig rig;
    CHECK(rig.service.keySerial("SLUS-00776") == "SLUS-00776");
}

TEST_CASE("metadata comes from the lookup; the volume label is the title when nothing knows the serial") {
    Rig rig;
    DiscGame known = rig.service.make("SCUS-94900", "", "CRASH", 0);
    CHECK(known.game->title == "Crash Bandicoot");
    CHECK(known.game->publisher == "Sony");
    CHECK(known.game->year == 1996);
    CHECK(known.game->players == 1);
    CHECK(known.game->region == "US"); // from the serial when the caller had none

    DiscGame unknown = rig.service.make("SLES-55555", "P", "SOME GAME", 0);
    CHECK(unknown.game->title == "SOME GAME");
    CHECK(unknown.game->region == "P");
    CHECK(unknown.cover.empty());
}
